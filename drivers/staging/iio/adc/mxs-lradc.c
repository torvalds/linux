/*
 * Freescale MXS LRADC driver
 *
 * Copyright (c) 2012 DENX Software Engineering, GmbH.
 * Marek Vasut <marex@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stmp_device.h>
#include <linux/sysfs.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/sysfs.h>

#define DRIVER_NAME		"mxs-lradc"

#define LRADC_MAX_DELAY_CHANS	4
#define LRADC_MAX_MAPPED_CHANS	8
#define LRADC_MAX_TOTAL_CHANS	16

#define LRADC_DELAY_TIMER_HZ	2000

/*
 * Make this runtime configurable if necessary. Currently, if the buffered mode
 * is enabled, the LRADC takes LRADC_DELAY_TIMER_LOOP samples of data before
 * triggering IRQ. The sampling happens every (LRADC_DELAY_TIMER_PER / 2000)
 * seconds. The result is that the samples arrive every 500mS.
 */
#define LRADC_DELAY_TIMER_PER	200
#define LRADC_DELAY_TIMER_LOOP	5

/*
 * Once the pen touches the touchscreen, the touchscreen switches from
 * IRQ-driven mode to polling mode to prevent interrupt storm. The polling
 * is realized by worker thread, which is called every 20 or so milliseconds.
 * This gives the touchscreen enough fluency and does not strain the system
 * too much.
 */
#define LRADC_TS_SAMPLE_DELAY_MS	5

/*
 * The LRADC reads the following amount of samples from each touchscreen
 * channel and the driver then computes average of these.
 */
#define LRADC_TS_SAMPLE_AMOUNT		4

enum mxs_lradc_id {
	IMX23_LRADC,
	IMX28_LRADC,
};

static const char * const mx23_lradc_irq_names[] = {
	"mxs-lradc-touchscreen",
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
	"mxs-lradc-channel6",
	"mxs-lradc-channel7",
};

static const char * const mx28_lradc_irq_names[] = {
	"mxs-lradc-touchscreen",
	"mxs-lradc-thresh0",
	"mxs-lradc-thresh1",
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
	"mxs-lradc-channel6",
	"mxs-lradc-channel7",
	"mxs-lradc-button0",
	"mxs-lradc-button1",
};

struct mxs_lradc_of_config {
	const int		irq_count;
	const char * const	*irq_name;
	const u32		*vref_mv;
};

#define VREF_MV_BASE 1850

static const u32 mx23_vref_mv[LRADC_MAX_TOTAL_CHANS] = {
	VREF_MV_BASE,		/* CH0 */
	VREF_MV_BASE,		/* CH1 */
	VREF_MV_BASE,		/* CH2 */
	VREF_MV_BASE,		/* CH3 */
	VREF_MV_BASE,		/* CH4 */
	VREF_MV_BASE,		/* CH5 */
	VREF_MV_BASE * 2,	/* CH6 VDDIO */
	VREF_MV_BASE * 4,	/* CH7 VBATT */
	VREF_MV_BASE,		/* CH8 Temp sense 0 */
	VREF_MV_BASE,		/* CH9 Temp sense 1 */
	VREF_MV_BASE,		/* CH10 */
	VREF_MV_BASE,		/* CH11 */
	VREF_MV_BASE,		/* CH12 USB_DP */
	VREF_MV_BASE,		/* CH13 USB_DN */
	VREF_MV_BASE,		/* CH14 VBG */
	VREF_MV_BASE * 4,	/* CH15 VDD5V */
};

static const u32 mx28_vref_mv[LRADC_MAX_TOTAL_CHANS] = {
	VREF_MV_BASE,		/* CH0 */
	VREF_MV_BASE,		/* CH1 */
	VREF_MV_BASE,		/* CH2 */
	VREF_MV_BASE,		/* CH3 */
	VREF_MV_BASE,		/* CH4 */
	VREF_MV_BASE,		/* CH5 */
	VREF_MV_BASE,		/* CH6 */
	VREF_MV_BASE * 4,	/* CH7 VBATT */
	VREF_MV_BASE,		/* CH8 Temp sense 0 */
	VREF_MV_BASE,		/* CH9 Temp sense 1 */
	VREF_MV_BASE * 2,	/* CH10 VDDIO */
	VREF_MV_BASE,		/* CH11 VTH */
	VREF_MV_BASE * 2,	/* CH12 VDDA */
	VREF_MV_BASE,		/* CH13 VDDD */
	VREF_MV_BASE,		/* CH14 VBG */
	VREF_MV_BASE * 4,	/* CH15 VDD5V */
};

static const struct mxs_lradc_of_config mxs_lradc_of_config[] = {
	[IMX23_LRADC] = {
		.irq_count	= ARRAY_SIZE(mx23_lradc_irq_names),
		.irq_name	= mx23_lradc_irq_names,
		.vref_mv	= mx23_vref_mv,
	},
	[IMX28_LRADC] = {
		.irq_count	= ARRAY_SIZE(mx28_lradc_irq_names),
		.irq_name	= mx28_lradc_irq_names,
		.vref_mv	= mx28_vref_mv,
	},
};

enum mxs_lradc_ts {
	MXS_LRADC_TOUCHSCREEN_NONE = 0,
	MXS_LRADC_TOUCHSCREEN_4WIRE,
	MXS_LRADC_TOUCHSCREEN_5WIRE,
};

/*
 * Touchscreen handling
 */
enum lradc_ts_plate {
	LRADC_TOUCH = 0,
	LRADC_SAMPLE_X,
	LRADC_SAMPLE_Y,
	LRADC_SAMPLE_PRESSURE,
	LRADC_SAMPLE_VALID,
};

enum mxs_lradc_divbytwo {
	MXS_LRADC_DIV_DISABLED = 0,
	MXS_LRADC_DIV_ENABLED,
};

struct mxs_lradc_scale {
	unsigned int		integer;
	unsigned int		nano;
};

struct mxs_lradc {
	struct device		*dev;
	void __iomem		*base;
	int			irq[13];

	struct clk		*clk;

	u32			*buffer;
	struct iio_trigger	*trig;

	struct mutex		lock;

	struct completion	completion;

	const u32		*vref_mv;
	struct mxs_lradc_scale	scale_avail[LRADC_MAX_TOTAL_CHANS][2];
	unsigned long		is_divided;

	/*
	 * When the touchscreen is enabled, we give it two private virtual
	 * channels: #6 and #7. This means that only 6 virtual channels (instead
	 * of 8) will be available for buffered capture.
	 */
#define TOUCHSCREEN_VCHANNEL1		7
#define TOUCHSCREEN_VCHANNEL2		6
#define BUFFER_VCHANS_LIMITED		0x3f
#define BUFFER_VCHANS_ALL		0xff
	u8			buffer_vchans;

	/*
	 * Furthermore, certain LRADC channels are shared between touchscreen
	 * and/or touch-buttons and generic LRADC block. Therefore when using
	 * either of these, these channels are not available for the regular
	 * sampling. The shared channels are as follows:
	 *
	 * CH0 -- Touch button #0
	 * CH1 -- Touch button #1
	 * CH2 -- Touch screen XPUL
	 * CH3 -- Touch screen YPLL
	 * CH4 -- Touch screen XNUL
	 * CH5 -- Touch screen YNLR
	 * CH6 -- Touch screen WIPER (5-wire only)
	 *
	 * The bit fields below represents which parts of the LRADC block are
	 * switched into special mode of operation. These channels can not
	 * be sampled as regular LRADC channels. The driver will refuse any
	 * attempt to sample these channels.
	 */
#define CHAN_MASK_TOUCHBUTTON		(BIT(1) | BIT(0))
#define CHAN_MASK_TOUCHSCREEN_4WIRE	(0xf << 2)
#define CHAN_MASK_TOUCHSCREEN_5WIRE	(0x1f << 2)
	enum mxs_lradc_ts	use_touchscreen;
	bool			use_touchbutton;

	struct input_dev	*ts_input;

	enum mxs_lradc_id	soc;
	enum lradc_ts_plate	cur_plate; /* state machine */
	bool			ts_valid;
	unsigned		ts_x_pos;
	unsigned		ts_y_pos;
	unsigned		ts_pressure;

	/* handle touchscreen's physical behaviour */
	/* samples per coordinate */
	unsigned		over_sample_cnt;
	/* time clocks between samples */
	unsigned		over_sample_delay;
	/* time in clocks to wait after the plates where switched */
	unsigned		settling_delay;
};

#define	LRADC_CTRL0				0x00
# define LRADC_CTRL0_MX28_TOUCH_DETECT_ENABLE	BIT(23)
# define LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE	BIT(22)
# define LRADC_CTRL0_MX28_YNNSW	/* YM */	BIT(21)
# define LRADC_CTRL0_MX28_YPNSW	/* YP */	BIT(20)
# define LRADC_CTRL0_MX28_YPPSW	/* YP */	BIT(19)
# define LRADC_CTRL0_MX28_XNNSW	/* XM */	BIT(18)
# define LRADC_CTRL0_MX28_XNPSW	/* XM */	BIT(17)
# define LRADC_CTRL0_MX28_XPPSW	/* XP */	BIT(16)

# define LRADC_CTRL0_MX23_TOUCH_DETECT_ENABLE	BIT(20)
# define LRADC_CTRL0_MX23_YM			BIT(19)
# define LRADC_CTRL0_MX23_XM			BIT(18)
# define LRADC_CTRL0_MX23_YP			BIT(17)
# define LRADC_CTRL0_MX23_XP			BIT(16)

# define LRADC_CTRL0_MX28_PLATE_MASK \
		(LRADC_CTRL0_MX28_TOUCH_DETECT_ENABLE | \
		LRADC_CTRL0_MX28_YNNSW | LRADC_CTRL0_MX28_YPNSW | \
		LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_XNNSW | \
		LRADC_CTRL0_MX28_XNPSW | LRADC_CTRL0_MX28_XPPSW)

# define LRADC_CTRL0_MX23_PLATE_MASK \
		(LRADC_CTRL0_MX23_TOUCH_DETECT_ENABLE | \
		LRADC_CTRL0_MX23_YM | LRADC_CTRL0_MX23_XM | \
		LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_XP)

#define	LRADC_CTRL1				0x10
#define	LRADC_CTRL1_TOUCH_DETECT_IRQ_EN		BIT(24)
#define	LRADC_CTRL1_LRADC_IRQ_EN(n)		(1 << ((n) + 16))
#define	LRADC_CTRL1_MX28_LRADC_IRQ_EN_MASK	(0x1fff << 16)
#define	LRADC_CTRL1_MX23_LRADC_IRQ_EN_MASK	(0x01ff << 16)
#define	LRADC_CTRL1_LRADC_IRQ_EN_OFFSET		16
#define	LRADC_CTRL1_TOUCH_DETECT_IRQ		BIT(8)
#define	LRADC_CTRL1_LRADC_IRQ(n)		(1 << (n))
#define	LRADC_CTRL1_MX28_LRADC_IRQ_MASK		0x1fff
#define	LRADC_CTRL1_MX23_LRADC_IRQ_MASK		0x01ff
#define	LRADC_CTRL1_LRADC_IRQ_OFFSET		0

#define	LRADC_CTRL2				0x20
#define	LRADC_CTRL2_DIVIDE_BY_TWO_OFFSET	24
#define	LRADC_CTRL2_TEMPSENSE_PWD		BIT(15)

#define	LRADC_STATUS				0x40
#define	LRADC_STATUS_TOUCH_DETECT_RAW		BIT(0)

#define	LRADC_CH(n)				(0x50 + (0x10 * (n)))
#define	LRADC_CH_ACCUMULATE			BIT(29)
#define	LRADC_CH_NUM_SAMPLES_MASK		(0x1f << 24)
#define	LRADC_CH_NUM_SAMPLES_OFFSET		24
#define	LRADC_CH_NUM_SAMPLES(x) \
				((x) << LRADC_CH_NUM_SAMPLES_OFFSET)
#define	LRADC_CH_VALUE_MASK			0x3ffff
#define	LRADC_CH_VALUE_OFFSET			0

#define	LRADC_DELAY(n)				(0xd0 + (0x10 * (n)))
#define	LRADC_DELAY_TRIGGER_LRADCS_MASK		(0xffUL << 24)
#define	LRADC_DELAY_TRIGGER_LRADCS_OFFSET	24
#define	LRADC_DELAY_TRIGGER(x) \
				(((x) << LRADC_DELAY_TRIGGER_LRADCS_OFFSET) & \
				LRADC_DELAY_TRIGGER_LRADCS_MASK)
#define	LRADC_DELAY_KICK			BIT(20)
#define	LRADC_DELAY_TRIGGER_DELAYS_MASK		(0xf << 16)
#define	LRADC_DELAY_TRIGGER_DELAYS_OFFSET	16
#define	LRADC_DELAY_TRIGGER_DELAYS(x) \
				(((x) << LRADC_DELAY_TRIGGER_DELAYS_OFFSET) & \
				LRADC_DELAY_TRIGGER_DELAYS_MASK)
#define	LRADC_DELAY_LOOP_COUNT_MASK		(0x1f << 11)
#define	LRADC_DELAY_LOOP_COUNT_OFFSET		11
#define	LRADC_DELAY_LOOP(x) \
				(((x) << LRADC_DELAY_LOOP_COUNT_OFFSET) & \
				LRADC_DELAY_LOOP_COUNT_MASK)
#define	LRADC_DELAY_DELAY_MASK			0x7ff
#define	LRADC_DELAY_DELAY_OFFSET		0
#define	LRADC_DELAY_DELAY(x) \
				(((x) << LRADC_DELAY_DELAY_OFFSET) & \
				LRADC_DELAY_DELAY_MASK)

#define	LRADC_CTRL4				0x140
#define	LRADC_CTRL4_LRADCSELECT_MASK(n)		(0xf << ((n) * 4))
#define	LRADC_CTRL4_LRADCSELECT_OFFSET(n)	((n) * 4)
#define	LRADC_CTRL4_LRADCSELECT(n, x) \
				(((x) << LRADC_CTRL4_LRADCSELECT_OFFSET(n)) & \
				LRADC_CTRL4_LRADCSELECT_MASK(n))

#define LRADC_RESOLUTION			12
#define LRADC_SINGLE_SAMPLE_MASK		((1 << LRADC_RESOLUTION) - 1)

static void mxs_lradc_reg_set(struct mxs_lradc *lradc, u32 val, u32 reg)
{
	writel(val, lradc->base + reg + STMP_OFFSET_REG_SET);
}

static void mxs_lradc_reg_clear(struct mxs_lradc *lradc, u32 val, u32 reg)
{
	writel(val, lradc->base + reg + STMP_OFFSET_REG_CLR);
}

static void mxs_lradc_reg_wrt(struct mxs_lradc *lradc, u32 val, u32 reg)
{
	writel(val, lradc->base + reg);
}

static u32 mxs_lradc_plate_mask(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_PLATE_MASK;
	return LRADC_CTRL0_MX28_PLATE_MASK;
}

static u32 mxs_lradc_irq_en_mask(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL1_MX23_LRADC_IRQ_EN_MASK;
	return LRADC_CTRL1_MX28_LRADC_IRQ_EN_MASK;
}

static u32 mxs_lradc_irq_mask(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL1_MX23_LRADC_IRQ_MASK;
	return LRADC_CTRL1_MX28_LRADC_IRQ_MASK;
}

static u32 mxs_lradc_touch_detect_bit(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_TOUCH_DETECT_ENABLE;
	return LRADC_CTRL0_MX28_TOUCH_DETECT_ENABLE;
}

static u32 mxs_lradc_drive_x_plate(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_XP | LRADC_CTRL0_MX23_XM;
	return LRADC_CTRL0_MX28_XPPSW | LRADC_CTRL0_MX28_XNNSW;
}

static u32 mxs_lradc_drive_y_plate(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_YM;
	return LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_YNNSW;
}

static u32 mxs_lradc_drive_pressure(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_XM;
	return LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_XNNSW;
}

static bool mxs_lradc_check_touch_event(struct mxs_lradc *lradc)
{
	return !!(readl(lradc->base + LRADC_STATUS) &
					LRADC_STATUS_TOUCH_DETECT_RAW);
}

static void mxs_lradc_map_channel(struct mxs_lradc *lradc, unsigned vch,
				  unsigned ch)
{
	mxs_lradc_reg_clear(lradc, LRADC_CTRL4_LRADCSELECT_MASK(vch),
			    LRADC_CTRL4);
	mxs_lradc_reg_set(lradc, LRADC_CTRL4_LRADCSELECT(vch, ch), LRADC_CTRL4);
}

static void mxs_lradc_setup_ts_channel(struct mxs_lradc *lradc, unsigned ch)
{
	/*
	 * prepare for oversampling conversion
	 *
	 * from the datasheet:
	 * "The ACCUMULATE bit in the appropriate channel register
	 * HW_LRADC_CHn must be set to 1 if NUM_SAMPLES is greater then 0;
	 * otherwise, the IRQs will not fire."
	 */
	mxs_lradc_reg_wrt(lradc, LRADC_CH_ACCUMULATE |
			  LRADC_CH_NUM_SAMPLES(lradc->over_sample_cnt - 1),
			  LRADC_CH(ch));

	/* from the datasheet:
	 * "Software must clear this register in preparation for a
	 * multi-cycle accumulation.
	 */
	mxs_lradc_reg_clear(lradc, LRADC_CH_VALUE_MASK, LRADC_CH(ch));

	/*
	 * prepare the delay/loop unit according to the oversampling count
	 *
	 * from the datasheet:
	 * "The DELAY fields in HW_LRADC_DELAY0, HW_LRADC_DELAY1,
	 * HW_LRADC_DELAY2, and HW_LRADC_DELAY3 must be non-zero; otherwise,
	 * the LRADC will not trigger the delay group."
	 */
	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_TRIGGER(1 << ch) |
			  LRADC_DELAY_TRIGGER_DELAYS(0) |
			  LRADC_DELAY_LOOP(lradc->over_sample_cnt - 1) |
			  LRADC_DELAY_DELAY(lradc->over_sample_delay - 1),
			  LRADC_DELAY(3));

	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ(ch), LRADC_CTRL1);

	/*
	 * after changing the touchscreen plates setting
	 * the signals need some initial time to settle. Start the
	 * SoC's delay unit and start the conversion later
	 * and automatically.
	 */
	mxs_lradc_reg_wrt(
		lradc,
		LRADC_DELAY_TRIGGER(0) | /* don't trigger ADC */
		LRADC_DELAY_TRIGGER_DELAYS(BIT(3)) | /* trigger DELAY unit#3 */
		LRADC_DELAY_KICK |
		LRADC_DELAY_DELAY(lradc->settling_delay),
		LRADC_DELAY(2));
}

/*
 * Pressure detection is special:
 * We want to do both required measurements for the pressure detection in
 * one turn. Use the hardware features to chain both conversions and let the
 * hardware report one interrupt if both conversions are done
 */
static void mxs_lradc_setup_ts_pressure(struct mxs_lradc *lradc, unsigned ch1,
					unsigned ch2)
{
	u32 reg;

	/*
	 * prepare for oversampling conversion
	 *
	 * from the datasheet:
	 * "The ACCUMULATE bit in the appropriate channel register
	 * HW_LRADC_CHn must be set to 1 if NUM_SAMPLES is greater then 0;
	 * otherwise, the IRQs will not fire."
	 */
	reg = LRADC_CH_ACCUMULATE |
		LRADC_CH_NUM_SAMPLES(lradc->over_sample_cnt - 1);
	mxs_lradc_reg_wrt(lradc, reg, LRADC_CH(ch1));
	mxs_lradc_reg_wrt(lradc, reg, LRADC_CH(ch2));

	/* from the datasheet:
	 * "Software must clear this register in preparation for a
	 * multi-cycle accumulation.
	 */
	mxs_lradc_reg_clear(lradc, LRADC_CH_VALUE_MASK, LRADC_CH(ch1));
	mxs_lradc_reg_clear(lradc, LRADC_CH_VALUE_MASK, LRADC_CH(ch2));

	/* prepare the delay/loop unit according to the oversampling count */
	mxs_lradc_reg_wrt(
		    lradc,
		    LRADC_DELAY_TRIGGER(1 << ch1) |
		    LRADC_DELAY_TRIGGER(1 << ch2) | /* start both channels */
		    LRADC_DELAY_TRIGGER_DELAYS(0) |
		    LRADC_DELAY_LOOP(lradc->over_sample_cnt - 1) |
		    LRADC_DELAY_DELAY(lradc->over_sample_delay - 1),
		    LRADC_DELAY(3));

	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ(ch2), LRADC_CTRL1);

	/*
	 * after changing the touchscreen plates setting
	 * the signals need some initial time to settle. Start the
	 * SoC's delay unit and start the conversion later
	 * and automatically.
	 */
	mxs_lradc_reg_wrt(
		lradc,
		LRADC_DELAY_TRIGGER(0) | /* don't trigger ADC */
		LRADC_DELAY_TRIGGER_DELAYS(BIT(3)) | /* trigger DELAY unit#3 */
		LRADC_DELAY_KICK |
		LRADC_DELAY_DELAY(lradc->settling_delay), LRADC_DELAY(2));
}

static unsigned mxs_lradc_read_raw_channel(struct mxs_lradc *lradc,
					   unsigned channel)
{
	u32 reg;
	unsigned num_samples, val;

	reg = readl(lradc->base + LRADC_CH(channel));
	if (reg & LRADC_CH_ACCUMULATE)
		num_samples = lradc->over_sample_cnt;
	else
		num_samples = 1;

	val = (reg & LRADC_CH_VALUE_MASK) >> LRADC_CH_VALUE_OFFSET;
	return val / num_samples;
}

static unsigned mxs_lradc_read_ts_pressure(struct mxs_lradc *lradc,
					   unsigned ch1, unsigned ch2)
{
	u32 reg, mask;
	unsigned pressure, m1, m2;

	mask = LRADC_CTRL1_LRADC_IRQ(ch1) | LRADC_CTRL1_LRADC_IRQ(ch2);
	reg = readl(lradc->base + LRADC_CTRL1) & mask;

	while (reg != mask) {
		reg = readl(lradc->base + LRADC_CTRL1) & mask;
		dev_dbg(lradc->dev, "One channel is still busy: %X\n", reg);
	}

	m1 = mxs_lradc_read_raw_channel(lradc, ch1);
	m2 = mxs_lradc_read_raw_channel(lradc, ch2);

	if (m2 == 0) {
		dev_warn(lradc->dev, "Cannot calculate pressure\n");
		return 1 << (LRADC_RESOLUTION - 1);
	}

	/* simply scale the value from 0 ... max ADC resolution */
	pressure = m1;
	pressure *= (1 << LRADC_RESOLUTION);
	pressure /= m2;

	dev_dbg(lradc->dev, "Pressure = %u\n", pressure);
	return pressure;
}

#define TS_CH_XP 2
#define TS_CH_YP 3
#define TS_CH_XM 4
#define TS_CH_YM 5

/*
 * YP(open)--+-------------+
 *           |             |--+
 *           |             |  |
 *    YM(-)--+-------------+  |
 *             +--------------+
 *             |              |
 *         XP(weak+)        XM(open)
 *
 * "weak+" means 200k Ohm VDDIO
 * (-) means GND
 */
static void mxs_lradc_setup_touch_detection(struct mxs_lradc *lradc)
{
	/*
	 * In order to detect a touch event the 'touch detect enable' bit
	 * enables:
	 *  - a weak pullup to the X+ connector
	 *  - a strong ground at the Y- connector
	 */
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_touch_detect_bit(lradc),
			  LRADC_CTRL0);
}

/*
 * YP(meas)--+-------------+
 *           |             |--+
 *           |             |  |
 * YM(open)--+-------------+  |
 *             +--------------+
 *             |              |
 *           XP(+)          XM(-)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_x_pos(struct mxs_lradc *lradc)
{
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_drive_x_plate(lradc), LRADC_CTRL0);

	lradc->cur_plate = LRADC_SAMPLE_X;
	mxs_lradc_map_channel(lradc, TOUCHSCREEN_VCHANNEL1, TS_CH_YP);
	mxs_lradc_setup_ts_channel(lradc, TOUCHSCREEN_VCHANNEL1);
}

/*
 *   YP(+)--+-------------+
 *          |             |--+
 *          |             |  |
 *   YM(-)--+-------------+  |
 *            +--------------+
 *            |              |
 *         XP(open)        XM(meas)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_y_pos(struct mxs_lradc *lradc)
{
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_drive_y_plate(lradc), LRADC_CTRL0);

	lradc->cur_plate = LRADC_SAMPLE_Y;
	mxs_lradc_map_channel(lradc, TOUCHSCREEN_VCHANNEL1, TS_CH_XM);
	mxs_lradc_setup_ts_channel(lradc, TOUCHSCREEN_VCHANNEL1);
}

/*
 *    YP(+)--+-------------+
 *           |             |--+
 *           |             |  |
 * YM(meas)--+-------------+  |
 *             +--------------+
 *             |              |
 *          XP(meas)        XM(-)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_pressure(struct mxs_lradc *lradc)
{
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_drive_pressure(lradc), LRADC_CTRL0);

	lradc->cur_plate = LRADC_SAMPLE_PRESSURE;
	mxs_lradc_map_channel(lradc, TOUCHSCREEN_VCHANNEL1, TS_CH_YM);
	mxs_lradc_map_channel(lradc, TOUCHSCREEN_VCHANNEL2, TS_CH_XP);
	mxs_lradc_setup_ts_pressure(lradc, TOUCHSCREEN_VCHANNEL2,
				    TOUCHSCREEN_VCHANNEL1);
}

static void mxs_lradc_enable_touch_detection(struct mxs_lradc *lradc)
{
	mxs_lradc_setup_touch_detection(lradc);

	lradc->cur_plate = LRADC_TOUCH;
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ |
			    LRADC_CTRL1_TOUCH_DETECT_IRQ_EN, LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ_EN, LRADC_CTRL1);
}

static void mxs_lradc_start_touch_event(struct mxs_lradc *lradc)
{
	mxs_lradc_reg_clear(lradc,
			    LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
			    LRADC_CTRL1);
	mxs_lradc_reg_set(lradc,
			  LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL1),
			  LRADC_CTRL1);
	/*
	 * start with the Y-pos, because it uses nearly the same plate
	 * settings like the touch detection
	 */
	mxs_lradc_prepare_y_pos(lradc);
}

static void mxs_lradc_report_ts_event(struct mxs_lradc *lradc)
{
	input_report_abs(lradc->ts_input, ABS_X, lradc->ts_x_pos);
	input_report_abs(lradc->ts_input, ABS_Y, lradc->ts_y_pos);
	input_report_abs(lradc->ts_input, ABS_PRESSURE, lradc->ts_pressure);
	input_report_key(lradc->ts_input, BTN_TOUCH, 1);
	input_sync(lradc->ts_input);
}

static void mxs_lradc_complete_touch_event(struct mxs_lradc *lradc)
{
	mxs_lradc_setup_touch_detection(lradc);
	lradc->cur_plate = LRADC_SAMPLE_VALID;
	/*
	 * start a dummy conversion to burn time to settle the signals
	 * note: we are not interested in the conversion's value
	 */
	mxs_lradc_reg_wrt(lradc, 0, LRADC_CH(TOUCHSCREEN_VCHANNEL1));
	mxs_lradc_reg_clear(lradc,
			    LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1) |
			    LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL2),
			    LRADC_CTRL1);
	mxs_lradc_reg_wrt(
		    lradc,
		    LRADC_DELAY_TRIGGER(1 << TOUCHSCREEN_VCHANNEL1) |
		    LRADC_DELAY_KICK | LRADC_DELAY_DELAY(10), /* waste 5 ms */
		    LRADC_DELAY(2));
}

/*
 * in order to avoid false measurements, report only samples where
 * the surface is still touched after the position measurement
 */
static void mxs_lradc_finish_touch_event(struct mxs_lradc *lradc, bool valid)
{
	/* if it is still touched, report the sample */
	if (valid && mxs_lradc_check_touch_event(lradc)) {
		lradc->ts_valid = true;
		mxs_lradc_report_ts_event(lradc);
	}

	/* if it is even still touched, continue with the next measurement */
	if (mxs_lradc_check_touch_event(lradc)) {
		mxs_lradc_prepare_y_pos(lradc);
		return;
	}

	if (lradc->ts_valid) {
		/* signal the release */
		lradc->ts_valid = false;
		input_report_key(lradc->ts_input, BTN_TOUCH, 0);
		input_sync(lradc->ts_input);
	}

	/* if it is released, wait for the next touch via IRQ */
	lradc->cur_plate = LRADC_TOUCH;
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(2));
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(3));
	mxs_lradc_reg_clear(lradc,
			    LRADC_CTRL1_TOUCH_DETECT_IRQ |
			    LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL1) |
			    LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1),
			    LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ_EN, LRADC_CTRL1);
}

/* touchscreen's state machine */
static void mxs_lradc_handle_touch(struct mxs_lradc *lradc)
{
	switch (lradc->cur_plate) {
	case LRADC_TOUCH:
		if (mxs_lradc_check_touch_event(lradc))
			mxs_lradc_start_touch_event(lradc);
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ,
				    LRADC_CTRL1);
		return;

	case LRADC_SAMPLE_Y:
		lradc->ts_y_pos =
		    mxs_lradc_read_raw_channel(lradc,
					       TOUCHSCREEN_VCHANNEL1);
		mxs_lradc_prepare_x_pos(lradc);
		return;

	case LRADC_SAMPLE_X:
		lradc->ts_x_pos =
		    mxs_lradc_read_raw_channel(lradc,
					       TOUCHSCREEN_VCHANNEL1);
		mxs_lradc_prepare_pressure(lradc);
		return;

	case LRADC_SAMPLE_PRESSURE:
		lradc->ts_pressure =
		    mxs_lradc_read_ts_pressure(lradc,
					       TOUCHSCREEN_VCHANNEL2,
					       TOUCHSCREEN_VCHANNEL1);
		mxs_lradc_complete_touch_event(lradc);
		return;

	case LRADC_SAMPLE_VALID:
		mxs_lradc_finish_touch_event(lradc, 1);
		break;
	}
}

/*
 * Raw I/O operations
 */
static int mxs_lradc_read_single(struct iio_dev *iio_dev, int chan, int *val)
{
	struct mxs_lradc *lradc = iio_priv(iio_dev);
	int ret;

	/*
	 * See if there is no buffered operation in progress. If there is, simply
	 * bail out. This can be improved to support both buffered and raw IO at
	 * the same time, yet the code becomes horribly complicated. Therefore I
	 * applied KISS principle here.
	 */
	ret = mutex_trylock(&lradc->lock);
	if (!ret)
		return -EBUSY;

	reinit_completion(&lradc->completion);

	/*
	 * No buffered operation in progress, map the channel and trigger it.
	 * Virtual channel 0 is always used here as the others are always not
	 * used if doing raw sampling.
	 */
	if (lradc->soc == IMX28_LRADC)
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ_EN(0),
				    LRADC_CTRL1);
	mxs_lradc_reg_clear(lradc, 0x1, LRADC_CTRL0);

	/* Enable / disable the divider per requirement */
	if (test_bit(chan, &lradc->is_divided))
		mxs_lradc_reg_set(lradc,
				  1 << LRADC_CTRL2_DIVIDE_BY_TWO_OFFSET,
				  LRADC_CTRL2);
	else
		mxs_lradc_reg_clear(lradc,
				    1 << LRADC_CTRL2_DIVIDE_BY_TWO_OFFSET,
				    LRADC_CTRL2);

	/* Clean the slot's previous content, then set new one. */
	mxs_lradc_reg_clear(lradc, LRADC_CTRL4_LRADCSELECT_MASK(0),
			    LRADC_CTRL4);
	mxs_lradc_reg_set(lradc, chan, LRADC_CTRL4);

	mxs_lradc_reg_wrt(lradc, 0, LRADC_CH(0));

	/* Enable the IRQ and start sampling the channel. */
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_LRADC_IRQ_EN(0), LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, BIT(0), LRADC_CTRL0);

	/* Wait for completion on the channel, 1 second max. */
	ret = wait_for_completion_killable_timeout(&lradc->completion, HZ);
	if (!ret)
		ret = -ETIMEDOUT;
	if (ret < 0)
		goto err;

	/* Read the data. */
	*val = readl(lradc->base + LRADC_CH(0)) & LRADC_CH_VALUE_MASK;
	ret = IIO_VAL_INT;

err:
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ_EN(0), LRADC_CTRL1);

	mutex_unlock(&lradc->lock);

	return ret;
}

static int mxs_lradc_read_temp(struct iio_dev *iio_dev, int *val)
{
	int ret, min, max;

	ret = mxs_lradc_read_single(iio_dev, 8, &min);
	if (ret != IIO_VAL_INT)
		return ret;

	ret = mxs_lradc_read_single(iio_dev, 9, &max);
	if (ret != IIO_VAL_INT)
		return ret;

	*val = max - min;

	return IIO_VAL_INT;
}

static int mxs_lradc_read_raw(struct iio_dev *iio_dev,
			      const struct iio_chan_spec *chan,
			      int *val, int *val2, long m)
{
	struct mxs_lradc *lradc = iio_priv(iio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_TEMP)
			return mxs_lradc_read_temp(iio_dev, val);

		return mxs_lradc_read_single(iio_dev, chan->channel, val);

	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP) {
			/* From the datasheet, we have to multiply by 1.012 and
			 * divide by 4
			 */
			*val = 0;
			*val2 = 253000;
			return IIO_VAL_INT_PLUS_MICRO;
		}

		*val = lradc->vref_mv[chan->channel];
		*val2 = chan->scan_type.realbits -
			test_bit(chan->channel, &lradc->is_divided);
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			/* The calculated value from the ADC is in Kelvin, we
			 * want Celsius for hwmon so the offset is -273.15
			 * The offset is applied before scaling so it is
			 * actually -213.15 * 4 / 1.012 = -1079.644268
			 */
			*val = -1079;
			*val2 = 644268;

			return IIO_VAL_INT_PLUS_MICRO;
		}

		return -EINVAL;

	default:
		break;
	}

	return -EINVAL;
}

static int mxs_lradc_write_raw(struct iio_dev *iio_dev,
			       const struct iio_chan_spec *chan,
			       int val, int val2, long m)
{
	struct mxs_lradc *lradc = iio_priv(iio_dev);
	struct mxs_lradc_scale *scale_avail =
			lradc->scale_avail[chan->channel];
	int ret;

	ret = mutex_trylock(&lradc->lock);
	if (!ret)
		return -EBUSY;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		ret = -EINVAL;
		if (val == scale_avail[MXS_LRADC_DIV_DISABLED].integer &&
		    val2 == scale_avail[MXS_LRADC_DIV_DISABLED].nano) {
			/* divider by two disabled */
			clear_bit(chan->channel, &lradc->is_divided);
			ret = 0;
		} else if (val == scale_avail[MXS_LRADC_DIV_ENABLED].integer &&
			   val2 == scale_avail[MXS_LRADC_DIV_ENABLED].nano) {
			/* divider by two enabled */
			set_bit(chan->channel, &lradc->is_divided);
			ret = 0;
		}

		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&lradc->lock);

	return ret;
}

static int mxs_lradc_write_raw_get_fmt(struct iio_dev *iio_dev,
				       const struct iio_chan_spec *chan,
				       long m)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static ssize_t mxs_lradc_show_scale_available_ch(struct device *dev,
						 struct device_attribute *attr,
						 char *buf,
						 int ch)
{
	struct iio_dev *iio = dev_to_iio_dev(dev);
	struct mxs_lradc *lradc = iio_priv(iio);
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(lradc->scale_avail[ch]); i++)
		len += sprintf(buf + len, "%u.%09u ",
			       lradc->scale_avail[ch][i].integer,
			       lradc->scale_avail[ch][i].nano);

	len += sprintf(buf + len, "\n");

	return len;
}

static ssize_t mxs_lradc_show_scale_available(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct iio_dev_attr *iio_attr = to_iio_dev_attr(attr);

	return mxs_lradc_show_scale_available_ch(dev, attr, buf,
						 iio_attr->address);
}

#define SHOW_SCALE_AVAILABLE_ATTR(ch)					\
static IIO_DEVICE_ATTR(in_voltage##ch##_scale_available, S_IRUGO,	\
		       mxs_lradc_show_scale_available, NULL, ch)

SHOW_SCALE_AVAILABLE_ATTR(0);
SHOW_SCALE_AVAILABLE_ATTR(1);
SHOW_SCALE_AVAILABLE_ATTR(2);
SHOW_SCALE_AVAILABLE_ATTR(3);
SHOW_SCALE_AVAILABLE_ATTR(4);
SHOW_SCALE_AVAILABLE_ATTR(5);
SHOW_SCALE_AVAILABLE_ATTR(6);
SHOW_SCALE_AVAILABLE_ATTR(7);
SHOW_SCALE_AVAILABLE_ATTR(10);
SHOW_SCALE_AVAILABLE_ATTR(11);
SHOW_SCALE_AVAILABLE_ATTR(12);
SHOW_SCALE_AVAILABLE_ATTR(13);
SHOW_SCALE_AVAILABLE_ATTR(14);
SHOW_SCALE_AVAILABLE_ATTR(15);

static struct attribute *mxs_lradc_attributes[] = {
	&iio_dev_attr_in_voltage0_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage1_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage2_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage3_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage4_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage5_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage6_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage7_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage10_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage11_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage12_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage13_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage14_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage15_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group mxs_lradc_attribute_group = {
	.attrs = mxs_lradc_attributes,
};

static const struct iio_info mxs_lradc_iio_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= mxs_lradc_read_raw,
	.write_raw		= mxs_lradc_write_raw,
	.write_raw_get_fmt	= mxs_lradc_write_raw_get_fmt,
	.attrs			= &mxs_lradc_attribute_group,
};

static int mxs_lradc_ts_open(struct input_dev *dev)
{
	struct mxs_lradc *lradc = input_get_drvdata(dev);

	/* Enable the touch-detect circuitry. */
	mxs_lradc_enable_touch_detection(lradc);

	return 0;
}

static void mxs_lradc_disable_ts(struct mxs_lradc *lradc)
{
	/* stop all interrupts from firing */
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ_EN |
		LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL1) |
		LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL2), LRADC_CTRL1);

	/* Power-down touchscreen touch-detect circuitry. */
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
}

static void mxs_lradc_ts_close(struct input_dev *dev)
{
	struct mxs_lradc *lradc = input_get_drvdata(dev);

	mxs_lradc_disable_ts(lradc);
}

static int mxs_lradc_ts_register(struct mxs_lradc *lradc)
{
	struct input_dev *input;
	struct device *dev = lradc->dev;
	int ret;

	if (!lradc->use_touchscreen)
		return 0;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->name = DRIVER_NAME;
	input->id.bustype = BUS_HOST;
	input->dev.parent = dev;
	input->open = mxs_lradc_ts_open;
	input->close = mxs_lradc_ts_close;

	__set_bit(EV_ABS, input->evbit);
	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	input_set_abs_params(input, ABS_X, 0, LRADC_SINGLE_SAMPLE_MASK, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, LRADC_SINGLE_SAMPLE_MASK, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, LRADC_SINGLE_SAMPLE_MASK,
			     0, 0);

	lradc->ts_input = input;
	input_set_drvdata(input, lradc);
	ret = input_register_device(input);
	if (ret)
		input_free_device(lradc->ts_input);

	return ret;
}

static void mxs_lradc_ts_unregister(struct mxs_lradc *lradc)
{
	if (!lradc->use_touchscreen)
		return;

	mxs_lradc_disable_ts(lradc);
	input_unregister_device(lradc->ts_input);
}

/*
 * IRQ Handling
 */
static irqreturn_t mxs_lradc_handle_irq(int irq, void *data)
{
	struct iio_dev *iio = data;
	struct mxs_lradc *lradc = iio_priv(iio);
	unsigned long reg = readl(lradc->base + LRADC_CTRL1);
	u32 clr_irq = mxs_lradc_irq_mask(lradc);
	const u32 ts_irq_mask =
		LRADC_CTRL1_TOUCH_DETECT_IRQ |
		LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1) |
		LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL2);

	if (!(reg & mxs_lradc_irq_mask(lradc)))
		return IRQ_NONE;

	if (lradc->use_touchscreen && (reg & ts_irq_mask)) {
		mxs_lradc_handle_touch(lradc);

		/* Make sure we don't clear the next conversion's interrupt. */
		clr_irq &= ~(LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1) |
				LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL2));
	}

	if (iio_buffer_enabled(iio)) {
		if (reg & lradc->buffer_vchans)
			iio_trigger_poll(iio->trig);
	} else if (reg & LRADC_CTRL1_LRADC_IRQ(0)) {
		complete(&lradc->completion);
	}

	mxs_lradc_reg_clear(lradc, reg & clr_irq, LRADC_CTRL1);

	return IRQ_HANDLED;
}

/*
 * Trigger handling
 */
static irqreturn_t mxs_lradc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio = pf->indio_dev;
	struct mxs_lradc *lradc = iio_priv(iio);
	const u32 chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	unsigned int i, j = 0;

	for_each_set_bit(i, iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS) {
		lradc->buffer[j] = readl(lradc->base + LRADC_CH(j));
		mxs_lradc_reg_wrt(lradc, chan_value, LRADC_CH(j));
		lradc->buffer[j] &= LRADC_CH_VALUE_MASK;
		lradc->buffer[j] /= LRADC_DELAY_TIMER_LOOP;
		j++;
	}

	iio_push_to_buffers_with_timestamp(iio, lradc->buffer, pf->timestamp);

	iio_trigger_notify_done(iio->trig);

	return IRQ_HANDLED;
}

static int mxs_lradc_configure_trigger(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio = iio_trigger_get_drvdata(trig);
	struct mxs_lradc *lradc = iio_priv(iio);
	const u32 st = state ? STMP_OFFSET_REG_SET : STMP_OFFSET_REG_CLR;

	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_KICK, LRADC_DELAY(0) + st);

	return 0;
}

static const struct iio_trigger_ops mxs_lradc_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &mxs_lradc_configure_trigger,
};

static int mxs_lradc_trigger_init(struct iio_dev *iio)
{
	int ret;
	struct iio_trigger *trig;
	struct mxs_lradc *lradc = iio_priv(iio);

	trig = iio_trigger_alloc("%s-dev%i", iio->name, iio->id);
	if (!trig)
		return -ENOMEM;

	trig->dev.parent = lradc->dev;
	iio_trigger_set_drvdata(trig, iio);
	trig->ops = &mxs_lradc_trigger_ops;

	ret = iio_trigger_register(trig);
	if (ret) {
		iio_trigger_free(trig);
		return ret;
	}

	lradc->trig = trig;

	return 0;
}

static void mxs_lradc_trigger_remove(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);

	iio_trigger_unregister(lradc->trig);
	iio_trigger_free(lradc->trig);
}

static int mxs_lradc_buffer_preenable(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);
	int ret = 0, chan, ofs = 0;
	unsigned long enable = 0;
	u32 ctrl4_set = 0;
	u32 ctrl4_clr = 0;
	u32 ctrl1_irq = 0;
	const u32 chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	const int len = bitmap_weight(iio->active_scan_mask,
			LRADC_MAX_TOTAL_CHANS);

	if (!len)
		return -EINVAL;

	/*
	 * Lock the driver so raw access can not be done during buffered
	 * operation. This simplifies the code a lot.
	 */
	ret = mutex_trylock(&lradc->lock);
	if (!ret)
		return -EBUSY;

	lradc->buffer = kmalloc_array(len, sizeof(*lradc->buffer), GFP_KERNEL);
	if (!lradc->buffer) {
		ret = -ENOMEM;
		goto err_mem;
	}

	if (lradc->soc == IMX28_LRADC)
		mxs_lradc_reg_clear(
			lradc,
			lradc->buffer_vchans << LRADC_CTRL1_LRADC_IRQ_EN_OFFSET,
			LRADC_CTRL1);
	mxs_lradc_reg_clear(lradc, lradc->buffer_vchans, LRADC_CTRL0);

	for_each_set_bit(chan, iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS) {
		ctrl4_set |= chan << LRADC_CTRL4_LRADCSELECT_OFFSET(ofs);
		ctrl4_clr |= LRADC_CTRL4_LRADCSELECT_MASK(ofs);
		ctrl1_irq |= LRADC_CTRL1_LRADC_IRQ_EN(ofs);
		mxs_lradc_reg_wrt(lradc, chan_value, LRADC_CH(ofs));
		bitmap_set(&enable, ofs, 1);
		ofs++;
	}

	mxs_lradc_reg_clear(lradc, LRADC_DELAY_TRIGGER_LRADCS_MASK |
			    LRADC_DELAY_KICK, LRADC_DELAY(0));
	mxs_lradc_reg_clear(lradc, ctrl4_clr, LRADC_CTRL4);
	mxs_lradc_reg_set(lradc, ctrl4_set, LRADC_CTRL4);
	mxs_lradc_reg_set(lradc, ctrl1_irq, LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, enable << LRADC_DELAY_TRIGGER_LRADCS_OFFSET,
			  LRADC_DELAY(0));

	return 0;

err_mem:
	mutex_unlock(&lradc->lock);
	return ret;
}

static int mxs_lradc_buffer_postdisable(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);

	mxs_lradc_reg_clear(lradc, LRADC_DELAY_TRIGGER_LRADCS_MASK |
			    LRADC_DELAY_KICK, LRADC_DELAY(0));

	mxs_lradc_reg_clear(lradc, lradc->buffer_vchans, LRADC_CTRL0);
	if (lradc->soc == IMX28_LRADC)
		mxs_lradc_reg_clear(
			lradc,
			lradc->buffer_vchans << LRADC_CTRL1_LRADC_IRQ_EN_OFFSET,
			LRADC_CTRL1);

	kfree(lradc->buffer);
	mutex_unlock(&lradc->lock);

	return 0;
}

static bool mxs_lradc_validate_scan_mask(struct iio_dev *iio,
					 const unsigned long *mask)
{
	struct mxs_lradc *lradc = iio_priv(iio);
	const int map_chans = bitmap_weight(mask, LRADC_MAX_TOTAL_CHANS);
	int rsvd_chans = 0;
	unsigned long rsvd_mask = 0;

	if (lradc->use_touchbutton)
		rsvd_mask |= CHAN_MASK_TOUCHBUTTON;
	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_4WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_4WIRE;
	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_5WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_5WIRE;

	if (lradc->use_touchbutton)
		rsvd_chans++;
	if (lradc->use_touchscreen)
		rsvd_chans += 2;

	/* Test for attempts to map channels with special mode of operation. */
	if (bitmap_intersects(mask, &rsvd_mask, LRADC_MAX_TOTAL_CHANS))
		return false;

	/* Test for attempts to map more channels then available slots. */
	if (map_chans + rsvd_chans > LRADC_MAX_MAPPED_CHANS)
		return false;

	return true;
}

static const struct iio_buffer_setup_ops mxs_lradc_buffer_ops = {
	.preenable = &mxs_lradc_buffer_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &mxs_lradc_buffer_postdisable,
	.validate_scan_mask = &mxs_lradc_validate_scan_mask,
};

/*
 * Driver initialization
 */

#define MXS_ADC_CHAN(idx, chan_type, name) {			\
	.type = (chan_type),					\
	.indexed = 1,						\
	.scan_index = (idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.channel = (idx),					\
	.address = (idx),					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = LRADC_RESOLUTION,			\
		.storagebits = 32,				\
	},							\
	.datasheet_name = (name),				\
}

static const struct iio_chan_spec mx23_lradc_chan_spec[] = {
	MXS_ADC_CHAN(0, IIO_VOLTAGE, "LRADC0"),
	MXS_ADC_CHAN(1, IIO_VOLTAGE, "LRADC1"),
	MXS_ADC_CHAN(2, IIO_VOLTAGE, "LRADC2"),
	MXS_ADC_CHAN(3, IIO_VOLTAGE, "LRADC3"),
	MXS_ADC_CHAN(4, IIO_VOLTAGE, "LRADC4"),
	MXS_ADC_CHAN(5, IIO_VOLTAGE, "LRADC5"),
	MXS_ADC_CHAN(6, IIO_VOLTAGE, "VDDIO"),
	MXS_ADC_CHAN(7, IIO_VOLTAGE, "VBATT"),
	/* Combined Temperature sensors */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = 8,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.channel = 8,
		.scan_type = {.sign = 'u', .realbits = 18, .storagebits = 32,},
		.datasheet_name = "TEMP_DIE",
	},
	/* Hidden channel to keep indexes */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = -1,
		.channel = 9,
	},
	MXS_ADC_CHAN(10, IIO_VOLTAGE, NULL),
	MXS_ADC_CHAN(11, IIO_VOLTAGE, NULL),
	MXS_ADC_CHAN(12, IIO_VOLTAGE, "USB_DP"),
	MXS_ADC_CHAN(13, IIO_VOLTAGE, "USB_DN"),
	MXS_ADC_CHAN(14, IIO_VOLTAGE, "VBG"),
	MXS_ADC_CHAN(15, IIO_VOLTAGE, "VDD5V"),
};

static const struct iio_chan_spec mx28_lradc_chan_spec[] = {
	MXS_ADC_CHAN(0, IIO_VOLTAGE, "LRADC0"),
	MXS_ADC_CHAN(1, IIO_VOLTAGE, "LRADC1"),
	MXS_ADC_CHAN(2, IIO_VOLTAGE, "LRADC2"),
	MXS_ADC_CHAN(3, IIO_VOLTAGE, "LRADC3"),
	MXS_ADC_CHAN(4, IIO_VOLTAGE, "LRADC4"),
	MXS_ADC_CHAN(5, IIO_VOLTAGE, "LRADC5"),
	MXS_ADC_CHAN(6, IIO_VOLTAGE, "LRADC6"),
	MXS_ADC_CHAN(7, IIO_VOLTAGE, "VBATT"),
	/* Combined Temperature sensors */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = 8,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.channel = 8,
		.scan_type = {.sign = 'u', .realbits = 18, .storagebits = 32,},
		.datasheet_name = "TEMP_DIE",
	},
	/* Hidden channel to keep indexes */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = -1,
		.channel = 9,
	},
	MXS_ADC_CHAN(10, IIO_VOLTAGE, "VDDIO"),
	MXS_ADC_CHAN(11, IIO_VOLTAGE, "VTH"),
	MXS_ADC_CHAN(12, IIO_VOLTAGE, "VDDA"),
	MXS_ADC_CHAN(13, IIO_VOLTAGE, "VDDD"),
	MXS_ADC_CHAN(14, IIO_VOLTAGE, "VBG"),
	MXS_ADC_CHAN(15, IIO_VOLTAGE, "VDD5V"),
};

static int mxs_lradc_hw_init(struct mxs_lradc *lradc)
{
	/* The ADC always uses DELAY CHANNEL 0. */
	const u32 adc_cfg =
		(1 << (LRADC_DELAY_TRIGGER_DELAYS_OFFSET + 0)) |
		(LRADC_DELAY_TIMER_PER << LRADC_DELAY_DELAY_OFFSET);

	int ret = stmp_reset_block(lradc->base);

	if (ret)
		return ret;

	/* Configure DELAY CHANNEL 0 for generic ADC sampling. */
	mxs_lradc_reg_wrt(lradc, adc_cfg, LRADC_DELAY(0));

	/* Disable remaining DELAY CHANNELs */
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(1));
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(2));
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(3));

	/* Configure the touchscreen type */
	if (lradc->soc == IMX28_LRADC) {
		mxs_lradc_reg_clear(lradc, LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE,
				    LRADC_CTRL0);

	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_5WIRE)
		mxs_lradc_reg_set(lradc, LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE,
				  LRADC_CTRL0);
	}

	/* Start internal temperature sensing. */
	mxs_lradc_reg_wrt(lradc, 0, LRADC_CTRL2);

	return 0;
}

static void mxs_lradc_hw_stop(struct mxs_lradc *lradc)
{
	int i;

	mxs_lradc_reg_clear(lradc, mxs_lradc_irq_en_mask(lradc), LRADC_CTRL1);

	for (i = 0; i < LRADC_MAX_DELAY_CHANS; i++)
		mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(i));
}

static const struct of_device_id mxs_lradc_dt_ids[] = {
	{ .compatible = "fsl,imx23-lradc", .data = (void *)IMX23_LRADC, },
	{ .compatible = "fsl,imx28-lradc", .data = (void *)IMX28_LRADC, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_lradc_dt_ids);

static int mxs_lradc_probe_touchscreen(struct mxs_lradc *lradc,
				       struct device_node *lradc_node)
{
	int ret;
	u32 ts_wires = 0, adapt;

	ret = of_property_read_u32(lradc_node, "fsl,lradc-touchscreen-wires",
				   &ts_wires);
	if (ret)
		return -ENODEV; /* touchscreen feature disabled */

	switch (ts_wires) {
	case 4:
		lradc->use_touchscreen = MXS_LRADC_TOUCHSCREEN_4WIRE;
		break;
	case 5:
		if (lradc->soc == IMX28_LRADC) {
			lradc->use_touchscreen = MXS_LRADC_TOUCHSCREEN_5WIRE;
			break;
		}
		/* fall through an error message for i.MX23 */
	default:
		dev_err(lradc->dev,
			"Unsupported number of touchscreen wires (%d)\n",
			ts_wires);
		return -EINVAL;
	}

	if (of_property_read_u32(lradc_node, "fsl,ave-ctrl", &adapt)) {
		lradc->over_sample_cnt = 4;
	} else {
		if (adapt < 1 || adapt > 32) {
			dev_err(lradc->dev, "Invalid sample count (%u)\n",
				adapt);
			return -EINVAL;
		}
		lradc->over_sample_cnt = adapt;
	}

	if (of_property_read_u32(lradc_node, "fsl,ave-delay", &adapt)) {
		lradc->over_sample_delay = 2;
	} else {
		if (adapt < 2 || adapt > LRADC_DELAY_DELAY_MASK + 1) {
			dev_err(lradc->dev, "Invalid sample delay (%u)\n",
				adapt);
			return -EINVAL;
		}
		lradc->over_sample_delay = adapt;
	}

	if (of_property_read_u32(lradc_node, "fsl,settling", &adapt)) {
		lradc->settling_delay = 10;
	} else {
		if (adapt < 1 || adapt > LRADC_DELAY_DELAY_MASK) {
			dev_err(lradc->dev, "Invalid settling delay (%u)\n",
				adapt);
			return -EINVAL;
		}
		lradc->settling_delay = adapt;
	}

	return 0;
}

static int mxs_lradc_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
		of_match_device(mxs_lradc_dt_ids, &pdev->dev);
	const struct mxs_lradc_of_config *of_cfg =
		&mxs_lradc_of_config[(enum mxs_lradc_id)of_id->data];
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct mxs_lradc *lradc;
	struct iio_dev *iio;
	struct resource *iores;
	int ret = 0, touch_ret;
	int i, s;
	u64 scale_uv;

	/* Allocate the IIO device. */
	iio = devm_iio_device_alloc(dev, sizeof(*lradc));
	if (!iio) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	lradc = iio_priv(iio);
	lradc->soc = (enum mxs_lradc_id)of_id->data;

	/* Grab the memory area */
	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lradc->dev = &pdev->dev;
	lradc->base = devm_ioremap_resource(dev, iores);
	if (IS_ERR(lradc->base))
		return PTR_ERR(lradc->base);

	lradc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lradc->clk)) {
		dev_err(dev, "Failed to get the delay unit clock\n");
		return PTR_ERR(lradc->clk);
	}
	ret = clk_prepare_enable(lradc->clk);
	if (ret != 0) {
		dev_err(dev, "Failed to enable the delay unit clock\n");
		return ret;
	}

	touch_ret = mxs_lradc_probe_touchscreen(lradc, node);

	if (touch_ret == 0)
		lradc->buffer_vchans = BUFFER_VCHANS_LIMITED;
	else
		lradc->buffer_vchans = BUFFER_VCHANS_ALL;

	/* Grab all IRQ sources */
	for (i = 0; i < of_cfg->irq_count; i++) {
		lradc->irq[i] = platform_get_irq(pdev, i);
		if (lradc->irq[i] < 0) {
			ret = lradc->irq[i];
			goto err_clk;
		}

		ret = devm_request_irq(dev, lradc->irq[i],
				       mxs_lradc_handle_irq, 0,
				       of_cfg->irq_name[i], iio);
		if (ret)
			goto err_clk;
	}

	lradc->vref_mv = of_cfg->vref_mv;

	platform_set_drvdata(pdev, iio);

	init_completion(&lradc->completion);
	mutex_init(&lradc->lock);

	iio->name = pdev->name;
	iio->dev.parent = &pdev->dev;
	iio->info = &mxs_lradc_iio_info;
	iio->modes = INDIO_DIRECT_MODE;
	iio->masklength = LRADC_MAX_TOTAL_CHANS;

	if (lradc->soc == IMX23_LRADC) {
		iio->channels = mx23_lradc_chan_spec;
		iio->num_channels = ARRAY_SIZE(mx23_lradc_chan_spec);
	} else {
		iio->channels = mx28_lradc_chan_spec;
		iio->num_channels = ARRAY_SIZE(mx28_lradc_chan_spec);
	}

	ret = iio_triggered_buffer_setup(iio, &iio_pollfunc_store_time,
					 &mxs_lradc_trigger_handler,
					 &mxs_lradc_buffer_ops);
	if (ret)
		goto err_clk;

	ret = mxs_lradc_trigger_init(iio);
	if (ret)
		goto err_trig;

	/* Populate available ADC input ranges */
	for (i = 0; i < LRADC_MAX_TOTAL_CHANS; i++) {
		for (s = 0; s < ARRAY_SIZE(lradc->scale_avail[i]); s++) {
			/*
			 * [s=0] = optional divider by two disabled (default)
			 * [s=1] = optional divider by two enabled
			 *
			 * The scale is calculated by doing:
			 *   Vref >> (realbits - s)
			 * which multiplies by two on the second component
			 * of the array.
			 */
			scale_uv = ((u64)lradc->vref_mv[i] * 100000000) >>
				   (LRADC_RESOLUTION - s);
			lradc->scale_avail[i][s].nano =
					do_div(scale_uv, 100000000) * 10;
			lradc->scale_avail[i][s].integer = scale_uv;
		}
	}

	/* Configure the hardware. */
	ret = mxs_lradc_hw_init(lradc);
	if (ret)
		goto err_dev;

	/* Register the touchscreen input device. */
	if (touch_ret == 0) {
		ret = mxs_lradc_ts_register(lradc);
		if (ret)
			goto err_ts_register;
	}

	/* Register IIO device. */
	ret = iio_device_register(iio);
	if (ret) {
		dev_err(dev, "Failed to register IIO device\n");
		goto err_ts;
	}

	return 0;

err_ts:
	mxs_lradc_ts_unregister(lradc);
err_ts_register:
	mxs_lradc_hw_stop(lradc);
err_dev:
	mxs_lradc_trigger_remove(iio);
err_trig:
	iio_triggered_buffer_cleanup(iio);
err_clk:
	clk_disable_unprepare(lradc->clk);
	return ret;
}

static int mxs_lradc_remove(struct platform_device *pdev)
{
	struct iio_dev *iio = platform_get_drvdata(pdev);
	struct mxs_lradc *lradc = iio_priv(iio);

	iio_device_unregister(iio);
	mxs_lradc_ts_unregister(lradc);
	mxs_lradc_hw_stop(lradc);
	mxs_lradc_trigger_remove(iio);
	iio_triggered_buffer_cleanup(iio);

	clk_disable_unprepare(lradc->clk);
	return 0;
}

static struct platform_driver mxs_lradc_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.of_match_table = mxs_lradc_dt_ids,
	},
	.probe	= mxs_lradc_probe,
	.remove	= mxs_lradc_remove,
};

module_platform_driver(mxs_lradc_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale MXS LRADC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
