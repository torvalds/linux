/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AD5592R / AD5593R Digital <-> Analog converters driver
 *
 * Copyright 2015-2016 Analog Devices Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __DRIVERS_IIO_DAC_AD5592R_BASE_H__
#define __DRIVERS_IIO_DAC_AD5592R_BASE_H__

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/mutex.h>
#include <linux/gpio/driver.h>

struct device;
struct ad5592r_state;

enum ad5592r_registers {
	AD5592R_REG_NOOP		= 0x0,
	AD5592R_REG_DAC_READBACK	= 0x1,
	AD5592R_REG_ADC_SEQ		= 0x2,
	AD5592R_REG_CTRL		= 0x3,
	AD5592R_REG_ADC_EN		= 0x4,
	AD5592R_REG_DAC_EN		= 0x5,
	AD5592R_REG_PULLDOWN		= 0x6,
	AD5592R_REG_LDAC		= 0x7,
	AD5592R_REG_GPIO_OUT_EN		= 0x8,
	AD5592R_REG_GPIO_SET		= 0x9,
	AD5592R_REG_GPIO_IN_EN		= 0xA,
	AD5592R_REG_PD			= 0xB,
	AD5592R_REG_OPEN_DRAIN		= 0xC,
	AD5592R_REG_TRISTATE		= 0xD,
	AD5592R_REG_RESET		= 0xF,
};

#define AD5592R_REG_PD_EN_REF		BIT(9)
#define AD5592R_REG_CTRL_ADC_RANGE	BIT(5)
#define AD5592R_REG_CTRL_DAC_RANGE	BIT(4)

struct ad5592r_rw_ops {
	int (*write_dac)(struct ad5592r_state *st, unsigned chan, u16 value);
	int (*read_adc)(struct ad5592r_state *st, unsigned chan, u16 *value);
	int (*reg_write)(struct ad5592r_state *st, u8 reg, u16 value);
	int (*reg_read)(struct ad5592r_state *st, u8 reg, u16 *value);
	int (*gpio_read)(struct ad5592r_state *st, u8 *value);
};

struct ad5592r_state {
	struct device *dev;
	struct regulator *reg;
	struct gpio_chip gpiochip;
	struct mutex gpio_lock;	/* Protect cached gpio_out, gpio_val, etc. */
	struct mutex lock;
	unsigned int num_channels;
	const struct ad5592r_rw_ops *ops;
	int scale_avail[2][2];
	u16 cached_dac[8];
	u16 cached_gp_ctrl;
	u8 channel_modes[8];
	u8 channel_offstate[8];
	u8 gpio_map;
	u8 gpio_out;
	u8 gpio_in;
	u8 gpio_val;

	__be16 spi_msg ____cacheline_aligned;
	__be16 spi_msg_nop;
};

int ad5592r_probe(struct device *dev, const char *name,
		const struct ad5592r_rw_ops *ops);
int ad5592r_remove(struct device *dev);

#endif /* __DRIVERS_IIO_DAC_AD5592R_BASE_H__ */
