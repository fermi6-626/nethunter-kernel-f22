/*
 *
 * Zinitix bt532 touchscreen driver
 *
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 */


#undef TSP_VERBOSE_DEBUG

#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/spu-verify.h>

#include <linux/input/mt.h>
#include <linux/sec_sysfs.h>
#include <linux/input/sec_cmd.h>
#include <linux/input/sec_tclm_v2.h>
#include <linux/of_gpio.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_BATTERY_SAMSUNG
#include <linux/sec_batt.h>
#endif
#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/common/muic.h>
#include <linux/muic/common/muic_notifier.h>
#endif
#if defined(CONFIG_VBUS_NOTIFIER)
#include <linux/vbus_notifier.h>
#endif

#include "zinitix_ts.h"

#define CONFIG_INPUT_ENABLED
#define SEC_FACTORY_TEST

#define NOT_SUPPORTED_TOUCH_DUMMY_KEY

#define GLOVE_MODE

#define MAX_FW_PATH 255

#define SPU_FW_SIGNED

#define TSP_PATH_EXTERNAL_FW		"/sdcard/Firmware/TSP/tsp.bin"
#define TSP_PATH_EXTERNAL_FW_SIGNED	"/sdcard/Firmware/TSP/tsp_signed.bin"
#define TSP_PATH_SPU_FW_SIGNED		"/spu/TSP/ffu_tsp.bin"

#define TSP_TYPE_BUILTIN_FW			0
#define TSP_TYPE_EXTERNAL_FW		1
#define TSP_TYPE_EXTERNAL_FW_SIGNED	2
#define TSP_TYPE_SPU_FW_SIGNED		3

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
#include <linux/t-base-tui.h>
#endif

#ifdef CONFIG_SAMSUNG_TUI
#include "stui_inf.h"
#endif

extern char *saved_command_line;
static bool bypass_mode = false;

#define ZINITIX_DEBUG				0
#define PDIFF_DEBUG					1
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
#define USE_MISC_DEVICE
#endif

/* added header file */

#define TOUCH_POINT_MODE			0

#ifdef CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX_WISDOM
#define MAX_SUPPORTED_FINGER_NUM	10 /* max 10 */
#else
#define MAX_SUPPORTED_FINGER_NUM	5 /* max 10 */
#endif

#ifdef NOT_SUPPORTED_TOUCH_DUMMY_KEY
#define MAX_SUPPORTED_BUTTON_NUM	2 /* max 8 */
#define SUPPORTED_BUTTON_NUM		2
#else
#define MAX_SUPPORTED_BUTTON_NUM	6 /* max 8 */
#define SUPPORTED_BUTTON_NUM		2
#endif

/* Upgrade Method*/
#define TOUCH_ONESHOT_UPGRADE		1
/* if you use isp mode, you must add i2c device :
name = "zinitix_isp" , addr 0x50*/

/* resolution offset */
#define ABS_PT_OFFSET				(-1)

#define TOUCH_FORCE_UPGRADE		1
#define USE_CHECKSUM			1
#define CHECK_HWID			0

#define CHIP_OFF_DELAY			50 /*ms*/
#define CHIP_ON_DELAY			50 /*ms*/
#define FIRMWARE_ON_DELAY		150 /*ms*/

#define DELAY_FOR_SIGNAL_DELAY		30 /*us*/
#define DELAY_FOR_TRANSCATION		50
#define DELAY_FOR_POST_TRANSCATION	10

#define CMD_RESULT_WORD_LEN	10

enum power_control {
	POWER_OFF,
	POWER_ON,
	POWER_ON_SEQUENCE,
};

/* Key Enum */
enum key_event {
	ICON_BUTTON_UNCHANGE,
	ICON_BUTTON_DOWN,
	ICON_BUTTON_UP,
};

/* ESD Protection */
/*second : if 0, no use. if you have to use, 3 is recommended*/
#define ESD_TIMER_INTERVAL			2
#define SCAN_RATE_HZ				1000
#define CHECK_ESD_TIMER				4

/*Test Mode (Monitoring Raw Data) */
#define TSP_INIT_TEST_RATIO  100

#define	SEC_MUTUAL_AMP_V_SEL	0x0232

#define	SEC_DND_N_COUNT			11
#define	SEC_DND_U_COUNT			16
#define	SEC_DND_FREQUENCY		139

#define	SEC_HFDND_N_COUNT		11
#define	SEC_HFDND_U_COUNT		16
#define	SEC_HFDND_FREQUENCY		104

#define	SEC_SX_AMP_V_SEL		0x0434
#define	SEC_SX_SUB_V_SEL		0x0055
#define	SEC_SY_AMP_V_SEL		0x0232
#define	SEC_SY_SUB_V_SEL		0x0022
#define	SEC_SHORT_N_COUNT		2
#define	SEC_SHORT_U_COUNT		1

#define SEC_SY_SAT_FREQUENCY	200
#define SEC_SY_SAT_N_COUNT		9
#define SEC_SY_SAT_U_COUNT		9
#define SEC_SY_SAT_RS0_TIME		0x00FF
#define SEC_SY_SAT_RBG_SEL		0x0404
#define SEC_SY_SAT_AMP_V_SEL	0x0434
#define SEC_SY_SAT_SUB_V_SEL	0x0044

#define SEC_SY_SAT2_FREQUENCY	200
#define SEC_SY_SAT2_N_COUNT		9
#define SEC_SY_SAT2_U_COUNT		3
#define SEC_SY_SAT2_RS0_TIME	0x00FF
#define SEC_SY_SAT2_RBG_SEL		0x0404
#define SEC_SY_SAT2_AMP_V_SEL	0x0434
#define SEC_SY_SAT2_SUB_V_SEL	0x0011

#define MAX_RAW_DATA_SZ				792 /* 36x22 */
#define MAX_TRAW_DATA_SZ	\
	(MAX_RAW_DATA_SZ + 4*MAX_SUPPORTED_FINGER_NUM + 2)

#define RAWDATA_DELAY_FOR_HOST		10000

struct raw_ioctl {
	u32 sz;
	u32 buf;
};

struct reg_ioctl {
	u32 addr;
	u32 val;
};

#define TOUCH_SEC_MODE				48
#define TOUCH_REF_MODE				10
#define TOUCH_NORMAL_MODE			5
#define TOUCH_DELTA_MODE			3
//#define TOUCH_SDND_MODE				6
#define TOUCH_RAW_MODE				7
#define TOUCH_REFERENCE_MODE			8
#define TOUCH_DND_MODE				11
#define TOUCH_HFDND_MODE			12
#define TOUCH_TXSHORT_MODE			13
#define TOUCH_RXSHORT_MODE			14
#define TOUCH_CHANNEL_TEST_MODE			14
#define TOUCH_JITTER_MODE			15
#define TOUCH_SELF_DND_MODE			17
#define TOUCH_SENTIVITY_MEASUREMENT_MODE	21
#define TOUCH_CHARGE_PUMP_MODE			25
#define TOUCH_REF_ABNORMAL_TEST_MODE		33
#define DEF_RAW_SELF_SSR_DATA_MODE		39	/* SELF SATURATION RX */
#define DEF_RAW_SELF_SFR_UNIT_DATA_MODE		40

#define TOUCH_SENTIVITY_MEASUREMENT_COUNT	9

/*  Other Things */
#define INIT_RETRY_CNT				3
#define I2C_SUCCESS					0
#define I2C_FAIL					1

/*---------------------------------------------------------------------*/

/* chip code */
#define BT43X_CHIP_CODE		0xE200
#define BT53X_CHIP_CODE		0xF400
#define ZT7548_CHIP_CODE	0xE548
#define ZT7538_CHIP_CODE	0xE538
#define ZT7532_CHIP_CODE	0xE532
#define ZT7554_CHIP_CODE	0xE700

/* Register Map*/
#define BT532_SWRESET_CMD					0x0000
#define BT532_WAKEUP_CMD					0x0001

#define BT532_IDLE_CMD						0x0004
#define BT532_SLEEP_CMD						0x0005

#define BT532_CLEAR_INT_STATUS_CMD			0x0003
#define BT532_CALIBRATE_CMD					0x0006
#define BT532_SAVE_STATUS_CMD				0x0007
#define BT532_SAVE_CALIBRATION_CMD			0x0008
#define BT532_RECALL_FACTORY_CMD			0x000f

#define BT532_THRESHOLD						0x0020

#define BT532_DEBUG_REG						0x0115

#define BT532_TOUCH_MODE					0x0010
#define BT532_CHIP_REVISION					0x0011
#define BT532_FIRMWARE_VERSION				0x0012

#define BT532_MINOR_FW_VERSION				0x0121

#define BT532_VENDOR_ID						0x001C
#define BT532_HW_ID							0x0014

#define BT532_DATA_VERSION_REG				0x0013
#define BT532_SUPPORTED_FINGER_NUM			0x0015
#define BT532_EEPROM_INFO					0x0018
#define BT532_INITIAL_TOUCH_MODE			0x0019

#define BT532_TOTAL_NUMBER_OF_X				0x0061
#define BT532_TOTAL_NUMBER_OF_Y				0x0060

#define BT532_CONNECTION_CHECK_REG			0x0062

#define BT532_DELAY_RAW_FOR_HOST			0x007f

#define BT532_BUTTON_SUPPORTED_NUM			0x00B0
#define BT532_BUTTON_SENSITIVITY			0x00B2
#define BT532_DUMMY_BUTTON_SENSITIVITY		0X00C8

#define BT532_X_RESOLUTION					0x00C0
#define BT532_Y_RESOLUTION					0x00C1

#define ZT75XX_CALL_AOT_REG				0x00D3

#define ZT75XX_STATUS_REG					0x0080

#define BT532_POINT_STATUS_REG				0x0200
#define BT532_POINT_STATUS_REG1				0x0201
#define BT532_ICON_STATUS_REG				0x00AA

#define ZT75XX_SET_AOD_X_REG				0x00AB
#define ZT75XX_SET_AOD_Y_REG				0x00AC
#define ZT75XX_SET_AOD_W_REG				0x00AD
#define ZT75XX_SET_AOD_H_REG				0x00AE
#define ZT75XX_LPM_MODE_REG				0x00AF

#define ZT75XX_GET_AOD_X_REG				0x0191
#define ZT75XX_GET_AOD_Y_REG				0x0192

#define BT532_DND_SHIFT_VALUE	0x012B
#define BT532_AFE_FREQUENCY					0x0100
#define BT532_DND_N_COUNT					0x0122
#define BT532_DND_U_COUNT					0x0135

#define BT532_RAWDATA_REG					0x0200

#define BT532_INT_ENABLE_FLAG				0x00f0
#define BT532_PERIODICAL_INTERRUPT_INTERVAL	0x00f1
#define BT532_BTN_WIDTH						0x0316
#define BT532_REAL_WIDTH					0x03A6

#define BT532_CHECKSUM_RESULT				0x012c

#define BT532_INIT_FLASH					0x01d0
#define BT532_WRITE_FLASH					0x01d1
#define BT532_READ_FLASH					0x01d2

#define ZINITIX_INTERNAL_FLAG_03		0x011f

#define BT532_OPTIONAL_SETTING				0x0116
#define BT532_COVER_CONTROL_REG			0x023E

#define ZT75XX_REJECT_ZONE_AREA			0x01AD

#define	ZT75XX_EDGE_LANDSCAPE_MODE					0x0038
#define	ZT75XX_EDGE_REJECT_PORT_SIDE_UP_DOWN_DIV	0x0039
#define	ZT75XX_EDGE_REJECT_PORT_SIDE_UP_WIDTH		0x003A
#define	ZT75XX_EDGE_REJECT_PORT_SIDE_DOWN_WIDTH		0x003E
#define	ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_SEL		0x003F
#define	ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_START	0x0040
#define	ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_END		0x0041
//#define	ZT75XX_EDGE_GRIP_PORT_TOP_BOT_WIDTH			0x0042
#define	ZT75XX_EDGE_GRIP_PORT_SIDE_WIDTH			0x0045
#define	ZT75XX_EDGE_REJECT_LAND_SIDE_WIDTH			0x0046
#define	ZT75XX_EDGE_REJECT_LAND_TOP_BOT_WIDTH		0x0047
#define	ZT75XX_EDGE_GRIP_LAND_SIDE_WIDTH			0x0048
#define	ZT75XX_EDGE_GRIP_LAND_TOP_BOT_WIDTH			0x0049

enum grip_write_mode {
	G_NONE				= 0,
	G_SET_EDGE_HANDLER		= 1,
	G_SET_EDGE_ZONE			= 2,
	G_SET_NORMAL_MODE		= 4,
	G_SET_LANDSCAPE_MODE	= 8,
	G_CLR_LANDSCAPE_MODE	= 16,
};
enum grip_set_data {
	ONLY_EDGE_HANDLER		= 0,
	GRIP_ALL_DATA			= 1,
};

#define ZT75XX_RESOLUTION_EXPANDER			0x0186
#define ZT75XX_MUTUAL_AMP_V_SEL			0x02F9
#define ZT75XX_SX_AMP_V_SEL				0x02DF
#define ZT75XX_SX_SUB_V_SEL				0x02E0
#define ZT75XX_SY_AMP_V_SEL				0x02EC
#define ZT75XX_SY_SUB_V_SEL				0x02ED
#define ZT75XX_CHECKSUM					0x03DF
#define ZT75XX_JITTER_SAMPLING_CNT			0x001F

#define ZT75XX_SY_SAT_FREQUENCY			0x03E0
#define ZT75XX_SY_SAT_N_COUNT			0x03E1
#define ZT75XX_SY_SAT_U_COUNT			0x03E2
#define ZT75XX_SY_SAT_RS0_TIME			0x03E3
#define ZT75XX_SY_SAT_RBG_SEL			0x03E4
#define ZT75XX_SY_SAT_AMP_V_SEL			0x03E5
#define ZT75XX_SY_SAT_SUB_V_SEL			0x03E6

#define ZT75XX_SY_SAT2_FREQUENCY		0x03E7
#define ZT75XX_SY_SAT2_N_COUNT			0x03E8
#define ZT75XX_SY_SAT2_U_COUNT			0x03E9
#define ZT75XX_SY_SAT2_RS0_TIME			0x03EA
#define ZT75XX_SY_SAT2_RBG_SEL			0x03EB
#define ZT75XX_SY_SAT2_AMP_V_SEL		0x03EC
#define ZT75XX_SY_SAT2_SUB_V_SEL		0x03ED

#define ZT75XX_PROXIMITY_DATA			0x030D
#define ZT75XX_PROXIMITY_DETECT			0x0024
#define ZT75XX_PROXIMITY_THRESHOLD_X	0x023F
#define ZT75XX_PROXIMITY_THRESHOLD_Y	0x02D5

#define ZT75XX_POCKET_DETECT			0x0037

#define REG_FOD_AREA_STR_X				0x013B
#define REG_FOD_AREA_STR_Y				0x013C
#define REG_FOD_AREA_END_X				0x013E
#define REG_FOD_AREA_END_Y				0x013F

#define REG_FOD_MODE_SET				0x0142

#define REG_FOD_MODE_VI_DATA				0x0143
#define REG_FOD_MODE_VI_DATA_LEN			0x0144
#define REG_FOD_MODE_VI_DATA_CH				0x0145

#define FOD_VI_DATA_LENGTH			17

/* Interrupt & status register flag bit
-------------------------------------------------
*/
#define BIT_PT_CNT_CHANGE	0
#define BIT_DOWN		1
#define BIT_MOVE		2
#define BIT_UP			3
#define BIT_PALM		4
#define BIT_PALM_REJECT		5
#define BIT_GESTURE		6
#define RESERVED_1		7
#define BIT_WEIGHT_CHANGE	8
#define BIT_POCKET_MODE		8
#define BIT_PT_NO_CHANGE	9
#define BIT_REJECT		10
#define BIT_PT_EXIST		11
#define BIT_PROXIMITY		12
#define BIT_MUST_ZERO		13
#define BIT_DEBUG		14
#define BIT_ICON_EVENT		15

/* button */
#define BIT_O_ICON0_DOWN	0
#define BIT_O_ICON1_DOWN	1
#define BIT_O_ICON2_DOWN	2
#define BIT_O_ICON3_DOWN	3
#define BIT_O_ICON4_DOWN	4
#define BIT_O_ICON5_DOWN	5
#define BIT_O_ICON6_DOWN	6
#define BIT_O_ICON7_DOWN	7

#define BIT_O_ICON0_UP		8
#define BIT_O_ICON1_UP		9
#define BIT_O_ICON2_UP		10
#define BIT_O_ICON3_UP		11
#define BIT_O_ICON4_UP		12
#define BIT_O_ICON5_UP		13
#define BIT_O_ICON6_UP		14
#define BIT_O_ICON7_UP		15


#define SUB_BIT_EXIST		0
#define SUB_BIT_DOWN		1
#define SUB_BIT_MOVE		2
#define SUB_BIT_UP		3
#define SUB_BIT_UPDATE		4
#define SUB_BIT_WAIT		5

/* BT532_DEBUG_REG */
#define DEF_DEVICE_STATUS_NPM			0
#define DEF_DEVICE_STATUS_WALLET_COVER_MODE	1
#define DEF_DEVICE_STATUS_NOISE_MODE		2
#define DEF_DEVICE_STATUS_WATER_MODE		3
#define DEF_DEVICE_STATUS_LPM__MODE		4
#define BIT_GLOVE_TOUCH				5
#define DEF_DEVICE_STATUS_PALM_DETECT		10
#define DEF_DEVICE_STATUS_SVIEW_MODE		11

/* BT532_BT532_COVER_CONTROL_REG */
#define WALLET_COVER_CLOSE	0x0000
#define VIEW_COVER_CLOSE	0x0100
#define COVER_OPEN			0x0200
#define LED_COVER_CLOSE		0x0700
#define CLEAR_COVER_CLOSE	0x0800
#define S_VIEW_COVER_CLOSE	0x1000

enum zt_cover_id {
	ZT_FLIP_WALLET = 0,
	ZT_VIEW_COVER,
	ZT_COVER_NOTHING1,
	ZT_VIEW_WIRELESS,
	ZT_COVER_NOTHING2,
	ZT_CHARGER_COVER,
	ZT_VIEW_WALLET,
	ZT_LED_COVER,
	ZT_CLEAR_FLIP_COVER,
	ZT_QWERTY_KEYBOARD_EUR,
	ZT_QWERTY_KEYBOARD_KOR,
	ZT_NEON_COVER,
	ZT_S_VIEW_COVER = 16,
	ZT_MONTBLANC_COVER = 100,
};

#define zinitix_bit_set(val, n)		((val) &= ~(1<<(n)), (val) |= (1<<(n)))
#define zinitix_bit_clr(val, n)		((val) &= ~(1<<(n)))
#define zinitix_bit_test(val, n)	((val) & (1<<(n)))
#define zinitix_swap_v(a, b, t)		((t) = (a), (a) = (b), (b) = (t))
#define zinitix_swap_16(s)			(((((s) & 0xff) << 8) | (((s) >> 8) & 0xff)))

/* REG_USB_STATUS : optional setting from AP */
#define DEF_OPTIONAL_MODE_USB_DETECT_BIT		0
#define	DEF_OPTIONAL_MODE_SVIEW_DETECT_BIT		1
#define DEF_OPTIONAL_MODE_SENSITIVE_BIT		2
#define DEF_OPTIONAL_MODE_EDGE_SELECT			3
#define	DEF_OPTIONAL_MODE_DUO_TOUCH		4
#define DEF_OPTIONAL_MODE_TOUCHABLE_AREA		5
#define DEF_OPTIONAL_MODE_EAR_DETECT		6
#define DEF_OPTIONAL_MODE_EAR_DETECT_MUTUAL		7
#define DEF_OPTIONAL_MODE_POCKET_MODE		8

/* end header file */

#define DEF_MIS_CAL_SPEC_MIN 40
#define DEF_MIS_CAL_SPEC_MAX 160
#define DEF_MIS_CAL_SPEC_MID 100

#define BIT_EVENT_SPAY	1
#define BIT_EVENT_AOD	2
#define BIT_EVENT_SINGLE_TAP   3
#define BIT_EVENT_AOT	5
#define BIT_EVENT_FOD   6

typedef enum {
	SPONGE_EVENT_TYPE_SPAY			= 0x04,
	SPONGE_EVENT_TYPE_SINGLE_TAP		= 0x08,
	SPONGE_EVENT_TYPE_AOD_PRESS		= 0x09,
	SPONGE_EVENT_TYPE_AOD_LONGPRESS		= 0x0A,
	SPONGE_EVENT_TYPE_AOD_DOUBLETAB		= 0x0B,
	SPONGE_EVENT_TYPE_FOD_PRESS			= 0x0F,
	SPONGE_EVENT_TYPE_FOD_RELEASE		= 0x10,
	SPONGE_EVENT_TYPE_FOD_OUT			= 0x11,
} SPONGE_EVENT_TYPE;

#ifdef SEC_FACTORY_TEST
/* Touch Screen */
#define TSP_CMD_STR_LEN			32
#define TSP_CMD_RESULT_STR_LEN		3240	//30*18*6
#define TSP_CMD_PARAM_NUM		8
#define TSP_CMD_X_NUM			30
#define TSP_CMD_Y_NUM			18
#define TSP_CMD_NODE_NUM		(TSP_CMD_Y_NUM * TSP_CMD_X_NUM)
#define tostring(x) #x

struct tsp_raw_data {
	s16 cnd_data[TSP_CMD_NODE_NUM];
	s16 dnd_data[TSP_CMD_NODE_NUM];
	s16 hfdnd_data[TSP_CMD_NODE_NUM];
	s16 delta_data[TSP_CMD_NODE_NUM];
	s16 vgap_data[TSP_CMD_NODE_NUM];
	s16 hgap_data[TSP_CMD_NODE_NUM];
	s16 rxshort_data[TSP_CMD_NODE_NUM];
	s16 txshort_data[TSP_CMD_NODE_NUM];
	s16 selfdnd_data[TSP_CMD_NODE_NUM];
	u16 ssr_data[TSP_CMD_NODE_NUM];
	s16 self_sat_dnd_data[TSP_CMD_NODE_NUM];
	s16 self_vgap_data[TSP_CMD_NODE_NUM];
	s16 self_hgap_data[TSP_CMD_NODE_NUM];
	s16 jitter_data[TSP_CMD_NODE_NUM];
	s16 reference_data[TSP_CMD_NODE_NUM];
	s16 reference_data_abnormal[TSP_CMD_NODE_NUM];
	s16 charge_pump_data[TSP_CMD_NODE_NUM];
	u16 channel_test_data[5];
};

/* ----------------------------------------
 * write 0xE4 [ 11 | 10 | 01 | 00 ]
 * MSB <-------------------> LSB
 * read 0xE4
 * mapping sequnce : LSB -> MSB
 * struct sec_ts_test_result {
 * * assy : front + OCTA assay
 * * module : only OCTA
 *	 union {
 *		 struct {
 *			 u8 assy_count:2;	-> 00
 *			 u8 assy_result:2;	-> 01
 *			 u8 module_count:2;	-> 10
 *			 u8 module_result:2;	-> 11
 *		 } __attribute__ ((packed));
 *		 unsigned char data[1];
 *	 };
 *};
 * ----------------------------------------
 */
struct ts_test_result {
	union {
		struct {
			u8 assy_count:2;
			u8 assy_result:2;
			u8 module_count:2;
			u8 module_result:2;
		} __attribute__ ((packed));
		unsigned char data[1];
	};
};
#define TEST_OCTA_MODULE	1
#define TEST_OCTA_ASSAY		2

#define TEST_OCTA_NONE		0
#define TEST_OCTA_FAIL		1
#define TEST_OCTA_PASS		2

#endif /* SEC_FACTORY_TEST */

#define TSP_NORMAL_EVENT_MSG 1
static int m_ts_debug_mode = ZINITIX_DEBUG;
struct tsp_callbacks {
	void (*inform_charger)(struct tsp_callbacks *tsp_cb, bool mode);
};

static bool g_ta_connected =0;
typedef union {
	u16 optional_mode;
	struct select_mode {
		u16 flag;
	} select_mode;
} zt7538_setting;

zt7538_setting m_optional_mode;
zt7538_setting m_prev_optional_mode;

#if ESD_TIMER_INTERVAL
static struct workqueue_struct *esd_tmr_workqueue;
#endif

#define TOUCH_V_FLIP	0x01
#define TOUCH_H_FLIP	0x02
#define TOUCH_XY_SWAP	0x04

struct capa_info {
	u16	vendor_id;
	u16	ic_revision;
	u16	fw_version;
	u16	fw_minor_version;
	u16	reg_data_version;
	u16	threshold;
	u16	key_threshold;
	u16	dummy_threshold;
	u32	ic_fw_size;
	u32	MaxX;
	u32	MaxY;
	u8	gesture_support;
	u16	multi_fingers;
	u16	button_num;
	u16	ic_int_mask;
	u16	x_node_num;
	u16	y_node_num;
	u16	total_node_num;
	u16	hw_id;
	u16	afe_frequency;
	u16	shift_value;
	u16	mutual_amp_v_sel;
	u16	N_cnt;
	u16	u_cnt;
	u16	is_zmt200;
	u16 sx_amp_v_sel;
	u16 sx_sub_v_sel;
	u16 sy_amp_v_sel;
	u16 sy_sub_v_sel;
	u16 current_touch_mode;
};

enum work_state {
	NOTHING = 0,
	NORMAL,
	ESD_TIMER,
	EALRY_SUSPEND,
	SUSPEND,
	RESUME,
	LATE_RESUME,
	UPGRADE,
	REMOVE,
	SET_MODE,
	HW_CALIBRAION,
	RAW_DATA,
	PROBE,
	SLEEP_MODE_IN,
	SLEEP_MODE_OUT,
};

enum {
	BUILT_IN = 0,
	UMS,
	REQ_FW,
	SPU,
};

struct bt532_ts_info {
	struct i2c_client				*client;
	struct input_dev				*input_dev;
	struct input_dev				*input_dev_proximity;
	struct bt532_ts_platform_data	*pdata;
	char							phys[32];
	/*struct task_struct				*task;*/
	/*wait_queue_head_t				wait;*/

	/*struct semaphore				update_lock;*/
	/*u32								i2c_dev_addr;*/
	struct capa_info				cap_info;
	struct point_info				touch_info[MAX_SUPPORTED_FINGER_NUM];
	struct ts_coordinate				cur_coord[MAX_SUPPORTED_FINGER_NUM];
	struct ts_coordinate				old_coord[MAX_SUPPORTED_FINGER_NUM];
	unsigned char *fw_data;
	u16								icon_event_reg;
	u16								prev_icon_event;
	/*u16								event_type;*/
	int								irq;
	u8								button[MAX_SUPPORTED_BUTTON_NUM];
	u8								work_state;
	struct semaphore				work_lock;
	u8 finger_cnt1;
	unsigned int move_count[MAX_SUPPORTED_FINGER_NUM];
	struct mutex					set_reg_lock;
	struct mutex					modechange;

	/*u16								debug_reg[8];*/ /* for debug */
	void (*register_cb)(void *);
	struct tsp_callbacks callbacks;

#if ESD_TIMER_INTERVAL
	struct work_struct				tmr_work;
	struct timer_list				esd_timeout_tmr;
	struct timer_list				*p_esd_timeout_tmr;
	spinlock_t	lock;
#endif
	struct semaphore				raw_data_lock;
	u16								touch_mode;
	s16								cur_data[MAX_TRAW_DATA_SZ];
	s16								sensitivity_data[TOUCH_SENTIVITY_MEASUREMENT_COUNT];
	u8								update;

#ifdef SEC_FACTORY_TEST
	struct tsp_raw_data				*raw_data;
	struct sec_cmd_data				sec;
#endif

	struct delayed_work work_read_info;
	bool info_work_done;

	struct delayed_work ghost_check;
	u8 tsp_dump_lock;

	struct completion resume_done;

	struct ts_test_result	test_result;

	s16 Gap_max_x;
	s16 Gap_max_y;
	s16 Gap_max_val;
	s16 Gap_min_x;
	s16 Gap_min_y;
	s16 Gap_min_val;
	s16 Gap_Gap_val;
	s16 Gap_node_num;

	struct pinctrl *pinctrl;
	bool tsp_pwr_enabled;
#ifdef CONFIG_VBUS_NOTIFIER
	struct notifier_block vbus_nb;
#endif
	u8 cover_type;
	bool flip_enable;
	bool spay_enable;
	bool fod_enable;
	bool fod_lp_mode;
	bool singletap_enable;
	bool aod_enable;
	bool aot_enable;
	bool sleep_mode;
	bool glove_touch;
	u8 lpm_mode;
	int enabled;

	u16 fod_mode_set;
	u8 fod_info_vi_trx[2];
	u16 fod_info_vi_data_len;

	unsigned int scrub_id;
	unsigned int scrub_x;
	unsigned int scrub_y;

	u8 grip_edgehandler_direction;
	int grip_edgehandler_start_y;
	int grip_edgehandler_end_y;
	u16 grip_edge_range;
	u8 grip_deadzone_up_x;
	u8 grip_deadzone_dn_x;
	int grip_deadzone_y;
	u8 grip_landscape_mode;
	int grip_landscape_edge;
	u16 grip_landscape_deadzone;
	u16 grip_landscape_top_deadzone;
	u16 grip_landscape_bottom_deadzone;
	u16 grip_landscape_top_gripzone;
	u16 grip_landscape_bottom_gripzone;

	u8 check_multi;
	unsigned int multi_count;
	unsigned int wet_count;
	u8 touched_finger_num;
	struct delayed_work work_print_info;
	int print_info_cnt_open;
	int print_info_cnt_release;
	unsigned int comm_err_count;
	u16 pressed_x[MAX_SUPPORTED_FINGER_NUM];
	u16 pressed_y[MAX_SUPPORTED_FINGER_NUM];
	long prox_power_off;

	u32 palm_flag;

	bool ed_enable;
	int pocket_enable;
	u16 hover_event; /* keystring for protos */
	u16 store_reg_data;

	u8 ito_test[4];
	struct sec_tclm_data *tdata;
};
/* Dummy touchkey code */
#define KEY_DUMMY_HOME1	249
#define KEY_DUMMY_HOME2	250
#define KEY_DUMMY_MENU	251
#define KEY_DUMMY_HOME	252
#define KEY_DUMMY_BACK	253
/*<= you must set key button mapping*/
#ifdef NOT_SUPPORTED_TOUCH_DUMMY_KEY
u32 BUTTON_MAPPING_KEY[MAX_SUPPORTED_BUTTON_NUM] = {
	KEY_RECENT,KEY_BACK};
#else
u32 BUTTON_MAPPING_KEY[MAX_SUPPORTED_BUTTON_NUM] = {
	KEY_DUMMY_MENU, KEY_RECENT,// KEY_DUMMY_HOME1,
	/*KEY_DUMMY_HOME2,*/ KEY_BACK, KEY_DUMMY_BACK};
#endif

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
struct bt532_ts_info *tui_tsp_info;
extern int tui_force_close(uint32_t arg);
#endif

#ifdef CONFIG_SAMSUNG_TUI
struct bt532_ts_info *tui_tsp_info;
#endif

extern unsigned int lcdtype;
#ifdef CONFIG_FB_MSM_MDSS_SAMSUNG
extern int get_lcd_attached(char *mode);
#endif

/* define i2c sub functions*/
static inline s32 read_data(struct i2c_client *client,
	u16 reg, u8 *values, u16 length)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	s32 ret;
	int count = 0;

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	if (TRUSTEDUI_MODE_INPUT_SECURED & trustedui_get_current_mode()) {
		input_err(true, &client->dev,
				"%s TSP no accessible from Linux, TUI is enabled!\n", __func__);
		return -EIO;
	}
#endif
#ifdef CONFIG_SAMSUNG_TUI
	if (STUI_MODE_TOUCH_SEC & stui_get_mode())
		return -EBUSY;
#endif

retry:
	/* select register*/
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0) {
		usleep_range(1 * 1000, 1 * 1000);

		if (++count < 8)
			goto retry;

		info->comm_err_count++;
		return ret;
	}
	/* for setup tx transaction. */
	usleep_range(DELAY_FOR_TRANSCATION, DELAY_FOR_TRANSCATION);
	ret = i2c_master_recv(client , values , length);
	if (ret < 0) {
		info->comm_err_count++;
		return ret;
	}

	usleep_range(DELAY_FOR_POST_TRANSCATION, DELAY_FOR_POST_TRANSCATION);
	return length;
}

#ifdef TCLM_CONCEPT
#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
static s32 read_data_only(struct i2c_client *client, u8 *values, u16 length)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	s32 ret;
	int count = 0;

retry:
	ret = i2c_master_recv(client, values, length);
	if (ret < 0) {
		input_err(true, &client->dev, "%s: failed to recv. ret:%d, try:%d\n",
							__func__, ret, count + 1);
		usleep_range(1 * 1000, 1 * 1000);
		if (++count < 8)
			goto retry;

		info->comm_err_count++;
		return ret;
	}
	usleep_range(DELAY_FOR_TRANSCATION, DELAY_FOR_TRANSCATION);
	return length;
}
#endif
#endif

static inline s32 write_data(struct i2c_client *client,
	u16 reg, u8 *values, u16 length)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	s32 ret;
	int count = 0;
	u8 pkt[66]; /* max packet */
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
		if (TRUSTEDUI_MODE_INPUT_SECURED & trustedui_get_current_mode()) {
			input_err(true, &client->dev,
					"%s TSP no accessible from Linux, TUI is enabled!\n", __func__);
			return -EIO;
		}
#endif
#ifdef CONFIG_SAMSUNG_TUI
	if (STUI_MODE_TOUCH_SEC & stui_get_mode())
		return -EBUSY;
#endif

	pkt[0] = (reg) & 0xff; /* reg addr */
	pkt[1] = (reg >> 8)&0xff;
	memcpy((u8 *)&pkt[2], values, length);

retry:
	ret = i2c_master_send(client , pkt , length + 2);
	if (ret < 0) {
		usleep_range(1 * 1000, 1 * 1000);

		if (++count < 8)
			goto retry;

		info->comm_err_count++;
		return ret;
	}

	usleep_range(DELAY_FOR_POST_TRANSCATION, DELAY_FOR_POST_TRANSCATION);
	return length;
}

static inline s32 write_reg(struct i2c_client *client, u16 reg, u16 value)
{
	if (write_data(client, reg, (u8 *)&value, 2) < 0)
		return I2C_FAIL;

	return I2C_SUCCESS;
}

static inline s32 write_cmd(struct i2c_client *client, u16 reg)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	s32 ret;
	int count = 0;

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
		if (TRUSTEDUI_MODE_INPUT_SECURED & trustedui_get_current_mode()) {
			input_err(true, &client->dev,
					"%s TSP no accessible from Linux, TUI is enabled!\n", __func__);
			return -EIO;
		}
#endif
#ifdef CONFIG_SAMSUNG_TUI
	if (STUI_MODE_TOUCH_SEC & stui_get_mode())
		return -EBUSY;
#endif

retry:
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0) {
		usleep_range(1 * 1000, 1 * 1000);

		if (++count < 8)
			goto retry;

		info->comm_err_count++;
		return ret;
	}

	usleep_range(DELAY_FOR_POST_TRANSCATION, DELAY_FOR_POST_TRANSCATION);
	return I2C_SUCCESS;
}

static inline s32 read_raw_data(struct i2c_client *client,
		u16 reg, u8 *values, u16 length)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	s32 ret;
	int count = 0;

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
		if (TRUSTEDUI_MODE_INPUT_SECURED & trustedui_get_current_mode()) {
			input_err(true, &client->dev,
					"%s TSP no accessible from Linux, TUI is enabled!\n", __func__);
			return -EIO;
		}
#endif
#ifdef CONFIG_SAMSUNG_TUI
	if (STUI_MODE_TOUCH_SEC & stui_get_mode())
		return -EBUSY;
#endif

retry:
	/* select register */
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0) {
		usleep_range(1 * 1000, 1 * 1000);

		if (++count < 8)
			goto retry;

		info->comm_err_count++;
		return ret;
	}

	/* for setup tx transaction. */
	usleep_range(200, 200);

	ret = i2c_master_recv(client , values , length);
	if (ret < 0) {
		info->comm_err_count++;
		return ret;
	}

	usleep_range(DELAY_FOR_POST_TRANSCATION, DELAY_FOR_POST_TRANSCATION);
	return length;
}

static inline s32 read_firmware_data(struct i2c_client *client,
	u16 addr, u8 *values, u16 length)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	s32 ret;

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	if (TRUSTEDUI_MODE_INPUT_SECURED & trustedui_get_current_mode()) {
		input_err(true, &client->dev,
				"%s TSP no accessible from Linux, TUI is enabled!\n", __func__);
		return -EIO;
	}
#endif
#ifdef CONFIG_SAMSUNG_TUI
	if (STUI_MODE_TOUCH_SEC & stui_get_mode())
		return -EBUSY;
#endif

	/* select register*/
	ret = i2c_master_send(client , (u8 *)&addr , 2);
	if (ret < 0) {
		info->comm_err_count++;
		return ret;
	}

	/* for setup tx transaction. */
	usleep_range(1 * 1000, 1 * 1000);

	ret = i2c_master_recv(client , values , length);
	if (ret < 0) {
		info->comm_err_count++;
		return ret;
	}

	usleep_range(DELAY_FOR_POST_TRANSCATION, DELAY_FOR_POST_TRANSCATION);
	return length;
}

#ifdef CONFIG_INPUT_ENABLED
static int  bt532_ts_open(struct input_dev *dev);
static void bt532_ts_close(struct input_dev *dev);
#endif

static bool bt532_power_control(struct bt532_ts_info *info, u8 ctl);
static int bt532_pinctrl_configure(struct bt532_ts_info *info, bool active);

static bool init_touch(struct bt532_ts_info *info);
static bool mini_init_touch(struct bt532_ts_info *info);
static void clear_report_data(struct bt532_ts_info *info);
#if ESD_TIMER_INTERVAL
static void esd_timer_start(u16 sec, struct bt532_ts_info *info);
static void esd_timer_stop(struct bt532_ts_info *info);
static void esd_timer_init(struct bt532_ts_info *info);
static void esd_timeout_handler(unsigned long data);
#endif

static void zinitix_display_rawdata(struct bt532_ts_info *info, struct tsp_raw_data *raw_data, int type, int gap);
void zinitix_set_grip_type(struct bt532_ts_info *info, u8 set_type);

#ifdef TCLM_CONCEPT
int get_zt_tsp_nvm_data(struct bt532_ts_info *info, u8 addr, u8 *values, u16 length);
int set_zt_tsp_nvm_data(struct bt532_ts_info *info, u8 addr, u8 *values, u16 length);
#endif

void location_detect(struct bt532_ts_info *info, char *loc, int x, int y);

#ifdef USE_MISC_DEVICE
static long ts_misc_fops_ioctl(struct file *filp, unsigned int cmd,
								unsigned long arg);
static int ts_misc_fops_open(struct inode *inode, struct file *filp);
static int ts_misc_fops_close(struct inode *inode, struct file *filp);

static const struct file_operations ts_misc_fops = {
	.owner = THIS_MODULE,
	.open = ts_misc_fops_open,
	.release = ts_misc_fops_close,
	//.unlocked_ioctl = ts_misc_fops_ioctl,
	.compat_ioctl = ts_misc_fops_ioctl,
};

static struct miscdevice touch_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "zinitix_touch_misc",
	.fops = &ts_misc_fops,
};

#define TOUCH_IOCTL_BASE	0xbc
#define TOUCH_IOCTL_GET_DEBUGMSG_STATE		_IOW(TOUCH_IOCTL_BASE, 0, int)
#define TOUCH_IOCTL_SET_DEBUGMSG_STATE		_IOW(TOUCH_IOCTL_BASE, 1, int)
#define TOUCH_IOCTL_GET_CHIP_REVISION		_IOW(TOUCH_IOCTL_BASE, 2, int)
#define TOUCH_IOCTL_GET_FW_VERSION			_IOW(TOUCH_IOCTL_BASE, 3, int)
#define TOUCH_IOCTL_GET_REG_DATA_VERSION	_IOW(TOUCH_IOCTL_BASE, 4, int)
#define TOUCH_IOCTL_VARIFY_UPGRADE_SIZE		_IOW(TOUCH_IOCTL_BASE, 5, int)
#define TOUCH_IOCTL_VARIFY_UPGRADE_DATA		_IOW(TOUCH_IOCTL_BASE, 6, int)
#define TOUCH_IOCTL_START_UPGRADE			_IOW(TOUCH_IOCTL_BASE, 7, int)
#define TOUCH_IOCTL_GET_X_NODE_NUM			_IOW(TOUCH_IOCTL_BASE, 8, int)
#define TOUCH_IOCTL_GET_Y_NODE_NUM			_IOW(TOUCH_IOCTL_BASE, 9, int)
#define TOUCH_IOCTL_GET_TOTAL_NODE_NUM		_IOW(TOUCH_IOCTL_BASE, 10, int)
#define TOUCH_IOCTL_SET_RAW_DATA_MODE		_IOW(TOUCH_IOCTL_BASE, 11, int)
#define TOUCH_IOCTL_GET_RAW_DATA			_IOW(TOUCH_IOCTL_BASE, 12, int)
#define TOUCH_IOCTL_GET_X_RESOLUTION		_IOW(TOUCH_IOCTL_BASE, 13, int)
#define TOUCH_IOCTL_GET_Y_RESOLUTION		_IOW(TOUCH_IOCTL_BASE, 14, int)
#define TOUCH_IOCTL_HW_CALIBRAION			_IOW(TOUCH_IOCTL_BASE, 15, int)
#define TOUCH_IOCTL_GET_REG					_IOW(TOUCH_IOCTL_BASE, 16, int)
#define TOUCH_IOCTL_SET_REG					_IOW(TOUCH_IOCTL_BASE, 17, int)
#define TOUCH_IOCTL_SEND_SAVE_STATUS		_IOW(TOUCH_IOCTL_BASE, 18, int)
#define TOUCH_IOCTL_DONOT_TOUCH_EVENT		_IOW(TOUCH_IOCTL_BASE, 19, int)
#endif

struct bt532_ts_info *misc_info;

static void set_cover_type(struct bt532_ts_info *info, bool enable)
{
	struct i2c_client *client = info->client;

	write_cmd(info->client, 0x0A);
	if (enable){
		switch (info->cover_type) {
			case ZT_FLIP_WALLET:
				write_reg(client, BT532_COVER_CONTROL_REG, WALLET_COVER_CLOSE);
				break;
			case ZT_VIEW_COVER:
				write_reg(client, BT532_COVER_CONTROL_REG, VIEW_COVER_CLOSE);
				break;
			case ZT_CLEAR_FLIP_COVER:
				write_reg(client, BT532_COVER_CONTROL_REG, CLEAR_COVER_CLOSE);
				break;
			case ZT_NEON_COVER:
				write_reg(client, BT532_COVER_CONTROL_REG, LED_COVER_CLOSE);
				break;
			case ZT_S_VIEW_COVER:
				write_reg(client, BT532_COVER_CONTROL_REG, S_VIEW_COVER_CLOSE);
				break;				
			default:
				input_err(true, &info->client->dev, "%s: touch is not supported for %d cover\n",
							__func__, info->cover_type);
		}
	}
	else
		write_reg(client, BT532_COVER_CONTROL_REG, COVER_OPEN);

	write_cmd(info->client, 0x0B);
	input_info(true, &info->client->dev, "%s: type %d enable %d\n", __func__, info->cover_type, enable);
}

static void bt532_set_optional_mode(struct bt532_ts_info *info, bool force)
{
	u16	reg_val;

	if (m_prev_optional_mode.optional_mode == m_optional_mode.optional_mode && !force)
		return;
	mutex_lock(&info->set_reg_lock);
	reg_val = m_optional_mode.optional_mode;
	mutex_unlock(&info->set_reg_lock);
	if (write_reg(info->client, BT532_OPTIONAL_SETTING, reg_val) == I2C_SUCCESS) {
		m_prev_optional_mode.optional_mode = reg_val;
	}
}
#ifdef SEC_FACTORY_TEST
static bool get_raw_data(struct bt532_ts_info *info, u8 *buff, int skip_cnt)
{
	struct i2c_client *client = info->client;
	struct bt532_ts_platform_data *pdata = info->pdata;
	u32 total_node = info->cap_info.total_node_num;
	u32 sz;
	int i, j = 0;

	disable_irq(info->irq);

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		input_info(true, &client->dev, "other process occupied.. (%d)\n",
			info->work_state);
		enable_irq(info->irq);
		up(&info->work_lock);
		return false;
		}

	info->work_state = RAW_DATA;

	for (i = 0; i < skip_cnt; i++) {
		while (gpio_get_value(pdata->gpio_int)) {
			usleep_range(1 * 1000, 1 * 1000);
			if (++j > 3000) {
				input_err(true, &info->client->dev, "%s: (skip_cnt) wait int timeout\n", __func__);
				break;
			}
		}

		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		usleep_range(1 * 1000, 1 * 1000);
	}

	zinitix_debug_msg("read raw data\r\n");
	sz = total_node*2;

	j = 0;
	while (gpio_get_value(pdata->gpio_int)) {
		usleep_range(1 * 1000, 1 * 1000);
		if (++j > 3000) {
			input_err(true, &info->client->dev, "%s: wait int timeout\n", __func__);
			break;
		}
	}

	if (read_raw_data(client, BT532_RAWDATA_REG, (char *)buff, sz) < 0) {
		input_info(true, &client->dev, "error : read zinitix tc raw data\n");
		info->work_state = NOTHING;
		enable_irq(info->irq);
		up(&info->work_lock);
		return false;
	}

	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);

	return true;
}
#endif
static bool ts_get_raw_data(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	u32 total_node = info->cap_info.total_node_num;
	u32 sz;

	if (down_trylock(&info->raw_data_lock)) {
		input_err(true, &client->dev, "Failed to occupy sema\n");
		return true;
	}

	sz = total_node * 2 + sizeof(struct point_info) * MAX_SUPPORTED_FINGER_NUM;

	if (read_raw_data(info->client, BT532_RAWDATA_REG,
			(char *)info->cur_data, sz) < 0) {
		input_err(true, &client->dev, "Failed to read raw data\n");
		up(&info->raw_data_lock);
		return false;
	}

	info->update = 1;
	memcpy((u8 *)(&info->touch_info[0]),
		(u8 *)&info->cur_data[total_node],
		sizeof(struct point_info) * MAX_SUPPORTED_FINGER_NUM);
	up(&info->raw_data_lock);

	return true;
}

static bool ts_read_coord(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	int i, retry_cnt;
	u16 status_data;
	u16 pocket_data;

	/* zinitix_debug_msg("ts_read_coord+\r\n"); */

	/* for  Debugging Tool */

	if (info->touch_mode != TOUCH_POINT_MODE) {
		if (ts_get_raw_data(info) == false)
			return false;

		if (info->touch_mode == TOUCH_SENTIVITY_MEASUREMENT_MODE) {
			for (i = 0; i < TOUCH_SENTIVITY_MEASUREMENT_COUNT; i++) {
				info->sensitivity_data[i] = info->cur_data[i];
			}
		}

		goto out;
	}

	if (info->pocket_enable) {
		if (read_data(info->client, ZT75XX_STATUS_REG, (u8 *)&status_data, 2) < 0) {
			input_err(true, &client->dev, "%s: fail to read status reg\n", __func__);
		}

		if (zinitix_bit_test(status_data, BIT_POCKET_MODE)) {
			if (read_data(info->client, ZT75XX_POCKET_DETECT, (u8 *)&pocket_data, 2) < 0) {
				input_err(true, &client->dev, "%s: fail to read pocket reg\n", __func__);
			} else {
				input_info(true, &client->dev, "Pocket %s \n", pocket_data == 11 ? "IN" : "OUT");
				input_report_abs(info->input_dev_proximity, ABS_MT_CUSTOM, pocket_data);
				input_sync(info->input_dev_proximity);
			}
		}
	}

	memset(&info->touch_info[0], 0x0, sizeof(struct point_info) * MAX_SUPPORTED_FINGER_NUM);

	retry_cnt = 0;
	while(retry_cnt < 10) {
		if (read_data(info->client, BT532_POINT_STATUS_REG,
			(u8 *)(&info->touch_info[0]), sizeof(struct point_info)) < 0) {
			input_err(true, &client->dev, "Failed to read point info, retry_cnt = %d\n", ++retry_cnt);
			continue;
		} else
			break;
	}

	if (retry_cnt >= 10)
		return false;
	
	if (info->touch_info[0].byte00.value.eid == COORDINATE_EVENT) {
		info->touched_finger_num = info->touch_info[0].byte07.value.left_event;
		if (info->touched_finger_num > 0)	{
			retry_cnt = 0;
			while(retry_cnt < 10) {
				if (read_data(info->client, BT532_POINT_STATUS_REG1, (u8 *)(&info->touch_info[1]),
						(info->touched_finger_num)*sizeof(struct point_info)) < 0) {
					input_err(true, &client->dev, "Failed to read touched point info, retry_cnt = %d\n", ++retry_cnt);
					continue;
				} else
					break;
			}
			if (retry_cnt >= 10)
				return false;
		}
	} else if ((info->pdata->support_lpm_mode) && (info->touch_info[0].byte00.value.eid == GESTURE_EVENT)) {
		if (info->touch_info[0].byte00.value.tid == SWIPE_UP) {
			input_info(true, &client->dev, "Spay Gesture\n");

			info->scrub_id = SPONGE_EVENT_TYPE_SPAY;
			info->scrub_x = 0;
			info->scrub_y = 0;

			input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 1);
			input_sync(info->input_dev);
			input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 0);
			input_sync(info->input_dev);
		} else if(info->touch_info[0].byte00.value.tid == FINGERPRINT) {
			if (info->fod_enable &&
				((info->touch_info[0].byte01.value_u8bit == 0)
				|| (info->touch_info[0].byte01.value_u8bit == 1))) {
				info->scrub_id = SPONGE_EVENT_TYPE_FOD_PRESS;

				info->scrub_x = ((info->touch_info[0].byte02.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0xF0) >> 4);
				info->scrub_y = ((info->touch_info[0].byte03.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0x0F));

				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 1);
				input_sync(info->input_dev);
				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 0);
				input_sync(info->input_dev);
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
				input_info(true, &client->dev, "%s: FOD %s PRESS: %d\n", __func__,
					info->touch_info[0].byte01.value_u8bit ? "NORMAL" : "LONG", info->scrub_id);
#else
				input_info(true, &client->dev, "%s: FOD %s PRESS: %d, %d, %d\n", __func__,
					info->touch_info[0].byte01.value_u8bit ? "NORMAL" : "LONG",
					info->scrub_id, info->scrub_x, info->scrub_y);
#endif
			} else if (info->fod_enable && (info->touch_info[0].byte01.value_u8bit == 2)) {
				info->scrub_id = SPONGE_EVENT_TYPE_FOD_RELEASE;

				info->scrub_x = ((info->touch_info[0].byte02.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0xF0) >> 4);
				info->scrub_y = ((info->touch_info[0].byte03.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0x0F));

				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 1);
				input_sync(info->input_dev);
				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 0);
				input_sync(info->input_dev);
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
				input_info(true, &client->dev, "%s: FOD RELEASE: %d\n", __func__, info->scrub_id);
#else
				input_info(true, &client->dev, "%s: FOD RELEASE: %d, %d, %d\n",
						__func__, info->scrub_id, info->scrub_x, info->scrub_y);
#endif
			} else if (info->fod_enable && (info->touch_info[0].byte01.value_u8bit == 3)) {
				info->scrub_id = SPONGE_EVENT_TYPE_FOD_OUT;

				info->scrub_x = ((info->touch_info[0].byte02.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0xF0) >> 4);
				info->scrub_y = ((info->touch_info[0].byte03.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0x0F));

				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 1);
				input_sync(info->input_dev);
				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 0);
				input_sync(info->input_dev);
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
				input_info(true, &client->dev, "%s: FOD OUT: %d\n", __func__, info->scrub_id);
#else
				input_info(true, &client->dev, "%s: FOD OUT: %d, %d, %d\n",
						__func__, info->scrub_id, info->scrub_x, info->scrub_y);
#endif
			}
		} else if(info->touch_info[0].byte00.value.tid == SINGLE_TAP) {
			if (info->singletap_enable) {
				info->scrub_id = SPONGE_EVENT_TYPE_SINGLE_TAP;

				info->scrub_x = ((info->touch_info[0].byte02.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0xF0) >> 4);
				info->scrub_y = ((info->touch_info[0].byte03.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0x0F));

				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 1);
				input_sync(info->input_dev);
				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 0);
				input_sync(info->input_dev);
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
				input_info(true, &client->dev, "%s: SINGLE TAP: %d\n", __func__, info->scrub_id);
#else
				input_info(true, &client->dev, "%s: SINGLE TAP: %d, %d, %d\n",
						__func__, info->scrub_id, info->scrub_x, info->scrub_y);
#endif
			}
		} else if (info->touch_info[0].byte00.value.tid == DOUBLE_TAP) {
			if (info->aot_enable && (info->touch_info[0].byte01.value_u8bit == 1)) {
				input_report_key(info->input_dev, KEY_WAKEUP, 1);
				input_sync(info->input_dev);
				input_report_key(info->input_dev, KEY_WAKEUP, 0);
				input_sync(info->input_dev);

				/* request from sensor team */
				input_report_abs(info->input_dev_proximity, ABS_MT_CUSTOM2, 1);
				input_sync(info->input_dev_proximity);
				input_report_abs(info->input_dev_proximity, ABS_MT_CUSTOM2, 0);
				input_sync(info->input_dev_proximity);

				input_info(true, &client->dev, "AOT Doubletab\n");
			} else if (info->aod_enable && (info->touch_info[0].byte01.value_u8bit == 0)) {
				info->scrub_id = SPONGE_EVENT_TYPE_AOD_DOUBLETAB;

				info->scrub_x = ((info->touch_info[0].byte02.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0xF0) >> 4);
				info->scrub_y = ((info->touch_info[0].byte03.value_u8bit << 4) & 0xFF0)
								| ((info->touch_info[0].byte04.value_u8bit & 0x0F));

				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 1);
				input_sync(info->input_dev);
				input_report_key(info->input_dev, KEY_BLACK_UI_GESTURE, 0);
				input_sync(info->input_dev);

#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
				input_info(true, &client->dev, "%s: AOD Doubletab: %d\n", __func__, info->scrub_id);
#else
				input_info(true, &client->dev, "%s: AOD Doubletab: %d, %d, %d\n",
						__func__, info->scrub_id, info->scrub_x, info->scrub_y);
#endif
			}
		}
	}

	bt532_set_optional_mode(info, false);
out:
	write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD);
	return true;
}

#if ESD_TIMER_INTERVAL
static void esd_timeout_handler(unsigned long data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)data;

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	struct i2c_client *client = info->client;
	if (TRUSTEDUI_MODE_INPUT_SECURED & trustedui_get_current_mode()) {
		input_err(true, &client->dev,
				"%s TSP no accessible from Linux, TUI is enabled!\n", __func__);
		esd_timer_stop(info);
		return;
	}
#endif
#ifdef CONFIG_SAMSUNG_TUI
	struct i2c_client *client = info->client;

	if (STUI_MODE_TOUCH_SEC & stui_get_mode()) {
		input_err(true, &client->dev,
				"%s TSP not accessible during TUI\n", __func__);
		return;
	}
#endif

	info->p_esd_timeout_tmr = NULL;
	queue_work(esd_tmr_workqueue, &info->tmr_work);
}

static void esd_timer_start(u16 sec, struct bt532_ts_info *info)
{
	unsigned long flags;

	if (info->sleep_mode){
		input_info(true, &info->client->dev, "%s skip (sleep_mode)!\n", __func__);
		return;
	}

	spin_lock_irqsave(&info->lock, flags);
	if (info->p_esd_timeout_tmr != NULL)
#ifdef CONFIG_SMP
		del_singleshot_timer_sync(info->p_esd_timeout_tmr);
#else
		del_timer(info->p_esd_timeout_tmr);
#endif
	info->p_esd_timeout_tmr = NULL;
	init_timer(&(info->esd_timeout_tmr));
	info->esd_timeout_tmr.data = (unsigned long)(info);
	info->esd_timeout_tmr.function = esd_timeout_handler;
	info->esd_timeout_tmr.expires = jiffies + (HZ * sec);
	info->p_esd_timeout_tmr = &info->esd_timeout_tmr;
	add_timer(&info->esd_timeout_tmr);
	spin_unlock_irqrestore(&info->lock, flags);
}

static void esd_timer_stop(struct bt532_ts_info *info)
{
	unsigned long flags;
	spin_lock_irqsave(&info->lock, flags);
	if (info->p_esd_timeout_tmr)
#ifdef CONFIG_SMP
		del_singleshot_timer_sync(info->p_esd_timeout_tmr);
#else
		del_timer(info->p_esd_timeout_tmr);
#endif

	info->p_esd_timeout_tmr = NULL;
	spin_unlock_irqrestore(&info->lock, flags);
}

static void esd_timer_init(struct bt532_ts_info *info)
{
	unsigned long flags;
	spin_lock_irqsave(&info->lock, flags);
	init_timer(&(info->esd_timeout_tmr));
	info->esd_timeout_tmr.data = (unsigned long)(info);
	info->esd_timeout_tmr.function = esd_timeout_handler;
	info->p_esd_timeout_tmr = NULL;
	spin_unlock_irqrestore(&info->lock, flags);
}

static void ts_tmr_work(struct work_struct *work)
{
	struct bt532_ts_info *info =
				container_of(work, struct bt532_ts_info, tmr_work);
	struct i2c_client *client = info->client;

#if defined(TSP_VERBOSE_DEBUG)
	input_info(true, &client->dev, "tmr queue work ++\n");
#endif

	if (down_trylock(&info->work_lock)) {
		input_err(true, &client->dev, "%s: Failed to occupy work lock\n", __func__);
		esd_timer_start(CHECK_ESD_TIMER, info);

		return;
	}

	if (info->work_state != NOTHING) {
		input_info(true, &client->dev, "%s: Other process occupied (%d)\n",
			__func__, info->work_state);
		up(&info->work_lock);

		return;
	}
	info->work_state = ESD_TIMER;

	disable_irq(info->irq);
	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	clear_report_data(info);
	if (mini_init_touch(info) == false)
		goto fail_time_out_init;

	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);
#if defined(TSP_VERBOSE_DEBUG)
	input_info(true, &client->dev, "tmr queue work--\n");
#endif

	return;
fail_time_out_init:
	input_err(true, &client->dev, "%s: Failed to restart\n", __func__);
	esd_timer_start(CHECK_ESD_TIMER, info);
	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);

	return;
}
#endif

static bool bt532_power_sequence(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	int retry = 0;
	u16 chip_code;

	info->cap_info.ic_fw_size = 32*1024;

retry_power_sequence:
	if (write_reg(client, 0xc000, 0x0001) != I2C_SUCCESS) {
		input_err(true, &client->dev, "Failed to send power sequence(vendor cmd enable)\n");
		goto fail_power_sequence;
	}
	usleep_range(10, 10);

	if (read_data(client, 0xcc00, (u8 *)&chip_code, 2) < 0) {
		input_err(true, &client->dev, "Failed to read chip code\n");
		goto fail_power_sequence;
	}

	input_info(true, &client->dev, "%s: chip code = 0x%x\n", __func__, chip_code);
	usleep_range(10, 10);

	if (chip_code == ZT7554_CHIP_CODE)
		info->cap_info.ic_fw_size = 64*1024;
	else if (chip_code == ZT7548_CHIP_CODE)
		info->cap_info.ic_fw_size = 48*1024;
	else if (chip_code == ZT7538_CHIP_CODE)
		info->cap_info.ic_fw_size = 44*1024;
	else if (chip_code == BT43X_CHIP_CODE)
		info->cap_info.ic_fw_size = 24*1024;
	else if (chip_code == BT53X_CHIP_CODE)
		info->cap_info.ic_fw_size = 32*1024;

	if (write_cmd(client, 0xc004) != I2C_SUCCESS) {
		input_err(true, &client->dev, "Failed to send power sequence(intn clear)\n");
		goto fail_power_sequence;
	}
	usleep_range(10, 10);

	if (write_reg(client, 0xc002, 0x0001) != I2C_SUCCESS) {
		input_err(true, &client->dev, "Failed to send power sequence(nvm init)\n");
		goto fail_power_sequence;
	}
	usleep_range(2 * 1000, 2 * 1000);

	if (write_reg(client, 0xc001, 0x0001) != I2C_SUCCESS) {
		input_err(true, &client->dev, "Failed to send power sequence(program start)\n");
		goto fail_power_sequence;
	}

		msleep(FIRMWARE_ON_DELAY);	/* wait for checksum cal */

	return true;

fail_power_sequence:
	if (retry++ < 3) {
		input_info(true, &client->dev, "retry = %d\n", retry);

		msleep(CHIP_ON_DELAY);
		goto retry_power_sequence;
	}

	return false;
}

static bool bt532_power_control(struct bt532_ts_info *info, u8 ctl)
{
	struct i2c_client *client = info->client;

	int ret = 0;

	input_info(true, &client->dev, "[TSP] %s, %d\n", __func__, ctl);

	ret = info->pdata->tsp_power(info, ctl);
	if (ret)
		return false;

	bt532_pinctrl_configure(info, ctl);

	if (ctl == POWER_ON_SEQUENCE) {
		msleep(CHIP_ON_DELAY);
		return bt532_power_sequence(info);
	}
	else if (ctl == POWER_OFF) {
		msleep(CHIP_OFF_DELAY);
	}
	else if (ctl == POWER_ON) {
		msleep(CHIP_ON_DELAY);
	}

	return true;
}

static void bt532_set_ta_status(struct bt532_ts_info *info)
{
	input_info(true, &info->client->dev, "%s g_ta_connected %d\n", __func__, g_ta_connected);

	if (g_ta_connected) {
		mutex_lock(&info->set_reg_lock);
		zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_USB_DETECT_BIT);
		mutex_unlock(&info->set_reg_lock);
	}
	else {
		mutex_lock(&info->set_reg_lock);
		zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_USB_DETECT_BIT);
		mutex_unlock(&info->set_reg_lock);
	}
}

#ifdef CONFIG_VBUS_NOTIFIER
int tsp_vbus_notification(struct notifier_block *nb,
		unsigned long cmd, void *data)
{
	struct bt532_ts_info *info = container_of(nb, struct bt532_ts_info, vbus_nb);
	vbus_status_t vbus_type = *(vbus_status_t *)data;

	input_info(true, &info->client->dev, "%s cmd=%lu, vbus_type=%d\n", __func__, cmd, vbus_type);

	switch (vbus_type) {
	case STATUS_VBUS_HIGH:
		input_info(true, &info->client->dev, "%s : attach\n", __func__);
		g_ta_connected = true;
		break;
	case STATUS_VBUS_LOW:
		input_info(true, &info->client->dev, "%s : detach\n", __func__);
		g_ta_connected = false;
		break;
	default:
		break;
	}

	if (!bypass_mode)
		bt532_set_ta_status(info);
	else
		g_ta_connected = false;
	return 0;
}
#endif

static void bt532_charger_status_cb(struct tsp_callbacks *cb, bool ta_status)
{
	struct bt532_ts_info *info =
			container_of(cb, struct bt532_ts_info, callbacks);
	if (!ta_status)
		g_ta_connected = false;
	else
		g_ta_connected = true;

	bt532_set_ta_status(info);
	input_info(true, &info->client->dev, "TA %s\n", ta_status ? "connected" : "disconnected");
}

static bool crc_check(struct bt532_ts_info *info)
{
	u16 chip_check_sum = 0;

	if (read_data(info->client, BT532_CHECKSUM_RESULT,
					(u8 *)&chip_check_sum, 2) < 0) {
		input_err(true, &info->client->dev, "%s: read crc fail", __func__);
	}

	input_info(true, &info->client->dev, "%s: 0x%04X\n", __func__, chip_check_sum);

	if (chip_check_sum == 0x55aa)
		return true;
	else
		return false;
}

#if TOUCH_ONESHOT_UPGRADE
static bool ts_check_need_upgrade(struct bt532_ts_info *info,
	u16 cur_version, u16 cur_minor_version, u16 cur_reg_version, u16 cur_hw_id)
{
	u16	new_version;
	u16	new_minor_version;
	u16	new_reg_version;
#if CHECK_HWID
	u16	new_hw_id;
#endif

	new_version = (u16) (info->fw_data[52] | (info->fw_data[53]<<8));
	new_minor_version = (u16) (info->fw_data[56] | (info->fw_data[57]<<8));
	new_reg_version = (u16) (info->fw_data[60] | (info->fw_data[61]<<8));

#if CHECK_HWID
	new_hw_id = (u16) (fw_data[0x7528] | (fw_data[0x7529]<<8));
	input_info(true, &info->client->dev, "cur HW_ID = 0x%x, new HW_ID = 0x%x\n",
							cur_hw_id, new_hw_id);
	if (cur_hw_id != new_hw_id)
		return false;
#endif

	input_info(true, &info->client->dev, "cur version = 0x%x, new version = 0x%x\n",
							cur_version, new_version);
	input_info(true, &info->client->dev, "cur minor version = 0x%x, new minor version = 0x%x\n",
						cur_minor_version, new_minor_version);
	input_info(true, &info->client->dev, "cur reg data version = 0x%x, new reg data version = 0x%x\n",
						cur_reg_version, new_reg_version);

	if (cur_version > 0xFF)
		return true;
	if (cur_version < new_version)
		return true;
	else if (cur_version > new_version)
		return false;
	if (cur_minor_version < new_minor_version)
		return true;
	else if (cur_minor_version > new_minor_version)
		return false;
	if (cur_reg_version < new_reg_version)
		return true;

	return false;
}
#endif

#define TC_SECTOR_SZ		8
#define TC_NVM_SECTOR_SZ	64
#ifdef TCLM_CONCEPT
#define TC_SECTOR_SZ_WRITE		64
#define TC_SECTOR_SZ_READ		8
#endif

#if TOUCH_ONESHOT_UPGRADE || TOUCH_FORCE_UPGRADE \
	|| defined(SEC_FACTORY_TEST) || defined(USE_MISC_DEVICE)
static u8 ts_upgrade_firmware(struct bt532_ts_info *info,
	const u8 *firmware_data, u32 size)
{
	struct i2c_client *client = info->client;
	u16 flash_addr;
	u8 *verify_data;
	int retry_cnt = 0;
	int i;
	int page_sz = 128;
	u16 chip_code;
#ifndef TCLM_CONCEPT
	int fuzing_udelay = 8000;
#endif

	verify_data = vzalloc(size);
	if (verify_data == NULL) {
		input_info(true, &client->dev, "cannot alloc verify buffer\n");
		return false;
	}

retry_upgrade:
	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON);
	usleep_range(10 * 1000, 10 * 1000);

	if (write_reg(client, 0xc000, 0x0001) != I2C_SUCCESS){
		input_info(true, &client->dev, "power sequence error (vendor cmd enable)\n");
		goto fail_upgrade;
	}

	usleep_range(10, 10);

	if (read_data(client, 0xcc00, (u8 *)&chip_code, 2) < 0) {
		input_info(true, &client->dev, "failed to read chip code\n");
		goto fail_upgrade;
	}

	input_info(true, &client->dev, "chip code = 0x%x\n", chip_code);

#ifdef TCLM_CONCEPT
	if (chip_code == ZT7538_CHIP_CODE || chip_code == ZT7548_CHIP_CODE || chip_code == ZT7532_CHIP_CODE) {
		flash_addr = (firmware_data[0x61]<<16) | (firmware_data[0x62]<<8) | firmware_data[0x63];
		flash_addr += ((firmware_data[0x65]<<16) | (firmware_data[0x66]<<8) | firmware_data[0x67]);

		if (flash_addr != 0)
			size = flash_addr;

		page_sz = 64;

		if (write_reg(client, 0xc201, 0x00be) != I2C_SUCCESS) {
			input_info(true, &client->dev, "power sequence error (set clk speed)\n");
			goto fail_upgrade;
		}
		usleep_range(200, 200);
	}
	input_info(true, &client->dev, "f/w size = 0x%x Page_sz = %d \n", size, page_sz);

#else
	if ((chip_code == ZT7538_CHIP_CODE)||(chip_code == ZT7548_CHIP_CODE)||(chip_code == BT43X_CHIP_CODE))
		page_sz = 64;
#endif
	usleep_range(10, 10);

	if (write_cmd(client, 0xc004) != I2C_SUCCESS){
		input_info(true, &client->dev, "power sequence error (intn clear)\n");
		goto fail_upgrade;
	}

	usleep_range(10, 10);

	if (write_reg(client, 0xc002, 0x0001) != I2C_SUCCESS){
		input_info(true, &client->dev, "power sequence error (nvm init)\n");
		goto fail_upgrade;
	}

	usleep_range(5 * 1000, 5 * 1000);

	input_info(true, &client->dev, "init flash\n");

	if (write_reg(client, 0xc003, 0x0001) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to write nvm vpp on\n");
		goto fail_upgrade;
	}

	if (write_reg(client, 0xc104, 0x0001) != I2C_SUCCESS){
		input_info(true, &client->dev, "failed to write nvm wp disable\n");
		goto fail_upgrade;
	}

#ifdef TCLM_CONCEPT
	if (write_reg(client, BT532_INIT_FLASH, 2) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to enter burst upgrade mode\n");
		goto fail_upgrade;
	}

	for (flash_addr = 0; flash_addr < size; ) {
		for (i = 0; i < page_sz/TC_SECTOR_SZ_WRITE; i++) {
			if (write_data(client, BT532_WRITE_FLASH,
						(u8 *)&firmware_data[flash_addr], TC_SECTOR_SZ_WRITE) < 0) {
				input_info(true, &client->dev, "error : write zinitix tc firmare\n");
				goto fail_upgrade;
			}
			flash_addr += TC_SECTOR_SZ_WRITE;
		}
		i = 0;
		while(1) {
			if (flash_addr >= size)
				break;
			if (gpio_get_value(info->pdata->gpio_int))
				break;
			msleep(30);
			if (++i>100)
				break;
		}

		if (i>100) {
			input_info(true, &client->dev, "write timeout\n");
			goto fail_upgrade;
		}
	}

	if (write_cmd(client, 0x01DD) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to flush cmd\n");
		goto fail_upgrade;
	}
	msleep(100);
	i = 0;

	while(1) {
		if (gpio_get_value(info->pdata->gpio_int))
			break;
		msleep(30);
		if (++i>1000) {
			input_info(true, &client->dev, "flush timeout\n");
			goto fail_upgrade;
		}
	}
#else
	if ((chip_code == ZT7538_CHIP_CODE)||(chip_code == ZT7548_CHIP_CODE)||(chip_code == ZT7554_CHIP_CODE)) {
		if (write_cmd(client, BT532_INIT_FLASH) != I2C_SUCCESS) {
			input_info(true, &client->dev, "failed to init flash\n");
			goto fail_upgrade;
		}

		// Mass Erase
		//====================================================
		if (write_cmd(client, 0x01DF) != I2C_SUCCESS) {
			input_info(true, &client->dev, "failed to mass erase\n");
			goto fail_upgrade;
		}

		msleep(100);

		// Mass Erase End
		//====================================================

		if (write_reg(client, 0x01DE, 0x0001) != I2C_SUCCESS) {
			input_info(true, &client->dev, "failed to enter upgrade mode\n");
			goto fail_upgrade;
		}

		usleep_range(1000, 1000);

		if (write_reg(client, 0x01D3, 0x0008) != I2C_SUCCESS) {
			input_info(true, &client->dev, "failed to init upgrade mode\n");
			goto fail_upgrade;
		}
	} else if (chip_code == BT43X_CHIP_CODE){
	// Mass Erase
	//====================================================
	if (write_reg(client, 0xc108, 0x0007) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to write 0xc108 - 7\n");
		goto fail_upgrade;
	}

	if (write_reg(client, 0xc109, 0x0000) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to write 0xc109\n");
		goto fail_upgrade;
	}

	if (write_reg(client, 0xc10A, 0x0000) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to write nvm wp disable\n");
		goto fail_upgrade;
	}

	if (write_cmd(client, 0xc10B) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to write mass erease\n");
		goto fail_upgrade;
	}

	msleep(20);

	if (write_reg(client, 0xc108, 0x0008) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to write 0xc108 - 8\n");
		goto fail_upgrade;
	}

	if (write_cmd(client, BT532_INIT_FLASH) != I2C_SUCCESS) {
		input_info(true, &client->dev, "failed to init flash\n");
		goto fail_upgrade;
	}

	}else {
		fuzing_udelay = 30000;
		if (write_cmd(client, BT532_INIT_FLASH) != I2C_SUCCESS) {
			input_info(true, &client->dev, "failed to init flash\n");
			goto fail_upgrade;
		}
	}

	for (flash_addr = 0; flash_addr < size; ) {
		for (i = 0; i < page_sz/TC_SECTOR_SZ; i++) {
			if (write_data(client,
				BT532_WRITE_FLASH,
				(u8 *)&firmware_data[flash_addr],TC_SECTOR_SZ) < 0) {
				input_info(true, &client->dev, "error : write zinitix tc firmare\n");
				goto fail_upgrade;
			}
			flash_addr += TC_SECTOR_SZ;
			usleep_range(100, 100);
		}

		usleep_range(fuzing_udelay, fuzing_udelay);	/*for fuzing delay*/
	}
#endif

	if (write_reg(client, 0xc003, 0x0000) != I2C_SUCCESS) {
		input_info(true, &client->dev, "nvm write vpp off\n");
		goto fail_upgrade;
	}

	if (write_reg(client, 0xc104, 0x0000) != I2C_SUCCESS){
		input_info(true, &client->dev, "nvm wp enable\n");
		goto fail_upgrade;
	}

	input_info(true, &client->dev, "init flash\n");

	if (write_cmd(client, BT532_INIT_FLASH) != I2C_SUCCESS) {
		input_info(true, &client->dev, KERN_INFO "failed to init flash\n");
		goto fail_upgrade;
	}

	input_info(true, &client->dev, "read firmware data\n");

#ifdef TCLM_CONCEPT
	if (write_reg(client, 0x01D3, 0x0008) != I2C_SUCCESS) {
		input_info(true, &client->dev,  "failed to init upgrade mode\n");
		goto fail_upgrade;
	}
	usleep_range(1 * 1000, 1 * 1000);

	for (flash_addr = 0; flash_addr < size; ) {
		for (i = 0; i < page_sz/TC_SECTOR_SZ_READ; i++) {
			if (read_firmware_data(client, BT532_READ_FLASH,
						(u8 *)&verify_data[flash_addr], TC_SECTOR_SZ_READ) < 0) {
				input_err(true, &client->dev, "Failed to read firmare\n");
				goto fail_upgrade;
			}
			flash_addr += TC_SECTOR_SZ_READ;
		}
	}
#else
	for (flash_addr = 0; flash_addr < size; ) {
		for (i = 0; i < page_sz/TC_SECTOR_SZ; i++) {
			if (read_firmware_data(client,
				BT532_READ_FLASH,
				(u8*)&verify_data[flash_addr], TC_SECTOR_SZ) < 0) {
				input_err(true, &client->dev, "Failed to read firmare\n");

				goto fail_upgrade;
			}

			flash_addr += TC_SECTOR_SZ;
		}
	}
#endif
	/* verify */
	input_info(true, &client->dev, "verify firmware data\n");
	if (memcmp((u8 *)&firmware_data[0], (u8 *)&verify_data[0], size) == 0) {
		input_info(true, &client->dev, "upgrade finished\n");

		bt532_power_control(info, POWER_OFF);
		bt532_power_control(info, POWER_ON_SEQUENCE);

		if (!crc_check(info))
			goto fail_upgrade;

		if (verify_data){
			input_info(true, &client->dev, "vfree\n");
			vfree(verify_data);
			verify_data = NULL;
		}

		return true;
	}

fail_upgrade:
	bt532_power_control(info, POWER_OFF);

	if (retry_cnt++ < INIT_RETRY_CNT) {
		input_err(true, &client->dev, "upgrade failed : so retry... (%d)\n", retry_cnt);
		goto retry_upgrade;
	}

	if (verify_data){
		input_info(true, &client->dev, "vfree\n");
		vfree(verify_data);
	}

	input_info(true, &client->dev, "Failed to upgrade\n");

	return false;
}
#endif

static bool ts_hw_calibration(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	u16	chip_eeprom_info;
	int time_out = 0;

	input_info(true, &client->dev, "%s start\n", __func__);

	if (write_reg(client,
		BT532_TOUCH_MODE, 0x07) != I2C_SUCCESS)
		return false;
	usleep_range(10 * 1000, 10 * 1000);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	usleep_range(10 * 1000, 10 * 1000);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	msleep(50);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	usleep_range(10 * 1000, 10 * 1000);

	if (write_cmd(client,
		BT532_CALIBRATE_CMD) != I2C_SUCCESS)
		return false;

	if (write_cmd(client,
		BT532_CLEAR_INT_STATUS_CMD) != I2C_SUCCESS)
		return false;

	usleep_range(10 * 1000, 10 * 1000);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);

	/* wait for h/w calibration*/
	do {
		msleep(200);
		write_cmd(client,
				BT532_CLEAR_INT_STATUS_CMD);

		if (read_data(client,
			BT532_EEPROM_INFO,
			(u8 *)&chip_eeprom_info, 2) < 0)
			return false;

		zinitix_debug_msg("touch eeprom info = 0x%04X\r\n",
			chip_eeprom_info);
		if (!zinitix_bit_test(chip_eeprom_info, 0))
			break;

		if (time_out++ == 4){
			write_cmd(client, BT532_CALIBRATE_CMD);
			usleep_range(10 * 1000, 10 * 1000);
			write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
			input_err(true, &client->dev, "h/w calibration retry timeout.\n");
		}

		if (time_out++ > 10){
			input_err(true, &client->dev, "h/w calibration timeout.\n");
			break;
		}

	} while (1);

	write_reg(client, 0xc003, 0x0001);
	write_reg(client, 0xc104, 0x0001);
	usleep_range(100, 100);
	if (write_cmd(client,
		BT532_SAVE_CALIBRATION_CMD) != I2C_SUCCESS)
		return false;

	msleep(1100);
	write_reg(client, 0xc003, 0x0000);
	write_reg(client, 0xc104, 0x0000);

	return true;
}

static int ic_version_check(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	struct capa_info *cap = &(info->cap_info);
	int ret;
	u8 data[8] = {0};

	/* get chip information */
	ret = read_data(client, BT532_VENDOR_ID, (u8 *)&cap->vendor_id, 2);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail vendor id\n", __func__);
		goto error;
	}

	ret = read_data(client, BT532_MINOR_FW_VERSION, (u8 *)&cap->fw_minor_version, 2);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail fw_minor_version\n", __func__);
		goto error;
	}

	ret = read_data(client, BT532_CHIP_REVISION, data, 8);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail chip_revision\n", __func__);
		goto error;
	}

	cap->ic_revision = data[0] | (data[1] << 8);
	cap->fw_version = data[2] | (data[3] << 8);
	cap->reg_data_version = data[4] | (data[5] << 8);
	cap->hw_id = data[6] | (data[7] << 8);

error:
	return ret;
}

static int fw_update_work(struct bt532_ts_info *info, bool force_update)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct capa_info *cap = &(info->cap_info);
	int ret;
	bool need_update = false;
	const struct firmware *tsp_fw = NULL;
	char fw_path[MAX_FW_PATH];
	u16 chip_eeprom_info;
#ifdef TCLM_CONCEPT
	int restore_cal = 0;
#endif
	if (pdata->bringup){
		input_info(true, &info->client->dev, "%s: bringup\n", __func__);
		return 0;
	}

	snprintf(fw_path, MAX_FW_PATH, "%s", pdata->firmware_name);
	input_info(true, &info->client->dev,
		"%s: start\n", __func__);

	ret = request_firmware(&tsp_fw, fw_path, &(info->client->dev));
	if (ret < 0) {
		input_info(true, &info->client->dev,
			"%s: Firmware image %s not available\n", __func__, fw_path);
		goto fw_request_fail;
	}
	else
		info->fw_data = (unsigned char *)tsp_fw->data;

	need_update = ts_check_need_upgrade(info, cap->fw_version,
		cap->fw_minor_version, cap->reg_data_version, cap->hw_id);
	if (!need_update) {
		if (!crc_check(info))
			need_update = true;
	}

	if (need_update == true || force_update == true) {
		ret = ts_upgrade_firmware(info, info->fw_data, cap->ic_fw_size);
		if (!ret)
			input_err(true, &info->client->dev, "%s: failed fw update\n", __func__);
		
#ifdef TCLM_CONCEPT
		ret = sec_tclm_get_nvm_all(info->tdata);
		if (ret < 0) {
			input_info(true, &info->client->dev, "%s: sec_tclm_get_nvm_all error \n", __func__);
		}
		input_info(true, &info->client->dev, "%s: tune_fix_ver [%04X] afe_base [%04X]\n",
			__func__, info->tdata->nvdata.tune_fix_ver, info->tdata->afe_base);

		if (((info->tdata->nvdata.tune_fix_ver == 0xffff)||(info->tdata->afe_base > info->tdata->nvdata.tune_fix_ver))
				&& (info->tdata->tclm_level > TCLM_LEVEL_CLEAR_NV)) {
			/* tune version up case */
			sec_tclm_root_of_cal(info->tdata, CALPOSITION_TUNEUP);
			restore_cal = 1;
		} else if (info->tdata->tclm_level == TCLM_LEVEL_CLEAR_NV) {
			/* firmup case */
			sec_tclm_root_of_cal(info->tdata, CALPOSITION_FIRMUP);
			restore_cal = 1;
		}

		if (restore_cal == 1) {
			input_err(true, &info->client->dev, "%s: RUN OFFSET CALIBRATION\n", __func__);
			ret = sec_execute_tclm_package(info->tdata, 0);
			if (ret < 0) {
				input_err(true, &info->client->dev, "%s: sec_execute_tclm_package fail\n", __func__);
				goto fw_request_fail;
		}
		}

		sec_tclm_root_of_cal(info->tdata, CALPOSITION_NONE);
#endif

		ret = ic_version_check(info);
		if (ret < 0) {
			input_err(true, &info->client->dev, "%s: failed ic version check\n", __func__);
		}
	}

	if (read_data(info->client, BT532_EEPROM_INFO,
					(u8 *)&chip_eeprom_info, 2) < 0){
		ret = -1;
		goto fw_request_fail;
	}

#ifndef TCLM_CONCEPT
	if (zinitix_bit_test(chip_eeprom_info, 0)) /* hw calibration bit*/
	{
		if (ts_hw_calibration(info) == false){
			ret = -1;
			goto fw_request_fail;
		}
	}
#endif

fw_request_fail:
	if (tsp_fw)
		release_firmware(tsp_fw);
	return ret;
}

static bool init_touch(struct bt532_ts_info *info)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	u16 reg_val = 0;
	u8 data[6] = {0};
	int ret;

	/* get x,y data */
	read_data(info->client, BT532_TOTAL_NUMBER_OF_Y, data, 4);
	info->cap_info.x_node_num = data[2] | (data[3] << 8);
	info->cap_info.y_node_num = data[0] | (data[1] << 8);

	info->cap_info.MaxX= pdata->x_resolution;
	info->cap_info.MaxY = pdata->y_resolution;

	info->cap_info.total_node_num = info->cap_info.x_node_num * info->cap_info.y_node_num;
	info->cap_info.multi_fingers = MAX_SUPPORTED_FINGER_NUM;

	input_info(true, &info->client->dev, "node x %d, y %d  resolution x %d, y %d\n",
		info->cap_info.x_node_num, info->cap_info.y_node_num, info->cap_info.MaxX, info->cap_info.MaxY	);

	/* get fod information */
	write_cmd(info->client, 0x0A);
	ret = read_data(info->client, REG_FOD_MODE_VI_DATA_CH, info->fod_info_vi_trx, 2);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail fod channel info.\n", __func__);
	}

	ret = read_data(info->client, REG_FOD_MODE_VI_DATA_LEN, (u8 *)&info->fod_info_vi_data_len, 2);
	if (ret < 0) {
		input_err(true, &info->client->dev, "%s: fail fod data len.\n", __func__);
	}
	write_cmd(info->client, 0x0B);

	input_info(true, &info->client->dev, "%s: fod info %d,%d,%d\n", __func__,
		info->fod_info_vi_trx[1], info->fod_info_vi_trx[0], info->fod_info_vi_data_len);

#if ESD_TIMER_INTERVAL
	if (write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL) != I2C_SUCCESS)
		goto fail_init;

	read_data(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, (u8 *)&reg_val, 2);
#if defined(TSP_VERBOSE_DEBUG)
	input_info(true, &info->client->dev, "Esd timer register = %d\n", reg_val);
#endif
#endif
	if (!mini_init_touch(info))
		goto fail_init;

	return true;
fail_init:
	return false;
}

static bool mini_init_touch(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	struct bt532_ts_platform_data *pdata = info->pdata;
	int i;

	if (write_cmd(client, BT532_SWRESET_CMD) != I2C_SUCCESS) {
		input_info(true, &client->dev, "Failed to write reset command\n");

		goto fail_mini_init;
	}

	if (write_reg(client, BT532_TOUCH_MODE,
			info->touch_mode) != I2C_SUCCESS)
		goto fail_mini_init;

	/* cover_set */
	if (write_reg(client, BT532_COVER_CONTROL_REG, COVER_OPEN) != I2C_SUCCESS)
		goto fail_mini_init;

	if (info->flip_enable) {
		set_cover_type(info, info->flip_enable);
	}

	bt532_set_optional_mode(info, true);

	/* read garbage data */
	for (i = 0; i < 10; i++) {
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		usleep_range(10, 10);
	}

#if ESD_TIMER_INTERVAL
	if (write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL) != I2C_SUCCESS)
		goto fail_mini_init;

	esd_timer_start(CHECK_ESD_TIMER, info);
#if defined(TSP_VERBOSE_DEBUG)
	input_info(true, &client->dev, "Started esd timer\n");
#endif
#endif

	if (pdata->support_lpm_mode) {
		write_reg(info->client, ZT75XX_LPM_MODE_REG, 0);

		if (pdata->support_aod && !pdata->support_aot) {
			write_reg(info->client, ZT75XX_SET_AOD_W_REG, 0);
			write_reg(info->client, ZT75XX_SET_AOD_H_REG, 0);
			write_reg(info->client, ZT75XX_SET_AOD_X_REG, 0);
			write_reg(info->client, ZT75XX_SET_AOD_Y_REG, 0);
		}
	}

	if (info->sleep_mode) {
#if ESD_TIMER_INTERVAL
		esd_timer_stop(info);
#endif
		write_cmd(info->client, BT532_SLEEP_CMD);
		input_info(true, &misc_info->client->dev, "%s, sleep mode\n", __func__);
	} else {
		zinitix_set_grip_type(info, ONLY_EDGE_HANDLER);
	}

	input_info(true, &client->dev, "Successfully mini initialized\r\n");
	return true;

fail_mini_init:
	input_err(true, &client->dev, "Failed to initialize mini init\n");
/*	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	if (init_touch(info) == false) {
		input_err(true, &client->dev, "Failed to initialize\n");

		return false;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, info);
#if defined(TSP_VERBOSE_DEBUG)
	input_info(true, &client->dev, "Started esd timer\n");
#endif
#endif
	return true;*/
	return false;
}

static void clear_report_data(struct bt532_ts_info *info)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	int i;
	u8 reported = 0;
	char location[7] = "";

	if (pdata->support_touchkey) {
		for (i = 0; i < info->cap_info.button_num; i++) {
			if (info->button[i] == ICON_BUTTON_DOWN) {
				info->button[i] = ICON_BUTTON_UP;
				input_report_key(info->input_dev, BUTTON_MAPPING_KEY[i], 0);
				reported = true;
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				input_info(true, &client->dev, "Button up = %d\n", i);
#else
				input_info(true, &client->dev, "Button up\n");
#endif
			}
		}
		input_report_key(info->input_dev, BTN_TOUCH, 0);
	}

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		if (info->cur_coord[i].touch_status > FINGER_NONE) {
			input_mt_slot(info->input_dev, i);
#ifdef CONFIG_SEC_FACTORY
			input_report_abs(info->input_dev, ABS_MT_PRESSURE, 0);
#endif
			input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, 0);
			reported = true;
			if (!m_ts_debug_mode && TSP_NORMAL_EVENT_MSG) {
				location_detect(info, location, info->cur_coord[i].x, info->cur_coord[i].y);
	
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				input_info(true, &client->dev, "[RA] tID:%d loc:%s dd:%d,%d mc:%d tc:%d lx:%d ly:%d p:%d\n",
					i, location,
					info->cur_coord[i].x - info->pressed_x[i],
					info->cur_coord[i].y - info->pressed_y[i],
					info->move_count[i], info->finger_cnt1,
					info->cur_coord[i].x,
					info->cur_coord[i].y,
					info->cur_coord[i].palm_count);
#else
				input_info(true, &client->dev, "[RA] tID:%02d loc:%s dd:%d,%d mc:%d tc:%d p:%d\n",
					i, location,
					info->cur_coord[i].x - info->pressed_x[i],
					info->cur_coord[i].y - info->pressed_y[i],
					info->move_count[i], info->finger_cnt1,
					info->cur_coord[i].palm_count);
#endif
			}
		}
		memset(&info->old_coord[i], 0, sizeof(struct ts_coordinate));
		memset(&info->cur_coord[i], 0, sizeof(struct ts_coordinate));
		info->move_count[i] = 0;
	}

#ifdef GLOVE_MODE
	input_report_switch(info->input_dev, SW_GLOVE, false);
	info->glove_touch = 0;
#endif

	input_report_key(info->input_dev, BTN_PALM, false);
	info->palm_flag = 0;

	if (reported) {
		input_sync(info->input_dev);
	}

	info->finger_cnt1 = 0;
	info->check_multi = 0;
}

#define	PALM_REPORT_WIDTH	200
#define	PALM_REJECT_WIDTH	255

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
void trustedui_mode_on(void){
	input_info(true, &tui_tsp_info->client->dev, "%s, release all finger..", __func__);
	clear_report_data(tui_tsp_info);
	input_info(true, &tui_tsp_info->client->dev, "%s : esd timer disable", __func__);
#if ESD_TIMER_INTERVAL
	esd_timer_stop(tui_tsp_info);
	write_reg(tui_tsp_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(tui_tsp_info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif
}
EXPORT_SYMBOL(trustedui_mode_on);

void trustedui_mode_off(void){
	input_info(true, &tui_tsp_info->client->dev, "%s : esd timer enable", __func__);
#if ESD_TIMER_INTERVAL
	write_reg(tui_tsp_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
	esd_timer_start(CHECK_ESD_TIMER, tui_tsp_info);
#endif
}
EXPORT_SYMBOL(trustedui_mode_off);
#endif

void location_detect(struct bt532_ts_info *info, char *loc, int x, int y)
{
	memset(loc, 0x00, 7);
	strncpy(loc, "xy:", 3);
	if (x < info->pdata->area_edge)
		strncat(loc, "E.", 2);
	else if (x < (info->pdata->x_resolution - info->pdata->area_edge))
		strncat(loc, "C.", 2);
	else
		strncat(loc, "e.", 2);
	if (y < info->pdata->area_indicator)
		strncat(loc, "S", 1);
	else if (y < (info->pdata->y_resolution - info->pdata->area_navigation))
		strncat(loc, "C", 1);
	else
		strncat(loc, "N", 1);
}

static irqreturn_t bt532_touch_work(int irq, void *data)
{
	struct bt532_ts_info* info = (struct bt532_ts_info*)data;
	struct i2c_client *client = info->client;
	int i;
	u8 reported = false;
	u8 tid = 0;
	u8 ttype, tstatus;
	u16 x, y, z, maxX, maxY, sen_max;
	u16 st;
	u16 prox_data = 0; 
	u8 info_major_w = 0;
	u8 info_minor_w = 0;
	char noise_flag = -1;
	char flip_cover_flag = 0;
	
	char location[7] = "";
	int ret;
	char pos[5];
	char cur = 0;
	char old = 0;
	char tclm_buff[55];

	if (info->sleep_mode) {
		pm_wakeup_event(info->input_dev->dev.parent, 500);

		/* waiting for blsp block resuming, if not occurs i2c error */
		ret = wait_for_completion_interruptible_timeout(&info->resume_done, msecs_to_jiffies(500));
		if (ret == 0) {
			input_err(true, &info->client->dev, "%s: LPM: pm resume is not handled\n", __func__);
			return IRQ_HANDLED;
		} else if (ret < 0) {
			input_err(true, &info->client->dev, "%s: LPM: -ERESTARTSYS if interrupted, %d\n", __func__, ret);
			return IRQ_HANDLED;
		}
	}

	if (gpio_get_value(info->pdata->gpio_int)) {
		input_err(true, &client->dev, "Invalid interrupt\n");

		return IRQ_HANDLED;
	}

	if (down_trylock(&info->work_lock)) {
		input_err(true, &client->dev, "%s: Failed to occupy work lock\n", __func__);
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);

		return IRQ_HANDLED;
	}
#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
#endif

	if (info->work_state != NOTHING) {
		input_err(true, &client->dev, "%s: Other process occupied\n", __func__);
		usleep_range(DELAY_FOR_SIGNAL_DELAY, DELAY_FOR_SIGNAL_DELAY);

		if (!gpio_get_value(info->pdata->gpio_int)) {
			write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
			usleep_range(DELAY_FOR_SIGNAL_DELAY, DELAY_FOR_SIGNAL_DELAY);
		}

		goto out;
	}

	info->work_state = NORMAL;

	if (ts_read_coord(info) == false) { /* maybe desirable reset */
		input_err(true, &client->dev, "%s: Failed to read info coord\n", __func__);
		bt532_power_control(info, POWER_OFF);
		bt532_power_control(info, POWER_ON_SEQUENCE);

		clear_report_data(info);
		mini_init_touch(info);

		goto out;
	}

	if (info->touch_info[0].byte00.value.eid == CUSTOM_EVENT)
		goto out;

	reported = false;
#if 0 /* support_touchkey, need button state*/
	if (pdata->support_touchkey){
		if (zinitix_bit_test(info->touch_info.status, BIT_ICON_EVENT)) {
			if (read_data(info->client, BT532_ICON_STATUS_REG,
				(u8 *)(&info->icon_event_reg), 2) < 0) {
				input_err(true, &client->dev, "Failed to read button info\n");
				write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);

				goto out;
			}

			for (i = 0; i < info->cap_info.button_num; i++) {
				if (zinitix_bit_test(info->icon_event_reg,
										(BIT_O_ICON0_DOWN + i))) {
					info->button[i] = ICON_BUTTON_DOWN;
					input_report_key(info->input_dev, BUTTON_MAPPING_KEY[i], 1);
					reported = true;
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
					input_info(true, &client->dev, "Button down = %d\n", i);
#else
					input_info(true, &client->dev, "Button down\n");
#endif
				}
			}

			for (i = 0; i < info->cap_info.button_num; i++) {
				if (zinitix_bit_test(info->icon_event_reg,
										(BIT_O_ICON0_UP + i))) {
					info->button[i] = ICON_BUTTON_UP;
					input_report_key(info->input_dev, BUTTON_MAPPING_KEY[i], 0);
					reported = true;
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
					input_info(true, &client->dev, "Button up = %d\n", i);
#else
					input_info(true, &client->dev, "Button up\n");
#endif
				}
			}
		}
	}
#endif

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		info->old_coord[i] = info->cur_coord[i];
		memset(&info->cur_coord[i], 0, sizeof(struct ts_coordinate));
	}

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		ttype = (info->touch_info[i].byte06.value.touch_type23 << 2) | (info->touch_info[i].byte07.value.touch_type01);
		tstatus = info->touch_info[i].byte00.value.touch_status;

		if (tstatus == FINGER_NONE && ttype != TOUCH_PROXIMITY) 
			continue;

		tid = info->touch_info[i].byte00.value.tid;

		info->cur_coord[tid].id = tid;
		info->cur_coord[tid].touch_status = tstatus;
		info->cur_coord[tid].x = (info->touch_info[i].byte01.value.x_coord_h << 4) | (info->touch_info[i].byte03.value.x_coord_l);
		info->cur_coord[tid].y = (info->touch_info[i].byte02.value.y_coord_h << 4) | (info->touch_info[i].byte03.value.y_coord_l);
		info->cur_coord[tid].z = info->touch_info[i].byte06.value.z_value;
		info->cur_coord[tid].ttype = ttype;
		info->cur_coord[tid].major = info->touch_info[i].byte04.value_u8bit;
		info->cur_coord[tid].minor = info->touch_info[i].byte05.value_u8bit;
		info->cur_coord[tid].noise = info->touch_info[i].byte08.value_u8bit;
		info->cur_coord[tid].max_sense= info->touch_info[i].byte09.value_u8bit;

		if (!info->cur_coord[tid].palm && (info->cur_coord[tid].ttype == TOUCH_PALM))
			info->cur_coord[tid].palm_count++;
		info->cur_coord[tid].palm = (info->cur_coord[tid].ttype == TOUCH_PALM);
		if (info->cur_coord[tid].palm)
			info->palm_flag |= (1 << tid);
		else
			info->palm_flag &= ~(1 << tid);

		if (info->cur_coord[tid].z <= 0)
			info->cur_coord[tid].z = 1;
	}

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		if((info->cur_coord[i].ttype == TOUCH_PROXIMITY)
			&& (info->pdata->support_ear_detect)) {
			if (read_data(info->client, ZT75XX_PROXIMITY_DETECT, (u8 *)&prox_data, 2) < 0)
				input_err(true, &client->dev, "%s: fail to read proximity detect reg\n", __func__);

			info->hover_event = prox_data;

			input_info(true, &client->dev, "PROXIMITY DETECT. LVL = %d \n", prox_data);
			input_report_abs(info->input_dev_proximity, ABS_MT_CUSTOM, prox_data);
			input_sync(info->input_dev_proximity);
			break;
		}
	}

	noise_flag = -1;
	flip_cover_flag = 0;

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		if (info->cur_coord[i].touch_status == FINGER_NONE)
			continue;

		if ((noise_flag == -1) && (info->old_coord[i].noise != info->cur_coord[i].noise)) {
			noise_flag = info->cur_coord[i].noise;
			input_info(true, &client->dev, "NOISE MODE %s [%d]\n", noise_flag > 0 ? "ON":"OFF", noise_flag);
		}

		if (flip_cover_flag == 0) {
			if (info->old_coord[i].ttype != TOUCH_FLIP_COVER && info->cur_coord[i].ttype == TOUCH_FLIP_COVER) {
				flip_cover_flag = 1;
				input_info(true, &client->dev, "FLIP COVER MODE ON\n");
			}

			if (info->old_coord[i].ttype == TOUCH_FLIP_COVER && info->cur_coord[i].ttype != TOUCH_FLIP_COVER) {
				flip_cover_flag = 1;
				input_info(true, &client->dev, "FLIP COVER MODE OFF\n");
			}
		}

		if (info->old_coord[i].ttype != info->cur_coord[i].ttype) {
			if (info->cur_coord[i].touch_status == FINGER_PRESS)
				snprintf(pos, 5, "P");
			else if (info->cur_coord[i].touch_status == FINGER_MOVE)
				snprintf(pos, 5, "M");
			else 
				snprintf(pos, 5, "R");

			if (info->cur_coord[i].ttype == TOUCH_PALM)
				cur = 'P';
			else if (info->cur_coord[i].ttype == TOUCH_GLOVE)
				cur = 'G';
			else 
				cur = 'N';

			if (info->old_coord[i].ttype == TOUCH_PALM)
				old = 'P';
			else if (info->old_coord[i].ttype == TOUCH_GLOVE)
				old = 'G';
			else
				old = 'N';

			if (cur != old) 
				input_info(true, &client->dev, "tID:%d ttype(%c->%c) : %s\n", i, old, cur, pos);
		}

		if ((info->cur_coord[i].touch_status == FINGER_PRESS || info->cur_coord[i].touch_status == FINGER_MOVE)) {
			x = info->cur_coord[i].x;
			y = info->cur_coord[i].y;
			z = info->cur_coord[i].z;
			info_major_w = info->cur_coord[i].major;
			info_minor_w = info->cur_coord[i].minor;
			sen_max = info->cur_coord[i].max_sense;

			maxX = info->cap_info.MaxX;
			maxY = info->cap_info.MaxY;

			if (x > maxX || y > maxY) {
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				input_err(true, &client->dev,
							"Invalid coord %d : x=%d, y=%d\n", i, x, y);
#endif
				continue;
			}

			st = sen_max & 0x0F;
			if (st < 1)
				st = 1;
			
			if ((info->cur_coord[i].touch_status == FINGER_PRESS) && (info->cur_coord[i].touch_status != info->old_coord[i].touch_status)) {
				info->pressed_x[i] = x; /*for getting coordinates of pressed point*/
				info->pressed_y[i] = y;
				info->finger_cnt1++;

				if ((info->finger_cnt1 > 4) && (info->check_multi == 0)) {
					info->check_multi = 1;
					info->multi_count++;
					input_info(true, &client->dev,"data : pn=%d mc=%d \n", info->finger_cnt1, info->multi_count);
				}

				location_detect(info, location, x, y);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				input_info(true, &client->dev, "[P] tID:%d.%d x:%d y:%d z:%d(st:%d) max:%d major:%d minor:%d loc:%s tc:%d touch_type:%x noise:%x\n",
							i, (info->input_dev->mt->trkid) & TRKID_MAX, x, y, z, st, sen_max, info_major_w,
							info_minor_w, location, info->finger_cnt1, info->cur_coord[i].ttype, info->cur_coord[i].noise);
#else
				input_info(true, &client->dev, "[P] tID:%d.%d z:%d(st:%d) max:%d major:%d minor:%d loc:%s tc:%d touch_type:%x noise:%x\n",
							i, (info->input_dev->mt->trkid) & TRKID_MAX, z, st, sen_max, info_major_w,
							info_minor_w, location, info->finger_cnt1, info->cur_coord[i].ttype, info->cur_coord[i].noise);
#endif
			} else if (info->cur_coord[i].touch_status == FINGER_MOVE) {
				info->move_count[i]++;
			}

			input_mt_slot(info->input_dev, i);
			input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, 1);

			input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, (u32)info_major_w);

#ifdef CONFIG_SEC_FACTORY
			input_report_abs(info->input_dev, ABS_MT_PRESSURE, (u32)st);
#endif
			input_report_abs(info->input_dev, ABS_MT_WIDTH_MAJOR, (u32)info_major_w);
			input_report_abs(info->input_dev, ABS_MT_TOUCH_MINOR, info_minor_w);

			input_report_abs(info->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(info->input_dev, ABS_MT_POSITION_Y, y);
			input_report_key(info->input_dev, BTN_PALM, info->palm_flag);

			input_report_key(info->input_dev, BTN_TOUCH, 1);
		} else if (info->cur_coord[i].touch_status == FINGER_RELEASE) {
			u16 fw_version = (info->cap_info.fw_version & 0xf) << 12
							| (info->cap_info.fw_minor_version & 0xf) << 8
							| (info->cap_info.reg_data_version & 0xff);

			location_detect(info, location, info->cur_coord[i].x, info->cur_coord[i].y);

#if IS_ENABLED(CONFIG_INPUT_TOUCHSCREEN_TCLMV2)
			if (info->tdata && info->tdata->tclm_string)
				snprintf(tclm_buff, sizeof(tclm_buff), "C%02XT%04X.%4s%s Cal_flag:%d fail_cnt:%d",
						info->tdata->nvdata.cal_count, info->tdata->nvdata.tune_fix_ver,
						info->tdata->tclm_string[info->tdata->nvdata.cal_position].f_name,
						(info->tdata->tclm_level == TCLM_LEVEL_LOCKDOWN) ? ".L" : " ",
						info->tdata->nvdata.cal_fail_falg, info->tdata->nvdata.cal_fail_cnt);
			else
				snprintf(tclm_buff, sizeof(tclm_buff), "TCLM data is empty");
#else
			snprintf(tclm_buff, sizeof(tclm_buff), "");
#endif

			if (info->cur_coord[i].palm_count > 0) {
				input_info(true, &client->dev, "tID:%d ttype(P->N) : R\n", i);
			}

#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			input_info(true, &client->dev,
						"[R] tID:%d loc:%s dd:%d,%d mc:%d tc:%d lx:%d ly:%d p:%d v:%04X (%s)\n",
						i, location,
						info->cur_coord[i].x - info->pressed_x[i],
						info->cur_coord[i].y - info->pressed_y[i],
						info->move_count[i], info->finger_cnt1,
						info->cur_coord[i].x,
						info->cur_coord[i].y,
						info->cur_coord[i].palm_count, fw_version, tclm_buff);
#else
			input_info(true, &client->dev,
						"[R] tID:%02d loc:%s dd:%d,%d mc:%d tc:%d p:%d v:%04X (%s)\n",
						i, location,
						info->cur_coord[i].x - info->pressed_x[i],
						info->cur_coord[i].y - info->pressed_y[i],
						info->move_count[i], info->finger_cnt1,
						info->cur_coord[i].palm_count, fw_version, tclm_buff);
#endif
			if (info->finger_cnt1 > 0)
				info->finger_cnt1--;

			if (info->finger_cnt1 == 0) {
				input_report_key(info->input_dev, BTN_TOUCH, 0);
				info->check_multi = 0;
			}

			input_mt_slot(info->input_dev, i);
			input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, 0);

			info->palm_flag &= ~(1 << i);
			input_report_key(info->input_dev, BTN_PALM, info->palm_flag);

			info->move_count[i] = 0;
			memset(&info->cur_coord[i], 0, sizeof(struct ts_coordinate));
		}
	}

	input_sync(info->input_dev);

out:
	if (info->work_state == NORMAL) {
#if ESD_TIMER_INTERVAL
		esd_timer_start(CHECK_ESD_TIMER, info);
#endif
		info->work_state = NOTHING;
	}

	up(&info->work_lock);

	return IRQ_HANDLED;
}

#ifdef CONFIG_INPUT_ENABLED
static int  bt532_ts_open(struct input_dev *dev)
{
	struct bt532_ts_info *info = misc_info;
	u8 prev_work_state;

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	msleep(100);
	if (TRUSTEDUI_MODE_TUI_SESSION & trustedui_get_current_mode()){
		tui_force_close(1);
		msleep(100);
		if (TRUSTEDUI_MODE_TUI_SESSION & trustedui_get_current_mode()){
			trustedui_clear_mask(TRUSTEDUI_MODE_VIDEO_SECURED|TRUSTEDUI_MODE_INPUT_SECURED);
			trustedui_set_mode(TRUSTEDUI_MODE_OFF);
		}
	}
#endif // CONFIG_TRUSTONIC_TRUSTED_UI

	if (info == NULL)
		return 0;

	if (!info->info_work_done) {
		input_err(true, &info->client->dev, "%s not finished info work\n", __func__);
		return 0;
	}

	input_info(true, &misc_info->client->dev, "%s, %d \n", __func__, __LINE__);

	if ((info->pdata->support_lpm_mode) && (info->sleep_mode)) {
		down(&info->work_lock);
		prev_work_state = info->work_state;
		info->work_state = SLEEP_MODE_OUT;

		input_info(true, &misc_info->client->dev, "%s, wake up\n", __func__);
		write_cmd(info->client, 0x0A);
		write_cmd(info->client, BT532_WAKEUP_CMD);
		info->sleep_mode = 0;

		bt532_set_optional_mode(info, false);
		write_cmd(info->client, 0x0B);
		info->work_state = prev_work_state;
		up(&info->work_lock);

#if ESD_TIMER_INTERVAL
		esd_timer_start(CHECK_ESD_TIMER, info);
#endif
		if (device_may_wakeup(&info->client->dev))
			disable_irq_wake(info->irq);
	} else {
		down(&info->work_lock);
		if (info->work_state != RESUME
			&& info->work_state != EALRY_SUSPEND) {
			input_info(true, &misc_info->client->dev, "invalid work proceedure (%d)\r\n",
				info->work_state);
			up(&info->work_lock);
			return 0;
		}

		bt532_power_control(info, POWER_ON_SEQUENCE);

		crc_check(info);

		if (mini_init_touch(info) == false)
			goto fail_late_resume;
		enable_irq(info->irq);
		info->work_state = NOTHING;

		if (g_ta_connected)
			bt532_set_ta_status(info);

		up(&info->work_lock);
		zinitix_debug_msg("bt532_ts_open--\n");
		return 0;

fail_late_resume:
		input_info(true, &misc_info->client->dev, "failed to late resume\n");
		enable_irq(info->irq);
		info->work_state = NOTHING;
		up(&info->work_lock);
	}

	return 0;
}

static void bt532_ts_close(struct input_dev *dev)
{
	struct bt532_ts_info *info = misc_info;
	int i;
	u8 prev_work_state;

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	msleep(100);
	if (TRUSTEDUI_MODE_TUI_SESSION & trustedui_get_current_mode()){
		tui_force_close(1);
		msleep(100);
		if (TRUSTEDUI_MODE_TUI_SESSION & trustedui_get_current_mode()){
			trustedui_clear_mask(TRUSTEDUI_MODE_VIDEO_SECURED|TRUSTEDUI_MODE_INPUT_SECURED);
			trustedui_set_mode(TRUSTEDUI_MODE_OFF);
		}
	}
#endif // CONFIG_TRUSTONIC_TRUSTED_UI

	if (info == NULL)
		return;

	if (!info->info_work_done) {
		input_err(true, &info->client->dev, "%s not finished info work\n", __func__);
		return;
	}

	input_info(true, &misc_info->client->dev,
			"%s, spay:%d aod:%d aot:%d singletap:%d prox:%d pocket:%d ed:%d\n",
			__func__, info->spay_enable, info->aod_enable,
			info->aot_enable, info->singletap_enable, info->prox_power_off,
			info->pocket_enable, info->ed_enable);

#ifdef TCLM_CONCEPT
	sec_tclm_debug_info(info->tdata);
#endif

#if ESD_TIMER_INTERVAL
	flush_work(&info->tmr_work);
#endif

	if (((info->pdata->support_lpm_mode) &&
		(info->spay_enable || info->aod_enable || info->aot_enable || info->singletap_enable))
		|| info->pocket_enable || info->ed_enable || info->fod_enable || info->fod_lp_mode){
		down(&info->work_lock);
		prev_work_state = info->work_state;
		info->work_state = SLEEP_MODE_IN;
		input_info(true, &misc_info->client->dev, "%s, sleep mode\n", __func__);

#if ESD_TIMER_INTERVAL
		esd_timer_stop(info);
#endif

		if (info->prox_power_off && info->aot_enable)
			zinitix_bit_clr(info->lpm_mode, BIT_EVENT_AOT);

		input_info(true, &misc_info->client->dev,
				"%s: write lpm_mode 0x%02x (spay:%d, aod:%d, singletap:%d, aot:%d, fod:%d, fod_lp:%d)\n",
				__func__, info->lpm_mode,
				(info->lpm_mode & (1 << BIT_EVENT_SPAY)) ? 1 : 0,
				(info->lpm_mode & (1 << BIT_EVENT_AOD)) ? 1 : 0,
				(info->lpm_mode & (1 << BIT_EVENT_SINGLE_TAP)) ? 1 : 0,
				(info->lpm_mode & (1 << BIT_EVENT_AOT)) ? 1 : 0,
				info->fod_enable, info->fod_lp_mode);

		write_cmd(info->client, 0x0A);
		if (write_reg(info->client, ZT75XX_LPM_MODE_REG, info->lpm_mode) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "%s, fail lpm mode set\n", __func__);

		write_cmd(info->client, BT532_SLEEP_CMD);
		info->sleep_mode = 1;

		if (info->prox_power_off && info->aot_enable)
			zinitix_bit_set(info->lpm_mode, BIT_EVENT_AOT);

		/* clear garbage data */
		for (i = 0; i < 2; i++) {
			usleep_range(10 * 1000, 10 * 1000);
			write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
		}
		clear_report_data(info);

		write_cmd(info->client, 0x0B);
		info->work_state = prev_work_state;
		if (device_may_wakeup(&info->client->dev))
			enable_irq_wake(info->irq);
	} else {
		disable_irq(info->irq);
		down(&info->work_lock);
		if (info->work_state != NOTHING) {
			input_info(true, &misc_info->client->dev, "invalid work proceedure (%d)\r\n",
				info->work_state);
			up(&info->work_lock);
			enable_irq(info->irq);
			return;
		}
		info->work_state = EALRY_SUSPEND;
	
		clear_report_data(info);

#if ESD_TIMER_INTERVAL
		/*write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);*/
		esd_timer_stop(info);
#endif

		bt532_power_control(info, POWER_OFF);
	}

	if (info->prox_power_off) {
		input_report_key(info->input_dev, KEY_INT_CANCEL, 1);
		input_sync(info->input_dev);
		input_report_key(info->input_dev, KEY_INT_CANCEL, 0);
		input_sync(info->input_dev);
	}

	info->prox_power_off = 0;

	zinitix_debug_msg("bt532_ts_close--\n");
	up(&info->work_lock);
	return;
}
#endif	/* CONFIG_INPUT_ENABLED */

#if defined(SEC_FACTORY_TEST) || defined(USE_MISC_DEVICE)
static u16 ts_get_touch_reg(u16 addr)
{
	int ret = 1;
	u16 reg_value;

	disable_irq(misc_info->irq);

	down(&misc_info->work_lock);
	if (misc_info->work_state != NOTHING) {
		input_info(true, &misc_info->client->dev, "other process occupied.. (%d)\n",
			misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);
		return -1;
	}
	misc_info->work_state = SET_MODE;

	write_reg(misc_info->client, 0x0A, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);
	write_reg(misc_info->client, 0x0A, 0x0A);

	ret = read_data(misc_info->client, addr, (u8 *)&reg_value, 2);
	if (ret < 0) {
		input_err(true, &misc_info->client->dev,"%s: fail read touch reg\n", __func__);
	}

	misc_info->work_state = NOTHING;
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);

	return reg_value;
}

static void ts_set_touch_reg(u16 addr, u16 value)
{
	disable_irq(misc_info->irq);

	down(&misc_info->work_lock);
	if (misc_info->work_state != NOTHING) {
		input_info(true, &misc_info->client->dev, "other process occupied.. (%d)\n",
			misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);
		return;
	}
	misc_info->work_state = SET_MODE;

	write_reg(misc_info->client, 0x0A, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);
	write_reg(misc_info->client, 0x0A, 0x0A);

	if (write_reg(misc_info->client, addr, value) != I2C_SUCCESS)
		input_err(true, &misc_info->client->dev,"%s: fail write touch reg\n", __func__);

	misc_info->work_state = NOTHING;
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);
}

static int ts_set_touchmode(u16 value)
{
	int i, ret = 1;
	int retry_cnt = 0;
	struct capa_info *cap = &(misc_info->cap_info);

	disable_irq(misc_info->irq);

	down(&misc_info->work_lock);
	if (misc_info->work_state != NOTHING) {
		input_info(true, &misc_info->client->dev, "other process occupied.. (%d)\n",
			misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);
		return -1;
	}

retry_ts_set_touchmode:
	//wakeup cmd
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);

	misc_info->work_state = SET_MODE;

	if (value == TOUCH_SEC_MODE)
		misc_info->touch_mode = TOUCH_POINT_MODE;
	else
		misc_info->touch_mode = value;

	input_info(true, &misc_info->client->dev, "[zinitix_touch] tsp_set_testmode, "
		"touchkey_testmode = %d\r\n", misc_info->touch_mode);

	if (!((misc_info->touch_mode == TOUCH_POINT_MODE) || 
		(misc_info->touch_mode == TOUCH_SENTIVITY_MEASUREMENT_MODE))) {
		if (write_reg(misc_info->client, BT532_DELAY_RAW_FOR_HOST,
			RAWDATA_DELAY_FOR_HOST) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "Fail to set BT532_DELAY_RAW_FOR_HOST.\r\n");
	}

	if (write_reg(misc_info->client, BT532_TOUCH_MODE,
			misc_info->touch_mode) != I2C_SUCCESS)
		input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
				"Fail to set ZINITX_TOUCH_MODE %d.\r\n", misc_info->touch_mode);

	input_info(true, &misc_info->client->dev, "%s: tsp_set_testmode. write regiter end \n", __func__);

	ret = read_data(misc_info->client, BT532_TOUCH_MODE, (u8 *)&cap->current_touch_mode, 2);
	if (ret < 0) {
		input_err(true, &misc_info->client->dev,"%s: fail touch mode read\n", __func__);
		goto out;
	}

	if (cap->current_touch_mode != misc_info->touch_mode) {
		if (retry_cnt < 1) {
			retry_cnt++;
			goto retry_ts_set_touchmode;
		}
		input_info(true, &misc_info->client->dev, "%s: fail to set touch_mode %d (current_touch_mode %d).\n",
				__func__, misc_info->touch_mode, cap->current_touch_mode);
		ret = -1;
		goto out;
	}

	/* clear garbage data */
	for (i = 0; i < 10; i++) {
		usleep_range(20 * 1000, 20 * 1000);
		write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
	}
	input_info(true, &misc_info->client->dev, "%s: tsp_set_testmode. garbage data end \n", __func__);

out:
	misc_info->work_state = NOTHING;
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);
	return ret;
}
#endif

static int ts_set_touchmode3(u16 value)
{
	int i;

	disable_irq(misc_info->irq);

	down(&misc_info->work_lock);
	if (misc_info->work_state != NOTHING) {
		input_info(true, &misc_info->client->dev, "other process occupied.. (%d)\n",
			misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);
		return -1;
	}
	//wakeup cmd
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);

	if (misc_info->touch_mode == TOUCH_POINT_MODE) {
		/* factory data */
		read_data(misc_info->client, ZT75XX_MUTUAL_AMP_V_SEL, (u8 *)&misc_info->cap_info.mutual_amp_v_sel, 2);
		read_data(misc_info->client, ZT75XX_SX_AMP_V_SEL, (u8 *)&misc_info->cap_info.sx_amp_v_sel, 2);
		read_data(misc_info->client, ZT75XX_SX_SUB_V_SEL, (u8 *)&misc_info->cap_info.sx_sub_v_sel, 2);
		read_data(misc_info->client, ZT75XX_SY_AMP_V_SEL, (u8 *)&misc_info->cap_info.sy_amp_v_sel, 2);
		read_data(misc_info->client, ZT75XX_SY_SUB_V_SEL, (u8 *)&misc_info->cap_info.sy_sub_v_sel, 2);
		read_data(misc_info->client, BT532_AFE_FREQUENCY, (u8 *)&misc_info->cap_info.afe_frequency, 2);
		read_data(misc_info->client, BT532_DND_SHIFT_VALUE, (u8 *)&misc_info->cap_info.shift_value, 2);
	}
	misc_info->work_state = SET_MODE;

	if (value == TOUCH_RXSHORT_MODE) {
		if (write_reg(misc_info->client, ZT75XX_SY_AMP_V_SEL,
						SEC_SY_AMP_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SY_AMP_V_SEL %d.\n", SEC_SY_AMP_V_SEL);
		if (write_reg(misc_info->client, ZT75XX_SY_SUB_V_SEL,
						SEC_SY_SUB_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SY_SUB_V_SEL %d.\n", SEC_SY_SUB_V_SEL);
		if (write_reg(misc_info->client, BT532_DND_N_COUNT,
						SEC_SHORT_N_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_N_COUNT %d.\n", SEC_SHORT_N_COUNT);
		if (write_reg(misc_info->client, BT532_DND_U_COUNT,
						SEC_SHORT_U_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_U_COUNT %d.\n", SEC_SHORT_U_COUNT);
	}
	else if (value == TOUCH_TXSHORT_MODE) {
		if (write_reg(misc_info->client, ZT75XX_SX_AMP_V_SEL,
						SEC_SX_AMP_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SX_AMP_V_SEL %d.\n", SEC_SX_AMP_V_SEL);
		if (write_reg(misc_info->client, ZT75XX_SX_SUB_V_SEL,
						SEC_SX_SUB_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SX_SUB_V_SEL %d.\n", SEC_SX_SUB_V_SEL);
		if (write_reg(misc_info->client, BT532_DND_N_COUNT,
						SEC_SHORT_N_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_N_COUNT %d.\n", SEC_SHORT_N_COUNT);
		if (write_reg(misc_info->client, BT532_DND_U_COUNT,
						SEC_SHORT_U_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_U_COUNT %d.\n", SEC_SHORT_U_COUNT);
	}
	else if (misc_info->touch_mode == TOUCH_RXSHORT_MODE || misc_info->touch_mode == TOUCH_TXSHORT_MODE ) {
		if (write_reg(misc_info->client, ZT75XX_MUTUAL_AMP_V_SEL,
						misc_info->cap_info.mutual_amp_v_sel) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to reset ZT75XX_MUTUAL_AMP_V_SEL %d.\n",
					misc_info->cap_info.mutual_amp_v_sel);
		if (write_reg(misc_info->client, ZT75XX_SY_AMP_V_SEL,
						misc_info->cap_info.sy_amp_v_sel) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to reset ZT75XX_SY_AMP_V_SEL %d.\n",
					misc_info->cap_info.sy_amp_v_sel);
		if (write_reg(misc_info->client, ZT75XX_SY_SUB_V_SEL,
						misc_info->cap_info.sy_sub_v_sel) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to reset ZT75XX_SY_SUB_V_SEL %d.\n",
					misc_info->cap_info.sy_sub_v_sel);
		if (write_reg(misc_info->client, ZT75XX_SX_AMP_V_SEL,
						misc_info->cap_info.sx_amp_v_sel) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to reset ZT75XX_SX_AMP_V_SEL %d.\n",
					misc_info->cap_info.sx_amp_v_sel);
		if (write_reg(misc_info->client, ZT75XX_SX_SUB_V_SEL,
						misc_info->cap_info.sx_sub_v_sel) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to reset ZT75XX_SX_SUB_V_SEL %d.\n",
					misc_info->cap_info.sx_sub_v_sel);
		if (write_reg(misc_info->client, BT532_DND_SHIFT_VALUE,
						misc_info->cap_info.shift_value) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to reset BT532_DND_SHIFT_VALUE %d.\n",
					misc_info->cap_info.shift_value);
		if (write_reg(misc_info->client, BT532_AFE_FREQUENCY,
						misc_info->cap_info.afe_frequency) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to reset BT532_AFE_FREQUENCY %d.\n",
					misc_info->cap_info.afe_frequency);
	}
	if (value == TOUCH_SEC_MODE)
		misc_info->touch_mode = TOUCH_POINT_MODE;
	else
		misc_info->touch_mode = value;

	input_info(true, &misc_info->client->dev, "[zinitix_touch] tsp_set_testmode, "
		"touchkey_testmode = %d\r\n", misc_info->touch_mode);

	if (misc_info->touch_mode != TOUCH_POINT_MODE) {
			if (write_reg(misc_info->client, BT532_DELAY_RAW_FOR_HOST,
				RAWDATA_DELAY_FOR_HOST) != I2C_SUCCESS)
				input_info(true, &misc_info->client->dev, "Fail to set BT532_DELAY_RAW_FOR_HOST.\r\n");
	}

	if (write_reg(misc_info->client, BT532_TOUCH_MODE,
				misc_info->touch_mode) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set ZINITX_TOUCH_MODE %d.\r\n", misc_info->touch_mode);

	/* clear garbage data */
	for (i = 0; i < 10; i++) {
		usleep_range(20 * 1000, 20 * 1000);
		write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
	}

	misc_info->work_state = NOTHING;
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);
	return 1;
}

static int ts_set_self_sat_touchmode(u16 value)
{
	int i;

	disable_irq(misc_info->irq);

	down(&misc_info->work_lock);
	if (misc_info->work_state != NOTHING) {
		input_info(true, &misc_info->client->dev, "other process occupied.. (%d)\n",
			misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);
		return -1;
	}
	//wakeup cmd
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);

	if (value == TOUCH_SELF_DND_MODE) {
		if (write_reg(misc_info->client, ZT75XX_SY_SAT_FREQUENCY,
						SEC_SY_SAT_FREQUENCY)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SY_AMP_V_SEL %d.\n", SEC_SY_SAT_FREQUENCY);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT_N_COUNT,
						SEC_SY_SAT_N_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SY_SUB_V_SEL %d.\n", SEC_SY_SAT_N_COUNT);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT_U_COUNT,
						SEC_SY_SAT_U_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_N_COUNT %d.\n", SEC_SY_SAT_U_COUNT);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT_RBG_SEL,
						SEC_SY_SAT_RBG_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_U_COUNT %d.\n", SEC_SY_SAT_RBG_SEL);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT_AMP_V_SEL,
						SEC_SY_SAT_AMP_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_N_COUNT %d.\n", SEC_SY_SAT_AMP_V_SEL);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT_SUB_V_SEL,
						SEC_SY_SAT_SUB_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_U_COUNT %d.\n", SEC_SY_SAT_SUB_V_SEL);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT2_FREQUENCY,
						SEC_SY_SAT2_FREQUENCY)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_N_COUNT %d.\n", SEC_SY_SAT2_FREQUENCY);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT2_N_COUNT,
						SEC_SY_SAT2_N_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_U_COUNT %d.\n", SEC_SY_SAT2_N_COUNT);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT2_U_COUNT,
						SEC_SY_SAT2_U_COUNT)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_N_COUNT %d.\n", SEC_SY_SAT2_U_COUNT);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT2_RBG_SEL,
						SEC_SY_SAT2_RBG_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_U_COUNT %d.\n", SEC_SY_SAT2_RBG_SEL);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT2_AMP_V_SEL,
						SEC_SY_SAT2_AMP_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_N_COUNT %d.\n", SEC_SY_SAT2_AMP_V_SEL);

		if (write_reg(misc_info->client, ZT75XX_SY_SAT2_SUB_V_SEL,
						SEC_SY_SAT2_SUB_V_SEL)!=I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set SEC_SHORT_U_COUNT %d.\n", SEC_SY_SAT2_SUB_V_SEL);
	}

	if (value == TOUCH_SEC_MODE)
		misc_info->touch_mode = TOUCH_POINT_MODE;
	else
		misc_info->touch_mode = value;

	input_info(true, &misc_info->client->dev, "[zinitix_touch] ts_set_self_sat_touchmode, "
		"touchkey_testmode = %d\r\n", misc_info->touch_mode);

	if (misc_info->touch_mode != TOUCH_POINT_MODE) {
			if (write_reg(misc_info->client, BT532_DELAY_RAW_FOR_HOST,
				RAWDATA_DELAY_FOR_HOST) != I2C_SUCCESS)
				input_info(true, &misc_info->client->dev, "Fail to set BT532_DELAY_RAW_FOR_HOST.\r\n");
	}

	if (write_reg(misc_info->client, BT532_TOUCH_MODE,
				misc_info->touch_mode) != I2C_SUCCESS)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] TEST Mode : "
					"Fail to set ZINITX_TOUCH_MODE %d.\r\n", misc_info->touch_mode);

	/* clear garbage data */
	for (i = 0; i < 10; i++) {
		usleep_range(20 * 1000, 20 * 1000);
		write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
	}

	misc_info->work_state = NOTHING;
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);
	return 1;
}

#if defined(SEC_FACTORY_TEST) || defined(USE_MISC_DEVICE)
static int ts_upgrade_sequence(struct bt532_ts_info *info, const u8 *firmware_data, int restore_cal)
{
	int ret = 0;
	disable_irq(misc_info->irq);
	down(&misc_info->work_lock);
	misc_info->work_state = UPGRADE;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
#endif
	clear_report_data(misc_info);

	input_info(true, &misc_info->client->dev, "start upgrade firmware\n");
	if (ts_upgrade_firmware(misc_info,
		firmware_data,
		misc_info->cap_info.ic_fw_size) == false) {
		ret = -1;
		goto out;
	}

	if (ic_version_check(misc_info) < 0)
			input_err(true, &misc_info->client->dev, "%s: failed ic version check\n", __func__);

#ifdef TCLM_CONCEPT
	if (restore_cal == 1) {
		input_err(true, &info->client->dev, "%s: RUN OFFSET CALIBRATION\n", __func__);
		ret = sec_execute_tclm_package(info->tdata, 0);
		if (ret < 0) {
			input_err(true, &info->client->dev, "%s: sec_execute_tclm_package fail\n", __func__);
			return -EIO;
		}
	}
#else
	if (ts_hw_calibration(misc_info) == false) {
		ret = -1;
		goto out;
	}
#endif

	if (mini_init_touch(misc_info) == false) {
		ret = -1;
		goto out;

	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
#if defined(TSP_VERBOSE_DEBUG)
	input_info(true, &misc_info->client->dev, "Started esd timer\n");
#endif
#endif
out:
	enable_irq(misc_info->irq);
	misc_info->work_state = NOTHING;
	up(&misc_info->work_lock);
	return ret;
}
#endif
#ifdef SEC_FACTORY_TEST
static void fw_update(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	const u8 *buff = 0;
	mm_segment_t old_fs = {0};
	struct file *fp = NULL;
	long fsize = 0, nread = 0;
	char fw_path[MAX_FW_PATH+1];
	char result[16] = {0};
	const struct firmware *tsp_fw = NULL;
	unsigned char *fw_data = NULL;
	int restore_cal;
	int ret;
	u8 update_type = 0;

	sec_cmd_set_default_result(sec);

	switch (sec->cmd_param[0]) {
	case BUILT_IN:
		update_type = TSP_TYPE_BUILTIN_FW;
		break;
	case UMS:
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
		update_type = TSP_TYPE_EXTERNAL_FW_SIGNED;
#else
		update_type = TSP_TYPE_EXTERNAL_FW;
#endif
		break;
	case SPU:
		update_type = TSP_TYPE_SPU_FW_SIGNED;
		break;
	default:
		goto fw_update_out;
	}

	sec->cmd_state = SEC_CMD_STATUS_FAIL;

	if (update_type == TSP_TYPE_BUILTIN_FW) {
		/* firmware update builtin binary */
		snprintf(fw_path, MAX_FW_PATH, "%s", pdata->firmware_name);

		ret = request_firmware(&tsp_fw, fw_path, &(client->dev));
		if (ret) {
			input_info(true, &client->dev,
				"%s: Firmware image %s not available\n", __func__,
				fw_path);
			if (tsp_fw)
				release_firmware(tsp_fw);

			goto fw_update_out;
		} else {
			fw_data = (unsigned char *)tsp_fw->data;
		}

#ifdef TCLM_CONCEPT
		sec_tclm_root_of_cal(info->tdata, CALPOSITION_TESTMODE);
		restore_cal = 1;
#endif
		ret = ts_upgrade_sequence(info, (u8*)fw_data, restore_cal);
		release_firmware(tsp_fw);
		if (ret < 0)
			goto fw_update_out;

		sec->cmd_state = SEC_CMD_STATUS_OK;
	} else {
		/* firmware update ums or spu */
		if (update_type == TSP_TYPE_EXTERNAL_FW)
			snprintf(fw_path, MAX_FW_PATH, TSP_PATH_EXTERNAL_FW);				
		else if (update_type == TSP_TYPE_EXTERNAL_FW_SIGNED)
			snprintf(fw_path, MAX_FW_PATH, TSP_PATH_EXTERNAL_FW_SIGNED);	
		else if (update_type == TSP_TYPE_SPU_FW_SIGNED)
			snprintf(fw_path, MAX_FW_PATH, TSP_PATH_SPU_FW_SIGNED);
		else
			goto fw_update_out;

		old_fs = get_fs();
		set_fs(get_ds());

		fp = filp_open(fw_path, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			input_err(true, &client->dev, "file %s open error\n", fw_path);
			set_fs(old_fs);
			goto fw_update_out;
		}

		fsize = fp->f_path.dentry->d_inode->i_size;

		/* signed firmware is not equal with fw.buf_size: add tag and signature */
		if (update_type == TSP_TYPE_EXTERNAL_FW) {
			if (fsize != info->cap_info.ic_fw_size) {
				input_err(true, &client->dev, "invalid fw size!!\n");
				filp_close(fp, NULL);
				set_fs(old_fs);
				goto fw_update_out;
			}
		} else {
			input_info(true, &client->dev, "%s: signed firmware\n", __func__);
		}

		buff = vzalloc(fsize);
		if (!buff) {
			input_err(true, &client->dev, "failed to alloc buffer for fw\n");
			filp_close(fp, NULL);
			set_fs(old_fs);
			goto fw_update_out;
		}

		nread = vfs_read(fp, (char __user *)buff, fsize, &fp->f_pos);
		if (nread != fsize) {
			filp_close(fp, NULL);
			set_fs(old_fs);
			goto fw_update_out;
		}

		filp_close(fp, current->files);
		set_fs(old_fs);
		input_info(true, &client->dev, "ums fw is loaded!!\n");

#ifdef SPU_FW_SIGNED
		info->fw_data = (unsigned char *)buff;
		if (!(ts_check_need_upgrade(info, info->cap_info.fw_version, info->cap_info.fw_minor_version,
			info->cap_info.reg_data_version, info->cap_info.hw_id)) &&
			(update_type == TSP_TYPE_SPU_FW_SIGNED)) {
			sec->cmd_state = SEC_CMD_STATUS_OK;
			input_info(true, &client->dev, "%s: skip ffu update\n", __func__);
			goto fw_update_out;
		}

		if (update_type == TSP_TYPE_EXTERNAL_FW_SIGNED || update_type == TSP_TYPE_SPU_FW_SIGNED) {
			int ori_size;
			int spu_ret;

			ori_size = fsize - SPU_METADATA_SIZE(TSP);

			spu_ret = spu_firmware_signature_verify("TSP", buff, fsize);
			if (ori_size != spu_ret) {
				input_err(true, &client->dev, "%s: signature verify failed, ori:%ld, fsize:%d\n",
						__func__, ori_size, fsize);				

				goto fw_update_out;
			}
		}
#endif

#ifdef TCLM_CONCEPT
		sec_tclm_root_of_cal(info->tdata, CALPOSITION_TESTMODE);
		restore_cal = 1;
#endif
		ret = ts_upgrade_sequence(info, (u8*)buff, restore_cal);
		if (ret < 0)
			goto fw_update_out;

		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

fw_update_out:
#ifdef TCLM_CONCEPT
	sec_tclm_root_of_cal(info->tdata, CALPOSITION_NONE);
#endif
	if (!buff)
		kfree(buff);

	if (sec->cmd_state == SEC_CMD_STATUS_OK)
		snprintf(result, sizeof(result), "OK");
	else
		snprintf(result, sizeof(result), "NG");

	sec_cmd_set_cmd_result(sec, result, strnlen(result, sizeof(result)));
	return;
}

static void get_fw_ver_bin(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	const struct firmware *tsp_fw = NULL;
	unsigned char *fw_data = NULL;
	char fw_path[MAX_FW_PATH];
	char buff[16] = { 0 };
	u16 fw_version, fw_minor_version, reg_version, hw_id, ic_revision;
	u16 offset;
	u32 version;
	int ret;

	snprintf(fw_path, MAX_FW_PATH, "%s", pdata->firmware_name);

	ret = request_firmware(&tsp_fw, fw_path, &(client->dev));
	if (ret) {
		input_info(true, &client->dev,
			"%s: Firmware image %s not available\n", __func__,
			fw_path);
		goto fw_request_fail;
	}
	else
		fw_data = (unsigned char *)tsp_fw->data;

	sec_cmd_set_default_result(sec);

	/* To Do */
	/* modify m_firmware_data */
	hw_id = (u16)(fw_data[48] | (fw_data[49] << 8));
	fw_version = (u16)(fw_data[52] | (fw_data[53] << 8));
	fw_minor_version = (u16)(fw_data[56] | (fw_data[57] << 8));
	reg_version = (u16)(fw_data[60] | (fw_data[61] << 8));

	offset = ((u16)(fw_data[0x62] << 8) | fw_data[0x63]) + 0x22;
	ic_revision = fw_data[offset];
	ic_revision = 0;
	version = (u32)((u32)(ic_revision & 0xff) << 24) | ((fw_version & 0xf) << 20)
				| ((fw_minor_version & 0xf) << 16)
				| ((hw_id & 0xff) << 8) | (reg_version & 0xff);

    snprintf(buff, sizeof(buff), "ZI%08X", version);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "FW_VER_BIN");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

fw_request_fail:
	if (tsp_fw)
		release_firmware(tsp_fw);
	return;
}

static void get_fw_ver_ic(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[16] = { 0 };
	char model[16] = { 0 };
	u16 fw_version, fw_minor_version, reg_version, hw_id, vendor_id, ic_revision;
	u32 version, length;
	int ret;

	sec_cmd_set_default_result(sec);

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	down(&info->work_lock);
	//wakeup cmd
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);
	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);

	ret = ic_version_check(info);
	up(&info->work_lock);
	if (ret < 0) {
		input_info(true, &client->dev, "%s: version check error\n", __func__);
		return;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif

	fw_version = info->cap_info.fw_version;
	fw_minor_version = info->cap_info.fw_minor_version;
	reg_version = info->cap_info.reg_data_version;
	hw_id = info->cap_info.hw_id;
	ic_revision=  info->cap_info.ic_revision;
	ic_revision = 0;
	vendor_id = ntohs(info->cap_info.vendor_id);
	version = (u32)((u32)(ic_revision & 0xff) << 24) | ((fw_version & 0xf) << 20)
				| ((fw_minor_version & 0xf) << 16)
				| ((hw_id & 0xff) << 8) | (reg_version & 0xff);

	length = sizeof(vendor_id);
	snprintf(buff, length + 1, "%s", (u8 *)&vendor_id);
	snprintf(buff + length, sizeof(buff) - length, "%08X", version);
	snprintf(model, length + 1, "%s", (u8 *)&vendor_id);
	snprintf(model + length, sizeof(model) - length, "%04X", version >> 16);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "FW_VER_IC");
		sec_cmd_set_cmd_result_all(sec, model, strnlen(model, sizeof(model)), "FW_MODEL");
	}
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_checksum_data(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[16] = { 0 };
	u16 checksum;

	sec_cmd_set_default_result(sec);

	read_data(client, ZT75XX_CHECKSUM, (u8 *)&checksum, 2);

	snprintf(buff, sizeof(buff), "0x%X", checksum);
	input_info(true, &client->dev, "%s %d %x\n",__func__,checksum,checksum);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_threshold(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[20] = { 0 };

	sec_cmd_set_default_result(sec);

	read_data(client, BT532_THRESHOLD, (u8 *)&info->cap_info.threshold, 2);

	snprintf(buff, sizeof(buff), "%d", info->cap_info.threshold);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void module_off_master(void *device_data)
{
	return;
}

static void module_on_master(void *device_data)
{
	return;
}

static void module_off_slave(void *device_data)
{
	return;
}

static void module_on_slave(void *device_data)
{
	return;
}

static void get_module_vendor(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	char buff[16] = {0};

	sec_cmd_set_default_result(sec);

	snprintf(buff, sizeof(buff),  "%s", tostring(NA));
	sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}


#define BT532_VENDOR_NAME "ZINITIX"

static void get_chip_vendor(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);

	snprintf(buff, sizeof(buff), "%s", BT532_VENDOR_NAME);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "IC_VENDOR");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

#define BT532_CHIP_NAME "ZT75XX"

static void get_chip_name(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	const char *name_buff;
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);

	if (pdata->chip_name)
		name_buff = pdata->chip_name;
	else
		name_buff = BT532_CHIP_NAME;

	snprintf(buff, sizeof(buff), "%s", name_buff);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "IC_NAME");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_x_num(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);

	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);

	read_data(client, BT532_TOTAL_NUMBER_OF_X, (u8 *)&info->cap_info.x_node_num, 2);

	snprintf(buff, sizeof(buff), "%u", info->cap_info.x_node_num);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_y_num(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);

	write_cmd(misc_info->client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);

	read_data(client, BT532_TOTAL_NUMBER_OF_Y, (u8 *)&info->cap_info.y_node_num, 2);

	snprintf(buff, sizeof(buff), "%u", info->cap_info.y_node_num);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void not_support_cmd(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

	snprintf(buff, sizeof(buff), "%s", "NA");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;

	sec_cmd_set_cmd_exit(sec);

	input_info(true, &client->dev, "%s: \"%s(%d)\"\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

#define REG_CHANNEL_TEST_RESULT				0x0296
#define TEST_CHANNEL_OPEN				0x0D
#define TEST_PATTERN_OPEN				0x04
#define TEST_SHORT					0x08
#define TEST_PASS					0xFF

static bool get_channel_test_result(struct bt532_ts_info *info, int skip_cnt)
{
	struct i2c_client *client = info->client;
	struct bt532_ts_platform_data *pdata = info->pdata;
	int i;
	int retry = 150;

	disable_irq(info->irq);

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		input_info(true, &client->dev, "other process occupied.. (%d)\n",
			info->work_state);
		enable_irq(info->irq);
		up(&info->work_lock);
		return false;
	}

	info->work_state = RAW_DATA;

	for (i = 0; i < skip_cnt; i++) {
		while (gpio_get_value(pdata->gpio_int)) {
			usleep_range(7 * 1000, 7 * 1000);
			if (--retry < 0)
				break;
		}

		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		usleep_range(1 * 1000, 1 * 1000);
	}

	retry = 100;
	input_info(true, &client->dev, "%s: channel_test_result read\n", __func__);

	while (gpio_get_value(pdata->gpio_int)) {
//		usleep_range(1 * 1000, 1 * 1000);
		msleep(30);
		if (--retry < 0)
			break;
		else
			input_info(true, &client->dev, "%s: retry:%d\n", __func__, retry);
	}

	read_data(info->client, REG_CHANNEL_TEST_RESULT, (u8 *)info->raw_data->channel_test_data, 10);

	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);

	return true;
}

static void run_test_open_short(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	ts_set_touchmode(TOUCH_CHANNEL_TEST_MODE);
	get_channel_test_result(info, 2);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true, &client->dev, "channel_test_result : %04X\n", raw_data->channel_test_data[0]);
	input_info(true, &client->dev, "RX Channel : %08X\n",
			raw_data->channel_test_data[1] | ((raw_data->channel_test_data[2] << 16) & 0xffff0000));
	input_info(true, &client->dev, "TX Channel : %08X\n",
			raw_data->channel_test_data[3] | ((raw_data->channel_test_data[4] << 16) & 0xffff0000));

	if (raw_data->channel_test_data[0] == TEST_SHORT) {
		info->ito_test[3] |= 0x0F;
	} else if (raw_data->channel_test_data[0] == TEST_CHANNEL_OPEN || raw_data->channel_test_data[0] == TEST_PATTERN_OPEN) {
		if (raw_data->channel_test_data[3] | ((raw_data->channel_test_data[4] << 16) & 0xffff0000))
			info->ito_test[3] |= 0x10;

		if (raw_data->channel_test_data[1] | ((raw_data->channel_test_data[2] << 16) & 0xffff0000))
			info->ito_test[3] |= 0x20;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
}

static void check_trx_channel_test(struct bt532_ts_info *info, char *buf)
{
	struct tsp_raw_data *raw_data = info->raw_data;
	u8 temp[10];
	int ii;
	u32 test_result;

	memset(temp, 0x00, sizeof(temp));

	test_result = raw_data->channel_test_data[3] | ((raw_data->channel_test_data[4] << 16) & 0xffff0000);
	for (ii = 0; ii < info->cap_info.x_node_num; ii++) {
		if (test_result & (1 << ii)) {
			memset(temp, 0x00, 10);
			snprintf(temp, sizeof(temp), "T%d, ", ii);
			strlcat(buf, temp, SEC_CMD_STR_LEN);
		}
	}

	test_result = raw_data->channel_test_data[1] | ((raw_data->channel_test_data[2] << 16) & 0xffff0000);
	for (ii = 0; ii < info->cap_info.y_node_num; ii++) {
		if (test_result & (1 << ii)) {
			memset(temp, 0x00, 10);
			snprintf(temp, sizeof(temp), "R%d, ", ii);
			strlcat(buf, temp, SEC_CMD_STR_LEN);
		}
	}
}

static void run_trx_short_test(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u8 temp[10];
	char test[32];

	sec_cmd_set_default_result(sec);

	if (info->tsp_pwr_enabled == POWER_OFF) {
		input_err(true, &info->client->dev, "%s: IC is power off\n", __func__);
		snprintf(buff, sizeof(buff), "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	if (sec->cmd_param[1])
		snprintf(test, sizeof(test), "TEST=%d,%d", sec->cmd_param[0], sec->cmd_param[1]);
	else
		snprintf(test, sizeof(test), "TEST=%d", sec->cmd_param[0]);

	/*
	 * run_test_open_short() need to be fix for separate by test item(open, short, pattern open)
	 */
	if (sec->cmd_param[0] == 1 && sec->cmd_param[1] == 1)
		run_test_open_short(info);

	if (sec->cmd_param[0] == 1 && sec->cmd_param[1] == 1) {
		/* 1,1 : open  */
		if (raw_data->channel_test_data[0] != TEST_CHANNEL_OPEN)
			goto OK;

		memset(temp, 0x00, sizeof(temp));
		snprintf(temp, sizeof(temp), "OPEN: ");
		strlcat(buff, temp, SEC_CMD_STR_LEN);

		check_trx_channel_test(info, buff);
		input_info(true, &client->dev, "%s\n", buff);
	} else if (sec->cmd_param[0] == 1 && sec->cmd_param[1] == 2) {
		/* 1,2 : short  */
		if (raw_data->channel_test_data[0] != TEST_SHORT)
			goto OK;

		memset(temp, 0x00, sizeof(temp));
		snprintf(temp, sizeof(temp), "SHORT: ");
		strlcat(buff, temp, SEC_CMD_STR_LEN);

		check_trx_channel_test(info, buff);
		input_info(true, &client->dev, "%s\n", buff);
	} else if (sec->cmd_param[0] == 2) {
		/* 2 : micro open(pattern open)  */
		if (raw_data->channel_test_data[0] != TEST_PATTERN_OPEN)
			goto OK;

		memset(temp, 0x00, sizeof(temp));
		snprintf(temp, sizeof(temp), "CRACK: ");
		strlcat(buff, temp, SEC_CMD_STR_LEN);

		check_trx_channel_test(info, buff);
		input_info(true, &client->dev, "%s\n", buff);
	} else if (sec->cmd_param[0] == 3) {
		/* 3 : bridge short  */
		snprintf(buff, sizeof(buff), "NA");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;

		sec_cmd_send_event_to_user(sec, test, "RESULT=FAIL");

		input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));
		return;
	} else {
		/* 0 or else : old command */
		if (raw_data->channel_test_data[0] == TEST_PASS)
			goto OK;
	}

	snprintf(buff, sizeof(buff), "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;

	sec_cmd_send_event_to_user(sec, test, "RESULT=FAIL");

	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));
	return;


OK:
	snprintf(buff, sizeof(buff), "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	sec_cmd_send_event_to_user(sec, test, "RESULT=PASS");

	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));
	return;

}

static void run_cnd_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 min, max;
	s32 i, j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_RAW_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->cnd_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true,&client->dev, "CND start\n");

	min = 0xFFFF;
	max = 0x0000;

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			if (raw_data->cnd_data[i * info->cap_info.x_node_num + j] < min &&
				raw_data->cnd_data[i * info->cap_info.x_node_num + j] != 0)
				min = raw_data->cnd_data[i * info->cap_info.x_node_num + j];

			if (raw_data->cnd_data[i * info->cap_info.x_node_num + j] > max)
				max = raw_data->cnd_data[i * info->cap_info.x_node_num + j];
		}
	}
	snprintf(buff, sizeof(buff), "%d,%d", min, max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "CND");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	zinitix_display_rawdata(info, raw_data, TOUCH_RAW_MODE, 0);
out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
			sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "CND");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void run_cnd_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[16] = { 0 };
	char all_cmdbuff[info->cap_info.x_node_num * info->cap_info.y_node_num * 6];
	s32 i, j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(misc_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_RAW_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->cnd_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	memset(all_cmdbuff, 0, sizeof(char) * (info->cap_info.x_node_num * info->cap_info.y_node_num * 6));	//size 6  ex(12000,)

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			sprintf(buff, "%u,", raw_data->cnd_data[i * info->cap_info.x_node_num + j]);
			strcat(all_cmdbuff, buff);
		}
	}

	sec_cmd_set_cmd_result(sec, all_cmdbuff,
			strnlen(all_cmdbuff, sizeof(all_cmdbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(misc_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void run_dnd_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 min, max;
	s32 i, j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_DND_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->dnd_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	min = 0xFFFF;
	max = 0x0000;

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			if (raw_data->dnd_data[i * info->cap_info.x_node_num + j] < min &&
				raw_data->dnd_data[i * info->cap_info.x_node_num + j] != 0)
				min = raw_data->dnd_data[i * info->cap_info.x_node_num + j];

			if (raw_data->dnd_data[i * info->cap_info.x_node_num + j] > max)
				max = raw_data->dnd_data[i * info->cap_info.x_node_num + j];
		}
	}
	snprintf(buff, sizeof(buff), "%d,%d", min, max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "DND");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	zinitix_display_rawdata(info, raw_data, TOUCH_DND_MODE, 0);
out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
			sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "DND");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif

	return;
}

static void get_dnd(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->dnd_data[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_dnd_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char *all_cmdbuff =  NULL;
	int i, j;
	int buff_size = info->cap_info.x_node_num * info->cap_info.y_node_num * CMD_RESULT_WORD_LEN;

	sec_cmd_set_default_result(sec);

	all_cmdbuff = kzalloc(buff_size, GFP_KERNEL);
	if (!all_cmdbuff) {
		snprintf(buff, sizeof(buff), "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			snprintf(buff, sizeof(buff), "%u,",
				raw_data->dnd_data[i * info->cap_info.x_node_num + j]);
			strlcat(all_cmdbuff, buff, buff_size);
		}
	}

	sec_cmd_set_cmd_result(sec, all_cmdbuff, buff_size);
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(all_cmdbuff);
	return;
}

static void run_dnd_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[16] = { 0 };
	char all_cmdbuff[info->cap_info.x_node_num * info->cap_info.y_node_num * 6];
	s32 i, j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(misc_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_DND_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->dnd_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	memset(all_cmdbuff, 0, sizeof(char) * (info->cap_info.x_node_num * info->cap_info.y_node_num * 6));	//size 6  ex(12000,)

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			sprintf(buff, "%u,", raw_data->dnd_data[i * info->cap_info.x_node_num + j]);
			strcat(all_cmdbuff, buff);
		}
	}

	sec_cmd_set_cmd_result(sec, all_cmdbuff,
			strnlen(all_cmdbuff, sizeof(all_cmdbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(misc_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void run_dnd_v_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_1[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_2[SEC_CMD_STR_LEN] = { 0 };
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;
	int i, j, offset, val, cur_val, next_val;
	u16 screen_max = 0x0000;
	u16 touchkey_max = 0x0000;

	sec_cmd_set_default_result(sec);

	memset(raw_data->vgap_data, 0x00, TSP_CMD_NODE_NUM);

	input_info(true, &client->dev, "DND V Gap start\n");

	input_info(true, &client->dev, "%s : ++++++ DND SPEC +++++++++\n",__func__);
	for (i = 0; i < y_num - 1; i++) {
		for (j = 0; j < x_num; j++) {
			offset = (i * x_num) + j;

			cur_val = raw_data->dnd_data[offset];
			next_val = raw_data->dnd_data[offset + x_num];
			if (!next_val) {
				raw_data->vgap_data[offset] = next_val;
				continue;
			}

			if (next_val > cur_val)
				val = 100 - ((cur_val * 100) / next_val);
			else
				val = 100 - ((next_val * 100) / cur_val);

			raw_data->vgap_data[offset] = val;

			if (pdata->support_touchkey){
				if (i < y_num - 2){
					if (raw_data->vgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->vgap_data[i * x_num + j];
				}
				else{
					if (raw_data->vgap_data[i * x_num + j] > touchkey_max)
						touchkey_max = raw_data->vgap_data[i * x_num + j];
				}
			}
			else{
				if (raw_data->vgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->vgap_data[i * x_num + j];
			}
		}
	}

	if (pdata->support_touchkey) {
		input_info(true, &client->dev, "DND V Gap screen_max %d touchkey_max %d\n", screen_max, touchkey_max);
		snprintf(buff, sizeof(buff), "%d,%d", screen_max, touchkey_max);
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
		snprintf(buff_onecmd_2, sizeof(buff_onecmd_2), "%d,%d", 0, touchkey_max);
	} else {
		input_info(true, &client->dev, "DND V Gap screen_max %d\n", screen_max);
		snprintf(buff, sizeof(buff), "%d", screen_max);
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
		if (pdata->support_touchkey) {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "DND_V_GAP_SCREEN");
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_2, strnlen(buff_onecmd_2, sizeof(buff_onecmd_2)), "DND_V_GAP_TOUCHKEY");
		} else {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "DND_V_GAP");
		}
	}
	sec->cmd_state = SEC_CMD_STATUS_OK;

	zinitix_display_rawdata(info, raw_data, TOUCH_DND_MODE, 1);

	return;
}

static void run_dnd_h_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_1[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_2[SEC_CMD_STR_LEN] = { 0 };
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;
	int i, j, offset, val, cur_val, next_val;
	u16 screen_max = 0x0000;
	u16 touchkey_max = 0x0000;

	sec_cmd_set_default_result(sec);

	memset(raw_data->hgap_data, 0x00, TSP_CMD_NODE_NUM);

	input_info(true, &client->dev, "DND H Gap start\n");

	for (i = 0; i < y_num; i++) {
		for (j = 0; j < x_num - 1; j++) {
			offset = (i * x_num) + j;

			cur_val = raw_data->dnd_data[offset];
			if (!cur_val) {
				raw_data->hgap_data[offset] = cur_val;
				continue;
			}

			next_val = raw_data->dnd_data[offset + 1];
			if (!next_val) {
				raw_data->hgap_data[offset] = next_val;
				for (++j; j < x_num - 1; j++) {
					offset = (i * x_num) + j;

					next_val = raw_data->dnd_data[offset];
					if (!next_val) {
						raw_data->hgap_data[offset] = next_val;
						continue;
					}
					break;
				}
			}

			if (next_val > cur_val)
				val = 100 - ((cur_val * 100) / next_val);
			else
				val = 100 - ((next_val * 100) / cur_val);

			raw_data->hgap_data[offset] = val;

			if (pdata->support_touchkey){
				if (i < y_num - 1){
					if (raw_data->hgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->hgap_data[i * x_num + j];
				}
				else{
					if (raw_data->hgap_data[i * x_num + j] > touchkey_max)
						touchkey_max = raw_data->hgap_data[i * x_num + j];
				}
			}
			else{
				if (raw_data->hgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->hgap_data[i * x_num + j];
			}
		}
	}

	if (pdata->support_touchkey) {
		input_info(true, &client->dev, "DND H Gap screen_max %d, touchkey_max %d\n", screen_max, touchkey_max);
		snprintf(buff, sizeof(buff), "%d,%d", screen_max, touchkey_max);
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
		snprintf(buff_onecmd_2, sizeof(buff_onecmd_2), "%d,%d", 0, touchkey_max);
	} else {
		input_info(true, &client->dev, "DND H Gap screen_max %d\n", screen_max);
		snprintf(buff, sizeof(buff), "%d", screen_max);
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
		if (pdata->support_touchkey) {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "DND_H_GAP_SCREEN");
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_2, strnlen(buff_onecmd_2, sizeof(buff_onecmd_2)), "DND_H_GAP_TOUCHKEY");
		} else {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "DND_H_GAP");
		}
	}
	sec->cmd_state = SEC_CMD_STATUS_OK;

	zinitix_display_rawdata(info, raw_data, TOUCH_DND_MODE, 2);

	return;
}

static void get_dnd_h_gap(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_node, y_node;
	int node_num;
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= x_num - 1 || y_node < 0 || y_node >= y_num) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = (y_node * x_num) + x_node;

	snprintf(buff, sizeof(buff), "%d", raw_data->hgap_data[node_num]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
}

static void run_dnd_h_gap_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char temp[SEC_CMD_STR_LEN] = { 0 };
	char *buff = NULL;
	int total_node = info->cap_info.x_node_num * info->cap_info.y_node_num;
	int i, j, offset, val, cur_val, next_val;

	sec_cmd_set_default_result(sec);

	buff = kzalloc(total_node * CMD_RESULT_WORD_LEN, GFP_KERNEL);
	if (!buff) {
		snprintf(temp, SEC_CMD_STR_LEN, "NG");
		sec_cmd_set_cmd_result(sec, temp, SEC_CMD_STR_LEN);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num - 1; j++) {
			offset = (i * info->cap_info.x_node_num) + j;

			cur_val = raw_data->dnd_data[offset];
			if (!cur_val) {
				raw_data->hgap_data[offset] = cur_val;
				continue;
			}

			next_val = raw_data->dnd_data[offset + 1];
			if (!next_val) {
				raw_data->hgap_data[offset] = next_val;
				for (++j; j < info->cap_info.x_node_num - 1; j++) {
					offset = (i * info->cap_info.x_node_num) + j;

					next_val = raw_data->dnd_data[offset];
					if (!next_val) {
						raw_data->hgap_data[offset] = next_val;
						continue;
					}
					break;
				}
			}

			if (next_val > cur_val)
				val = 100 - ((cur_val * 100) / next_val);
			else
				val = 100 - ((next_val * 100) / cur_val);

			raw_data->hgap_data[offset] = val;

			snprintf(temp, CMD_RESULT_WORD_LEN, "%d,", raw_data->hgap_data[offset]);
			strncat(buff, temp, CMD_RESULT_WORD_LEN);
			memset(temp, 0x00, SEC_CMD_STR_LEN);
		}
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, total_node * CMD_RESULT_WORD_LEN));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(buff);
}

static void get_dnd_v_gap(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_node, y_node;
	int node_num;
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= x_num || y_node < 0 || y_node >= y_num - 1) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = (y_node * x_num) + x_node;

	sprintf(buff, "%d", raw_data->vgap_data[node_num]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
}

static void run_dnd_v_gap_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char temp[SEC_CMD_STR_LEN] = { 0 };
	char *buff = NULL;
	int total_node = info->cap_info.x_node_num * info->cap_info.y_node_num;
	int i, j, offset, val, cur_val, next_val;

	sec_cmd_set_default_result(sec);

	buff = kzalloc(total_node * CMD_RESULT_WORD_LEN, GFP_KERNEL);
	if (!buff) {
		snprintf(temp, SEC_CMD_STR_LEN, "NG");
		sec_cmd_set_cmd_result(sec, temp, SEC_CMD_STR_LEN);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	for (i = 0; i < info->cap_info.y_node_num - 1; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			offset = (i * info->cap_info.x_node_num) + j;

			cur_val = raw_data->dnd_data[offset];
			next_val = raw_data->dnd_data[offset + info->cap_info.x_node_num];
			if (!next_val) {
				raw_data->vgap_data[offset] = next_val;
				continue;
			}

			if (next_val > cur_val)
				val = 100 - ((cur_val * 100) / next_val);
			else
				val = 100 - ((next_val * 100) / cur_val);

			raw_data->vgap_data[offset] = val;

			snprintf(temp, CMD_RESULT_WORD_LEN, "%d,", raw_data->vgap_data[offset]);
			strncat(buff, temp, CMD_RESULT_WORD_LEN);
			memset(temp, 0x00, SEC_CMD_STR_LEN);
		}
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, total_node * CMD_RESULT_WORD_LEN));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(buff);
}

static void run_delta_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	s16 min, max;
	s32 i, j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_DELTA_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->delta_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	min = (s16)0x7FFF;
	max = (s16)0x8000;

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			if (raw_data->delta_data[i * info->cap_info.x_node_num + j] < min &&
				raw_data->delta_data[i * info->cap_info.x_node_num + j] != 0)
				min = raw_data->delta_data[i * info->cap_info.x_node_num + j];

			if (raw_data->delta_data[i * info->cap_info.x_node_num + j] > max)
				max = raw_data->delta_data[i * info->cap_info.x_node_num + j];

		}
	}

	snprintf(buff, sizeof(buff), "%d,%d\n", min, max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	zinitix_display_rawdata(info, raw_data, TOUCH_DELTA_MODE, 0);
out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_delta(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;

		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->delta_data[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void run_hfdnd_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;
	int i, j, offset;
	u16 min = 0xFFFF, max = 0x0000;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_HFDND_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->hfdnd_data, 2);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true,&client->dev, "HF DND start\n");

	for (i = 0; i < y_num; i++) {
		for (j = 0; j < x_num; j++) {
			offset = (i * x_num) + j;
			input_info(true, &client->dev, "%d ", raw_data->hfdnd_data[offset]);
			if (raw_data->hfdnd_data[offset] < min && raw_data->hfdnd_data[offset] != 0)
				min = raw_data->hfdnd_data[offset];
			if (raw_data->hfdnd_data[offset] > max)
				max = raw_data->hfdnd_data[offset];
		}
		input_info(true, &client->dev, "\n");
	}

	input_info(true, &client->dev, "HF DND Pass\n");

	snprintf(buff, sizeof(buff), "%d,%d", min, max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "HF_DND");

	sec->cmd_state = SEC_CMD_STATUS_OK;

out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_hfdnd(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->hfdnd_data[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true,&client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
		(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_hfdnd_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char *all_cmdbuff =  NULL;
	int i, j;
	int buff_size = info->cap_info.x_node_num * info->cap_info.y_node_num * CMD_RESULT_WORD_LEN;

	sec_cmd_set_default_result(sec);

	all_cmdbuff = kzalloc(buff_size, GFP_KERNEL);
	if (!all_cmdbuff) {
		snprintf(buff, sizeof(buff), "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			snprintf(buff, sizeof(buff), "%u,",
				raw_data->hfdnd_data[i * info->cap_info.x_node_num + j]);
			strlcat(all_cmdbuff, buff, buff_size);
		}
	}

	sec_cmd_set_cmd_result(sec, all_cmdbuff, buff_size);
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(all_cmdbuff);
	return;
}

static void run_hfdnd_v_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_1[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_2[SEC_CMD_STR_LEN] = { 0 };
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;
	int i, j, offset, val, cur_val, next_val;
	u16 screen_max = 0x0000;
	u16 touchkey_max = 0x0000;

	sec_cmd_set_default_result(sec);

	memset(raw_data->vgap_data, 0x00, TSP_CMD_NODE_NUM);

	input_info(true, &info->client->dev, "HF DND V Gap start\n");

	for (i = 0; i < y_num - 1; i++) {
		for (j = 0; j < x_num; j++) {
			offset = (i * x_num) + j;

			cur_val = raw_data->hfdnd_data[offset];
			next_val = raw_data->hfdnd_data[offset + x_num];
			if (!next_val) {
				raw_data->vgap_data[offset] = next_val;
				continue;
			}

			if (next_val > cur_val)
				val = 100 - ((cur_val * 100) / next_val);
			else
				val = 100 - ((next_val * 100) / cur_val);

			input_info(true, &info->client->dev, "%d ", val);
			raw_data->vgap_data[offset] = val;

			if (pdata->support_touchkey){
				if (i < y_num - 2){
					if (raw_data->vgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->vgap_data[i * x_num + j];
				}
				else{
					if (raw_data->vgap_data[i * x_num + j] > touchkey_max)
						touchkey_max = raw_data->vgap_data[i * x_num + j];
				}
			}
			else{
				if (raw_data->vgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->vgap_data[i * x_num + j];
			}
		}
		input_info(true, &info->client->dev, "\n");
	}

	input_info(true, &info->client->dev, "HFDND V Gap screen_max %d, touchkey_max %d\n", screen_max, touchkey_max);
	snprintf(buff, sizeof(buff), "%d,%d\n", screen_max, touchkey_max);

	if (pdata->support_touchkey) {
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
		snprintf(buff_onecmd_2, sizeof(buff_onecmd_2), "%d,%d", 0, touchkey_max);
	} else {
		input_info(true, &info->client->dev, "HFDND V Gap screen_max %d\n", screen_max);
		snprintf(buff, sizeof(buff), "%d", screen_max);
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
		if (pdata->support_touchkey) {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "HF_DND_V_GAP_SCREEN");
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_2, strnlen(buff_onecmd_2, sizeof(buff_onecmd_2)), "HF_DND_V_GAP_TOUCHKEY");
		} else {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "HF_DND_V_GAP");
		}
	}

	sec->cmd_state = SEC_CMD_STATUS_OK;

	return;
}

static void run_hfdnd_h_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_1[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd_2[SEC_CMD_STR_LEN] = { 0 };
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;
	int i, j, offset, val, cur_val, next_val;
	u16 screen_max = 0x0000;
	u16 touchkey_max = 0x0000;

	sec_cmd_set_default_result(sec);

	memset(raw_data->hgap_data, 0x00, TSP_CMD_NODE_NUM);

	input_info(true, &info->client->dev, "HF DND H Gap start\n");

	for (i = 0; i < y_num ; i++) {
		for (j = 0; j < x_num - 1; j++) {
			offset = (i * x_num) + j;

			cur_val = raw_data->hfdnd_data[offset];
			if (!cur_val) {
				raw_data->hgap_data[offset] = cur_val;
				continue;
			}

			next_val = raw_data->hfdnd_data[offset + 1];
			if (!next_val) {
				raw_data->hgap_data[offset] = next_val;
				for (++j; j < x_num - 1; j++) {
					offset = (i * x_num) + j;

					next_val = raw_data->hfdnd_data[offset];
					if (!next_val) {
						raw_data->hgap_data[offset]	= next_val;
						continue;
					}
					break;
				}
			}

			if (next_val > cur_val)
				val = 100 - ((cur_val * 100) / next_val);
		   else
				val = 100 - ((next_val * 100) / cur_val);

			input_info(true, &info->client->dev, "%d ", val);
			raw_data->hgap_data[offset] = val;

			if (pdata->support_touchkey){
				if (i < y_num - 1){
					if (raw_data->hgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->hgap_data[i * x_num + j];
				}
				else{
					if (raw_data->hgap_data[i * x_num + j] > touchkey_max)
						touchkey_max = raw_data->hgap_data[i * x_num + j];
				}
			}
			else{
				if (raw_data->hgap_data[i * x_num + j] > screen_max)
						screen_max = raw_data->hgap_data[i * x_num + j];
			}
		}
		input_info(true, &info->client->dev, "\n");
	}

	input_info(true, &info->client->dev, "HFDND H Gap screen_max %d, touchkey_max %d\n", screen_max, touchkey_max);
	snprintf(buff, sizeof(buff), "%d,%d", screen_max, touchkey_max);

	if (pdata->support_touchkey) {
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
		snprintf(buff_onecmd_2, sizeof(buff_onecmd_2), "%d,%d", 0, touchkey_max);
	} else {
		input_info(true, &info->client->dev, "HFDND H Gap screen_max %d\n", screen_max);
		snprintf(buff, sizeof(buff), "%d", screen_max);
		snprintf(buff_onecmd_1, sizeof(buff_onecmd_1), "%d,%d", 0, screen_max);
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
		if (pdata->support_touchkey) {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "HF_DND_H_GAP_SCREEN");
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_2, strnlen(buff_onecmd_2, sizeof(buff_onecmd_2)), "HF_DND_H_GAP_TOUCHKEY");
		} else {
			sec_cmd_set_cmd_result_all(sec, buff_onecmd_1, strnlen(buff_onecmd_1, sizeof(buff_onecmd_1)), "HF_DND_H_GAP");
		}
	}

	sec->cmd_state = SEC_CMD_STATUS_OK;

	return;
}

static void get_hfdnd_h_gap(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_node, y_node;
	int node_num;
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= x_num - 1 || y_node < 0 || y_node >= y_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = (y_node * x_num) + x_node;

	snprintf(buff, sizeof(buff), "%d", raw_data->hgap_data[node_num]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true,&info->client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
		(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
}

static void get_hfdnd_h_gap_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char *all_cmdbuff =  NULL;
	int i, j;
	int buff_size = info->cap_info.x_node_num * info->cap_info.y_node_num * CMD_RESULT_WORD_LEN;

	sec_cmd_set_default_result(sec);

	all_cmdbuff = kzalloc(buff_size, GFP_KERNEL);
	if (!all_cmdbuff) {
		snprintf(buff, sizeof(buff), "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num - 1; j++) {
			snprintf(buff, sizeof(buff), "%u,",
				raw_data->hgap_data[i * info->cap_info.x_node_num + j]);
			strlcat(all_cmdbuff, buff, buff_size);
		}
	}

	sec_cmd_set_cmd_result(sec, all_cmdbuff, buff_size);
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(all_cmdbuff);
	return;
}

static void get_hfdnd_v_gap(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_node, y_node;
	int node_num;
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= x_num || y_node < 0 || y_node >= y_num - 1) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = (y_node * x_num) + x_node;

	snprintf(buff, sizeof(buff), "%d", raw_data->vgap_data[node_num]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true,&info->client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
		(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
}

static void get_hfdnd_v_gap_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char *all_cmdbuff =  NULL;
	int i, j;
	int buff_size = info->cap_info.x_node_num * info->cap_info.y_node_num * CMD_RESULT_WORD_LEN;

	sec_cmd_set_default_result(sec);

	all_cmdbuff = kzalloc(buff_size, GFP_KERNEL);
	if (!all_cmdbuff) {
		snprintf(buff, sizeof(buff), "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	for (i = 0; i < info->cap_info.y_node_num - 1; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			snprintf(buff, sizeof(buff), "%u,",
				raw_data->vgap_data[i * info->cap_info.x_node_num + j]);
			strlcat(all_cmdbuff, buff, buff_size);
		}
	}

	sec_cmd_set_cmd_result(sec, all_cmdbuff, buff_size);
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(all_cmdbuff);
	return;
}

static void run_rxshort_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int i, touchkey_node = 2;
	u16 screen_max = 0x0000, touchkey_max = 0x0000;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ts_set_touchmode3(TOUCH_RXSHORT_MODE);
	get_raw_data(info, (u8 *)raw_data->rxshort_data, 2);
	ts_set_touchmode3(TOUCH_POINT_MODE);

	input_info(true,&client->dev, "RX SHORT start\n");

	for (i = 0; i < info->cap_info.x_node_num; i++) {
		input_info(true,&client->dev, "%d ", raw_data->rxshort_data[i]);

		if ((i == touchkey_node) || (i == (info->cap_info.x_node_num - 1) - touchkey_node)){
			if (raw_data->rxshort_data[i] > touchkey_max)
				touchkey_max = raw_data->rxshort_data[i];
		} else {
			if (raw_data->rxshort_data[i] > screen_max)
				screen_max = raw_data->rxshort_data[i];
		}
	}

	input_info(true, &client->dev, "RX SHORT end\n");

	snprintf(buff, sizeof(buff), "%d,%d\n", screen_max, touchkey_max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_rxshort(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->rxshort_data[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true,&client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
		(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void run_txshort_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int i;
	u16 screen_max = 0x0000, touchkey_max = 0x0000;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ts_set_touchmode3(TOUCH_TXSHORT_MODE);
	get_raw_data(info, (u8 *)raw_data->txshort_data, 2);
	ts_set_touchmode3(TOUCH_POINT_MODE);

	input_info(true,&client->dev, "TX SHORT start\n");

	for (i = 0; i < info->cap_info.y_node_num - 1; i++) {
		input_info(true,&client->dev, "%d ", raw_data->txshort_data[i]);

		if (raw_data->txshort_data[i]>screen_max)
			screen_max = raw_data->txshort_data[i];
	}
	input_info(true,&client->dev, "%d ", raw_data->txshort_data[info->cap_info.y_node_num - 1]);
	touchkey_max = raw_data->txshort_data[info->cap_info.y_node_num - 1];

	input_info(true, &client->dev, "TX SHORT end\n");

	snprintf(buff, sizeof(buff), "%d,%d\n", screen_max, touchkey_max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_txshort(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->txshort_data[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true,&client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
		(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}
/*
static void run_trxshort_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_num = info->cap_info.x_node_num;
	int y_num = info->cap_info.y_node_num;
	int i, j;
	u16 rx_max = 0x0000, tx_max = 0x0000;
	u16 short_pass = 0x0001, short_fail = 0x0000;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ts_set_touchmode(TOUCH_TXSHORT_MODE);
	get_raw_data(info, (u8 *)raw_data->txshort_data, 2);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true, &client->dev, "TRX SHORT start\n");

	for (i = 0; i < y_num; i++) {
		for (j = 0; j < x_num; j++) {
			input_info(true, &client->dev, "%d\t", raw_data->txshort_data[i * x_num + j]);
		}
		input_info(true, &client->dev, "\n", raw_data->txshort_data[i * x_num + j]);
	}

	input_info(true,&client->dev, "RX SHORT :\n");
	for (i = 0; i < x_num; i++) {
		input_info(true, &client->dev, "%d ", raw_data->txshort_data[i]);
		if (raw_data->txshort_data[i] > rx_max)
			rx_max = raw_data->txshort_data[i];
	}
	input_info(true, &client->dev, "\n");

	input_info(true, &client->dev, "TX SHORT :\n");
	for (i = x_num; i < x_num + y_num; i++) {
		input_info(true, &client->dev, "%d ", raw_data->txshort_data[i]);
		if (raw_data->txshort_data[i] > tx_max)
			tx_max = raw_data->txshort_data[i];
	}
	input_info(true, &client->dev, "\n");

	input_info(true, &client->dev, "TRX SHORT end\n");

	if (rx_max == 100 && tx_max == 200)
		snprintf(buff, sizeof(buff), "%d", short_pass);
	else 
		snprintf(buff, sizeof(buff), "%d", short_fail);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "SHORT_TEST");
	sec->cmd_state = SEC_CMD_STATUS_OK;

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}
*/
static void run_selfdnd_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	int total_node = info->cap_info.x_node_num + info->cap_info.y_node_num;
	char tx_buff[SEC_CMD_STR_LEN] = { 0 };
	char rx_buff[SEC_CMD_STR_LEN] = { 0 };
	u16 tx_min, tx_max, rx_min, rx_max;
	s32 j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_SELF_DND_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->selfdnd_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true,&client->dev, "SELF DND start\n");

	tx_min = 0xFFFF;
	tx_max = 0x0000;
	rx_min = 0xFFFF;
	rx_max = 0x0000;

	for (j = 0; j < info->cap_info.x_node_num; j++) {
		if (raw_data->selfdnd_data[j] < rx_min && raw_data->selfdnd_data[j] != 0)
			rx_min = raw_data->selfdnd_data[j];

		if (raw_data->selfdnd_data[j] > rx_max)
			rx_max = raw_data->selfdnd_data[j];
	}

	for (j = info->cap_info.x_node_num; j < total_node; j++) {
		if (raw_data->selfdnd_data[j] < tx_min && raw_data->selfdnd_data[j] != 0)
			tx_min = raw_data->selfdnd_data[j];

		if (raw_data->selfdnd_data[j] > tx_max)
			tx_max = raw_data->selfdnd_data[j];
	}

	input_info(true, &client->dev, "SELF DND Pass\n");

	snprintf(tx_buff, sizeof(tx_buff), "%d,%d", tx_min, tx_max);
	snprintf(rx_buff, sizeof(rx_buff), "%d,%d", rx_min, rx_max);
	sec_cmd_set_cmd_result(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
		sec_cmd_set_cmd_result_all(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)), "SELF_DND_RX");
	}
	sec->cmd_state = SEC_CMD_STATUS_OK;

out:
	if (ret < 0) {
		snprintf(rx_buff, sizeof(rx_buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)));
		if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
			sec_cmd_set_cmd_result_all(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)), "SELF_DND_RX");
		}
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void run_charge_pump_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_CHARGE_PUMP_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->charge_pump_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	snprintf(buff, sizeof(buff), "%d,%d", raw_data->charge_pump_data[0], raw_data->charge_pump_data[0]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "CHARGE_PUMP");
	sec->cmd_state = SEC_CMD_STATUS_OK;

out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
			sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "CHARGE_PUMP");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_selfdnd_rx(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	val = raw_data->selfdnd_data[x_node];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_selfdnd_tx(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	val = raw_data->selfdnd_data[info->cap_info.x_node_num + y_node];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void run_selfdnd_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char temp[SEC_CMD_STR_LEN] = { 0 };
	char *buff = NULL;
	int total_node = info->cap_info.x_node_num + info->cap_info.y_node_num;
	s32 j;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ts_set_touchmode(TOUCH_SELF_DND_MODE);

	get_raw_data(info, (u8 *)raw_data->selfdnd_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	buff = kzalloc(total_node * CMD_RESULT_WORD_LEN, GFP_KERNEL);
	if (!buff)
		goto NG;

	for (j = 0; j < total_node; j++) {
		snprintf(temp, CMD_RESULT_WORD_LEN, "%d,", raw_data->selfdnd_data[j]);
		strncat(buff, temp, CMD_RESULT_WORD_LEN);
		memset(temp, 0x00, SEC_CMD_STR_LEN);
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, total_node * CMD_RESULT_WORD_LEN));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(buff);

NG:
	if (sec->cmd_state != SEC_CMD_STATUS_OK) {
		snprintf(temp, SEC_CMD_STR_LEN, "NG");
		sec_cmd_set_cmd_result(sec, temp, SEC_CMD_STR_LEN);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
}

static void run_self_saturation_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	int total_node = info->cap_info.x_node_num + info->cap_info.y_node_num;
	char tx_buff[SEC_CMD_STR_LEN] = { 0 };
	char rx_buff[SEC_CMD_STR_LEN] = { 0 };
	u16 tx_min, tx_max, rx_min, rx_max;
	s32 j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(DEF_RAW_SELF_SSR_DATA_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->ssr_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true,&client->dev, "SELF SATURATION start\n");

	tx_min = 0xFFFF;
	tx_max = 0x0000;
	rx_min = 0xFFFF;
	rx_max = 0x0000;

	for (j = 0; j < info->cap_info.x_node_num; j++) {
		if (raw_data->ssr_data[j] < rx_min && raw_data->ssr_data[j] != 0)
			rx_min = raw_data->ssr_data[j];

		if (raw_data->ssr_data[j] > rx_max)
			rx_max = raw_data->ssr_data[j];
	}

	for (j = info->cap_info.x_node_num; j < total_node; j++) {
		if (raw_data->ssr_data[j] < tx_min && raw_data->ssr_data[j] != 0)
			tx_min = raw_data->ssr_data[j];

		if (raw_data->ssr_data[j] > tx_max)
			tx_max = raw_data->ssr_data[j];
	}

	input_info(true, &client->dev, "SELF SATURATION Pass\n");

	snprintf(tx_buff, sizeof(tx_buff), "%d,%d", tx_min, tx_max);
	snprintf(rx_buff, sizeof(rx_buff), "%d,%d", rx_min, rx_max);
	sec_cmd_set_cmd_result(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
		sec_cmd_set_cmd_result_all(sec, tx_buff, strnlen(tx_buff, sizeof(tx_buff)), "SELF_SATURATION_TX");
		sec_cmd_set_cmd_result_all(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)), "SELF_SATURATION_RX");
	}
	sec->cmd_state = SEC_CMD_STATUS_OK;

out:
	if (ret < 0) {
		snprintf(rx_buff, sizeof(rx_buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)));
		if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING) {
			sec_cmd_set_cmd_result_all(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)), "SELF_SATURATION_TX");
			sec_cmd_set_cmd_result_all(sec, rx_buff, strnlen(rx_buff, sizeof(rx_buff)), "SELF_SATURATION_RX");
		}
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_ssr_rx(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 val;
	int x_node, y_node;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	val = raw_data->ssr_data[info->cap_info.y_node_num + x_node];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void get_ssr_tx(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 val;
	int x_node, y_node;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	val = raw_data->ssr_data[y_node];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void run_ssr_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[16] = { 0 };
	int total_node = info->cap_info.x_node_num + info->cap_info.y_node_num;
	char all_cmdbuff[total_node * 6];
	s32 j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(DEF_RAW_SELF_SSR_DATA_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->ssr_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	memset(all_cmdbuff, 0, sizeof(all_cmdbuff));

	for (j = 0; j < total_node; j++) {
		sprintf(buff, "%u,", raw_data->ssr_data[j]);
		strcat(all_cmdbuff, buff);
	}

	sec_cmd_set_cmd_result(sec, all_cmdbuff,
			strnlen(all_cmdbuff, sizeof(all_cmdbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void run_selfdnd_v_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd[SEC_CMD_STR_LEN] = { 0 };
	int total_node = info->cap_info.x_node_num + info->cap_info.y_node_num;
	int j, offset, val, cur_val, next_val;
	u16 screen_max = 0x0000;

	sec_cmd_set_default_result(sec);

	memset(raw_data->self_vgap_data, 0x00, TSP_CMD_NODE_NUM);

	input_info(true, &client->dev, "SELFDND V Gap start\n");

	for (j = info->cap_info.x_node_num; j < total_node - 1; j++) {
		offset = j;
		cur_val = raw_data->selfdnd_data[offset];

		if (!cur_val) {
			raw_data->self_vgap_data[offset] = cur_val;
			continue;
		}

		next_val = raw_data->selfdnd_data[offset + 1];

		if (next_val > cur_val)
			val = 100 - ((cur_val * 100) / next_val);
		else
			val = 100 - ((next_val * 100) / cur_val);

		input_info(true, &client->dev, "%d ", val);

		raw_data->self_vgap_data[offset] = val;

		if (raw_data->self_vgap_data[j] > screen_max)
			screen_max = raw_data->self_vgap_data[j];

	}
	input_info(true, &client->dev, "\n");

	input_info(true, &client->dev, "SELFDND V Gap screen_max %d\n", screen_max);
	snprintf(buff, sizeof(buff), "%d", screen_max);
	snprintf(buff_onecmd, sizeof(buff_onecmd), "%d,%d", 0, screen_max);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff_onecmd, strnlen(buff_onecmd, sizeof(buff_onecmd)), "SELF_DND_V_GAP");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	return;
}

static void run_selfdnd_h_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	char buff_onecmd[SEC_CMD_STR_LEN] = { 0 };
	int j, offset, val, cur_val, next_val;
	u16 screen_max = 0x0000;

	sec_cmd_set_default_result(sec);

	memset(raw_data->self_hgap_data, 0x00, TSP_CMD_NODE_NUM);

	input_info(true, &client->dev, "SELFDND H Gap start\n");

	for (j = 0; j < info->cap_info.x_node_num - 1; j++) {
		offset = j;
		cur_val = raw_data->selfdnd_data[offset];

		if (!cur_val) {
			raw_data->self_hgap_data[offset] = cur_val;
			continue;
		}

		next_val = raw_data->selfdnd_data[offset + 1];

		if (next_val > cur_val)
			val = 100 - ((cur_val * 100) / next_val);
		else
			val = 100 - ((next_val * 100) / cur_val);

		raw_data->self_hgap_data[offset] = val;

		if (raw_data->self_hgap_data[j] > screen_max)
			screen_max = raw_data->self_hgap_data[j];

	}

	input_info(true, &client->dev, "SELFDND H Gap screen_max %d\n", screen_max);
	snprintf(buff, sizeof(buff), "%d", screen_max);
	snprintf(buff_onecmd, sizeof(buff_onecmd), "%d,%d", 0, screen_max);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff_onecmd, strnlen(buff_onecmd, sizeof(buff_onecmd)), "SELF_DND_H_GAP");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	return;
}

static void get_selfdnd_h_gap(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_node, y_node;
	int x_num = info->cap_info.x_node_num;
	int y_num = info->cap_info.y_node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= x_num - 1 ||
		y_node < 0 || y_node >= y_num) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	sprintf(buff, "%d", raw_data->self_hgap_data[x_node]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
}

static void get_selfdnd_v_gap(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_node, y_node;
	int x_num = info->cap_info.x_node_num;
	int y_num = info->cap_info.y_node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= x_num ||
		y_node < 0 || y_node >= y_num - 1) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	sprintf(buff, "%d", raw_data->self_vgap_data[y_node]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
}


static void run_selfdnd_h_gap_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char temp[SEC_CMD_STR_LEN] = { 0 };
	char *buff = NULL;
	int total_node = info->cap_info.y_node_num;
	int j, offset, val, cur_val, next_val;

	sec_cmd_set_default_result(sec);

	buff = kzalloc(total_node * CMD_RESULT_WORD_LEN, GFP_KERNEL);
	if (!buff) {
		snprintf(temp, SEC_CMD_STR_LEN, "NG");
		sec_cmd_set_cmd_result(sec, temp, SEC_CMD_STR_LEN);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	for (j = 0; j < info->cap_info.x_node_num - 1; j++) {
		offset = j;
		cur_val = raw_data->selfdnd_data[offset];

		if (!cur_val) {
			raw_data->self_hgap_data[offset] = cur_val;
			continue;
		}

		next_val = raw_data->selfdnd_data[offset + 1];

		if (next_val > cur_val)
			val = 100 - ((cur_val * 100) / next_val);
		else
			val = 100 - ((next_val * 100) / cur_val);

		raw_data->self_hgap_data[offset] = val;

		snprintf(temp, CMD_RESULT_WORD_LEN, "%d,", raw_data->self_hgap_data[offset]);
		strncat(buff, temp, CMD_RESULT_WORD_LEN);
		memset(temp, 0x00, SEC_CMD_STR_LEN);
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, total_node * CMD_RESULT_WORD_LEN));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(buff);
}

static void run_jitter_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 min, max;
	s32 i, j;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	if (write_reg(info->client, ZT75XX_JITTER_SAMPLING_CNT, 100) != I2C_SUCCESS)
		input_info(true, &client->dev, "%s: Fail to set JITTER_CNT.\n", __func__);

	ret = ts_set_touchmode(TOUCH_JITTER_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->jitter_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	min = 0xFFFF;
	max = 0x0000;

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			if (raw_data->jitter_data[i * info->cap_info.x_node_num + j] < min &&
				raw_data->jitter_data[i * info->cap_info.x_node_num + j] != 0)
				min = raw_data->jitter_data[i * info->cap_info.x_node_num + j];

			if (raw_data->jitter_data[i * info->cap_info.x_node_num + j] > max)
				max = raw_data->jitter_data[i * info->cap_info.x_node_num + j];
		}
	}
	snprintf(buff, sizeof(buff), "%d,%d\n", min, max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	zinitix_display_rawdata(info, raw_data, TOUCH_JITTER_MODE, 0);
out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_jitter(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->jitter_data[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void run_jitter_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char temp[SEC_CMD_STR_LEN] = { 0 };
	char *buff = NULL;
	int total_node = info->cap_info.x_node_num * info->cap_info.y_node_num;
	s32 i,j;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	if (write_reg(info->client, ZT75XX_JITTER_SAMPLING_CNT, 100) != I2C_SUCCESS)
		input_info(true, &info->client->dev, "%s: Fail to set JITTER_CNT.\n", __func__);

	ts_set_touchmode(TOUCH_JITTER_MODE);
	get_raw_data(info, (u8 *)raw_data->jitter_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	buff = kzalloc(total_node * CMD_RESULT_WORD_LEN, GFP_KERNEL);
	if (!buff)
		goto NG;

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			snprintf(temp, CMD_RESULT_WORD_LEN, "%d,", raw_data->jitter_data[i * info->cap_info.x_node_num + j]);
			strncat(buff, temp, CMD_RESULT_WORD_LEN);
			memset(temp, 0x00, SEC_CMD_STR_LEN);
		}
	}
	
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, total_node * CMD_RESULT_WORD_LEN));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(buff);

NG:
	if (sec->cmd_state != SEC_CMD_STATUS_OK) {
		snprintf(temp, SEC_CMD_STR_LEN, "NG");
		sec_cmd_set_cmd_result(sec, temp, SEC_CMD_STR_LEN);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
}

#define I2C_BUFFER_SIZE 64
static bool get_raw_data_size(struct bt532_ts_info *info, u8 *buff, int skip_cnt, int sz)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	int i;
	u32 temp_sz;

	disable_irq(info->irq);

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		input_err(true, &client->dev, "%s: other process occupied. (%d)\n",
			__func__, info->work_state);
		enable_irq(info->irq);
		up(&info->work_lock);
		return false;
	}

	info->work_state = RAW_DATA;

	for (i = 0; i < skip_cnt; i++) {
		while (gpio_get_value(pdata->gpio_int))
			usleep_range(1 * 1000, 1 * 1000);

		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		usleep_range(1 * 1000, 1 * 1000);
	}

	while (gpio_get_value(pdata->gpio_int))
		usleep_range(1 * 1000, 1 * 1000);

	for (i = 0; sz > 0; i++) {
		temp_sz = I2C_BUFFER_SIZE;

		if (sz < I2C_BUFFER_SIZE)
			temp_sz = sz;
		if (read_raw_data(client, BT532_RAWDATA_REG + i,
			(char *)(buff + (i * I2C_BUFFER_SIZE)), temp_sz) < 0) {

			input_err(true, &info->client->dev, "error : read zinitix tc raw data\n");
			info->work_state = NOTHING;
			enable_irq(info->irq);
			up(&info->work_lock);
			return false;
		}
		sz -= I2C_BUFFER_SIZE;
	}

	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);

	return true;
}

static void run_reference_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int min = 0xFFFF, max = 0x0000;
	s32 i, j, touchkey_node = 2;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ret = ts_set_touchmode(TOUCH_REFERENCE_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data_size(info, (u8 *)raw_data->reference_data, 2,
		info->cap_info.total_node_num * 2 + info->cap_info.y_node_num + info->cap_info.x_node_num);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true, &client->dev, "%s start\n",__func__);

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			if (i == (info->cap_info.x_node_num-1) && info->pdata->support_touchkey){
				if ((j == touchkey_node) || (j == (info->cap_info.x_node_num - 1) - touchkey_node)) {
					if (raw_data->reference_data[(i * info->cap_info.x_node_num) + j] < min &&
						raw_data->reference_data[(i * info->cap_info.x_node_num) + j] >= 0)
						min = raw_data->reference_data[(i * info->cap_info.x_node_num) + j];

					if (raw_data->reference_data[(i * info->cap_info.x_node_num) + j] > max)
						max = raw_data->reference_data[(i * info->cap_info.x_node_num) + j];
				}
			} else {
				if (raw_data->reference_data[(i * info->cap_info.x_node_num) + j] < min &&
					raw_data->reference_data[(i * info->cap_info.x_node_num) + j] >= 0)
					min = raw_data->reference_data[(i * info->cap_info.x_node_num) + j];

				if (raw_data->reference_data[(i * info->cap_info.x_node_num) + j] > max)
					max = raw_data->reference_data[(i * info->cap_info.x_node_num) + j];
			}
		}
	}

	snprintf(buff, sizeof(buff), "%d,%d\n", min, max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	zinitix_display_rawdata(info, raw_data, TOUCH_REFERENCE_MODE, 0);
out:
	if (ret < 0) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_reference(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;

		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->reference_data[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void run_self_sat_dnd_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 min, max;
	s32 j;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	ts_set_self_sat_touchmode(TOUCH_SELF_DND_MODE);
	get_raw_data_size(info, (u8 *)raw_data->self_sat_dnd_data, 1, 32);
	misc_info->touch_mode = TOUCH_POINT_MODE;
	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);
	clear_report_data(info);
	mini_init_touch(info);

	input_info(true,&client->dev, "SELF SAT DND start\n");

	min = 0xFFFF;
	max = 0x0000;

	for (j = 0; j < info->cap_info.y_node_num; j++) {
		input_info(true, &client->dev, "%d ", raw_data->self_sat_dnd_data[j]);

		if (raw_data->self_sat_dnd_data[j] < min && raw_data->self_sat_dnd_data[j] != 0)
			min = raw_data->self_sat_dnd_data[j];

		if (raw_data->self_sat_dnd_data[j] > max)
			max = raw_data->self_sat_dnd_data[j];
	}
	input_info(true, &client->dev, "\n");

	input_info(true, &client->dev, "SELF SAT DND Pass\n");

	snprintf(buff, sizeof(buff), "%d,%d\n", min, max);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_self_sat_dnd(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;

	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	val = raw_data->self_sat_dnd_data[y_node];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

static void run_tsp_rawdata_read(void *device_data, u16 rawdata_mode, s16* buff)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;
	int i, j, ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(misc_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	ret = ts_set_touchmode(rawdata_mode);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)buff, 2);
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true, &info->client->dev, "touch rawdata %d start\n", rawdata_mode);

	for (i = 0; i < y_num; i++) {
		input_info(true, &info->client->dev, "[%5d] :", i);
		for (j = 0; j < x_num; j++) {
			input_info(true, &info->client->dev, "%06d ", buff[(i * x_num) + j]);
		}
		input_info(true, &info->client->dev, "\n");
	}
out:
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(misc_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

#ifdef TCLM_CONCEPT
/*
## Mis Cal result ##
FD : spec out
F3,F4 : i2c faile
F2 : power off state
F1 : not support mis cal concept
F0 : initial value in function
00 : pass
*/

static void run_mis_cal_read(void * device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int x_num = info->cap_info.x_node_num, y_num = info->cap_info.y_node_num;
	int i, j, offset;
	char mis_cal_data = 0xF0;
	int ret = 0;
	s16 raw_data_buff[TSP_CMD_NODE_NUM];
	u16 chip_eeprom_info;
	int min = 0xFFFF, max = -0xFF;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	disable_irq(info->irq);
	sec_cmd_set_default_result(sec);

	if (pdata->mis_cal_check == 0) {
		input_info(true, &info->client->dev, "%s: [ERROR] not support\n", __func__);
		mis_cal_data = 0xF1;
		goto NG;
	}

	if (info->work_state == SUSPEND) {
		input_info(true, &info->client->dev, "%s: [ERROR] Touch is stopped\n",__func__);
		mis_cal_data = 0xF2;
		goto NG;
	}

	if (read_data(info->client, BT532_EEPROM_INFO, (u8 *)&chip_eeprom_info, 2) < 0){
		input_info(true, &info->client->dev, "%s: read eeprom_info i2c fail!\n", __func__);
		mis_cal_data = 0xF3;
		goto NG;
	}

	if (zinitix_bit_test(chip_eeprom_info, 0)){
		input_info(true, &info->client->dev, "%s: eeprom cal 0, skip !\n", __func__);
		mis_cal_data = 0xF1;
		goto NG;
	}

	ret = ts_set_touchmode(TOUCH_REF_ABNORMAL_TEST_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		mis_cal_data = 0xF4;
		goto NG;
	}
	ret = get_raw_data(info, (u8 *)raw_data->reference_data_abnormal, 2);
	if (!ret) {
		input_info(true, &info->client->dev, "%s:[ERROR] i2c fail!\n", __func__);
		ts_set_touchmode(TOUCH_POINT_MODE);
		mis_cal_data = 0xF4;
		goto NG;
	}
	ts_set_touchmode(TOUCH_POINT_MODE);

	input_info(true, &info->client->dev, "%s start\n", __func__);

	ret = 1;
	for (i = 0; i < y_num; i++) {
		for (j = 0; j < x_num; j++) {
			offset = (i * x_num) + j;

			if (raw_data->reference_data_abnormal[offset] < min)
				min = raw_data->reference_data_abnormal[offset];

			if (raw_data->reference_data_abnormal[offset] > max)
				max = raw_data->reference_data_abnormal[offset];
		}
	}
	if (!ret)
		goto NG;

	mis_cal_data = 0x00;
	snprintf(buff, sizeof(buff), "%d,%d", min, max);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "MIS_CAL");
	sec->cmd_state = SEC_CMD_STATUS_OK;
 	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));
	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif

	zinitix_display_rawdata(info, raw_data, TOUCH_REF_ABNORMAL_TEST_MODE, 0);

	return;
NG:
	snprintf(buff, sizeof(buff), "%s_%d", "NG", mis_cal_data);

	if (mis_cal_data == 0xFD) {
		run_tsp_rawdata_read(device_data, 7, raw_data_buff);
		run_tsp_rawdata_read(device_data, TOUCH_REFERENCE_MODE, raw_data_buff);
	}
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "MIS_CAL");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
 	input_info(true, &client->dev, "%s: \"%s\"(%d)\n", __func__, sec->cmd_result,
				(int)strlen(sec->cmd_result));
	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void get_mis_cal(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	unsigned int val;
	int x_node, y_node;
	int node_num;

	disable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	sec_cmd_set_default_result(sec);

	x_node = sec->cmd_param[0];
	y_node = sec->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(buff, sizeof(buff), "%s", "abnormal");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
		esd_timer_start(CHECK_ESD_TIMER, misc_info);
		write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
		return;
	}

	node_num = y_node * info->cap_info.x_node_num + x_node;

	val = raw_data->reference_data_abnormal[node_num];
	snprintf(buff, sizeof(buff), "%u", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void run_mis_cal_read_all(void * device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct tsp_raw_data *raw_data = info->raw_data;
	char temp[SEC_CMD_STR_LEN] = { 0 };
	char *buff = NULL;
	int total_node = info->cap_info.x_node_num * info->cap_info.y_node_num;
	int i, j, offset;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	disable_irq(info->irq);

	sec_cmd_set_default_result(sec);

	ts_set_touchmode(TOUCH_POINT_MODE);

	buff = kzalloc(total_node * CMD_RESULT_WORD_LEN, GFP_KERNEL);
	if (!buff)
		goto NG;

	for (i = 0; i < info->cap_info.y_node_num ; i++) {
		for (j = 0; j < info->cap_info.x_node_num ; j++) {
			offset = (i * info->cap_info.x_node_num) + j;
			snprintf(temp, CMD_RESULT_WORD_LEN, "%d,", raw_data->reference_data_abnormal[offset]);
			strncat(buff, temp, CMD_RESULT_WORD_LEN);
			memset(temp, 0x00, SEC_CMD_STR_LEN);
		}
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, total_node * CMD_RESULT_WORD_LEN));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	kfree(buff);
	
NG:
	if (sec->cmd_state != SEC_CMD_STATUS_OK) {
		snprintf(temp, SEC_CMD_STR_LEN, "NG");
		sec_cmd_set_cmd_result(sec, temp, SEC_CMD_STR_LEN);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
}

#ifdef TCLM_CONCEPT
static void get_pat_information(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[50] = { 0 };

	sec_cmd_set_default_result(sec);

	snprintf(buff, sizeof(buff), "C%02XT%04X.%4s%s%c%d%c%d%c%d",
		info->tdata->nvdata.cal_count, info->tdata->nvdata.tune_fix_ver, info->tdata->tclm_string[info->tdata->nvdata.cal_position].f_name,
		(info->tdata->tclm_level == TCLM_LEVEL_LOCKDOWN) ? ".L " : " ",
		info->tdata->cal_pos_hist_last3[0], info->tdata->cal_pos_hist_last3[1],
		info->tdata->cal_pos_hist_last3[2], info->tdata->cal_pos_hist_last3[3],
		info->tdata->cal_pos_hist_last3[4], info->tdata->cal_pos_hist_last3[5]);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
}

/* FACTORY TEST RESULT SAVING FUNCTION
 * bit 3 ~ 0 : OCTA Assy
 * bit 7 ~ 4 : OCTA module
 * param[0] : OCTA module(1) / OCTA Assy(2)
 * param[1] : TEST NONE(0) / TEST FAIL(1) / TEST PASS(2) : 2 bit
 */
static void get_tsp_test_result(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char cbuff[SEC_CMD_STR_LEN] = { 0 };
	u8 buff[2] = {0};

	sec_cmd_set_default_result(sec);

	get_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_FAC_RESULT, (u8 *)buff, 2);
	info->test_result.data[0] = buff[0];

	input_info(true, &info->client->dev, "%s : %X", __func__, info->test_result.data[0]);

	if (info->test_result.data[0] == 0xFF) {
		input_info(true, &info->client->dev, "%s: clear factory_result as zero\n", __func__);
		info->test_result.data[0] = 0;
	}

	snprintf(cbuff, sizeof(cbuff), "M:%s, M:%d, A:%s, A:%d",
			info->test_result.module_result == 0 ? "NONE" :
				info->test_result.module_result == 1 ? "FAIL" : "PASS",
			info->test_result.module_count,
			info->test_result.assy_result == 0 ? "NONE" :
				info->test_result.assy_result == 1 ? "FAIL" : "PASS",
			info->test_result.assy_count);

	sec_cmd_set_cmd_result(sec, cbuff, strnlen(cbuff, sizeof(cbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
}

static void set_tsp_test_result(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char cbuff[SEC_CMD_STR_LEN] = { 0 };
	u8 buff[2] = {0};

	sec_cmd_set_default_result(sec);

	get_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_FAC_RESULT, (u8 *)buff, 2);
	info->test_result.data[0] = buff[0];

	input_info(true, &info->client->dev, "%s : %X", __func__, info->test_result.data[0]);

	if (info->test_result.data[0] == 0xFF) {
		input_info(true, &info->client->dev, "%s: clear factory_result as zero\n", __func__);
		info->test_result.data[0] = 0;
	}

	if (sec->cmd_param[0] == TEST_OCTA_ASSAY) {
		info->test_result.assy_result = sec->cmd_param[1];
		if (info->test_result.assy_count < 3)
			info->test_result.assy_count++;

	} else if (sec->cmd_param[0] == TEST_OCTA_MODULE) {
		info->test_result.module_result = sec->cmd_param[1];
		if (info->test_result.module_count < 3)
			info->test_result.module_count++;
	}

	input_info(true, &info->client->dev, "%s: [0x%X] M:%s, M:%d, A:%s, A:%d\n",
					__func__, info->test_result.data[0],
					info->test_result.module_result == 0 ? "NONE" :
						info->test_result.module_result == 1 ? "FAIL" : "PASS",
					info->test_result.module_count,
					info->test_result.assy_result == 0 ? "NONE" :
						info->test_result.assy_result == 1 ? "FAIL" : "PASS",
					info->test_result.assy_count);

	set_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_FAC_RESULT, &info->test_result.data[0], 1);

	snprintf(cbuff, sizeof(cbuff), "OK");
	sec_cmd_set_cmd_result(sec, cbuff, strnlen(cbuff, sizeof(cbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
}

static void increase_disassemble_count(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u8 count[2] = { 0 };

	sec_cmd_set_default_result(sec);

	if (info->tsp_pwr_enabled == POWER_OFF) {
		input_err(true, &info->client->dev, "%s: IC is power off\n", __func__);
		snprintf(buff, sizeof(buff), "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	get_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_DISASSEMBLE_COUNT, count, 2);
	input_info(true, &info->client->dev, "%s: current disassemble count: %d\n", __func__, count[0]);

	if (count[0] == 0xFF)
		count[0] = 0;
	if (count[0] < 0xFE)
		count[0]++;

	set_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_DISASSEMBLE_COUNT, count , 2);

	msleep(5);

	memset(count, 0x00, 2);
	get_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_DISASSEMBLE_COUNT, count, 2);
	input_info(true, &info->client->dev, "%s: check disassemble count: %d\n", __func__, count[0]);

	snprintf(buff, sizeof(buff), "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

}

static void get_disassemble_count(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u8 count[2] = { 0 };

	sec_cmd_set_default_result(sec);

	if (info->tsp_pwr_enabled == POWER_OFF) {
		input_err(true, &info->client->dev, "%s: IC is power off\n", __func__);
		snprintf(buff, sizeof(buff), "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	get_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_DISASSEMBLE_COUNT, count, 2);
	if (count[0] == 0xFF) {
		count[0] = 0;
		count[1] = 0;
		set_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_DISASSEMBLE_COUNT, count , 2);
	}

	input_info(true, &info->client->dev, "%s: read disassemble count: %d\n", __func__, count[0]);
	snprintf(buff, sizeof(buff), "%d", count[0]);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
}

#define DEF_IUM_ADDR_OFFSET		0xF0A0
#define DEF_IUM_LOCK			0xF0F6
#define DEF_IUM_UNLOCK			0xF0FA

int get_zt_tsp_nvm_data(struct bt532_ts_info *info, u8 addr, u8 *values, u16 length)
{
	struct i2c_client *client = info->client;
	u16 buff_start;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	disable_irq(info->irq);

	if (write_cmd(client, DEF_IUM_LOCK) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed ium lock\n");
		goto fail_ium_random_read;
	}
	msleep(40);

	buff_start = addr;	//custom setting address(0~62, 0,2,4,6)
	//length = 2;		// custom setting(max 64)
	if (length > TC_NVM_SECTOR_SZ)
		length = TC_NVM_SECTOR_SZ;
	if (length < 2){
		length = 2;	//read 2byte
	}

	if (read_raw_data(client, buff_start + DEF_IUM_ADDR_OFFSET,
			values, length) < 0) {
		input_err(true, &client->dev, "Failed to read raw data %d\n", length);
		goto fail_ium_random_read;
	}

	if (write_cmd(client, DEF_IUM_UNLOCK) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed ium unlock\n");
		goto fail_ium_random_read;
	}

	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return 0;

fail_ium_random_read:

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	mini_init_touch(info);

	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return -1;
}

int set_zt_tsp_nvm_data(struct bt532_ts_info *info, u8 addr, u8 *values, u16 length)
{
	struct i2c_client *client = info->client;
	u8 buff[64];
	u16 buff_start;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	disable_irq(info->irq);

	if (write_cmd(client, DEF_IUM_LOCK) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed ium lock\n");
		goto fail_ium_random_write;
	}

	buff_start = addr;	//custom setting address(0~62, 0,2,4,6)

	memcpy((u8 *)&buff[buff_start], values, length);

	/* data write start */
	if (length > TC_NVM_SECTOR_SZ)
		length = TC_NVM_SECTOR_SZ;
	if (length < 2){
		length = 2;	//write 2byte
		buff[buff_start+1] = 0;
	}

	if (write_data(client, buff_start + DEF_IUM_ADDR_OFFSET,
			(u8 *)&buff[buff_start], length) < 0) {
		input_err(true, &client->dev, "error : write zinitix tc firmare\n");
		goto fail_ium_random_write;
	}
	/* data write end */

	/* for save rom start */
	if (write_reg(client, 0xc104, 0x0001) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed to write nvm wp disable\n");
		goto fail_ium_random_write;
	}
	mdelay(10);

	if (write_cmd(client, 0xF0F8) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed save ium\n");
		goto fail_ium_random_write;
	}
	mdelay(30);

	if (write_reg(client, 0xc104, 0x0000) != I2C_SUCCESS) {
		input_err(true, &client->dev, "nvm wp enable\n");
		goto fail_ium_random_write;
	}
	mdelay(10);
	/* for save rom end */

	if (write_cmd(client, DEF_IUM_UNLOCK) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed ium unlock\n");
		goto fail_ium_random_write;
	}

	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return 0;

fail_ium_random_write:
	if (write_reg(client, 0xc104, 0x0000) != I2C_SUCCESS) {
		input_err(true, &client->dev, "nvm wp enable\n");
	}
	mdelay(10);

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	mini_init_touch(info);

	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return -1;
}

int bt532_tclm_data_read(struct i2c_client *client, int address)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	int i, ret = 0;
	u8 buff[10];
	u8 nbuff[BT532_TS_NVM_OFFSET_LENGTH];

	switch (address) {
	case SEC_TCLM_NVM_OFFSET_IC_FIRMWARE_VER:
		ret = read_data(client, BT532_MINOR_FW_VERSION, buff, 2);
		if (ret < 0) {
			input_err(true, &info->client->dev,"%s: fail fw_minor_version\n", __func__);
			return ret;
		}

		ret = read_data(client, BT532_FIRMWARE_VERSION, (u8*)&buff[2], 4);
		if (ret < 0) {
			input_err(true, &info->client->dev,"%s: fail chip_revision\n", __func__);
			return ret;
		}
		/* ((fw_version & 0xf) << 12) | ((fw_minor_version & 0xf) << 8) | (reg_version & 0xff); */
		ret = ((buff[2] & 0xf) << 12) | ((buff[0] & 0xf) << 8) | (buff[4] & 0xff);
		return ret;

	case SEC_TCLM_NVM_ALL_DATA:
		/* Zinitx driver support index read/write so do not need read FAC_RESULT and DISASSEMBLE_COUNT here
		  * length minus the first 4 bytes
		  */
		ret = get_zt_tsp_nvm_data(info, 4, &nbuff[4], BT532_TS_NVM_OFFSET_LENGTH - 4);
		if (ret < 0)
			return ret;

		info->tdata->nvdata.cal_count = nbuff[BT532_TS_NVM_OFFSET_CAL_COUNT];
		info->tdata->nvdata.tune_fix_ver = (nbuff[BT532_TS_NVM_OFFSET_TUNE_VERSION] << 8) | nbuff[BT532_TS_NVM_OFFSET_TUNE_VERSION + 1];
		info->tdata->nvdata.cal_position = nbuff[BT532_TS_NVM_OFFSET_CAL_POSITION];
		info->tdata->nvdata.cal_pos_hist_cnt = nbuff[BT532_TS_NVM_OFFSET_HISTORY_QUEUE_COUNT];
		info->tdata->nvdata.cal_pos_hist_lastp = nbuff[BT532_TS_NVM_OFFSET_HISTORY_QUEUE_LASTP];
		for (i = BT532_TS_NVM_OFFSET_HISTORY_QUEUE_ZERO; i < BT532_TS_NVM_OFFSET_LENGTH; i++)
			info->tdata->nvdata.cal_pos_hist_queue[i - BT532_TS_NVM_OFFSET_HISTORY_QUEUE_ZERO] = nbuff[i];

		input_err(true, &info->client->dev, "%s: %d %X %x %d %d\n", __func__,
			info->tdata->nvdata.cal_count, info->tdata->nvdata.tune_fix_ver, info->tdata->nvdata.cal_position,
			info->tdata->nvdata.cal_pos_hist_cnt, info->tdata->nvdata.cal_pos_hist_lastp);

		return ret;
	default:
	return ret;
	}
}

int bt532_tclm_data_write(struct i2c_client *client, int address)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	int i, ret = 1;
	u8 nbuff[BT532_TS_NVM_OFFSET_LENGTH];

	memset(&nbuff[4], 0x00, BT532_TS_NVM_OFFSET_LENGTH - 4);

	nbuff[BT532_TS_NVM_OFFSET_CAL_COUNT] = info->tdata->nvdata.cal_count;
	nbuff[BT532_TS_NVM_OFFSET_TUNE_VERSION] = (u8)(info->tdata->nvdata.tune_fix_ver >> 8);
	nbuff[BT532_TS_NVM_OFFSET_TUNE_VERSION + 1] = (u8)(0xff & info->tdata->nvdata.tune_fix_ver);
	nbuff[BT532_TS_NVM_OFFSET_CAL_POSITION] = info->tdata->nvdata.cal_position;
	nbuff[BT532_TS_NVM_OFFSET_HISTORY_QUEUE_COUNT] = info->tdata->nvdata.cal_pos_hist_cnt;
	nbuff[BT532_TS_NVM_OFFSET_HISTORY_QUEUE_LASTP] = info->tdata->nvdata.cal_pos_hist_lastp;
	for (i = BT532_TS_NVM_OFFSET_HISTORY_QUEUE_ZERO; i < BT532_TS_NVM_OFFSET_LENGTH; i++)
		nbuff[i] = info->tdata->nvdata.cal_pos_hist_queue[i - BT532_TS_NVM_OFFSET_HISTORY_QUEUE_ZERO];

	ret = set_zt_tsp_nvm_data(info, 4, &nbuff[4], BT532_TS_NVM_OFFSET_LENGTH - 4);
	if (ret < 0) {
		input_err(true, &info->client->dev, "%s: [ERROR] set_tsp_nvm_data ret:%d\n", __func__, ret);
	}

	return ret;
}
#endif

#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
static void ium_random_write(struct bt532_ts_info *info, u8 data)
{
	struct i2c_client *client = info->client;
	u8 buff[64]; // custom data buffer
	u16 length, buff_start;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	disable_irq(info->irq);

	input_info(true, &client->dev, "%s %x %d", __func__, data, data);

	if (write_cmd(client, DEF_IUM_LOCK) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed ium lock\n");
		goto fail_ium_random_write;
	}

	//for ( i=0 ; i<64 ; i++)
		buff[data] = data;
		buff[data+1] = data;

	buff_start = data;	//custom setting address(0~62)
	length = 2;		// custom odd number setting(max 64)
	if (length > TC_SECTOR_SZ)
		length = TC_SECTOR_SZ;
	if (write_data(client, buff_start + DEF_IUM_ADDR_OFFSET,
			(u8 *)&buff[buff_start], length) < 0) {
		input_err(true, &client->dev, "error : write zinitix tc firmare\n");
		goto fail_ium_random_write;
	}

	if (write_reg(client, 0xc104, 0x0001) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed to write nvm wp disable\n");
		goto fail_ium_random_write;
	}
	mdelay(10);

	if (write_cmd(client, 0xF0F8) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed save ium\n");
		goto fail_ium_random_write;
	}
	mdelay(30);

	if (write_reg(client, 0xc104, 0x0000) != I2C_SUCCESS) {
		input_err(true, &client->dev, "nvm wp enable\n");
	}
	mdelay(10);

	if (write_cmd(client, DEF_IUM_UNLOCK) != I2C_SUCCESS) {
		input_err(true, &client->dev, "failed ium unlock\n");
		goto fail_ium_random_write;
	}

	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	input_info(true, &client->dev, "%s %d", __func__, __LINE__);
	return;

fail_ium_random_write:
	if (write_reg(client, 0xc104, 0x0000) != I2C_SUCCESS) {
		input_err(true, &client->dev, "nvm wp enable\n");
	}
	mdelay(10);

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	enable_irq(info->irq);
	return;
}


static void ium_random_read(struct bt532_ts_info *info, u8 data)
{
	struct i2c_client *client = info->client;
	u8 buff[64]; // custom data buffer
	u16 length, buff_start;

	disable_irq(info->irq);

	buff_start = 8;	//custom setting address(0~62)
	length = 2;		// custom setting(max 64)
	if (length > TC_SECTOR_SZ)
		length = TC_SECTOR_SZ;

	if (read_raw_data(client, data + DEF_IUM_ADDR_OFFSET,
			(u8 *)&buff[data], length) < 0) {
		input_err(true, &client->dev, "Failed to read raw data %d\n", length);
		goto fail_ium_random_read;
	}

	enable_irq(info->irq);

	input_info(true, &info->client->dev, "%s %x %d", __func__, buff[data], buff[data]);

	return;

fail_ium_random_read:

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	enable_irq(info->irq);
	return;
}

static void ium_r_write(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec->cmd_param[1] = sec->cmd_param[0];

	input_info(true, &info->client->dev, "%s %x %x", __func__, sec->cmd_param[0], sec->cmd_param[1]);

	set_zt_tsp_nvm_data(info, sec->cmd_param[0], (u8 *)&sec->cmd_param[0], 2);

	sec_cmd_set_default_result(sec);

	ium_random_write(info, sec->cmd_param[0]);

	snprintf(buff, sizeof(buff), "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	return;
}

static void ium_r_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u8 val = sec->cmd_param[0];

	ium_random_read(info, val);

	sec_cmd_set_default_result(sec);
	snprintf(buff, sizeof(buff), "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	return;
}

static void ium_write(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char cbuff[SEC_CMD_STR_LEN] = { 0 };
	int i;
	u8 temp[10];
	u8 buff[64]; // custom data buffer
	u8 val = sec->cmd_param[0];

	for (i = 0 ; i < 64 ; i++)
		buff[i] = val;

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON);

	write_reg(client, 0xC000, 0x0001);
	write_reg(client, 0xC004, 0x0001);

	for (i = 0 ; i < 16 ; i++) {
		temp[0] = i;
		temp[1] = 0x00;
		temp[2] = buff[i*4];
		temp[3] = buff[i*4+1];
		temp[4] = buff[i*4+2];
		temp[5] = buff[i*4+3];
		write_data(client, 0xC020, temp, 6);
	}

	write_reg(client, 0xC003, 0x0001);
	temp[0] = 0x00;
	temp[1] = 0x00;
	temp[2] = 0x01;
	temp[3] = 0x00;
	write_data(client, 0xC10D, temp, 4);
	mdelay(5);

	temp[0] = 0x10;
	temp[1] = 0x00;
	temp[2] = 0x00;
	temp[3] = 0x00;
	temp[4] = 0x00;
	temp[5] = 0x20;
	temp[6] = 0xe0;
	temp[7] = 0x00;
	temp[8] = 0x00;
	temp[9] = 0x40;
	write_data(client, 0xC10B, temp, 10);
	msleep(5);

	write_reg(client, 0xC003, 0x0000);
	msleep(5);

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);
	mini_init_touch(info);

	sec_cmd_set_default_result(sec);
	snprintf(cbuff, sizeof(cbuff), "OK");
	sec_cmd_set_cmd_result(sec, cbuff, strnlen(cbuff, sizeof(cbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
	return;
}

static void ium_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char cbuff[SEC_CMD_STR_LEN] = { 0 };
	int i;
	u8 temp[8];
	u8 buff[64]; // custom data buffer

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON);

	write_reg(client, 0xC000, 0x0001);

	temp[0] = 0x0C;
	temp[1] = 0x00;
	temp[2] = 0x02;
	temp[3] = 0x20;
	temp[4] = 0x03;
	temp[5] = 0x00;
	temp[6] = 0x00;
	temp[7] = 0x00;
	write_data(client, 0xCC02, temp, 8);

	for (i = 0 ; i < 16 ; i++) {
		temp[0] = i*4;
		temp[1] = 0x00;
		temp[2] = 0x00;
		temp[3] = 0x20;
		write_data(client, 0xCC01, temp, 4);
		read_data_only(client, buff + i*4, 4);
	}
	temp[0] = 0x0C;
	temp[1] = 0x00;
	temp[2] = 0x02;
	temp[3] = 0x20;
	temp[4] = 0x02;
	temp[5] = 0x00;
	temp[6] = 0x00;
	temp[7] = 0x00;
	write_data(client, 0xCC02, temp, 8);
	msleep(5);

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);
	mini_init_touch(info);

	for (i = 63 ; i > 0 ; i -= 4) {
		input_info(true, &client->dev, "%s: [%2d]:%02X %02X %02X %02X\n",
							__func__, i, buff[i], buff[i-1], buff[i-2], buff[i-3]);
	}

	sec_cmd_set_default_result(sec);
	snprintf(cbuff, sizeof(cbuff), "OK");
	sec_cmd_set_cmd_result(sec, cbuff, strnlen(cbuff, sizeof(cbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
	return;

}
#endif
#endif

/*
 *	flag     1  :  set edge handler
 *		2  :  set (portrait, normal) edge zone data
 *		4  :  set (portrait, normal) dead zone data
 *		8  :  set landscape mode data
 *		16 :  mode clear
 *	data
 *		0xAA, FFF (y start), FFF (y end),  FF(direction)
 *		0xAB, FFFF (edge zone)
 *		0xAC, FF (up x), FF (down x), FFFF (y)
 *		0xAD, FF (mode), FFF (edge), FFF (dead zone x), FF (dead zone top y), FF (dead zone bottom y)
 *	case
 *		edge handler set :  0xAA....
 *		booting time :  0xAA...  + 0xAB...
 *		normal mode : 0xAC...  (+0xAB...)
 *		landscape mode : 0xAD...
 *		landscape -> normal (if same with old data) : 0xAD, 0
 *		landscape -> normal (etc) : 0xAC....  + 0xAD, 0
 */

void set_grip_data_to_ic(struct bt532_ts_info *ts, u8 flag)
{
	struct i2c_client *client = ts->client;

	input_info(true, &ts->client->dev, "%s: flag: %02X (clr,lan,nor,edg,han)\n", __func__, flag);

	write_cmd(client, 0x0A);

	if (flag & G_SET_EDGE_HANDLER) {
		if (ts->grip_edgehandler_direction == 0) {
			ts->grip_edgehandler_start_y = 0x0;
			ts->grip_edgehandler_end_y = 0x0;
		}

		if (write_reg(client, ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_START,
			ts->grip_edgehandler_start_y) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set except start y error\n");
		}

		if (write_reg(client, ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_END,
			ts->grip_edgehandler_end_y) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set except end y error\n");
		}
	
		if (write_reg(client, ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_SEL,
			(ts->grip_edgehandler_direction) & 0x0003) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set except direct error\n");
		}

		input_info(true, &ts->client->dev, "%s: 0x%02X %02X, 0x%02X %02X, 0x%02X %02X\n", __func__,
				ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_SEL, ts->grip_edgehandler_direction,
				ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_START, ts->grip_edgehandler_start_y,
				ZT75XX_EDGE_REJECT_PORT_EDGE_EXCEPT_END, ts->grip_edgehandler_end_y);
	}

	if (flag & G_SET_EDGE_ZONE) {
		if (write_reg(client, ZT75XX_EDGE_GRIP_PORT_SIDE_WIDTH, ts->grip_edge_range) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set grip side width error\n");
		}

		input_info(true, &ts->client->dev, "%s: 0x%02X %02X\n", __func__,
				ZT75XX_EDGE_GRIP_PORT_SIDE_WIDTH, ts->grip_edge_range);
	}

	if (flag & G_SET_NORMAL_MODE) {
		if (write_reg(client, ZT75XX_EDGE_REJECT_PORT_SIDE_UP_WIDTH, ts->grip_deadzone_up_x) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set dead zone up x error\n");
		}

		if (write_reg(client, ZT75XX_EDGE_REJECT_PORT_SIDE_DOWN_WIDTH, ts->grip_deadzone_dn_x) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set dead zone down x error\n");
		}

		if (write_reg(client, ZT75XX_EDGE_REJECT_PORT_SIDE_UP_DOWN_DIV, ts->grip_deadzone_y) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set dead zone up/down div location error\n");
		}		

		input_info(true, &ts->client->dev, "%s: 0x%02X %02X, 0x%02X %02X, 0x%02X %02X\n", __func__,
				ZT75XX_EDGE_REJECT_PORT_SIDE_UP_WIDTH, ts->grip_deadzone_up_x,
				ZT75XX_EDGE_REJECT_PORT_SIDE_DOWN_WIDTH, ts->grip_deadzone_dn_x,
				ZT75XX_EDGE_REJECT_PORT_SIDE_UP_DOWN_DIV, ts->grip_deadzone_y);
	}

	if (flag & G_SET_LANDSCAPE_MODE) {
		if (write_reg(client, ZT75XX_EDGE_LANDSCAPE_MODE, ts->grip_landscape_mode & 0x1) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set landscape mode error\n");
		}

		if (write_reg(client, ZT75XX_EDGE_GRIP_LAND_SIDE_WIDTH, ts->grip_landscape_edge) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set landscape side edge error\n");
		}

		if (write_reg(client, ZT75XX_EDGE_REJECT_LAND_SIDE_WIDTH, ts->grip_landscape_deadzone) != I2C_SUCCESS) {
			input_err(true, &client->dev, "set landscape side deadzone error\n");
		}

		if (write_reg(client, ZT75XX_EDGE_REJECT_LAND_TOP_BOT_WIDTH,
			(((ts->grip_landscape_top_deadzone << 8) & 0xFF00) | (ts->grip_landscape_bottom_deadzone & 0x00FF)))
			!= I2C_SUCCESS) {
			input_err(true, &client->dev, "set landscape top bot deazone error\n");
		}

		if (write_reg(client, ZT75XX_EDGE_GRIP_LAND_TOP_BOT_WIDTH,
			(((ts->grip_landscape_top_gripzone << 8) & 0xFF00) | (ts->grip_landscape_bottom_gripzone & 0x00FF)))
			!= I2C_SUCCESS) {
			input_err(true, &client->dev, "set landscape top bot gripzone error\n");
		}

		input_info(true, &ts->client->dev,
				"%s: 0x%02X %02X, 0x%02X %02X, 0x%02X %02X, 0x%02X %02X, 0x%02X %02X\n", __func__,
				ZT75XX_EDGE_LANDSCAPE_MODE, ts->grip_landscape_mode & 0x1,
				ZT75XX_EDGE_GRIP_LAND_SIDE_WIDTH, ts->grip_landscape_edge,
				ZT75XX_EDGE_REJECT_LAND_SIDE_WIDTH, ts->grip_landscape_deadzone,
				ZT75XX_EDGE_REJECT_LAND_TOP_BOT_WIDTH,
				((ts->grip_landscape_top_deadzone << 8) & 0xFF00) | (ts->grip_landscape_bottom_deadzone & 0x00FF),
				ZT75XX_EDGE_GRIP_LAND_TOP_BOT_WIDTH,
				((ts->grip_landscape_top_gripzone << 8) & 0xFF00) | (ts->grip_landscape_bottom_gripzone & 0x00FF));
	}

	if (flag & G_CLR_LANDSCAPE_MODE) {
		if (write_reg(client, ZT75XX_EDGE_LANDSCAPE_MODE, ts->grip_landscape_mode) != I2C_SUCCESS) { 
			input_err(true, &client->dev, "clr landscape mode error\n");
		}

		input_info(true, &ts->client->dev, "%s: 0x%02X %02X\n", __func__,
				ZT75XX_EDGE_LANDSCAPE_MODE, ts->grip_landscape_mode);
	}

	write_cmd(client, 0x0B);
}

/*
 *	index  0 :  set edge handler
 *		1 :  portrait (normal) mode
 *		2 :  landscape mode
 *
 *	data
 *		0, X (direction), X (y start), X (y end)
 *		direction : 0 (off), 1 (left), 2 (right)
 *			ex) echo set_grip_data,0,2,600,900 > cmd
 *
 *		1, X (edge zone), X (dead zone up x), X (dead zone down x), X (dead zone y)
 *			ex) echo set_grip_data,1,200,10,50,1500 > cmd
 *
 *		2, 1 (landscape mode), X (edge zone), X (dead zone x), X (dead zone top y), X (dead zone bottom y), X (edge zone top y), X (edge zone bottom y)
 *			ex) echo set_grip_data,2,1,200,100,120,0 > cmd
 *
 *		2, 0 (portrait mode)
 *			ex) echo set_grip_data,2,0  > cmd
 */

static void set_grip_data(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *ts = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u8 mode = G_NONE;

	sec_cmd_set_default_result(sec);

	memset(buff, 0, sizeof(buff));

	if (sec->cmd_param[0] == 0) {	// edge handler
		if (sec->cmd_param[1] == 0) {	// clear
			ts->grip_edgehandler_direction = 0;
		} else if (sec->cmd_param[1] < 3) {
			ts->grip_edgehandler_direction = sec->cmd_param[1];
			ts->grip_edgehandler_start_y = sec->cmd_param[2];
			ts->grip_edgehandler_end_y = sec->cmd_param[3];
		} else {
			input_err(true, &ts->client->dev, "%s: cmd1 is abnormal, %d (%d)\n",
					__func__, sec->cmd_param[1], __LINE__);
			goto err_grip_data;
		}

		mode = mode | G_SET_EDGE_HANDLER;
		set_grip_data_to_ic(ts, mode);
	} else if (sec->cmd_param[0] == 1) {	// normal mode
		if (ts->grip_edge_range != sec->cmd_param[1])
			mode = mode | G_SET_EDGE_ZONE;

		ts->grip_edge_range = sec->cmd_param[1];
		ts->grip_deadzone_up_x = sec->cmd_param[2];
		ts->grip_deadzone_dn_x = sec->cmd_param[3];
		ts->grip_deadzone_y = sec->cmd_param[4];
		mode = mode | G_SET_NORMAL_MODE;

		if (ts->grip_landscape_mode == 1) {
			ts->grip_landscape_mode = 0;
			mode = mode | G_CLR_LANDSCAPE_MODE;
		}
		set_grip_data_to_ic(ts, mode);
	} else if (sec->cmd_param[0] == 2) {	// landscape mode
		if (sec->cmd_param[1] == 0) {	// normal mode
			ts->grip_landscape_mode = 0;
			mode = mode | G_CLR_LANDSCAPE_MODE;
		} else if (sec->cmd_param[1] == 1) {
			ts->grip_landscape_mode = 1;
			ts->grip_landscape_edge = sec->cmd_param[2];
			ts->grip_landscape_deadzone	= sec->cmd_param[3];
			ts->grip_landscape_top_deadzone = sec->cmd_param[4];
			ts->grip_landscape_bottom_deadzone = sec->cmd_param[5];
			ts->grip_landscape_top_gripzone = sec->cmd_param[6];
			ts->grip_landscape_bottom_gripzone = sec->cmd_param[7];
			mode = mode | G_SET_LANDSCAPE_MODE;
		} else {
			input_err(true, &ts->client->dev, "%s: cmd1 is abnormal, %d (%d)\n",
					__func__, sec->cmd_param[1], __LINE__);
			goto err_grip_data;
		}
		set_grip_data_to_ic(ts, mode);
	} else {
		input_err(true, &ts->client->dev, "%s: cmd0 is abnormal, %d", __func__, sec->cmd_param[0]);
		goto err_grip_data;
	}

	snprintf(buff, sizeof(buff), "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);
	return;

err_grip_data:

	snprintf(buff, sizeof(buff), "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);
}

void zinitix_set_grip_type(struct bt532_ts_info *ts, u8 set_type)
{
	u8 mode = G_NONE;

	input_info(true, &ts->client->dev, "%s: re-init grip(%d), edh:%d, edg:%d, lan:%d\n", __func__,
			set_type, ts->grip_edgehandler_direction, ts->grip_edge_range, ts->grip_landscape_mode);

	/* edge handler */
	if (ts->grip_edgehandler_direction != 0)
		mode |= G_SET_EDGE_HANDLER;

	if (set_type == GRIP_ALL_DATA) {
		/* edge */
		if (ts->grip_edge_range != 60)
			mode |= G_SET_EDGE_ZONE;

		/* dead zone */
		if (ts->grip_landscape_mode == 1)	/* default 0 mode, 32 */
			mode |= G_SET_LANDSCAPE_MODE;
		else
			mode |= G_SET_NORMAL_MODE;
	}

	if (mode)
		set_grip_data_to_ic(ts, mode);
}

static void set_touchable_area(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int val = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		goto out;
	}

	input_info(true, &info->client->dev,
			"%s: set 16:9 mode %s\n", __func__, val ? "enable" : "disable");

	if (val)
		zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_TOUCHABLE_AREA);
	else
		zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_TOUCHABLE_AREA);
	
	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
out:
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
}

static void clear_cover_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int arg = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);
	snprintf(buff, sizeof(buff), "%u", (unsigned int) arg);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 3) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		if (sec->cmd_param[0] > 1) {
			info->flip_enable = true;
			info->cover_type = sec->cmd_param[1];
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
			msleep(100);
			if (TRUSTEDUI_MODE_TUI_SESSION & trustedui_get_current_mode()){
				tui_force_close(1);
				msleep(100);
				if (TRUSTEDUI_MODE_TUI_SESSION & trustedui_get_current_mode()){
					trustedui_clear_mask(TRUSTEDUI_MODE_VIDEO_SECURED|TRUSTEDUI_MODE_INPUT_SECURED);
					trustedui_set_mode(TRUSTEDUI_MODE_OFF);
				}
			}
#endif // CONFIG_TRUSTONIC_TRUSTED_UI
#ifdef CONFIG_SAMSUNG_TUI
			stui_cancel_session();
#endif
		} else {
			info->flip_enable = false;
		}

		set_cover_type(info, info->flip_enable);

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	return;
}

static void clear_reference_data(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	write_reg(client, BT532_EEPROM_INFO, 0xffff);

	write_reg(client, 0xc003, 0x0001);
	write_reg(client, 0xc104, 0x0001);
	usleep_range(100, 100);
	if (write_cmd(client, BT532_SAVE_STATUS_CMD) != I2C_SUCCESS)
		return;

	msleep(500);
	write_reg(client, 0xc003, 0x0000);
	write_reg(client, 0xc104, 0x0000);
	usleep_range(100, 100);

#if ESD_TIMER_INTERVAL
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
	esd_timer_start(CHECK_ESD_TIMER, info);
#endif
	input_info(true, &client->dev, "%s: TSP clear calibration bit\n", __func__);

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
	return;
}

int zt_tclm_execute_force_calibration(struct i2c_client *client, int cal_mode)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);

	if (ts_hw_calibration(info) == false)
		return -1;

	mini_init_touch(info);

	return 0;
}

static void ts_enter_strength_mode(struct bt532_ts_info *info, int testnum)
{
	struct i2c_client *client = info->client;
	u8 i;

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		input_info(true, &info->client->dev, "other process occupied.. (%d)\n", info->work_state);
		up(&info->work_lock);
		return;
	}

	info->touch_mode = testnum;

	write_cmd(client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);
	write_cmd(client, 0x0A);
	usleep_range(20 * 1000, 20 * 1000);

	if (testnum == TOUCH_DELTA_MODE) {
		input_info(true, &info->client->dev, "%s: shorten delay raw for host\n", __func__);
		if (write_reg(client, BT532_DELAY_RAW_FOR_HOST, RAWDATA_DELAY_FOR_HOST / 5) != I2C_SUCCESS) {
			input_info(true, &client->dev, "%s: Fail to delay_raw_for_host enter\n", __func__);
			up(&info->work_lock);
			return;
		}
	}

	if (write_reg(client, 0x007E, 1) != I2C_SUCCESS) {
		input_info(true, &client->dev, "%s: Fail to set report_rate 1\n", __func__);
		up(&info->work_lock);
		return;
	}

	if (write_reg(client, BT532_TOUCH_MODE, testnum) != I2C_SUCCESS) {
		input_info(true, &client->dev, "%s: Fail to set ZINITX_TOUCH_MODE %d\n", __func__, testnum);
		up(&info->work_lock);
		return;
	}

	/* clear garbage data */
	for (i = 0; i < 10; i++) {
		usleep_range(20 * 1000, 20 * 1000);
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	}

	clear_report_data(info);

	input_info(true, &client->dev, "%s: Enter_strength_mode\n", __func__);

	up(&info->work_lock);
}

static void ts_exit_strength_mode(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	u8 i;

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		input_info(true, &info->client->dev, "other process occupied.. (%d)\n", info->work_state);
		up(&info->work_lock);
		return;
	}

	if (write_reg(client, BT532_TOUCH_MODE, TOUCH_POINT_MODE) != I2C_SUCCESS) {
		input_info(true, &client->dev, "[zinitix_touch] TEST Mode : "
				"Fail to set ZINITX_TOUCH_MODE %d.\r\n", TOUCH_POINT_MODE);
		up(&info->work_lock);
		return;
	}
	if (write_reg(client, 0x007E, 0) != I2C_SUCCESS) {
		input_info(true, &client->dev, "[zinitix_touch] report_rate : "
				"Fail to set report_rate %d.\r\n", 0);
		up(&info->work_lock);
		return;
	}

	if (info->touch_mode == TOUCH_DELTA_MODE) {
		input_info(true, &info->client->dev, "%s: restore delay raw for host\n", __func__);
		if (write_reg(client, BT532_DELAY_RAW_FOR_HOST, RAWDATA_DELAY_FOR_HOST) != I2C_SUCCESS) {
			input_info(true, &client->dev, "%s: Fail to delay_raw_for_host exit\n", __func__);
			up(&info->work_lock);
			return;
		}
	}

	/* clear garbage data */
	for (i = 0; i < 10; i++) {
		usleep_range(20 * 1000, 20 * 1000);
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	}

	clear_report_data(info);
	input_info(true, &client->dev, "[zinitix_touch] Exit_strength_mode\r\n");

	info->touch_mode = TOUCH_POINT_MODE;
	up(&info->work_lock);
}

static void ts_get_strength_data(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	int i, j, n;
	u8 ref_max[2] = {0, 0};

	down(&info->raw_data_lock);
	read_data(info->client, 0x0308, ref_max, 2);

	input_info(true, &client->dev, "reference max: %X %X\n", ref_max[0], ref_max[1]);

	n = 0;
	for (i = 0 ; i < info->cap_info.y_node_num; i++) {
		pr_info("[sec_input] %d |", i);
		for (j = 0 ; j < info->cap_info.x_node_num; j++, n++) {
			pr_cont(" %d", info->cur_data[n]);
		}
		pr_cont("\n");
	}
	up(&info->raw_data_lock);
}

static void run_cs_raw_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int retry = 0;
	char all_cmdbuff[info->cap_info.x_node_num * info->cap_info.y_node_num * 6];
	s32 i, j;

	sec_cmd_set_default_result(sec);

	disable_irq(info->irq);

	ts_enter_strength_mode(info, TOUCH_RAW_MODE);

	while (gpio_get_value(info->pdata->gpio_int)) {
		msleep(30);

		retry++;

		input_info(true, &client->dev, "%s: retry:%d\n", __func__, retry);

		if (retry > 100)
			goto out;
	}
	ts_get_raw_data(info);

	ts_exit_strength_mode(info);
	
	enable_irq(info->irq);

	ts_get_strength_data(info);
	memset(all_cmdbuff, 0, sizeof(all_cmdbuff));	//size 6  ex(12000,)

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			sprintf(buff, "%d,", info->cur_data[i * info->cap_info.x_node_num + j]);
			strcat(all_cmdbuff, buff);
		}
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, all_cmdbuff,
			strnlen(all_cmdbuff, sizeof(all_cmdbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
	return;

out:
	enable_irq(info->irq);
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

}

static void run_cs_delta_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int retry = 0;
	char all_cmdbuff[info->cap_info.x_node_num * info->cap_info.y_node_num * 6];
	s32 i, j;

	sec_cmd_set_default_result(sec);

#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	disable_irq(info->irq);

	ts_enter_strength_mode(info, TOUCH_DELTA_MODE);

	while (gpio_get_value(info->pdata->gpio_int)) {
		msleep(30);

		retry++;

		input_info(true, &client->dev, "%s: retry:%d\n", __func__, retry);

		if (retry > 100)
			goto out;
	}
	ts_get_raw_data(info);

	ts_exit_strength_mode(info);
	
	enable_irq(info->irq);

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif

	ts_get_strength_data(info);
	memset(all_cmdbuff, 0, sizeof(all_cmdbuff));	//size 6  ex(12000,)

	for (i = 0; i < info->cap_info.y_node_num; i++) {
		for (j = 0; j < info->cap_info.x_node_num; j++) {
			sprintf(buff, "%d,", info->cur_data[i * info->cap_info.x_node_num + j]);
			strcat(all_cmdbuff, buff);
		}
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, all_cmdbuff,
			strnlen(all_cmdbuff, sizeof(all_cmdbuff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
	return;

out:
	enable_irq(info->irq);
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
}

static void run_ref_calibration(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int i;
#ifdef TCLM_CONCEPT
	int ret;
#endif
	sec_cmd_set_default_result(sec);

	if (info->finger_cnt1 != 0){
		input_info(true, &client->dev, "%s: return (finger cnt %d)\n", __func__, info->finger_cnt1);
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	disable_irq(info->irq);

#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	if (ts_hw_calibration(info) == true) {
#ifdef TCLM_CONCEPT
		/* devide tclm case */
		sec_tclm_case(info->tdata, sec->cmd_param[0]);

		input_info(true, &info->client->dev, "%s: param, %d, %c, %d\n", __func__,
				sec->cmd_param[0], sec->cmd_param[0], info->tdata->root_of_calibration);

		ret = sec_execute_tclm_package(info->tdata, 1);
		if (ret < 0) {
			input_err(true, &info->client->dev,
					"%s: sec_execute_tclm_package\n", __func__);
		}
		sec_tclm_root_of_cal(info->tdata, CALPOSITION_NONE);
#endif
		input_info(true, &client->dev, "%s: TSP calibration Pass\n", __func__);
		snprintf(buff, sizeof(buff), "%s", "OK");
		sec_cmd_set_cmd_result(sec, buff, (int)strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_OK;
	} else {
		input_info(true, &client->dev, "%s: TSP calibration Fail\n", __func__);
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, (int)strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);
	mini_init_touch(info);

	for (i = 0; i < 5; i++) {
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		usleep_range(10, 10);
	}

#if ESD_TIMER_INTERVAL
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
	esd_timer_start(CHECK_ESD_TIMER, info);
#endif

	enable_irq(info->irq);
	input_info(true, &client->dev, "%s: %s(%d)\n", __func__,
			sec->cmd_result, (int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));
	return;
}

static void dead_zone_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int val = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (val) //normal
		zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EDGE_SELECT);
	else //factory
		zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EDGE_SELECT);

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void spay_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int val = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (val) {
		info->spay_enable = 1;
		zinitix_bit_set(info->lpm_mode, BIT_EVENT_SPAY);
	}else {
		info->spay_enable = 0;
		zinitix_bit_clr(info->lpm_mode, BIT_EVENT_SPAY);
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void fod_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 val = (u16)sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	input_info(true, &info->client->dev, "%s: fod_enable:%d, short_mode:%d, strict mode:%d\n",
			__func__, sec->cmd_param[0], sec->cmd_param[1],	sec->cmd_param[2]);

	info->fod_mode_set = (u16)(((sec->cmd_param[2] << 8) & 0xFF00) |
		((sec->cmd_param[1] << 4) & 0x00F0) | (sec->cmd_param[0] & 0x000F));

	if (val) {
		info->fod_enable = 1;
	} else {
		info->fod_enable = 0;
	}

	write_cmd(info->client, 0x0A);
	if (write_reg(client, REG_FOD_MODE_SET, info->fod_mode_set) != I2C_SUCCESS)
		input_info(true, &client->dev, "%s, fail fod mode set\n", __func__);
	write_cmd(info->client, 0x0B);

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void fod_lp_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int val = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (val) {
		info->fod_lp_mode = 1;
	} else {
		info->fod_lp_mode = 0;
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void singletap_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int val = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (val) {
		info->singletap_enable = 1;
		zinitix_bit_set(info->lpm_mode, BIT_EVENT_SINGLE_TAP);
	} else {
		info->singletap_enable = 0;
		zinitix_bit_clr(info->lpm_mode, BIT_EVENT_SINGLE_TAP);
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void aot_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int val = sec->cmd_param[0];
	
	sec_cmd_set_default_result(sec);

	if (val) {
		info->aot_enable = 1;
		zinitix_bit_set(info->lpm_mode, BIT_EVENT_AOT);
	} else {
		info->aot_enable = 0;
		zinitix_bit_clr(info->lpm_mode, BIT_EVENT_AOT);
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void aod_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int val = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (val) {
		info->aod_enable = 1;
		zinitix_bit_set(info->lpm_mode, BIT_EVENT_AOD);
	}else {
		info->aod_enable = 0;
		zinitix_bit_clr(info->lpm_mode, BIT_EVENT_AOD);
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}
static void set_fod_rect(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

	input_info(true, &info->client->dev, "%s: start x:%d, start y:%d, end x:%d, end y:%d\n",
			__func__, sec->cmd_param[0], sec->cmd_param[1],
			sec->cmd_param[2], sec->cmd_param[3]);

	write_cmd(info->client, 0x0A);
	write_reg(info->client, REG_FOD_AREA_STR_X, (u16)sec->cmd_param[0]);
	write_reg(info->client, REG_FOD_AREA_STR_Y, (u16)sec->cmd_param[1]);
	write_reg(info->client, REG_FOD_AREA_END_X, (u16)sec->cmd_param[2]);
	write_reg(info->client, REG_FOD_AREA_END_Y, (u16)sec->cmd_param[3]);
	write_cmd(info->client, 0x0B);

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}


static void set_aod_rect(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

	input_info(true, &info->client->dev, "%s: w:%d, h:%d, x:%d, y:%d\n",
			__func__, sec->cmd_param[0], sec->cmd_param[1],
			sec->cmd_param[2], sec->cmd_param[3]);

	write_cmd(info->client, 0x0A);
	write_reg(info->client, ZT75XX_SET_AOD_W_REG, (u16)sec->cmd_param[0]);
	write_reg(info->client, ZT75XX_SET_AOD_H_REG, (u16)sec->cmd_param[1]);
	write_reg(info->client, ZT75XX_SET_AOD_X_REG, (u16)sec->cmd_param[2]);
	write_reg(info->client, ZT75XX_SET_AOD_Y_REG, (u16)sec->cmd_param[3]);
	write_cmd(info->client, 0x0B);

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void get_wet_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 temp;

	sec_cmd_set_default_result(sec);

	down(&info->work_lock);
	read_data(client, BT532_DEBUG_REG, (u8 *)&temp, 2);
	up(&info->work_lock);

	input_info(true, &client->dev, "%s, %x\n", __func__, temp);

	if (zinitix_bit_test(temp, DEF_DEVICE_STATUS_WATER_MODE))
		temp = true;
	else
		temp = false;

	snprintf(buff, sizeof(buff), "%u", temp);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (sec->cmd_all_factory_state == SEC_CMD_STATUS_RUNNING)
		sec_cmd_set_cmd_result_all(sec, buff, strnlen(buff, sizeof(buff)), "WET_MODE");
	sec->cmd_state = SEC_CMD_STATUS_OK;

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}

#ifdef GLOVE_MODE
static void glove_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		if (sec->cmd_param[0])
			zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_SENSITIVE_BIT);
		else
			zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_SENSITIVE_BIT);

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	input_info(true, &client->dev, "%s: %s(%d)\n", __func__, sec->cmd_result,
				(int)strnlen(sec->cmd_result, sizeof(sec->cmd_result)));

	return;
}
#endif

static void pocket_mode_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int value = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		info->pocket_enable = value;

		if (value)
			zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_POCKET_MODE);
		else
			zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_POCKET_MODE);

		bt532_set_optional_mode(info, false);

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static void get_crc_check(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 chip_check_sum = 0;

	sec_cmd_set_default_result(sec);

	if (read_data(info->client, BT532_CHECKSUM_RESULT,
					(u8 *)&chip_check_sum, 2) < 0) {
		input_err(true, &info->client->dev, "%s: read crc fail", __func__);
		goto err_get_crc_check;
	}

	input_info(true, &info->client->dev, "%s: 0x%04X\n", __func__, chip_check_sum);

	if (chip_check_sum != 0x55aa)
		goto err_get_crc_check;

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
	return;

err_get_crc_check:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
	return;
}

static void factory_cmd_result_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;

	sec->item_count = 0;
	memset(sec->cmd_result_all, 0x00, SEC_CMD_RESULT_STR_LEN);

	if (info->tsp_pwr_enabled == POWER_OFF) {
		input_err(true, &info->client->dev, "%s: IC is power off\n", __func__);
		sec->cmd_all_factory_state = SEC_CMD_STATUS_FAIL;
		goto out;
	}

	sec->cmd_all_factory_state = SEC_CMD_STATUS_RUNNING;

	get_chip_vendor(sec);
	get_chip_name(sec);
	get_fw_ver_bin(sec);
	get_fw_ver_ic(sec);

	run_dnd_read(sec);
	run_dnd_v_gap_read(sec);
	run_dnd_h_gap_read(sec);
	run_cnd_read(sec);
	run_selfdnd_read(sec);
	run_selfdnd_h_gap_read(sec);
	run_self_saturation_read(sec);
	run_charge_pump_read(sec);

#ifdef TCLM_CONCEPT
	run_mis_cal_read(sec);
#endif

	sec->cmd_all_factory_state = SEC_CMD_STATUS_OK;

out:
	input_info(true, &client->dev, "%s: %d%s\n", __func__, sec->item_count,
				sec->cmd_result_all);
}

static void check_connection(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[3] = { 0 };
	u8 conn_check_val;
	int ret;

	sec_cmd_set_default_result(sec);

	ret = read_data(client, BT532_CONNECTION_CHECK_REG, (u8 *)&conn_check_val, 1);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail read TSP connection value\n", __func__);
		goto err_conn_check;
	}

	if (conn_check_val != 1)
		goto err_conn_check;

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
	return;

err_conn_check:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
	return;
}

static void run_prox_intensity_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u16 prox_data;
	u16 threshold_x, threshold_y;
	int ret;

	sec_cmd_set_default_result(sec);

#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	disable_irq(info->irq);
	down(&info->work_lock);

	ret = read_data(client, ZT75XX_PROXIMITY_DATA, (u8 *)&prox_data, 2);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail read proximity data\n", __func__);
		goto READ_FAIL;
	}

	ret = read_data(client, ZT75XX_PROXIMITY_THRESHOLD_X, (u8 *)&threshold_x, 2);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail read proximity threshold x\n", __func__);
		goto READ_FAIL;
	}

	ret = read_data(client, ZT75XX_PROXIMITY_THRESHOLD_Y, (u8 *)&threshold_y, 2);
	if (ret < 0) {
		input_err(true, &info->client->dev,"%s: fail read proximity threshold y\n", __func__);
		goto READ_FAIL;
	}

	up(&info->work_lock);
	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif

	snprintf(buff, sizeof(buff), "SUM_X:%d SUM_Y:%d THD_X:%d THD_Y:%d", prox_data >> 8, prox_data & 0xFF, threshold_x, threshold_y);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
	return;

READ_FAIL:
	up(&info->work_lock);
	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif

	snprintf(buff, sizeof(buff), "%s", "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buff);
	return;
}

static void ear_detect_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int value = sec->cmd_param[0];

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 3) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		info->ed_enable = value;

		if (value == 3) {
			/* Self_Proximity */
			zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT);
			zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT_MUTUAL);
		} else if (value == 1) {
			/* Mutual_Proximity */
			zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT);
			zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT_MUTUAL);
		} else {
			zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT);
			zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT_MUTUAL);
		}

		bt532_set_optional_mode(info, false);

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	input_info(true, &client->dev, "%s, %s\n", __func__, sec->cmd_result);

	return;
}

static ssize_t scrub_position_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	input_info(true, &client->dev, "%s: scrub_id: %d, X:%d, Y:%d \n", __func__,
				info->scrub_id, info->scrub_x, info->scrub_y);

	snprintf(buff, sizeof(buff), "%d %d %d", info->scrub_id, info->scrub_x, info->scrub_y);

	info->scrub_id = 0;
	return snprintf(buf, SEC_CMD_BUF_SIZE, "%s", buff);
}

static ssize_t sensitivity_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	s16 i, value[TOUCH_SENTIVITY_MEASUREMENT_COUNT];
	char buff[SEC_CMD_STR_LEN] = { 0 };

	for (i = 0; i < TOUCH_SENTIVITY_MEASUREMENT_COUNT; i++) {
		value[i] = info->sensitivity_data[i];
	}

	input_info(true, &info->client->dev, "%s: sensitivity mode,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", __func__,
		value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8]);

	snprintf(buff, sizeof(buff),"%d,%d,%d,%d,%d,%d,%d,%d,%d",
				value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8]);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%s", buff);
}

static ssize_t sensitivity_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;

	int ret;
	unsigned long value = 0;

	if (count > 2)
		return -EINVAL;

	ret = kstrtoul(buf, 10, &value);
	if (ret != 0)
		return ret;

	if (info->tsp_pwr_enabled == POWER_OFF) {
		input_err(true, &info->client->dev, "%s: power off in IC\n", __func__);
		return 0;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	input_err(true, &info->client->dev, "%s: enable:%d\n", __func__, value);

	if (value == 1) {
		ts_set_touchmode(TOUCH_SENTIVITY_MEASUREMENT_MODE);
		input_info(true, &info->client->dev, "%s: enable end\n", __func__);
	} else {
		ts_set_touchmode(TOUCH_POINT_MODE);
		input_info(true, &info->client->dev, "%s: disable end\n", __func__);
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif

	input_info(true, &info->client->dev, "%s: done\n", __func__);
	return count;
}

static ssize_t fod_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	int ret;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	//info->fod_info_vi_trx[0] : TX CH width. ( = 10)
	//info->fod_info_vi_trx[1] : RX CH width. ( = 13)

	/* get fod information */
	if (!info->fod_info_vi_trx[0] || !info->fod_info_vi_trx[1] || !info->fod_info_vi_data_len) {
		write_cmd(info->client, 0x0A);
		ret = read_data(info->client, REG_FOD_MODE_VI_DATA_CH, info->fod_info_vi_trx, 2);
		if (ret < 0) {
			input_err(true, &info->client->dev,"%s: fail fod channel info.\n", __func__);
			write_cmd(info->client, 0x0B);
			return ret;
		}

		ret = read_data(info->client, REG_FOD_MODE_VI_DATA_LEN, (u8 *)&info->fod_info_vi_data_len, 2);
		if (ret < 0) {
			input_err(true, &info->client->dev, "%s: fail fod data len.\n", __func__);
			write_cmd(info->client, 0x0B);
			return ret;
		}
		write_cmd(info->client, 0x0B);
	}

	input_info(true, &info->client->dev, "%s: %d,%d,%d\n", __func__,
		info->fod_info_vi_trx[1], info->fod_info_vi_trx[0], info->fod_info_vi_data_len);

	snprintf(buff, sizeof(buff), "%d,%d,%d,%d,%d",
				info->fod_info_vi_trx[1], info->fod_info_vi_trx[0], info->fod_info_vi_data_len,
				info->cap_info.x_node_num, info->cap_info.y_node_num);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%s", buff);
}

static ssize_t fod_pos_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	int i, ret = -1;
	u8 fod_info_vi_data[FOD_VI_DATA_LENGTH];
	char buff[SEC_CMD_STR_LEN] = { 0 };

	write_cmd(info->client, 0x0A);

	if (FOD_VI_DATA_LENGTH != info->fod_info_vi_data_len) {
		input_err(true, &info->client->dev,
			"%s: fod vi data length mismatch. fod vi len (%d)\n",
			__func__, info->fod_info_vi_data_len);
		write_cmd(info->client, 0x0B);
		return ret;
	}	
	
	ret = read_data(info->client, REG_FOD_MODE_VI_DATA, fod_info_vi_data, info->fod_info_vi_data_len);
	if (ret < 0) {
		input_err(true, &info->client->dev, "%s: fail fod data read error.\n", __func__);
		write_cmd(info->client, 0x0B);
		return ret;
	}
	write_cmd(info->client, 0x0B);

	for (i = 0; i < info->fod_info_vi_data_len; i++) {
		input_dbg(true, &info->client->dev, "%s: %02X", __func__, fod_info_vi_data[i]);
		snprintf(buff, 3, "%02X", fod_info_vi_data[i]);
		strlcat(buf, buff, SEC_CMD_BUF_SIZE);
	}

	return strlen(buf);
}

static ssize_t read_ito_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s: %02X%02X%02X%02X\n",
			__func__, info->ito_test[0], info->ito_test[1],
			info->ito_test[2], info->ito_test[3]);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%02X%02X%02X%02X",
			info->ito_test[0], info->ito_test[1],
			info->ito_test[2], info->ito_test[3]);
}

static ssize_t read_wet_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s: %d\n", __func__,info->wet_count);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", info->wet_count);
}

static ssize_t clear_wet_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	info->wet_count = 0;

	input_info(true, &info->client->dev, "%s: clear\n", __func__);

	return count;
}

static ssize_t read_multi_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s: %d\n", __func__,
			info->multi_count);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", info->multi_count);
}

static ssize_t clear_multi_count_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	info->multi_count = 0;

	input_info(true, &info->client->dev, "%s: clear\n", __func__);

	return count;
}

static ssize_t read_comm_err_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s: %d\n", __func__,
			info->comm_err_count);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", info->comm_err_count);
}

static ssize_t clear_comm_err_count_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	info->comm_err_count = 0;

	input_info(true, &info->client->dev, "%s: clear\n", __func__);

	return count;
}

static ssize_t read_module_id_show(struct device *dev,
					struct device_attribute *devattr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s\n", __func__);
#ifdef TCLM_CONCEPT
	return snprintf(buf, SEC_CMD_BUF_SIZE, "ZI%02X%02x%c%01X%04X\n",
			info->cap_info.reg_data_version, info->test_result.data[0],
			info->tdata->tclm_string[info->tdata->nvdata.cal_position].s_name,
			info->tdata->nvdata.cal_count & 0xF, info->tdata->nvdata.tune_fix_ver);
#else
	return snprintf(buf, SEC_CMD_BUF_SIZE, "ZI%02X\n",
			info->cap_info.reg_data_version);
#endif
}

static ssize_t set_ta_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	int ret;
	unsigned long value = 0;

	if (count > 2)
		return -EINVAL;

	ret = kstrtoul(buf, 10, &value);
	if (ret != 0)
		return ret;

	if (info->tsp_pwr_enabled == POWER_OFF) {
		input_err(true, &info->client->dev, "%s: power off in IC\n", __func__);
		return 0;
	}

	input_info(true, &info->client->dev, "%s: enable:%d\n", __func__, value);

	if (value == 1) {
		bypass_mode = false;
		g_ta_connected = true;
	} else {
		bypass_mode = true;
		g_ta_connected = false;
	}

	bt532_set_ta_status(info);
	return count;
}

static ssize_t prox_power_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s: %d\n", __func__,
			info->prox_power_off);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%ld", info->prox_power_off);
}

static ssize_t prox_power_off_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	int ret;
	unsigned long value = 0;

	ret = kstrtoul(buf, 10, &value);
	if (ret != 0)
		return ret;

	input_info(true, &info->client->dev, "%s: enable:%d\n", __func__, value);
	info->prox_power_off = value;

	return count;
}

/*
 * read_support_feature function
 * returns the bit combination of specific feature that is supported.
 */
static ssize_t read_support_feature(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	struct i2c_client *client = info->client;
	
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u32 feature = 0;

	if (info->pdata->support_aot)
		feature |= INPUT_FEATURE_ENABLE_SETTINGS_AOT;
	if (info->pdata->support_open_short_test)
		feature |= INPUT_FEATURE_SUPPORT_OPEN_SHORT_TEST;

	snprintf(buff, sizeof(buff), "%d", feature);
	input_info(true, &client->dev, "%s: %s\n", __func__, buff);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%s\n", buff);
}

/** for protos **/
static ssize_t protos_event_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s: %d\n", __func__,
			info->hover_event);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", info->hover_event != 3 ? 0 : 3);
}

static ssize_t protos_event_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	u8 data;
	int ret;

	ret = kstrtou8(buf, 10, &data);
	if (ret < 0)
		return ret;

	input_info(true, &info->client->dev, "%s: %d\n", __func__, data);

	if (data < 0 || data > 3) {
		input_err(true, &info->client->dev, "%s: incorrect data\n", __func__);
		return -EINVAL;
	}

	info->ed_enable = data;
	
	if (data == 3) {
		/* Self_Proximity */
		zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT);
		zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT_MUTUAL);
	} else if (data == 1) {
		/* Mutual_Proximity */
		zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT);
		zinitix_bit_set(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT_MUTUAL);
	} else {
		zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT);
		zinitix_bit_clr(m_optional_mode.select_mode.flag, DEF_OPTIONAL_MODE_EAR_DETECT_MUTUAL);
	}

	bt532_set_optional_mode(info, false);

	return count;
}

static ssize_t read_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);

	input_info(true, &info->client->dev, "%s: 0x%x\n", __func__,
			info->store_reg_data);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "0x%x", info->store_reg_data);
}

static ssize_t store_read_reg(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	u32 buff[2] = {0, }; //addr, size
	int ret;

	ret = sscanf(buf, "0x%x,0x%x", &buff[0], &buff[1]);
	if (ret != 2) {
		input_err(true, &info->client->dev,
				"%s: failed read params[0x%x]\n", __func__, ret);
		return -EINVAL;
	}

	if (buff[1] != 1 && buff[1] != 2) {
		input_err(true, &info->client->dev,
				"%s: incorrect byte length [0x%x]\n", __func__, buff[1]);
		return -EINVAL;
	} 

	info->store_reg_data = ts_get_touch_reg((u16)buff[0]);

	if (buff[1] == 1)
		info->store_reg_data = info->store_reg_data & 0x00FF;

	input_info(true, &info->client->dev,
			"%s: read touch reg [addr:0x%x][data:0x%x]\n",
			__func__, buff[0], info->store_reg_data);

	return size;
}

static ssize_t store_write_reg(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct bt532_ts_info *info = container_of(sec, struct bt532_ts_info, sec);
	int buff[3];
	int ret;

	ret = sscanf(buf, "0x%x,0x%x,0x%x", &buff[0], &buff[1], &buff[2]);
	if (ret != 3) {
		input_err(true, &info->client->dev,
				"%s: failed read params[0x%x]\n", __func__, ret);
		return -EINVAL;
	}

	if (buff[1] != 1 && buff[1] != 2) {
		input_err(true, &info->client->dev,
				"%s: incorrect byte length [0x%x]\n", __func__, buff[1]);
		return -EINVAL;
	} 

	if (buff[1] == 1)
		buff[2] = buff[2] & 0x00FF;

	ts_set_touch_reg((u16)buff[0], (u16)buff[2]);
	input_info(true, &info->client->dev,
			"%s: write touch reg [addr:0x%x][byte:0x%x][data:0x%x]\n",
			__func__, buff[0], buff[1], buff[2]);

	return size;
}

static ssize_t enabled_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);

	input_info(true, &info->client->dev, "%s: %d\n", __func__, info->enabled);
	return scnprintf(buf, PAGE_SIZE, "%d", info->enabled);
}

static ssize_t enabled_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	int value;
	int err;

	err = kstrtoint(buf, 10, &value);

	if (info->enabled == value) {
		input_info(true, &info->client->dev, "%s: already %s\n",
				__func__, value ? "enabled" : "disabled");
		return count;
	}

	if (value > 0)
		bt532_ts_open(info->input_dev);
	else
		bt532_ts_close(info->input_dev);

	info->enabled = value;
	return count;
}

static DEVICE_ATTR(scrub_pos, S_IRUGO, scrub_position_show, NULL);
static DEVICE_ATTR(sensitivity_mode, S_IRUGO | S_IWUSR | S_IWGRP, sensitivity_mode_show, sensitivity_mode_store);
static DEVICE_ATTR(ito_check, S_IRUGO | S_IWUSR | S_IWGRP, read_ito_check_show, clear_wet_mode_store);
static DEVICE_ATTR(wet_mode, S_IRUGO | S_IWUSR | S_IWGRP, read_wet_mode_show, clear_wet_mode_store);
static DEVICE_ATTR(comm_err_count, S_IRUGO | S_IWUSR | S_IWGRP, read_comm_err_count_show, clear_comm_err_count_store);
static DEVICE_ATTR(multi_count, S_IRUGO | S_IWUSR | S_IWGRP, read_multi_count_show, clear_multi_count_store);
static DEVICE_ATTR(module_id, S_IRUGO, read_module_id_show, NULL);
static DEVICE_ATTR(ta_mode, S_IWUSR | S_IWGRP, NULL, set_ta_mode_store);
static DEVICE_ATTR(prox_power_off, S_IRUGO | S_IWUSR | S_IWGRP, prox_power_off_show, prox_power_off_store);
static DEVICE_ATTR(support_feature, S_IRUGO, read_support_feature, NULL);
static DEVICE_ATTR(virtual_prox, S_IRUGO | S_IWUSR | S_IWGRP, protos_event_show, protos_event_store);
static DEVICE_ATTR(fod_info, S_IRUGO, fod_info_show, NULL);
static DEVICE_ATTR(fod_pos, S_IRUGO, fod_pos_show, NULL);
static DEVICE_ATTR(read_reg_data, S_IRUGO | S_IWUSR | S_IWGRP, read_reg_show, store_read_reg);
static DEVICE_ATTR(write_reg_data, S_IWUSR | S_IWGRP, NULL, store_write_reg);
static DEVICE_ATTR(enabled, 0664, enabled_show, enabled_store);

static struct attribute *input_attributes[] = {
	&dev_attr_enabled.attr,
	NULL,
};
static struct attribute *touchscreen_attributes[] = {
	&dev_attr_scrub_pos.attr,
	&dev_attr_sensitivity_mode.attr,
	&dev_attr_ito_check.attr,
	&dev_attr_wet_mode.attr,
	&dev_attr_multi_count.attr,
	&dev_attr_comm_err_count.attr,
	&dev_attr_module_id.attr,
	&dev_attr_ta_mode.attr,
	&dev_attr_prox_power_off.attr,
	&dev_attr_support_feature.attr,
	&dev_attr_virtual_prox.attr,
	&dev_attr_fod_info.attr,
	&dev_attr_fod_pos.attr,
	&dev_attr_read_reg_data.attr,
	&dev_attr_write_reg_data.attr,
	NULL,
};

static struct attribute_group input_attr_group = {
	.attrs = input_attributes,
};
static struct attribute_group touchscreen_attr_group = {
	.attrs = touchscreen_attributes,
};

static ssize_t show_touchkey_threshold(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	struct capa_info *cap = &(info->cap_info);

	read_data(client, BT532_BUTTON_SENSITIVITY, (u8 *)&cap->key_threshold, 2);

#ifdef NOT_SUPPORTED_TOUCH_DUMMY_KEY
	input_info(true, &client->dev, "%s: key threshold = %d\n", __func__, cap->key_threshold);

	return snprintf(buf, 41, "%d", cap->key_threshold);
#else
	read_data(client, BT532_DUMMY_BUTTON_SENSITIVITY, (u8 *)&cap->dummy_threshold, 2);
	input_info(true, &client->dev, "%s: key threshold = %d %d %d %d\n", __func__,
			cap->dummy_threshold, cap->key_threshold, cap->key_threshold, cap->dummy_threshold);

	return snprintf(buf, 41, "%d %d %d %d", cap->dummy_threshold,
					cap->key_threshold,  cap->key_threshold,
					cap->dummy_threshold);
#endif
}

static ssize_t show_touchkey_sensitivity(struct device *dev,
										 struct device_attribute *attr,
										 char *buf)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u16 val = 0;
	int ret;
	int i;

#ifdef NOT_SUPPORTED_TOUCH_DUMMY_KEY
	if (!strcmp(attr->attr.name, "touchkey_recent"))
		i = 0;
	else if (!strcmp(attr->attr.name, "touchkey_back"))
		i = 1;
	else {
		input_err(true, &client->dev, "%s: Invalid attribute\n",__func__);

		goto err_out;
	}

#else
	if (!strcmp(attr->attr.name, "touchkey_dummy_btn1"))
		i = 0;
	else if (!strcmp(attr->attr.name, "touchkey_recent"))
		i = 1;
	else if (!strcmp(attr->attr.name, "touchkey_back"))
		i = 2;
	else if (!strcmp(attr->attr.name, "touchkey_dummy_btn4"))
		i = 3;
	else if (!strcmp(attr->attr.name, "touchkey_dummy_btn5"))
		i = 4;
	else if (!strcmp(attr->attr.name, "touchkey_dummy_btn6"))
		i = 5;
	else {
		input_err(true, &client->dev, "%s: Invalid attribute\n",__func__);

		goto err_out;
	}
#endif
	down(&info->work_lock);
	ret = read_data(client, BT532_BTN_WIDTH + i, (u8*)&val, 2);
	up(&info->work_lock);
	if (ret < 0) {
		input_err(true, &client->dev, "%s: Failed to read %d's key sensitivity\n",
					 __func__,i);

		goto err_out;
	}

	input_info(true, &client->dev, "%s: %d's key sensitivity = %d\n",
				__func__, i, val);

	return snprintf(buf, 6, "%d", val);

err_out:
	return sprintf(buf, "NG");
}

static ssize_t show_back_key_raw_data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t show_menu_key_raw_data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t touch_led_control(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct regulator *regulator_led = NULL;
	int retval = 0;
	u8 data;

	sscanf(buf, "%hhu", &data);

	if (pdata->regulator_tkled){
		regulator_led = regulator_get(NULL, pdata->regulator_tkled);
		if (IS_ERR(regulator_led)) {
			input_err(true, dev, "%s: Failed to get regulator_led.\n", __func__);
			goto out_led_control;
		}

		input_info(true, &info->client->dev, "[TKEY] %s : %d _ %d\n",__func__,data,__LINE__);

		if (data) {
			retval = regulator_enable(regulator_led);
			if (retval)
				input_err(true, dev, "%s: Failed to enable regulator_led: %d\n", __func__, retval);
		} else {
			if (regulator_is_enabled(regulator_led)){
				retval = regulator_disable(regulator_led);
				if (retval)
					input_err(true, dev, "%s: Failed to disable regulator_led: %d\n", __func__, retval);
			}
		}

	out_led_control:
		regulator_put(regulator_led);
	}

	return size;
}

static DEVICE_ATTR(touchkey_threshold, S_IRUGO, show_touchkey_threshold, NULL);
static DEVICE_ATTR(touchkey_recent, S_IRUGO, show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO, show_touchkey_sensitivity, NULL);
#ifndef NOT_SUPPORTED_TOUCH_DUMMY_KEY
static DEVICE_ATTR(touchkey_dummy_btn1, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_dummy_btn3, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_dummy_btn4, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_dummy_btn6, S_IRUGO,
					show_touchkey_sensitivity, NULL);
#endif
static DEVICE_ATTR(touchkey_raw_back, S_IRUGO, show_back_key_raw_data, NULL);
static DEVICE_ATTR(touchkey_raw_menu, S_IRUGO, show_menu_key_raw_data, NULL);
static DEVICE_ATTR(brightness, 0664, NULL, touch_led_control);

static struct attribute *touchkey_attributes[] = {
	&dev_attr_touchkey_threshold.attr,
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_recent.attr,
	&dev_attr_touchkey_raw_menu.attr,
	&dev_attr_touchkey_raw_back.attr,
#ifndef NOT_SUPPORTED_TOUCH_DUMMY_KEY
	&dev_attr_touchkey_dummy_btn1.attr,
	&dev_attr_touchkey_dummy_btn3.attr,
	&dev_attr_touchkey_dummy_btn4.attr,
	&dev_attr_touchkey_dummy_btn6.attr,
#endif
	&dev_attr_brightness.attr,
	NULL,
};
static struct attribute_group touchkey_attr_group = {
	.attrs = touchkey_attributes,
};

static struct sec_cmd sec_cmds[] = {
	{SEC_CMD("fw_update", fw_update),},
	{SEC_CMD("get_fw_ver_bin", get_fw_ver_bin),},
	{SEC_CMD("get_fw_ver_ic", get_fw_ver_ic),},
	{SEC_CMD("get_checksum_data", get_checksum_data),},
	{SEC_CMD("get_threshold", get_threshold),},
	{SEC_CMD("module_off_master", module_off_master),},
	{SEC_CMD("module_on_master", module_on_master),},
	{SEC_CMD("module_off_slave", module_off_slave),},
	{SEC_CMD("module_on_slave", module_on_slave),},
	{SEC_CMD("get_module_vendor", get_module_vendor),},
	{SEC_CMD("get_chip_vendor", get_chip_vendor),},
	{SEC_CMD("get_chip_name", get_chip_name),},
	{SEC_CMD("get_x_num", get_x_num),},
	{SEC_CMD("get_y_num", get_y_num),},

	/* vendor dependant command */
	{SEC_CMD("run_delta_read", run_delta_read),},
	{SEC_CMD("get_delta_all_data", get_delta),},
	{SEC_CMD("run_cnd_read", run_cnd_read),},
	{SEC_CMD("run_cnd_read_all", run_cnd_read_all),},
	{SEC_CMD("run_dnd_read", run_dnd_read),},
	{SEC_CMD("get_dnd", get_dnd),},
	{SEC_CMD("get_dnd_all", get_dnd_all),},
	{SEC_CMD("run_dnd_read_all", run_dnd_read_all),},
	{SEC_CMD("run_dnd_v_gap_read", run_dnd_v_gap_read),},
	{SEC_CMD("get_dnd_v_gap", get_dnd_v_gap),},
	{SEC_CMD("run_dnd_v_gap_read_all", run_dnd_v_gap_read_all),},
	{SEC_CMD("run_dnd_h_gap_read", run_dnd_h_gap_read),},
	{SEC_CMD("get_dnd_h_gap", get_dnd_h_gap),},
	{SEC_CMD("run_dnd_h_gap_read_all", run_dnd_h_gap_read_all),},
	{SEC_CMD("run_hfdnd_read", run_hfdnd_read),},
	{SEC_CMD("get_hfdnd", get_hfdnd),},
	{SEC_CMD("get_hfdnd_all", get_hfdnd_all),},
	{SEC_CMD("run_hfdnd_v_gap_read", run_hfdnd_v_gap_read),},
	{SEC_CMD("get_hfdnd_v_gap", get_hfdnd_v_gap),},
	{SEC_CMD("get_hfdnd_v_gap_all", get_hfdnd_v_gap_all),},
	{SEC_CMD("run_hfdnd_h_gap_read", run_hfdnd_h_gap_read),},
	{SEC_CMD("get_hfdnd_h_gap", get_hfdnd_h_gap),},
	{SEC_CMD("get_hfdnd_h_gap_all", get_hfdnd_h_gap_all),},
	{SEC_CMD("run_rxshort_read", run_rxshort_read),},
	{SEC_CMD("get_rxshort", get_rxshort),},
	{SEC_CMD("run_txshort_read", run_txshort_read),},
	{SEC_CMD("get_txshort", get_txshort),},
	{SEC_CMD("run_selfdnd_read", run_selfdnd_read),},
	{SEC_CMD("get_selfdnd", get_selfdnd_rx),},
	{SEC_CMD("get_selfdnd_tx", get_selfdnd_tx),},
	{SEC_CMD("run_selfdnd_read_all", run_selfdnd_read_all),},
	{SEC_CMD("run_ssr_read", run_self_saturation_read),},	/* self_saturation_rx */
	{SEC_CMD("get_ssr", get_ssr_rx),},
	{SEC_CMD("get_ssr_tx", get_ssr_tx),},
	{SEC_CMD("run_self_saturation_read_all", run_ssr_read_all),},
	{SEC_CMD("run_selfdnd_h_gap_read", run_selfdnd_h_gap_read),},
	{SEC_CMD("get_selfdnd_h_gap", get_selfdnd_h_gap),},	
	{SEC_CMD("run_selfdnd_h_gap_read_all", run_selfdnd_h_gap_read_all),},
	{SEC_CMD("run_selfdnd_v_gap_read", run_selfdnd_v_gap_read),},
	{SEC_CMD("get_selfdnd_v_gap", get_selfdnd_v_gap),},
	{SEC_CMD("run_self_sat_dnd_read", run_self_sat_dnd_read),},
	{SEC_CMD("get_self_sat_dnd", get_self_sat_dnd),},
	{SEC_CMD("run_jitter_read", run_jitter_read),},
	{SEC_CMD("get_jitter", get_jitter),},
	{SEC_CMD("run_jitter_read_all", run_jitter_read_all),},
	{SEC_CMD("run_reference_read", run_reference_read),},
	{SEC_CMD("get_reference", get_reference),},
	{SEC_CMD("run_trx_short_test", run_trx_short_test),},
#ifdef TCLM_CONCEPT
	{SEC_CMD("run_mis_cal_read", run_mis_cal_read),},
	{SEC_CMD("get_mis_cal", get_mis_cal),},
	{SEC_CMD("run_mis_cal_read_all", run_mis_cal_read_all),},
	{SEC_CMD("get_pat_information", get_pat_information),},
	{SEC_CMD("get_tsp_test_result", get_tsp_test_result),},
	{SEC_CMD("set_tsp_test_result", set_tsp_test_result),},
	{SEC_CMD("increase_disassemble_count", increase_disassemble_count),},
	{SEC_CMD("get_disassemble_count", get_disassemble_count),},
#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
	{SEC_CMD("ium_write", ium_write),},
	{SEC_CMD("ium_read", ium_read),},
	{SEC_CMD("ium_r_write", ium_r_write),},
	{SEC_CMD("ium_r_read", ium_r_read),},
#endif
#endif
	{SEC_CMD("run_cs_raw_read_all", run_cs_raw_read_all),},
	{SEC_CMD("run_cs_delta_read_all", run_cs_delta_read_all),},
	{SEC_CMD("run_force_calibration", run_ref_calibration),},
	{SEC_CMD("clear_reference_data", clear_reference_data),},
	{SEC_CMD("run_ref_calibration", run_ref_calibration),},
	{SEC_CMD("dead_zone_enable", dead_zone_enable),},
	{SEC_CMD("set_grip_data", set_grip_data),},
	{SEC_CMD_H("set_touchable_area", set_touchable_area),},
	{SEC_CMD_H("clear_cover_mode", clear_cover_mode),},
	{SEC_CMD_H("spay_enable", spay_enable),},
	{SEC_CMD("fod_enable", fod_enable),},
	{SEC_CMD_H("fod_lp_mode", fod_lp_mode),},
	{SEC_CMD("set_fod_rect", set_fod_rect),},
	{SEC_CMD_H("singletap_enable", singletap_enable),},
	{SEC_CMD_H("aot_enable", aot_enable),},
	{SEC_CMD_H("aod_enable", aod_enable),},
	{SEC_CMD("set_aod_rect", set_aod_rect),},
	{SEC_CMD("get_wet_mode", get_wet_mode),},
#ifdef GLOVE_MODE
	{SEC_CMD_H("glove_mode", glove_mode),},
#endif
	{SEC_CMD_H("pocket_mode_enable", pocket_mode_enable),},
	{SEC_CMD("get_crc_check", get_crc_check),},
	{SEC_CMD("factory_cmd_result_all", factory_cmd_result_all),},
	{SEC_CMD("check_connection", check_connection),},
	{SEC_CMD("run_prox_intensity_read_all", run_prox_intensity_read_all),},
	{SEC_CMD("run_charge_pump_read", run_charge_pump_read),},
	{SEC_CMD_H("ear_detect_enable", ear_detect_enable),},
	{SEC_CMD("not_support_cmd", not_support_cmd),},
};

static int init_sec_factory(struct bt532_ts_info *info)
{
	struct device *factory_tk_dev = NULL;
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct tsp_raw_data *raw_data;
	int ret;

	raw_data = kzalloc(sizeof(struct tsp_raw_data), GFP_KERNEL);
	if (unlikely(!raw_data)) {
		input_err(true, &info->client->dev, "%s: Failed to allocate memory\n",
				__func__);
		ret = -ENOMEM;

		goto err_alloc;
	}

/*	if (pdata->support_touchkey){
		factory_tk_dev = sec_device_create( info, "sec_touchkey");
		if (IS_ERR(factory_tk_dev)) {
			input_err(true, &info->client->dev, "Failed to create factory dev\n");
			ret = -ENODEV;
			goto err_create_device;
		}
	}
*/
	ret = sec_cmd_init(&info->sec, sec_cmds,
			ARRAY_SIZE(sec_cmds), SEC_CLASS_DEVT_TSP);
	if (ret < 0) {
		input_err(true, &info->client->dev,
				"%s: Failed to sec_cmd_init\n", __func__);
		goto err_init_cmd;
	}

	ret = sysfs_create_group(&info->sec.fac_dev->kobj,
			&touchscreen_attr_group);
	if (ret < 0) {
		input_err(true, &info->client->dev,
				"%s: FTS Failed to create sysfs attributes\n", __func__);
		goto err_create_sysfs;
	}

	ret = sysfs_create_link(&info->sec.fac_dev->kobj,
			&info->input_dev->dev.kobj, "input");
	if (ret < 0) {
		input_err(true, &info->client->dev, "%s: Failed to create link\n", __func__);
		goto err_create_sysfs;
	}

	if (pdata->support_touchkey){
		ret = sysfs_create_group(&factory_tk_dev->kobj, &touchkey_attr_group);
		if (unlikely(ret)) {
			input_err(true, &info->client->dev, "Failed to create touchkey sysfs group\n");
			goto err_create_sysfs;
		}
	}

	info->raw_data = raw_data;

	return ret;

err_create_sysfs:
err_init_cmd:
//err_create_device:
	kfree(raw_data);
err_alloc:

	return ret;
}
#endif

#ifdef USE_MISC_DEVICE
static int ts_misc_fops_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ts_misc_fops_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static long ts_misc_fops_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	struct raw_ioctl raw_ioctl;
	u8 *u8Data;
	int ret = 0;
	size_t sz = 0;
	//u16 version;
	u16 mode;
	struct reg_ioctl reg_ioctl;
	u16 val;
	int nval = 0;
#ifdef CONFIG_COMPAT
	void __user *argp = compat_ptr(arg);
#else
	void __user *argp = (void __user *)arg;
#endif

	if (misc_info == NULL)
	{
		zinitix_debug_msg("misc device NULL?\n");
		return -1;
	}

	switch (cmd) {

	case TOUCH_IOCTL_GET_DEBUGMSG_STATE:
		ret = m_ts_debug_mode;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_SET_DEBUGMSG_STATE:
		if (copy_from_user(&nval, argp, sizeof(nval))) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}
		if (nval)
			input_info(true, &misc_info->client->dev, "[zinitix_touch] on debug mode (%d)\n", nval);
		else
			input_info(true, &misc_info->client->dev, "[zinitix_touch] off debug mode (%d)\n", nval);
		m_ts_debug_mode = nval;
		break;

	case TOUCH_IOCTL_GET_CHIP_REVISION:
		ret = misc_info->cap_info.ic_revision;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_FW_VERSION:
		ret = misc_info->cap_info.fw_version;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_REG_DATA_VERSION:
		ret = misc_info->cap_info.reg_data_version;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_VARIFY_UPGRADE_SIZE:
		if (copy_from_user(&sz, argp, sizeof(size_t)))
			return -1;

		//input_info(true, &misc_info->client->dev, KERN_INFO "[zinitix_touch]: firmware size = %d\r\n", sz);
		if (misc_info->cap_info.ic_fw_size != sz) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch]: firmware size error\r\n");
			return -1;
		}
		break;
/*
	case TOUCH_IOCTL_VARIFY_UPGRADE_DATA:
		if (copy_from_user(m_firmware_data,
			argp, misc_info->cap_info.ic_fw_size))
			return -1;

		version = (u16) (m_firmware_data[52] | (m_firmware_data[53]<<8));

		input_info(true, &misc_info->client->dev, "[zinitix_touch]: firmware version = %x\r\n", version);

		if (copy_to_user(argp, &version, sizeof(version)))
			return -1;
		break;

	case TOUCH_IOCTL_START_UPGRADE:
		return ts_upgrade_sequence((u8*)m_firmware_data);
*/
	case TOUCH_IOCTL_GET_X_RESOLUTION:
		ret = misc_info->pdata->x_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_RESOLUTION:
		ret = misc_info->pdata->y_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_X_NODE_NUM:
		ret = misc_info->cap_info.x_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_NODE_NUM:
		ret = misc_info->cap_info.y_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_TOTAL_NODE_NUM:
		ret = misc_info->cap_info.total_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_HW_CALIBRAION:
		ret = -1;
		disable_irq(misc_info->irq);
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch]: other process occupied.. (%d)\r\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}
		misc_info->work_state = HW_CALIBRAION;
		msleep(100);

		/* h/w calibration */
		if (ts_hw_calibration(misc_info) == true) {
			ret = 0;
#ifdef TCLM_CONCEPT
			sec_tclm_root_of_cal(misc_info->tdata, CALPOSITION_TESTMODE);
			sec_execute_tclm_package(misc_info->tdata, 1);
			sec_tclm_root_of_cal(misc_info->tdata, CALPOSITION_NONE);
#endif
		}

		mode = misc_info->touch_mode;
		if (write_reg(misc_info->client,
			BT532_TOUCH_MODE, mode) != I2C_SUCCESS) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch]: failed to set touch mode %d.\n",
				mode);
			goto fail_hw_cal;
		}

		if (write_cmd(misc_info->client,
			BT532_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;

		enable_irq(misc_info->irq);
		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;
fail_hw_cal:
		enable_irq(misc_info->irq);
		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return -1;

	case TOUCH_IOCTL_SET_RAW_DATA_MODE:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		if (copy_from_user(&nval, argp, sizeof(nval))) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch] error : copy_from_user\r\n");
			misc_info->work_state = NOTHING;
			return -1;
		}
		ts_set_touchmode((u16)nval);

		return 0;

	case TOUCH_IOCTL_GET_REG:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch]:other process occupied.. (%d)\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}

		misc_info->work_state = SET_MODE;

		if (copy_from_user(&reg_ioctl, argp, sizeof(struct reg_ioctl))) {
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			input_info(true, &misc_info->client->dev, "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (read_data(misc_info->client,
			(u16)reg_ioctl.addr, (u8 *)&val, 2) < 0)
			ret = -1;

		nval = (int)val;

#ifdef CONFIG_COMPAT
		if (copy_to_user(compat_ptr(reg_ioctl.val), (u8 *)&nval, 4)) {
#else
		if (copy_to_user((void __user *)(reg_ioctl.val), (u8 *)&nval, 4)) {
#endif
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			input_info(true, &misc_info->client->dev, "[zinitix_touch] error : copy_to_user\n");
			return -1;
		}

		zinitix_debug_msg("read : reg addr = 0x%x, val = 0x%x\n",
			reg_ioctl.addr, nval);

		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_SET_REG:

		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch]: other process occupied.. (%d)\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}

		misc_info->work_state = SET_MODE;
		if (copy_from_user(&reg_ioctl,
				argp, sizeof(struct reg_ioctl))) {
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			input_info(true, &misc_info->client->dev, "[zinitix_touch] error : copy_from_user(1)\n");
			return -1;
		}

#ifdef CONFIG_COMPAT
		if (copy_from_user(&val, compat_ptr(reg_ioctl.val), sizeof(val))) {
#else
		if (copy_from_user(&val,(void __user *)(reg_ioctl.val), sizeof(val))) {
#endif
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			input_info(true, &misc_info->client->dev, "[zinitix_touch] error : copy_from_user(2)\n");
			return -1;
		}

		if (write_reg(misc_info->client,
			(u16)reg_ioctl.addr, val) != I2C_SUCCESS)
			ret = -1;

		zinitix_debug_msg("write : reg addr = 0x%x, val = 0x%x\r\n",
			reg_ioctl.addr, val);
		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_DONOT_TOUCH_EVENT:

		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch]: other process occupied.. (%d)\r\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}

		misc_info->work_state = SET_MODE;
		if (write_reg(misc_info->client,
			BT532_INT_ENABLE_FLAG, 0) != I2C_SUCCESS)
			ret = -1;
		zinitix_debug_msg("write : reg addr = 0x%x, val = 0x0\r\n",
			BT532_INT_ENABLE_FLAG);

		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_SEND_SAVE_STATUS:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			input_info(true, &misc_info->client->dev, "[zinitix_touch]: other process occupied.." \
				"(%d)\r\n", misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}
		misc_info->work_state = SET_MODE;
		ret = 0;
		write_reg(misc_info->client, 0xc003, 0x0001);
		write_reg(misc_info->client, 0xc104, 0x0001);
		if (write_cmd(misc_info->client,
			BT532_SAVE_STATUS_CMD) != I2C_SUCCESS)
			ret =  -1;

		msleep(1000);	/* for fusing eeprom */
		write_reg(misc_info->client, 0xc003, 0x0000);
		write_reg(misc_info->client, 0xc104, 0x0000);

		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_GET_RAW_DATA:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}

		if (misc_info->touch_mode == TOUCH_POINT_MODE)
			return -1;

		down(&misc_info->raw_data_lock);
		if (misc_info->update == 0) {
			up(&misc_info->raw_data_lock);
			return -2;
		}

		if (copy_from_user(&raw_ioctl,
			argp, sizeof(struct raw_ioctl))) {
			up(&misc_info->raw_data_lock);
			input_info(true, &misc_info->client->dev, "[zinitix_touch] error : copy_from_user\r\n");
			return -1;
		}

		misc_info->update = 0;

		u8Data = (u8 *)&misc_info->cur_data[0];
		if (raw_ioctl.sz > MAX_TRAW_DATA_SZ*2)
			raw_ioctl.sz = MAX_TRAW_DATA_SZ*2;
#ifdef CONFIG_COMPAT
		if (copy_to_user(compat_ptr(raw_ioctl.buf), (u8 *)u8Data,
			raw_ioctl.sz)) {
#else
		if (copy_to_user((void __user *)(raw_ioctl.buf), (u8 *)u8Data,
			raw_ioctl.sz)) {
#endif
			up(&misc_info->raw_data_lock);
			return -1;
		}

		up(&misc_info->raw_data_lock);
		return 0;

	default:
		break;
	}
	return 0;
}
#endif

#ifdef CONFIG_OF
static int bt532_pinctrl_configure(struct bt532_ts_info *info, bool active)
{
	struct device *dev = &info->client->dev;
	struct pinctrl_state *pinctrl_state;
	int retval = 0;

	input_dbg(true, dev, "%s: pinctrl %d\n", __func__, active);

	if (active)
		pinctrl_state = pinctrl_lookup_state(info->pinctrl, "on_state");
	else
		pinctrl_state = pinctrl_lookup_state(info->pinctrl, "off_state");

	if (IS_ERR(pinctrl_state)) {
		input_err(true, dev, "%s: Failed to lookup pinctrl.\n", __func__);
	} else {
		retval = pinctrl_select_state(info->pinctrl, pinctrl_state);
		if (retval)
			input_err(true, dev, "%s: Failed to configure pinctrl.\n", __func__);
	}
	return 0;
}

static int bt532_power_ctrl(void *data, bool on)
{
	struct bt532_ts_info* info = (struct bt532_ts_info*)data;
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct device *dev = &info->client->dev;
	struct regulator *regulator_dvdd = NULL;
	struct regulator *regulator_avdd;
	int retval = 0;

	if (info->tsp_pwr_enabled == on)
		return retval;

	if (!pdata->gpio_ldo_en) {
		regulator_dvdd = regulator_get(NULL, pdata->regulator_dvdd);
		if (IS_ERR(regulator_dvdd)) {
			input_err(true, dev, "%s: Failed to get %s regulator.\n",
				 __func__, pdata->regulator_dvdd);
			return PTR_ERR(regulator_dvdd);
		}
	}
	regulator_avdd = regulator_get(NULL, pdata->regulator_avdd);
	if (IS_ERR(regulator_avdd)) {
		input_err(true, dev, "%s: Failed to get %s regulator.\n",
			 __func__, pdata->regulator_avdd);
		return PTR_ERR(regulator_avdd);
	}

	input_info(true, dev, "%s: %s\n", __func__, on ? "on" : "off");

	if (on) {
		retval = regulator_enable(regulator_avdd);
		if (retval) {
			input_err(true, dev, "%s: Failed to enable avdd: %d\n", __func__, retval);
			return retval;
		}
		if (!pdata->gpio_ldo_en) {
			retval = regulator_enable(regulator_dvdd);
			if (retval) {
				input_err(true, dev, "%s: Failed to enable vdd: %d\n", __func__, retval);
				return retval;
			}
		}
	} else {
		if (!pdata->gpio_ldo_en) {
			if (regulator_is_enabled(regulator_dvdd))
				regulator_disable(regulator_dvdd);
		}
		if (regulator_is_enabled(regulator_avdd))
			regulator_disable(regulator_avdd);
	}

	info->tsp_pwr_enabled = on;
	if (!pdata->gpio_ldo_en)
		regulator_put(regulator_dvdd);
	regulator_put(regulator_avdd);

	return retval;
}


static int zinitix_init_gpio(struct bt532_ts_platform_data *pdata)
{
	int ret = 0;

	ret = gpio_request(pdata->gpio_int, "zinitix_tsp_irq");
	if (ret) {
		pr_err("[TSP]%s: unable to request zinitix_tsp_irq [%d]\n",
			__func__, pdata->gpio_int);
		return ret;
	}

	return ret;
}

static int bt532_ts_parse_dt(struct device_node *np,
			 struct device *dev,
			 struct bt532_ts_platform_data *pdata)
{
	int ret = 0;
	u32 temp;
	u32 px_zone[3] = { 0 };

	ret = of_property_read_u32(np, "zinitix,x_resolution", &temp);
	if (ret) {
		input_info(true, dev, "Unable to read controller version\n");
		return ret;
	} else
		pdata->x_resolution = (u16) temp;

	ret = of_property_read_u32(np, "zinitix,y_resolution", &temp);
	if (ret) {
		input_info(true, dev, "Unable to read controller version\n");
		return ret;
	} else
		pdata->y_resolution = (u16) temp;

	if (of_property_read_u32_array(np, "zinitix,area-size", px_zone, 3)){
		dev_info(dev, "Failed to get zone's size\n");
		pdata->area_indicator = 48;
		pdata->area_navigation = 96;
		pdata->area_edge = 60;
	} else {
		pdata->area_indicator = (u8) px_zone[0];
		pdata->area_navigation = (u8) px_zone[1];
		pdata->area_edge = (u8) px_zone[2];
	}

	ret = of_property_read_u32(np, "zinitix,page_size", &temp);
	if (ret) {
		input_info(true, dev, "Unable to read controller version\n");
		return ret;
	} else
		pdata->page_size = (u16) temp;

	pdata->gpio_int = of_get_named_gpio(np, "zinitix,irq_gpio", 0);
	if (pdata->gpio_int < 0) {
		pr_err("%s: of_get_named_gpio failed: tsp_gpio %d\n", __func__,
			pdata->gpio_int);
		return -EINVAL;
	}

	if (of_get_property(np, "zinitix,gpio_ldo_en", NULL)) {
			pdata->gpio_ldo_en = true;
	} else {
		if (of_property_read_string(np, "zinitix,regulator_dvdd", &pdata->regulator_dvdd)) {
			input_err(true, dev, "Failed to get regulator_dvdd name property\n");
			return -EINVAL;
		}
	}
	if (of_property_read_string(np, "zinitix,regulator_avdd", &pdata->regulator_avdd)) {
		input_err(true, dev, "Failed to get regulator_avdd name property\n");
		return -EINVAL;
	}

	pdata->tsp_power = bt532_power_ctrl;

	/* Optional parmeters(those values are not mandatory)
	 * do not return error value even if fail to get the value
	 */
	of_property_read_string(np, "zinitix,firmware_name", &pdata->firmware_name);
	of_property_read_string(np, "zinitix,chip_name", &pdata->chip_name);
	of_property_read_string(np, "zinitix,project_name", &pdata->project_name);
	of_property_read_string(np, "zinitix,regulator_tkled", &pdata->regulator_tkled);

	pdata->support_touchkey = of_property_read_bool(np, "zinitix,touchkey");
	pdata->support_spay = of_property_read_bool(np, "zinitix,spay");
	pdata->support_aod = of_property_read_bool(np, "zinitix,aod");
	pdata->support_aot = of_property_read_bool(np, "zinitix,aot");
	pdata->support_ear_detect = of_property_read_bool(np, "support_ear_detect_mode");
	pdata->support_open_short_test = of_property_read_bool(np, "support_open_short_test");
	pdata->support_lpm_mode = (pdata->support_spay | pdata->support_aod | pdata->support_aot);
	pdata->bringup = of_property_read_bool(np, "zinitix,bringup");
	pdata->mis_cal_check = of_property_read_bool(np, "zinitix,mis_cal_check");

	if (of_property_read_u32(np, "zinitix,factory_item_version", &pdata->item_version) < 0)
		pdata->item_version = 0;

	return 0;
}

static void sec_tclm_parse_dt(struct i2c_client *client, struct sec_tclm_data *tdata)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;

	if (of_property_read_u32(np, "zinitix,tclm_level", &tdata->tclm_level) < 0) {
		tdata->tclm_level = 0;
		input_err(true, dev, "%s: Failed to get tclm_level property\n", __func__);
	}

	if (of_property_read_u32(np, "zinitix,afe_base", &tdata->afe_base) < 0) {
		tdata->afe_base = 0;
		input_err(true, dev, "%s: Failed to get afe_base property\n", __func__);
	}

	input_err(true, &client->dev, "%s: tclm_level %d, afe_base %04X\n", __func__, tdata->tclm_level, tdata->afe_base);

}
#endif

static void zinitix_display_rawdata(struct bt532_ts_info *info, struct tsp_raw_data *raw_data, int type, int gap)
{
	int x_num = info->cap_info.x_node_num;
	int y_num = info->cap_info.y_node_num;
	unsigned char *pStr = NULL;
	unsigned char pTmp[16] = { 0 };
	int tmp_rawdata;
	int i, j;

	pStr = kzalloc(6 * (x_num + 1), GFP_KERNEL);
	if (pStr == NULL)
		return;

	memset(pStr, 0x0, 6 * (x_num + 1));
	snprintf(pTmp, sizeof(pTmp), "      Rx");
	strlcat(pStr, pTmp, 6 * (x_num + 1));

	for (i = 0; i < x_num; i++) {
		snprintf(pTmp, sizeof(pTmp), " %02d  ", i);
		strlcat(pStr, pTmp, 6 * (x_num + 1));
	}

	input_info(true, &info->client->dev, "%s\n", pStr);

	memset(pStr, 0x0, 6 * (x_num + 1));
	snprintf(pTmp, sizeof(pTmp), " +");
	strlcat(pStr, pTmp, 6 * (x_num + 1));

	for (i = 0; i < x_num; i++) {
		snprintf(pTmp, sizeof(pTmp), "-----");
		strlcat(pStr, pTmp, 6 * (x_num + 1));
	}

	input_info(true, &info->client->dev, "%s\n", pStr);

	for (i = 0; i < y_num; i++) {
		memset(pStr, 0x0, 6 * (x_num + 1));
		snprintf(pTmp, sizeof(pTmp), "Tx%02d | ", i);
		strlcat(pStr, pTmp, 6 * (x_num + 1));

		for (j = 0; j < x_num; j++) {
			switch (type) {
			case TOUCH_REF_ABNORMAL_TEST_MODE:
				/* print mis_cal data (value - DEF_MIS_CAL_SPEC_MID) */
				tmp_rawdata = raw_data->reference_data_abnormal[(i * x_num) + j];
				snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				break;
			case TOUCH_DND_MODE:
				if (gap == 1) {
					/* print dnd v gap data */
					tmp_rawdata = raw_data->vgap_data[(i * x_num) + j];
					snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				} else if (gap == 2) {
					/* print dnd h gap data */
					tmp_rawdata = raw_data->hgap_data[(i * x_num) + j];
					snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				} else {
					/* print dnd data */
					tmp_rawdata = raw_data->dnd_data[(i * x_num) + j];
					snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				}
				break;
			case TOUCH_RAW_MODE:
				/* print cnd data */
				tmp_rawdata = raw_data->cnd_data[(i * x_num) + j];
				snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				break;
			case TOUCH_JITTER_MODE:
				/* print jitter data */
				tmp_rawdata = raw_data->jitter_data[(i * x_num) + j];
				snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				break;
			case TOUCH_REFERENCE_MODE:
				/* print reference data */
				tmp_rawdata = raw_data->reference_data[(i * x_num) + j];
				snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				break;
			case TOUCH_DELTA_MODE:
				/* print delta data */
				tmp_rawdata = raw_data->delta_data[(i * x_num) + j];
				snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);
				break;
			}
			strlcat(pStr, pTmp, 6 * (x_num + 1));
		}
		input_info(true, &info->client->dev, "%s\n", pStr);
	}

	kfree(pStr);
}

/* print raw data at booting time */
static void bt532_display_rawdata(struct bt532_ts_info *info, struct tsp_raw_data *raw_data, int *min, int *max, bool is_mis_cal)
{
	int x_num = info->cap_info.x_node_num;
	int y_num = info->cap_info.y_node_num;
	unsigned char *pStr = NULL;
	unsigned char pTmp[16] = { 0 };
	int tmp_rawdata;
	int i, j;

	input_raw_info(true, &info->client->dev, "%s: %s\n", __func__, is_mis_cal ? "mis_cal ": "dnd ");

	pStr = kzalloc(6 * (x_num + 1), GFP_KERNEL);
	if (pStr == NULL)
		return;

	memset(pStr, 0x0, 6 * (x_num + 1));
	snprintf(pTmp, sizeof(pTmp), "      Rx");
	strlcat(pStr, pTmp, 6 * (x_num + 1));

	for (i = 0; i < x_num; i++) {
		snprintf(pTmp, sizeof(pTmp), " %02d  ", i);
		strlcat(pStr, pTmp, 6 * (x_num + 1));
	}

	input_raw_info(true, &info->client->dev, "%s\n", pStr);

	memset(pStr, 0x0, 6 * (x_num + 1));
	snprintf(pTmp, sizeof(pTmp), " +");
	strlcat(pStr, pTmp, 6 * (x_num + 1));

	for (i = 0; i < x_num; i++) {
		snprintf(pTmp, sizeof(pTmp), "-----");
		strlcat(pStr, pTmp, 6 * (x_num + 1));
	}

	input_raw_info(true, &info->client->dev, "%s\n", pStr);

	for (i = 0; i < y_num; i++) {
		memset(pStr, 0x0, 6 * (x_num + 1));
		snprintf(pTmp, sizeof(pTmp), "Tx%02d | ", i);
		strlcat(pStr, pTmp, 6 * (x_num + 1));

		for (j = 0; j < x_num; j++) {

			if (is_mis_cal) {
				/* print mis_cal data (value - DEF_MIS_CAL_SPEC_MID) */
				tmp_rawdata = raw_data->reference_data_abnormal[(i * x_num) + j];
				snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);

				if (tmp_rawdata < *min)
					*min = tmp_rawdata;

				if (tmp_rawdata > *max)
					*max = tmp_rawdata;

			} else {
				/* print dnd data */
				tmp_rawdata = raw_data->dnd_data[(i * x_num) + j];
				snprintf(pTmp, sizeof(pTmp), " %4d", tmp_rawdata);

				if (tmp_rawdata < *min && tmp_rawdata != 0)
					*min = tmp_rawdata;

				if (tmp_rawdata > *max)
					*max = tmp_rawdata;
			}
			strlcat(pStr, pTmp, 6 * (x_num + 1));
		}
		input_raw_info(true, &info->client->dev, "%s\n", pStr);
	}

	input_raw_info(true, &info->client->dev, "Max/Min %d,%d ##\n", *max, *min);

	kfree(pStr);
}

static void bt532_run_dnd(struct bt532_ts_info *info)
{
	struct tsp_raw_data *raw_data = info->raw_data;
	int min = 0xFFFF, max = -0xFF;
	int ret;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	ret = ts_set_touchmode(TOUCH_DND_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		goto out;
	}
	get_raw_data(info, (u8 *)raw_data->dnd_data, 1);
	ts_set_touchmode(TOUCH_POINT_MODE);

	bt532_display_rawdata(info, raw_data, &min, &max, false);

out:
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void bt532_run_mis_cal(struct bt532_ts_info *info)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	struct tsp_raw_data *raw_data = info->raw_data;

	char mis_cal_data = 0xF0;
	int ret = 0;
	s16 raw_data_buff[TSP_CMD_NODE_NUM];
	u16 chip_eeprom_info;
	int min = 0xFFFF, max = -0xFF;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
#endif
	disable_irq(info->irq);

	if (pdata->mis_cal_check == 0) {
		input_info(true, &info->client->dev, "%s: [ERROR] not support\n", __func__);
		mis_cal_data = 0xF1;
		goto NG;
	}

	if (info->work_state == SUSPEND) {
		input_info(true, &info->client->dev, "%s: [ERROR] Touch is stopped\n",__func__);
		mis_cal_data = 0xF2;
		goto NG;
	}

	if (read_data(info->client, BT532_EEPROM_INFO, (u8 *)&chip_eeprom_info, 2) < 0){
		input_info(true, &info->client->dev, "%s: read eeprom_info i2c fail!\n", __func__);
		mis_cal_data = 0xF3;
		goto NG;
	}

	if (zinitix_bit_test(chip_eeprom_info, 0)){
		input_info(true, &info->client->dev, "%s: eeprom cal 0, skip !\n", __func__);
		mis_cal_data = 0xF1;
		goto NG;
	}

	ret = ts_set_touchmode(TOUCH_REF_ABNORMAL_TEST_MODE);
	if (ret < 0) {
		ts_set_touchmode(TOUCH_POINT_MODE);
		mis_cal_data = 0xF4;
		goto NG;
	}
	ret = get_raw_data(info, (u8 *)raw_data->reference_data_abnormal, 2);
	if (!ret) {
		input_info(true, &info->client->dev, "%s:[ERROR] i2c fail!\n", __func__);
		ts_set_touchmode(TOUCH_POINT_MODE);
		mis_cal_data = 0xF4;
		goto NG;
	}
	ts_set_touchmode(TOUCH_POINT_MODE);

	bt532_display_rawdata(info, raw_data, &min, &max, true);
	if ((min + DEF_MIS_CAL_SPEC_MID) < DEF_MIS_CAL_SPEC_MIN ||
		(max + DEF_MIS_CAL_SPEC_MID) > DEF_MIS_CAL_SPEC_MAX) {
		mis_cal_data = 0xFD;
		goto NG;
	}

	mis_cal_data = 0x00;
	input_info(true, &info->client->dev, "%s : mis_cal_data: %X", __func__, mis_cal_data);
	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
NG:
	input_err(true, &info->client->dev, "%s : mis_cal_data: %X", __func__, mis_cal_data);
	if (mis_cal_data == 0xFD)
	{
		run_tsp_rawdata_read(&info->sec, 7, raw_data_buff);
		run_tsp_rawdata_read(&info->sec, TOUCH_REFERENCE_MODE, raw_data_buff);
	}
	enable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
	write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
	return;
}

static void bt532_run_rawdata(struct bt532_ts_info *info)
{
	info->tsp_dump_lock = 1;
	input_raw_data_clear();
	input_raw_info(true, &info->client->dev, "%s: start ##\n", __func__);
	bt532_run_dnd(info);
	bt532_run_mis_cal(info);
	input_raw_info(true, &info->client->dev, "%s: done ##\n", __func__);
	info->tsp_dump_lock = 0;
}

#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
#include <linux/sec_debug.h>
extern struct tsp_dump_callbacks dump_callbacks;
static struct delayed_work *p_ghost_check;

static void bt532_check_rawdata(struct work_struct *work)
{
	struct bt532_ts_info *info = container_of(work, struct bt532_ts_info,
			ghost_check.work);

	if (info->tsp_dump_lock == 1) {
		input_info(true, &info->client->dev, "%s: ignored ## already checking..\n", __func__);
		return;
	}

	if (info->tsp_pwr_enabled == POWER_OFF) {
		input_info(true, &info->client->dev, "%s: ignored ## IC is power off\n", __func__);
		return;
	}

	bt532_run_rawdata(info);
}

static void dump_tsp_log(void)
{
	pr_info("%s: %s %s: start\n", BT532_TS_DEVICE, SECLOG, __func__);

#ifdef CONFIG_BATTERY_SAMSUNG
	if (lpcharge == 1) {
		pr_err("%s: %s %s: ignored ## lpm charging Mode!!\n", BT532_TS_DEVICE, SECLOG, __func__);
		return;
	}
#endif

	if (p_ghost_check == NULL) {
		pr_err("%s: %s %s: ignored ## tsp probe fail!!\n", BT532_TS_DEVICE, SECLOG, __func__);
		return;
	}
	schedule_delayed_work(p_ghost_check, msecs_to_jiffies(100));
}
#endif

static void zt_read_info_work(struct work_struct *work)
{
	struct bt532_ts_info *info = container_of(work, struct bt532_ts_info,
			work_read_info.work);
#ifdef TCLM_CONCEPT
	u8 data[2] = {0};
	int ret;
#endif

	mutex_lock(&info->modechange);

#ifdef TCLM_CONCEPT
	get_zt_tsp_nvm_data(info, BT532_TS_NVM_OFFSET_FAC_RESULT, (u8 *)data, 2);
	info->test_result.data[0] = data[0];

#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD);
#endif

	write_reg(info->client, BT532_INT_ENABLE_FLAG, 0x153F);
	input_info(true, &info->client->dev, "%s: int disabled addr = 0x%x, val = 0x153F\r\n",
		__func__, BT532_INT_ENABLE_FLAG);

	ret = sec_tclm_check_cal_case(info->tdata);
	input_info(true, &info->client->dev, "%s: sec_tclm_check_cal_case result %d; test result %X\n",
				__func__, ret, info->test_result.data[0]);

	write_reg(info->client, BT532_INT_ENABLE_FLAG, 0x113F);
	input_info(true, &info->client->dev, "%s: int enabled addr = 0x%x, val = 0x113F\r\n",
		__func__, BT532_INT_ENABLE_FLAG);

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, info);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
#endif
#endif
	run_test_open_short(info);

	input_log_fix();
	bt532_run_rawdata(info);
	info->info_work_done = true;
	mutex_unlock(&info->modechange);
}

static void zt_set_input_prop_proximity(struct bt532_ts_info *info, struct input_dev *dev)
{
	static char zt_phys[64] = { 0 };

	snprintf(zt_phys, sizeof(zt_phys), "%s/input1", dev->name);
	dev->phys = zt_phys;
	dev->id.bustype = BUS_I2C;
	dev->dev.parent = &info->client->dev;

	set_bit(EV_SYN, dev->evbit);
	set_bit(EV_SW, dev->evbit);

	set_bit(INPUT_PROP_DIRECT, dev->propbit);

	input_set_abs_params(dev, ABS_MT_CUSTOM, 0, 0xFFFFFFFF, 0, 0);
	input_set_abs_params(dev, ABS_MT_CUSTOM2, 0, 0xFFFFFFFF, 0, 0);
	input_set_drvdata(dev, info);
}

static int bt532_ts_probe(struct i2c_client *client,
		const struct i2c_device_id *i2c_id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct bt532_ts_platform_data *pdata = client->dev.platform_data;
	struct sec_tclm_data *tdata = NULL;
	struct bt532_ts_info *info;
	struct device_node *np = client->dev.of_node;
	int ret = 0;
	int i;
	bool force_update = false;
#if (!defined(CONFIG_EXYNOS_DECON_MDNIE_LITE) && defined(CONFIG_FB_MSM_MDSS_SAMSUNG))
	int lcdtype = 0;
#endif

#ifdef CONFIG_BATTERY_SAMSUNG
	if (lpcharge == 1) {
		input_err(true, &client->dev, "%s : Do not load driver due to : lpm %d\n",
				__func__, lpcharge);
		return -ENODEV;
	}
#endif

#ifdef CONFIG_FB_MSM_MDSS_SAMSUNG
		lcdtype = get_lcd_attached("GET");
		if (lcdtype == 0xFFFFFF) {
			input_err(true, &client->dev, "%s: lcd is not attached\n", __func__);
			return -ENODEV;
		}
#endif

#ifdef CONFIG_EXYNOS_DECON_MDNIE_LITE
		if (lcdtype == 0) {
			input_err(true, &client->dev, "%s: lcd is not attached\n", __func__);
			return -ENODEV;
		}
#endif

	if (client->dev.of_node) {
		if (!pdata) {
			pdata = devm_kzalloc(&client->dev,
					sizeof(*pdata), GFP_KERNEL);
			if (!pdata)
				return -ENOMEM;
		}
		ret = bt532_ts_parse_dt(np, &client->dev, pdata);
		if (ret){
			input_err(true, &client->dev, "Error parsing dt %d\n", ret);
			goto err_no_platform_data;
		}
		tdata = devm_kzalloc(&client->dev,
				sizeof(struct sec_tclm_data), GFP_KERNEL);
		if (!tdata)
			goto error_allocate_tdata;

		sec_tclm_parse_dt(client, tdata);

		ret = zinitix_init_gpio(pdata);
		if (ret < 0)
			goto err_gpio_request;

	}
	else if (!pdata) {
		input_err(true, &client->dev, "Not exist platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		input_err(true, &client->dev, "Not compatible i2c function\n");
		return -EIO;
	}

	info = kzalloc(sizeof(struct bt532_ts_info), GFP_KERNEL);
	if (!info) {
		input_err(true, &client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, info);
	info->client = client;
	info->pdata = pdata;

	info->tdata = tdata;
	if (!info->tdata)
		goto error_null_data;

#ifdef TCLM_CONCEPT
	sec_tclm_initialize(info->tdata);
	info->tdata->client = info->client;
	info->tdata->tclm_read = bt532_tclm_data_read;
	info->tdata->tclm_write = bt532_tclm_data_write;
	info->tdata->tclm_execute_force_calibration = zt_tclm_execute_force_calibration;
#endif
	INIT_DELAYED_WORK(&info->work_read_info, zt_read_info_work);
	mutex_init(&info->modechange);

	info->input_dev = input_allocate_device();
	if (!info->input_dev) {
		input_err(true, &client->dev, "Failed to allocate input device\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	if (pdata->support_ear_detect) {
		info->input_dev_proximity = input_allocate_device();
		if (!info->input_dev_proximity) {
			input_err(true, &client->dev, "%s: allocate input_dev_proximity err!\n", __func__);
			ret = -ENOMEM;
			goto err_allocate_input_dev_proximity;
		}

		info->input_dev_proximity->name = "sec_touchproximity";
		zt_set_input_prop_proximity(info, info->input_dev_proximity);
	}

	info->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(info->pinctrl)) {
		input_err(true, &client->dev, "%s: Failed to get pinctrl data\n", __func__);
		ret = PTR_ERR(info->pinctrl);
		goto err_get_pinctrl;
	}

	info->work_state = PROBE;

	// power on
	if (bt532_power_control(info, POWER_ON_SEQUENCE) == false) {
		ret = -EPERM;
		goto err_power_sequence;
	}

	/* init touch mode */
	info->touch_mode = TOUCH_POINT_MODE;
	misc_info = info;
	mutex_init(&info->set_reg_lock);

#if ESD_TIMER_INTERVAL
	spin_lock_init(&info->lock);
	INIT_WORK(&info->tmr_work, ts_tmr_work);
	esd_tmr_workqueue =
		create_singlethread_workqueue("esd_tmr_workqueue");

	if (!esd_tmr_workqueue) {
		input_err(true, &client->dev, "Failed to create esd tmr work queue\n");
		ret = -EPERM;

		goto err_esd_sequence;
	}

	esd_timer_init(info);
#endif
	sema_init(&info->work_lock, 1);

	ret = ic_version_check(info);
	if (ret < 0) {
		input_err(true, &info->client->dev,
			"%s: fail version check", __func__);
		force_update = true;
	}

	ret = fw_update_work(info, force_update);
	if (ret < 0) {
		ret = -EPERM;
		input_err(true, &info->client->dev,
			"%s: fail update_work", __func__);
		goto err_fw_update;
	}

	if (pdata->support_touchkey){
		for (i = 0; i < MAX_SUPPORTED_BUTTON_NUM; i++)
			info->button[i] = ICON_BUTTON_UNCHANGE;
	}
	snprintf(info->phys, sizeof(info->phys),
		"%s/input0", dev_name(&client->dev));
	info->input_dev->name = "sec_touchscreen";
	info->input_dev->id.bustype = BUS_I2C;
/*	info->input_dev->id.vendor = 0x0001; */
	info->input_dev->phys = info->phys;
/*	info->input_dev->id.product = 0x0002; */
/*	info->input_dev->id.version = 0x0100; */
	info->input_dev->dev.parent = &client->dev;

#ifdef GLOVE_MODE
	input_set_capability(info->input_dev, EV_SW, SW_GLOVE);
#endif

	set_bit(EV_SYN, info->input_dev->evbit);
	set_bit(EV_KEY, info->input_dev->evbit);
	set_bit(EV_ABS, info->input_dev->evbit);
	set_bit(BTN_TOUCH, info->input_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, info->input_dev->propbit);
	set_bit(KEY_INT_CANCEL, info->input_dev->keybit);
	set_bit(EV_LED, info->input_dev->evbit);
	set_bit(LED_MISC, info->input_dev->ledbit);
	set_bit(BTN_PALM, info->input_dev->keybit);

	if (pdata->support_touchkey){
		for (i = 0; i < MAX_SUPPORTED_BUTTON_NUM; i++)
			set_bit(BUTTON_MAPPING_KEY[i], info->input_dev->keybit);
	}

	if (pdata->support_lpm_mode){
		set_bit(KEY_BLACK_UI_GESTURE, info->input_dev->keybit);
		set_bit(KEY_WAKEUP, info->input_dev->keybit);
	}

	input_set_abs_params(info->input_dev, ABS_MT_POSITION_X,
		0, pdata->x_resolution + ABS_PT_OFFSET,	0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_POSITION_Y,
		0, pdata->y_resolution + ABS_PT_OFFSET,	0, 0);
#ifdef CONFIG_SEC_FACTORY
	input_set_abs_params(info->input_dev, ABS_MT_PRESSURE,
		0, 3000, 0, 0);
#endif
	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MAJOR,
		0, 255, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_WIDTH_MAJOR,
		0, 255, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MINOR,
		0, 255, 0, 0);

	set_bit(MT_TOOL_FINGER, info->input_dev->keybit);
	input_mt_init_slots(info->input_dev, MAX_SUPPORTED_FINGER_NUM,
			INPUT_MT_DIRECT);

	input_set_drvdata(info->input_dev, info);
	ret = input_register_device(info->input_dev);
	if (ret) {
		input_info(true, &client->dev, "unable to register %s input device\r\n",
			info->input_dev->name);
		goto err_input_register_device;
	}

	if (pdata->support_ear_detect) {
		ret = input_register_device(info->input_dev_proximity);
		if (ret) {
			input_err(true, &client->dev, "%s: Unable to register %s input device\n",
							__func__, info->input_dev_proximity->name);
			goto err_input_proximity_register_device;
		}
	}

	if (init_touch(info) == false) {
		ret = -EPERM;
		goto err_init_touch;
	}

	info->work_state = NOTHING;

	init_completion(&info->resume_done);
	complete_all(&info->resume_done);

	/* configure irq */
	info->irq = gpio_to_irq(pdata->gpio_int);
	if (info->irq < 0){
		input_info(true, &client->dev, "error. gpio_to_irq(..) function is not \
			supported? you should define GPIO_TOUCH_IRQ.\r\n");
		ret = -EINVAL;
		goto error_gpio_irq;
	}

	/* ret = request_threaded_irq(info->irq, ts_int_handler, bt532_touch_work,*/
	ret = request_threaded_irq(info->irq, NULL, bt532_touch_work,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT , BT532_TS_DEVICE, info);

	if (ret) {
		input_info(true, &client->dev, "unable to register irq.(%s)\r\n",
			info->input_dev->name);
		goto err_request_irq;
	}
	input_info(true, &client->dev, "zinitix touch probe.\r\n");

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	trustedui_set_tsp_irq(info->irq);
	input_info(true, &client->dev, "%s[%d] called!\n",
		__func__, info->irq);
#endif

	sema_init(&info->raw_data_lock, 1);
#ifdef USE_MISC_DEVICE
	ret = misc_register(&touch_misc_device);
	if (ret) {
		input_err(true, &client->dev, "Failed to register touch misc device\n");
		goto err_misc_register;
	}
#endif
#ifdef SEC_FACTORY_TEST
	ret = init_sec_factory(info);
	if (ret) {
		input_err(true, &client->dev, "Failed to init sec factory device\n");

		goto err_kthread_create_failed;
	}
#endif
#ifdef CONFIG_INPUT_ENABLED
	ret = sysfs_create_group(&info->input_dev->dev.kobj, &input_attr_group);
	if (ret < 0)
		input_err(true, &info->client->dev, "%s: Failed to create input_attr_group\n", __func__);
#endif

	info->register_cb = info->pdata->register_cb;

	info->callbacks.inform_charger = bt532_charger_status_cb;
	if (info->register_cb)
		info->register_cb(&info->callbacks);
#ifdef CONFIG_VBUS_NOTIFIER
	vbus_notifier_register(&info->vbus_nb, tsp_vbus_notification,
				VBUS_NOTIFY_DEV_CHARGER);
#endif

	if (pdata->support_lpm_mode){
		device_init_wakeup(&client->dev, true);
	}

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	tui_tsp_info = info;
#endif
#ifdef CONFIG_SAMSUNG_TUI
	tui_tsp_info = info;
#endif

	schedule_delayed_work(&info->work_read_info, msecs_to_jiffies(5000));

#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
	dump_callbacks.inform_dump = dump_tsp_log;
	INIT_DELAYED_WORK(&info->ghost_check, bt532_check_rawdata);
	p_ghost_check = &info->ghost_check;
#endif

	input_log_fix();
	return 0;

#ifdef SEC_FACTORY_TEST
err_kthread_create_failed:
	sec_cmd_exit(&info->sec, SEC_CLASS_DEVT_TSP);
	kfree(info->raw_data);
#endif
#ifdef USE_MISC_DEVICE
err_misc_register:
#endif
	free_irq(info->irq, info);
err_request_irq:
error_gpio_irq:
err_init_touch:
	if (pdata->support_ear_detect)
		input_unregister_device(info->input_dev_proximity);
err_input_proximity_register_device:
	input_unregister_device(info->input_dev);
err_input_register_device:
err_fw_update:
#if ESD_TIMER_INTERVAL
	del_timer(&(info->esd_timeout_tmr));
err_esd_sequence:
#endif
err_power_sequence:
	bt532_power_control(info, POWER_OFF);
err_get_pinctrl:
	if (pdata->support_ear_detect) {
		input_free_device(info->input_dev_proximity);
		info->input_dev_proximity = NULL;
	}
err_allocate_input_dev_proximity:
	input_free_device(info->input_dev);
	info->input_dev = NULL;
error_null_data:
err_alloc:
	kfree(info);
err_gpio_request:
error_allocate_tdata:
	if (IS_ENABLED(CONFIG_OF))
		devm_kfree(&client->dev, (void *)tdata);
err_no_platform_data:
	if (IS_ENABLED(CONFIG_OF))
		devm_kfree(&client->dev, (void *)pdata);

#ifdef CONFIG_TOUCHSCREEN_DUMP_MODE
		p_ghost_check = NULL;
#endif
	input_info(true, &client->dev, "Failed to probe\n");
	input_log_fix();
	return ret;
}

static int bt532_ts_remove(struct i2c_client *client)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	struct bt532_ts_platform_data *pdata = info->pdata;

	disable_irq(info->irq);
	down(&info->work_lock);

	info->work_state = REMOVE;

	cancel_delayed_work_sync(&info->work_read_info);
	flush_delayed_work(&info->work_read_info);

#ifdef SEC_FACTORY_TEST
	sec_cmd_exit(&info->sec, SEC_CLASS_DEVT_TSP);
	kfree(info->raw_data);
#endif

#ifdef CONFIG_TOUCHSCREEN_DUMP_MODE
	p_ghost_check = NULL;
#endif

#if ESD_TIMER_INTERVAL
	flush_work(&info->tmr_work);
	write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
	esd_timer_stop(info);
#if defined(TSP_VERBOSE_DEBUG)
	input_info(true, &client->dev, "Stopped esd timer\n");
#endif
	destroy_workqueue(esd_tmr_workqueue);
#endif

	if (info->irq)
		free_irq(info->irq, info);
#ifdef USE_MISC_DEVICE
	misc_deregister(&touch_misc_device);
#endif

	if (gpio_is_valid(pdata->gpio_int) != 0)
		gpio_free(pdata->gpio_int);

	if (pdata->support_ear_detect) {
		input_mt_destroy_slots(info->input_dev_proximity);
		input_unregister_device(info->input_dev_proximity);
	}

	input_unregister_device(info->input_dev);
	input_free_device(info->input_dev);
	up(&info->work_lock);
	kfree(info);

	return 0;
}

void bt532_ts_shutdown(struct i2c_client *client)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);

	input_info(true, &client->dev, "%s++\n",__func__);
	disable_irq(info->irq);
	down(&info->work_lock);
#if ESD_TIMER_INTERVAL
	flush_work(&info->tmr_work);
	esd_timer_stop(info);
#endif
	up(&info->work_lock);
	bt532_power_control(info, POWER_OFF);
	input_info(true, &client->dev, "%s--\n",__func__);
}

#ifdef CONFIG_SAMSUNG_TUI
extern int stui_i2c_lock(struct i2c_adapter *adap);
extern int stui_i2c_unlock(struct i2c_adapter *adap);

int stui_tsp_enter(void)
{
	int ret = 0;

	if (!tui_tsp_info)
		return -EINVAL;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(tui_tsp_info);
	write_reg(tui_tsp_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);
#endif

	disable_irq(tui_tsp_info->irq);
	clear_report_data(tui_tsp_info);

	ret = stui_i2c_lock(tui_tsp_info->client->adapter);
	if (ret) {
		pr_err("[STUI] stui_i2c_lock failed : %d\n", ret);
		enable_irq(tui_tsp_info->client->irq);
		return -1;
	}

	return 0;
}

int stui_tsp_exit(void)
{
	int ret = 0;

	if (!tui_tsp_info)
		return -EINVAL;

	ret = stui_i2c_unlock(tui_tsp_info->client->adapter);
	if (ret)
		pr_err("[STUI] stui_i2c_unlock failed : %d\n", ret);

	enable_irq(tui_tsp_info->irq);

#if ESD_TIMER_INTERVAL
	write_reg(tui_tsp_info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
		SCAN_RATE_HZ * ESD_TIMER_INTERVAL);
	esd_timer_start(CHECK_ESD_TIMER, tui_tsp_info);
#endif

	return ret;
}
#endif

#ifdef CONFIG_PM
static int bt532_ts_pm_suspend(struct device *dev)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);

	reinit_completion(&info->resume_done);

	return 0;
}

static int bt532_ts_pm_resume(struct device *dev)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);

	complete_all(&info->resume_done);

	return 0;
}

static const struct dev_pm_ops bt532_ts_dev_pm_ops = {
	.suspend = bt532_ts_pm_suspend,
	.resume = bt532_ts_pm_resume,
};
#endif

static struct i2c_device_id bt532_idtable[] = {
	{BT532_TS_DEVICE, 0},
	{ }
};

#ifdef CONFIG_OF
static const struct of_device_id zinitix_match_table[] = {
	{ .compatible = "zinitix,bt532_ts_device",},
	{},
};
#endif

static struct i2c_driver bt532_ts_driver = {
	.probe	= bt532_ts_probe,
	.remove	= bt532_ts_remove,
	.shutdown = bt532_ts_shutdown,
	.id_table	= bt532_idtable,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= BT532_TS_DEVICE,
#ifdef CONFIG_OF
		.of_match_table = zinitix_match_table,
#endif
#ifdef CONFIG_PM
		.pm = &bt532_ts_dev_pm_ops,
#endif
	},
};

static int __init bt532_ts_init(void)
{
	pr_info("[TSP]: %s\n", __func__);
	return i2c_add_driver(&bt532_ts_driver);
}

static void __exit bt532_ts_exit(void)
{
	i2c_del_driver(&bt532_ts_driver);
}

module_init(bt532_ts_init);
module_exit(bt532_ts_exit);

MODULE_DESCRIPTION("touch-screen device driver using i2c interface");
MODULE_AUTHOR("<mika.kim@samsung.com>");
MODULE_LICENSE("GPL");
