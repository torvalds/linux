/*
 * 1-Wire implementation for the ds2780 chip
 *
 * Author: Renata Sayakhova <renata@oktetlabs.ru>
 *
 * Based on w1-ds2760 driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _W1_DS2781_H
#define _W1_DS2781_H

/* Function commands */
#define W1_DS2781_READ_DATA		0x69
#define W1_DS2781_WRITE_DATA		0x6C
#define W1_DS2781_COPY_DATA		0x48
#define W1_DS2781_RECALL_DATA		0xB8
#define W1_DS2781_LOCK			0x6A

/* Register map */
/* Register 0x00 Reserved */
#define DS2781_STATUS			0x01
#define DS2781_RAAC_MSB			0x02
#define DS2781_RAAC_LSB			0x03
#define DS2781_RSAC_MSB			0x04
#define DS2781_RSAC_LSB			0x05
#define DS2781_RARC			0x06
#define DS2781_RSRC			0x07
#define DS2781_IAVG_MSB			0x08
#define DS2781_IAVG_LSB			0x09
#define DS2781_TEMP_MSB			0x0A
#define DS2781_TEMP_LSB			0x0B
#define DS2781_VOLT_MSB			0x0C
#define DS2781_VOLT_LSB			0x0D
#define DS2781_CURRENT_MSB		0x0E
#define DS2781_CURRENT_LSB		0x0F
#define DS2781_ACR_MSB			0x10
#define DS2781_ACR_LSB			0x11
#define DS2781_ACRL_MSB			0x12
#define DS2781_ACRL_LSB			0x13
#define DS2781_AS			0x14
#define DS2781_SFR			0x15
#define DS2781_FULL_MSB			0x16
#define DS2781_FULL_LSB			0x17
#define DS2781_AE_MSB			0x18
#define DS2781_AE_LSB			0x19
#define DS2781_SE_MSB			0x1A
#define DS2781_SE_LSB			0x1B
/* Register 0x1C - 0x1E Reserved */
#define DS2781_EEPROM		0x1F
#define DS2781_EEPROM_BLOCK0_START	0x20
/* Register 0x20 - 0x2F User EEPROM */
#define DS2781_EEPROM_BLOCK0_END	0x2F
/* Register 0x30 - 0x5F Reserved */
#define DS2781_EEPROM_BLOCK1_START	0x60
#define DS2781_CONTROL			0x60
#define DS2781_AB			0x61
#define DS2781_AC_MSB			0x62
#define DS2781_AC_LSB			0x63
#define DS2781_VCHG			0x64
#define DS2781_IMIN			0x65
#define DS2781_VAE			0x66
#define DS2781_IAE			0x67
#define DS2781_AE_40			0x68
#define DS2781_RSNSP			0x69
#define DS2781_FULL_40_MSB		0x6A
#define DS2781_FULL_40_LSB		0x6B
#define DS2781_FULL_4_SLOPE		0x6C
#define DS2781_FULL_3_SLOPE		0x6D
#define DS2781_FULL_2_SLOPE		0x6E
#define DS2781_FULL_1_SLOPE		0x6F
#define DS2781_AE_4_SLOPE		0x70
#define DS2781_AE_3_SLOPE		0x71
#define DS2781_AE_2_SLOPE		0x72
#define DS2781_AE_1_SLOPE		0x73
#define DS2781_SE_4_SLOPE		0x74
#define DS2781_SE_3_SLOPE		0x75
#define DS2781_SE_2_SLOPE		0x76
#define DS2781_SE_1_SLOPE		0x77
#define DS2781_RSGAIN_MSB		0x78
#define DS2781_RSGAIN_LSB		0x79
#define DS2781_RSTC			0x7A
#define DS2781_COB			0x7B
#define DS2781_TBP34			0x7C
#define DS2781_TBP23			0x7D
#define DS2781_TBP12			0x7E
#define DS2781_EEPROM_BLOCK1_END	0x7F
/* Register 0x7D - 0xFF Reserved */

#define DS2781_FSGAIN_MSB		0xB0
#define DS2781_FSGAIN_LSB		0xB1

/* Number of valid register addresses */
#define DS2781_DATA_SIZE		0xB2

/* Status register bits */
#define DS2781_STATUS_CHGTF		(1 << 7)
#define DS2781_STATUS_AEF		(1 << 6)
#define DS2781_STATUS_SEF		(1 << 5)
#define DS2781_STATUS_LEARNF		(1 << 4)
/* Bit 3 Reserved */
#define DS2781_STATUS_UVF		(1 << 2)
#define DS2781_STATUS_PORF		(1 << 1)
/* Bit 0 Reserved */

/* Control register bits */
/* Bit 7 Reserved */
#define DS2781_CONTROL_NBEN		(1 << 7)
#define DS2781_CONTROL_UVEN		(1 << 6)
#define DS2781_CONTROL_PMOD		(1 << 5)
#define DS2781_CONTROL_RNAOP		(1 << 4)
#define DS1781_CONTROL_UVTH		(1 << 3)
/* Bit 0 - 2 Reserved */

/* Special feature register bits */
/* Bit 1 - 7 Reserved */
#define DS2781_SFR_PIOSC		(1 << 0)

/* EEPROM register bits */
#define DS2781_EEPROM_EEC		(1 << 7)
#define DS2781_EEPROM_LOCK		(1 << 6)
/* Bit 2 - 6 Reserved */
#define DS2781_EEPROM_BL1		(1 << 1)
#define DS2781_EEPROM_BL0		(1 << 0)

extern int w1_ds2781_io(struct device *dev, char *buf, int addr, size_t count,
			int io);
extern int w1_ds2781_eeprom_cmd(struct device *dev, int addr, int cmd);

#endif /* !_W1_DS2781_H */
