/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Xilinx XADC driver
 *
 * Copyright 2013 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#ifndef __IIO_XILINX_XADC__
#define __IIO_XILINX_XADC__

#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

struct iio_dev;
struct clk;
struct xadc_ops;
struct platform_device;

void xadc_handle_events(struct iio_dev *indio_dev, unsigned long events);

int xadc_read_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir);
int xadc_write_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, int state);
int xadc_read_event_value(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info,
	int *val, int *val2);
int xadc_write_event_value(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info,
	int val, int val2);

enum xadc_external_mux_mode {
	XADC_EXTERNAL_MUX_NONE,
	XADC_EXTERNAL_MUX_SINGLE,
	XADC_EXTERNAL_MUX_DUAL,
};

struct xadc {
	void __iomem *base;
	struct clk *clk;

	const struct xadc_ops *ops;

	uint16_t threshold[16];
	uint16_t temp_hysteresis;
	unsigned int alarm_mask;

	uint16_t *data;

	struct iio_trigger *trigger;
	struct iio_trigger *convst_trigger;
	struct iio_trigger *samplerate_trigger;

	enum xadc_external_mux_mode external_mux_mode;

	unsigned int zynq_masked_alarm;
	unsigned int zynq_intmask;
	struct delayed_work zynq_unmask_work;

	struct mutex mutex;
	spinlock_t lock;

	struct completion completion;
};

enum xadc_type {
	XADC_TYPE_S7, /* Series 7 */
	XADC_TYPE_US, /* UltraScale and UltraScale+ */
};

struct xadc_ops {
	int (*read)(struct xadc *xadc, unsigned int reg, uint16_t *val);
	int (*write)(struct xadc *xadc, unsigned int reg, uint16_t val);
	int (*setup)(struct platform_device *pdev, struct iio_dev *indio_dev,
			int irq);
	void (*update_alarm)(struct xadc *xadc, unsigned int alarm);
	unsigned long (*get_dclk_rate)(struct xadc *xadc);
	irqreturn_t (*interrupt_handler)(int irq, void *devid);

	unsigned int flags;
	enum xadc_type type;
};

static inline int _xadc_read_adc_reg(struct xadc *xadc, unsigned int reg,
	uint16_t *val)
{
	lockdep_assert_held(&xadc->mutex);
	return xadc->ops->read(xadc, reg, val);
}

static inline int _xadc_write_adc_reg(struct xadc *xadc, unsigned int reg,
	uint16_t val)
{
	lockdep_assert_held(&xadc->mutex);
	return xadc->ops->write(xadc, reg, val);
}

static inline int xadc_read_adc_reg(struct xadc *xadc, unsigned int reg,
	uint16_t *val)
{
	int ret;

	mutex_lock(&xadc->mutex);
	ret = _xadc_read_adc_reg(xadc, reg, val);
	mutex_unlock(&xadc->mutex);
	return ret;
}

static inline int xadc_write_adc_reg(struct xadc *xadc, unsigned int reg,
	uint16_t val)
{
	int ret;

	mutex_lock(&xadc->mutex);
	ret = _xadc_write_adc_reg(xadc, reg, val);
	mutex_unlock(&xadc->mutex);
	return ret;
}

/* XADC hardmacro register definitions */
#define XADC_REG_TEMP		0x00
#define XADC_REG_VCCINT		0x01
#define XADC_REG_VCCAUX		0x02
#define XADC_REG_VPVN		0x03
#define XADC_REG_VREFP		0x04
#define XADC_REG_VREFN		0x05
#define XADC_REG_VCCBRAM	0x06

#define XADC_REG_VCCPINT	0x0d
#define XADC_REG_VCCPAUX	0x0e
#define XADC_REG_VCCO_DDR	0x0f
#define XADC_REG_VAUX(x)	(0x10 + (x))

#define XADC_REG_MAX_TEMP	0x20
#define XADC_REG_MAX_VCCINT	0x21
#define XADC_REG_MAX_VCCAUX	0x22
#define XADC_REG_MAX_VCCBRAM	0x23
#define XADC_REG_MIN_TEMP	0x24
#define XADC_REG_MIN_VCCINT	0x25
#define XADC_REG_MIN_VCCAUX	0x26
#define XADC_REG_MIN_VCCBRAM	0x27
#define XADC_REG_MAX_VCCPINT	0x28
#define XADC_REG_MAX_VCCPAUX	0x29
#define XADC_REG_MAX_VCCO_DDR	0x2a
#define XADC_REG_MIN_VCCPINT	0x2c
#define XADC_REG_MIN_VCCPAUX	0x2d
#define XADC_REG_MIN_VCCO_DDR	0x2e

#define XADC_REG_CONF0		0x40
#define XADC_REG_CONF1		0x41
#define XADC_REG_CONF2		0x42
#define XADC_REG_SEQ(x)		(0x48 + (x))
#define XADC_REG_INPUT_MODE(x)	(0x4c + (x))
#define XADC_REG_THRESHOLD(x)	(0x50 + (x))

#define XADC_REG_FLAG		0x3f

#define XADC_CONF0_EC			BIT(9)
#define XADC_CONF0_ACQ			BIT(8)
#define XADC_CONF0_MUX			BIT(11)
#define XADC_CONF0_CHAN(x)		(x)

#define XADC_CONF1_SEQ_MASK		(0xf << 12)
#define XADC_CONF1_SEQ_DEFAULT		(0 << 12)
#define XADC_CONF1_SEQ_SINGLE_PASS	(1 << 12)
#define XADC_CONF1_SEQ_CONTINUOUS	(2 << 12)
#define XADC_CONF1_SEQ_SINGLE_CHANNEL	(3 << 12)
#define XADC_CONF1_SEQ_SIMULTANEOUS	(4 << 12)
#define XADC_CONF1_SEQ_INDEPENDENT	(8 << 12)
#define XADC_CONF1_ALARM_MASK		0x0f0f

#define XADC_CONF2_DIV_MASK	0xff00
#define XADC_CONF2_DIV_OFFSET	8

#define XADC_CONF2_PD_MASK	(0x3 << 4)
#define XADC_CONF2_PD_NONE	(0x0 << 4)
#define XADC_CONF2_PD_ADC_B	(0x2 << 4)
#define XADC_CONF2_PD_BOTH	(0x3 << 4)

#define XADC_ALARM_TEMP_MASK		BIT(0)
#define XADC_ALARM_VCCINT_MASK		BIT(1)
#define XADC_ALARM_VCCAUX_MASK		BIT(2)
#define XADC_ALARM_OT_MASK		BIT(3)
#define XADC_ALARM_VCCBRAM_MASK		BIT(4)
#define XADC_ALARM_VCCPINT_MASK		BIT(5)
#define XADC_ALARM_VCCPAUX_MASK		BIT(6)
#define XADC_ALARM_VCCODDR_MASK		BIT(7)

#define XADC_THRESHOLD_TEMP_MAX		0x0
#define XADC_THRESHOLD_VCCINT_MAX	0x1
#define XADC_THRESHOLD_VCCAUX_MAX	0x2
#define XADC_THRESHOLD_OT_MAX		0x3
#define XADC_THRESHOLD_TEMP_MIN		0x4
#define XADC_THRESHOLD_VCCINT_MIN	0x5
#define XADC_THRESHOLD_VCCAUX_MIN	0x6
#define XADC_THRESHOLD_OT_MIN		0x7
#define XADC_THRESHOLD_VCCBRAM_MAX	0x8
#define XADC_THRESHOLD_VCCPINT_MAX	0x9
#define XADC_THRESHOLD_VCCPAUX_MAX	0xa
#define XADC_THRESHOLD_VCCODDR_MAX	0xb
#define XADC_THRESHOLD_VCCBRAM_MIN	0xc
#define XADC_THRESHOLD_VCCPINT_MIN	0xd
#define XADC_THRESHOLD_VCCPAUX_MIN	0xe
#define XADC_THRESHOLD_VCCODDR_MIN	0xf

#endif
