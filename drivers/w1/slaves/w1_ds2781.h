/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based on w1_ds2784.h which is:
 * Copyright (C) 2009 HTC Corporation
 */

#ifndef __W1_DS2781_H__
#define __W1_DS2781_H__

#ifdef __KERNEL__

/* Known commands to the DS2781 chip */
#define W1_DS2781_READ_DATA		0x69
#define W1_DS2781_WRITE_DATA		0x6C
#define W1_DS2781_COPY_DATA		0x48
#define W1_DS2781_RECALL_DATA		0xB8
#define W1_DS2781_LOCK			0x6A

/* Number of valid register addresses */
#define DS2781_DATA_SIZE		0x80

/* DS2781 1-wire slave memory map definitions */
/* Reserved: 0x00 */
#define DS2781_REG_STATUS		0x01
#define DS2781_REG_RAAC_MSB		0x02
#define DS2781_REG_RAAC_LSB		0x03
#define DS2781_REG_RSAC_MSB		0x04
#define DS2781_REG_RSAC_LSB		0x05
#define DS2781_REG_RARC			0x06
#define DS2781_REG_RSRC			0x07
#define DS2781_REG_AVG_CURR_MSB		0x08
#define DS2781_REG_AVG_CURR_LSB		0x09
#define DS2781_REG_TEMP_MSB		0x0A
#define DS2781_REG_TEMP_LSB		0x0B
#define DS2781_REG_VOLT_MSB		0x0C
#define DS2781_REG_VOLT_LSB		0x0D
#define DS2781_REG_CURR_MSB		0x0E
#define DS2781_REG_CURR_LSB		0x0F
#define DS2781_REG_ACCUMULATE_CURR_MSB	0x10
#define DS2781_REG_ACCUMULATE_CURR_LSB	0x11
#define DS2781_REG_ACCUMULATE_CURR_LSB1	0x12
#define DS2781_REG_ACCUMULATE_CURR_LSB2	0x13
#define DS2781_REG_AGE_SCALAR		0x14
#define DS2781_REG_SPECIAL_FEATURE	0x15
#define DS2781_REG_FULL_MSB		0x16
#define DS2781_REG_FULL_LSB		0x17
#define DS2781_REG_ACTIVE_EMPTY_MSB	0x18
#define DS2781_REG_ACTIVE_EMPTY_LSB	0x19
#define DS2781_REG_STBY_EMPTY_MSB	0x1A
#define DS2781_REG_STBY_EMPTY_LSB	0x1B
/* Reserved: 0x1C - 0x1E */
#define DS2781_REG_EEPROM		0x1F
#define DS2781_REG_USER_EEPROM		0x20
/* Reserved: 0x30 - 0x5F */
#define DS2781_REG_CTRL			0x60
#define DS2781_REG_ACCUMULATION_BIAS	0x61
#define DS2781_REG_AGE_CAPACITY_MSB	0x62
#define DS2781_REG_AGE_CAPACITY_LSB	0x63
#define DS2781_REG_CHARGE_VOLT		0x64
#define DS2781_REG_MIN_CHARGE_CURR	0x65
#define DS2781_REG_ACTIVE_EMPTY_VOLT	0x66
#define DS2781_REG_ACTIVE_EMPTY_CURR	0x67
#define DS2781_REG_ACTIVE_EMPTY_40	0x68
#define DS2781_REG_RSNSP		0x69
#define DS2781_REG_FULL_40_MSB		0x6A
#define DS2781_REG_FULL_40_LSB		0x6B
#define DS2781_REG_FULL_SEG_4_SLOPE	0x6C
#define DS2781_REG_FULL_SEG_3_SLOPE	0x6D
#define DS2781_REG_FULL_SEG_2_SLOPE	0x6E
#define DS2781_REG_FULL_SEG_1_SLOPE	0x6F
#define DS2781_REG_AE_SEG_4_SLOPE	0x70
#define DS2781_REG_AE_SEG_3_SLOPE	0x71
#define DS2781_REG_AE_SEG_2_SLOPE	0x72
#define DS2781_REG_AE_SEG_1_SLOPE	0x73
#define DS2781_REG_SE_SEG_4_SLOPE	0x74
#define DS2781_REG_SE_SEG_3_SLOPE	0x75
#define DS2781_REG_SE_SEG_2_SLOPE	0x76
#define DS2781_REG_SE_SEG_1_SLOPE	0x77
#define DS2781_REG_RSGAIN_MSB		0x78
#define DS2781_REG_RSGAIN_LSB		0x79
#define DS2781_REG_RSTC			0x7A
#define DS2781_REG_CURR_OFFSET_BIAS	0x7B
#define DS2781_REG_TBP34		0x7C
#define DS2781_REG_TBP23		0x7D
#define DS2781_REG_TBP12		0x7E
/* Reserved: 0x7F */

extern int w1_ds2781_read(struct device *dev, char *buf, int addr,
			  size_t count);
extern int w1_ds2781_write(struct device *dev, char *buf, int addr,
			   size_t count);

#endif /* __KERNEL__ */

#endif /* __W1_DS2781_H__ */
