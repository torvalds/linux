// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Azoteq IQS7210A/7211A/E Trackpad/Touchscreen Controller
 *
 * Copyright (C) 2023 Jeff LaBundy <jeff@labundy.com>
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#define IQS7211_PROD_NUM			0x00

#define IQS7211_EVENT_MASK_ALL			GENMASK(14, 8)
#define IQS7211_EVENT_MASK_ALP			BIT(13)
#define IQS7211_EVENT_MASK_BTN			BIT(12)
#define IQS7211_EVENT_MASK_ATI			BIT(11)
#define IQS7211_EVENT_MASK_MOVE			BIT(10)
#define IQS7211_EVENT_MASK_GSTR			BIT(9)
#define IQS7211_EVENT_MODE			BIT(8)

#define IQS7211_COMMS_ERROR			0xEEEE
#define IQS7211_COMMS_RETRY_MS			50
#define IQS7211_COMMS_SLEEP_US			100
#define IQS7211_COMMS_TIMEOUT_US		(100 * USEC_PER_MSEC)
#define IQS7211_RESET_TIMEOUT_MS		150
#define IQS7211_START_TIMEOUT_US		(1 * USEC_PER_SEC)

#define IQS7211_NUM_RETRIES			5
#define IQS7211_NUM_CRX				8
#define IQS7211_MAX_CTX				13

#define IQS7211_MAX_CONTACTS			2
#define IQS7211_MAX_CYCLES			21

/*
 * The following delay is used during instances that must wait for the open-
 * drain RDY pin to settle. Its value is calculated as 5*R*C, where R and C
 * represent typical datasheet values of 4.7k and 100 nF, respectively.
 */
#define iqs7211_irq_wait()			usleep_range(2500, 2600)

enum iqs7211_dev_id {
	IQS7210A,
	IQS7211A,
	IQS7211E,
};

enum iqs7211_comms_mode {
	IQS7211_COMMS_MODE_WAIT,
	IQS7211_COMMS_MODE_FREE,
	IQS7211_COMMS_MODE_FORCE,
};

struct iqs7211_reg_field_desc {
	struct list_head list;
	u8 addr;
	u16 mask;
	u16 val;
};

enum iqs7211_reg_key_id {
	IQS7211_REG_KEY_NONE,
	IQS7211_REG_KEY_PROX,
	IQS7211_REG_KEY_TOUCH,
	IQS7211_REG_KEY_TAP,
	IQS7211_REG_KEY_HOLD,
	IQS7211_REG_KEY_PALM,
	IQS7211_REG_KEY_AXIAL_X,
	IQS7211_REG_KEY_AXIAL_Y,
	IQS7211_REG_KEY_RESERVED
};

enum iqs7211_reg_grp_id {
	IQS7211_REG_GRP_TP,
	IQS7211_REG_GRP_BTN,
	IQS7211_REG_GRP_ALP,
	IQS7211_REG_GRP_SYS,
	IQS7211_NUM_REG_GRPS
};

static const char * const iqs7211_reg_grp_names[IQS7211_NUM_REG_GRPS] = {
	[IQS7211_REG_GRP_TP] = "trackpad",
	[IQS7211_REG_GRP_BTN] = "button",
	[IQS7211_REG_GRP_ALP] = "alp",
};

static const u16 iqs7211_reg_grp_masks[IQS7211_NUM_REG_GRPS] = {
	[IQS7211_REG_GRP_TP] = IQS7211_EVENT_MASK_GSTR,
	[IQS7211_REG_GRP_BTN] = IQS7211_EVENT_MASK_BTN,
	[IQS7211_REG_GRP_ALP] = IQS7211_EVENT_MASK_ALP,
};

struct iqs7211_event_desc {
	const char *name;
	u16 mask;
	u16 enable;
	enum iqs7211_reg_grp_id reg_grp;
	enum iqs7211_reg_key_id reg_key;
};

static const struct iqs7211_event_desc iqs7210a_kp_events[] = {
	{
		.mask = BIT(10),
		.enable = BIT(13) | BIT(12),
		.reg_grp = IQS7211_REG_GRP_ALP,
	},
	{
		.name = "event-prox",
		.mask = BIT(2),
		.enable = BIT(5) | BIT(4),
		.reg_grp = IQS7211_REG_GRP_BTN,
		.reg_key = IQS7211_REG_KEY_PROX,
	},
	{
		.name = "event-touch",
		.mask = BIT(3),
		.enable = BIT(5) | BIT(4),
		.reg_grp = IQS7211_REG_GRP_BTN,
		.reg_key = IQS7211_REG_KEY_TOUCH,
	},
	{
		.name = "event-tap",
		.mask = BIT(0),
		.enable = BIT(0),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_TAP,
	},
	{
		.name = "event-hold",
		.mask = BIT(1),
		.enable = BIT(1),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_HOLD,
	},
	{
		.name = "event-swipe-x-neg",
		.mask = BIT(2),
		.enable = BIT(2),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
	},
	{
		.name = "event-swipe-x-pos",
		.mask = BIT(3),
		.enable = BIT(3),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
	},
	{
		.name = "event-swipe-y-pos",
		.mask = BIT(4),
		.enable = BIT(4),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
	},
	{
		.name = "event-swipe-y-neg",
		.mask = BIT(5),
		.enable = BIT(5),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
	},
};

static const struct iqs7211_event_desc iqs7211a_kp_events[] = {
	{
		.mask = BIT(14),
		.reg_grp = IQS7211_REG_GRP_ALP,
	},
	{
		.name = "event-tap",
		.mask = BIT(0),
		.enable = BIT(0),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_TAP,
	},
	{
		.name = "event-hold",
		.mask = BIT(1),
		.enable = BIT(1),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_HOLD,
	},
	{
		.name = "event-swipe-x-neg",
		.mask = BIT(2),
		.enable = BIT(2),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
	},
	{
		.name = "event-swipe-x-pos",
		.mask = BIT(3),
		.enable = BIT(3),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
	},
	{
		.name = "event-swipe-y-pos",
		.mask = BIT(4),
		.enable = BIT(4),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
	},
	{
		.name = "event-swipe-y-neg",
		.mask = BIT(5),
		.enable = BIT(5),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
	},
};

static const struct iqs7211_event_desc iqs7211e_kp_events[] = {
	{
		.mask = BIT(14),
		.reg_grp = IQS7211_REG_GRP_ALP,
	},
	{
		.name = "event-tap",
		.mask = BIT(0),
		.enable = BIT(0),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_TAP,
	},
	{
		.name = "event-tap-double",
		.mask = BIT(1),
		.enable = BIT(1),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_TAP,
	},
	{
		.name = "event-tap-triple",
		.mask = BIT(2),
		.enable = BIT(2),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_TAP,
	},
	{
		.name = "event-hold",
		.mask = BIT(3),
		.enable = BIT(3),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_HOLD,
	},
	{
		.name = "event-palm",
		.mask = BIT(4),
		.enable = BIT(4),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_PALM,
	},
	{
		.name = "event-swipe-x-pos",
		.mask = BIT(8),
		.enable = BIT(8),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
	},
	{
		.name = "event-swipe-x-neg",
		.mask = BIT(9),
		.enable = BIT(9),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
	},
	{
		.name = "event-swipe-y-pos",
		.mask = BIT(10),
		.enable = BIT(10),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
	},
	{
		.name = "event-swipe-y-neg",
		.mask = BIT(11),
		.enable = BIT(11),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
	},
	{
		.name = "event-swipe-x-pos-hold",
		.mask = BIT(12),
		.enable = BIT(12),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_HOLD,
	},
	{
		.name = "event-swipe-x-neg-hold",
		.mask = BIT(13),
		.enable = BIT(13),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_HOLD,
	},
	{
		.name = "event-swipe-y-pos-hold",
		.mask = BIT(14),
		.enable = BIT(14),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_HOLD,
	},
	{
		.name = "event-swipe-y-neg-hold",
		.mask = BIT(15),
		.enable = BIT(15),
		.reg_grp = IQS7211_REG_GRP_TP,
		.reg_key = IQS7211_REG_KEY_HOLD,
	},
};

struct iqs7211_dev_desc {
	const char *tp_name;
	const char *kp_name;
	u16 prod_num;
	u16 show_reset;
	u16 ati_error[IQS7211_NUM_REG_GRPS];
	u16 ati_start[IQS7211_NUM_REG_GRPS];
	u16 suspend;
	u16 ack_reset;
	u16 comms_end;
	u16 comms_req;
	int charge_shift;
	int info_offs;
	int gesture_offs;
	int contact_offs;
	u8 sys_stat;
	u8 sys_ctrl;
	u8 alp_config;
	u8 tp_config;
	u8 exp_file;
	u8 kp_enable[IQS7211_NUM_REG_GRPS];
	u8 gesture_angle;
	u8 rx_tx_map;
	u8 cycle_alloc[2];
	u8 cycle_limit[2];
	const struct iqs7211_event_desc *kp_events;
	int num_kp_events;
	int min_crx_alp;
	int num_ctx;
};

static const struct iqs7211_dev_desc iqs7211_devs[] = {
	[IQS7210A] = {
		.tp_name = "iqs7210a_trackpad",
		.kp_name = "iqs7210a_keys",
		.prod_num = 944,
		.show_reset = BIT(15),
		.ati_error = {
			[IQS7211_REG_GRP_TP] = BIT(12),
			[IQS7211_REG_GRP_BTN] = BIT(0),
			[IQS7211_REG_GRP_ALP] = BIT(8),
		},
		.ati_start = {
			[IQS7211_REG_GRP_TP] = BIT(13),
			[IQS7211_REG_GRP_BTN] = BIT(1),
			[IQS7211_REG_GRP_ALP] = BIT(9),
		},
		.suspend = BIT(11),
		.ack_reset = BIT(7),
		.comms_end = BIT(2),
		.comms_req = BIT(1),
		.charge_shift = 4,
		.info_offs = 0,
		.gesture_offs = 1,
		.contact_offs = 4,
		.sys_stat = 0x0A,
		.sys_ctrl = 0x35,
		.alp_config = 0x39,
		.tp_config = 0x4E,
		.exp_file = 0x57,
		.kp_enable = {
			[IQS7211_REG_GRP_TP] = 0x58,
			[IQS7211_REG_GRP_BTN] = 0x37,
			[IQS7211_REG_GRP_ALP] = 0x37,
		},
		.gesture_angle = 0x5F,
		.rx_tx_map = 0x60,
		.cycle_alloc = { 0x66, 0x75, },
		.cycle_limit = { 10, 6, },
		.kp_events = iqs7210a_kp_events,
		.num_kp_events = ARRAY_SIZE(iqs7210a_kp_events),
		.min_crx_alp = 4,
		.num_ctx = IQS7211_MAX_CTX - 1,
	},
	[IQS7211A] = {
		.tp_name = "iqs7211a_trackpad",
		.kp_name = "iqs7211a_keys",
		.prod_num = 763,
		.show_reset = BIT(7),
		.ati_error = {
			[IQS7211_REG_GRP_TP] = BIT(3),
			[IQS7211_REG_GRP_ALP] = BIT(5),
		},
		.ati_start = {
			[IQS7211_REG_GRP_TP] = BIT(5),
			[IQS7211_REG_GRP_ALP] = BIT(6),
		},
		.ack_reset = BIT(7),
		.comms_req = BIT(4),
		.charge_shift = 0,
		.info_offs = 0,
		.gesture_offs = 1,
		.contact_offs = 4,
		.sys_stat = 0x10,
		.sys_ctrl = 0x50,
		.tp_config = 0x60,
		.alp_config = 0x72,
		.exp_file = 0x74,
		.kp_enable = {
			[IQS7211_REG_GRP_TP] = 0x80,
		},
		.gesture_angle = 0x87,
		.rx_tx_map = 0x90,
		.cycle_alloc = { 0xA0, 0xB0, },
		.cycle_limit = { 10, 8, },
		.kp_events = iqs7211a_kp_events,
		.num_kp_events = ARRAY_SIZE(iqs7211a_kp_events),
		.num_ctx = IQS7211_MAX_CTX - 1,
	},
	[IQS7211E] = {
		.tp_name = "iqs7211e_trackpad",
		.kp_name = "iqs7211e_keys",
		.prod_num = 1112,
		.show_reset = BIT(7),
		.ati_error = {
			[IQS7211_REG_GRP_TP] = BIT(3),
			[IQS7211_REG_GRP_ALP] = BIT(5),
		},
		.ati_start = {
			[IQS7211_REG_GRP_TP] = BIT(5),
			[IQS7211_REG_GRP_ALP] = BIT(6),
		},
		.suspend = BIT(11),
		.ack_reset = BIT(7),
		.comms_end = BIT(6),
		.comms_req = BIT(4),
		.charge_shift = 0,
		.info_offs = 1,
		.gesture_offs = 0,
		.contact_offs = 2,
		.sys_stat = 0x0E,
		.sys_ctrl = 0x33,
		.tp_config = 0x41,
		.alp_config = 0x36,
		.exp_file = 0x4A,
		.kp_enable = {
			[IQS7211_REG_GRP_TP] = 0x4B,
		},
		.gesture_angle = 0x55,
		.rx_tx_map = 0x56,
		.cycle_alloc = { 0x5D, 0x6C, },
		.cycle_limit = { 10, 11, },
		.kp_events = iqs7211e_kp_events,
		.num_kp_events = ARRAY_SIZE(iqs7211e_kp_events),
		.num_ctx = IQS7211_MAX_CTX,
	},
};

struct iqs7211_prop_desc {
	const char *name;
	enum iqs7211_reg_key_id reg_key;
	u8 reg_addr[IQS7211_NUM_REG_GRPS][ARRAY_SIZE(iqs7211_devs)];
	int reg_shift;
	int reg_width;
	int val_pitch;
	int val_min;
	int val_max;
	const char *label;
};

static const struct iqs7211_prop_desc iqs7211_props[] = {
	{
		.name = "azoteq,ati-frac-div-fine",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x1E,
				[IQS7211A] = 0x30,
				[IQS7211E] = 0x21,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x22,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x23,
				[IQS7211A] = 0x36,
				[IQS7211E] = 0x25,
			},
		},
		.reg_shift = 9,
		.reg_width = 5,
		.label = "ATI fine fractional divider",
	},
	{
		.name = "azoteq,ati-frac-mult-coarse",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x1E,
				[IQS7211A] = 0x30,
				[IQS7211E] = 0x21,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x22,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x23,
				[IQS7211A] = 0x36,
				[IQS7211E] = 0x25,
			},
		},
		.reg_shift = 5,
		.reg_width = 4,
		.label = "ATI coarse fractional multiplier",
	},
	{
		.name = "azoteq,ati-frac-div-coarse",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x1E,
				[IQS7211A] = 0x30,
				[IQS7211E] = 0x21,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x22,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x23,
				[IQS7211A] = 0x36,
				[IQS7211E] = 0x25,
			},
		},
		.reg_shift = 0,
		.reg_width = 5,
		.label = "ATI coarse fractional divider",
	},
	{
		.name = "azoteq,ati-comp-div",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x1F,
				[IQS7211E] = 0x22,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x24,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7211E] = 0x26,
			},
		},
		.reg_shift = 0,
		.reg_width = 8,
		.val_max = 31,
		.label = "ATI compensation divider",
	},
	{
		.name = "azoteq,ati-comp-div",
		.reg_addr = {
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x24,
			},
		},
		.reg_shift = 8,
		.reg_width = 8,
		.val_max = 31,
		.label = "ATI compensation divider",
	},
	{
		.name = "azoteq,ati-comp-div",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7211A] = 0x31,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7211A] = 0x37,
			},
		},
		.val_max = 31,
		.label = "ATI compensation divider",
	},
	{
		.name = "azoteq,ati-target",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x20,
				[IQS7211A] = 0x32,
				[IQS7211E] = 0x23,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x27,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x28,
				[IQS7211A] = 0x38,
				[IQS7211E] = 0x27,
			},
		},
		.label = "ATI target",
	},
	{
		.name = "azoteq,ati-base",
		.reg_addr[IQS7211_REG_GRP_ALP] = {
			[IQS7210A] = 0x26,
		},
		.reg_shift = 8,
		.reg_width = 8,
		.val_pitch = 8,
		.label = "ATI base",
	},
	{
		.name = "azoteq,ati-base",
		.reg_addr[IQS7211_REG_GRP_BTN] = {
			[IQS7210A] = 0x26,
		},
		.reg_shift = 0,
		.reg_width = 8,
		.val_pitch = 8,
		.label = "ATI base",
	},
	{
		.name = "azoteq,rate-active-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x29,
			[IQS7211A] = 0x40,
			[IQS7211E] = 0x28,
		},
		.label = "active mode report rate",
	},
	{
		.name = "azoteq,rate-touch-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x2A,
			[IQS7211A] = 0x41,
			[IQS7211E] = 0x29,
		},
		.label = "idle-touch mode report rate",
	},
	{
		.name = "azoteq,rate-idle-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x2B,
			[IQS7211A] = 0x42,
			[IQS7211E] = 0x2A,
		},
		.label = "idle mode report rate",
	},
	{
		.name = "azoteq,rate-lp1-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x2C,
			[IQS7211A] = 0x43,
			[IQS7211E] = 0x2B,
		},
		.label = "low-power mode 1 report rate",
	},
	{
		.name = "azoteq,rate-lp2-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x2D,
			[IQS7211A] = 0x44,
			[IQS7211E] = 0x2C,
		},
		.label = "low-power mode 2 report rate",
	},
	{
		.name = "azoteq,timeout-active-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x2E,
			[IQS7211A] = 0x45,
			[IQS7211E] = 0x2D,
		},
		.val_pitch = 1000,
		.label = "active mode timeout",
	},
	{
		.name = "azoteq,timeout-touch-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x2F,
			[IQS7211A] = 0x46,
			[IQS7211E] = 0x2E,
		},
		.val_pitch = 1000,
		.label = "idle-touch mode timeout",
	},
	{
		.name = "azoteq,timeout-idle-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x30,
			[IQS7211A] = 0x47,
			[IQS7211E] = 0x2F,
		},
		.val_pitch = 1000,
		.label = "idle mode timeout",
	},
	{
		.name = "azoteq,timeout-lp1-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x31,
			[IQS7211A] = 0x48,
			[IQS7211E] = 0x30,
		},
		.val_pitch = 1000,
		.label = "low-power mode 1 timeout",
	},
	{
		.name = "azoteq,timeout-lp2-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x32,
			[IQS7211E] = 0x31,
		},
		.reg_shift = 8,
		.reg_width = 8,
		.val_pitch = 1000,
		.val_max = 60000,
		.label = "trackpad reference value update rate",
	},
	{
		.name = "azoteq,timeout-lp2-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7211A] = 0x49,
		},
		.val_pitch = 1000,
		.val_max = 60000,
		.label = "trackpad reference value update rate",
	},
	{
		.name = "azoteq,timeout-ati-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x32,
			[IQS7211E] = 0x31,
		},
		.reg_width = 8,
		.val_pitch = 1000,
		.val_max = 60000,
		.label = "ATI error timeout",
	},
	{
		.name = "azoteq,timeout-ati-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7211A] = 0x35,
		},
		.val_pitch = 1000,
		.val_max = 60000,
		.label = "ATI error timeout",
	},
	{
		.name = "azoteq,timeout-comms-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x33,
			[IQS7211A] = 0x4A,
			[IQS7211E] = 0x32,
		},
		.label = "communication timeout",
	},
	{
		.name = "azoteq,timeout-press-ms",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x34,
		},
		.reg_width = 8,
		.val_pitch = 1000,
		.val_max = 60000,
		.label = "press timeout",
	},
	{
		.name = "azoteq,ati-mode",
		.reg_addr[IQS7211_REG_GRP_ALP] = {
			[IQS7210A] = 0x37,
		},
		.reg_shift = 15,
		.reg_width = 1,
		.label = "ATI mode",
	},
	{
		.name = "azoteq,ati-mode",
		.reg_addr[IQS7211_REG_GRP_BTN] = {
			[IQS7210A] = 0x37,
		},
		.reg_shift = 7,
		.reg_width = 1,
		.label = "ATI mode",
	},
	{
		.name = "azoteq,sense-mode",
		.reg_addr[IQS7211_REG_GRP_ALP] = {
			[IQS7210A] = 0x37,
			[IQS7211A] = 0x72,
			[IQS7211E] = 0x36,
		},
		.reg_shift = 8,
		.reg_width = 1,
		.label = "sensing mode",
	},
	{
		.name = "azoteq,sense-mode",
		.reg_addr[IQS7211_REG_GRP_BTN] = {
			[IQS7210A] = 0x37,
		},
		.reg_shift = 0,
		.reg_width = 2,
		.val_max = 2,
		.label = "sensing mode",
	},
	{
		.name = "azoteq,fosc-freq",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x38,
			[IQS7211A] = 0x52,
			[IQS7211E] = 0x35,
		},
		.reg_shift = 4,
		.reg_width = 1,
		.label = "core clock frequency selection",
	},
	{
		.name = "azoteq,fosc-trim",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x38,
			[IQS7211A] = 0x52,
			[IQS7211E] = 0x35,
		},
		.reg_shift = 0,
		.reg_width = 4,
		.label = "core clock frequency trim",
	},
	{
		.name = "azoteq,touch-exit",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x3B,
				[IQS7211A] = 0x53,
				[IQS7211E] = 0x38,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x3E,
			},
		},
		.reg_shift = 8,
		.reg_width = 8,
		.label = "touch exit factor",
	},
	{
		.name = "azoteq,touch-enter",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x3B,
				[IQS7211A] = 0x53,
				[IQS7211E] = 0x38,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x3E,
			},
		},
		.reg_shift = 0,
		.reg_width = 8,
		.label = "touch entrance factor",
	},
	{
		.name = "azoteq,thresh",
		.reg_addr = {
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x3C,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x3D,
				[IQS7211A] = 0x54,
				[IQS7211E] = 0x39,
			},
		},
		.label = "threshold",
	},
	{
		.name = "azoteq,debounce-exit",
		.reg_addr = {
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x3F,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x40,
				[IQS7211A] = 0x56,
				[IQS7211E] = 0x3A,
			},
		},
		.reg_shift = 8,
		.reg_width = 8,
		.label = "debounce exit factor",
	},
	{
		.name = "azoteq,debounce-enter",
		.reg_addr = {
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x3F,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x40,
				[IQS7211A] = 0x56,
				[IQS7211E] = 0x3A,
			},
		},
		.reg_shift = 0,
		.reg_width = 8,
		.label = "debounce entrance factor",
	},
	{
		.name = "azoteq,conv-frac",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x48,
				[IQS7211A] = 0x58,
				[IQS7211E] = 0x3D,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x49,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x4A,
				[IQS7211A] = 0x59,
				[IQS7211E] = 0x3E,
			},
		},
		.reg_shift = 8,
		.reg_width = 8,
		.label = "conversion frequency fractional divider",
	},
	{
		.name = "azoteq,conv-period",
		.reg_addr = {
			[IQS7211_REG_GRP_TP] = {
				[IQS7210A] = 0x48,
				[IQS7211A] = 0x58,
				[IQS7211E] = 0x3D,
			},
			[IQS7211_REG_GRP_BTN] = {
				[IQS7210A] = 0x49,
			},
			[IQS7211_REG_GRP_ALP] = {
				[IQS7210A] = 0x4A,
				[IQS7211A] = 0x59,
				[IQS7211E] = 0x3E,
			},
		},
		.reg_shift = 0,
		.reg_width = 8,
		.label = "conversion period",
	},
	{
		.name = "azoteq,thresh",
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x55,
			[IQS7211A] = 0x67,
			[IQS7211E] = 0x48,
		},
		.reg_shift = 0,
		.reg_width = 8,
		.label = "threshold",
	},
	{
		.name = "azoteq,contact-split",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x55,
			[IQS7211A] = 0x67,
			[IQS7211E] = 0x48,
		},
		.reg_shift = 8,
		.reg_width = 8,
		.label = "contact split factor",
	},
	{
		.name = "azoteq,trim-x",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x56,
			[IQS7211E] = 0x49,
		},
		.reg_shift = 0,
		.reg_width = 8,
		.label = "horizontal trim width",
	},
	{
		.name = "azoteq,trim-x",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7211A] = 0x68,
		},
		.label = "horizontal trim width",
	},
	{
		.name = "azoteq,trim-y",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7210A] = 0x56,
			[IQS7211E] = 0x49,
		},
		.reg_shift = 8,
		.reg_width = 8,
		.label = "vertical trim height",
	},
	{
		.name = "azoteq,trim-y",
		.reg_addr[IQS7211_REG_GRP_SYS] = {
			[IQS7211A] = 0x69,
		},
		.label = "vertical trim height",
	},
	{
		.name = "azoteq,gesture-max-ms",
		.reg_key = IQS7211_REG_KEY_TAP,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x59,
			[IQS7211A] = 0x81,
			[IQS7211E] = 0x4C,
		},
		.label = "maximum gesture time",
	},
	{
		.name = "azoteq,gesture-mid-ms",
		.reg_key = IQS7211_REG_KEY_TAP,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7211E] = 0x4D,
		},
		.label = "repeated gesture time",
	},
	{
		.name = "azoteq,gesture-dist",
		.reg_key = IQS7211_REG_KEY_TAP,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x5A,
			[IQS7211A] = 0x82,
			[IQS7211E] = 0x4E,
		},
		.label = "gesture distance",
	},
	{
		.name = "azoteq,gesture-dist",
		.reg_key = IQS7211_REG_KEY_HOLD,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x5A,
			[IQS7211A] = 0x82,
			[IQS7211E] = 0x4E,
		},
		.label = "gesture distance",
	},
	{
		.name = "azoteq,gesture-min-ms",
		.reg_key = IQS7211_REG_KEY_HOLD,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x5B,
			[IQS7211A] = 0x83,
			[IQS7211E] = 0x4F,
		},
		.label = "minimum gesture time",
	},
	{
		.name = "azoteq,gesture-max-ms",
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x5C,
			[IQS7211A] = 0x84,
			[IQS7211E] = 0x50,
		},
		.label = "maximum gesture time",
	},
	{
		.name = "azoteq,gesture-max-ms",
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x5C,
			[IQS7211A] = 0x84,
			[IQS7211E] = 0x50,
		},
		.label = "maximum gesture time",
	},
	{
		.name = "azoteq,gesture-dist",
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x5D,
			[IQS7211A] = 0x85,
			[IQS7211E] = 0x51,
		},
		.label = "gesture distance",
	},
	{
		.name = "azoteq,gesture-dist",
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7210A] = 0x5E,
			[IQS7211A] = 0x86,
			[IQS7211E] = 0x52,
		},
		.label = "gesture distance",
	},
	{
		.name = "azoteq,gesture-dist-rep",
		.reg_key = IQS7211_REG_KEY_AXIAL_X,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7211E] = 0x53,
		},
		.label = "repeated gesture distance",
	},
	{
		.name = "azoteq,gesture-dist-rep",
		.reg_key = IQS7211_REG_KEY_AXIAL_Y,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7211E] = 0x54,
		},
		.label = "repeated gesture distance",
	},
	{
		.name = "azoteq,thresh",
		.reg_key = IQS7211_REG_KEY_PALM,
		.reg_addr[IQS7211_REG_GRP_TP] = {
			[IQS7211E] = 0x55,
		},
		.reg_shift = 8,
		.reg_width = 8,
		.val_max = 42,
		.label = "threshold",
	},
};

static const u8 iqs7211_gesture_angle[] = {
	0x00, 0x01, 0x02, 0x03,
	0x04, 0x06, 0x07, 0x08,
	0x09, 0x0A, 0x0B, 0x0C,
	0x0E, 0x0F, 0x10, 0x11,
	0x12, 0x14, 0x15, 0x16,
	0x17, 0x19, 0x1A, 0x1B,
	0x1C, 0x1E, 0x1F, 0x21,
	0x22, 0x23, 0x25, 0x26,
	0x28, 0x2A, 0x2B, 0x2D,
	0x2E, 0x30, 0x32, 0x34,
	0x36, 0x38, 0x3A, 0x3C,
	0x3E, 0x40, 0x42, 0x45,
	0x47, 0x4A, 0x4C, 0x4F,
	0x52, 0x55, 0x58, 0x5B,
	0x5F, 0x63, 0x66, 0x6B,
	0x6F, 0x73, 0x78, 0x7E,
	0x83, 0x89, 0x90, 0x97,
	0x9E, 0xA7, 0xB0, 0xBA,
	0xC5, 0xD1, 0xDF, 0xEF,
};

struct iqs7211_ver_info {
	__le16 prod_num;
	__le16 major;
	__le16 minor;
	__le32 patch;
} __packed;

struct iqs7211_touch_data {
	__le16 abs_x;
	__le16 abs_y;
	__le16 pressure;
	__le16 area;
} __packed;

struct iqs7211_tp_config {
	u8 tp_settings;
	u8 total_rx;
	u8 total_tx;
	u8 num_contacts;
	__le16 max_x;
	__le16 max_y;
} __packed;

struct iqs7211_private {
	const struct iqs7211_dev_desc *dev_desc;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *irq_gpio;
	struct i2c_client *client;
	struct input_dev *tp_idev;
	struct input_dev *kp_idev;
	struct iqs7211_ver_info ver_info;
	struct iqs7211_tp_config tp_config;
	struct touchscreen_properties prop;
	struct list_head reg_field_head;
	enum iqs7211_comms_mode comms_init;
	enum iqs7211_comms_mode comms_mode;
	unsigned int num_contacts;
	unsigned int kp_code[ARRAY_SIZE(iqs7211e_kp_events)];
	u8 rx_tx_map[IQS7211_MAX_CTX + 1];
	u8 cycle_alloc[2][33];
	u8 exp_file[2];
	u16 event_mask;
	u16 ati_start;
	u16 gesture_cache;
};

static int iqs7211_irq_poll(struct iqs7211_private *iqs7211, u64 timeout_us)
{
	int error, val;

	error = readx_poll_timeout(gpiod_get_value_cansleep, iqs7211->irq_gpio,
				   val, val, IQS7211_COMMS_SLEEP_US, timeout_us);

	return val < 0 ? val : error;
}

static int iqs7211_hard_reset(struct iqs7211_private *iqs7211)
{
	if (!iqs7211->reset_gpio)
		return 0;

	gpiod_set_value_cansleep(iqs7211->reset_gpio, 1);

	/*
	 * The following delay ensures the shared RDY/MCLR pin is sampled in
	 * between periodic assertions by the device and assumes the default
	 * communication timeout has not been overwritten in OTP memory.
	 */
	if (iqs7211->reset_gpio == iqs7211->irq_gpio)
		msleep(IQS7211_RESET_TIMEOUT_MS);
	else
		usleep_range(1000, 1100);

	gpiod_set_value_cansleep(iqs7211->reset_gpio, 0);
	if (iqs7211->reset_gpio == iqs7211->irq_gpio)
		iqs7211_irq_wait();

	return iqs7211_irq_poll(iqs7211, IQS7211_START_TIMEOUT_US);
}

static int iqs7211_force_comms(struct iqs7211_private *iqs7211)
{
	u8 msg_buf[] = { 0xFF, };
	int ret;

	switch (iqs7211->comms_mode) {
	case IQS7211_COMMS_MODE_WAIT:
		return iqs7211_irq_poll(iqs7211, IQS7211_START_TIMEOUT_US);

	case IQS7211_COMMS_MODE_FREE:
		return 0;

	case IQS7211_COMMS_MODE_FORCE:
		break;

	default:
		return -EINVAL;
	}

	/*
	 * The device cannot communicate until it asserts its interrupt (RDY)
	 * pin. Attempts to do so while RDY is deasserted return an ACK; how-
	 * ever all write data is ignored, and all read data returns 0xEE.
	 *
	 * Unsolicited communication must be preceded by a special force com-
	 * munication command, after which the device eventually asserts its
	 * RDY pin and agrees to communicate.
	 *
	 * Regardless of whether communication is forced or the result of an
	 * interrupt, the device automatically deasserts its RDY pin once it
	 * detects an I2C stop condition, or a timeout expires.
	 */
	ret = gpiod_get_value_cansleep(iqs7211->irq_gpio);
	if (ret < 0)
		return ret;
	else if (ret > 0)
		return 0;

	ret = i2c_master_send(iqs7211->client, msg_buf, sizeof(msg_buf));
	if (ret < (int)sizeof(msg_buf)) {
		if (ret >= 0)
			ret = -EIO;

		msleep(IQS7211_COMMS_RETRY_MS);
		return ret;
	}

	iqs7211_irq_wait();

	return iqs7211_irq_poll(iqs7211, IQS7211_COMMS_TIMEOUT_US);
}

static int iqs7211_read_burst(struct iqs7211_private *iqs7211,
			      u8 reg, void *val, u16 val_len)
{
	int ret, i;
	struct i2c_client *client = iqs7211->client;
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(reg),
			.buf = &reg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = val_len,
			.buf = (u8 *)val,
		},
	};

	/*
	 * The following loop protects against an edge case in which the RDY
	 * pin is automatically deasserted just as the read is initiated. In
	 * that case, the read must be retried using forced communication.
	 */
	for (i = 0; i < IQS7211_NUM_RETRIES; i++) {
		ret = iqs7211_force_comms(iqs7211);
		if (ret < 0)
			continue;

		ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (ret < (int)ARRAY_SIZE(msg)) {
			if (ret >= 0)
				ret = -EIO;

			msleep(IQS7211_COMMS_RETRY_MS);
			continue;
		}

		if (get_unaligned_le16(msg[1].buf) == IQS7211_COMMS_ERROR) {
			ret = -ENODATA;
			continue;
		}

		ret = 0;
		break;
	}

	iqs7211_irq_wait();

	if (ret < 0)
		dev_err(&client->dev,
			"Failed to read from address 0x%02X: %d\n", reg, ret);

	return ret;
}

static int iqs7211_read_word(struct iqs7211_private *iqs7211, u8 reg, u16 *val)
{
	__le16 val_buf;
	int error;

	error = iqs7211_read_burst(iqs7211, reg, &val_buf, sizeof(val_buf));
	if (error)
		return error;

	*val = le16_to_cpu(val_buf);

	return 0;
}

static int iqs7211_write_burst(struct iqs7211_private *iqs7211,
			       u8 reg, const void *val, u16 val_len)
{
	int msg_len = sizeof(reg) + val_len;
	int ret, i;
	struct i2c_client *client = iqs7211->client;
	u8 *msg_buf;

	msg_buf = kzalloc(msg_len, GFP_KERNEL);
	if (!msg_buf)
		return -ENOMEM;

	*msg_buf = reg;
	memcpy(msg_buf + sizeof(reg), val, val_len);

	/*
	 * The following loop protects against an edge case in which the RDY
	 * pin is automatically asserted just before the force communication
	 * command is sent.
	 *
	 * In that case, the subsequent I2C stop condition tricks the device
	 * into preemptively deasserting the RDY pin and the command must be
	 * sent again.
	 */
	for (i = 0; i < IQS7211_NUM_RETRIES; i++) {
		ret = iqs7211_force_comms(iqs7211);
		if (ret < 0)
			continue;

		ret = i2c_master_send(client, msg_buf, msg_len);
		if (ret < msg_len) {
			if (ret >= 0)
				ret = -EIO;

			msleep(IQS7211_COMMS_RETRY_MS);
			continue;
		}

		ret = 0;
		break;
	}

	kfree(msg_buf);

	iqs7211_irq_wait();

	if (ret < 0)
		dev_err(&client->dev,
			"Failed to write to address 0x%02X: %d\n", reg, ret);

	return ret;
}

static int iqs7211_write_word(struct iqs7211_private *iqs7211, u8 reg, u16 val)
{
	__le16 val_buf = cpu_to_le16(val);

	return iqs7211_write_burst(iqs7211, reg, &val_buf, sizeof(val_buf));
}

static int iqs7211_start_comms(struct iqs7211_private *iqs7211)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct i2c_client *client = iqs7211->client;
	bool forced_comms;
	unsigned int val;
	u16 comms_setup;
	int error;

	/*
	 * Until forced communication can be enabled, the host must wait for a
	 * communication window each time it intends to elicit a response from
	 * the device.
	 *
	 * Forced communication is not necessary, however, if the host adapter
	 * can support clock stretching. In that case, the device freely clock
	 * stretches until all pending conversions are complete.
	 */
	forced_comms = device_property_present(&client->dev,
					       "azoteq,forced-comms");

	error = device_property_read_u32(&client->dev,
					 "azoteq,forced-comms-default", &val);
	if (error == -EINVAL) {
		iqs7211->comms_init = IQS7211_COMMS_MODE_WAIT;
	} else if (error) {
		dev_err(&client->dev,
			"Failed to read default communication mode: %d\n",
			error);
		return error;
	} else if (val) {
		iqs7211->comms_init = forced_comms ? IQS7211_COMMS_MODE_FORCE
						   : IQS7211_COMMS_MODE_WAIT;
	} else {
		iqs7211->comms_init = forced_comms ? IQS7211_COMMS_MODE_WAIT
						   : IQS7211_COMMS_MODE_FREE;
	}

	iqs7211->comms_mode = iqs7211->comms_init;

	error = iqs7211_hard_reset(iqs7211);
	if (error) {
		dev_err(&client->dev, "Failed to reset device: %d\n", error);
		return error;
	}

	error = iqs7211_read_burst(iqs7211, IQS7211_PROD_NUM,
				   &iqs7211->ver_info,
				   sizeof(iqs7211->ver_info));
	if (error)
		return error;

	if (le16_to_cpu(iqs7211->ver_info.prod_num) != dev_desc->prod_num) {
		dev_err(&client->dev, "Invalid product number: %u\n",
			le16_to_cpu(iqs7211->ver_info.prod_num));
		return -EINVAL;
	}

	error = iqs7211_read_word(iqs7211, dev_desc->sys_ctrl + 1,
				  &comms_setup);
	if (error)
		return error;

	if (forced_comms)
		comms_setup |= dev_desc->comms_req;
	else
		comms_setup &= ~dev_desc->comms_req;

	error = iqs7211_write_word(iqs7211, dev_desc->sys_ctrl + 1,
				   comms_setup | dev_desc->comms_end);
	if (error)
		return error;

	if (forced_comms)
		iqs7211->comms_mode = IQS7211_COMMS_MODE_FORCE;
	else
		iqs7211->comms_mode = IQS7211_COMMS_MODE_FREE;

	error = iqs7211_read_burst(iqs7211, dev_desc->exp_file,
				   iqs7211->exp_file,
				   sizeof(iqs7211->exp_file));
	if (error)
		return error;

	error = iqs7211_read_burst(iqs7211, dev_desc->tp_config,
				   &iqs7211->tp_config,
				   sizeof(iqs7211->tp_config));
	if (error)
		return error;

	error = iqs7211_write_word(iqs7211, dev_desc->sys_ctrl + 1,
				   comms_setup);
	if (error)
		return error;

	iqs7211->event_mask = comms_setup & ~IQS7211_EVENT_MASK_ALL;
	iqs7211->event_mask |= (IQS7211_EVENT_MASK_ATI | IQS7211_EVENT_MODE);

	return 0;
}

static int iqs7211_init_device(struct iqs7211_private *iqs7211)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct iqs7211_reg_field_desc *reg_field;
	__le16 sys_ctrl[] = {
		cpu_to_le16(dev_desc->ack_reset),
		cpu_to_le16(iqs7211->event_mask),
	};
	int error, i;

	/*
	 * Acknowledge reset before writing any registers in case the device
	 * suffers a spurious reset during initialization. The communication
	 * mode is configured at this time as well.
	 */
	error = iqs7211_write_burst(iqs7211, dev_desc->sys_ctrl, sys_ctrl,
				    sizeof(sys_ctrl));
	if (error)
		return error;

	if (iqs7211->event_mask & dev_desc->comms_req)
		iqs7211->comms_mode = IQS7211_COMMS_MODE_FORCE;
	else
		iqs7211->comms_mode = IQS7211_COMMS_MODE_FREE;

	/*
	 * Take advantage of the stop-bit disable function, if available, to
	 * save the trouble of having to reopen a communication window after
	 * each read or write.
	 */
	error = iqs7211_write_word(iqs7211, dev_desc->sys_ctrl + 1,
				   iqs7211->event_mask | dev_desc->comms_end);
	if (error)
		return error;

	list_for_each_entry(reg_field, &iqs7211->reg_field_head, list) {
		u16 new_val = reg_field->val;

		if (reg_field->mask < U16_MAX) {
			u16 old_val;

			error = iqs7211_read_word(iqs7211, reg_field->addr,
						  &old_val);
			if (error)
				return error;

			new_val = old_val & ~reg_field->mask;
			new_val |= reg_field->val;

			if (new_val == old_val)
				continue;
		}

		error = iqs7211_write_word(iqs7211, reg_field->addr, new_val);
		if (error)
			return error;
	}

	error = iqs7211_write_burst(iqs7211, dev_desc->tp_config,
				    &iqs7211->tp_config,
				    sizeof(iqs7211->tp_config));
	if (error)
		return error;

	if (**iqs7211->cycle_alloc) {
		error = iqs7211_write_burst(iqs7211, dev_desc->rx_tx_map,
					    &iqs7211->rx_tx_map,
					    dev_desc->num_ctx);
		if (error)
			return error;

		for (i = 0; i < sizeof(dev_desc->cycle_limit); i++) {
			error = iqs7211_write_burst(iqs7211,
						    dev_desc->cycle_alloc[i],
						    iqs7211->cycle_alloc[i],
						    dev_desc->cycle_limit[i] * 3);
			if (error)
				return error;
		}
	}

	*sys_ctrl = cpu_to_le16(iqs7211->ati_start);

	return iqs7211_write_burst(iqs7211, dev_desc->sys_ctrl, sys_ctrl,
				   sizeof(sys_ctrl));
}

static int iqs7211_add_field(struct iqs7211_private *iqs7211,
			     struct iqs7211_reg_field_desc new_field)
{
	struct i2c_client *client = iqs7211->client;
	struct iqs7211_reg_field_desc *reg_field;

	if (!new_field.addr)
		return 0;

	list_for_each_entry(reg_field, &iqs7211->reg_field_head, list) {
		if (reg_field->addr != new_field.addr)
			continue;

		reg_field->mask |= new_field.mask;
		reg_field->val |= new_field.val;
		return 0;
	}

	reg_field = devm_kzalloc(&client->dev, sizeof(*reg_field), GFP_KERNEL);
	if (!reg_field)
		return -ENOMEM;

	reg_field->addr = new_field.addr;
	reg_field->mask = new_field.mask;
	reg_field->val = new_field.val;

	list_add(&reg_field->list, &iqs7211->reg_field_head);

	return 0;
}

static int iqs7211_parse_props(struct iqs7211_private *iqs7211,
			       struct fwnode_handle *reg_grp_node,
			       enum iqs7211_reg_grp_id reg_grp,
			       enum iqs7211_reg_key_id reg_key)
{
	struct i2c_client *client = iqs7211->client;
	int i;

	for (i = 0; i < ARRAY_SIZE(iqs7211_props); i++) {
		const char *name = iqs7211_props[i].name;
		u8 reg_addr = iqs7211_props[i].reg_addr[reg_grp]
						       [iqs7211->dev_desc -
							iqs7211_devs];
		int reg_shift = iqs7211_props[i].reg_shift;
		int reg_width = iqs7211_props[i].reg_width ? : 16;
		int val_pitch = iqs7211_props[i].val_pitch ? : 1;
		int val_min = iqs7211_props[i].val_min;
		int val_max = iqs7211_props[i].val_max;
		const char *label = iqs7211_props[i].label ? : name;
		struct iqs7211_reg_field_desc reg_field;
		unsigned int val;
		int error;

		if (iqs7211_props[i].reg_key != reg_key)
			continue;

		if (!reg_addr)
			continue;

		error = fwnode_property_read_u32(reg_grp_node, name, &val);
		if (error == -EINVAL) {
			continue;
		} else if (error) {
			dev_err(&client->dev, "Failed to read %s %s: %d\n",
				fwnode_get_name(reg_grp_node), label, error);
			return error;
		}

		if (!val_max)
			val_max = GENMASK(reg_width - 1, 0) * val_pitch;

		if (val < val_min || val > val_max) {
			dev_err(&client->dev, "Invalid %s: %u\n", label, val);
			return -EINVAL;
		}

		reg_field.addr = reg_addr;
		reg_field.mask = GENMASK(reg_shift + reg_width - 1, reg_shift);
		reg_field.val = val / val_pitch << reg_shift;

		error = iqs7211_add_field(iqs7211, reg_field);
		if (error)
			return error;
	}

	return 0;
}

static int iqs7211_parse_event(struct iqs7211_private *iqs7211,
			       struct fwnode_handle *event_node,
			       enum iqs7211_reg_grp_id reg_grp,
			       enum iqs7211_reg_key_id reg_key,
			       unsigned int *event_code)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct i2c_client *client = iqs7211->client;
	struct iqs7211_reg_field_desc reg_field;
	unsigned int val;
	int error;

	error = iqs7211_parse_props(iqs7211, event_node, reg_grp, reg_key);
	if (error)
		return error;

	if (reg_key == IQS7211_REG_KEY_AXIAL_X ||
	    reg_key == IQS7211_REG_KEY_AXIAL_Y) {
		error = fwnode_property_read_u32(event_node,
						 "azoteq,gesture-angle", &val);
		if (!error) {
			if (val >= ARRAY_SIZE(iqs7211_gesture_angle)) {
				dev_err(&client->dev,
					"Invalid %s gesture angle: %u\n",
					fwnode_get_name(event_node), val);
				return -EINVAL;
			}

			reg_field.addr = dev_desc->gesture_angle;
			reg_field.mask = U8_MAX;
			reg_field.val = iqs7211_gesture_angle[val];

			error = iqs7211_add_field(iqs7211, reg_field);
			if (error)
				return error;
		} else if (error != -EINVAL) {
			dev_err(&client->dev,
				"Failed to read %s gesture angle: %d\n",
				fwnode_get_name(event_node), error);
			return error;
		}
	}

	error = fwnode_property_read_u32(event_node, "linux,code", event_code);
	if (error == -EINVAL)
		error = 0;
	else if (error)
		dev_err(&client->dev, "Failed to read %s code: %d\n",
			fwnode_get_name(event_node), error);

	return error;
}

static int iqs7211_parse_cycles(struct iqs7211_private *iqs7211,
				struct fwnode_handle *tp_node)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct i2c_client *client = iqs7211->client;
	int num_cycles = dev_desc->cycle_limit[0] + dev_desc->cycle_limit[1];
	int error, count, i, j, k, cycle_start;
	unsigned int cycle_alloc[IQS7211_MAX_CYCLES][2];
	u8 total_rx = iqs7211->tp_config.total_rx;
	u8 total_tx = iqs7211->tp_config.total_tx;

	for (i = 0; i < IQS7211_MAX_CYCLES * 2; i++)
		*(cycle_alloc[0] + i) = U8_MAX;

	count = fwnode_property_count_u32(tp_node, "azoteq,channel-select");
	if (count == -EINVAL) {
		/*
		 * Assign each sensing cycle's slots (0 and 1) to a channel,
		 * defined as the intersection between two CRx and CTx pins.
		 * A channel assignment of 255 means the slot is unused.
		 */
		for (i = 0, cycle_start = 0; i < total_tx; i++) {
			int cycle_stop = 0;

			for (j = 0; j < total_rx; j++) {
				/*
				 * Channels formed by CRx0-3 and CRx4-7 are
				 * bound to slots 0 and 1, respectively.
				 */
				int slot = iqs7211->rx_tx_map[j] < 4 ? 0 : 1;
				int chan = i * total_rx + j;

				for (k = cycle_start; k < num_cycles; k++) {
					if (cycle_alloc[k][slot] < U8_MAX)
						continue;

					cycle_alloc[k][slot] = chan;
					break;
				}

				if (k < num_cycles) {
					cycle_stop = max(k, cycle_stop);
					continue;
				}

				dev_err(&client->dev,
					"Insufficient number of cycles\n");
				return -EINVAL;
			}

			/*
			 * Sensing cycles cannot straddle more than one CTx
			 * pin. As such, the next row's starting cycle must
			 * be greater than the previous row's highest cycle.
			 */
			cycle_start = cycle_stop + 1;
		}
	} else if (count < 0) {
		dev_err(&client->dev, "Failed to count channels: %d\n", count);
		return count;
	} else if (count > num_cycles * 2) {
		dev_err(&client->dev, "Insufficient number of cycles\n");
		return -EINVAL;
	} else if (count > 0) {
		error = fwnode_property_read_u32_array(tp_node,
						       "azoteq,channel-select",
						       cycle_alloc[0], count);
		if (error) {
			dev_err(&client->dev, "Failed to read channels: %d\n",
				error);
			return error;
		}

		for (i = 0; i < count; i++) {
			int chan = *(cycle_alloc[0] + i);

			if (chan == U8_MAX)
				continue;

			if (chan >= total_rx * total_tx) {
				dev_err(&client->dev, "Invalid channel: %d\n",
					chan);
				return -EINVAL;
			}

			for (j = 0; j < count; j++) {
				if (j == i || *(cycle_alloc[0] + j) != chan)
					continue;

				dev_err(&client->dev, "Duplicate channel: %d\n",
					chan);
				return -EINVAL;
			}
		}
	}

	/*
	 * Once the raw channel assignments have been derived, they must be
	 * packed according to the device's register map.
	 */
	for (i = 0, cycle_start = 0; i < sizeof(dev_desc->cycle_limit); i++) {
		int offs = 0;

		for (j = cycle_start;
		     j < cycle_start + dev_desc->cycle_limit[i]; j++) {
			iqs7211->cycle_alloc[i][offs++] = 0x05;
			iqs7211->cycle_alloc[i][offs++] = cycle_alloc[j][0];
			iqs7211->cycle_alloc[i][offs++] = cycle_alloc[j][1];
		}

		cycle_start += dev_desc->cycle_limit[i];
	}

	return 0;
}

static int iqs7211_parse_tp(struct iqs7211_private *iqs7211,
			    struct fwnode_handle *tp_node)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct i2c_client *client = iqs7211->client;
	unsigned int pins[IQS7211_MAX_CTX];
	int error, count, i, j;

	count = fwnode_property_count_u32(tp_node, "azoteq,rx-enable");
	if (count == -EINVAL) {
		return 0;
	} else if (count < 0) {
		dev_err(&client->dev, "Failed to count CRx pins: %d\n", count);
		return count;
	} else if (count > IQS7211_NUM_CRX) {
		dev_err(&client->dev, "Invalid number of CRx pins\n");
		return -EINVAL;
	}

	error = fwnode_property_read_u32_array(tp_node, "azoteq,rx-enable",
					       pins, count);
	if (error) {
		dev_err(&client->dev, "Failed to read CRx pins: %d\n", error);
		return error;
	}

	for (i = 0; i < count; i++) {
		if (pins[i] >= IQS7211_NUM_CRX) {
			dev_err(&client->dev, "Invalid CRx pin: %u\n", pins[i]);
			return -EINVAL;
		}

		iqs7211->rx_tx_map[i] = pins[i];
	}

	iqs7211->tp_config.total_rx = count;

	count = fwnode_property_count_u32(tp_node, "azoteq,tx-enable");
	if (count < 0) {
		dev_err(&client->dev, "Failed to count CTx pins: %d\n", count);
		return count;
	} else if (count > dev_desc->num_ctx) {
		dev_err(&client->dev, "Invalid number of CTx pins\n");
		return -EINVAL;
	}

	error = fwnode_property_read_u32_array(tp_node, "azoteq,tx-enable",
					       pins, count);
	if (error) {
		dev_err(&client->dev, "Failed to read CTx pins: %d\n", error);
		return error;
	}

	for (i = 0; i < count; i++) {
		if (pins[i] >= dev_desc->num_ctx) {
			dev_err(&client->dev, "Invalid CTx pin: %u\n", pins[i]);
			return -EINVAL;
		}

		for (j = 0; j < iqs7211->tp_config.total_rx; j++) {
			if (iqs7211->rx_tx_map[j] != pins[i])
				continue;

			dev_err(&client->dev, "Conflicting CTx pin: %u\n",
				pins[i]);
			return -EINVAL;
		}

		iqs7211->rx_tx_map[iqs7211->tp_config.total_rx + i] = pins[i];
	}

	iqs7211->tp_config.total_tx = count;

	return iqs7211_parse_cycles(iqs7211, tp_node);
}

static int iqs7211_parse_alp(struct iqs7211_private *iqs7211,
			     struct fwnode_handle *alp_node)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct i2c_client *client = iqs7211->client;
	struct iqs7211_reg_field_desc reg_field;
	int error, count, i;

	count = fwnode_property_count_u32(alp_node, "azoteq,rx-enable");
	if (count < 0 && count != -EINVAL) {
		dev_err(&client->dev, "Failed to count CRx pins: %d\n", count);
		return count;
	} else if (count > IQS7211_NUM_CRX) {
		dev_err(&client->dev, "Invalid number of CRx pins\n");
		return -EINVAL;
	} else if (count >= 0) {
		unsigned int pins[IQS7211_NUM_CRX];

		error = fwnode_property_read_u32_array(alp_node,
						       "azoteq,rx-enable",
						       pins, count);
		if (error) {
			dev_err(&client->dev, "Failed to read CRx pins: %d\n",
				error);
			return error;
		}

		reg_field.addr = dev_desc->alp_config;
		reg_field.mask = GENMASK(IQS7211_NUM_CRX - 1, 0);
		reg_field.val = 0;

		for (i = 0; i < count; i++) {
			if (pins[i] < dev_desc->min_crx_alp ||
			    pins[i] >= IQS7211_NUM_CRX) {
				dev_err(&client->dev, "Invalid CRx pin: %u\n",
					pins[i]);
				return -EINVAL;
			}

			reg_field.val |= BIT(pins[i]);
		}

		error = iqs7211_add_field(iqs7211, reg_field);
		if (error)
			return error;
	}

	count = fwnode_property_count_u32(alp_node, "azoteq,tx-enable");
	if (count < 0 && count != -EINVAL) {
		dev_err(&client->dev, "Failed to count CTx pins: %d\n", count);
		return count;
	} else if (count > dev_desc->num_ctx) {
		dev_err(&client->dev, "Invalid number of CTx pins\n");
		return -EINVAL;
	} else if (count >= 0) {
		unsigned int pins[IQS7211_MAX_CTX];

		error = fwnode_property_read_u32_array(alp_node,
						       "azoteq,tx-enable",
						       pins, count);
		if (error) {
			dev_err(&client->dev, "Failed to read CTx pins: %d\n",
				error);
			return error;
		}

		reg_field.addr = dev_desc->alp_config + 1;
		reg_field.mask = GENMASK(dev_desc->num_ctx - 1, 0);
		reg_field.val = 0;

		for (i = 0; i < count; i++) {
			if (pins[i] >= dev_desc->num_ctx) {
				dev_err(&client->dev, "Invalid CTx pin: %u\n",
					pins[i]);
				return -EINVAL;
			}

			reg_field.val |= BIT(pins[i]);
		}

		error = iqs7211_add_field(iqs7211, reg_field);
		if (error)
			return error;
	}

	return 0;
}

static int (*iqs7211_parse_extra[IQS7211_NUM_REG_GRPS])
				(struct iqs7211_private *iqs7211,
				 struct fwnode_handle *reg_grp_node) = {
	[IQS7211_REG_GRP_TP] = iqs7211_parse_tp,
	[IQS7211_REG_GRP_ALP] = iqs7211_parse_alp,
};

static int iqs7211_parse_reg_grp(struct iqs7211_private *iqs7211,
				 struct fwnode_handle *reg_grp_node,
				 enum iqs7211_reg_grp_id reg_grp)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct iqs7211_reg_field_desc reg_field;
	int error, i;

	error = iqs7211_parse_props(iqs7211, reg_grp_node, reg_grp,
				    IQS7211_REG_KEY_NONE);
	if (error)
		return error;

	if (iqs7211_parse_extra[reg_grp]) {
		error = iqs7211_parse_extra[reg_grp](iqs7211, reg_grp_node);
		if (error)
			return error;
	}

	iqs7211->ati_start |= dev_desc->ati_start[reg_grp];

	reg_field.addr = dev_desc->kp_enable[reg_grp];
	reg_field.mask = 0;
	reg_field.val = 0;

	for (i = 0; i < dev_desc->num_kp_events; i++) {
		const char *event_name = dev_desc->kp_events[i].name;
		struct fwnode_handle *event_node;

		if (dev_desc->kp_events[i].reg_grp != reg_grp)
			continue;

		reg_field.mask |= dev_desc->kp_events[i].enable;

		if (event_name)
			event_node = fwnode_get_named_child_node(reg_grp_node,
								 event_name);
		else
			event_node = fwnode_handle_get(reg_grp_node);

		if (!event_node)
			continue;

		error = iqs7211_parse_event(iqs7211, event_node,
					    dev_desc->kp_events[i].reg_grp,
					    dev_desc->kp_events[i].reg_key,
					    &iqs7211->kp_code[i]);
		fwnode_handle_put(event_node);
		if (error)
			return error;

		reg_field.val |= dev_desc->kp_events[i].enable;

		iqs7211->event_mask |= iqs7211_reg_grp_masks[reg_grp];
	}

	return iqs7211_add_field(iqs7211, reg_field);
}

static int iqs7211_register_kp(struct iqs7211_private *iqs7211)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct input_dev *kp_idev = iqs7211->kp_idev;
	struct i2c_client *client = iqs7211->client;
	int error, i;

	for (i = 0; i < dev_desc->num_kp_events; i++)
		if (iqs7211->kp_code[i])
			break;

	if (i == dev_desc->num_kp_events)
		return 0;

	kp_idev = devm_input_allocate_device(&client->dev);
	if (!kp_idev)
		return -ENOMEM;

	iqs7211->kp_idev = kp_idev;

	kp_idev->name = dev_desc->kp_name;
	kp_idev->id.bustype = BUS_I2C;

	for (i = 0; i < dev_desc->num_kp_events; i++)
		if (iqs7211->kp_code[i])
			input_set_capability(iqs7211->kp_idev, EV_KEY,
					     iqs7211->kp_code[i]);

	error = input_register_device(kp_idev);
	if (error)
		dev_err(&client->dev, "Failed to register %s: %d\n",
			kp_idev->name, error);

	return error;
}

static int iqs7211_register_tp(struct iqs7211_private *iqs7211)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct touchscreen_properties *prop = &iqs7211->prop;
	struct input_dev *tp_idev = iqs7211->tp_idev;
	struct i2c_client *client = iqs7211->client;
	int error;

	error = device_property_read_u32(&client->dev, "azoteq,num-contacts",
					 &iqs7211->num_contacts);
	if (error == -EINVAL) {
		return 0;
	} else if (error) {
		dev_err(&client->dev, "Failed to read number of contacts: %d\n",
			error);
		return error;
	} else if (iqs7211->num_contacts > IQS7211_MAX_CONTACTS) {
		dev_err(&client->dev, "Invalid number of contacts: %u\n",
			iqs7211->num_contacts);
		return -EINVAL;
	}

	iqs7211->tp_config.num_contacts = iqs7211->num_contacts ? : 1;

	if (!iqs7211->num_contacts)
		return 0;

	iqs7211->event_mask |= IQS7211_EVENT_MASK_MOVE;

	tp_idev = devm_input_allocate_device(&client->dev);
	if (!tp_idev)
		return -ENOMEM;

	iqs7211->tp_idev = tp_idev;

	tp_idev->name = dev_desc->tp_name;
	tp_idev->id.bustype = BUS_I2C;

	input_set_abs_params(tp_idev, ABS_MT_POSITION_X,
			     0, le16_to_cpu(iqs7211->tp_config.max_x), 0, 0);

	input_set_abs_params(tp_idev, ABS_MT_POSITION_Y,
			     0, le16_to_cpu(iqs7211->tp_config.max_y), 0, 0);

	input_set_abs_params(tp_idev, ABS_MT_PRESSURE, 0, U16_MAX, 0, 0);

	touchscreen_parse_properties(tp_idev, true, prop);

	/*
	 * The device reserves 0xFFFF for coordinates that correspond to slots
	 * which are not in a state of touch.
	 */
	if (prop->max_x >= U16_MAX || prop->max_y >= U16_MAX) {
		dev_err(&client->dev, "Invalid trackpad size: %u*%u\n",
			prop->max_x, prop->max_y);
		return -EINVAL;
	}

	iqs7211->tp_config.max_x = cpu_to_le16(prop->max_x);
	iqs7211->tp_config.max_y = cpu_to_le16(prop->max_y);

	error = input_mt_init_slots(tp_idev, iqs7211->num_contacts,
				    INPUT_MT_DIRECT);
	if (error) {
		dev_err(&client->dev, "Failed to initialize slots: %d\n",
			error);
		return error;
	}

	error = input_register_device(tp_idev);
	if (error)
		dev_err(&client->dev, "Failed to register %s: %d\n",
			tp_idev->name, error);

	return error;
}

static int iqs7211_report(struct iqs7211_private *iqs7211)
{
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	struct i2c_client *client = iqs7211->client;
	struct iqs7211_touch_data *touch_data;
	u16 info_flags, charge_mode, gesture_flags;
	__le16 status[12];
	int error, i;

	error = iqs7211_read_burst(iqs7211, dev_desc->sys_stat, status,
				   dev_desc->contact_offs * sizeof(__le16) +
				   iqs7211->num_contacts * sizeof(*touch_data));
	if (error)
		return error;

	info_flags = le16_to_cpu(status[dev_desc->info_offs]);

	if (info_flags & dev_desc->show_reset) {
		dev_err(&client->dev, "Unexpected device reset\n");

		/*
		 * The device may or may not expect forced communication after
		 * it exits hardware reset, so the corresponding state machine
		 * must be reset as well.
		 */
		iqs7211->comms_mode = iqs7211->comms_init;

		return iqs7211_init_device(iqs7211);
	}

	for (i = 0; i < ARRAY_SIZE(dev_desc->ati_error); i++) {
		if (!(info_flags & dev_desc->ati_error[i]))
			continue;

		dev_err(&client->dev, "Unexpected %s ATI error\n",
			iqs7211_reg_grp_names[i]);
		return 0;
	}

	for (i = 0; i < iqs7211->num_contacts; i++) {
		u16 pressure;

		touch_data = (struct iqs7211_touch_data *)
			     &status[dev_desc->contact_offs] + i;
		pressure = le16_to_cpu(touch_data->pressure);

		input_mt_slot(iqs7211->tp_idev, i);
		if (input_mt_report_slot_state(iqs7211->tp_idev, MT_TOOL_FINGER,
					       pressure != 0)) {
			touchscreen_report_pos(iqs7211->tp_idev, &iqs7211->prop,
					       le16_to_cpu(touch_data->abs_x),
					       le16_to_cpu(touch_data->abs_y),
					       true);
			input_report_abs(iqs7211->tp_idev, ABS_MT_PRESSURE,
					 pressure);
		}
	}

	if (iqs7211->num_contacts) {
		input_mt_sync_frame(iqs7211->tp_idev);
		input_sync(iqs7211->tp_idev);
	}

	if (!iqs7211->kp_idev)
		return 0;

	charge_mode = info_flags & GENMASK(dev_desc->charge_shift + 2,
					   dev_desc->charge_shift);
	charge_mode >>= dev_desc->charge_shift;

	/*
	 * A charging mode higher than 2 (idle mode) indicates the device last
	 * operated in low-power mode and intends to express an ALP event.
	 */
	if (info_flags & dev_desc->kp_events->mask && charge_mode > 2) {
		input_report_key(iqs7211->kp_idev, *iqs7211->kp_code, 1);
		input_sync(iqs7211->kp_idev);

		input_report_key(iqs7211->kp_idev, *iqs7211->kp_code, 0);
	}

	for (i = 0; i < dev_desc->num_kp_events; i++) {
		if (dev_desc->kp_events[i].reg_grp != IQS7211_REG_GRP_BTN)
			continue;

		input_report_key(iqs7211->kp_idev, iqs7211->kp_code[i],
				 info_flags & dev_desc->kp_events[i].mask);
	}

	gesture_flags = le16_to_cpu(status[dev_desc->gesture_offs]);

	for (i = 0; i < dev_desc->num_kp_events; i++) {
		enum iqs7211_reg_key_id reg_key = dev_desc->kp_events[i].reg_key;
		u16 mask = dev_desc->kp_events[i].mask;

		if (dev_desc->kp_events[i].reg_grp != IQS7211_REG_GRP_TP)
			continue;

		if ((gesture_flags ^ iqs7211->gesture_cache) & mask)
			input_report_key(iqs7211->kp_idev, iqs7211->kp_code[i],
					 gesture_flags & mask);

		iqs7211->gesture_cache &= ~mask;

		/*
		 * Hold and palm gestures persist while the contact remains in
		 * place; all others are momentary and hence are followed by a
		 * complementary release event.
		 */
		if (reg_key == IQS7211_REG_KEY_HOLD ||
		    reg_key == IQS7211_REG_KEY_PALM) {
			iqs7211->gesture_cache |= gesture_flags & mask;
			gesture_flags &= ~mask;
		}
	}

	if (gesture_flags) {
		input_sync(iqs7211->kp_idev);

		for (i = 0; i < dev_desc->num_kp_events; i++)
			if (dev_desc->kp_events[i].reg_grp == IQS7211_REG_GRP_TP &&
			    gesture_flags & dev_desc->kp_events[i].mask)
				input_report_key(iqs7211->kp_idev,
						 iqs7211->kp_code[i], 0);
	}

	input_sync(iqs7211->kp_idev);

	return 0;
}

static irqreturn_t iqs7211_irq(int irq, void *context)
{
	struct iqs7211_private *iqs7211 = context;

	return iqs7211_report(iqs7211) ? IRQ_NONE : IRQ_HANDLED;
}

static int iqs7211_suspend(struct device *dev)
{
	struct iqs7211_private *iqs7211 = dev_get_drvdata(dev);
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	int error;

	if (!dev_desc->suspend || device_may_wakeup(dev))
		return 0;

	/*
	 * I2C communication prompts the device to assert its RDY pin if it is
	 * not already asserted. As such, the interrupt must be disabled so as
	 * to prevent reentrant interrupts.
	 */
	disable_irq(gpiod_to_irq(iqs7211->irq_gpio));

	error = iqs7211_write_word(iqs7211, dev_desc->sys_ctrl,
				   dev_desc->suspend);

	enable_irq(gpiod_to_irq(iqs7211->irq_gpio));

	return error;
}

static int iqs7211_resume(struct device *dev)
{
	struct iqs7211_private *iqs7211 = dev_get_drvdata(dev);
	const struct iqs7211_dev_desc *dev_desc = iqs7211->dev_desc;
	__le16 sys_ctrl[] = {
		0,
		cpu_to_le16(iqs7211->event_mask),
	};
	int error;

	if (!dev_desc->suspend || device_may_wakeup(dev))
		return 0;

	disable_irq(gpiod_to_irq(iqs7211->irq_gpio));

	/*
	 * Forced communication, if in use, must be explicitly enabled as part
	 * of the wake-up command.
	 */
	error = iqs7211_write_burst(iqs7211, dev_desc->sys_ctrl, sys_ctrl,
				    sizeof(sys_ctrl));

	enable_irq(gpiod_to_irq(iqs7211->irq_gpio));

	return error;
}

static DEFINE_SIMPLE_DEV_PM_OPS(iqs7211_pm, iqs7211_suspend, iqs7211_resume);

static ssize_t fw_info_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct iqs7211_private *iqs7211 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u.%u.%u.%u:%u.%u\n",
			 le16_to_cpu(iqs7211->ver_info.prod_num),
			 le32_to_cpu(iqs7211->ver_info.patch),
			 le16_to_cpu(iqs7211->ver_info.major),
			 le16_to_cpu(iqs7211->ver_info.minor),
			 iqs7211->exp_file[1], iqs7211->exp_file[0]);
}

static DEVICE_ATTR_RO(fw_info);

static struct attribute *iqs7211_attrs[] = {
	&dev_attr_fw_info.attr,
	NULL
};
ATTRIBUTE_GROUPS(iqs7211);

static const struct of_device_id iqs7211_of_match[] = {
	{
		.compatible = "azoteq,iqs7210a",
		.data = &iqs7211_devs[IQS7210A],
	},
	{
		.compatible = "azoteq,iqs7211a",
		.data = &iqs7211_devs[IQS7211A],
	},
	{
		.compatible = "azoteq,iqs7211e",
		.data = &iqs7211_devs[IQS7211E],
	},
	{ }
};
MODULE_DEVICE_TABLE(of, iqs7211_of_match);

static int iqs7211_probe(struct i2c_client *client)
{
	struct iqs7211_private *iqs7211;
	enum iqs7211_reg_grp_id reg_grp;
	unsigned long irq_flags;
	bool shared_irq;
	int error, irq;

	iqs7211 = devm_kzalloc(&client->dev, sizeof(*iqs7211), GFP_KERNEL);
	if (!iqs7211)
		return -ENOMEM;

	i2c_set_clientdata(client, iqs7211);
	iqs7211->client = client;

	INIT_LIST_HEAD(&iqs7211->reg_field_head);

	iqs7211->dev_desc = device_get_match_data(&client->dev);
	if (!iqs7211->dev_desc)
		return -ENODEV;

	shared_irq = iqs7211->dev_desc->num_ctx == IQS7211_MAX_CTX;

	/*
	 * The RDY pin behaves as an interrupt, but must also be polled ahead
	 * of unsolicited I2C communication. As such, it is first opened as a
	 * GPIO and then passed to gpiod_to_irq() to register the interrupt.
	 *
	 * If an extra CTx pin is present, the RDY and MCLR pins are combined
	 * into a single bidirectional pin. In that case, the platform's GPIO
	 * must be configured as an open-drain output.
	 */
	iqs7211->irq_gpio = devm_gpiod_get(&client->dev, "irq",
					   shared_irq ? GPIOD_OUT_LOW
						      : GPIOD_IN);
	if (IS_ERR(iqs7211->irq_gpio)) {
		error = PTR_ERR(iqs7211->irq_gpio);
		dev_err(&client->dev, "Failed to request IRQ GPIO: %d\n",
			error);
		return error;
	}

	if (shared_irq) {
		iqs7211->reset_gpio = iqs7211->irq_gpio;
	} else {
		iqs7211->reset_gpio = devm_gpiod_get_optional(&client->dev,
							      "reset",
							      GPIOD_OUT_HIGH);
		if (IS_ERR(iqs7211->reset_gpio)) {
			error = PTR_ERR(iqs7211->reset_gpio);
			dev_err(&client->dev,
				"Failed to request reset GPIO: %d\n", error);
			return error;
		}
	}

	error = iqs7211_start_comms(iqs7211);
	if (error)
		return error;

	for (reg_grp = 0; reg_grp < IQS7211_NUM_REG_GRPS; reg_grp++) {
		const char *reg_grp_name = iqs7211_reg_grp_names[reg_grp];
		struct fwnode_handle *reg_grp_node;

		if (reg_grp_name)
			reg_grp_node = device_get_named_child_node(&client->dev,
								   reg_grp_name);
		else
			reg_grp_node = fwnode_handle_get(dev_fwnode(&client->dev));

		if (!reg_grp_node)
			continue;

		error = iqs7211_parse_reg_grp(iqs7211, reg_grp_node, reg_grp);
		fwnode_handle_put(reg_grp_node);
		if (error)
			return error;
	}

	error = iqs7211_register_kp(iqs7211);
	if (error)
		return error;

	error = iqs7211_register_tp(iqs7211);
	if (error)
		return error;

	error = iqs7211_init_device(iqs7211);
	if (error)
		return error;

	irq = gpiod_to_irq(iqs7211->irq_gpio);
	if (irq < 0)
		return irq;

	irq_flags = gpiod_is_active_low(iqs7211->irq_gpio) ? IRQF_TRIGGER_LOW
							   : IRQF_TRIGGER_HIGH;
	irq_flags |= IRQF_ONESHOT;

	error = devm_request_threaded_irq(&client->dev, irq, NULL, iqs7211_irq,
					  irq_flags, client->name, iqs7211);
	if (error)
		dev_err(&client->dev, "Failed to request IRQ: %d\n", error);

	return error;
}

static struct i2c_driver iqs7211_i2c_driver = {
	.probe = iqs7211_probe,
	.driver = {
		.name = "iqs7211",
		.of_match_table = iqs7211_of_match,
		.dev_groups = iqs7211_groups,
		.pm = pm_sleep_ptr(&iqs7211_pm),
	},
};
module_i2c_driver(iqs7211_i2c_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("Azoteq IQS7210A/7211A/E Trackpad/Touchscreen Controller");
MODULE_LICENSE("GPL");
