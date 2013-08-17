/*
 * Copyright (C) 2010-2011 Michael Hennerich, Analog Devices Inc.
 * Copyright (C) 2008-2010 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * ad799x.h
 */

#ifndef _AD799X_H_
#define  _AD799X_H_

#define AD799X_CHANNEL_SHIFT			4
#define AD799X_STORAGEBITS			16
/*
 * AD7991, AD7995 and AD7999 defines
 */

#define AD7991_REF_SEL				0x08
#define AD7991_FLTR				0x04
#define AD7991_BIT_TRIAL_DELAY			0x02
#define AD7991_SAMPLE_DELAY			0x01

/*
 * AD7992, AD7993, AD7994, AD7997 and AD7998 defines
 */

#define AD7998_FLTR				0x08
#define AD7998_ALERT_EN				0x04
#define AD7998_BUSY_ALERT			0x02
#define AD7998_BUSY_ALERT_POL			0x01

#define AD7998_CONV_RES_REG			0x0
#define AD7998_ALERT_STAT_REG			0x1
#define AD7998_CONF_REG				0x2
#define AD7998_CYCLE_TMR_REG			0x3
#define AD7998_DATALOW_CH1_REG			0x4
#define AD7998_DATAHIGH_CH1_REG			0x5
#define AD7998_HYST_CH1_REG			0x6
#define AD7998_DATALOW_CH2_REG			0x7
#define AD7998_DATAHIGH_CH2_REG			0x8
#define AD7998_HYST_CH2_REG			0x9
#define AD7998_DATALOW_CH3_REG			0xA
#define AD7998_DATAHIGH_CH3_REG			0xB
#define AD7998_HYST_CH3_REG			0xC
#define AD7998_DATALOW_CH4_REG			0xD
#define AD7998_DATAHIGH_CH4_REG			0xE
#define AD7998_HYST_CH4_REG			0xF

#define AD7998_CYC_MASK				0x7
#define AD7998_CYC_DIS				0x0
#define AD7998_CYC_TCONF_32			0x1
#define AD7998_CYC_TCONF_64			0x2
#define AD7998_CYC_TCONF_128			0x3
#define AD7998_CYC_TCONF_256			0x4
#define AD7998_CYC_TCONF_512			0x5
#define AD7998_CYC_TCONF_1024			0x6
#define AD7998_CYC_TCONF_2048			0x7

#define AD7998_ALERT_STAT_CLEAR			0xFF

/*
 * AD7997 and AD7997 defines
 */

#define AD7997_8_READ_SINGLE			0x80
#define AD7997_8_READ_SEQUENCE			0x70
/* TODO: move this into a common header */
#define RES_MASK(bits)	((1 << (bits)) - 1)

enum {
	ad7991,
	ad7995,
	ad7999,
	ad7992,
	ad7993,
	ad7994,
	ad7997,
	ad7998
};

struct ad799x_state;

/**
 * struct ad799x_chip_info - chip specifc information
 * @channel:		channel specification
 * @num_channels:	number of channels
 * @int_vref_mv:	the internal reference voltage
 * @monitor_mode:	whether the chip supports monitor interrupts
 * @default_config:	device default configuration
 * @event_attrs:	pointer to the monitor event attribute group
 */

struct ad799x_chip_info {
	struct iio_chan_spec		channel[9];
	int				num_channels;
	u16				int_vref_mv;
	u16				default_config;
	const struct iio_info		*info;
};

struct ad799x_state {
	struct i2c_client		*client;
	const struct ad799x_chip_info	*chip_info;
	size_t				d_size;
	struct iio_trigger		*trig;
	struct regulator		*reg;
	u16				int_vref_mv;
	unsigned			id;
	char				*name;
	u16				config;
};

/*
 * TODO: struct ad799x_platform_data needs to go into include/linux/iio
 */

struct ad799x_platform_data {
	u16				vref_mv;
};

int ad7997_8_set_scan_mode(struct ad799x_state *st, unsigned mask);

#ifdef CONFIG_AD799X_RING_BUFFER
int ad799x_register_ring_funcs_and_init(struct iio_dev *indio_dev);
void ad799x_ring_cleanup(struct iio_dev *indio_dev);
#else /* CONFIG_AD799X_RING_BUFFER */

static inline int
ad799x_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void ad799x_ring_cleanup(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_AD799X_RING_BUFFER */
#endif /* _AD799X_H_ */
