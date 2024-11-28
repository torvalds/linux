/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022 ROHM Semiconductors
 *
 * ROHM/KIONIX KX022A accelerometer driver
 */

#ifndef _KX022A_H_
#define _KX022A_H_

#include <linux/bits.h>
#include <linux/regmap.h>

#define KX022A_REG_WHO		0x0f
#define KX022A_ID		0xc8
#define KX132ACR_LBZ_ID		0xd8
#define KX134ACR_LBZ_ID		0xcc

#define KX022A_REG_CNTL2	0x19
#define KX022A_MASK_SRST	BIT(7)
#define KX022A_REG_CNTL		0x18
#define KX022A_MASK_PC1		BIT(7)
#define KX022A_MASK_RES		BIT(6)
#define KX022A_MASK_DRDY	BIT(5)
#define KX022A_MASK_GSEL	GENMASK(4, 3)
#define KX022A_GSEL_SHIFT	3
#define KX022A_GSEL_2		0x0
#define KX022A_GSEL_4		BIT(3)
#define KX022A_GSEL_8		BIT(4)
#define KX022A_GSEL_16		GENMASK(4, 3)

#define KX022A_REG_INS2		0x13
#define KX022A_MASK_INS2_DRDY	BIT(4)
#define KX122_MASK_INS2_WMI	BIT(5)

#define KX022A_REG_XHP_L	0x0
#define KX022A_REG_XOUT_L	0x06
#define KX022A_REG_YOUT_L	0x08
#define KX022A_REG_ZOUT_L	0x0a
#define KX022A_REG_COTR		0x0c
#define KX022A_REG_TSCP		0x10
#define KX022A_REG_INT_REL	0x17

#define KX022A_REG_ODCNTL	0x1b

#define KX022A_REG_BTS_WUF_TH	0x31
#define KX022A_REG_MAN_WAKE	0x2c

#define KX022A_REG_BUF_CNTL1	0x3a
#define KX022A_MASK_WM_TH	GENMASK(6, 0)
#define KX022A_REG_BUF_CNTL2	0x3b
#define KX022A_MASK_BUF_EN	BIT(7)
#define KX022A_MASK_BRES16	BIT(6)
#define KX022A_REG_BUF_STATUS_1	0x3c
#define KX022A_REG_BUF_STATUS_2	0x3d
#define KX022A_REG_BUF_CLEAR	0x3e
#define KX022A_REG_BUF_READ	0x3f
#define KX022A_MASK_ODR		GENMASK(3, 0)
#define KX022A_ODR_SHIFT	3
#define KX022A_FIFO_MAX_WMI_TH	41

#define KX022A_REG_INC1		0x1c
#define KX022A_REG_INC5		0x20
#define KX022A_REG_INC6		0x21
#define KX022A_MASK_IEN		BIT(5)
#define KX022A_MASK_IPOL	BIT(4)
#define KX022A_IPOL_LOW		0
#define KX022A_IPOL_HIGH	KX022A_MASK_IPOL1
#define KX022A_MASK_ITYP	BIT(3)
#define KX022A_ITYP_PULSE	KX022A_MASK_ITYP
#define KX022A_ITYP_LEVEL	0

#define KX022A_REG_INC4		0x1f
#define KX022A_MASK_WMI		BIT(5)

#define KX022A_REG_SELF_TEST	0x60
#define KX022A_MAX_REGISTER	0x60

#define KX132_REG_WHO		0x13
#define KX132_ID		0x3d
#define KX134_1211_ID		0x46

#define KX132_FIFO_LENGTH	86

#define KX132_REG_CNTL		0x1b
#define KX132_REG_CNTL2		0x1c
#define KX132_REG_CNTL5		0x1f
#define KX132_MASK_RES		BIT(6)
#define KX132_GSEL_2		0x0
#define KX132_GSEL_4		BIT(3)
#define KX132_GSEL_8		BIT(4)
#define KX132_GSEL_16		GENMASK(4, 3)

#define KX132_REG_INS2		0x17
#define KX132_MASK_INS2_WMI	BIT(5)

#define KX132_REG_XADP_L	0x02
#define KX132_REG_XOUT_L	0x08
#define KX132_REG_YOUT_L	0x0a
#define KX132_REG_ZOUT_L	0x0c
#define KX132_REG_COTR		0x12
#define KX132_REG_TSCP		0x14
#define KX132_REG_INT_REL	0x1a

#define KX132_REG_ODCNTL	0x21

#define KX132_REG_BTS_WUF_TH	0x4a

#define KX132_REG_BUF_CNTL1	0x5e
#define KX132_REG_BUF_CNTL2	0x5f
#define KX132_REG_BUF_STATUS_1	0x60
#define KX132_REG_BUF_STATUS_2	0x61
#define KX132_MASK_BUF_SMP_LVL	GENMASK(9, 0)
#define KX132_REG_BUF_CLEAR	0x62
#define KX132_REG_BUF_READ	0x63
#define KX132_ODR_SHIFT		3
#define KX132_FIFO_MAX_WMI_TH	86

#define KX132_REG_INC1		0x22
#define KX132_REG_INC5		0x26
#define KX132_REG_INC6		0x27
#define KX132_IPOL_LOW		0
#define KX132_IPOL_HIGH		KX022A_MASK_IPOL
#define KX132_ITYP_PULSE	KX022A_MASK_ITYP

#define KX132_REG_INC4		0x25

#define KX132_REG_SELF_TEST	0x5d
#define KX132_MAX_REGISTER	0x76

struct device;

struct kx022a_data;

/**
 * struct kx022a_chip_info - Kionix accelerometer chip specific information
 *
 * @name:			name of the device
 * @regmap_config:		pointer to register map configuration
 * @channels:			pointer to iio_chan_spec array
 * @num_channels:		number of iio_chan_spec channels
 * @fifo_length:		number of 16-bit samples in a full buffer
 * @buf_smp_lvl_mask:		buffer sample level mask
 * @who:			WHO_AM_I register
 * @id:				WHO_AM_I register value
 * @cntl:			control register 1
 * @cntl2:			control register 2
 * @odcntl:			output data control register
 * @buf_cntl1:			buffer control register 1
 * @buf_cntl2:			buffer control register 2
 * @buf_clear:			buffer clear register
 * @buf_status1:		buffer status register 1
 * @buf_read:			buffer read register
 * @inc1:			interrupt control register 1
 * @inc4:			interrupt control register 4
 * @inc5:			interrupt control register 5
 * @inc6:			interrupt control register 6
 * @xout_l:			x-axis output least significant byte
 * @get_fifo_bytes_available:	function pointer to get amount of acceleration
 *				data bytes currently stored in the sensor's FIFO
 *				buffer
 */
struct kx022a_chip_info {
	const char *name;
	const struct regmap_config *regmap_config;
	const int (*scale_table)[2];
	const int scale_table_size;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	unsigned int fifo_length;
	u16 buf_smp_lvl_mask;
	u8 who;
	u8 id;
	u8 cntl;
	u8 cntl2;
	u8 odcntl;
	u8 buf_cntl1;
	u8 buf_cntl2;
	u8 buf_clear;
	u8 buf_status1;
	u8 buf_read;
	u8 inc1;
	u8 inc4;
	u8 inc5;
	u8 inc6;
	u8 xout_l;
	int (*get_fifo_bytes_available)(struct kx022a_data *);
};

int kx022a_probe_internal(struct device *dev, const struct kx022a_chip_info *chip_info);

extern const struct kx022a_chip_info kx022a_chip_info;
extern const struct kx022a_chip_info kx132_chip_info;
extern const struct kx022a_chip_info kx134_chip_info;
extern const struct kx022a_chip_info kx132acr_chip_info;
extern const struct kx022a_chip_info kx134acr_chip_info;

#endif
