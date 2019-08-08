/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AFE440X Heart Rate Monitors and Low-Cost Pulse Oximeters
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#ifndef _AFE440X_H
#define _AFE440X_H

/* AFE440X registers */
#define AFE440X_CONTROL0		0x00
#define AFE440X_LED2STC			0x01
#define AFE440X_LED2ENDC		0x02
#define AFE440X_LED1LEDSTC		0x03
#define AFE440X_LED1LEDENDC		0x04
#define AFE440X_ALED2STC		0x05
#define AFE440X_ALED2ENDC		0x06
#define AFE440X_LED1STC			0x07
#define AFE440X_LED1ENDC		0x08
#define AFE440X_LED2LEDSTC		0x09
#define AFE440X_LED2LEDENDC		0x0a
#define AFE440X_ALED1STC		0x0b
#define AFE440X_ALED1ENDC		0x0c
#define AFE440X_LED2CONVST		0x0d
#define AFE440X_LED2CONVEND		0x0e
#define AFE440X_ALED2CONVST		0x0f
#define AFE440X_ALED2CONVEND		0x10
#define AFE440X_LED1CONVST		0x11
#define AFE440X_LED1CONVEND		0x12
#define AFE440X_ALED1CONVST		0x13
#define AFE440X_ALED1CONVEND		0x14
#define AFE440X_ADCRSTSTCT0		0x15
#define AFE440X_ADCRSTENDCT0		0x16
#define AFE440X_ADCRSTSTCT1		0x17
#define AFE440X_ADCRSTENDCT1		0x18
#define AFE440X_ADCRSTSTCT2		0x19
#define AFE440X_ADCRSTENDCT2		0x1a
#define AFE440X_ADCRSTSTCT3		0x1b
#define AFE440X_ADCRSTENDCT3		0x1c
#define AFE440X_PRPCOUNT		0x1d
#define AFE440X_CONTROL1		0x1e
#define AFE440X_LEDCNTRL		0x22
#define AFE440X_CONTROL2		0x23
#define AFE440X_ALARM			0x29
#define AFE440X_LED2VAL			0x2a
#define AFE440X_ALED2VAL		0x2b
#define AFE440X_LED1VAL			0x2c
#define AFE440X_ALED1VAL		0x2d
#define AFE440X_LED2_ALED2VAL		0x2e
#define AFE440X_LED1_ALED1VAL		0x2f
#define AFE440X_CONTROL3		0x31
#define AFE440X_PDNCYCLESTC		0x32
#define AFE440X_PDNCYCLEENDC		0x33

/* CONTROL0 register fields */
#define AFE440X_CONTROL0_REG_READ	BIT(0)
#define AFE440X_CONTROL0_TM_COUNT_RST	BIT(1)
#define AFE440X_CONTROL0_SW_RESET	BIT(3)

/* CONTROL1 register fields */
#define AFE440X_CONTROL1_TIMEREN	BIT(8)

/* TIAGAIN register fields */
#define AFE440X_TIAGAIN_ENSEPGAIN	BIT(15)

/* CONTROL2 register fields */
#define AFE440X_CONTROL2_PDN_AFE	BIT(0)
#define AFE440X_CONTROL2_PDN_RX		BIT(1)
#define AFE440X_CONTROL2_DYNAMIC4	BIT(3)
#define AFE440X_CONTROL2_DYNAMIC3	BIT(4)
#define AFE440X_CONTROL2_DYNAMIC2	BIT(14)
#define AFE440X_CONTROL2_DYNAMIC1	BIT(20)

/* CONTROL3 register fields */
#define AFE440X_CONTROL3_CLKDIV		GENMASK(2, 0)

/* CONTROL0 values */
#define AFE440X_CONTROL0_WRITE		0x0
#define AFE440X_CONTROL0_READ		0x1

#define AFE440X_INTENSITY_CHAN(_index, _mask)			\
	{							\
		.type = IIO_INTENSITY,				\
		.channel = _index,				\
		.address = _index,				\
		.scan_index = _index,				\
		.scan_type = {					\
				.sign = 's',			\
				.realbits = 24,			\
				.storagebits = 32,		\
				.endianness = IIO_CPU,		\
		},						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			_mask,					\
		.indexed = true,				\
	}

#define AFE440X_CURRENT_CHAN(_index)				\
	{							\
		.type = IIO_CURRENT,				\
		.channel = _index,				\
		.address = _index,				\
		.scan_index = -1,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			BIT(IIO_CHAN_INFO_SCALE),		\
		.indexed = true,				\
		.output = true,					\
	}

struct afe440x_val_table {
	int integer;
	int fract;
};

#define AFE440X_TABLE_ATTR(_name, _table)				\
static ssize_t _name ## _show(struct device *dev,			\
			      struct device_attribute *attr, char *buf)	\
{									\
	ssize_t len = 0;						\
	int i;								\
									\
	for (i = 0; i < ARRAY_SIZE(_table); i++)			\
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06u ", \
				 _table[i].integer,			\
				 _table[i].fract);			\
									\
	buf[len - 1] = '\n';						\
									\
	return len;							\
}									\
static DEVICE_ATTR_RO(_name)

struct afe440x_attr {
	struct device_attribute dev_attr;
	unsigned int field;
	const struct afe440x_val_table *val_table;
	unsigned int table_size;
};

#define to_afe440x_attr(_dev_attr)				\
	container_of(_dev_attr, struct afe440x_attr, dev_attr)

#define AFE440X_ATTR(_name, _field, _table)			\
	struct afe440x_attr afe440x_attr_##_name = {		\
		.dev_attr = __ATTR(_name, (S_IRUGO | S_IWUSR),	\
				   afe440x_show_register,	\
				   afe440x_store_register),	\
		.field = _field,				\
		.val_table = _table,				\
		.table_size = ARRAY_SIZE(_table),		\
	}

#endif /* _AFE440X_H */
