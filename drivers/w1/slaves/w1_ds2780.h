/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 1-Wire implementation for the ds2780 chip
 *
 * Copyright (C) 2010 Indesign, LLC
 *
 * Author: Clifton Barnes <cabarnes@indesign-llc.com>
 *
 * Based on w1-ds2760 driver
 */

#ifndef _W1_DS2780_H
#define _W1_DS2780_H

/* Function commands */
#define W1_DS2780_READ_DATA		0x69
#define W1_DS2780_WRITE_DATA		0x6C
#define W1_DS2780_COPY_DATA		0x48
#define W1_DS2780_RECALL_DATA		0xB8
#define W1_DS2780_LOCK			0x6A

/* Register map */
/* Register 0x00 Reserved */
#define DS2780_STATUS_REG		0x01
#define DS2780_RAAC_MSB_REG		0x02
#define DS2780_RAAC_LSB_REG		0x03
#define DS2780_RSAC_MSB_REG		0x04
#define DS2780_RSAC_LSB_REG		0x05
#define DS2780_RARC_REG			0x06
#define DS2780_RSRC_REG			0x07
#define DS2780_IAVG_MSB_REG		0x08
#define DS2780_IAVG_LSB_REG		0x09
#define DS2780_TEMP_MSB_REG		0x0A
#define DS2780_TEMP_LSB_REG		0x0B
#define DS2780_VOLT_MSB_REG		0x0C
#define DS2780_VOLT_LSB_REG		0x0D
#define DS2780_CURRENT_MSB_REG		0x0E
#define DS2780_CURRENT_LSB_REG		0x0F
#define DS2780_ACR_MSB_REG		0x10
#define DS2780_ACR_LSB_REG		0x11
#define DS2780_ACRL_MSB_REG		0x12
#define DS2780_ACRL_LSB_REG		0x13
#define DS2780_AS_REG			0x14
#define DS2780_SFR_REG			0x15
#define DS2780_FULL_MSB_REG		0x16
#define DS2780_FULL_LSB_REG		0x17
#define DS2780_AE_MSB_REG		0x18
#define DS2780_AE_LSB_REG		0x19
#define DS2780_SE_MSB_REG		0x1A
#define DS2780_SE_LSB_REG		0x1B
/* Register 0x1C - 0x1E Reserved */
#define DS2780_EEPROM_REG		0x1F
#define DS2780_EEPROM_BLOCK0_START	0x20
/* Register 0x20 - 0x2F User EEPROM */
#define DS2780_EEPROM_BLOCK0_END	0x2F
/* Register 0x30 - 0x5F Reserved */
#define DS2780_EEPROM_BLOCK1_START	0x60
#define DS2780_CONTROL_REG		0x60
#define DS2780_AB_REG			0x61
#define DS2780_AC_MSB_REG		0x62
#define DS2780_AC_LSB_REG		0x63
#define DS2780_VCHG_REG			0x64
#define DS2780_IMIN_REG			0x65
#define DS2780_VAE_REG			0x66
#define DS2780_IAE_REG			0x67
#define DS2780_AE_40_REG		0x68
#define DS2780_RSNSP_REG		0x69
#define DS2780_FULL_40_MSB_REG		0x6A
#define DS2780_FULL_40_LSB_REG		0x6B
#define DS2780_FULL_3040_SLOPE_REG	0x6C
#define DS2780_FULL_2030_SLOPE_REG	0x6D
#define DS2780_FULL_1020_SLOPE_REG	0x6E
#define DS2780_FULL_0010_SLOPE_REG	0x6F
#define DS2780_AE_3040_SLOPE_REG	0x70
#define DS2780_AE_2030_SLOPE_REG	0x71
#define DS2780_AE_1020_SLOPE_REG	0x72
#define DS2780_AE_0010_SLOPE_REG	0x73
#define DS2780_SE_3040_SLOPE_REG	0x74
#define DS2780_SE_2030_SLOPE_REG	0x75
#define DS2780_SE_1020_SLOPE_REG	0x76
#define DS2780_SE_0010_SLOPE_REG	0x77
#define DS2780_RSGAIN_MSB_REG		0x78
#define DS2780_RSGAIN_LSB_REG		0x79
#define DS2780_RSTC_REG			0x7A
#define DS2780_FRSGAIN_MSB_REG		0x7B
#define DS2780_FRSGAIN_LSB_REG		0x7C
#define DS2780_EEPROM_BLOCK1_END	0x7C
/* Register 0x7D - 0xFF Reserved */

/* Number of valid register addresses */
#define DS2780_DATA_SIZE		0x80

/* Status register bits */
#define DS2780_STATUS_REG_CHGTF		(1 << 7)
#define DS2780_STATUS_REG_AEF		(1 << 6)
#define DS2780_STATUS_REG_SEF		(1 << 5)
#define DS2780_STATUS_REG_LEARNF	(1 << 4)
/* Bit 3 Reserved */
#define DS2780_STATUS_REG_UVF		(1 << 2)
#define DS2780_STATUS_REG_PORF		(1 << 1)
/* Bit 0 Reserved */

/* Control register bits */
/* Bit 7 Reserved */
#define DS2780_CONTROL_REG_UVEN		(1 << 6)
#define DS2780_CONTROL_REG_PMOD		(1 << 5)
#define DS2780_CONTROL_REG_RNAOP	(1 << 4)
/* Bit 0 - 3 Reserved */

/* Special feature register bits */
/* Bit 1 - 7 Reserved */
#define DS2780_SFR_REG_PIOSC		(1 << 0)

/* EEPROM register bits */
#define DS2780_EEPROM_REG_EEC		(1 << 7)
#define DS2780_EEPROM_REG_LOCK		(1 << 6)
/* Bit 2 - 6 Reserved */
#define DS2780_EEPROM_REG_BL1		(1 << 1)
#define DS2780_EEPROM_REG_BL0		(1 << 0)

extern int w1_ds2780_io(struct device *dev, char *buf, int addr, size_t count,
			int io);
extern int w1_ds2780_eeprom_cmd(struct device *dev, int addr, int cmd);

#endif /* !_W1_DS2780_H */
