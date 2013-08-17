/*
 * Copyright (C) 2011 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * Shared definitions and data structures for the AB5500 MFD driver
 */

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

int ab5500_get_register_interruptible_raw(struct ab5500 *ab, u8 bank, u8 reg,
	u8 *value);
int ab5500_mask_and_set_register_interruptible_raw(struct ab5500 *ab, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues);
