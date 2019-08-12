/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * drivers/input/tablet/wacom_wac.h
 */
#ifndef WACOM_WAC_H
#define WACOM_WAC_H

#include <linux/types.h>
#include <linux/hid.h>
#include <linux/kfifo.h>

/* maximum packet length for USB/BT devices */
#define WACOM_PKGLEN_MAX	361

#define WACOM_NAME_MAX		64
#define WACOM_MAX_REMOTES	5
#define WACOM_STATUS_UNKNOWN	255

/* packet length for individual models */
#define WACOM_PKGLEN_BBFUN	 9
#define WACOM_PKGLEN_TPC1FG	 5
#define WACOM_PKGLEN_TPC1FG_B	10
#define WACOM_PKGLEN_TPC2FG	14
#define WACOM_PKGLEN_BBTOUCH	20
#define WACOM_PKGLEN_BBTOUCH3	64
#define WACOM_PKGLEN_BBPEN	10
#define WACOM_PKGLEN_WIRELESS	32
#define WACOM_PKGLEN_PENABLED	 8
#define WACOM_PKGLEN_BPAD_TOUCH	32
#define WACOM_PKGLEN_BPAD_TOUCH_USB	64

/* wacom data size per MT contact */
#define WACOM_BYTES_PER_MT_PACKET	11
#define WACOM_BYTES_PER_24HDT_PACKET	14
#define WACOM_BYTES_PER_QHDTHID_PACKET	 6

/* device IDs */
#define STYLUS_DEVICE_ID	0x02
#define TOUCH_DEVICE_ID		0x03
#define CURSOR_DEVICE_ID	0x06
#define ERASER_DEVICE_ID	0x0A
#define PAD_DEVICE_ID		0x0F

/* wacom data packet report IDs */
#define WACOM_REPORT_PENABLED		2
#define WACOM_REPORT_PENABLED_BT	3
#define WACOM_REPORT_INTUOS_ID1		5
#define WACOM_REPORT_INTUOS_ID2		6
#define WACOM_REPORT_INTUOSPAD		12
#define WACOM_REPORT_INTUOS5PAD		3
#define WACOM_REPORT_DTUSPAD		21
#define WACOM_REPORT_TPC1FG		6
#define WACOM_REPORT_TPC2FG		13
#define WACOM_REPORT_TPCMT		13
#define WACOM_REPORT_TPCMT2		3
#define WACOM_REPORT_TPCHID		15
#define WACOM_REPORT_CINTIQ		16
#define WACOM_REPORT_CINTIQPAD		17
#define WACOM_REPORT_TPCST		16
#define WACOM_REPORT_DTUS		17
#define WACOM_REPORT_TPC1FGE		18
#define WACOM_REPORT_24HDT		1
#define WACOM_REPORT_WL			128
#define WACOM_REPORT_USB		192
#define WACOM_REPORT_BPAD_PEN		3
#define WACOM_REPORT_BPAD_TOUCH		16
#define WACOM_REPORT_DEVICE_LIST	16
#define WACOM_REPORT_INTUOS_PEN		16
#define WACOM_REPORT_REMOTE		17
#define WACOM_REPORT_INTUOSHT2_ID	8

/* wacom command report ids */
#define WAC_CMD_WL_LED_CONTROL          0x03
#define WAC_CMD_LED_CONTROL             0x20
#define WAC_CMD_ICON_START              0x21
#define WAC_CMD_ICON_XFER               0x23
#define WAC_CMD_ICON_BT_XFER            0x26
#define WAC_CMD_DELETE_PAIRING          0x20
#define WAC_CMD_LED_CONTROL_GENERIC     0x32
#define WAC_CMD_UNPAIR_ALL              0xFF
#define WAC_CMD_WL_INTUOSP2             0x82

/* device quirks */
#define WACOM_QUIRK_BBTOUCH_LOWRES	0x0001
#define WACOM_QUIRK_SENSE		0x0002
#define WACOM_QUIRK_AESPEN		0x0004
#define WACOM_QUIRK_BATTERY		0x0008
#define WACOM_QUIRK_TOOLSERIAL		0x0010

/* device types */
#define WACOM_DEVICETYPE_NONE           0x0000
#define WACOM_DEVICETYPE_PEN            0x0001
#define WACOM_DEVICETYPE_TOUCH          0x0002
#define WACOM_DEVICETYPE_PAD            0x0004
#define WACOM_DEVICETYPE_WL_MONITOR     0x0008
#define WACOM_DEVICETYPE_DIRECT         0x0010

#define WACOM_POWER_SUPPLY_STATUS_AUTO  -1

#define WACOM_HID_UP_WACOMDIGITIZER     0xff0d0000
#define WACOM_HID_SP_PAD                0x00040000
#define WACOM_HID_SP_BUTTON             0x00090000
#define WACOM_HID_SP_DIGITIZER          0x000d0000
#define WACOM_HID_SP_DIGITIZERINFO      0x00100000
#define WACOM_HID_WD_DIGITIZER          (WACOM_HID_UP_WACOMDIGITIZER | 0x01)
#define WACOM_HID_WD_PEN                (WACOM_HID_UP_WACOMDIGITIZER | 0x02)
#define WACOM_HID_WD_SENSE              (WACOM_HID_UP_WACOMDIGITIZER | 0x36)
#define WACOM_HID_WD_DIGITIZERFNKEYS    (WACOM_HID_UP_WACOMDIGITIZER | 0x39)
#define WACOM_HID_WD_SERIALNUMBER       (WACOM_HID_UP_WACOMDIGITIZER | 0x5b)
#define WACOM_HID_WD_SERIALHI           (WACOM_HID_UP_WACOMDIGITIZER | 0x5c)
#define WACOM_HID_WD_TOOLTYPE           (WACOM_HID_UP_WACOMDIGITIZER | 0x77)
#define WACOM_HID_WD_DISTANCE           (WACOM_HID_UP_WACOMDIGITIZER | 0x0132)
#define WACOM_HID_WD_TOUCHSTRIP         (WACOM_HID_UP_WACOMDIGITIZER | 0x0136)
#define WACOM_HID_WD_TOUCHSTRIP2        (WACOM_HID_UP_WACOMDIGITIZER | 0x0137)
#define WACOM_HID_WD_TOUCHRING          (WACOM_HID_UP_WACOMDIGITIZER | 0x0138)
#define WACOM_HID_WD_TOUCHRINGSTATUS    (WACOM_HID_UP_WACOMDIGITIZER | 0x0139)
#define WACOM_HID_WD_REPORT_VALID       (WACOM_HID_UP_WACOMDIGITIZER | 0x01d0)
#define WACOM_HID_WD_ACCELEROMETER_X    (WACOM_HID_UP_WACOMDIGITIZER | 0x0401)
#define WACOM_HID_WD_ACCELEROMETER_Y    (WACOM_HID_UP_WACOMDIGITIZER | 0x0402)
#define WACOM_HID_WD_ACCELEROMETER_Z    (WACOM_HID_UP_WACOMDIGITIZER | 0x0403)
#define WACOM_HID_WD_BATTERY_CHARGING   (WACOM_HID_UP_WACOMDIGITIZER | 0x0404)
#define WACOM_HID_WD_TOUCHONOFF         (WACOM_HID_UP_WACOMDIGITIZER | 0x0454)
#define WACOM_HID_WD_BATTERY_LEVEL      (WACOM_HID_UP_WACOMDIGITIZER | 0x043b)
#define WACOM_HID_WD_EXPRESSKEY00       (WACOM_HID_UP_WACOMDIGITIZER | 0x0910)
#define WACOM_HID_WD_EXPRESSKEYCAP00    (WACOM_HID_UP_WACOMDIGITIZER | 0x0950)
#define WACOM_HID_WD_MODE_CHANGE        (WACOM_HID_UP_WACOMDIGITIZER | 0x0980)
#define WACOM_HID_WD_MUTE_DEVICE        (WACOM_HID_UP_WACOMDIGITIZER | 0x0981)
#define WACOM_HID_WD_CONTROLPANEL       (WACOM_HID_UP_WACOMDIGITIZER | 0x0982)
#define WACOM_HID_WD_ONSCREEN_KEYBOARD  (WACOM_HID_UP_WACOMDIGITIZER | 0x0983)
#define WACOM_HID_WD_BUTTONCONFIG       (WACOM_HID_UP_WACOMDIGITIZER | 0x0986)
#define WACOM_HID_WD_BUTTONHOME         (WACOM_HID_UP_WACOMDIGITIZER | 0x0990)
#define WACOM_HID_WD_BUTTONUP           (WACOM_HID_UP_WACOMDIGITIZER | 0x0991)
#define WACOM_HID_WD_BUTTONDOWN         (WACOM_HID_UP_WACOMDIGITIZER | 0x0992)
#define WACOM_HID_WD_BUTTONLEFT         (WACOM_HID_UP_WACOMDIGITIZER | 0x0993)
#define WACOM_HID_WD_BUTTONRIGHT        (WACOM_HID_UP_WACOMDIGITIZER | 0x0994)
#define WACOM_HID_WD_BUTTONCENTER       (WACOM_HID_UP_WACOMDIGITIZER | 0x0995)
#define WACOM_HID_WD_FINGERWHEEL        (WACOM_HID_UP_WACOMDIGITIZER | 0x0d03)
#define WACOM_HID_WD_OFFSETLEFT         (WACOM_HID_UP_WACOMDIGITIZER | 0x0d30)
#define WACOM_HID_WD_OFFSETTOP          (WACOM_HID_UP_WACOMDIGITIZER | 0x0d31)
#define WACOM_HID_WD_OFFSETRIGHT        (WACOM_HID_UP_WACOMDIGITIZER | 0x0d32)
#define WACOM_HID_WD_OFFSETBOTTOM       (WACOM_HID_UP_WACOMDIGITIZER | 0x0d33)
#define WACOM_HID_WD_DATAMODE           (WACOM_HID_UP_WACOMDIGITIZER | 0x1002)
#define WACOM_HID_WD_DIGITIZERINFO      (WACOM_HID_UP_WACOMDIGITIZER | 0x1013)
#define WACOM_HID_WD_TOUCH_RING_SETTING (WACOM_HID_UP_WACOMDIGITIZER | 0x1032)
#define WACOM_HID_UP_G9                 0xff090000
#define WACOM_HID_G9_PEN                (WACOM_HID_UP_G9 | 0x02)
#define WACOM_HID_G9_TOUCHSCREEN        (WACOM_HID_UP_G9 | 0x11)
#define WACOM_HID_UP_G11                0xff110000
#define WACOM_HID_G11_PEN               (WACOM_HID_UP_G11 | 0x02)
#define WACOM_HID_G11_TOUCHSCREEN       (WACOM_HID_UP_G11 | 0x11)
#define WACOM_HID_UP_WACOMTOUCH         0xff000000
#define WACOM_HID_WT_TOUCHSCREEN        (WACOM_HID_UP_WACOMTOUCH | 0x04)
#define WACOM_HID_WT_TOUCHPAD           (WACOM_HID_UP_WACOMTOUCH | 0x05)
#define WACOM_HID_WT_CONTACTMAX         (WACOM_HID_UP_WACOMTOUCH | 0x55)
#define WACOM_HID_WT_SERIALNUMBER       (WACOM_HID_UP_WACOMTOUCH | 0x5b)
#define WACOM_HID_WT_X                  (WACOM_HID_UP_WACOMTOUCH | 0x130)
#define WACOM_HID_WT_Y                  (WACOM_HID_UP_WACOMTOUCH | 0x131)
#define WACOM_HID_WT_REPORT_VALID       (WACOM_HID_UP_WACOMTOUCH | 0x1d0)

#define WACOM_BATTERY_USAGE(f)	(((f)->hid == HID_DG_BATTERYSTRENGTH) || \
				 ((f)->hid == WACOM_HID_WD_BATTERY_CHARGING) || \
				 ((f)->hid == WACOM_HID_WD_BATTERY_LEVEL))

#define WACOM_PAD_FIELD(f)	(((f)->physical == HID_DG_TABLETFUNCTIONKEY) || \
				 ((f)->physical == WACOM_HID_WD_DIGITIZERFNKEYS) || \
				 ((f)->physical == WACOM_HID_WD_DIGITIZERINFO))

#define WACOM_PEN_FIELD(f)	(((f)->logical == HID_DG_STYLUS) || \
				 ((f)->physical == HID_DG_STYLUS) || \
				 ((f)->physical == HID_DG_PEN) || \
				 ((f)->application == HID_DG_PEN) || \
				 ((f)->application == HID_DG_DIGITIZER) || \
				 ((f)->application == WACOM_HID_WD_PEN) || \
				 ((f)->application == WACOM_HID_WD_DIGITIZER) || \
				 ((f)->application == WACOM_HID_G9_PEN) || \
				 ((f)->application == WACOM_HID_G11_PEN))
#define WACOM_FINGER_FIELD(f)	(((f)->logical == HID_DG_FINGER) || \
				 ((f)->physical == HID_DG_FINGER) || \
				 ((f)->application == HID_DG_TOUCHSCREEN) || \
				 ((f)->application == WACOM_HID_G9_TOUCHSCREEN) || \
				 ((f)->application == WACOM_HID_G11_TOUCHSCREEN) || \
				 ((f)->application == WACOM_HID_WT_TOUCHPAD) || \
				 ((f)->application == HID_DG_TOUCHPAD))

#define WACOM_DIRECT_DEVICE(f)	(((f)->application == HID_DG_TOUCHSCREEN) || \
				 ((f)->application == WACOM_HID_WT_TOUCHSCREEN) || \
				 ((f)->application == HID_DG_PEN) || \
				 ((f)->application == WACOM_HID_WD_PEN))

enum {
	PENPARTNER = 0,
	GRAPHIRE,
	GRAPHIRE_BT,
	WACOM_G4,
	PTU,
	PL,
	DTU,
	DTUS,
	DTUSX,
	INTUOS,
	INTUOS3S,
	INTUOS3,
	INTUOS3L,
	INTUOS4S,
	INTUOS4,
	INTUOS4WL,
	INTUOS4L,
	INTUOS5S,
	INTUOS5,
	INTUOS5L,
	INTUOSPS,
	INTUOSPM,
	INTUOSPL,
	INTUOSP2_BT,
	INTUOSP2S_BT,
	INTUOSHT3_BT,
	WACOM_21UX2,
	WACOM_22HD,
	DTK,
	WACOM_24HD,
	WACOM_27QHD,
	CINTIQ_HYBRID,
	CINTIQ_COMPANION_2,
	CINTIQ,
	WACOM_BEE,
	WACOM_13HD,
	WACOM_MO,
	BAMBOO_PEN,
	INTUOSHT,
	INTUOSHT2,
	BAMBOO_TOUCH,
	BAMBOO_PT,
	WACOM_24HDT,
	WACOM_27QHDT,
	BAMBOO_PAD,
	WIRELESS,
	REMOTE,
	TABLETPC,   /* add new TPC below */
	TABLETPCE,
	TABLETPC2FG,
	MTSCREEN,
	MTTPC,
	MTTPC_B,
	HID_GENERIC,
	MAX_TYPE
};

struct wacom_features {
	const char *name;
	int x_max;
	int y_max;
	int pressure_max;
	int distance_max;
	int type;
	int x_resolution;
	int y_resolution;
	int numbered_buttons;
	int offset_left;
	int offset_right;
	int offset_top;
	int offset_bottom;
	int device_type;
	int x_phy;
	int y_phy;
	unsigned unit;
	int unitExpo;
	int x_fuzz;
	int y_fuzz;
	int pressure_fuzz;
	int distance_fuzz;
	int tilt_fuzz;
	unsigned quirks;
	unsigned touch_max;
	int oVid;
	int oPid;
	int pktlen;
	bool check_for_hid_type;
	int hid_type;
};

struct wacom_shared {
	bool stylus_in_proximity;
	bool touch_down;
	/* for wireless device to access USB interfaces */
	unsigned touch_max;
	int type;
	struct input_dev *touch_input;
	struct hid_device *pen;
	struct hid_device *touch;
	bool has_mute_touch_switch;
	bool is_touch_on;
};

struct hid_data {
	__s16 inputmode;	/* InputMode HID feature, -1 if non-existent */
	__s16 inputmode_index;	/* InputMode HID feature index in the report */
	bool sense_state;
	bool inrange_state;
	bool invert_state;
	bool tipswitch;
	bool barrelswitch;
	bool barrelswitch2;
	int x;
	int y;
	int pressure;
	int width;
	int height;
	int id;
	int cc_report;
	int cc_index;
	int cc_value_index;
	int last_slot_field;
	int num_expected;
	int num_received;
	int bat_status;
	int battery_capacity;
	int bat_charging;
	int bat_connected;
	int ps_connected;
	bool pad_input_event_flag;
};

struct wacom_remote_data {
	struct {
		u32 serial;
		bool connected;
	} remote[WACOM_MAX_REMOTES];
};

struct wacom_wac {
	char name[WACOM_NAME_MAX];
	char pen_name[WACOM_NAME_MAX];
	char touch_name[WACOM_NAME_MAX];
	char pad_name[WACOM_NAME_MAX];
	unsigned char data[WACOM_PKGLEN_MAX];
	int tool[2];
	int id[2];
	__u64 serial[2];
	bool reporting_data;
	struct wacom_features features;
	struct wacom_shared *shared;
	struct input_dev *pen_input;
	struct input_dev *touch_input;
	struct input_dev *pad_input;
	struct kfifo_rec_ptr_2 pen_fifo;
	int pid;
	int num_contacts_left;
	u8 bt_features;
	u8 bt_high_speed;
	int mode_report;
	int mode_value;
	struct hid_data hid_data;
	bool has_mute_touch_switch;
	bool has_mode_change;
	bool is_direct_mode;
	bool is_invalid_bt_frame;
};

#endif
