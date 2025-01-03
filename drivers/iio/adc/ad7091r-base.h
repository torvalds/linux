/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AD7091RX Analog to Digital converter driver
 *
 * Copyright 2014-2019 Analog Devices Inc.
 */

#ifndef __DRIVERS_IIO_ADC_AD7091R_BASE_H__
#define __DRIVERS_IIO_ADC_AD7091R_BASE_H__

#include <linux/regmap.h>

#define AD7091R_REG_RESULT  0
#define AD7091R_REG_CHANNEL 1
#define AD7091R_REG_CONF    2
#define AD7091R_REG_ALERT   3
#define AD7091R_REG_CH_LOW_LIMIT(ch) ((ch) * 3 + 4)
#define AD7091R_REG_CH_HIGH_LIMIT(ch) ((ch) * 3 + 5)
#define AD7091R_REG_CH_HYSTERESIS(ch) ((ch) * 3 + 6)

/* AD7091R_REG_RESULT */
#define AD7091R5_REG_RESULT_CH_ID(x)	    (((x) >> 13) & 0x3)
#define AD7091R8_REG_RESULT_CH_ID(x)	    (((x) >> 13) & 0x7)
#define AD7091R_REG_RESULT_CONV_RESULT(x)   ((x) & 0xfff)

/* AD7091R_REG_CONF */
#define AD7091R_REG_CONF_INT_VREF	BIT(0)
#define AD7091R_REG_CONF_ALERT_EN	BIT(4)
#define AD7091R_REG_CONF_AUTO		BIT(8)
#define AD7091R_REG_CONF_CMD		BIT(10)

#define AD7091R_REG_CONF_MODE_MASK  \
	(AD7091R_REG_CONF_AUTO | AD7091R_REG_CONF_CMD)

/* AD7091R_REG_CH_LIMIT */
#define AD7091R_HIGH_LIMIT		0xFFF
#define AD7091R_LOW_LIMIT		0x0

#define AD7091R_CHANNEL(idx, bits, ev, num_ev) {			\
	.type = IIO_VOLTAGE,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.indexed = 1,							\
	.channel = idx,							\
	.event_spec = ev,						\
	.num_event_specs = num_ev,					\
	.scan_type.storagebits = 16,					\
	.scan_type.realbits = bits,					\
}

struct device;
struct gpio_desc;

enum ad7091r_mode {
	AD7091R_MODE_SAMPLE,
	AD7091R_MODE_COMMAND,
	AD7091R_MODE_AUTOCYCLE,
};

struct ad7091r_state {
	struct device *dev;
	struct regmap *map;
	struct gpio_desc *convst_gpio;
	struct gpio_desc *reset_gpio;
	struct regulator *vref;
	const struct ad7091r_chip_info *chip_info;
	enum ad7091r_mode mode;
	struct mutex lock; /*lock to prevent concurrent reads */
	__be16 tx_buf __aligned(IIO_DMA_MINALIGN);
	__be16 rx_buf;
};

struct ad7091r_chip_info {
	const char *name;
	unsigned int num_channels;
	const struct iio_chan_spec *channels;
	unsigned int vref_mV;
	unsigned int (*reg_result_chan_id)(unsigned int val);
	int (*set_mode)(struct ad7091r_state *st, enum ad7091r_mode mode);
};

struct ad7091r_init_info {
	const struct ad7091r_chip_info *info_irq;
	const struct ad7091r_chip_info *info_no_irq;
	const struct regmap_config *regmap_config;
	void (*init_adc_regmap)(struct ad7091r_state *st,
				const struct regmap_config *regmap_conf);
	int (*setup)(struct ad7091r_state *st);
};

extern const struct iio_event_spec ad7091r_events[3];

int ad7091r_probe(struct device *dev, const struct ad7091r_init_info *init_info,
		  int irq);

bool ad7091r_volatile_reg(struct device *dev, unsigned int reg);
bool ad7091r_writeable_reg(struct device *dev, unsigned int reg);

#endif /* __DRIVERS_IIO_ADC_AD7091R_BASE_H__ */
