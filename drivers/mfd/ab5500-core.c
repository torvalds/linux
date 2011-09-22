/*
 * Copyright (C) 2007-2011 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * Low-level core for exclusive access to the AB5500 IC on the I2C bus
 * and some basic chip-configuration.
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 * Author: Mattias Wallin <mattias.wallin@stericsson.com>
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>
 * Author: Karl Komierowski  <karl.komierowski@stericsson.com>
 * Author: Bibek Basu <bibek.basu@stericsson.com>
 *
 * TODO: Event handling with irq_chip. Waiting for PRCMU fw support.
 */

#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/mfd/ab5500/ab5500.h>
#include <linux/mfd/abx500.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/mfd/core.h>
#include <linux/version.h>
#include <linux/mfd/db5500-prcmu.h>

#define AB5500_NUM_EVENT_REG 23
#define AB5500_IT_LATCH0_REG 0x40
#define AB5500_IT_MASK0_REG 0x60

/* Read/write operation values. */
#define AB5500_PERM_RD (0x01)
#define AB5500_PERM_WR (0x02)

/* Read/write permissions. */
#define AB5500_PERM_RO (AB5500_PERM_RD)
#define AB5500_PERM_RW (AB5500_PERM_RD | AB5500_PERM_WR)

#define AB5500_MASK_BASE (0x60)
#define AB5500_MASK_END (0x79)
#define AB5500_CHIP_ID (0x20)

/**
 * struct ab5500_bank
 * @slave_addr: I2C slave_addr found in AB5500 specification
 * @name: Documentation name of the bank. For reference
 */
struct ab5500_bank {
	u8 slave_addr;
	const char *name;
};

static const struct ab5500_bank bankinfo[AB5500_NUM_BANKS] = {
	[AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP] = {
		AB5500_ADDR_VIT_IO_I2C_CLK_TST_OTP, "VIT_IO_I2C_CLK_TST_OTP"},
	[AB5500_BANK_VDDDIG_IO_I2C_CLK_TST] = {
		AB5500_ADDR_VDDDIG_IO_I2C_CLK_TST, "VDDDIG_IO_I2C_CLK_TST"},
	[AB5500_BANK_VDENC] = {AB5500_ADDR_VDENC, "VDENC"},
	[AB5500_BANK_SIM_USBSIM] = {AB5500_ADDR_SIM_USBSIM, "SIM_USBSIM"},
	[AB5500_BANK_LED] = {AB5500_ADDR_LED, "LED"},
	[AB5500_BANK_ADC] = {AB5500_ADDR_ADC, "ADC"},
	[AB5500_BANK_RTC] = {AB5500_ADDR_RTC, "RTC"},
	[AB5500_BANK_STARTUP] = {AB5500_ADDR_STARTUP, "STARTUP"},
	[AB5500_BANK_DBI_ECI] = {AB5500_ADDR_DBI_ECI, "DBI-ECI"},
	[AB5500_BANK_CHG] = {AB5500_ADDR_CHG, "CHG"},
	[AB5500_BANK_FG_BATTCOM_ACC] = {
		AB5500_ADDR_FG_BATTCOM_ACC, "FG_BATCOM_ACC"},
	[AB5500_BANK_USB] = {AB5500_ADDR_USB, "USB"},
	[AB5500_BANK_IT] = {AB5500_ADDR_IT, "IT"},
	[AB5500_BANK_VIBRA] = {AB5500_ADDR_VIBRA, "VIBRA"},
	[AB5500_BANK_AUDIO_HEADSETUSB] = {
		AB5500_ADDR_AUDIO_HEADSETUSB, "AUDIO_HEADSETUSB"},
};

/**
 * struct ab5500_reg_range
 * @first: the first address of the range
 * @last: the last address of the range
 * @perm: access permissions for the range
 */
struct ab5500_reg_range {
	u8 first;
	u8 last;
	u8 perm;
};

/**
 * struct ab5500_i2c_ranges
 * @count: the number of ranges in the list
 * @range: the list of register ranges
 */
struct ab5500_i2c_ranges {
	u8 nranges;
	u8 bankid;
	const struct ab5500_reg_range *range;
};

/**
 * struct ab5500_i2c_banks
 * @count: the number of ranges in the list
 * @range: the list of register ranges
 */
struct ab5500_i2c_banks {
	u8 nbanks;
	const struct ab5500_i2c_ranges *bank;
};

/*
 * Permissible register ranges for reading and writing per device and bank.
 *
 * The ranges must be listed in increasing address order, and no overlaps are
 * allowed. It is assumed that write permission implies read permission
 * (i.e. only RO and RW permissions should be used).  Ranges with write
 * permission must not be split up.
 */

#define NO_RANGE {.count = 0, .range = NULL,}
static struct ab5500_i2c_banks ab5500_bank_ranges[AB5500_NUM_DEVICES] = {
	[AB5500_DEVID_USB] =  {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_USB,
				.nranges = 12,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x01,
						.last = 0x01,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x80,
						.last = 0x83,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x87,
						.last = 0x8A,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x8B,
						.last = 0x8B,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x91,
						.last = 0x92,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x93,
						.last = 0x93,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x94,
						.last = 0x94,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0xA8,
						.last = 0xB0,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0xB2,
						.last = 0xB2,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0xB4,
						.last = 0xBC,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0xBF,
						.last = 0xBF,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0xC1,
						.last = 0xC5,
						.perm = AB5500_PERM_RO,
					},
				},
			},
		},
	},
	[AB5500_DEVID_ADC] =  {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_ADC,
				.nranges = 6,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x1F,
						.last = 0x22,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x23,
						.last = 0x24,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x26,
						.last = 0x2D,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x2F,
						.last = 0x34,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x37,
						.last = 0x57,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x58,
						.last = 0x58,
						.perm = AB5500_PERM_RO,
					},
				},
			},
		},
	},
	[AB5500_DEVID_LEDS] =  {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_LED,
				.nranges = 1,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x00,
						.last = 0x0C,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_VIDEO] =   {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_VDENC,
				.nranges = 12,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x00,
						.last = 0x08,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x09,
						.last = 0x09,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x0A,
						.last = 0x12,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x15,
						.last = 0x19,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x1B,
						.last = 0x21,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x27,
						.last = 0x2C,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x41,
						.last = 0x41,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x45,
						.last = 0x5B,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x5D,
						.last = 0x5D,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x69,
						.last = 0x69,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x6C,
						.last = 0x6D,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x80,
						.last = 0x81,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_REGULATORS] =   {
		.nbanks = 2,
		.bank =  (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_STARTUP,
				.nranges = 12,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x00,
						.last = 0x01,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x1F,
						.last = 0x1F,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x2E,
						.last = 0x2E,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x2F,
						.last = 0x30,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x50,
						.last = 0x51,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x60,
						.last = 0x61,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x66,
						.last = 0x8A,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x8C,
						.last = 0x96,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0xAA,
						.last = 0xB4,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0xB7,
						.last = 0xBF,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0xC1,
						.last = 0xCA,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0xD3,
						.last = 0xE0,
						.perm = AB5500_PERM_RW,
					},
				},
			},
			{
				.bankid = AB5500_BANK_SIM_USBSIM,
				.nranges = 1,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x13,
						.last = 0x19,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_SIM] =   {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_SIM_USBSIM,
				.nranges = 1,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x13,
						.last = 0x19,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_RTC] =   {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_RTC,
				.nranges = 2,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x00,
						.last = 0x04,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0x06,
						.last = 0x0C,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_CHARGER] =   {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_CHG,
				.nranges = 2,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x11,
						.last = 0x11,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x12,
						.last = 0x1B,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_FUELGAUGE] =   {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_FG_BATTCOM_ACC,
				.nranges = 2,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x00,
						.last = 0x0B,
						.perm = AB5500_PERM_RO,
					},
					{
						.first = 0x0C,
						.last = 0x10,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_VIBRATOR] =   {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_VIBRA,
				.nranges = 2,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x10,
						.last = 0x13,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0xFE,
						.last = 0xFE,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_CODEC] =   {
		.nbanks = 1,
		.bank = (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_AUDIO_HEADSETUSB,
				.nranges = 2,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x00,
						.last = 0x48,
						.perm = AB5500_PERM_RW,
					},
					{
						.first = 0xEB,
						.last = 0xFB,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
	[AB5500_DEVID_POWER] = {
		.nbanks	= 2,
		.bank	= (struct ab5500_i2c_ranges []) {
			{
				.bankid = AB5500_BANK_STARTUP,
				.nranges = 1,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x30,
						.last = 0x30,
						.perm = AB5500_PERM_RW,
					},
				},
			},
			{
				.bankid = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
				.nranges = 1,
				.range = (struct ab5500_reg_range[]) {
					{
						.first = 0x01,
						.last = 0x01,
						.perm = AB5500_PERM_RW,
					},
				},
			},
		},
	},
};

#define AB5500_IRQ(bank, bit)	((bank) * 8 + (bit))

/* I appologize for the resource names beeing a mix of upper case
 * and lower case but I want them to be exact as the documentation */
static struct mfd_cell ab5500_devs[AB5500_NUM_DEVICES] = {
	[AB5500_DEVID_LEDS] = {
		.name = "ab5500-leds",
		.id = AB5500_DEVID_LEDS,
	},
	[AB5500_DEVID_POWER] = {
		.name = "ab5500-power",
		.id = AB5500_DEVID_POWER,
	},
	[AB5500_DEVID_REGULATORS] = {
		.name = "ab5500-regulator",
		.id = AB5500_DEVID_REGULATORS,
	},
	[AB5500_DEVID_SIM] = {
		.name = "ab5500-sim",
		.id = AB5500_DEVID_SIM,
		.num_resources = 1,
		.resources = (struct resource[]) {
			{
				.name = "SIMOFF",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(2, 0), /*rising*/
				.end = AB5500_IRQ(2, 1), /*falling*/
			},
		},
	},
	[AB5500_DEVID_RTC] = {
		.name = "ab5500-rtc",
		.id = AB5500_DEVID_RTC,
		.num_resources = 1,
		.resources = (struct resource[]) {
			{
				.name	= "RTC_Alarm",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(1, 7),
				.end	= AB5500_IRQ(1, 7),
			}
		},
	},
	[AB5500_DEVID_CHARGER] = {
		.name = "ab5500-charger",
		.id = AB5500_DEVID_CHARGER,
	},
	[AB5500_DEVID_ADC] = {
		.name = "ab5500-adc",
		.id = AB5500_DEVID_ADC,
		.num_resources = 10,
		.resources = (struct resource[]) {
			{
				.name = "TRIGGER-0",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 0),
				.end = AB5500_IRQ(0, 0),
			},
			{
				.name = "TRIGGER-1",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 1),
				.end = AB5500_IRQ(0, 1),
			},
			{
				.name = "TRIGGER-2",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 2),
				.end = AB5500_IRQ(0, 2),
			},
			{
				.name = "TRIGGER-3",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 3),
				.end = AB5500_IRQ(0, 3),
			},
			{
				.name = "TRIGGER-4",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 4),
				.end = AB5500_IRQ(0, 4),
			},
			{
				.name = "TRIGGER-5",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 5),
				.end = AB5500_IRQ(0, 5),
			},
			{
				.name = "TRIGGER-6",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 6),
				.end = AB5500_IRQ(0, 6),
			},
			{
				.name = "TRIGGER-7",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 7),
				.end = AB5500_IRQ(0, 7),
			},
			{
				.name = "TRIGGER-VBAT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 8),
				.end = AB5500_IRQ(0, 8),
			},
			{
				.name = "TRIGGER-VBAT-TXON",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 9),
				.end = AB5500_IRQ(0, 9),
			},
		},
	},
	[AB5500_DEVID_FUELGAUGE] = {
		.name = "ab5500-fuelgauge",
		.id = AB5500_DEVID_FUELGAUGE,
		.num_resources = 6,
		.resources = (struct resource[]) {
			{
				.name = "Batt_attach",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 5),
				.end = AB5500_IRQ(7, 5),
			},
			{
				.name = "Batt_removal",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 6),
				.end = AB5500_IRQ(7, 6),
			},
			{
				.name = "UART_framing",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 7),
				.end = AB5500_IRQ(7, 7),
			},
			{
				.name = "UART_overrun",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 0),
				.end = AB5500_IRQ(8, 0),
			},
			{
				.name = "UART_Rdy_RX",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 1),
				.end = AB5500_IRQ(8, 1),
			},
			{
				.name = "UART_Rdy_TX",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 2),
				.end = AB5500_IRQ(8, 2),
			},
		},
	},
	[AB5500_DEVID_VIBRATOR] = {
		.name = "ab5500-vibrator",
		.id = AB5500_DEVID_VIBRATOR,
	},
	[AB5500_DEVID_CODEC] = {
		.name = "ab5500-codec",
		.id = AB5500_DEVID_CODEC,
		.num_resources = 3,
		.resources = (struct resource[]) {
			{
				.name = "audio_spkr1_ovc",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 5),
				.end = AB5500_IRQ(9, 5),
			},
			{
				.name = "audio_plllocked",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 6),
				.end = AB5500_IRQ(9, 6),
			},
			{
				.name = "audio_spkr2_ovc",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 4),
				.end = AB5500_IRQ(17, 4),
			},
		},
	},
	[AB5500_DEVID_USB] = {
		.name = "ab5500-usb",
		.id = AB5500_DEVID_USB,
		.num_resources = 36,
		.resources = (struct resource[]) {
			{
				.name = "Link_Update",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(22, 1),
				.end = AB5500_IRQ(22, 1),
			},
			{
				.name = "DCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 3),
				.end = AB5500_IRQ(8, 4),
			},
			{
				.name = "VBUS_R",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 5),
				.end = AB5500_IRQ(8, 5),
			},
			{
				.name = "VBUS_F",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 6),
				.end = AB5500_IRQ(8, 6),
			},
			{
				.name = "CHGstate_10_PCVBUSchg",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 7),
				.end = AB5500_IRQ(8, 7),
			},
			{
				.name = "DCIOreverse_ovc",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 0),
				.end = AB5500_IRQ(9, 0),
			},
			{
				.name = "USBCharDetDone",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 1),
				.end = AB5500_IRQ(9, 1),
			},
			{
				.name = "DCIO_no_limit",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 2),
				.end = AB5500_IRQ(9, 2),
			},
			{
				.name = "USB_suspend",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 3),
				.end = AB5500_IRQ(9, 3),
			},
			{
				.name = "DCIOreverse_fwdcurrent",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 4),
				.end = AB5500_IRQ(9, 4),
			},
			{
				.name = "Vbus_Imeasmax_change",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 5),
				.end = AB5500_IRQ(9, 6),
			},
			{
				.name = "OVV",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 5),
				.end = AB5500_IRQ(14, 5),
			},
			{
				.name = "USBcharging_NOTok",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 3),
				.end = AB5500_IRQ(15, 3),
			},
			{
				.name = "usb_adp_sensoroff",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 6),
				.end = AB5500_IRQ(15, 6),
			},
			{
				.name = "usb_adp_probeplug",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 7),
				.end = AB5500_IRQ(15, 7),
			},
			{
				.name = "usb_adp_sinkerror",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 0),
				.end = AB5500_IRQ(16, 6),
			},
			{
				.name = "usb_adp_sourceerror",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 1),
				.end = AB5500_IRQ(16, 1),
			},
			{
				.name = "usb_idgnd_r",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 2),
				.end = AB5500_IRQ(16, 2),
			},
			{
				.name = "usb_idgnd_f",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 3),
				.end = AB5500_IRQ(16, 3),
			},
			{
				.name = "usb_iddetR1",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 4),
				.end = AB5500_IRQ(16, 5),
			},
			{
				.name = "usb_iddetR2",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 6),
				.end = AB5500_IRQ(16, 7),
			},
			{
				.name = "usb_iddetR3",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 0),
				.end = AB5500_IRQ(17, 1),
			},
			{
				.name = "usb_iddetR4",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 2),
				.end = AB5500_IRQ(17, 3),
			},
			{
				.name = "CharTempWindowOk",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 7),
				.end = AB5500_IRQ(18, 0),
			},
			{
				.name = "USB_SprDetect",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 1),
				.end = AB5500_IRQ(18, 1),
			},
			{
				.name = "usb_adp_probe_unplug",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 2),
				.end = AB5500_IRQ(18, 2),
			},
			{
				.name = "VBUSChDrop",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 3),
				.end = AB5500_IRQ(18, 4),
			},
			{
				.name = "dcio_char_rec_done",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 5),
				.end = AB5500_IRQ(18, 5),
			},
			{
				.name = "Charging_stopped_by_temp",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 6),
				.end = AB5500_IRQ(18, 6),
			},
			{
				.name = "CHGstate_11_SafeModeVBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 1),
				.end = AB5500_IRQ(21, 2),
			},
			{
				.name = "CHGstate_12_comletedVBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 2),
				.end = AB5500_IRQ(21, 2),
			},
			{
				.name = "CHGstate_13_completedVBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 3),
				.end = AB5500_IRQ(21, 3),
			},
			{
				.name = "CHGstate_14_FullChgDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 4),
				.end = AB5500_IRQ(21, 4),
			},
			{
				.name = "CHGstate_15_SafeModeDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 5),
				.end = AB5500_IRQ(21, 5),
			},
			{
				.name = "CHGstate_16_OFFsuspendDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 6),
				.end = AB5500_IRQ(21, 6),
			},
			{
				.name = "CHGstate_17_completedDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 7),
				.end = AB5500_IRQ(21, 7),
			},
		},
	},
	[AB5500_DEVID_OTP] = {
		.name = "ab5500-otp",
		.id = AB5500_DEVID_OTP,
	},
	[AB5500_DEVID_VIDEO] = {
		.name = "ab5500-video",
		.id = AB5500_DEVID_VIDEO,
		.num_resources = 1,
		.resources = (struct resource[]) {
			{
				.name = "plugTVdet",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(22, 2),
				.end = AB5500_IRQ(22, 2),
			},
		},
	},
	[AB5500_DEVID_DBIECI] = {
		.name = "ab5500-dbieci",
		.id = AB5500_DEVID_DBIECI,
		.num_resources = 10,
		.resources = (struct resource[]) {
			{
				.name = "COLL",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 0),
				.end = AB5500_IRQ(14, 0),
			},
			{
				.name = "RESERR",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 1),
				.end = AB5500_IRQ(14, 1),
			},
			{
				.name = "FRAERR",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 2),
				.end = AB5500_IRQ(14, 2),
			},
			{
				.name = "COMERR",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 3),
				.end = AB5500_IRQ(14, 3),
			},
			{
				.name = "BSI_indicator",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 4),
				.end = AB5500_IRQ(14, 4),
			},
			{
				.name = "SPDSET",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 6),
				.end = AB5500_IRQ(14, 6),
			},
			{
				.name = "DSENT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 7),
				.end = AB5500_IRQ(14, 7),
			},
			{
				.name = "DREC",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 0),
				.end = AB5500_IRQ(15, 0),
			},
			{
				.name = "ACCINT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 1),
				.end = AB5500_IRQ(15, 1),
			},
			{
				.name = "NOPINT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 2),
				.end = AB5500_IRQ(15, 2),
			},
		},
	},
	[AB5500_DEVID_ONSWA] = {
		.name = "ab5500-onswa",
		.id = AB5500_DEVID_ONSWA,
		.num_resources = 2,
		.resources = (struct resource[]) {
			{
				.name	= "ONSWAn_rising",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(1, 3),
				.end	= AB5500_IRQ(1, 3),
			},
			{
				.name	= "ONSWAn_falling",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(1, 4),
				.end	= AB5500_IRQ(1, 4),
			},
		},
	},
};

/*
 * Functionality for getting/setting register values.
 */
static int get_register_interruptible(struct ab5500 *ab, u8 bank, u8 reg,
	u8 *value)
{
	int err;

	if (bank >= AB5500_NUM_BANKS)
		return -EINVAL;

	err = mutex_lock_interruptible(&ab->access_mutex);
	if (err)
		return err;
	err = db5500_prcmu_abb_read(bankinfo[bank].slave_addr, reg, value, 1);

	mutex_unlock(&ab->access_mutex);
	return err;
}

static int get_register_page_interruptible(struct ab5500 *ab, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs)
{
	int err;

	if (bank >= AB5500_NUM_BANKS)
		return -EINVAL;

	err = mutex_lock_interruptible(&ab->access_mutex);
	if (err)
		return err;

	while (numregs) {
		/* The hardware limit for get page is 4 */
		u8 curnum = min_t(u8, numregs, 4u);

		err = db5500_prcmu_abb_read(bankinfo[bank].slave_addr,
					    first_reg, regvals, curnum);
		if (err)
			goto out;

		numregs -= curnum;
		first_reg += curnum;
		regvals += curnum;
	}

out:
	mutex_unlock(&ab->access_mutex);
	return err;
}

static int mask_and_set_register_interruptible(struct ab5500 *ab, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues)
{
	int err = 0;

	if (bank >= AB5500_NUM_BANKS)
		return -EINVAL;

	if (bitmask) {
		u8 buf;

		err = mutex_lock_interruptible(&ab->access_mutex);
		if (err)
			return err;

		if (bitmask == 0xFF) /* No need to read in this case. */
			buf = bitvalues;
		else { /* Read and modify the register value. */
			err = db5500_prcmu_abb_read(bankinfo[bank].slave_addr,
				reg, &buf, 1);
			if (err)
				return err;

			buf = ((~bitmask & buf) | (bitmask & bitvalues));
		}
		/* Write the new value. */
		err = db5500_prcmu_abb_write(bankinfo[bank].slave_addr, reg,
					     &buf, 1);

		mutex_unlock(&ab->access_mutex);
	}
	return err;
}

static int
set_register_interruptible(struct ab5500 *ab, u8 bank, u8 reg, u8 value)
{
	return mask_and_set_register_interruptible(ab, bank, reg, 0xff, value);
}

/*
 * Read/write permission checking functions.
 */
static const struct ab5500_i2c_ranges *get_bankref(u8 devid, u8 bank)
{
	u8 i;

	if (devid < AB5500_NUM_DEVICES) {
		for (i = 0; i < ab5500_bank_ranges[devid].nbanks; i++) {
			if (ab5500_bank_ranges[devid].bank[i].bankid == bank)
				return &ab5500_bank_ranges[devid].bank[i];
		}
	}
	return NULL;
}

static bool page_write_allowed(u8 devid, u8 bank, u8 first_reg, u8 last_reg)
{
	u8 i; /* range loop index */
	const struct ab5500_i2c_ranges *bankref;

	bankref = get_bankref(devid, bank);
	if (bankref == NULL || last_reg < first_reg)
		return false;

	for (i = 0; i < bankref->nranges; i++) {
		if (first_reg < bankref->range[i].first)
			break;
		if ((last_reg <= bankref->range[i].last) &&
			(bankref->range[i].perm & AB5500_PERM_WR))
			return true;
	}
	return false;
}

static bool reg_write_allowed(u8 devid, u8 bank, u8 reg)
{
	return page_write_allowed(devid, bank, reg, reg);
}

static bool page_read_allowed(u8 devid, u8 bank, u8 first_reg, u8 last_reg)
{
	u8 i;
	const struct ab5500_i2c_ranges *bankref;

	bankref = get_bankref(devid, bank);
	if (bankref == NULL || last_reg < first_reg)
		return false;


	/* Find the range (if it exists in the list) that includes first_reg. */
	for (i = 0; i < bankref->nranges; i++) {
		if (first_reg < bankref->range[i].first)
			return false;
		if (first_reg <= bankref->range[i].last)
			break;
	}
	/* Make sure that the entire range up to and including last_reg is
	 * readable. This may span several of the ranges in the list.
	 */
	while ((i < bankref->nranges) &&
		(bankref->range[i].perm & AB5500_PERM_RD)) {
		if (last_reg <= bankref->range[i].last)
			return true;
		if ((++i >= bankref->nranges) ||
			(bankref->range[i].first !=
				(bankref->range[i - 1].last + 1))) {
			break;
		}
	}
	return false;
}

static bool reg_read_allowed(u8 devid, u8 bank, u8 reg)
{
	return page_read_allowed(devid, bank, reg, reg);
}


/*
 * The exported register access functionality.
 */
static int ab5500_get_chip_id(struct device *dev)
{
	struct ab5500 *ab = dev_get_drvdata(dev->parent);

	return (int)ab->chip_id;
}

static int ab5500_mask_and_set_register_interruptible(struct device *dev,
		u8 bank, u8 reg, u8 bitmask, u8 bitvalues)
{
	struct ab5500 *ab;
	struct platform_device *pdev = to_platform_device(dev);

	if ((AB5500_NUM_BANKS <= bank) ||
		!reg_write_allowed(pdev->id, bank, reg))
		return -EINVAL;

	ab = dev_get_drvdata(dev->parent);
	return mask_and_set_register_interruptible(ab, bank, reg,
		bitmask, bitvalues);
}

static int ab5500_set_register_interruptible(struct device *dev, u8 bank,
	u8 reg, u8 value)
{
	return ab5500_mask_and_set_register_interruptible(dev, bank, reg, 0xFF,
		value);
}

static int ab5500_get_register_interruptible(struct device *dev, u8 bank,
		u8 reg, u8 *value)
{
	struct ab5500 *ab;
	struct platform_device *pdev = to_platform_device(dev);

	if ((AB5500_NUM_BANKS <= bank) ||
		!reg_read_allowed(pdev->id, bank, reg))
		return -EINVAL;

	ab = dev_get_drvdata(dev->parent);
	return get_register_interruptible(ab, bank, reg, value);
}

static int ab5500_get_register_page_interruptible(struct device *dev, u8 bank,
		u8 first_reg, u8 *regvals, u8 numregs)
{
	struct ab5500 *ab;
	struct platform_device *pdev = to_platform_device(dev);

	if ((AB5500_NUM_BANKS <= bank) ||
		!page_read_allowed(pdev->id, bank,
			first_reg, (first_reg + numregs - 1)))
		return -EINVAL;

	ab = dev_get_drvdata(dev->parent);
	return get_register_page_interruptible(ab, bank, first_reg, regvals,
		numregs);
}

static int
ab5500_event_registers_startup_state_get(struct device *dev, u8 *event)
{
	struct ab5500 *ab;

	ab = dev_get_drvdata(dev->parent);
	if (!ab->startup_events_read)
		return -EAGAIN; /* Try again later */

	memcpy(event, ab->startup_events, AB5500_NUM_EVENT_REG);
	return 0;
}

static struct abx500_ops ab5500_ops = {
	.get_chip_id = ab5500_get_chip_id,
	.get_register = ab5500_get_register_interruptible,
	.set_register = ab5500_set_register_interruptible,
	.get_register_page = ab5500_get_register_page_interruptible,
	.set_register_page = NULL,
	.mask_and_set_register = ab5500_mask_and_set_register_interruptible,
	.event_registers_startup_state_get =
		ab5500_event_registers_startup_state_get,
	.startup_irq_enabled = NULL,
};

#ifdef CONFIG_DEBUG_FS
static struct ab5500_i2c_ranges ab5500_reg_ranges[AB5500_NUM_BANKS] = {
	[AB5500_BANK_LED] = {
		.bankid = AB5500_BANK_LED,
		.nranges = 1,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0C,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_ADC] = {
		.bankid = AB5500_BANK_ADC,
		.nranges = 6,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x1F,
				.last = 0x22,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x23,
				.last = 0x24,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x26,
				.last = 0x2D,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x2F,
				.last = 0x34,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x37,
				.last = 0x57,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x58,
				.last = 0x58,
				.perm = AB5500_PERM_RO,
			},
		},
	},
	[AB5500_BANK_RTC] = {
		.bankid = AB5500_BANK_RTC,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x04,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x06,
				.last = 0x0C,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_STARTUP] = {
		.bankid = AB5500_BANK_STARTUP,
		.nranges = 12,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x01,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x1F,
				.last = 0x1F,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x2E,
				.last = 0x2E,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x2F,
				.last = 0x30,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x50,
				.last = 0x51,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x60,
				.last = 0x61,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x66,
				.last = 0x8A,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x8C,
				.last = 0x96,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0xAA,
				.last = 0xB4,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0xB7,
				.last = 0xBF,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0xC1,
				.last = 0xCA,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0xD3,
				.last = 0xE0,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_DBI_ECI] = {
		.bankid = AB5500_BANK_DBI_ECI,
		.nranges = 3,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x07,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x10,
				.last = 0x10,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x13,
				.last = 0x13,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_CHG] = {
		.bankid = AB5500_BANK_CHG,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x11,
				.last = 0x11,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x12,
				.last = 0x1B,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_FG_BATTCOM_ACC] = {
		.bankid = AB5500_BANK_FG_BATTCOM_ACC,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0B,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x0C,
				.last = 0x10,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_USB] = {
		.bankid = AB5500_BANK_USB,
		.nranges = 12,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x01,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x80,
				.last = 0x83,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x87,
				.last = 0x8A,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x8B,
				.last = 0x8B,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x91,
				.last = 0x92,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x93,
				.last = 0x93,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x94,
				.last = 0x94,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0xA8,
				.last = 0xB0,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0xB2,
				.last = 0xB2,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0xB4,
				.last = 0xBC,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0xBF,
				.last = 0xBF,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0xC1,
				.last = 0xC5,
				.perm = AB5500_PERM_RO,
			},
		},
	},
	[AB5500_BANK_IT] = {
		.bankid = AB5500_BANK_IT,
		.nranges = 4,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x02,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x20,
				.last = 0x36,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x40,
				.last = 0x56,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x60,
				.last = 0x76,
				.perm = AB5500_PERM_RO,
			},
		},
	},
	[AB5500_BANK_VDDDIG_IO_I2C_CLK_TST] = {
		.bankid = AB5500_BANK_VDDDIG_IO_I2C_CLK_TST,
		.nranges = 7,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x02,
				.last = 0x02,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x12,
				.last = 0x12,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x30,
				.last = 0x34,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x40,
				.last = 0x44,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x50,
				.last = 0x54,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x60,
				.last = 0x64,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x70,
				.last = 0x74,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP] = {
		.bankid = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		.nranges = 13,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x01,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x02,
				.last = 0x02,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x0D,
				.last = 0x0F,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x1C,
				.last = 0x1C,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x1E,
				.last = 0x1E,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x20,
				.last = 0x21,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x25,
				.last = 0x25,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x28,
				.last = 0x2A,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x30,
				.last = 0x33,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x40,
				.last = 0x43,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x50,
				.last = 0x53,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x60,
				.last = 0x63,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x70,
				.last = 0x73,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_VIBRA] = {
		.bankid = AB5500_BANK_VIBRA,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x10,
				.last = 0x13,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0xFE,
				.last = 0xFE,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_AUDIO_HEADSETUSB] = {
		.bankid = AB5500_BANK_AUDIO_HEADSETUSB,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x48,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0xEB,
				.last = 0xFB,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_SIM_USBSIM] = {
		.bankid = AB5500_BANK_SIM_USBSIM,
		.nranges = 1,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x13,
				.last = 0x19,
				.perm = AB5500_PERM_RW,
			},
		},
	},
	[AB5500_BANK_VDENC] = {
		.bankid = AB5500_BANK_VDENC,
		.nranges = 12,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x08,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x09,
				.last = 0x09,
				.perm = AB5500_PERM_RO,
			},
			{
				.first = 0x0A,
				.last = 0x12,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x15,
				.last = 0x19,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x1B,
				.last = 0x21,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x27,
				.last = 0x2C,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x41,
				.last = 0x41,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x45,
				.last = 0x5B,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x5D,
				.last = 0x5D,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x69,
				.last = 0x69,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x6C,
				.last = 0x6D,
				.perm = AB5500_PERM_RW,
			},
			{
				.first = 0x80,
				.last = 0x81,
				.perm = AB5500_PERM_RW,
			},
		},
	},
};
static int ab5500_registers_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;
	unsigned int i;
	u8 bank = (u8)ab->debug_bank;

	seq_printf(s, "ab5500 register values:\n");
	for (bank = 0; bank < AB5500_NUM_BANKS; bank++) {
		seq_printf(s, " bank %u, %s (0x%x):\n", bank,
				bankinfo[bank].name,
				bankinfo[bank].slave_addr);
		for (i = 0; i < ab5500_reg_ranges[bank].nranges; i++) {
			u8 reg;
			int err;

			for (reg = ab5500_reg_ranges[bank].range[i].first;
				reg <= ab5500_reg_ranges[bank].range[i].last;
				reg++) {
				u8 value;

				err = get_register_interruptible(ab, bank, reg,
						&value);
				if (err < 0) {
					dev_err(ab->dev, "get_reg failed %d"
						"bank 0x%x reg 0x%x\n",
						err, bank, reg);
					return err;
				}

				err = seq_printf(s, "[%d/0x%02X]: 0x%02X\n",
						bank, reg, value);
				if (err < 0) {
					dev_err(ab->dev,
						"seq_printf overflow\n");
					/*
					 * Error is not returned here since
					 * the output is wanted in any case
					 */
					return 0;
				}
			}
		}
	}
	return 0;
}

static int ab5500_registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_registers_print, inode->i_private);
}

static const struct file_operations ab5500_registers_fops = {
	.open = ab5500_registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab5500_bank_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;

	seq_printf(s, "%d\n", ab->debug_bank);
	return 0;
}

static int ab5500_bank_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_bank_print, inode->i_private);
}

static ssize_t ab5500_bank_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab5500 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_bank;
	int err;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_bank);
	if (err)
		return -EINVAL;

	if (user_bank >= AB5500_NUM_BANKS) {
		dev_err(ab->dev,
			"debugfs error input > number of banks\n");
		return -EINVAL;
	}

	ab->debug_bank = user_bank;

	return buf_size;
}

static int ab5500_address_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;

	seq_printf(s, "0x%02X\n", ab->debug_address);
	return 0;
}

static int ab5500_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_address_print, inode->i_private);
}

static ssize_t ab5500_address_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab5500 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_address;
	int err;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_address);
	if (err)
		return -EINVAL;
	if (user_address > 0xff) {
		dev_err(ab->dev,
			"debugfs error input > 0xff\n");
		return -EINVAL;
	}
	ab->debug_address = user_address;
	return buf_size;
}

static int ab5500_val_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;
	int err;
	u8 regvalue;

	err = get_register_interruptible(ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, &regvalue);
	if (err) {
		dev_err(ab->dev, "get_reg failed %d, bank 0x%x"
			", reg 0x%x\n", err, ab->debug_bank,
			ab->debug_address);
		return -EINVAL;
	}
	seq_printf(s, "0x%02X\n", regvalue);

	return 0;
}

static int ab5500_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_val_print, inode->i_private);
}

static ssize_t ab5500_val_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab5500 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_val;
	int err;
	u8 regvalue;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_val);
	if (err)
		return -EINVAL;
	if (user_val > 0xff) {
		dev_err(ab->dev,
			"debugfs error input > 0xff\n");
		return -EINVAL;
	}
	err = mask_and_set_register_interruptible(
		ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, 0xFF, (u8)user_val);
	if (err)
		return -EINVAL;

	get_register_interruptible(ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, &regvalue);
	if (err)
		return -EINVAL;

	return buf_size;
}

static const struct file_operations ab5500_bank_fops = {
	.open = ab5500_bank_open,
	.write = ab5500_bank_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab5500_address_fops = {
	.open = ab5500_address_open,
	.write = ab5500_address_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab5500_val_fops = {
	.open = ab5500_val_open,
	.write = ab5500_val_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *ab5500_dir;
static struct dentry *ab5500_reg_file;
static struct dentry *ab5500_bank_file;
static struct dentry *ab5500_address_file;
static struct dentry *ab5500_val_file;

static inline void ab5500_setup_debugfs(struct ab5500 *ab)
{
	ab->debug_bank = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP;
	ab->debug_address = AB5500_CHIP_ID;

	ab5500_dir = debugfs_create_dir("ab5500", NULL);
	if (!ab5500_dir)
		goto exit_no_debugfs;

	ab5500_reg_file = debugfs_create_file("all-bank-registers",
		S_IRUGO, ab5500_dir, ab, &ab5500_registers_fops);
	if (!ab5500_reg_file)
		goto exit_destroy_dir;

	ab5500_bank_file = debugfs_create_file("register-bank",
		(S_IRUGO | S_IWUGO), ab5500_dir, ab, &ab5500_bank_fops);
	if (!ab5500_bank_file)
		goto exit_destroy_reg;

	ab5500_address_file = debugfs_create_file("register-address",
		(S_IRUGO | S_IWUGO), ab5500_dir, ab, &ab5500_address_fops);
	if (!ab5500_address_file)
		goto exit_destroy_bank;

	ab5500_val_file = debugfs_create_file("register-value",
		(S_IRUGO | S_IWUGO), ab5500_dir, ab, &ab5500_val_fops);
	if (!ab5500_val_file)
		goto exit_destroy_address;

	return;

exit_destroy_address:
	debugfs_remove(ab5500_address_file);
exit_destroy_bank:
	debugfs_remove(ab5500_bank_file);
exit_destroy_reg:
	debugfs_remove(ab5500_reg_file);
exit_destroy_dir:
	debugfs_remove(ab5500_dir);
exit_no_debugfs:
	dev_err(ab->dev, "failed to create debugfs entries.\n");
	return;
}

static inline void ab5500_remove_debugfs(void)
{
	debugfs_remove(ab5500_val_file);
	debugfs_remove(ab5500_address_file);
	debugfs_remove(ab5500_bank_file);
	debugfs_remove(ab5500_reg_file);
	debugfs_remove(ab5500_dir);
}

#else /* !CONFIG_DEBUG_FS */
static inline void ab5500_setup_debugfs(struct ab5500 *ab)
{
}
static inline void ab5500_remove_debugfs(void)
{
}
#endif

/*
 * ab5500_setup : Basic set-up, datastructure creation/destruction
 *		  and I2C interface.This sets up a default config
 *		  in the AB5500 chip so that it will work as expected.
 * @ab :	  Pointer to ab5500 structure
 * @settings :    Pointer to struct abx500_init_settings
 * @size :        Size of init data
 */
static int __init ab5500_setup(struct ab5500 *ab,
	struct abx500_init_settings *settings, unsigned int size)
{
	int err = 0;
	int i;

	for (i = 0; i < size; i++) {
		err = mask_and_set_register_interruptible(ab,
			settings[i].bank,
			settings[i].reg,
			0xFF, settings[i].setting);
		if (err)
			goto exit_no_setup;

		/* If event mask register update the event mask in ab5500 */
		if ((settings[i].bank == AB5500_BANK_IT) &&
			(AB5500_MASK_BASE <= settings[i].reg) &&
			(settings[i].reg <= AB5500_MASK_END)) {
			ab->mask[settings[i].reg - AB5500_MASK_BASE] =
				settings[i].setting;
		}
	}
exit_no_setup:
	return err;
}

struct ab_family_id {
	u8	id;
	char	*name;
};

static const struct ab_family_id ids[] __initdata = {
	/* AB5500 */
	{
		.id = AB5500_1_0,
		.name = "1.0"
	},
	{
		.id = AB5500_1_1,
		.name = "1.1"
	},
	/* Terminator */
	{
		.id = 0x00,
	}
};

static int __init ab5500_probe(struct platform_device *pdev)
{
	struct ab5500 *ab;
	struct ab5500_platform_data *ab5500_plf_data =
		pdev->dev.platform_data;
	int err;
	int i;

	ab = kzalloc(sizeof(struct ab5500), GFP_KERNEL);
	if (!ab) {
		dev_err(&pdev->dev,
			"could not allocate ab5500 device\n");
		return -ENOMEM;
	}

	/* Initialize data structure */
	mutex_init(&ab->access_mutex);
	mutex_init(&ab->irq_lock);
	ab->dev = &pdev->dev;

	platform_set_drvdata(pdev, ab);

	/* Read chip ID register */
	err = get_register_interruptible(ab, AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		AB5500_CHIP_ID, &ab->chip_id);
	if (err) {
		dev_err(&pdev->dev, "could not communicate with the analog "
			"baseband chip\n");
		goto exit_no_detect;
	}

	for (i = 0; ids[i].id != 0x0; i++) {
		if (ids[i].id == ab->chip_id) {
			snprintf(&ab->chip_name[0], sizeof(ab->chip_name) - 1,
				"AB5500 %s", ids[i].name);
			break;
		}
	}
	if (ids[i].id == 0x0) {
		dev_err(&pdev->dev, "unknown analog baseband chip id: 0x%x\n",
			ab->chip_id);
		dev_err(&pdev->dev, "driver not started!\n");
		goto exit_no_detect;
	}

	/* Clear and mask all interrupts */
	for (i = 0; i < AB5500_NUM_IRQ_REGS; i++) {
		u8 latchreg = AB5500_IT_LATCH0_REG + i;
		u8 maskreg = AB5500_IT_MASK0_REG + i;
		u8 val;

		get_register_interruptible(ab, AB5500_BANK_IT, latchreg, &val);
		set_register_interruptible(ab, AB5500_BANK_IT, maskreg, 0xff);
		ab->mask[i] = ab->oldmask[i] = 0xff;
	}

	err = abx500_register_ops(&pdev->dev, &ab5500_ops);
	if (err) {
		dev_err(&pdev->dev, "ab5500_register ops error\n");
		goto exit_no_detect;
	}

	/* Set up and register the platform devices. */
	for (i = 0; i < AB5500_NUM_DEVICES; i++) {
		ab5500_devs[i].platform_data = ab5500_plf_data->dev_data[i];
		ab5500_devs[i].pdata_size =
			sizeof(ab5500_plf_data->dev_data[i]);
	}

	err = mfd_add_devices(&pdev->dev, 0, ab5500_devs,
		ARRAY_SIZE(ab5500_devs), NULL,
		ab5500_plf_data->irq.base);
	if (err) {
		dev_err(&pdev->dev, "ab5500_mfd_add_device error\n");
		goto exit_no_detect;
	}

	err = ab5500_setup(ab, ab5500_plf_data->init_settings,
		ab5500_plf_data->init_settings_sz);
	if (err) {
		dev_err(&pdev->dev, "ab5500_setup error\n");
		goto exit_no_detect;
	}

	ab5500_setup_debugfs(ab);

	dev_info(&pdev->dev, "detected AB chip: %s\n", &ab->chip_name[0]);
	return 0;

exit_no_detect:
	kfree(ab);
	return err;
}

static int __exit ab5500_remove(struct platform_device *pdev)
{
	struct ab5500 *ab = platform_get_drvdata(pdev);

	ab5500_remove_debugfs();
	mfd_remove_devices(&pdev->dev);
	kfree(ab);
	return 0;
}

static struct platform_driver ab5500_driver = {
	.driver = {
		.name = "ab5500-core",
		.owner = THIS_MODULE,
	},
	.remove  = __exit_p(ab5500_remove),
};

static int __init ab5500_core_init(void)
{
	return platform_driver_probe(&ab5500_driver, ab5500_probe);
}

static void __exit ab5500_core_exit(void)
{
	platform_driver_unregister(&ab5500_driver);
}

subsys_initcall(ab5500_core_init);
module_exit(ab5500_core_exit);

MODULE_AUTHOR("Mattias Wallin <mattias.wallin@stericsson.com>");
MODULE_DESCRIPTION("AB5500 core driver");
MODULE_LICENSE("GPL");
