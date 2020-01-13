// SPDX-License-Identifier: GPL-2.0-only
/*
 *  HID driver for Logitech Unifying receivers
 *
 *  Copyright (c) 2011 Logitech
 */



#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/kfifo.h>
#include <linux/delay.h>
#include <linux/usb.h> /* For to_usb_interface for kvm extra intf check */
#include <asm/unaligned.h>
#include "hid-ids.h"

#define DJ_MAX_PAIRED_DEVICES			6
#define DJ_MAX_NUMBER_NOTIFS			8
#define DJ_RECEIVER_INDEX			0
#define DJ_DEVICE_INDEX_MIN			1
#define DJ_DEVICE_INDEX_MAX			6

#define DJREPORT_SHORT_LENGTH			15
#define DJREPORT_LONG_LENGTH			32

#define REPORT_ID_DJ_SHORT			0x20
#define REPORT_ID_DJ_LONG			0x21

#define REPORT_ID_HIDPP_SHORT			0x10
#define REPORT_ID_HIDPP_LONG			0x11
#define REPORT_ID_HIDPP_VERY_LONG		0x12

#define HIDPP_REPORT_SHORT_LENGTH		7
#define HIDPP_REPORT_LONG_LENGTH		20

#define HIDPP_RECEIVER_INDEX			0xff

#define REPORT_TYPE_RFREPORT_FIRST		0x01
#define REPORT_TYPE_RFREPORT_LAST		0x1F

/* Command Switch to DJ mode */
#define REPORT_TYPE_CMD_SWITCH			0x80
#define CMD_SWITCH_PARAM_DEVBITFIELD		0x00
#define CMD_SWITCH_PARAM_TIMEOUT_SECONDS	0x01
#define TIMEOUT_NO_KEEPALIVE			0x00

/* Command to Get the list of Paired devices */
#define REPORT_TYPE_CMD_GET_PAIRED_DEVICES	0x81

/* Device Paired Notification */
#define REPORT_TYPE_NOTIF_DEVICE_PAIRED		0x41
#define SPFUNCTION_MORE_NOTIF_EXPECTED		0x01
#define SPFUNCTION_DEVICE_LIST_EMPTY		0x02
#define DEVICE_PAIRED_PARAM_SPFUNCTION		0x00
#define DEVICE_PAIRED_PARAM_EQUAD_ID_LSB	0x01
#define DEVICE_PAIRED_PARAM_EQUAD_ID_MSB	0x02
#define DEVICE_PAIRED_RF_REPORT_TYPE		0x03

/* Device Un-Paired Notification */
#define REPORT_TYPE_NOTIF_DEVICE_UNPAIRED	0x40

/* Connection Status Notification */
#define REPORT_TYPE_NOTIF_CONNECTION_STATUS	0x42
#define CONNECTION_STATUS_PARAM_STATUS		0x00
#define STATUS_LINKLOSS				0x01

/* Error Notification */
#define REPORT_TYPE_NOTIF_ERROR			0x7F
#define NOTIF_ERROR_PARAM_ETYPE			0x00
#define ETYPE_KEEPALIVE_TIMEOUT			0x01

/* supported DJ HID && RF report types */
#define REPORT_TYPE_KEYBOARD			0x01
#define REPORT_TYPE_MOUSE			0x02
#define REPORT_TYPE_CONSUMER_CONTROL		0x03
#define REPORT_TYPE_SYSTEM_CONTROL		0x04
#define REPORT_TYPE_MEDIA_CENTER		0x08
#define REPORT_TYPE_LEDS			0x0E

/* RF Report types bitfield */
#define STD_KEYBOARD				BIT(1)
#define STD_MOUSE				BIT(2)
#define MULTIMEDIA				BIT(3)
#define POWER_KEYS				BIT(4)
#define MEDIA_CENTER				BIT(8)
#define KBD_LEDS				BIT(14)
/* Fake (bitnr > NUMBER_OF_HID_REPORTS) bit to track HID++ capability */
#define HIDPP					BIT_ULL(63)

/* HID++ Device Connected Notification */
#define REPORT_TYPE_NOTIF_DEVICE_CONNECTED	0x41
#define HIDPP_PARAM_PROTO_TYPE			0x00
#define HIDPP_PARAM_DEVICE_INFO			0x01
#define HIDPP_PARAM_EQUAD_LSB			0x02
#define HIDPP_PARAM_EQUAD_MSB			0x03
#define HIDPP_PARAM_27MHZ_DEVID			0x03
#define HIDPP_DEVICE_TYPE_MASK			GENMASK(3, 0)
#define HIDPP_LINK_STATUS_MASK			BIT(6)
#define HIDPP_MANUFACTURER_MASK			BIT(7)

#define HIDPP_DEVICE_TYPE_KEYBOARD		1
#define HIDPP_DEVICE_TYPE_MOUSE			2

#define HIDPP_SET_REGISTER			0x80
#define HIDPP_GET_LONG_REGISTER			0x83
#define HIDPP_REG_CONNECTION_STATE		0x02
#define HIDPP_REG_PAIRING_INFORMATION		0xB5
#define HIDPP_PAIRING_INFORMATION		0x20
#define HIDPP_FAKE_DEVICE_ARRIVAL		0x02

enum recvr_type {
	recvr_type_dj,
	recvr_type_hidpp,
	recvr_type_gaming_hidpp,
	recvr_type_mouse_only,
	recvr_type_27mhz,
	recvr_type_bluetooth,
};

struct dj_report {
	u8 report_id;
	u8 device_index;
	u8 report_type;
	u8 report_params[DJREPORT_SHORT_LENGTH - 3];
};

struct hidpp_event {
	u8 report_id;
	u8 device_index;
	u8 sub_id;
	u8 params[HIDPP_REPORT_LONG_LENGTH - 3U];
} __packed;

struct dj_receiver_dev {
	struct hid_device *mouse;
	struct hid_device *keyboard;
	struct hid_device *hidpp;
	struct dj_device *paired_dj_devices[DJ_MAX_PAIRED_DEVICES +
					    DJ_DEVICE_INDEX_MIN];
	struct list_head list;
	struct kref kref;
	struct work_struct work;
	struct kfifo notif_fifo;
	unsigned long last_query; /* in jiffies */
	bool ready;
	enum recvr_type type;
	unsigned int unnumbered_application;
	spinlock_t lock;
};

struct dj_device {
	struct hid_device *hdev;
	struct dj_receiver_dev *dj_receiver_dev;
	u64 reports_supported;
	u8 device_index;
};

#define WORKITEM_TYPE_EMPTY	0
#define WORKITEM_TYPE_PAIRED	1
#define WORKITEM_TYPE_UNPAIRED	2
#define WORKITEM_TYPE_UNKNOWN	255

struct dj_workitem {
	u8 type;		/* WORKITEM_TYPE_* */
	u8 device_index;
	u8 device_type;
	u8 quad_id_msb;
	u8 quad_id_lsb;
	u64 reports_supported;
};

/* Keyboard descriptor (1) */
static const char kbd_descriptor[] = {
	0x05, 0x01,		/* USAGE_PAGE (generic Desktop)     */
	0x09, 0x06,		/* USAGE (Keyboard)         */
	0xA1, 0x01,		/* COLLECTION (Application)     */
	0x85, 0x01,		/* REPORT_ID (1)            */
	0x95, 0x08,		/*   REPORT_COUNT (8)           */
	0x75, 0x01,		/*   REPORT_SIZE (1)            */
	0x15, 0x00,		/*   LOGICAL_MINIMUM (0)        */
	0x25, 0x01,		/*   LOGICAL_MAXIMUM (1)        */
	0x05, 0x07,		/*   USAGE_PAGE (Keyboard)      */
	0x19, 0xE0,		/*   USAGE_MINIMUM (Left Control)   */
	0x29, 0xE7,		/*   USAGE_MAXIMUM (Right GUI)      */
	0x81, 0x02,		/*   INPUT (Data,Var,Abs)       */
	0x95, 0x06,		/*   REPORT_COUNT (6)           */
	0x75, 0x08,		/*   REPORT_SIZE (8)            */
	0x15, 0x00,		/*   LOGICAL_MINIMUM (0)        */
	0x26, 0xFF, 0x00,	/*   LOGICAL_MAXIMUM (255)      */
	0x05, 0x07,		/*   USAGE_PAGE (Keyboard)      */
	0x19, 0x00,		/*   USAGE_MINIMUM (no event)       */
	0x2A, 0xFF, 0x00,	/*   USAGE_MAXIMUM (reserved)       */
	0x81, 0x00,		/*   INPUT (Data,Ary,Abs)       */
	0x85, 0x0e,		/* REPORT_ID (14)               */
	0x05, 0x08,		/*   USAGE PAGE (LED page)      */
	0x95, 0x05,		/*   REPORT COUNT (5)           */
	0x75, 0x01,		/*   REPORT SIZE (1)            */
	0x15, 0x00,		/*   LOGICAL_MINIMUM (0)        */
	0x25, 0x01,		/*   LOGICAL_MAXIMUM (1)        */
	0x19, 0x01,		/*   USAGE MINIMUM (1)          */
	0x29, 0x05,		/*   USAGE MAXIMUM (5)          */
	0x91, 0x02,		/*   OUTPUT (Data, Variable, Absolute)  */
	0x95, 0x01,		/*   REPORT COUNT (1)           */
	0x75, 0x03,		/*   REPORT SIZE (3)            */
	0x91, 0x01,		/*   OUTPUT (Constant)          */
	0xC0
};

/* Mouse descriptor (2)     */
static const char mse_descriptor[] = {
	0x05, 0x01,		/*  USAGE_PAGE (Generic Desktop)        */
	0x09, 0x02,		/*  USAGE (Mouse)                       */
	0xA1, 0x01,		/*  COLLECTION (Application)            */
	0x85, 0x02,		/*    REPORT_ID = 2                     */
	0x09, 0x01,		/*    USAGE (pointer)                   */
	0xA1, 0x00,		/*    COLLECTION (physical)             */
	0x05, 0x09,		/*      USAGE_PAGE (buttons)            */
	0x19, 0x01,		/*      USAGE_MIN (1)                   */
	0x29, 0x10,		/*      USAGE_MAX (16)                  */
	0x15, 0x00,		/*      LOGICAL_MIN (0)                 */
	0x25, 0x01,		/*      LOGICAL_MAX (1)                 */
	0x95, 0x10,		/*      REPORT_COUNT (16)               */
	0x75, 0x01,		/*      REPORT_SIZE (1)                 */
	0x81, 0x02,		/*      INPUT (data var abs)            */
	0x05, 0x01,		/*      USAGE_PAGE (generic desktop)    */
	0x16, 0x01, 0xF8,	/*      LOGICAL_MIN (-2047)             */
	0x26, 0xFF, 0x07,	/*      LOGICAL_MAX (2047)              */
	0x75, 0x0C,		/*      REPORT_SIZE (12)                */
	0x95, 0x02,		/*      REPORT_COUNT (2)                */
	0x09, 0x30,		/*      USAGE (X)                       */
	0x09, 0x31,		/*      USAGE (Y)                       */
	0x81, 0x06,		/*      INPUT                           */
	0x15, 0x81,		/*      LOGICAL_MIN (-127)              */
	0x25, 0x7F,		/*      LOGICAL_MAX (127)               */
	0x75, 0x08,		/*      REPORT_SIZE (8)                 */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x09, 0x38,		/*      USAGE (wheel)                   */
	0x81, 0x06,		/*      INPUT                           */
	0x05, 0x0C,		/*      USAGE_PAGE(consumer)            */
	0x0A, 0x38, 0x02,	/*      USAGE(AC Pan)                   */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x81, 0x06,		/*      INPUT                           */
	0xC0,			/*    END_COLLECTION                    */
	0xC0,			/*  END_COLLECTION                      */
};

/* Mouse descriptor (2) for 27 MHz receiver, only 8 buttons */
static const char mse_27mhz_descriptor[] = {
	0x05, 0x01,		/*  USAGE_PAGE (Generic Desktop)        */
	0x09, 0x02,		/*  USAGE (Mouse)                       */
	0xA1, 0x01,		/*  COLLECTION (Application)            */
	0x85, 0x02,		/*    REPORT_ID = 2                     */
	0x09, 0x01,		/*    USAGE (pointer)                   */
	0xA1, 0x00,		/*    COLLECTION (physical)             */
	0x05, 0x09,		/*      USAGE_PAGE (buttons)            */
	0x19, 0x01,		/*      USAGE_MIN (1)                   */
	0x29, 0x08,		/*      USAGE_MAX (8)                   */
	0x15, 0x00,		/*      LOGICAL_MIN (0)                 */
	0x25, 0x01,		/*      LOGICAL_MAX (1)                 */
	0x95, 0x08,		/*      REPORT_COUNT (8)                */
	0x75, 0x01,		/*      REPORT_SIZE (1)                 */
	0x81, 0x02,		/*      INPUT (data var abs)            */
	0x05, 0x01,		/*      USAGE_PAGE (generic desktop)    */
	0x16, 0x01, 0xF8,	/*      LOGICAL_MIN (-2047)             */
	0x26, 0xFF, 0x07,	/*      LOGICAL_MAX (2047)              */
	0x75, 0x0C,		/*      REPORT_SIZE (12)                */
	0x95, 0x02,		/*      REPORT_COUNT (2)                */
	0x09, 0x30,		/*      USAGE (X)                       */
	0x09, 0x31,		/*      USAGE (Y)                       */
	0x81, 0x06,		/*      INPUT                           */
	0x15, 0x81,		/*      LOGICAL_MIN (-127)              */
	0x25, 0x7F,		/*      LOGICAL_MAX (127)               */
	0x75, 0x08,		/*      REPORT_SIZE (8)                 */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x09, 0x38,		/*      USAGE (wheel)                   */
	0x81, 0x06,		/*      INPUT                           */
	0x05, 0x0C,		/*      USAGE_PAGE(consumer)            */
	0x0A, 0x38, 0x02,	/*      USAGE(AC Pan)                   */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x81, 0x06,		/*      INPUT                           */
	0xC0,			/*    END_COLLECTION                    */
	0xC0,			/*  END_COLLECTION                      */
};

/* Mouse descriptor (2) for Bluetooth receiver, low-res hwheel, 12 buttons */
static const char mse_bluetooth_descriptor[] = {
	0x05, 0x01,		/*  USAGE_PAGE (Generic Desktop)        */
	0x09, 0x02,		/*  USAGE (Mouse)                       */
	0xA1, 0x01,		/*  COLLECTION (Application)            */
	0x85, 0x02,		/*    REPORT_ID = 2                     */
	0x09, 0x01,		/*    USAGE (pointer)                   */
	0xA1, 0x00,		/*    COLLECTION (physical)             */
	0x05, 0x09,		/*      USAGE_PAGE (buttons)            */
	0x19, 0x01,		/*      USAGE_MIN (1)                   */
	0x29, 0x08,		/*      USAGE_MAX (8)                   */
	0x15, 0x00,		/*      LOGICAL_MIN (0)                 */
	0x25, 0x01,		/*      LOGICAL_MAX (1)                 */
	0x95, 0x08,		/*      REPORT_COUNT (8)                */
	0x75, 0x01,		/*      REPORT_SIZE (1)                 */
	0x81, 0x02,		/*      INPUT (data var abs)            */
	0x05, 0x01,		/*      USAGE_PAGE (generic desktop)    */
	0x16, 0x01, 0xF8,	/*      LOGICAL_MIN (-2047)             */
	0x26, 0xFF, 0x07,	/*      LOGICAL_MAX (2047)              */
	0x75, 0x0C,		/*      REPORT_SIZE (12)                */
	0x95, 0x02,		/*      REPORT_COUNT (2)                */
	0x09, 0x30,		/*      USAGE (X)                       */
	0x09, 0x31,		/*      USAGE (Y)                       */
	0x81, 0x06,		/*      INPUT                           */
	0x15, 0x81,		/*      LOGICAL_MIN (-127)              */
	0x25, 0x7F,		/*      LOGICAL_MAX (127)               */
	0x75, 0x08,		/*      REPORT_SIZE (8)                 */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x09, 0x38,		/*      USAGE (wheel)                   */
	0x81, 0x06,		/*      INPUT                           */
	0x05, 0x0C,		/*      USAGE_PAGE(consumer)            */
	0x0A, 0x38, 0x02,	/*      USAGE(AC Pan)                   */
	0x15, 0xF9,		/*      LOGICAL_MIN (-7)                */
	0x25, 0x07,		/*      LOGICAL_MAX (7)                 */
	0x75, 0x04,		/*      REPORT_SIZE (4)                 */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x81, 0x06,		/*      INPUT                           */
	0x05, 0x09,		/*      USAGE_PAGE (buttons)            */
	0x19, 0x09,		/*      USAGE_MIN (9)                   */
	0x29, 0x0C,		/*      USAGE_MAX (12)                  */
	0x15, 0x00,		/*      LOGICAL_MIN (0)                 */
	0x25, 0x01,		/*      LOGICAL_MAX (1)                 */
	0x75, 0x01,		/*      REPORT_SIZE (1)                 */
	0x95, 0x04,		/*      REPORT_COUNT (4)                */
	0x81, 0x06,		/*      INPUT                           */
	0xC0,			/*    END_COLLECTION                    */
	0xC0,			/*  END_COLLECTION                      */
};

/* Gaming Mouse descriptor (2) */
static const char mse_high_res_descriptor[] = {
	0x05, 0x01,		/*  USAGE_PAGE (Generic Desktop)        */
	0x09, 0x02,		/*  USAGE (Mouse)                       */
	0xA1, 0x01,		/*  COLLECTION (Application)            */
	0x85, 0x02,		/*    REPORT_ID = 2                     */
	0x09, 0x01,		/*    USAGE (pointer)                   */
	0xA1, 0x00,		/*    COLLECTION (physical)             */
	0x05, 0x09,		/*      USAGE_PAGE (buttons)            */
	0x19, 0x01,		/*      USAGE_MIN (1)                   */
	0x29, 0x10,		/*      USAGE_MAX (16)                  */
	0x15, 0x00,		/*      LOGICAL_MIN (0)                 */
	0x25, 0x01,		/*      LOGICAL_MAX (1)                 */
	0x95, 0x10,		/*      REPORT_COUNT (16)               */
	0x75, 0x01,		/*      REPORT_SIZE (1)                 */
	0x81, 0x02,		/*      INPUT (data var abs)            */
	0x05, 0x01,		/*      USAGE_PAGE (generic desktop)    */
	0x16, 0x01, 0x80,	/*      LOGICAL_MIN (-32767)            */
	0x26, 0xFF, 0x7F,	/*      LOGICAL_MAX (32767)             */
	0x75, 0x10,		/*      REPORT_SIZE (16)                */
	0x95, 0x02,		/*      REPORT_COUNT (2)                */
	0x09, 0x30,		/*      USAGE (X)                       */
	0x09, 0x31,		/*      USAGE (Y)                       */
	0x81, 0x06,		/*      INPUT                           */
	0x15, 0x81,		/*      LOGICAL_MIN (-127)              */
	0x25, 0x7F,		/*      LOGICAL_MAX (127)               */
	0x75, 0x08,		/*      REPORT_SIZE (8)                 */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x09, 0x38,		/*      USAGE (wheel)                   */
	0x81, 0x06,		/*      INPUT                           */
	0x05, 0x0C,		/*      USAGE_PAGE(consumer)            */
	0x0A, 0x38, 0x02,	/*      USAGE(AC Pan)                   */
	0x95, 0x01,		/*      REPORT_COUNT (1)                */
	0x81, 0x06,		/*      INPUT                           */
	0xC0,			/*    END_COLLECTION                    */
	0xC0,			/*  END_COLLECTION                      */
};

/* Consumer Control descriptor (3) */
static const char consumer_descriptor[] = {
	0x05, 0x0C,		/* USAGE_PAGE (Consumer Devices)       */
	0x09, 0x01,		/* USAGE (Consumer Control)            */
	0xA1, 0x01,		/* COLLECTION (Application)            */
	0x85, 0x03,		/* REPORT_ID = 3                       */
	0x75, 0x10,		/* REPORT_SIZE (16)                    */
	0x95, 0x02,		/* REPORT_COUNT (2)                    */
	0x15, 0x01,		/* LOGICAL_MIN (1)                     */
	0x26, 0xFF, 0x02,	/* LOGICAL_MAX (767)                   */
	0x19, 0x01,		/* USAGE_MIN (1)                       */
	0x2A, 0xFF, 0x02,	/* USAGE_MAX (767)                     */
	0x81, 0x00,		/* INPUT (Data Ary Abs)                */
	0xC0,			/* END_COLLECTION                      */
};				/*                                     */

/* System control descriptor (4) */
static const char syscontrol_descriptor[] = {
	0x05, 0x01,		/*   USAGE_PAGE (Generic Desktop)      */
	0x09, 0x80,		/*   USAGE (System Control)            */
	0xA1, 0x01,		/*   COLLECTION (Application)          */
	0x85, 0x04,		/*   REPORT_ID = 4                     */
	0x75, 0x02,		/*   REPORT_SIZE (2)                   */
	0x95, 0x01,		/*   REPORT_COUNT (1)                  */
	0x15, 0x01,		/*   LOGICAL_MIN (1)                   */
	0x25, 0x03,		/*   LOGICAL_MAX (3)                   */
	0x09, 0x82,		/*   USAGE (System Sleep)              */
	0x09, 0x81,		/*   USAGE (System Power Down)         */
	0x09, 0x83,		/*   USAGE (System Wake Up)            */
	0x81, 0x60,		/*   INPUT (Data Ary Abs NPrf Null)    */
	0x75, 0x06,		/*   REPORT_SIZE (6)                   */
	0x81, 0x03,		/*   INPUT (Cnst Var Abs)              */
	0xC0,			/*   END_COLLECTION                    */
};

/* Media descriptor (8) */
static const char media_descriptor[] = {
	0x06, 0xbc, 0xff,	/* Usage Page 0xffbc                   */
	0x09, 0x88,		/* Usage 0x0088                        */
	0xa1, 0x01,		/* BeginCollection                     */
	0x85, 0x08,		/*   Report ID 8                       */
	0x19, 0x01,		/*   Usage Min 0x0001                  */
	0x29, 0xff,		/*   Usage Max 0x00ff                  */
	0x15, 0x01,		/*   Logical Min 1                     */
	0x26, 0xff, 0x00,	/*   Logical Max 255                   */
	0x75, 0x08,		/*   Report Size 8                     */
	0x95, 0x01,		/*   Report Count 1                    */
	0x81, 0x00,		/*   Input                             */
	0xc0,			/* EndCollection                       */
};				/*                                     */

/* HIDPP descriptor */
static const char hidpp_descriptor[] = {
	0x06, 0x00, 0xff,	/* Usage Page (Vendor Defined Page 1)  */
	0x09, 0x01,		/* Usage (Vendor Usage 1)              */
	0xa1, 0x01,		/* Collection (Application)            */
	0x85, 0x10,		/*   Report ID (16)                    */
	0x75, 0x08,		/*   Report Size (8)                   */
	0x95, 0x06,		/*   Report Count (6)                  */
	0x15, 0x00,		/*   Logical Minimum (0)               */
	0x26, 0xff, 0x00,	/*   Logical Maximum (255)             */
	0x09, 0x01,		/*   Usage (Vendor Usage 1)            */
	0x81, 0x00,		/*   Input (Data,Arr,Abs)              */
	0x09, 0x01,		/*   Usage (Vendor Usage 1)            */
	0x91, 0x00,		/*   Output (Data,Arr,Abs)             */
	0xc0,			/* End Collection                      */
	0x06, 0x00, 0xff,	/* Usage Page (Vendor Defined Page 1)  */
	0x09, 0x02,		/* Usage (Vendor Usage 2)              */
	0xa1, 0x01,		/* Collection (Application)            */
	0x85, 0x11,		/*   Report ID (17)                    */
	0x75, 0x08,		/*   Report Size (8)                   */
	0x95, 0x13,		/*   Report Count (19)                 */
	0x15, 0x00,		/*   Logical Minimum (0)               */
	0x26, 0xff, 0x00,	/*   Logical Maximum (255)             */
	0x09, 0x02,		/*   Usage (Vendor Usage 2)            */
	0x81, 0x00,		/*   Input (Data,Arr,Abs)              */
	0x09, 0x02,		/*   Usage (Vendor Usage 2)            */
	0x91, 0x00,		/*   Output (Data,Arr,Abs)             */
	0xc0,			/* End Collection                      */
	0x06, 0x00, 0xff,	/* Usage Page (Vendor Defined Page 1)  */
	0x09, 0x04,		/* Usage (Vendor Usage 0x04)           */
	0xa1, 0x01,		/* Collection (Application)            */
	0x85, 0x20,		/*   Report ID (32)                    */
	0x75, 0x08,		/*   Report Size (8)                   */
	0x95, 0x0e,		/*   Report Count (14)                 */
	0x15, 0x00,		/*   Logical Minimum (0)               */
	0x26, 0xff, 0x00,	/*   Logical Maximum (255)             */
	0x09, 0x41,		/*   Usage (Vendor Usage 0x41)         */
	0x81, 0x00,		/*   Input (Data,Arr,Abs)              */
	0x09, 0x41,		/*   Usage (Vendor Usage 0x41)         */
	0x91, 0x00,		/*   Output (Data,Arr,Abs)             */
	0x85, 0x21,		/*   Report ID (33)                    */
	0x95, 0x1f,		/*   Report Count (31)                 */
	0x15, 0x00,		/*   Logical Minimum (0)               */
	0x26, 0xff, 0x00,	/*   Logical Maximum (255)             */
	0x09, 0x42,		/*   Usage (Vendor Usage 0x42)         */
	0x81, 0x00,		/*   Input (Data,Arr,Abs)              */
	0x09, 0x42,		/*   Usage (Vendor Usage 0x42)         */
	0x91, 0x00,		/*   Output (Data,Arr,Abs)             */
	0xc0,			/* End Collection                      */
};

/* Maximum size of all defined hid reports in bytes (including report id) */
#define MAX_REPORT_SIZE 8

/* Make sure all descriptors are present here */
#define MAX_RDESC_SIZE				\
	(sizeof(kbd_descriptor) +		\
	 sizeof(mse_bluetooth_descriptor) +	\
	 sizeof(consumer_descriptor) +		\
	 sizeof(syscontrol_descriptor) +	\
	 sizeof(media_descriptor) +	\
	 sizeof(hidpp_descriptor))

/* Number of possible hid report types that can be created by this driver.
 *
 * Right now, RF report types have the same report types (or report id's)
 * than the hid report created from those RF reports. In the future
 * this doesnt have to be true.
 *
 * For instance, RF report type 0x01 which has a size of 8 bytes, corresponds
 * to hid report id 0x01, this is standard keyboard. Same thing applies to mice
 * reports and consumer control, etc. If a new RF report is created, it doesn't
 * has to have the same report id as its corresponding hid report, so an
 * translation may have to take place for future report types.
 */
#define NUMBER_OF_HID_REPORTS 32
static const u8 hid_reportid_size_map[NUMBER_OF_HID_REPORTS] = {
	[1] = 8,		/* Standard keyboard */
	[2] = 8,		/* Standard mouse */
	[3] = 5,		/* Consumer control */
	[4] = 2,		/* System control */
	[8] = 2,		/* Media Center */
};


#define LOGITECH_DJ_INTERFACE_NUMBER 0x02

static struct hid_ll_driver logi_dj_ll_driver;

static int logi_dj_recv_query_paired_devices(struct dj_receiver_dev *djrcv_dev);
static void delayedwork_callback(struct work_struct *work);

static LIST_HEAD(dj_hdev_list);
static DEFINE_MUTEX(dj_hdev_list_lock);

/*
 * dj/HID++ receivers are really a single logical entity, but for BIOS/Windows
 * compatibility they have multiple USB interfaces. On HID++ receivers we need
 * to listen for input reports on both interfaces. The functions below are used
 * to create a single struct dj_receiver_dev for all interfaces belonging to
 * a single USB-device / receiver.
 */
static struct dj_receiver_dev *dj_find_receiver_dev(struct hid_device *hdev,
						    enum recvr_type type)
{
	struct dj_receiver_dev *djrcv_dev;
	char sep;

	/*
	 * The bluetooth receiver contains a built-in hub and has separate
	 * USB-devices for the keyboard and mouse interfaces.
	 */
	sep = (type == recvr_type_bluetooth) ? '.' : '/';

	/* Try to find an already-probed interface from the same device */
	list_for_each_entry(djrcv_dev, &dj_hdev_list, list) {
		if (djrcv_dev->mouse &&
		    hid_compare_device_paths(hdev, djrcv_dev->mouse, sep)) {
			kref_get(&djrcv_dev->kref);
			return djrcv_dev;
		}
		if (djrcv_dev->keyboard &&
		    hid_compare_device_paths(hdev, djrcv_dev->keyboard, sep)) {
			kref_get(&djrcv_dev->kref);
			return djrcv_dev;
		}
		if (djrcv_dev->hidpp &&
		    hid_compare_device_paths(hdev, djrcv_dev->hidpp, sep)) {
			kref_get(&djrcv_dev->kref);
			return djrcv_dev;
		}
	}

	return NULL;
}

static void dj_release_receiver_dev(struct kref *kref)
{
	struct dj_receiver_dev *djrcv_dev = container_of(kref, struct dj_receiver_dev, kref);

	list_del(&djrcv_dev->list);
	kfifo_free(&djrcv_dev->notif_fifo);
	kfree(djrcv_dev);
}

static void dj_put_receiver_dev(struct hid_device *hdev)
{
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);

	mutex_lock(&dj_hdev_list_lock);

	if (djrcv_dev->mouse == hdev)
		djrcv_dev->mouse = NULL;
	if (djrcv_dev->keyboard == hdev)
		djrcv_dev->keyboard = NULL;
	if (djrcv_dev->hidpp == hdev)
		djrcv_dev->hidpp = NULL;

	kref_put(&djrcv_dev->kref, dj_release_receiver_dev);

	mutex_unlock(&dj_hdev_list_lock);
}

static struct dj_receiver_dev *dj_get_receiver_dev(struct hid_device *hdev,
						   enum recvr_type type,
						   unsigned int application,
						   bool is_hidpp)
{
	struct dj_receiver_dev *djrcv_dev;

	mutex_lock(&dj_hdev_list_lock);

	djrcv_dev = dj_find_receiver_dev(hdev, type);
	if (!djrcv_dev) {
		djrcv_dev = kzalloc(sizeof(*djrcv_dev), GFP_KERNEL);
		if (!djrcv_dev)
			goto out;

		INIT_WORK(&djrcv_dev->work, delayedwork_callback);
		spin_lock_init(&djrcv_dev->lock);
		if (kfifo_alloc(&djrcv_dev->notif_fifo,
			    DJ_MAX_NUMBER_NOTIFS * sizeof(struct dj_workitem),
			    GFP_KERNEL)) {
			kfree(djrcv_dev);
			djrcv_dev = NULL;
			goto out;
		}
		kref_init(&djrcv_dev->kref);
		list_add_tail(&djrcv_dev->list, &dj_hdev_list);
		djrcv_dev->last_query = jiffies;
		djrcv_dev->type = type;
	}

	if (application == HID_GD_KEYBOARD)
		djrcv_dev->keyboard = hdev;
	if (application == HID_GD_MOUSE)
		djrcv_dev->mouse = hdev;
	if (is_hidpp)
		djrcv_dev->hidpp = hdev;

	hid_set_drvdata(hdev, djrcv_dev);
out:
	mutex_unlock(&dj_hdev_list_lock);
	return djrcv_dev;
}

static void logi_dj_recv_destroy_djhid_device(struct dj_receiver_dev *djrcv_dev,
					      struct dj_workitem *workitem)
{
	/* Called in delayed work context */
	struct dj_device *dj_dev;
	unsigned long flags;

	spin_lock_irqsave(&djrcv_dev->lock, flags);
	dj_dev = djrcv_dev->paired_dj_devices[workitem->device_index];
	djrcv_dev->paired_dj_devices[workitem->device_index] = NULL;
	spin_unlock_irqrestore(&djrcv_dev->lock, flags);

	if (dj_dev != NULL) {
		hid_destroy_device(dj_dev->hdev);
		kfree(dj_dev);
	} else {
		hid_err(djrcv_dev->hidpp, "%s: can't destroy a NULL device\n",
			__func__);
	}
}

static void logi_dj_recv_add_djhid_device(struct dj_receiver_dev *djrcv_dev,
					  struct dj_workitem *workitem)
{
	/* Called in delayed work context */
	struct hid_device *djrcv_hdev = djrcv_dev->hidpp;
	struct hid_device *dj_hiddev;
	struct dj_device *dj_dev;
	u8 device_index = workitem->device_index;
	unsigned long flags;

	/* Device index goes from 1 to 6, we need 3 bytes to store the
	 * semicolon, the index, and a null terminator
	 */
	unsigned char tmpstr[3];

	/* We are the only one ever adding a device, no need to lock */
	if (djrcv_dev->paired_dj_devices[device_index]) {
		/* The device is already known. No need to reallocate it. */
		dbg_hid("%s: device is already known\n", __func__);
		return;
	}

	dj_hiddev = hid_allocate_device();
	if (IS_ERR(dj_hiddev)) {
		hid_err(djrcv_hdev, "%s: hid_allocate_dev failed\n", __func__);
		return;
	}

	dj_hiddev->ll_driver = &logi_dj_ll_driver;

	dj_hiddev->dev.parent = &djrcv_hdev->dev;
	dj_hiddev->bus = BUS_USB;
	dj_hiddev->vendor = djrcv_hdev->vendor;
	dj_hiddev->product = (workitem->quad_id_msb << 8) |
			      workitem->quad_id_lsb;
	if (workitem->device_type) {
		const char *type_str = "Device";

		switch (workitem->device_type) {
		case 0x01: type_str = "Keyboard";	break;
		case 0x02: type_str = "Mouse";		break;
		case 0x03: type_str = "Numpad";		break;
		case 0x04: type_str = "Presenter";	break;
		case 0x07: type_str = "Remote Control";	break;
		case 0x08: type_str = "Trackball";	break;
		case 0x09: type_str = "Touchpad";	break;
		}
		snprintf(dj_hiddev->name, sizeof(dj_hiddev->name),
			"Logitech Wireless %s PID:%04x",
			type_str, dj_hiddev->product);
	} else {
		snprintf(dj_hiddev->name, sizeof(dj_hiddev->name),
			"Logitech Unifying Device. Wireless PID:%04x",
			dj_hiddev->product);
	}

	if (djrcv_dev->type == recvr_type_27mhz)
		dj_hiddev->group = HID_GROUP_LOGITECH_27MHZ_DEVICE;
	else
		dj_hiddev->group = HID_GROUP_LOGITECH_DJ_DEVICE;

	memcpy(dj_hiddev->phys, djrcv_hdev->phys, sizeof(djrcv_hdev->phys));
	snprintf(tmpstr, sizeof(tmpstr), ":%d", device_index);
	strlcat(dj_hiddev->phys, tmpstr, sizeof(dj_hiddev->phys));

	dj_dev = kzalloc(sizeof(struct dj_device), GFP_KERNEL);

	if (!dj_dev) {
		hid_err(djrcv_hdev, "%s: failed allocating dj_dev\n", __func__);
		goto dj_device_allocate_fail;
	}

	dj_dev->reports_supported = workitem->reports_supported;
	dj_dev->hdev = dj_hiddev;
	dj_dev->dj_receiver_dev = djrcv_dev;
	dj_dev->device_index = device_index;
	dj_hiddev->driver_data = dj_dev;

	spin_lock_irqsave(&djrcv_dev->lock, flags);
	djrcv_dev->paired_dj_devices[device_index] = dj_dev;
	spin_unlock_irqrestore(&djrcv_dev->lock, flags);

	if (hid_add_device(dj_hiddev)) {
		hid_err(djrcv_hdev, "%s: failed adding dj_device\n", __func__);
		goto hid_add_device_fail;
	}

	return;

hid_add_device_fail:
	spin_lock_irqsave(&djrcv_dev->lock, flags);
	djrcv_dev->paired_dj_devices[device_index] = NULL;
	spin_unlock_irqrestore(&djrcv_dev->lock, flags);
	kfree(dj_dev);
dj_device_allocate_fail:
	hid_destroy_device(dj_hiddev);
}

static void delayedwork_callback(struct work_struct *work)
{
	struct dj_receiver_dev *djrcv_dev =
		container_of(work, struct dj_receiver_dev, work);

	struct dj_workitem workitem;
	unsigned long flags;
	int count;
	int retval;

	dbg_hid("%s\n", __func__);

	spin_lock_irqsave(&djrcv_dev->lock, flags);

	/*
	 * Since we attach to multiple interfaces, we may get scheduled before
	 * we are bound to the HID++ interface, catch this.
	 */
	if (!djrcv_dev->ready) {
		pr_warn("%s: delayedwork queued before hidpp interface was enumerated\n",
			__func__);
		spin_unlock_irqrestore(&djrcv_dev->lock, flags);
		return;
	}

	count = kfifo_out(&djrcv_dev->notif_fifo, &workitem, sizeof(workitem));

	if (count != sizeof(workitem)) {
		spin_unlock_irqrestore(&djrcv_dev->lock, flags);
		return;
	}

	if (!kfifo_is_empty(&djrcv_dev->notif_fifo))
		schedule_work(&djrcv_dev->work);

	spin_unlock_irqrestore(&djrcv_dev->lock, flags);

	switch (workitem.type) {
	case WORKITEM_TYPE_PAIRED:
		logi_dj_recv_add_djhid_device(djrcv_dev, &workitem);
		break;
	case WORKITEM_TYPE_UNPAIRED:
		logi_dj_recv_destroy_djhid_device(djrcv_dev, &workitem);
		break;
	case WORKITEM_TYPE_UNKNOWN:
		retval = logi_dj_recv_query_paired_devices(djrcv_dev);
		if (retval) {
			hid_err(djrcv_dev->hidpp, "%s: logi_dj_recv_query_paired_devices error: %d\n",
				__func__, retval);
		}
		break;
	case WORKITEM_TYPE_EMPTY:
		dbg_hid("%s: device list is empty\n", __func__);
		break;
	}
}

/*
 * Sometimes we receive reports for which we do not have a paired dj_device
 * associated with the device_index or report-type to forward the report to.
 * This means that the original "device paired" notification corresponding
 * to the dj_device never arrived to this driver. Possible reasons for this are:
 * 1) hid-core discards all packets coming from a device during probe().
 * 2) if the receiver is plugged into a KVM switch then the pairing reports
 * are only forwarded to it if the focus is on this PC.
 * This function deals with this by re-asking the receiver for the list of
 * connected devices in the delayed work callback.
 * This function MUST be called with djrcv->lock held.
 */
static void logi_dj_recv_queue_unknown_work(struct dj_receiver_dev *djrcv_dev)
{
	struct dj_workitem workitem = { .type = WORKITEM_TYPE_UNKNOWN };

	/* Rate limit queries done because of unhandeled reports to 2/sec */
	if (time_before(jiffies, djrcv_dev->last_query + HZ / 2))
		return;

	kfifo_in(&djrcv_dev->notif_fifo, &workitem, sizeof(workitem));
	schedule_work(&djrcv_dev->work);
}

static void logi_dj_recv_queue_notification(struct dj_receiver_dev *djrcv_dev,
					   struct dj_report *dj_report)
{
	/* We are called from atomic context (tasklet && djrcv->lock held) */
	struct dj_workitem workitem = {
		.device_index = dj_report->device_index,
	};

	switch (dj_report->report_type) {
	case REPORT_TYPE_NOTIF_DEVICE_PAIRED:
		workitem.type = WORKITEM_TYPE_PAIRED;
		if (dj_report->report_params[DEVICE_PAIRED_PARAM_SPFUNCTION] &
		    SPFUNCTION_DEVICE_LIST_EMPTY) {
			workitem.type = WORKITEM_TYPE_EMPTY;
			break;
		}
		/* fall-through */
	case REPORT_TYPE_NOTIF_DEVICE_UNPAIRED:
		workitem.quad_id_msb =
			dj_report->report_params[DEVICE_PAIRED_PARAM_EQUAD_ID_MSB];
		workitem.quad_id_lsb =
			dj_report->report_params[DEVICE_PAIRED_PARAM_EQUAD_ID_LSB];
		workitem.reports_supported = get_unaligned_le32(
						dj_report->report_params +
						DEVICE_PAIRED_RF_REPORT_TYPE);
		workitem.reports_supported |= HIDPP;
		if (dj_report->report_type == REPORT_TYPE_NOTIF_DEVICE_UNPAIRED)
			workitem.type = WORKITEM_TYPE_UNPAIRED;
		break;
	default:
		logi_dj_recv_queue_unknown_work(djrcv_dev);
		return;
	}

	kfifo_in(&djrcv_dev->notif_fifo, &workitem, sizeof(workitem));
	schedule_work(&djrcv_dev->work);
}

static void logi_hidpp_dev_conn_notif_equad(struct hid_device *hdev,
					    struct hidpp_event *hidpp_report,
					    struct dj_workitem *workitem)
{
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);

	workitem->type = WORKITEM_TYPE_PAIRED;
	workitem->device_type = hidpp_report->params[HIDPP_PARAM_DEVICE_INFO] &
				HIDPP_DEVICE_TYPE_MASK;
	workitem->quad_id_msb = hidpp_report->params[HIDPP_PARAM_EQUAD_MSB];
	workitem->quad_id_lsb = hidpp_report->params[HIDPP_PARAM_EQUAD_LSB];
	switch (workitem->device_type) {
	case REPORT_TYPE_KEYBOARD:
		workitem->reports_supported |= STD_KEYBOARD | MULTIMEDIA |
					       POWER_KEYS | MEDIA_CENTER |
					       HIDPP;
		break;
	case REPORT_TYPE_MOUSE:
		workitem->reports_supported |= STD_MOUSE | HIDPP;
		if (djrcv_dev->type == recvr_type_mouse_only)
			workitem->reports_supported |= MULTIMEDIA;
		break;
	}
}

static void logi_hidpp_dev_conn_notif_27mhz(struct hid_device *hdev,
					    struct hidpp_event *hidpp_report,
					    struct dj_workitem *workitem)
{
	workitem->type = WORKITEM_TYPE_PAIRED;
	workitem->quad_id_lsb = hidpp_report->params[HIDPP_PARAM_27MHZ_DEVID];
	switch (hidpp_report->device_index) {
	case 1: /* Index 1 is always a mouse */
	case 2: /* Index 2 is always a mouse */
		workitem->device_type = HIDPP_DEVICE_TYPE_MOUSE;
		workitem->reports_supported |= STD_MOUSE | HIDPP;
		break;
	case 3: /* Index 3 is always the keyboard */
	case 4: /* Index 4 is used for an optional separate numpad */
		workitem->device_type = HIDPP_DEVICE_TYPE_KEYBOARD;
		workitem->reports_supported |= STD_KEYBOARD | MULTIMEDIA |
					       POWER_KEYS | HIDPP;
		break;
	default:
		hid_warn(hdev, "%s: unexpected device-index %d", __func__,
			 hidpp_report->device_index);
	}
}

static void logi_hidpp_recv_queue_notif(struct hid_device *hdev,
					struct hidpp_event *hidpp_report)
{
	/* We are called from atomic context (tasklet && djrcv->lock held) */
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);
	const char *device_type = "UNKNOWN";
	struct dj_workitem workitem = {
		.type = WORKITEM_TYPE_EMPTY,
		.device_index = hidpp_report->device_index,
	};

	switch (hidpp_report->params[HIDPP_PARAM_PROTO_TYPE]) {
	case 0x01:
		device_type = "Bluetooth";
		/* Bluetooth connect packet contents is the same as (e)QUAD */
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		if (!(hidpp_report->params[HIDPP_PARAM_DEVICE_INFO] &
						HIDPP_MANUFACTURER_MASK)) {
			hid_info(hdev, "Non Logitech device connected on slot %d\n",
				 hidpp_report->device_index);
			workitem.reports_supported &= ~HIDPP;
		}
		break;
	case 0x02:
		device_type = "27 Mhz";
		logi_hidpp_dev_conn_notif_27mhz(hdev, hidpp_report, &workitem);
		break;
	case 0x03:
		device_type = "QUAD or eQUAD";
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		break;
	case 0x04:
		device_type = "eQUAD step 4 DJ";
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		break;
	case 0x05:
		device_type = "DFU Lite";
		break;
	case 0x06:
		device_type = "eQUAD step 4 Lite";
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		break;
	case 0x07:
		device_type = "eQUAD step 4 Gaming";
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		break;
	case 0x08:
		device_type = "eQUAD step 4 for gamepads";
		break;
	case 0x0a:
		device_type = "eQUAD nano Lite";
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		break;
	case 0x0c:
		device_type = "eQUAD Lightspeed 1";
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		workitem.reports_supported |= STD_KEYBOARD;
		break;
	case 0x0d:
		device_type = "eQUAD Lightspeed 1_1";
		logi_hidpp_dev_conn_notif_equad(hdev, hidpp_report, &workitem);
		workitem.reports_supported |= STD_KEYBOARD;
		break;
	}

	if (workitem.type == WORKITEM_TYPE_EMPTY) {
		hid_warn(hdev,
			 "unusable device of type %s (0x%02x) connected on slot %d",
			 device_type,
			 hidpp_report->params[HIDPP_PARAM_PROTO_TYPE],
			 hidpp_report->device_index);
		return;
	}

	hid_info(hdev, "device of type %s (0x%02x) connected on slot %d",
		 device_type, hidpp_report->params[HIDPP_PARAM_PROTO_TYPE],
		 hidpp_report->device_index);

	kfifo_in(&djrcv_dev->notif_fifo, &workitem, sizeof(workitem));
	schedule_work(&djrcv_dev->work);
}

static void logi_dj_recv_forward_null_report(struct dj_receiver_dev *djrcv_dev,
					     struct dj_report *dj_report)
{
	/* We are called from atomic context (tasklet && djrcv->lock held) */
	unsigned int i;
	u8 reportbuffer[MAX_REPORT_SIZE];
	struct dj_device *djdev;

	djdev = djrcv_dev->paired_dj_devices[dj_report->device_index];

	memset(reportbuffer, 0, sizeof(reportbuffer));

	for (i = 0; i < NUMBER_OF_HID_REPORTS; i++) {
		if (djdev->reports_supported & (1 << i)) {
			reportbuffer[0] = i;
			if (hid_input_report(djdev->hdev,
					     HID_INPUT_REPORT,
					     reportbuffer,
					     hid_reportid_size_map[i], 1)) {
				dbg_hid("hid_input_report error sending null "
					"report\n");
			}
		}
	}
}

static void logi_dj_recv_forward_dj(struct dj_receiver_dev *djrcv_dev,
				    struct dj_report *dj_report)
{
	/* We are called from atomic context (tasklet && djrcv->lock held) */
	struct dj_device *dj_device;

	dj_device = djrcv_dev->paired_dj_devices[dj_report->device_index];

	if ((dj_report->report_type > ARRAY_SIZE(hid_reportid_size_map) - 1) ||
	    (hid_reportid_size_map[dj_report->report_type] == 0)) {
		dbg_hid("invalid report type:%x\n", dj_report->report_type);
		return;
	}

	if (hid_input_report(dj_device->hdev,
			HID_INPUT_REPORT, &dj_report->report_type,
			hid_reportid_size_map[dj_report->report_type], 1)) {
		dbg_hid("hid_input_report error\n");
	}
}

static void logi_dj_recv_forward_report(struct dj_device *dj_dev, u8 *data,
					int size)
{
	/* We are called from atomic context (tasklet && djrcv->lock held) */
	if (hid_input_report(dj_dev->hdev, HID_INPUT_REPORT, data, size, 1))
		dbg_hid("hid_input_report error\n");
}

static void logi_dj_recv_forward_input_report(struct hid_device *hdev,
					      u8 *data, int size)
{
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);
	struct dj_device *dj_dev;
	unsigned long flags;
	u8 report = data[0];
	int i;

	if (report > REPORT_TYPE_RFREPORT_LAST) {
		hid_err(hdev, "Unexpected input report number %d\n", report);
		return;
	}

	spin_lock_irqsave(&djrcv_dev->lock, flags);
	for (i = 0; i < (DJ_MAX_PAIRED_DEVICES + DJ_DEVICE_INDEX_MIN); i++) {
		dj_dev = djrcv_dev->paired_dj_devices[i];
		if (dj_dev && (dj_dev->reports_supported & BIT(report))) {
			logi_dj_recv_forward_report(dj_dev, data, size);
			spin_unlock_irqrestore(&djrcv_dev->lock, flags);
			return;
		}
	}

	logi_dj_recv_queue_unknown_work(djrcv_dev);
	spin_unlock_irqrestore(&djrcv_dev->lock, flags);

	dbg_hid("No dj-devs handling input report number %d\n", report);
}

static int logi_dj_recv_send_report(struct dj_receiver_dev *djrcv_dev,
				    struct dj_report *dj_report)
{
	struct hid_device *hdev = djrcv_dev->hidpp;
	struct hid_report *report;
	struct hid_report_enum *output_report_enum;
	u8 *data = (u8 *)(&dj_report->device_index);
	unsigned int i;

	output_report_enum = &hdev->report_enum[HID_OUTPUT_REPORT];
	report = output_report_enum->report_id_hash[REPORT_ID_DJ_SHORT];

	if (!report) {
		hid_err(hdev, "%s: unable to find dj report\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < DJREPORT_SHORT_LENGTH - 1; i++)
		report->field[0]->value[i] = data[i];

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);

	return 0;
}

static int logi_dj_recv_query_hidpp_devices(struct dj_receiver_dev *djrcv_dev)
{
	static const u8 template[] = {
		REPORT_ID_HIDPP_SHORT,
		HIDPP_RECEIVER_INDEX,
		HIDPP_SET_REGISTER,
		HIDPP_REG_CONNECTION_STATE,
		HIDPP_FAKE_DEVICE_ARRIVAL,
		0x00, 0x00
	};
	u8 *hidpp_report;
	int retval;

	hidpp_report = kmemdup(template, sizeof(template), GFP_KERNEL);
	if (!hidpp_report)
		return -ENOMEM;

	retval = hid_hw_raw_request(djrcv_dev->hidpp,
				    REPORT_ID_HIDPP_SHORT,
				    hidpp_report, sizeof(template),
				    HID_OUTPUT_REPORT,
				    HID_REQ_SET_REPORT);

	kfree(hidpp_report);
	return (retval < 0) ? retval : 0;
}

static int logi_dj_recv_query_paired_devices(struct dj_receiver_dev *djrcv_dev)
{
	struct dj_report *dj_report;
	int retval;

	djrcv_dev->last_query = jiffies;

	if (djrcv_dev->type != recvr_type_dj)
		return logi_dj_recv_query_hidpp_devices(djrcv_dev);

	dj_report = kzalloc(sizeof(struct dj_report), GFP_KERNEL);
	if (!dj_report)
		return -ENOMEM;
	dj_report->report_id = REPORT_ID_DJ_SHORT;
	dj_report->device_index = 0xFF;
	dj_report->report_type = REPORT_TYPE_CMD_GET_PAIRED_DEVICES;
	retval = logi_dj_recv_send_report(djrcv_dev, dj_report);
	kfree(dj_report);
	return retval;
}


static int logi_dj_recv_switch_to_dj_mode(struct dj_receiver_dev *djrcv_dev,
					  unsigned timeout)
{
	struct hid_device *hdev = djrcv_dev->hidpp;
	struct dj_report *dj_report;
	u8 *buf;
	int retval = 0;

	dj_report = kzalloc(sizeof(struct dj_report), GFP_KERNEL);
	if (!dj_report)
		return -ENOMEM;

	if (djrcv_dev->type == recvr_type_dj) {
		dj_report->report_id = REPORT_ID_DJ_SHORT;
		dj_report->device_index = 0xFF;
		dj_report->report_type = REPORT_TYPE_CMD_SWITCH;
		dj_report->report_params[CMD_SWITCH_PARAM_DEVBITFIELD] = 0x3F;
		dj_report->report_params[CMD_SWITCH_PARAM_TIMEOUT_SECONDS] =
								(u8)timeout;

		retval = logi_dj_recv_send_report(djrcv_dev, dj_report);

		/*
		 * Ugly sleep to work around a USB 3.0 bug when the receiver is
		 * still processing the "switch-to-dj" command while we send an
		 * other command.
		 * 50 msec should gives enough time to the receiver to be ready.
		 */
		msleep(50);
	}

	/*
	 * Magical bits to set up hidpp notifications when the dj devices
	 * are connected/disconnected.
	 *
	 * We can reuse dj_report because HIDPP_REPORT_SHORT_LENGTH is smaller
	 * than DJREPORT_SHORT_LENGTH.
	 */
	buf = (u8 *)dj_report;

	memset(buf, 0, HIDPP_REPORT_SHORT_LENGTH);

	buf[0] = REPORT_ID_HIDPP_SHORT;
	buf[1] = 0xFF;
	buf[2] = 0x80;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x09;
	buf[6] = 0x00;

	hid_hw_raw_request(hdev, REPORT_ID_HIDPP_SHORT, buf,
			HIDPP_REPORT_SHORT_LENGTH, HID_OUTPUT_REPORT,
			HID_REQ_SET_REPORT);

	kfree(dj_report);
	return retval;
}


static int logi_dj_ll_open(struct hid_device *hid)
{
	dbg_hid("%s: %s\n", __func__, hid->phys);
	return 0;

}

static void logi_dj_ll_close(struct hid_device *hid)
{
	dbg_hid("%s: %s\n", __func__, hid->phys);
}

/*
 * Register 0xB5 is "pairing information". It is solely intended for the
 * receiver, so do not overwrite the device index.
 */
static u8 unifying_pairing_query[]  = { REPORT_ID_HIDPP_SHORT,
					HIDPP_RECEIVER_INDEX,
					HIDPP_GET_LONG_REGISTER,
					HIDPP_REG_PAIRING_INFORMATION };
static u8 unifying_pairing_answer[] = { REPORT_ID_HIDPP_LONG,
					HIDPP_RECEIVER_INDEX,
					HIDPP_GET_LONG_REGISTER,
					HIDPP_REG_PAIRING_INFORMATION };

static int logi_dj_ll_raw_request(struct hid_device *hid,
				  unsigned char reportnum, __u8 *buf,
				  size_t count, unsigned char report_type,
				  int reqtype)
{
	struct dj_device *djdev = hid->driver_data;
	struct dj_receiver_dev *djrcv_dev = djdev->dj_receiver_dev;
	u8 *out_buf;
	int ret;

	if ((buf[0] == REPORT_ID_HIDPP_SHORT) ||
	    (buf[0] == REPORT_ID_HIDPP_LONG) ||
	    (buf[0] == REPORT_ID_HIDPP_VERY_LONG)) {
		if (count < 2)
			return -EINVAL;

		/* special case where we should not overwrite
		 * the device_index */
		if (count == 7 && !memcmp(buf, unifying_pairing_query,
					  sizeof(unifying_pairing_query)))
			buf[4] = (buf[4] & 0xf0) | (djdev->device_index - 1);
		else
			buf[1] = djdev->device_index;
		return hid_hw_raw_request(djrcv_dev->hidpp, reportnum, buf,
				count, report_type, reqtype);
	}

	if (buf[0] != REPORT_TYPE_LEDS)
		return -EINVAL;

	if (djrcv_dev->type != recvr_type_dj && count >= 2) {
		if (!djrcv_dev->keyboard) {
			hid_warn(hid, "Received REPORT_TYPE_LEDS request before the keyboard interface was enumerated\n");
			return 0;
		}
		/* usbhid overrides the report ID and ignores the first byte */
		return hid_hw_raw_request(djrcv_dev->keyboard, 0, buf, count,
					  report_type, reqtype);
	}

	out_buf = kzalloc(DJREPORT_SHORT_LENGTH, GFP_ATOMIC);
	if (!out_buf)
		return -ENOMEM;

	if (count > DJREPORT_SHORT_LENGTH - 2)
		count = DJREPORT_SHORT_LENGTH - 2;

	out_buf[0] = REPORT_ID_DJ_SHORT;
	out_buf[1] = djdev->device_index;
	memcpy(out_buf + 2, buf, count);

	ret = hid_hw_raw_request(djrcv_dev->hidpp, out_buf[0], out_buf,
		DJREPORT_SHORT_LENGTH, report_type, reqtype);

	kfree(out_buf);
	return ret;
}

static void rdcat(char *rdesc, unsigned int *rsize, const char *data, unsigned int size)
{
	memcpy(rdesc + *rsize, data, size);
	*rsize += size;
}

static int logi_dj_ll_parse(struct hid_device *hid)
{
	struct dj_device *djdev = hid->driver_data;
	unsigned int rsize = 0;
	char *rdesc;
	int retval;

	dbg_hid("%s\n", __func__);

	djdev->hdev->version = 0x0111;
	djdev->hdev->country = 0x00;

	rdesc = kmalloc(MAX_RDESC_SIZE, GFP_KERNEL);
	if (!rdesc)
		return -ENOMEM;

	if (djdev->reports_supported & STD_KEYBOARD) {
		dbg_hid("%s: sending a kbd descriptor, reports_supported: %llx\n",
			__func__, djdev->reports_supported);
		rdcat(rdesc, &rsize, kbd_descriptor, sizeof(kbd_descriptor));
	}

	if (djdev->reports_supported & STD_MOUSE) {
		dbg_hid("%s: sending a mouse descriptor, reports_supported: %llx\n",
			__func__, djdev->reports_supported);
		if (djdev->dj_receiver_dev->type == recvr_type_gaming_hidpp ||
		    djdev->dj_receiver_dev->type == recvr_type_mouse_only)
			rdcat(rdesc, &rsize, mse_high_res_descriptor,
			      sizeof(mse_high_res_descriptor));
		else if (djdev->dj_receiver_dev->type == recvr_type_27mhz)
			rdcat(rdesc, &rsize, mse_27mhz_descriptor,
			      sizeof(mse_27mhz_descriptor));
		else if (djdev->dj_receiver_dev->type == recvr_type_bluetooth)
			rdcat(rdesc, &rsize, mse_bluetooth_descriptor,
			      sizeof(mse_bluetooth_descriptor));
		else
			rdcat(rdesc, &rsize, mse_descriptor,
			      sizeof(mse_descriptor));
	}

	if (djdev->reports_supported & MULTIMEDIA) {
		dbg_hid("%s: sending a multimedia report descriptor: %llx\n",
			__func__, djdev->reports_supported);
		rdcat(rdesc, &rsize, consumer_descriptor, sizeof(consumer_descriptor));
	}

	if (djdev->reports_supported & POWER_KEYS) {
		dbg_hid("%s: sending a power keys report descriptor: %llx\n",
			__func__, djdev->reports_supported);
		rdcat(rdesc, &rsize, syscontrol_descriptor, sizeof(syscontrol_descriptor));
	}

	if (djdev->reports_supported & MEDIA_CENTER) {
		dbg_hid("%s: sending a media center report descriptor: %llx\n",
			__func__, djdev->reports_supported);
		rdcat(rdesc, &rsize, media_descriptor, sizeof(media_descriptor));
	}

	if (djdev->reports_supported & KBD_LEDS) {
		dbg_hid("%s: need to send kbd leds report descriptor: %llx\n",
			__func__, djdev->reports_supported);
	}

	if (djdev->reports_supported & HIDPP) {
		dbg_hid("%s: sending a HID++ descriptor, reports_supported: %llx\n",
			__func__, djdev->reports_supported);
		rdcat(rdesc, &rsize, hidpp_descriptor,
		      sizeof(hidpp_descriptor));
	}

	retval = hid_parse_report(hid, rdesc, rsize);
	kfree(rdesc);

	return retval;
}

static int logi_dj_ll_start(struct hid_device *hid)
{
	dbg_hid("%s\n", __func__);
	return 0;
}

static void logi_dj_ll_stop(struct hid_device *hid)
{
	dbg_hid("%s\n", __func__);
}


static struct hid_ll_driver logi_dj_ll_driver = {
	.parse = logi_dj_ll_parse,
	.start = logi_dj_ll_start,
	.stop = logi_dj_ll_stop,
	.open = logi_dj_ll_open,
	.close = logi_dj_ll_close,
	.raw_request = logi_dj_ll_raw_request,
};

static int logi_dj_dj_event(struct hid_device *hdev,
			     struct hid_report *report, u8 *data,
			     int size)
{
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);
	struct dj_report *dj_report = (struct dj_report *) data;
	unsigned long flags;

	/*
	 * Here we receive all data coming from iface 2, there are 3 cases:
	 *
	 * 1) Data is intended for this driver i. e. data contains arrival,
	 * departure, etc notifications, in which case we queue them for delayed
	 * processing by the work queue. We return 1 to hid-core as no further
	 * processing is required from it.
	 *
	 * 2) Data informs a connection change, if the change means rf link
	 * loss, then we must send a null report to the upper layer to discard
	 * potentially pressed keys that may be repeated forever by the input
	 * layer. Return 1 to hid-core as no further processing is required.
	 *
	 * 3) Data is an actual input event from a paired DJ device in which
	 * case we forward it to the correct hid device (via hid_input_report()
	 * ) and return 1 so hid-core does not anything else with it.
	 */

	if ((dj_report->device_index < DJ_DEVICE_INDEX_MIN) ||
	    (dj_report->device_index > DJ_DEVICE_INDEX_MAX)) {
		/*
		 * Device index is wrong, bail out.
		 * This driver can ignore safely the receiver notifications,
		 * so ignore those reports too.
		 */
		if (dj_report->device_index != DJ_RECEIVER_INDEX)
			hid_err(hdev, "%s: invalid device index:%d\n",
				__func__, dj_report->device_index);
		return false;
	}

	spin_lock_irqsave(&djrcv_dev->lock, flags);

	if (!djrcv_dev->paired_dj_devices[dj_report->device_index]) {
		/* received an event for an unknown device, bail out */
		logi_dj_recv_queue_notification(djrcv_dev, dj_report);
		goto out;
	}

	switch (dj_report->report_type) {
	case REPORT_TYPE_NOTIF_DEVICE_PAIRED:
		/* pairing notifications are handled above the switch */
		break;
	case REPORT_TYPE_NOTIF_DEVICE_UNPAIRED:
		logi_dj_recv_queue_notification(djrcv_dev, dj_report);
		break;
	case REPORT_TYPE_NOTIF_CONNECTION_STATUS:
		if (dj_report->report_params[CONNECTION_STATUS_PARAM_STATUS] ==
		    STATUS_LINKLOSS) {
			logi_dj_recv_forward_null_report(djrcv_dev, dj_report);
		}
		break;
	default:
		logi_dj_recv_forward_dj(djrcv_dev, dj_report);
	}

out:
	spin_unlock_irqrestore(&djrcv_dev->lock, flags);

	return true;
}

static int logi_dj_hidpp_event(struct hid_device *hdev,
			     struct hid_report *report, u8 *data,
			     int size)
{
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);
	struct hidpp_event *hidpp_report = (struct hidpp_event *) data;
	struct dj_device *dj_dev;
	unsigned long flags;
	u8 device_index = hidpp_report->device_index;

	if (device_index == HIDPP_RECEIVER_INDEX) {
		/* special case were the device wants to know its unifying
		 * name */
		if (size == HIDPP_REPORT_LONG_LENGTH &&
		    !memcmp(data, unifying_pairing_answer,
			    sizeof(unifying_pairing_answer)))
			device_index = (data[4] & 0x0F) + 1;
		else
			return false;
	}

	/*
	 * Data is from the HID++ collection, in this case, we forward the
	 * data to the corresponding child dj device and return 0 to hid-core
	 * so he data also goes to the hidraw device of the receiver. This
	 * allows a user space application to implement the full HID++ routing
	 * via the receiver.
	 */

	if ((device_index < DJ_DEVICE_INDEX_MIN) ||
	    (device_index > DJ_DEVICE_INDEX_MAX)) {
		/*
		 * Device index is wrong, bail out.
		 * This driver can ignore safely the receiver notifications,
		 * so ignore those reports too.
		 */
		hid_err(hdev, "%s: invalid device index:%d\n", __func__,
			hidpp_report->device_index);
		return false;
	}

	spin_lock_irqsave(&djrcv_dev->lock, flags);

	dj_dev = djrcv_dev->paired_dj_devices[device_index];

	/*
	 * With 27 MHz receivers, we do not get an explicit unpair event,
	 * remove the old device if the user has paired a *different* device.
	 */
	if (djrcv_dev->type == recvr_type_27mhz && dj_dev &&
	    hidpp_report->sub_id == REPORT_TYPE_NOTIF_DEVICE_CONNECTED &&
	    hidpp_report->params[HIDPP_PARAM_PROTO_TYPE] == 0x02 &&
	    hidpp_report->params[HIDPP_PARAM_27MHZ_DEVID] !=
						dj_dev->hdev->product) {
		struct dj_workitem workitem = {
			.device_index = hidpp_report->device_index,
			.type = WORKITEM_TYPE_UNPAIRED,
		};
		kfifo_in(&djrcv_dev->notif_fifo, &workitem, sizeof(workitem));
		/* logi_hidpp_recv_queue_notif will queue the work */
		dj_dev = NULL;
	}

	if (dj_dev) {
		logi_dj_recv_forward_report(dj_dev, data, size);
	} else {
		if (hidpp_report->sub_id == REPORT_TYPE_NOTIF_DEVICE_CONNECTED)
			logi_hidpp_recv_queue_notif(hdev, hidpp_report);
		else
			logi_dj_recv_queue_unknown_work(djrcv_dev);
	}

	spin_unlock_irqrestore(&djrcv_dev->lock, flags);

	return false;
}

static int logi_dj_raw_event(struct hid_device *hdev,
			     struct hid_report *report, u8 *data,
			     int size)
{
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);
	dbg_hid("%s, size:%d\n", __func__, size);

	if (!djrcv_dev)
		return 0;

	if (!hdev->report_enum[HID_INPUT_REPORT].numbered) {

		if (djrcv_dev->unnumbered_application == HID_GD_KEYBOARD) {
			/*
			 * For the keyboard, we can reuse the same report by
			 * using the second byte which is constant in the USB
			 * HID report descriptor.
			 */
			data[1] = data[0];
			data[0] = REPORT_TYPE_KEYBOARD;

			logi_dj_recv_forward_input_report(hdev, data, size);

			/* restore previous state */
			data[0] = data[1];
			data[1] = 0;
		}
		/*
		 * Mouse-only receivers send unnumbered mouse data. The 27 MHz
		 * receiver uses 6 byte packets, the nano receiver 8 bytes.
		 */
		if (djrcv_dev->unnumbered_application == HID_GD_MOUSE &&
		    size <= 8) {
			u8 mouse_report[9];

			/* Prepend report id */
			mouse_report[0] = REPORT_TYPE_MOUSE;
			memcpy(mouse_report + 1, data, size);
			logi_dj_recv_forward_input_report(hdev, mouse_report,
							  size + 1);
		}

		return false;
	}

	switch (data[0]) {
	case REPORT_ID_DJ_SHORT:
		if (size != DJREPORT_SHORT_LENGTH) {
			hid_err(hdev, "Short DJ report bad size (%d)", size);
			return false;
		}
		return logi_dj_dj_event(hdev, report, data, size);
	case REPORT_ID_DJ_LONG:
		if (size != DJREPORT_LONG_LENGTH) {
			hid_err(hdev, "Long DJ report bad size (%d)", size);
			return false;
		}
		return logi_dj_dj_event(hdev, report, data, size);
	case REPORT_ID_HIDPP_SHORT:
		if (size != HIDPP_REPORT_SHORT_LENGTH) {
			hid_err(hdev, "Short HID++ report bad size (%d)", size);
			return false;
		}
		return logi_dj_hidpp_event(hdev, report, data, size);
	case REPORT_ID_HIDPP_LONG:
		if (size != HIDPP_REPORT_LONG_LENGTH) {
			hid_err(hdev, "Long HID++ report bad size (%d)", size);
			return false;
		}
		return logi_dj_hidpp_event(hdev, report, data, size);
	}

	logi_dj_recv_forward_input_report(hdev, data, size);

	return false;
}

static int logi_dj_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	struct hid_report_enum *rep_enum;
	struct hid_report *rep;
	struct dj_receiver_dev *djrcv_dev;
	struct usb_interface *intf;
	unsigned int no_dj_interfaces = 0;
	bool has_hidpp = false;
	unsigned long flags;
	int retval;

	/*
	 * Call to usbhid to fetch the HID descriptors of the current
	 * interface subsequently call to the hid/hid-core to parse the
	 * fetched descriptors.
	 */
	retval = hid_parse(hdev);
	if (retval) {
		hid_err(hdev, "%s: parse failed\n", __func__);
		return retval;
	}

	/*
	 * Some KVMs add an extra interface for e.g. mouse emulation. If we
	 * treat these as logitech-dj interfaces then this causes input events
	 * reported through this extra interface to not be reported correctly.
	 * To avoid this, we treat these as generic-hid devices.
	 */
	switch (id->driver_data) {
	case recvr_type_dj:		no_dj_interfaces = 3; break;
	case recvr_type_hidpp:		no_dj_interfaces = 2; break;
	case recvr_type_gaming_hidpp:	no_dj_interfaces = 3; break;
	case recvr_type_mouse_only:	no_dj_interfaces = 2; break;
	case recvr_type_27mhz:		no_dj_interfaces = 2; break;
	case recvr_type_bluetooth:	no_dj_interfaces = 2; break;
	}
	if (hid_is_using_ll_driver(hdev, &usb_hid_driver)) {
		intf = to_usb_interface(hdev->dev.parent);
		if (intf && intf->altsetting->desc.bInterfaceNumber >=
							no_dj_interfaces) {
			hdev->quirks |= HID_QUIRK_INPUT_PER_APP;
			return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
		}
	}

	rep_enum = &hdev->report_enum[HID_INPUT_REPORT];

	/* no input reports, bail out */
	if (list_empty(&rep_enum->report_list))
		return -ENODEV;

	/*
	 * Check for the HID++ application.
	 * Note: we should theoretically check for HID++ and DJ
	 * collections, but this will do.
	 */
	list_for_each_entry(rep, &rep_enum->report_list, list) {
		if (rep->application == 0xff000001)
			has_hidpp = true;
	}

	/*
	 * Ignore interfaces without DJ/HID++ collection, they will not carry
	 * any data, dont create any hid_device for them.
	 */
	if (!has_hidpp && id->driver_data == recvr_type_dj)
		return -ENODEV;

	/* get the current application attached to the node */
	rep = list_first_entry(&rep_enum->report_list, struct hid_report, list);
	djrcv_dev = dj_get_receiver_dev(hdev, id->driver_data,
					rep->application, has_hidpp);
	if (!djrcv_dev) {
		hid_err(hdev, "%s: dj_get_receiver_dev failed\n", __func__);
		return -ENOMEM;
	}

	if (!rep_enum->numbered)
		djrcv_dev->unnumbered_application = rep->application;

	/* Starts the usb device and connects to upper interfaces hiddev and
	 * hidraw */
	retval = hid_hw_start(hdev, HID_CONNECT_HIDRAW|HID_CONNECT_HIDDEV);
	if (retval) {
		hid_err(hdev, "%s: hid_hw_start returned error\n", __func__);
		goto hid_hw_start_fail;
	}

	if (has_hidpp) {
		retval = logi_dj_recv_switch_to_dj_mode(djrcv_dev, 0);
		if (retval < 0) {
			hid_err(hdev, "%s: logi_dj_recv_switch_to_dj_mode returned error:%d\n",
				__func__, retval);
			goto switch_to_dj_mode_fail;
		}
	}

	/* This is enabling the polling urb on the IN endpoint */
	retval = hid_hw_open(hdev);
	if (retval < 0) {
		hid_err(hdev, "%s: hid_hw_open returned error:%d\n",
			__func__, retval);
		goto llopen_failed;
	}

	/* Allow incoming packets to arrive: */
	hid_device_io_start(hdev);

	if (has_hidpp) {
		spin_lock_irqsave(&djrcv_dev->lock, flags);
		djrcv_dev->ready = true;
		spin_unlock_irqrestore(&djrcv_dev->lock, flags);
		retval = logi_dj_recv_query_paired_devices(djrcv_dev);
		if (retval < 0) {
			hid_err(hdev, "%s: logi_dj_recv_query_paired_devices error:%d\n",
				__func__, retval);
			/*
			 * This can happen with a KVM, let the probe succeed,
			 * logi_dj_recv_queue_unknown_work will retry later.
			 */
		}
	}

	return 0;

llopen_failed:
switch_to_dj_mode_fail:
	hid_hw_stop(hdev);

hid_hw_start_fail:
	dj_put_receiver_dev(hdev);
	return retval;
}

#ifdef CONFIG_PM
static int logi_dj_reset_resume(struct hid_device *hdev)
{
	int retval;
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);

	if (!djrcv_dev || djrcv_dev->hidpp != hdev)
		return 0;

	retval = logi_dj_recv_switch_to_dj_mode(djrcv_dev, 0);
	if (retval < 0) {
		hid_err(hdev, "%s: logi_dj_recv_switch_to_dj_mode returned error:%d\n",
			__func__, retval);
	}

	return 0;
}
#endif

static void logi_dj_remove(struct hid_device *hdev)
{
	struct dj_receiver_dev *djrcv_dev = hid_get_drvdata(hdev);
	struct dj_device *dj_dev;
	unsigned long flags;
	int i;

	dbg_hid("%s\n", __func__);

	if (!djrcv_dev)
		return hid_hw_stop(hdev);

	/*
	 * This ensures that if the work gets requeued from another
	 * interface of the same receiver it will be a no-op.
	 */
	spin_lock_irqsave(&djrcv_dev->lock, flags);
	djrcv_dev->ready = false;
	spin_unlock_irqrestore(&djrcv_dev->lock, flags);

	cancel_work_sync(&djrcv_dev->work);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);

	/*
	 * For proper operation we need access to all interfaces, so we destroy
	 * the paired devices when we're unbound from any interface.
	 *
	 * Note we may still be bound to other interfaces, sharing the same
	 * djrcv_dev, so we need locking here.
	 */
	for (i = 0; i < (DJ_MAX_PAIRED_DEVICES + DJ_DEVICE_INDEX_MIN); i++) {
		spin_lock_irqsave(&djrcv_dev->lock, flags);
		dj_dev = djrcv_dev->paired_dj_devices[i];
		djrcv_dev->paired_dj_devices[i] = NULL;
		spin_unlock_irqrestore(&djrcv_dev->lock, flags);
		if (dj_dev != NULL) {
			hid_destroy_device(dj_dev->hdev);
			kfree(dj_dev);
		}
	}

	dj_put_receiver_dev(hdev);
}

static const struct hid_device_id logi_dj_receivers[] = {
	{HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_UNIFYING_RECEIVER),
	 .driver_data = recvr_type_dj},
	{HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_UNIFYING_RECEIVER_2),
	 .driver_data = recvr_type_dj},
	{ /* Logitech Nano mouse only receiver */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
			 USB_DEVICE_ID_LOGITECH_NANO_RECEIVER),
	 .driver_data = recvr_type_mouse_only},
	{ /* Logitech Nano (non DJ) receiver */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
			 USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_2),
	 .driver_data = recvr_type_hidpp},
	{ /* Logitech G700(s) receiver (0xc531) */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		0xc531),
	 .driver_data = recvr_type_gaming_hidpp},
	{ /* Logitech lightspeed receiver (0xc539) */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1),
	 .driver_data = recvr_type_gaming_hidpp},
	{ /* Logitech lightspeed receiver (0xc53f) */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1_1),
	 .driver_data = recvr_type_gaming_hidpp},
	{ /* Logitech 27 MHz HID++ 1.0 receiver (0xc513) */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_MX3000_RECEIVER),
	 .driver_data = recvr_type_27mhz},
	{ /* Logitech powerplay receiver (0xc53a) */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_POWERPLAY),
	 .driver_data = recvr_type_gaming_hidpp},
	{ /* Logitech 27 MHz HID++ 1.0 receiver (0xc517) */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_S510_RECEIVER_2),
	 .driver_data = recvr_type_27mhz},
	{ /* Logitech 27 MHz HID++ 1.0 mouse-only receiver (0xc51b) */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_27MHZ_MOUSE_RECEIVER),
	 .driver_data = recvr_type_27mhz},
	{ /* Logitech MX5000 HID++ / bluetooth receiver keyboard intf. */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		0xc70e),
	 .driver_data = recvr_type_bluetooth},
	{ /* Logitech MX5000 HID++ / bluetooth receiver mouse intf. */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		0xc70a),
	 .driver_data = recvr_type_bluetooth},
	{ /* Logitech MX5500 HID++ / bluetooth receiver keyboard intf. */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		0xc71b),
	 .driver_data = recvr_type_bluetooth},
	{ /* Logitech MX5500 HID++ / bluetooth receiver mouse intf. */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
		0xc71c),
	 .driver_data = recvr_type_bluetooth},
	{}
};

MODULE_DEVICE_TABLE(hid, logi_dj_receivers);

static struct hid_driver logi_djreceiver_driver = {
	.name = "logitech-djreceiver",
	.id_table = logi_dj_receivers,
	.probe = logi_dj_probe,
	.remove = logi_dj_remove,
	.raw_event = logi_dj_raw_event,
#ifdef CONFIG_PM
	.reset_resume = logi_dj_reset_resume,
#endif
};

module_hid_driver(logi_djreceiver_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Logitech");
MODULE_AUTHOR("Nestor Lopez Casado");
MODULE_AUTHOR("nlopezcasad@logitech.com");
