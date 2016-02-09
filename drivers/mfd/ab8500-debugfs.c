/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Mattias Wallin <mattias.wallin@stericsson.com> for ST-Ericsson.
 * License Terms: GNU General Public License v2
 */
/*
 * AB8500 register access
 * ======================
 *
 * read:
 * # echo BANK  >  <debugfs>/ab8500/register-bank
 * # echo ADDR  >  <debugfs>/ab8500/register-address
 * # cat <debugfs>/ab8500/register-value
 *
 * write:
 * # echo BANK  >  <debugfs>/ab8500/register-bank
 * # echo ADDR  >  <debugfs>/ab8500/register-address
 * # echo VALUE >  <debugfs>/ab8500/register-value
 *
 * read all registers from a bank:
 * # echo BANK  >  <debugfs>/ab8500/register-bank
 * # cat <debugfs>/ab8500/all-bank-register
 *
 * BANK   target AB8500 register bank
 * ADDR   target AB8500 register address
 * VALUE  decimal or 0x-prefixed hexadecimal
 *
 *
 * User Space notification on AB8500 IRQ
 * =====================================
 *
 * Allows user space entity to be notified when target AB8500 IRQ occurs.
 * When subscribed, a sysfs entry is created in ab8500.i2c platform device.
 * One can pool this file to get target IRQ occurence information.
 *
 * subscribe to an AB8500 IRQ:
 * # echo IRQ  >  <debugfs>/ab8500/irq-subscribe
 *
 * unsubscribe from an AB8500 IRQ:
 * # echo IRQ  >  <debugfs>/ab8500/irq-unsubscribe
 *
 *
 * AB8500 register formated read/write access
 * ==========================================
 *
 * Read:  read data, data>>SHIFT, data&=MASK, output data
 *        [0xABCDEF98] shift=12 mask=0xFFF => 0x00000CDE
 * Write: read data, data &= ~(MASK<<SHIFT), data |= (VALUE<<SHIFT), write data
 *        [0xABCDEF98] shift=12 mask=0xFFF value=0x123 => [0xAB123F98]
 *
 * Usage:
 * # echo "CMD [OPTIONS] BANK ADRESS [VALUE]" > $debugfs/ab8500/hwreg
 *
 * CMD      read      read access
 *          write     write access
 *
 * BANK     target reg bank
 * ADDRESS  target reg address
 * VALUE    (write) value to be updated
 *
 * OPTIONS
 *  -d|-dec            (read) output in decimal
 *  -h|-hexa           (read) output in 0x-hexa (default)
 *  -l|-w|-b           32bit (default), 16bit or 8bit reg access
 *  -m|-mask MASK      0x-hexa mask (default 0xFFFFFFFF)
 *  -s|-shift SHIFT    bit shift value (read:left, write:right)
 *  -o|-offset OFFSET  address offset to add to ADDRESS value
 *
 * Warning: bit shift operation is applied to bit-mask.
 * Warning: bit shift direction depends on read or right command.
 */

#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>

#ifdef CONFIG_DEBUG_FS
#include <linux/string.h>
#include <linux/ctype.h>
#endif

static u32 debug_bank;
static u32 debug_address;

static int irq_ab8500;
static int irq_first;
static int irq_last;
static u32 *irq_count;
static int num_irqs;

static struct device_attribute **dev_attr;
static char **event_name;

static u8 avg_sample = SAMPLE_16;
static u8 trig_edge = RISING_EDGE;
static u8 conv_type = ADC_SW;
static u8 trig_timer;

/**
 * struct ab8500_reg_range
 * @first: the first address of the range
 * @last: the last address of the range
 * @perm: access permissions for the range
 */
struct ab8500_reg_range {
	u8 first;
	u8 last;
	u8 perm;
};

/**
 * struct ab8500_prcmu_ranges
 * @num_ranges: the number of ranges in the list
 * @bankid: bank identifier
 * @range: the list of register ranges
 */
struct ab8500_prcmu_ranges {
	u8 num_ranges;
	u8 bankid;
	const struct ab8500_reg_range *range;
};

/* hwreg- "mask" and "shift" entries ressources */
struct hwreg_cfg {
	u32  bank;      /* target bank */
	unsigned long addr;      /* target address */
	uint fmt;       /* format */
	unsigned long mask; /* read/write mask, applied before any bit shift */
	long shift;     /* bit shift (read:right shift, write:left shift */
};
/* fmt bit #0: 0=hexa, 1=dec */
#define REG_FMT_DEC(c) ((c)->fmt & 0x1)
#define REG_FMT_HEX(c) (!REG_FMT_DEC(c))

static struct hwreg_cfg hwreg_cfg = {
	.addr = 0,			/* default: invalid phys addr */
	.fmt = 0,			/* default: 32bit access, hex output */
	.mask = 0xFFFFFFFF,	/* default: no mask */
	.shift = 0,			/* default: no bit shift */
};

#define AB8500_NAME_STRING "ab8500"
#define AB8500_ADC_NAME_STRING "gpadc"
#define AB8500_NUM_BANKS 24

#define AB8500_REV_REG 0x80

static struct ab8500_prcmu_ranges *debug_ranges;

static struct ab8500_prcmu_ranges ab8500_debug_ranges[AB8500_NUM_BANKS] = {
	[0x0] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_SYS_CTRL1_BLOCK] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x02,
			},
			{
				.first = 0x42,
				.last = 0x42,
			},
			{
				.first = 0x80,
				.last = 0x81,
			},
		},
	},
	[AB8500_SYS_CTRL2_BLOCK] = {
		.num_ranges = 4,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0D,
			},
			{
				.first = 0x0F,
				.last = 0x17,
			},
			{
				.first = 0x30,
				.last = 0x30,
			},
			{
				.first = 0x32,
				.last = 0x33,
			},
		},
	},
	[AB8500_REGU_CTRL1] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x03,
				.last = 0x10,
			},
			{
				.first = 0x80,
				.last = 0x84,
			},
		},
	},
	[AB8500_REGU_CTRL2] = {
		.num_ranges = 5,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x15,
			},
			{
				.first = 0x17,
				.last = 0x19,
			},
			{
				.first = 0x1B,
				.last = 0x1D,
			},
			{
				.first = 0x1F,
				.last = 0x22,
			},
			{
				.first = 0x40,
				.last = 0x44,
			},
			/*
			 * 0x80-0x8B are SIM registers and should
			 * not be accessed from here
			 */
		},
	},
	[AB8500_USB] = {
		.num_ranges = 2,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x80,
				.last = 0x83,
			},
			{
				.first = 0x87,
				.last = 0x8A,
			},
		},
	},
	[AB8500_TVOUT] = {
		.num_ranges = 9,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x12,
			},
			{
				.first = 0x15,
				.last = 0x17,
			},
			{
				.first = 0x19,
				.last = 0x21,
			},
			{
				.first = 0x27,
				.last = 0x2C,
			},
			{
				.first = 0x41,
				.last = 0x41,
			},
			{
				.first = 0x45,
				.last = 0x5B,
			},
			{
				.first = 0x5D,
				.last = 0x5D,
			},
			{
				.first = 0x69,
				.last = 0x69,
			},
			{
				.first = 0x80,
				.last = 0x81,
			},
		},
	},
	[AB8500_DBI] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_ECI_AV_ACC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x80,
				.last = 0x82,
			},
		},
	},
	[0x9] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_GPADC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x08,
			},
		},
	},
	[AB8500_CHARGER] = {
		.num_ranges = 9,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x03,
			},
			{
				.first = 0x05,
				.last = 0x05,
			},
			{
				.first = 0x40,
				.last = 0x40,
			},
			{
				.first = 0x42,
				.last = 0x42,
			},
			{
				.first = 0x44,
				.last = 0x44,
			},
			{
				.first = 0x50,
				.last = 0x55,
			},
			{
				.first = 0x80,
				.last = 0x82,
			},
			{
				.first = 0xC0,
				.last = 0xC2,
			},
			{
				.first = 0xf5,
				.last = 0xf6,
			},
		},
	},
	[AB8500_GAS_GAUGE] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x07,
				.last = 0x0A,
			},
			{
				.first = 0x10,
				.last = 0x14,
			},
		},
	},
	[AB8500_DEVELOPMENT] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
		},
	},
	[AB8500_DEBUG] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x05,
				.last = 0x07,
			},
		},
	},
	[AB8500_AUDIO] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x6F,
			},
		},
	},
	[AB8500_INTERRUPT] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_RTC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0F,
			},
		},
	},
	[AB8500_MISC] = {
		.num_ranges = 8,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x05,
			},
			{
				.first = 0x10,
				.last = 0x15,
			},
			{
				.first = 0x20,
				.last = 0x25,
			},
			{
				.first = 0x30,
				.last = 0x35,
			},
			{
				.first = 0x40,
				.last = 0x45,
			},
			{
				.first = 0x50,
				.last = 0x50,
			},
			{
				.first = 0x60,
				.last = 0x67,
			},
			{
				.first = 0x80,
				.last = 0x80,
			},
		},
	},
	[0x11] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[0x12] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[0x13] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[0x14] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_OTP_EMUL] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x0F,
			},
		},
	},
};

static struct ab8500_prcmu_ranges ab8505_debug_ranges[AB8500_NUM_BANKS] = {
	[0x0] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_SYS_CTRL1_BLOCK] = {
		.num_ranges = 5,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x04,
			},
			{
				.first = 0x42,
				.last = 0x42,
			},
			{
				.first = 0x52,
				.last = 0x52,
			},
			{
				.first = 0x54,
				.last = 0x57,
			},
			{
				.first = 0x80,
				.last = 0x83,
			},
		},
	},
	[AB8500_SYS_CTRL2_BLOCK] = {
		.num_ranges = 5,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0D,
			},
			{
				.first = 0x0F,
				.last = 0x17,
			},
			{
				.first = 0x20,
				.last = 0x20,
			},
			{
				.first = 0x30,
				.last = 0x30,
			},
			{
				.first = 0x32,
				.last = 0x3A,
			},
		},
	},
	[AB8500_REGU_CTRL1] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x03,
				.last = 0x11,
			},
			{
				.first = 0x80,
				.last = 0x86,
			},
		},
	},
	[AB8500_REGU_CTRL2] = {
		.num_ranges = 6,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x06,
			},
			{
				.first = 0x08,
				.last = 0x15,
			},
			{
				.first = 0x17,
				.last = 0x19,
			},
			{
				.first = 0x1B,
				.last = 0x1D,
			},
			{
				.first = 0x1F,
				.last = 0x30,
			},
			{
				.first = 0x40,
				.last = 0x48,
			},
			/*
			 * 0x80-0x8B are SIM registers and should
			 * not be accessed from here
			 */
		},
	},
	[AB8500_USB] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x80,
				.last = 0x83,
			},
			{
				.first = 0x87,
				.last = 0x8A,
			},
			{
				.first = 0x91,
				.last = 0x94,
			},
		},
	},
	[AB8500_TVOUT] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_DBI] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_ECI_AV_ACC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x80,
				.last = 0x82,
			},
		},
	},
	[AB8500_RESERVED] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_GPADC] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x08,
			},
		},
	},
	[AB8500_CHARGER] = {
		.num_ranges = 9,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x02,
				.last = 0x03,
			},
			{
				.first = 0x05,
				.last = 0x05,
			},
			{
				.first = 0x40,
				.last = 0x44,
			},
			{
				.first = 0x50,
				.last = 0x57,
			},
			{
				.first = 0x60,
				.last = 0x60,
			},
			{
				.first = 0xA0,
				.last = 0xA7,
			},
			{
				.first = 0xAF,
				.last = 0xB2,
			},
			{
				.first = 0xC0,
				.last = 0xC2,
			},
			{
				.first = 0xF5,
				.last = 0xF5,
			},
		},
	},
	[AB8500_GAS_GAUGE] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x07,
				.last = 0x0A,
			},
			{
				.first = 0x10,
				.last = 0x14,
			},
		},
	},
	[AB8500_AUDIO] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x83,
			},
		},
	},
	[AB8500_INTERRUPT] = {
		.num_ranges = 11,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x04,
			},
			{
				.first = 0x06,
				.last = 0x07,
			},
			{
				.first = 0x09,
				.last = 0x09,
			},
			{
				.first = 0x0B,
				.last = 0x0C,
			},
			{
				.first = 0x12,
				.last = 0x15,
			},
			{
				.first = 0x18,
				.last = 0x18,
			},
			/* Latch registers should not be read here */
			{
				.first = 0x40,
				.last = 0x44,
			},
			{
				.first = 0x46,
				.last = 0x49,
			},
			{
				.first = 0x4B,
				.last = 0x4D,
			},
			{
				.first = 0x52,
				.last = 0x55,
			},
			{
				.first = 0x58,
				.last = 0x58,
			},
			/* LatchHier registers should not be read here */
		},
	},
	[AB8500_RTC] = {
		.num_ranges = 2,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x14,
			},
			{
				.first = 0x16,
				.last = 0x17,
			},
		},
	},
	[AB8500_MISC] = {
		.num_ranges = 8,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x06,
			},
			{
				.first = 0x10,
				.last = 0x16,
			},
			{
				.first = 0x20,
				.last = 0x26,
			},
			{
				.first = 0x30,
				.last = 0x36,
			},
			{
				.first = 0x40,
				.last = 0x46,
			},
			{
				.first = 0x50,
				.last = 0x50,
			},
			{
				.first = 0x60,
				.last = 0x6B,
			},
			{
				.first = 0x80,
				.last = 0x82,
			},
		},
	},
	[AB8500_DEVELOPMENT] = {
		.num_ranges = 2,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x05,
				.last = 0x05,
			},
		},
	},
	[AB8500_DEBUG] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x05,
				.last = 0x07,
			},
		},
	},
	[AB8500_PROD_TEST] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_STE_TEST] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_OTP_EMUL] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x15,
			},
		},
	},
};

static struct ab8500_prcmu_ranges ab8540_debug_ranges[AB8500_NUM_BANKS] = {
	[AB8500_M_FSM_RANK] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0B,
			},
		},
	},
	[AB8500_SYS_CTRL1_BLOCK] = {
		.num_ranges = 6,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x04,
			},
			{
				.first = 0x42,
				.last = 0x42,
			},
			{
				.first = 0x50,
				.last = 0x54,
			},
			{
				.first = 0x57,
				.last = 0x57,
			},
			{
				.first = 0x80,
				.last = 0x83,
			},
			{
				.first = 0x90,
				.last = 0x90,
			},
		},
	},
	[AB8500_SYS_CTRL2_BLOCK] = {
		.num_ranges = 5,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0D,
			},
			{
				.first = 0x0F,
				.last = 0x10,
			},
			{
				.first = 0x20,
				.last = 0x21,
			},
			{
				.first = 0x32,
				.last = 0x3C,
			},
			{
				.first = 0x40,
				.last = 0x42,
			},
		},
	},
	[AB8500_REGU_CTRL1] = {
		.num_ranges = 4,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x03,
				.last = 0x15,
			},
			{
				.first = 0x20,
				.last = 0x20,
			},
			{
				.first = 0x80,
				.last = 0x85,
			},
			{
				.first = 0x87,
				.last = 0x88,
			},
		},
	},
	[AB8500_REGU_CTRL2] = {
		.num_ranges = 8,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x06,
			},
			{
				.first = 0x08,
				.last = 0x15,
			},
			{
				.first = 0x17,
				.last = 0x19,
			},
			{
				.first = 0x1B,
				.last = 0x1D,
			},
			{
				.first = 0x1F,
				.last = 0x2F,
			},
			{
				.first = 0x31,
				.last = 0x3A,
			},
			{
				.first = 0x43,
				.last = 0x44,
			},
			{
				.first = 0x48,
				.last = 0x49,
			},
		},
	},
	[AB8500_USB] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x80,
				.last = 0x83,
			},
			{
				.first = 0x87,
				.last = 0x8A,
			},
			{
				.first = 0x91,
				.last = 0x94,
			},
		},
	},
	[AB8500_TVOUT] = {
		.num_ranges = 0,
		.range = NULL
	},
	[AB8500_DBI] = {
		.num_ranges = 4,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x07,
			},
			{
				.first = 0x10,
				.last = 0x11,
			},
			{
				.first = 0x20,
				.last = 0x21,
			},
			{
				.first = 0x30,
				.last = 0x43,
			},
		},
	},
	[AB8500_ECI_AV_ACC] = {
		.num_ranges = 2,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x03,
			},
			{
				.first = 0x80,
				.last = 0x82,
			},
		},
	},
	[AB8500_RESERVED] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_GPADC] = {
		.num_ranges = 4,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x01,
			},
			{
				.first = 0x04,
				.last = 0x06,
			},
			{
				.first = 0x09,
				.last = 0x0A,
			},
			{
				.first = 0x10,
				.last = 0x14,
			},
		},
	},
	[AB8500_CHARGER] = {
		.num_ranges = 10,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x02,
				.last = 0x05,
			},
			{
				.first = 0x40,
				.last = 0x44,
			},
			{
				.first = 0x50,
				.last = 0x57,
			},
			{
				.first = 0x60,
				.last = 0x60,
			},
			{
				.first = 0x70,
				.last = 0x70,
			},
			{
				.first = 0xA0,
				.last = 0xA9,
			},
			{
				.first = 0xAF,
				.last = 0xB2,
			},
			{
				.first = 0xC0,
				.last = 0xC6,
			},
			{
				.first = 0xF5,
				.last = 0xF5,
			},
		},
	},
	[AB8500_GAS_GAUGE] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x00,
			},
			{
				.first = 0x07,
				.last = 0x0A,
			},
			{
				.first = 0x10,
				.last = 0x14,
			},
		},
	},
	[AB8500_AUDIO] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x9f,
			},
		},
	},
	[AB8500_INTERRUPT] = {
		.num_ranges = 6,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x05,
			},
			{
				.first = 0x0B,
				.last = 0x0D,
			},
			{
				.first = 0x12,
				.last = 0x20,
			},
			/* Latch registers should not be read here */
			{
				.first = 0x40,
				.last = 0x45,
			},
			{
				.first = 0x4B,
				.last = 0x4D,
			},
			{
				.first = 0x52,
				.last = 0x60,
			},
			/* LatchHier registers should not be read here */
		},
	},
	[AB8500_RTC] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x07,
			},
			{
				.first = 0x0B,
				.last = 0x18,
			},
			{
				.first = 0x20,
				.last = 0x25,
			},
		},
	},
	[AB8500_MISC] = {
		.num_ranges = 9,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x06,
			},
			{
				.first = 0x10,
				.last = 0x16,
			},
			{
				.first = 0x20,
				.last = 0x26,
			},
			{
				.first = 0x30,
				.last = 0x36,
			},
			{
				.first = 0x40,
				.last = 0x49,
			},
			{
				.first = 0x50,
				.last = 0x50,
			},
			{
				.first = 0x60,
				.last = 0x6B,
			},
			{
				.first = 0x70,
				.last = 0x74,
			},
			{
				.first = 0x80,
				.last = 0x82,
			},
		},
	},
	[AB8500_DEVELOPMENT] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x01,
			},
			{
				.first = 0x06,
				.last = 0x06,
			},
			{
				.first = 0x10,
				.last = 0x21,
			},
		},
	},
	[AB8500_DEBUG] = {
		.num_ranges = 3,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x0C,
			},
			{
				.first = 0x0E,
				.last = 0x11,
			},
			{
				.first = 0x80,
				.last = 0x81,
			},
		},
	},
	[AB8500_PROD_TEST] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_STE_TEST] = {
		.num_ranges = 0,
		.range = NULL,
	},
	[AB8500_OTP_EMUL] = {
		.num_ranges = 1,
		.range = (struct ab8500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x3F,
			},
		},
	},
};


static irqreturn_t ab8500_debug_handler(int irq, void *data)
{
	char buf[16];
	struct kobject *kobj = (struct kobject *)data;
	unsigned int irq_abb = irq - irq_first;

	if (irq_abb < num_irqs)
		irq_count[irq_abb]++;
	/*
	 * This makes it possible to use poll for events (POLLPRI | POLLERR)
	 * from userspace on sysfs file named <irq-nr>
	 */
	sprintf(buf, "%d", irq);
	sysfs_notify(kobj, NULL, buf);

	return IRQ_HANDLED;
}

/* Prints to seq_file or log_buf */
static int ab8500_registers_print(struct device *dev, u32 bank,
				  struct seq_file *s)
{
	unsigned int i;

	for (i = 0; i < debug_ranges[bank].num_ranges; i++) {
		u32 reg;

		for (reg = debug_ranges[bank].range[i].first;
			reg <= debug_ranges[bank].range[i].last;
			reg++) {
			u8 value;
			int err;

			err = abx500_get_register_interruptible(dev,
				(u8)bank, (u8)reg, &value);
			if (err < 0) {
				dev_err(dev, "ab->read fail %d\n", err);
				return err;
			}

			if (s) {
				seq_printf(s, "  [0x%02X/0x%02X]: 0x%02X\n",
					   bank, reg, value);
				/*
				 * Error is not returned here since
				 * the output is wanted in any case
				 */
				if (seq_has_overflowed(s))
					return 0;
			} else {
				dev_info(dev, " [0x%02X/0x%02X]: 0x%02X\n",
					 bank, reg, value);
			}
		}
	}

	return 0;
}

static int ab8500_print_bank_registers(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	u32 bank = debug_bank;

	seq_puts(s, AB8500_NAME_STRING " register values:\n");

	seq_printf(s, " bank 0x%02X:\n", bank);

	return ab8500_registers_print(dev, bank, s);
}

static int ab8500_registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_print_bank_registers, inode->i_private);
}

static const struct file_operations ab8500_registers_fops = {
	.open = ab8500_registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_print_all_banks(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	unsigned int i;

	seq_puts(s, AB8500_NAME_STRING " register values:\n");

	for (i = 0; i < AB8500_NUM_BANKS; i++) {
		int err;

		seq_printf(s, " bank 0x%02X:\n", i);
		err = ab8500_registers_print(dev, i, s);
		if (err)
			return err;
	}
	return 0;
}

/* Dump registers to kernel log */
void ab8500_dump_all_banks(struct device *dev)
{
	unsigned int i;

	dev_info(dev, "ab8500 register values:\n");

	for (i = 1; i < AB8500_NUM_BANKS; i++) {
		dev_info(dev, " bank 0x%02X:\n", i);
		ab8500_registers_print(dev, i, NULL);
	}
}

/* Space for 500 registers. */
#define DUMP_MAX_REGS 700
static struct ab8500_register_dump
{
	u8 bank;
	u8 reg;
	u8 value;
} ab8500_complete_register_dump[DUMP_MAX_REGS];

/* This shall only be called upon kernel panic! */
void ab8500_dump_all_banks_to_mem(void)
{
	int i, r = 0;
	u8 bank;
	int err = 0;

	pr_info("Saving all ABB registers for crash analysis.\n");

	for (bank = 0; bank < AB8500_NUM_BANKS; bank++) {
		for (i = 0; i < debug_ranges[bank].num_ranges; i++) {
			u8 reg;

			for (reg = debug_ranges[bank].range[i].first;
			     reg <= debug_ranges[bank].range[i].last;
			     reg++) {
				u8 value;

				err = prcmu_abb_read(bank, reg, &value, 1);

				if (err < 0)
					goto out;

				ab8500_complete_register_dump[r].bank = bank;
				ab8500_complete_register_dump[r].reg = reg;
				ab8500_complete_register_dump[r].value = value;

				r++;

				if (r >= DUMP_MAX_REGS) {
					pr_err("%s: too many register to dump!\n",
						__func__);
					err = -EINVAL;
					goto out;
				}
			}
		}
	}
out:
	if (err >= 0)
		pr_info("Saved all ABB registers.\n");
	else
		pr_info("Failed to save all ABB registers.\n");
}

static int ab8500_all_banks_open(struct inode *inode, struct file *file)
{
	struct seq_file *s;
	int err;

	err = single_open(file, ab8500_print_all_banks, inode->i_private);
	if (!err) {
		/* Default buf size in seq_read is not enough */
		s = (struct seq_file *)file->private_data;
		s->size = (PAGE_SIZE * 2);
		s->buf = kmalloc(s->size, GFP_KERNEL);
		if (!s->buf) {
			single_release(inode, file);
			err = -ENOMEM;
		}
	}
	return err;
}

static const struct file_operations ab8500_all_banks_fops = {
	.open = ab8500_all_banks_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_bank_print(struct seq_file *s, void *p)
{
	seq_printf(s, "0x%02X\n", debug_bank);
	return 0;
}

static int ab8500_bank_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_bank_print, inode->i_private);
}

static ssize_t ab8500_bank_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_bank;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_bank);
	if (err)
		return err;

	if (user_bank >= AB8500_NUM_BANKS) {
		dev_err(dev, "debugfs error input > number of banks\n");
		return -EINVAL;
	}

	debug_bank = user_bank;

	return count;
}

static int ab8500_address_print(struct seq_file *s, void *p)
{
	seq_printf(s, "0x%02X\n", debug_address);
	return 0;
}

static int ab8500_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_address_print, inode->i_private);
}

static ssize_t ab8500_address_write(struct file *file,
				    const char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_address;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_address);
	if (err)
		return err;

	if (user_address > 0xff) {
		dev_err(dev, "debugfs error input > 0xff\n");
		return -EINVAL;
	}
	debug_address = user_address;

	return count;
}

static int ab8500_val_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int ret;
	u8 regvalue;

	ret = abx500_get_register_interruptible(dev,
		(u8)debug_bank, (u8)debug_address, &regvalue);
	if (ret < 0) {
		dev_err(dev, "abx500_get_reg fail %d, %d\n",
			ret, __LINE__);
		return -EINVAL;
	}
	seq_printf(s, "0x%02X\n", regvalue);

	return 0;
}

static int ab8500_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_val_print, inode->i_private);
}

static ssize_t ab8500_val_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_val;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_val);
	if (err)
		return err;

	if (user_val > 0xff) {
		dev_err(dev, "debugfs error input > 0xff\n");
		return -EINVAL;
	}
	err = abx500_set_register_interruptible(dev,
		(u8)debug_bank, debug_address, (u8)user_val);
	if (err < 0) {
		pr_err("abx500_set_reg failed %d, %d", err, __LINE__);
		return -EINVAL;
	}

	return count;
}

/*
 * Interrupt status
 */
static u32 num_interrupts[AB8500_MAX_NR_IRQS];
static u32 num_wake_interrupts[AB8500_MAX_NR_IRQS];
static int num_interrupt_lines;

bool __attribute__((weak)) suspend_test_wake_cause_interrupt_is_mine(u32 my_int)
{
	return false;
}

void ab8500_debug_register_interrupt(int line)
{
	if (line < num_interrupt_lines) {
		num_interrupts[line]++;
		if (suspend_test_wake_cause_interrupt_is_mine(irq_ab8500))
			num_wake_interrupts[line]++;
	}
}

static int ab8500_interrupts_print(struct seq_file *s, void *p)
{
	int line;

	seq_puts(s, "name: number:  number of: wake:\n");

	for (line = 0; line < num_interrupt_lines; line++) {
		struct irq_desc *desc = irq_to_desc(line + irq_first);

		seq_printf(s, "%3i:  %6i %4i",
			   line,
			   num_interrupts[line],
			   num_wake_interrupts[line]);

		if (desc && desc->name)
			seq_printf(s, "-%-8s", desc->name);
		if (desc && desc->action) {
			struct irqaction *action = desc->action;

			seq_printf(s, "  %s", action->name);
			while ((action = action->next) != NULL)
				seq_printf(s, ", %s", action->name);
		}
		seq_putc(s, '\n');
	}

	return 0;
}

static int ab8500_interrupts_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_interrupts_print, inode->i_private);
}

/*
 * - HWREG DB8500 formated routines
 */
static int ab8500_hwreg_print(struct seq_file *s, void *d)
{
	struct device *dev = s->private;
	int ret;
	u8 regvalue;

	ret = abx500_get_register_interruptible(dev,
		(u8)hwreg_cfg.bank, (u8)hwreg_cfg.addr, &regvalue);
	if (ret < 0) {
		dev_err(dev, "abx500_get_reg fail %d, %d\n",
			ret, __LINE__);
		return -EINVAL;
	}

	if (hwreg_cfg.shift >= 0)
		regvalue >>= hwreg_cfg.shift;
	else
		regvalue <<= -hwreg_cfg.shift;
	regvalue &= hwreg_cfg.mask;

	if (REG_FMT_DEC(&hwreg_cfg))
		seq_printf(s, "%d\n", regvalue);
	else
		seq_printf(s, "0x%02X\n", regvalue);
	return 0;
}

static int ab8500_hwreg_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_hwreg_print, inode->i_private);
}

#define AB8500_SUPPLY_CONTROL_CONFIG_1 0x01
#define AB8500_SUPPLY_CONTROL_REG 0x00
#define AB8500_FIRST_SIM_REG 0x80
#define AB8500_LAST_SIM_REG 0x8B
#define AB8505_LAST_SIM_REG 0x8C

static int ab8500_print_modem_registers(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	struct ab8500 *ab8500;
	int err;
	u8 value;
	u8 orig_value;
	u32 bank = AB8500_REGU_CTRL2;
	u32 last_sim_reg = AB8500_LAST_SIM_REG;
	u32 reg;

	ab8500 = dev_get_drvdata(dev->parent);
	dev_warn(dev, "WARNING! This operation can interfer with modem side\n"
		"and should only be done with care\n");

	err = abx500_get_register_interruptible(dev,
		AB8500_REGU_CTRL1, AB8500_SUPPLY_CONTROL_REG, &orig_value);
	if (err < 0) {
		dev_err(dev, "ab->read fail %d\n", err);
		return err;
	}
	/* Config 1 will allow APE side to read SIM registers */
	err = abx500_set_register_interruptible(dev,
		AB8500_REGU_CTRL1, AB8500_SUPPLY_CONTROL_REG,
		AB8500_SUPPLY_CONTROL_CONFIG_1);
	if (err < 0) {
		dev_err(dev, "ab->write fail %d\n", err);
		return err;
	}

	seq_printf(s, " bank 0x%02X:\n", bank);

	if (is_ab9540(ab8500) || is_ab8505(ab8500))
		last_sim_reg = AB8505_LAST_SIM_REG;

	for (reg = AB8500_FIRST_SIM_REG; reg <= last_sim_reg; reg++) {
		err = abx500_get_register_interruptible(dev,
			bank, reg, &value);
		if (err < 0) {
			dev_err(dev, "ab->read fail %d\n", err);
			return err;
		}
		seq_printf(s, "  [0x%02X/0x%02X]: 0x%02X\n", bank, reg, value);
	}
	err = abx500_set_register_interruptible(dev,
		AB8500_REGU_CTRL1, AB8500_SUPPLY_CONTROL_REG, orig_value);
	if (err < 0) {
		dev_err(dev, "ab->write fail %d\n", err);
		return err;
	}
	return 0;
}

static int ab8500_modem_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_print_modem_registers,
			   inode->i_private);
}

static const struct file_operations ab8500_modem_fops = {
	.open = ab8500_modem_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_bat_ctrl_print(struct seq_file *s, void *p)
{
	int bat_ctrl_raw;
	int bat_ctrl_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	bat_ctrl_raw = ab8500_gpadc_read_raw(gpadc, BAT_CTRL,
		avg_sample, trig_edge, trig_timer, conv_type);
	bat_ctrl_convert = ab8500_gpadc_ad_to_voltage(gpadc,
		BAT_CTRL, bat_ctrl_raw);

	seq_printf(s, "%d,0x%X\n", bat_ctrl_convert, bat_ctrl_raw);

	return 0;
}

static int ab8500_gpadc_bat_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_bat_ctrl_print,
			   inode->i_private);
}

static const struct file_operations ab8500_gpadc_bat_ctrl_fops = {
	.open = ab8500_gpadc_bat_ctrl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_btemp_ball_print(struct seq_file *s, void *p)
{
	int btemp_ball_raw;
	int btemp_ball_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	btemp_ball_raw = ab8500_gpadc_read_raw(gpadc, BTEMP_BALL,
		avg_sample, trig_edge, trig_timer, conv_type);
	btemp_ball_convert = ab8500_gpadc_ad_to_voltage(gpadc, BTEMP_BALL,
		btemp_ball_raw);

	seq_printf(s, "%d,0x%X\n", btemp_ball_convert, btemp_ball_raw);

	return 0;
}

static int ab8500_gpadc_btemp_ball_open(struct inode *inode,
					struct file *file)
{
	return single_open(file, ab8500_gpadc_btemp_ball_print,
			   inode->i_private);
}

static const struct file_operations ab8500_gpadc_btemp_ball_fops = {
	.open = ab8500_gpadc_btemp_ball_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_main_charger_v_print(struct seq_file *s, void *p)
{
	int main_charger_v_raw;
	int main_charger_v_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	main_charger_v_raw = ab8500_gpadc_read_raw(gpadc, MAIN_CHARGER_V,
		avg_sample, trig_edge, trig_timer, conv_type);
	main_charger_v_convert = ab8500_gpadc_ad_to_voltage(gpadc,
		MAIN_CHARGER_V, main_charger_v_raw);

	seq_printf(s, "%d,0x%X\n", main_charger_v_convert, main_charger_v_raw);

	return 0;
}

static int ab8500_gpadc_main_charger_v_open(struct inode *inode,
					    struct file *file)
{
	return single_open(file, ab8500_gpadc_main_charger_v_print,
		inode->i_private);
}

static const struct file_operations ab8500_gpadc_main_charger_v_fops = {
	.open = ab8500_gpadc_main_charger_v_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_acc_detect1_print(struct seq_file *s, void *p)
{
	int acc_detect1_raw;
	int acc_detect1_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	acc_detect1_raw = ab8500_gpadc_read_raw(gpadc, ACC_DETECT1,
		avg_sample, trig_edge, trig_timer, conv_type);
	acc_detect1_convert = ab8500_gpadc_ad_to_voltage(gpadc, ACC_DETECT1,
		acc_detect1_raw);

	seq_printf(s, "%d,0x%X\n", acc_detect1_convert, acc_detect1_raw);

	return 0;
}

static int ab8500_gpadc_acc_detect1_open(struct inode *inode,
					 struct file *file)
{
	return single_open(file, ab8500_gpadc_acc_detect1_print,
		inode->i_private);
}

static const struct file_operations ab8500_gpadc_acc_detect1_fops = {
	.open = ab8500_gpadc_acc_detect1_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_acc_detect2_print(struct seq_file *s, void *p)
{
	int acc_detect2_raw;
	int acc_detect2_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	acc_detect2_raw = ab8500_gpadc_read_raw(gpadc, ACC_DETECT2,
		avg_sample, trig_edge, trig_timer, conv_type);
	acc_detect2_convert = ab8500_gpadc_ad_to_voltage(gpadc,
		ACC_DETECT2, acc_detect2_raw);

	seq_printf(s, "%d,0x%X\n", acc_detect2_convert, acc_detect2_raw);

	return 0;
}

static int ab8500_gpadc_acc_detect2_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8500_gpadc_acc_detect2_print,
		inode->i_private);
}

static const struct file_operations ab8500_gpadc_acc_detect2_fops = {
	.open = ab8500_gpadc_acc_detect2_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_aux1_print(struct seq_file *s, void *p)
{
	int aux1_raw;
	int aux1_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	aux1_raw = ab8500_gpadc_read_raw(gpadc, ADC_AUX1,
		avg_sample, trig_edge, trig_timer, conv_type);
	aux1_convert = ab8500_gpadc_ad_to_voltage(gpadc, ADC_AUX1,
		aux1_raw);

	seq_printf(s, "%d,0x%X\n", aux1_convert, aux1_raw);

	return 0;
}

static int ab8500_gpadc_aux1_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_aux1_print, inode->i_private);
}

static const struct file_operations ab8500_gpadc_aux1_fops = {
	.open = ab8500_gpadc_aux1_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_aux2_print(struct seq_file *s, void *p)
{
	int aux2_raw;
	int aux2_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	aux2_raw = ab8500_gpadc_read_raw(gpadc, ADC_AUX2,
		avg_sample, trig_edge, trig_timer, conv_type);
	aux2_convert = ab8500_gpadc_ad_to_voltage(gpadc, ADC_AUX2,
		aux2_raw);

	seq_printf(s, "%d,0x%X\n", aux2_convert, aux2_raw);

	return 0;
}

static int ab8500_gpadc_aux2_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_aux2_print, inode->i_private);
}

static const struct file_operations ab8500_gpadc_aux2_fops = {
	.open = ab8500_gpadc_aux2_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_main_bat_v_print(struct seq_file *s, void *p)
{
	int main_bat_v_raw;
	int main_bat_v_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	main_bat_v_raw = ab8500_gpadc_read_raw(gpadc, MAIN_BAT_V,
		avg_sample, trig_edge, trig_timer, conv_type);
	main_bat_v_convert = ab8500_gpadc_ad_to_voltage(gpadc, MAIN_BAT_V,
		main_bat_v_raw);

	seq_printf(s, "%d,0x%X\n", main_bat_v_convert, main_bat_v_raw);

	return 0;
}

static int ab8500_gpadc_main_bat_v_open(struct inode *inode,
					struct file *file)
{
	return single_open(file, ab8500_gpadc_main_bat_v_print,
			   inode->i_private);
}

static const struct file_operations ab8500_gpadc_main_bat_v_fops = {
	.open = ab8500_gpadc_main_bat_v_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_vbus_v_print(struct seq_file *s, void *p)
{
	int vbus_v_raw;
	int vbus_v_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	vbus_v_raw =  ab8500_gpadc_read_raw(gpadc, VBUS_V,
		avg_sample, trig_edge, trig_timer, conv_type);
	vbus_v_convert = ab8500_gpadc_ad_to_voltage(gpadc, VBUS_V,
		vbus_v_raw);

	seq_printf(s, "%d,0x%X\n", vbus_v_convert, vbus_v_raw);

	return 0;
}

static int ab8500_gpadc_vbus_v_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_vbus_v_print, inode->i_private);
}

static const struct file_operations ab8500_gpadc_vbus_v_fops = {
	.open = ab8500_gpadc_vbus_v_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_main_charger_c_print(struct seq_file *s, void *p)
{
	int main_charger_c_raw;
	int main_charger_c_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	main_charger_c_raw = ab8500_gpadc_read_raw(gpadc, MAIN_CHARGER_C,
		avg_sample, trig_edge, trig_timer, conv_type);
	main_charger_c_convert = ab8500_gpadc_ad_to_voltage(gpadc,
		MAIN_CHARGER_C, main_charger_c_raw);

	seq_printf(s, "%d,0x%X\n", main_charger_c_convert, main_charger_c_raw);

	return 0;
}

static int ab8500_gpadc_main_charger_c_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8500_gpadc_main_charger_c_print,
		inode->i_private);
}

static const struct file_operations ab8500_gpadc_main_charger_c_fops = {
	.open = ab8500_gpadc_main_charger_c_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_usb_charger_c_print(struct seq_file *s, void *p)
{
	int usb_charger_c_raw;
	int usb_charger_c_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	usb_charger_c_raw = ab8500_gpadc_read_raw(gpadc, USB_CHARGER_C,
		avg_sample, trig_edge, trig_timer, conv_type);
	usb_charger_c_convert = ab8500_gpadc_ad_to_voltage(gpadc,
		USB_CHARGER_C, usb_charger_c_raw);

	seq_printf(s, "%d,0x%X\n", usb_charger_c_convert, usb_charger_c_raw);

	return 0;
}

static int ab8500_gpadc_usb_charger_c_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8500_gpadc_usb_charger_c_print,
		inode->i_private);
}

static const struct file_operations ab8500_gpadc_usb_charger_c_fops = {
	.open = ab8500_gpadc_usb_charger_c_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_bk_bat_v_print(struct seq_file *s, void *p)
{
	int bk_bat_v_raw;
	int bk_bat_v_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	bk_bat_v_raw = ab8500_gpadc_read_raw(gpadc, BK_BAT_V,
		avg_sample, trig_edge, trig_timer, conv_type);
	bk_bat_v_convert = ab8500_gpadc_ad_to_voltage(gpadc,
		BK_BAT_V, bk_bat_v_raw);

	seq_printf(s, "%d,0x%X\n", bk_bat_v_convert, bk_bat_v_raw);

	return 0;
}

static int ab8500_gpadc_bk_bat_v_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_bk_bat_v_print,
			   inode->i_private);
}

static const struct file_operations ab8500_gpadc_bk_bat_v_fops = {
	.open = ab8500_gpadc_bk_bat_v_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_die_temp_print(struct seq_file *s, void *p)
{
	int die_temp_raw;
	int die_temp_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	die_temp_raw = ab8500_gpadc_read_raw(gpadc, DIE_TEMP,
		avg_sample, trig_edge, trig_timer, conv_type);
	die_temp_convert = ab8500_gpadc_ad_to_voltage(gpadc, DIE_TEMP,
		die_temp_raw);

	seq_printf(s, "%d,0x%X\n", die_temp_convert, die_temp_raw);

	return 0;
}

static int ab8500_gpadc_die_temp_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_die_temp_print,
			   inode->i_private);
}

static const struct file_operations ab8500_gpadc_die_temp_fops = {
	.open = ab8500_gpadc_die_temp_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_usb_id_print(struct seq_file *s, void *p)
{
	int usb_id_raw;
	int usb_id_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	usb_id_raw = ab8500_gpadc_read_raw(gpadc, USB_ID,
		avg_sample, trig_edge, trig_timer, conv_type);
	usb_id_convert = ab8500_gpadc_ad_to_voltage(gpadc, USB_ID,
		usb_id_raw);

	seq_printf(s, "%d,0x%X\n", usb_id_convert, usb_id_raw);

	return 0;
}

static int ab8500_gpadc_usb_id_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_usb_id_print, inode->i_private);
}

static const struct file_operations ab8500_gpadc_usb_id_fops = {
	.open = ab8500_gpadc_usb_id_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8540_gpadc_xtal_temp_print(struct seq_file *s, void *p)
{
	int xtal_temp_raw;
	int xtal_temp_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	xtal_temp_raw = ab8500_gpadc_read_raw(gpadc, XTAL_TEMP,
		avg_sample, trig_edge, trig_timer, conv_type);
	xtal_temp_convert = ab8500_gpadc_ad_to_voltage(gpadc, XTAL_TEMP,
		xtal_temp_raw);

	seq_printf(s, "%d,0x%X\n", xtal_temp_convert, xtal_temp_raw);

	return 0;
}

static int ab8540_gpadc_xtal_temp_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8540_gpadc_xtal_temp_print,
		inode->i_private);
}

static const struct file_operations ab8540_gpadc_xtal_temp_fops = {
	.open = ab8540_gpadc_xtal_temp_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8540_gpadc_vbat_true_meas_print(struct seq_file *s, void *p)
{
	int vbat_true_meas_raw;
	int vbat_true_meas_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	vbat_true_meas_raw = ab8500_gpadc_read_raw(gpadc, VBAT_TRUE_MEAS,
		avg_sample, trig_edge, trig_timer, conv_type);
	vbat_true_meas_convert =
		ab8500_gpadc_ad_to_voltage(gpadc, VBAT_TRUE_MEAS,
					   vbat_true_meas_raw);

	seq_printf(s, "%d,0x%X\n", vbat_true_meas_convert, vbat_true_meas_raw);

	return 0;
}

static int ab8540_gpadc_vbat_true_meas_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8540_gpadc_vbat_true_meas_print,
		inode->i_private);
}

static const struct file_operations ab8540_gpadc_vbat_true_meas_fops = {
	.open = ab8540_gpadc_vbat_true_meas_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8540_gpadc_bat_ctrl_and_ibat_print(struct seq_file *s, void *p)
{
	int bat_ctrl_raw;
	int bat_ctrl_convert;
	int ibat_raw;
	int ibat_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	bat_ctrl_raw = ab8500_gpadc_double_read_raw(gpadc, BAT_CTRL_AND_IBAT,
		avg_sample, trig_edge, trig_timer, conv_type, &ibat_raw);

	bat_ctrl_convert = ab8500_gpadc_ad_to_voltage(gpadc, BAT_CTRL,
		bat_ctrl_raw);
	ibat_convert = ab8500_gpadc_ad_to_voltage(gpadc, IBAT_VIRTUAL_CHANNEL,
		ibat_raw);

	seq_printf(s,
		   "%d,0x%X\n"
		   "%d,0x%X\n",
		   bat_ctrl_convert, bat_ctrl_raw,
		   ibat_convert, ibat_raw);

	return 0;
}

static int ab8540_gpadc_bat_ctrl_and_ibat_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8540_gpadc_bat_ctrl_and_ibat_print,
		inode->i_private);
}

static const struct file_operations ab8540_gpadc_bat_ctrl_and_ibat_fops = {
	.open = ab8540_gpadc_bat_ctrl_and_ibat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8540_gpadc_vbat_meas_and_ibat_print(struct seq_file *s, void *p)
{
	int vbat_meas_raw;
	int vbat_meas_convert;
	int ibat_raw;
	int ibat_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	vbat_meas_raw = ab8500_gpadc_double_read_raw(gpadc, VBAT_MEAS_AND_IBAT,
		avg_sample, trig_edge, trig_timer, conv_type, &ibat_raw);
	vbat_meas_convert = ab8500_gpadc_ad_to_voltage(gpadc, MAIN_BAT_V,
		vbat_meas_raw);
	ibat_convert = ab8500_gpadc_ad_to_voltage(gpadc, IBAT_VIRTUAL_CHANNEL,
		ibat_raw);

	seq_printf(s,
		   "%d,0x%X\n"
		   "%d,0x%X\n",
		   vbat_meas_convert, vbat_meas_raw,
		   ibat_convert, ibat_raw);

	return 0;
}

static int ab8540_gpadc_vbat_meas_and_ibat_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8540_gpadc_vbat_meas_and_ibat_print,
		inode->i_private);
}

static const struct file_operations ab8540_gpadc_vbat_meas_and_ibat_fops = {
	.open = ab8540_gpadc_vbat_meas_and_ibat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8540_gpadc_vbat_true_meas_and_ibat_print(struct seq_file *s,
						      void *p)
{
	int vbat_true_meas_raw;
	int vbat_true_meas_convert;
	int ibat_raw;
	int ibat_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	vbat_true_meas_raw = ab8500_gpadc_double_read_raw(gpadc,
			VBAT_TRUE_MEAS_AND_IBAT, avg_sample, trig_edge,
			trig_timer, conv_type, &ibat_raw);
	vbat_true_meas_convert = ab8500_gpadc_ad_to_voltage(gpadc,
			VBAT_TRUE_MEAS, vbat_true_meas_raw);
	ibat_convert = ab8500_gpadc_ad_to_voltage(gpadc, IBAT_VIRTUAL_CHANNEL,
		ibat_raw);

	seq_printf(s,
		   "%d,0x%X\n"
		   "%d,0x%X\n",
		   vbat_true_meas_convert, vbat_true_meas_raw,
		   ibat_convert, ibat_raw);

	return 0;
}

static int ab8540_gpadc_vbat_true_meas_and_ibat_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8540_gpadc_vbat_true_meas_and_ibat_print,
		inode->i_private);
}

static const struct file_operations
ab8540_gpadc_vbat_true_meas_and_ibat_fops = {
	.open = ab8540_gpadc_vbat_true_meas_and_ibat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8540_gpadc_bat_temp_and_ibat_print(struct seq_file *s, void *p)
{
	int bat_temp_raw;
	int bat_temp_convert;
	int ibat_raw;
	int ibat_convert;
	struct ab8500_gpadc *gpadc;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	bat_temp_raw = ab8500_gpadc_double_read_raw(gpadc, BAT_TEMP_AND_IBAT,
		avg_sample, trig_edge, trig_timer, conv_type, &ibat_raw);
	bat_temp_convert = ab8500_gpadc_ad_to_voltage(gpadc, BTEMP_BALL,
		bat_temp_raw);
	ibat_convert = ab8500_gpadc_ad_to_voltage(gpadc, IBAT_VIRTUAL_CHANNEL,
		ibat_raw);

	seq_printf(s,
		   "%d,0x%X\n"
		   "%d,0x%X\n",
		   bat_temp_convert, bat_temp_raw,
		   ibat_convert, ibat_raw);

	return 0;
}

static int ab8540_gpadc_bat_temp_and_ibat_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, ab8540_gpadc_bat_temp_and_ibat_print,
		inode->i_private);
}

static const struct file_operations ab8540_gpadc_bat_temp_and_ibat_fops = {
	.open = ab8540_gpadc_bat_temp_and_ibat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8540_gpadc_otp_cal_print(struct seq_file *s, void *p)
{
	struct ab8500_gpadc *gpadc;
	u16 vmain_l, vmain_h, btemp_l, btemp_h;
	u16 vbat_l, vbat_h, ibat_l, ibat_h;

	gpadc = ab8500_gpadc_get("ab8500-gpadc.0");
	ab8540_gpadc_get_otp(gpadc, &vmain_l, &vmain_h, &btemp_l, &btemp_h,
			&vbat_l, &vbat_h, &ibat_l, &ibat_h);
	seq_printf(s,
		   "VMAIN_L:0x%X\n"
		   "VMAIN_H:0x%X\n"
		   "BTEMP_L:0x%X\n"
		   "BTEMP_H:0x%X\n"
		   "VBAT_L:0x%X\n"
		   "VBAT_H:0x%X\n"
		   "IBAT_L:0x%X\n"
		   "IBAT_H:0x%X\n",
		   vmain_l, vmain_h, btemp_l, btemp_h,
		   vbat_l, vbat_h, ibat_l, ibat_h);

	return 0;
}

static int ab8540_gpadc_otp_cal_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8540_gpadc_otp_cal_print, inode->i_private);
}

static const struct file_operations ab8540_gpadc_otp_calib_fops = {
	.open = ab8540_gpadc_otp_cal_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_avg_sample_print(struct seq_file *s, void *p)
{
	seq_printf(s, "%d\n", avg_sample);

	return 0;
}

static int ab8500_gpadc_avg_sample_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_avg_sample_print,
		inode->i_private);
}

static ssize_t ab8500_gpadc_avg_sample_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_avg_sample;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_avg_sample);
	if (err)
		return err;

	if ((user_avg_sample == SAMPLE_1) || (user_avg_sample == SAMPLE_4)
			|| (user_avg_sample == SAMPLE_8)
			|| (user_avg_sample == SAMPLE_16)) {
		avg_sample = (u8) user_avg_sample;
	} else {
		dev_err(dev,
			"debugfs err input: should be egal to 1, 4, 8 or 16\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations ab8500_gpadc_avg_sample_fops = {
	.open = ab8500_gpadc_avg_sample_open,
	.read = seq_read,
	.write = ab8500_gpadc_avg_sample_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_trig_edge_print(struct seq_file *s, void *p)
{
	seq_printf(s, "%d\n", trig_edge);

	return 0;
}

static int ab8500_gpadc_trig_edge_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_trig_edge_print,
		inode->i_private);
}

static ssize_t ab8500_gpadc_trig_edge_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_trig_edge;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_trig_edge);
	if (err)
		return err;

	if ((user_trig_edge == RISING_EDGE)
			|| (user_trig_edge == FALLING_EDGE)) {
		trig_edge = (u8) user_trig_edge;
	} else {
		dev_err(dev, "Wrong input:\n"
			"Enter 0. Rising edge\n"
			"Enter 1. Falling edge\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations ab8500_gpadc_trig_edge_fops = {
	.open = ab8500_gpadc_trig_edge_open,
	.read = seq_read,
	.write = ab8500_gpadc_trig_edge_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_trig_timer_print(struct seq_file *s, void *p)
{
	seq_printf(s, "%d\n", trig_timer);

	return 0;
}

static int ab8500_gpadc_trig_timer_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_trig_timer_print,
		inode->i_private);
}

static ssize_t ab8500_gpadc_trig_timer_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_trig_timer;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_trig_timer);
	if (err)
		return err;

	if (user_trig_timer & ~0xFF) {
		dev_err(dev,
			"debugfs error input: should be beetween 0 to 255\n");
		return -EINVAL;
	}

	trig_timer = (u8) user_trig_timer;

	return count;
}

static const struct file_operations ab8500_gpadc_trig_timer_fops = {
	.open = ab8500_gpadc_trig_timer_open,
	.read = seq_read,
	.write = ab8500_gpadc_trig_timer_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab8500_gpadc_conv_type_print(struct seq_file *s, void *p)
{
	seq_printf(s, "%d\n", conv_type);

	return 0;
}

static int ab8500_gpadc_conv_type_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_gpadc_conv_type_print,
		inode->i_private);
}

static ssize_t ab8500_gpadc_conv_type_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_conv_type;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_conv_type);
	if (err)
		return err;

	if ((user_conv_type == ADC_SW)
			|| (user_conv_type == ADC_HW)) {
		conv_type = (u8) user_conv_type;
	} else {
		dev_err(dev, "Wrong input:\n"
			"Enter 0. ADC SW conversion\n"
			"Enter 1. ADC HW conversion\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations ab8500_gpadc_conv_type_fops = {
	.open = ab8500_gpadc_conv_type_open,
	.read = seq_read,
	.write = ab8500_gpadc_conv_type_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/*
 * return length of an ASCII numerical value, 0 is string is not a
 * numerical value.
 * string shall start at value 1st char.
 * string can be tailed with \0 or space or newline chars only.
 * value can be decimal or hexadecimal (prefixed 0x or 0X).
 */
static int strval_len(char *b)
{
	char *s = b;

	if ((*s == '0') && ((*(s+1) == 'x') || (*(s+1) == 'X'))) {
		s += 2;
		for (; *s && (*s != ' ') && (*s != '\n'); s++) {
			if (!isxdigit(*s))
				return 0;
		}
	} else {
		if (*s == '-')
			s++;
		for (; *s && (*s != ' ') && (*s != '\n'); s++) {
			if (!isdigit(*s))
				return 0;
		}
	}
	return (int) (s-b);
}

/*
 * parse hwreg input data.
 * update global hwreg_cfg only if input data syntax is ok.
 */
static ssize_t hwreg_common_write(char *b, struct hwreg_cfg *cfg,
		struct device *dev)
{
	uint write, val = 0;
	u8  regvalue;
	int ret;
	struct hwreg_cfg loc = {
		.bank = 0,          /* default: invalid phys addr */
		.addr = 0,          /* default: invalid phys addr */
		.fmt = 0,           /* default: 32bit access, hex output */
		.mask = 0xFFFFFFFF, /* default: no mask */
		.shift = 0,         /* default: no bit shift */
	};

	/* read or write ? */
	if (!strncmp(b, "read ", 5)) {
		write = 0;
		b += 5;
	} else if (!strncmp(b, "write ", 6)) {
		write = 1;
		b += 6;
	} else
		return -EINVAL;

	/* OPTIONS -l|-w|-b -s -m -o */
	while ((*b == ' ') || (*b == '-')) {
		if (*(b-1) != ' ') {
			b++;
			continue;
		}
		if ((!strncmp(b, "-d ", 3)) ||
				(!strncmp(b, "-dec ", 5))) {
			b += (*(b+2) == ' ') ? 3 : 5;
			loc.fmt |= (1<<0);
		} else if ((!strncmp(b, "-h ", 3)) ||
				(!strncmp(b, "-hex ", 5))) {
			b += (*(b+2) == ' ') ? 3 : 5;
			loc.fmt &= ~(1<<0);
		} else if ((!strncmp(b, "-m ", 3)) ||
				(!strncmp(b, "-mask ", 6))) {
			b += (*(b+2) == ' ') ? 3 : 6;
			if (strval_len(b) == 0)
				return -EINVAL;
			ret = kstrtoul(b, 0, &loc.mask);
			if (ret)
				return ret;
		} else if ((!strncmp(b, "-s ", 3)) ||
				(!strncmp(b, "-shift ", 7))) {
			b += (*(b+2) == ' ') ? 3 : 7;
			if (strval_len(b) == 0)
				return -EINVAL;
			ret = kstrtol(b, 0, &loc.shift);
			if (ret)
				return ret;
		} else {
			return -EINVAL;
		}
	}
	/* get arg BANK and ADDRESS */
	if (strval_len(b) == 0)
		return -EINVAL;
	ret = kstrtouint(b, 0, &loc.bank);
	if (ret)
		return ret;
	while (*b == ' ')
		b++;
	if (strval_len(b) == 0)
		return -EINVAL;
	ret = kstrtoul(b, 0, &loc.addr);
	if (ret)
		return ret;

	if (write) {
		while (*b == ' ')
			b++;
		if (strval_len(b) == 0)
			return -EINVAL;
		ret = kstrtouint(b, 0, &val);
		if (ret)
			return ret;
	}

	/* args are ok, update target cfg (mainly for read) */
	*cfg = loc;

#ifdef ABB_HWREG_DEBUG
	pr_warn("HWREG request: %s, %s,\n", (write) ? "write" : "read",
		REG_FMT_DEC(cfg) ? "decimal" : "hexa");
	pr_warn("  addr=0x%08X, mask=0x%X, shift=%d" "value=0x%X\n",
		cfg->addr, cfg->mask, cfg->shift, val);
#endif

	if (!write)
		return 0;

	ret = abx500_get_register_interruptible(dev,
			(u8)cfg->bank, (u8)cfg->addr, &regvalue);
	if (ret < 0) {
		dev_err(dev, "abx500_get_reg fail %d, %d\n",
			ret, __LINE__);
		return -EINVAL;
	}

	if (cfg->shift >= 0) {
		regvalue &= ~(cfg->mask << (cfg->shift));
		val = (val & cfg->mask) << (cfg->shift);
	} else {
		regvalue &= ~(cfg->mask >> (-cfg->shift));
		val = (val & cfg->mask) >> (-cfg->shift);
	}
	val = val | regvalue;

	ret = abx500_set_register_interruptible(dev,
			(u8)cfg->bank, (u8)cfg->addr, (u8)val);
	if (ret < 0) {
		pr_err("abx500_set_reg failed %d, %d", ret, __LINE__);
		return -EINVAL;
	}

	return 0;
}

static ssize_t ab8500_hwreg_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	char buf[128];
	int buf_size, ret;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/* get args and process */
	ret = hwreg_common_write(buf, &hwreg_cfg, dev);
	return (ret) ? ret : buf_size;
}

/*
 * - irq subscribe/unsubscribe stuff
 */
static int ab8500_subscribe_unsubscribe_print(struct seq_file *s, void *p)
{
	seq_printf(s, "%d\n", irq_first);

	return 0;
}

static int ab8500_subscribe_unsubscribe_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, ab8500_subscribe_unsubscribe_print,
		inode->i_private);
}

/*
 * Userspace should use poll() on this file. When an event occur
 * the blocking poll will be released.
 */
static ssize_t show_irq(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned long name;
	unsigned int irq_index;
	int err;

	err = kstrtoul(attr->attr.name, 0, &name);
	if (err)
		return err;

	irq_index = name - irq_first;
	if (irq_index >= num_irqs)
		return -EINVAL;

	return sprintf(buf, "%u\n", irq_count[irq_index]);
}

static ssize_t ab8500_subscribe_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_val;
	int err;
	unsigned int irq_index;

	err = kstrtoul_from_user(user_buf, count, 0, &user_val);
	if (err)
		return err;

	if (user_val < irq_first) {
		dev_err(dev, "debugfs error input < %d\n", irq_first);
		return -EINVAL;
	}
	if (user_val > irq_last) {
		dev_err(dev, "debugfs error input > %d\n", irq_last);
		return -EINVAL;
	}

	irq_index = user_val - irq_first;
	if (irq_index >= num_irqs)
		return -EINVAL;

	/*
	 * This will create a sysfs file named <irq-nr> which userspace can
	 * use to select or poll and get the AB8500 events
	 */
	dev_attr[irq_index] = kmalloc(sizeof(struct device_attribute),
		GFP_KERNEL);
	if (!dev_attr[irq_index])
		return -ENOMEM;

	event_name[irq_index] = kmalloc(count, GFP_KERNEL);
	if (!event_name[irq_index])
		return -ENOMEM;

	sprintf(event_name[irq_index], "%lu", user_val);
	dev_attr[irq_index]->show = show_irq;
	dev_attr[irq_index]->store = NULL;
	dev_attr[irq_index]->attr.name = event_name[irq_index];
	dev_attr[irq_index]->attr.mode = S_IRUGO;
	err = sysfs_create_file(&dev->kobj, &dev_attr[irq_index]->attr);
	if (err < 0) {
		pr_info("sysfs_create_file failed %d\n", err);
		return err;
	}

	err = request_threaded_irq(user_val, NULL, ab8500_debug_handler,
				   IRQF_SHARED | IRQF_NO_SUSPEND | IRQF_ONESHOT,
				   "ab8500-debug", &dev->kobj);
	if (err < 0) {
		pr_info("request_threaded_irq failed %d, %lu\n",
			err, user_val);
		sysfs_remove_file(&dev->kobj, &dev_attr[irq_index]->attr);
		return err;
	}

	return count;
}

static ssize_t ab8500_unsubscribe_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct device *dev = ((struct seq_file *)(file->private_data))->private;
	unsigned long user_val;
	int err;
	unsigned int irq_index;

	err = kstrtoul_from_user(user_buf, count, 0, &user_val);
	if (err)
		return err;

	if (user_val < irq_first) {
		dev_err(dev, "debugfs error input < %d\n", irq_first);
		return -EINVAL;
	}
	if (user_val > irq_last) {
		dev_err(dev, "debugfs error input > %d\n", irq_last);
		return -EINVAL;
	}

	irq_index = user_val - irq_first;
	if (irq_index >= num_irqs)
		return -EINVAL;

	/* Set irq count to 0 when unsubscribe */
	irq_count[irq_index] = 0;

	if (dev_attr[irq_index])
		sysfs_remove_file(&dev->kobj, &dev_attr[irq_index]->attr);


	free_irq(user_val, &dev->kobj);
	kfree(event_name[irq_index]);
	kfree(dev_attr[irq_index]);

	return count;
}

/*
 * - several deubgfs nodes fops
 */

static const struct file_operations ab8500_bank_fops = {
	.open = ab8500_bank_open,
	.write = ab8500_bank_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_address_fops = {
	.open = ab8500_address_open,
	.write = ab8500_address_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_val_fops = {
	.open = ab8500_val_open,
	.write = ab8500_val_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_interrupts_fops = {
	.open = ab8500_interrupts_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_subscribe_fops = {
	.open = ab8500_subscribe_unsubscribe_open,
	.write = ab8500_subscribe_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_unsubscribe_fops = {
	.open = ab8500_subscribe_unsubscribe_open,
	.write = ab8500_unsubscribe_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab8500_hwreg_fops = {
	.open = ab8500_hwreg_open,
	.write = ab8500_hwreg_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *ab8500_dir;
static struct dentry *ab8500_gpadc_dir;

static int ab8500_debug_probe(struct platform_device *plf)
{
	struct dentry *file;
	struct ab8500 *ab8500;
	struct resource *res;

	debug_bank = AB8500_MISC;
	debug_address = AB8500_REV_REG & 0x00FF;

	ab8500 = dev_get_drvdata(plf->dev.parent);
	num_irqs = ab8500->mask_size;

	irq_count = devm_kzalloc(&plf->dev,
				 sizeof(*irq_count)*num_irqs, GFP_KERNEL);
	if (!irq_count)
		return -ENOMEM;

	dev_attr = devm_kzalloc(&plf->dev,
				sizeof(*dev_attr)*num_irqs, GFP_KERNEL);
	if (!dev_attr)
		return -ENOMEM;

	event_name = devm_kzalloc(&plf->dev,
				  sizeof(*event_name)*num_irqs, GFP_KERNEL);
	if (!event_name)
		return -ENOMEM;

	res = platform_get_resource_byname(plf, 0, "IRQ_AB8500");
	if (!res) {
		dev_err(&plf->dev, "AB8500 irq not found, err %d\n", irq_first);
		return -ENXIO;
	}
	irq_ab8500 = res->start;

	irq_first = platform_get_irq_byname(plf, "IRQ_FIRST");
	if (irq_first < 0) {
		dev_err(&plf->dev, "First irq not found, err %d\n", irq_first);
		return irq_first;
	}

	irq_last = platform_get_irq_byname(plf, "IRQ_LAST");
	if (irq_last < 0) {
		dev_err(&plf->dev, "Last irq not found, err %d\n", irq_last);
		return irq_last;
	}

	ab8500_dir = debugfs_create_dir(AB8500_NAME_STRING, NULL);
	if (!ab8500_dir)
		goto err;

	ab8500_gpadc_dir = debugfs_create_dir(AB8500_ADC_NAME_STRING,
					      ab8500_dir);
	if (!ab8500_gpadc_dir)
		goto err;

	file = debugfs_create_file("all-bank-registers", S_IRUGO, ab8500_dir,
				   &plf->dev, &ab8500_registers_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("all-banks", S_IRUGO, ab8500_dir,
				   &plf->dev, &ab8500_all_banks_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("register-bank",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_dir, &plf->dev, &ab8500_bank_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("register-address",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_dir, &plf->dev, &ab8500_address_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("register-value",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_dir, &plf->dev, &ab8500_val_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("irq-subscribe",
				   (S_IRUGO | S_IWUSR | S_IWGRP), ab8500_dir,
				   &plf->dev, &ab8500_subscribe_fops);
	if (!file)
		goto err;

	if (is_ab8500(ab8500)) {
		debug_ranges = ab8500_debug_ranges;
		num_interrupt_lines = AB8500_NR_IRQS;
	} else if (is_ab8505(ab8500)) {
		debug_ranges = ab8505_debug_ranges;
		num_interrupt_lines = AB8505_NR_IRQS;
	} else if (is_ab9540(ab8500)) {
		debug_ranges = ab8505_debug_ranges;
		num_interrupt_lines = AB9540_NR_IRQS;
	} else if (is_ab8540(ab8500)) {
		debug_ranges = ab8540_debug_ranges;
		num_interrupt_lines = AB8540_NR_IRQS;
	}

	file = debugfs_create_file("interrupts", (S_IRUGO), ab8500_dir,
				   &plf->dev, &ab8500_interrupts_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("irq-unsubscribe",
				   (S_IRUGO | S_IWUSR | S_IWGRP), ab8500_dir,
				   &plf->dev, &ab8500_unsubscribe_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("hwreg", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_dir, &plf->dev, &ab8500_hwreg_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("all-modem-registers",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_dir, &plf->dev, &ab8500_modem_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("bat_ctrl", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_bat_ctrl_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("btemp_ball", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir,
				   &plf->dev, &ab8500_gpadc_btemp_ball_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("main_charger_v",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_main_charger_v_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("acc_detect1",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_acc_detect1_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("acc_detect2",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_acc_detect2_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("adc_aux1", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_aux1_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("adc_aux2", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_aux2_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("main_bat_v", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_main_bat_v_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("vbus_v", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_vbus_v_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("main_charger_c",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_main_charger_c_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("usb_charger_c",
				   (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir,
				   &plf->dev, &ab8500_gpadc_usb_charger_c_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("bk_bat_v", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_bk_bat_v_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("die_temp", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_die_temp_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("usb_id", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_usb_id_fops);
	if (!file)
		goto err;

	if (is_ab8540(ab8500)) {
		file = debugfs_create_file("xtal_temp",
					   (S_IRUGO | S_IWUSR | S_IWGRP),
					   ab8500_gpadc_dir, &plf->dev,
					   &ab8540_gpadc_xtal_temp_fops);
		if (!file)
			goto err;
		file = debugfs_create_file("vbattruemeas",
					   (S_IRUGO | S_IWUSR | S_IWGRP),
					   ab8500_gpadc_dir, &plf->dev,
					   &ab8540_gpadc_vbat_true_meas_fops);
		if (!file)
			goto err;
		file = debugfs_create_file("batctrl_and_ibat",
					(S_IRUGO | S_IWUGO),
					ab8500_gpadc_dir,
					&plf->dev,
					&ab8540_gpadc_bat_ctrl_and_ibat_fops);
		if (!file)
			goto err;
		file = debugfs_create_file("vbatmeas_and_ibat",
					(S_IRUGO | S_IWUGO),
					ab8500_gpadc_dir, &plf->dev,
					&ab8540_gpadc_vbat_meas_and_ibat_fops);
		if (!file)
			goto err;
		file = debugfs_create_file("vbattruemeas_and_ibat",
				(S_IRUGO | S_IWUGO),
				ab8500_gpadc_dir,
				&plf->dev,
				&ab8540_gpadc_vbat_true_meas_and_ibat_fops);
		if (!file)
			goto err;
		file = debugfs_create_file("battemp_and_ibat",
			(S_IRUGO | S_IWUGO),
			ab8500_gpadc_dir,
			&plf->dev, &ab8540_gpadc_bat_temp_and_ibat_fops);
		if (!file)
			goto err;
		file = debugfs_create_file("otp_calib",
				(S_IRUGO | S_IWUSR | S_IWGRP),
				ab8500_gpadc_dir,
				&plf->dev, &ab8540_gpadc_otp_calib_fops);
		if (!file)
			goto err;
	}
	file = debugfs_create_file("avg_sample", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_avg_sample_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("trig_edge", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_trig_edge_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("trig_timer", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_trig_timer_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("conv_type", (S_IRUGO | S_IWUSR | S_IWGRP),
				   ab8500_gpadc_dir, &plf->dev,
				   &ab8500_gpadc_conv_type_fops);
	if (!file)
		goto err;

	return 0;

err:
	debugfs_remove_recursive(ab8500_dir);
	dev_err(&plf->dev, "failed to create debugfs entries.\n");

	return -ENOMEM;
}

static int ab8500_debug_remove(struct platform_device *plf)
{
	debugfs_remove_recursive(ab8500_dir);

	return 0;
}

static struct platform_driver ab8500_debug_driver = {
	.driver = {
		.name = "ab8500-debug",
	},
	.probe  = ab8500_debug_probe,
	.remove = ab8500_debug_remove
};

static int __init ab8500_debug_init(void)
{
	return platform_driver_register(&ab8500_debug_driver);
}

static void __exit ab8500_debug_exit(void)
{
	platform_driver_unregister(&ab8500_debug_driver);
}
subsys_initcall(ab8500_debug_init);
module_exit(ab8500_debug_exit);

MODULE_AUTHOR("Mattias WALLIN <mattias.wallin@stericsson.com");
MODULE_DESCRIPTION("AB8500 DEBUG");
MODULE_LICENSE("GPL v2");
