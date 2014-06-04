/*
 * Driver for the ADC present in the Atmel AT91 evaluation boards.
 *
 * Copyright 2011 Free Electrons
 *
 * Licensed under the GPLv2 or later.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <linux/platform_data/at91_adc.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <mach/at91_adc.h>

#define AT91_ADC_CHAN(st, ch) \
	(st->registers->channel_base + (ch * 4))
#define at91_adc_readl(st, reg) \
	(readl_relaxed(st->reg_base + reg))
#define at91_adc_writel(st, reg, val) \
	(writel_relaxed(val, st->reg_base + reg))

#define DRIVER_NAME		"at91_adc"
#define MAX_POS_BITS		12

#define TOUCH_SAMPLE_PERIOD_US		2000	/* 2ms */
#define TOUCH_PEN_DETECT_DEBOUNCE_US	200

struct at91_adc_caps {
	bool	has_ts;		/* Support touch screen */
	bool	has_tsmr;	/* only at91sam9x5, sama5d3 have TSMR reg */
	/*
	 * Numbers of sampling data will be averaged. Can be 0~3.
	 * Hardware can average (2 ^ ts_filter_average) sample data.
	 */
	u8	ts_filter_average;
	/* Pen Detection input pull-up resistor, can be 0~3 */
	u8	ts_pen_detect_sensitivity;

	/* startup time calculate function */
	u32 (*calc_startup_ticks)(u8 startup_time, u32 adc_clk_khz);

	u8	num_channels;
	struct at91_adc_reg_desc registers;
};

enum atmel_adc_ts_type {
	ATMEL_ADC_TOUCHSCREEN_NONE = 0,
	ATMEL_ADC_TOUCHSCREEN_4WIRE = 4,
	ATMEL_ADC_TOUCHSCREEN_5WIRE = 5,
};

struct at91_adc_state {
	struct clk		*adc_clk;
	u16			*buffer;
	unsigned long		channels_mask;
	struct clk		*clk;
	bool			done;
	int			irq;
	u16			last_value;
	struct mutex		lock;
	u8			num_channels;
	void __iomem		*reg_base;
	struct at91_adc_reg_desc *registers;
	u8			startup_time;
	u8			sample_hold_time;
	bool			sleep_mode;
	struct iio_trigger	**trig;
	struct at91_adc_trigger	*trigger_list;
	u32			trigger_number;
	bool			use_external;
	u32			vref_mv;
	u32			res;		/* resolution used for convertions */
	bool			low_res;	/* the resolution corresponds to the lowest one */
	wait_queue_head_t	wq_data_avail;
	struct at91_adc_caps	*caps;

	/*
	 * Following ADC channels are shared by touchscreen:
	 *
	 * CH0 -- Touch screen XP/UL
	 * CH1 -- Touch screen XM/UR
	 * CH2 -- Touch screen YP/LL
	 * CH3 -- Touch screen YM/Sense
	 * CH4 -- Touch screen LR(5-wire only)
	 *
	 * The bitfields below represents the reserved channel in the
	 * touchscreen mode.
	 */
#define CHAN_MASK_TOUCHSCREEN_4WIRE	(0xf << 0)
#define CHAN_MASK_TOUCHSCREEN_5WIRE	(0x1f << 0)
	enum atmel_adc_ts_type	touchscreen_type;
	struct input_dev	*ts_input;

	u16			ts_sample_period_val;
	u32			ts_pressure_threshold;
};

static irqreturn_t at91_adc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *idev = pf->indio_dev;
	struct at91_adc_state *st = iio_priv(idev);
	int i, j = 0;

	for (i = 0; i < idev->masklength; i++) {
		if (!test_bit(i, idev->active_scan_mask))
			continue;
		st->buffer[j] = at91_adc_readl(st, AT91_ADC_CHAN(st, i));
		j++;
	}

	iio_push_to_buffers_with_timestamp(idev, st->buffer, pf->timestamp);

	iio_trigger_notify_done(idev->trig);

	/* Needed to ACK the DRDY interruption */
	at91_adc_readl(st, AT91_ADC_LCDR);

	enable_irq(st->irq);

	return IRQ_HANDLED;
}

/* Handler for classic adc channel eoc trigger */
void handle_adc_eoc_trigger(int irq, struct iio_dev *idev)
{
	struct at91_adc_state *st = iio_priv(idev);

	if (iio_buffer_enabled(idev)) {
		disable_irq_nosync(irq);
		iio_trigger_poll(idev->trig, iio_get_time_ns());
	} else {
		st->last_value = at91_adc_readl(st, AT91_ADC_LCDR);
		st->done = true;
		wake_up_interruptible(&st->wq_data_avail);
	}
}

static int at91_ts_sample(struct at91_adc_state *st)
{
	unsigned int xscale, yscale, reg, z1, z2;
	unsigned int x, y, pres, xpos, ypos;
	unsigned int rxp = 1;
	unsigned int factor = 1000;
	struct iio_dev *idev = iio_priv_to_dev(st);

	unsigned int xyz_mask_bits = st->res;
	unsigned int xyz_mask = (1 << xyz_mask_bits) - 1;

	/* calculate position */
	/* x position = (x / xscale) * max, max = 2^MAX_POS_BITS - 1 */
	reg = at91_adc_readl(st, AT91_ADC_TSXPOSR);
	xpos = reg & xyz_mask;
	x = (xpos << MAX_POS_BITS) - xpos;
	xscale = (reg >> 16) & xyz_mask;
	if (xscale == 0) {
		dev_err(&idev->dev, "Error: xscale == 0!\n");
		return -1;
	}
	x /= xscale;

	/* y position = (y / yscale) * max, max = 2^MAX_POS_BITS - 1 */
	reg = at91_adc_readl(st, AT91_ADC_TSYPOSR);
	ypos = reg & xyz_mask;
	y = (ypos << MAX_POS_BITS) - ypos;
	yscale = (reg >> 16) & xyz_mask;
	if (yscale == 0) {
		dev_err(&idev->dev, "Error: yscale == 0!\n");
		return -1;
	}
	y /= yscale;

	/* calculate the pressure */
	reg = at91_adc_readl(st, AT91_ADC_TSPRESSR);
	z1 = reg & xyz_mask;
	z2 = (reg >> 16) & xyz_mask;

	if (z1 != 0)
		pres = rxp * (x * factor / 1024) * (z2 * factor / z1 - factor)
			/ factor;
	else
		pres = st->ts_pressure_threshold;	/* no pen contacted */

	dev_dbg(&idev->dev, "xpos = %d, xscale = %d, ypos = %d, yscale = %d, z1 = %d, z2 = %d, press = %d\n",
				xpos, xscale, ypos, yscale, z1, z2, pres);

	if (pres < st->ts_pressure_threshold) {
		dev_dbg(&idev->dev, "x = %d, y = %d, pressure = %d\n",
					x, y, pres / factor);
		input_report_abs(st->ts_input, ABS_X, x);
		input_report_abs(st->ts_input, ABS_Y, y);
		input_report_abs(st->ts_input, ABS_PRESSURE, pres);
		input_report_key(st->ts_input, BTN_TOUCH, 1);
		input_sync(st->ts_input);
	} else {
		dev_dbg(&idev->dev, "pressure too low: not reporting\n");
	}

	return 0;
}

static irqreturn_t at91_adc_interrupt(int irq, void *private)
{
	struct iio_dev *idev = private;
	struct at91_adc_state *st = iio_priv(idev);
	u32 status = at91_adc_readl(st, st->registers->status_register);
	const uint32_t ts_data_irq_mask =
		AT91_ADC_IER_XRDY |
		AT91_ADC_IER_YRDY |
		AT91_ADC_IER_PRDY;

	if (status & st->registers->drdy_mask)
		handle_adc_eoc_trigger(irq, idev);

	if (status & AT91_ADC_IER_PEN) {
		at91_adc_writel(st, AT91_ADC_IDR, AT91_ADC_IER_PEN);
		at91_adc_writel(st, AT91_ADC_IER, AT91_ADC_IER_NOPEN |
			ts_data_irq_mask);
		/* Set up period trigger for sampling */
		at91_adc_writel(st, st->registers->trigger_register,
			AT91_ADC_TRGR_MOD_PERIOD_TRIG |
			AT91_ADC_TRGR_TRGPER_(st->ts_sample_period_val));
	} else if (status & AT91_ADC_IER_NOPEN) {
		at91_adc_writel(st, st->registers->trigger_register, 0);
		at91_adc_writel(st, AT91_ADC_IDR, AT91_ADC_IER_NOPEN |
			ts_data_irq_mask);
		at91_adc_writel(st, AT91_ADC_IER, AT91_ADC_IER_PEN);

		input_report_key(st->ts_input, BTN_TOUCH, 0);
		input_sync(st->ts_input);
	} else if ((status & ts_data_irq_mask) == ts_data_irq_mask) {
		/* Now all touchscreen data is ready */

		if (status & AT91_ADC_ISR_PENS) {
			/* validate data by pen contact */
			at91_ts_sample(st);
		} else {
			/* triggered by event that is no pen contact, just read
			 * them to clean the interrupt and discard all.
			 */
			at91_adc_readl(st, AT91_ADC_TSXPOSR);
			at91_adc_readl(st, AT91_ADC_TSYPOSR);
			at91_adc_readl(st, AT91_ADC_TSPRESSR);
		}
	}

	return IRQ_HANDLED;
}

static int at91_adc_channel_init(struct iio_dev *idev)
{
	struct at91_adc_state *st = iio_priv(idev);
	struct iio_chan_spec *chan_array, *timestamp;
	int bit, idx = 0;
	unsigned long rsvd_mask = 0;

	/* If touchscreen is enable, then reserve the adc channels */
	if (st->touchscreen_type == ATMEL_ADC_TOUCHSCREEN_4WIRE)
		rsvd_mask = CHAN_MASK_TOUCHSCREEN_4WIRE;
	else if (st->touchscreen_type == ATMEL_ADC_TOUCHSCREEN_5WIRE)
		rsvd_mask = CHAN_MASK_TOUCHSCREEN_5WIRE;

	/* set up the channel mask to reserve touchscreen channels */
	st->channels_mask &= ~rsvd_mask;

	idev->num_channels = bitmap_weight(&st->channels_mask,
					   st->num_channels) + 1;

	chan_array = devm_kzalloc(&idev->dev,
				  ((idev->num_channels + 1) *
					sizeof(struct iio_chan_spec)),
				  GFP_KERNEL);

	if (!chan_array)
		return -ENOMEM;

	for_each_set_bit(bit, &st->channels_mask, st->num_channels) {
		struct iio_chan_spec *chan = chan_array + idx;

		chan->type = IIO_VOLTAGE;
		chan->indexed = 1;
		chan->channel = bit;
		chan->scan_index = idx;
		chan->scan_type.sign = 'u';
		chan->scan_type.realbits = st->res;
		chan->scan_type.storagebits = 16;
		chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE);
		chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		idx++;
	}
	timestamp = chan_array + idx;

	timestamp->type = IIO_TIMESTAMP;
	timestamp->channel = -1;
	timestamp->scan_index = idx;
	timestamp->scan_type.sign = 's';
	timestamp->scan_type.realbits = 64;
	timestamp->scan_type.storagebits = 64;

	idev->channels = chan_array;
	return idev->num_channels;
}

static u8 at91_adc_get_trigger_value_by_name(struct iio_dev *idev,
					     struct at91_adc_trigger *triggers,
					     const char *trigger_name)
{
	struct at91_adc_state *st = iio_priv(idev);
	u8 value = 0;
	int i;

	for (i = 0; i < st->trigger_number; i++) {
		char *name = kasprintf(GFP_KERNEL,
				"%s-dev%d-%s",
				idev->name,
				idev->id,
				triggers[i].name);
		if (!name)
			return -ENOMEM;

		if (strcmp(trigger_name, name) == 0) {
			value = triggers[i].value;
			kfree(name);
			break;
		}

		kfree(name);
	}

	return value;
}

static int at91_adc_configure_trigger(struct iio_trigger *trig, bool state)
{
	struct iio_dev *idev = iio_trigger_get_drvdata(trig);
	struct at91_adc_state *st = iio_priv(idev);
	struct iio_buffer *buffer = idev->buffer;
	struct at91_adc_reg_desc *reg = st->registers;
	u32 status = at91_adc_readl(st, reg->trigger_register);
	u8 value;
	u8 bit;

	value = at91_adc_get_trigger_value_by_name(idev,
						   st->trigger_list,
						   idev->trig->name);
	if (value == 0)
		return -EINVAL;

	if (state) {
		st->buffer = kmalloc(idev->scan_bytes, GFP_KERNEL);
		if (st->buffer == NULL)
			return -ENOMEM;

		at91_adc_writel(st, reg->trigger_register,
				status | value);

		for_each_set_bit(bit, buffer->scan_mask,
				 st->num_channels) {
			struct iio_chan_spec const *chan = idev->channels + bit;
			at91_adc_writel(st, AT91_ADC_CHER,
					AT91_ADC_CH(chan->channel));
		}

		at91_adc_writel(st, AT91_ADC_IER, reg->drdy_mask);

	} else {
		at91_adc_writel(st, AT91_ADC_IDR, reg->drdy_mask);

		at91_adc_writel(st, reg->trigger_register,
				status & ~value);

		for_each_set_bit(bit, buffer->scan_mask,
				 st->num_channels) {
			struct iio_chan_spec const *chan = idev->channels + bit;
			at91_adc_writel(st, AT91_ADC_CHDR,
					AT91_ADC_CH(chan->channel));
		}
		kfree(st->buffer);
	}

	return 0;
}

static const struct iio_trigger_ops at91_adc_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &at91_adc_configure_trigger,
};

static struct iio_trigger *at91_adc_allocate_trigger(struct iio_dev *idev,
						     struct at91_adc_trigger *trigger)
{
	struct iio_trigger *trig;
	int ret;

	trig = iio_trigger_alloc("%s-dev%d-%s", idev->name,
				 idev->id, trigger->name);
	if (trig == NULL)
		return NULL;

	trig->dev.parent = idev->dev.parent;
	iio_trigger_set_drvdata(trig, idev);
	trig->ops = &at91_adc_trigger_ops;

	ret = iio_trigger_register(trig);
	if (ret)
		return NULL;

	return trig;
}

static int at91_adc_trigger_init(struct iio_dev *idev)
{
	struct at91_adc_state *st = iio_priv(idev);
	int i, ret;

	st->trig = devm_kzalloc(&idev->dev,
				st->trigger_number * sizeof(*st->trig),
				GFP_KERNEL);

	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	for (i = 0; i < st->trigger_number; i++) {
		if (st->trigger_list[i].is_external && !(st->use_external))
			continue;

		st->trig[i] = at91_adc_allocate_trigger(idev,
							st->trigger_list + i);
		if (st->trig[i] == NULL) {
			dev_err(&idev->dev,
				"Could not allocate trigger %d\n", i);
			ret = -ENOMEM;
			goto error_trigger;
		}
	}

	return 0;

error_trigger:
	for (i--; i >= 0; i--) {
		iio_trigger_unregister(st->trig[i]);
		iio_trigger_free(st->trig[i]);
	}
error_ret:
	return ret;
}

static void at91_adc_trigger_remove(struct iio_dev *idev)
{
	struct at91_adc_state *st = iio_priv(idev);
	int i;

	for (i = 0; i < st->trigger_number; i++) {
		iio_trigger_unregister(st->trig[i]);
		iio_trigger_free(st->trig[i]);
	}
}

static int at91_adc_buffer_init(struct iio_dev *idev)
{
	return iio_triggered_buffer_setup(idev, &iio_pollfunc_store_time,
		&at91_adc_trigger_handler, NULL);
}

static void at91_adc_buffer_remove(struct iio_dev *idev)
{
	iio_triggered_buffer_cleanup(idev);
}

static int at91_adc_read_raw(struct iio_dev *idev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct at91_adc_state *st = iio_priv(idev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);

		at91_adc_writel(st, AT91_ADC_CHER,
				AT91_ADC_CH(chan->channel));
		at91_adc_writel(st, AT91_ADC_IER, st->registers->drdy_mask);
		at91_adc_writel(st, AT91_ADC_CR, AT91_ADC_START);

		ret = wait_event_interruptible_timeout(st->wq_data_avail,
						       st->done,
						       msecs_to_jiffies(1000));
		if (ret == 0)
			ret = -ETIMEDOUT;
		if (ret < 0) {
			mutex_unlock(&st->lock);
			return ret;
		}

		*val = st->last_value;

		at91_adc_writel(st, AT91_ADC_CHDR,
				AT91_ADC_CH(chan->channel));
		at91_adc_writel(st, AT91_ADC_IDR, st->registers->drdy_mask);

		st->last_value = 0;
		st->done = false;
		mutex_unlock(&st->lock);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		break;
	}
	return -EINVAL;
}

static int at91_adc_of_get_resolution(struct at91_adc_state *st,
				      struct platform_device *pdev)
{
	struct iio_dev *idev = iio_priv_to_dev(st);
	struct device_node *np = pdev->dev.of_node;
	int count, i, ret = 0;
	char *res_name, *s;
	u32 *resolutions;

	count = of_property_count_strings(np, "atmel,adc-res-names");
	if (count < 2) {
		dev_err(&idev->dev, "You must specified at least two resolution names for "
				    "adc-res-names property in the DT\n");
		return count;
	}

	resolutions = kmalloc(count * sizeof(*resolutions), GFP_KERNEL);
	if (!resolutions)
		return -ENOMEM;

	if (of_property_read_u32_array(np, "atmel,adc-res", resolutions, count)) {
		dev_err(&idev->dev, "Missing adc-res property in the DT.\n");
		ret = -ENODEV;
		goto ret;
	}

	if (of_property_read_string(np, "atmel,adc-use-res", (const char **)&res_name))
		res_name = "highres";

	for (i = 0; i < count; i++) {
		if (of_property_read_string_index(np, "atmel,adc-res-names", i, (const char **)&s))
			continue;

		if (strcmp(res_name, s))
			continue;

		st->res = resolutions[i];
		if (!strcmp(res_name, "lowres"))
			st->low_res = true;
		else
			st->low_res = false;

		dev_info(&idev->dev, "Resolution used: %u bits\n", st->res);
		goto ret;
	}

	dev_err(&idev->dev, "There is no resolution for %s\n", res_name);

ret:
	kfree(resolutions);
	return ret;
}

static u32 calc_startup_ticks_9260(u8 startup_time, u32 adc_clk_khz)
{
	/*
	 * Number of ticks needed to cover the startup time of the ADC
	 * as defined in the electrical characteristics of the board,
	 * divided by 8. The formula thus is :
	 *   Startup Time = (ticks + 1) * 8 / ADC Clock
	 */
	return round_up((startup_time * adc_clk_khz / 1000) - 1, 8) / 8;
}

static u32 calc_startup_ticks_9x5(u8 startup_time, u32 adc_clk_khz)
{
	/*
	 * For sama5d3x and at91sam9x5, the formula changes to:
	 * Startup Time = <lookup_table_value> / ADC Clock
	 */
	const int startup_lookup[] = {
		0  , 8  , 16 , 24 ,
		64 , 80 , 96 , 112,
		512, 576, 640, 704,
		768, 832, 896, 960
		};
	int i, size = ARRAY_SIZE(startup_lookup);
	unsigned int ticks;

	ticks = startup_time * adc_clk_khz / 1000;
	for (i = 0; i < size; i++)
		if (ticks < startup_lookup[i])
			break;

	ticks = i;
	if (ticks == size)
		/* Reach the end of lookup table */
		ticks = size - 1;

	return ticks;
}

static const struct of_device_id at91_adc_dt_ids[];

static int at91_adc_probe_dt_ts(struct device_node *node,
	struct at91_adc_state *st, struct device *dev)
{
	int ret;
	u32 prop;

	ret = of_property_read_u32(node, "atmel,adc-ts-wires", &prop);
	if (ret) {
		dev_info(dev, "ADC Touch screen is disabled.\n");
		return 0;
	}

	switch (prop) {
	case 4:
	case 5:
		st->touchscreen_type = prop;
		break;
	default:
		dev_err(dev, "Unsupported number of touchscreen wires (%d). Should be 4 or 5.\n", prop);
		return -EINVAL;
	}

	prop = 0;
	of_property_read_u32(node, "atmel,adc-ts-pressure-threshold", &prop);
	st->ts_pressure_threshold = prop;
	if (st->ts_pressure_threshold) {
		return 0;
	} else {
		dev_err(dev, "Invalid pressure threshold for the touchscreen\n");
		return -EINVAL;
	}
}

static int at91_adc_probe_dt(struct at91_adc_state *st,
			     struct platform_device *pdev)
{
	struct iio_dev *idev = iio_priv_to_dev(st);
	struct device_node *node = pdev->dev.of_node;
	struct device_node *trig_node;
	int i = 0, ret;
	u32 prop;

	if (!node)
		return -EINVAL;

	st->caps = (struct at91_adc_caps *)
		of_match_device(at91_adc_dt_ids, &pdev->dev)->data;

	st->use_external = of_property_read_bool(node, "atmel,adc-use-external-triggers");

	if (of_property_read_u32(node, "atmel,adc-channels-used", &prop)) {
		dev_err(&idev->dev, "Missing adc-channels-used property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->channels_mask = prop;

	st->sleep_mode = of_property_read_bool(node, "atmel,adc-sleep-mode");

	if (of_property_read_u32(node, "atmel,adc-startup-time", &prop)) {
		dev_err(&idev->dev, "Missing adc-startup-time property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->startup_time = prop;

	prop = 0;
	of_property_read_u32(node, "atmel,adc-sample-hold-time", &prop);
	st->sample_hold_time = prop;

	if (of_property_read_u32(node, "atmel,adc-vref", &prop)) {
		dev_err(&idev->dev, "Missing adc-vref property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->vref_mv = prop;

	ret = at91_adc_of_get_resolution(st, pdev);
	if (ret)
		goto error_ret;

	st->registers = &st->caps->registers;
	st->num_channels = st->caps->num_channels;
	st->trigger_number = of_get_child_count(node);
	st->trigger_list = devm_kzalloc(&idev->dev, st->trigger_number *
					sizeof(struct at91_adc_trigger),
					GFP_KERNEL);
	if (!st->trigger_list) {
		dev_err(&idev->dev, "Could not allocate trigger list memory.\n");
		ret = -ENOMEM;
		goto error_ret;
	}

	for_each_child_of_node(node, trig_node) {
		struct at91_adc_trigger *trig = st->trigger_list + i;
		const char *name;

		if (of_property_read_string(trig_node, "trigger-name", &name)) {
			dev_err(&idev->dev, "Missing trigger-name property in the DT.\n");
			ret = -EINVAL;
			goto error_ret;
		}
	        trig->name = name;

		if (of_property_read_u32(trig_node, "trigger-value", &prop)) {
			dev_err(&idev->dev, "Missing trigger-value property in the DT.\n");
			ret = -EINVAL;
			goto error_ret;
		}
	        trig->value = prop;
		trig->is_external = of_property_read_bool(trig_node, "trigger-external");
		i++;
	}

	/* Check if touchscreen is supported. */
	if (st->caps->has_ts)
		return at91_adc_probe_dt_ts(node, st, &idev->dev);
	else
		dev_info(&idev->dev, "not support touchscreen in the adc compatible string.\n");

	return 0;

error_ret:
	return ret;
}

static int at91_adc_probe_pdata(struct at91_adc_state *st,
				struct platform_device *pdev)
{
	struct at91_adc_data *pdata = pdev->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	st->caps = (struct at91_adc_caps *)
			platform_get_device_id(pdev)->driver_data;

	st->use_external = pdata->use_external_triggers;
	st->vref_mv = pdata->vref;
	st->channels_mask = pdata->channels_used;
	st->num_channels = st->caps->num_channels;
	st->startup_time = pdata->startup_time;
	st->trigger_number = pdata->trigger_number;
	st->trigger_list = pdata->trigger_list;
	st->registers = &st->caps->registers;

	return 0;
}

static const struct iio_info at91_adc_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &at91_adc_read_raw,
};

/* Touchscreen related functions */
static int atmel_ts_open(struct input_dev *dev)
{
	struct at91_adc_state *st = input_get_drvdata(dev);

	at91_adc_writel(st, AT91_ADC_IER, AT91_ADC_IER_PEN);
	return 0;
}

static void atmel_ts_close(struct input_dev *dev)
{
	struct at91_adc_state *st = input_get_drvdata(dev);

	at91_adc_writel(st, AT91_ADC_IDR, AT91_ADC_IER_PEN);
}

static int at91_ts_hw_init(struct at91_adc_state *st, u32 adc_clk_khz)
{
	u32 reg = 0, pendbc;
	int i = 0;

	if (st->touchscreen_type == ATMEL_ADC_TOUCHSCREEN_4WIRE)
		reg = AT91_ADC_TSMR_TSMODE_4WIRE_PRESS;
	else
		reg = AT91_ADC_TSMR_TSMODE_5WIRE;

	/* a Pen Detect Debounce Time is necessary for the ADC Touch to avoid
	 * pen detect noise.
	 * The formula is : Pen Detect Debounce Time = (2 ^ pendbc) / ADCClock
	 */
	pendbc = round_up(TOUCH_PEN_DETECT_DEBOUNCE_US * adc_clk_khz / 1000, 1);

	while (pendbc >> ++i)
		;	/* Empty! Find the shift offset */
	if (abs(pendbc - (1 << i)) < abs(pendbc - (1 << (i - 1))))
		pendbc = i;
	else
		pendbc = i - 1;

	if (st->caps->has_tsmr) {
		reg |= AT91_ADC_TSMR_TSAV_(st->caps->ts_filter_average)
				& AT91_ADC_TSMR_TSAV;
		reg |= AT91_ADC_TSMR_PENDBC_(pendbc) & AT91_ADC_TSMR_PENDBC;
		reg |= AT91_ADC_TSMR_NOTSDMA;
		reg |= AT91_ADC_TSMR_PENDET_ENA;
		reg |= 0x03 << 8;	/* TSFREQ, need bigger than TSAV */

		at91_adc_writel(st, AT91_ADC_TSMR, reg);
	} else {
		/* TODO: for 9g45 which has no TSMR */
	}

	/* Change adc internal resistor value for better pen detection,
	 * default value is 100 kOhm.
	 * 0 = 200 kOhm, 1 = 150 kOhm, 2 = 100 kOhm, 3 = 50 kOhm
	 * option only available on ES2 and higher
	 */
	at91_adc_writel(st, AT91_ADC_ACR, st->caps->ts_pen_detect_sensitivity
			& AT91_ADC_ACR_PENDETSENS);

	/* Sample Peroid Time = (TRGPER + 1) / ADCClock */
	st->ts_sample_period_val = round_up((TOUCH_SAMPLE_PERIOD_US *
			adc_clk_khz / 1000) - 1, 1);

	return 0;
}

static int at91_ts_register(struct at91_adc_state *st,
		struct platform_device *pdev)
{
	struct input_dev *input;
	struct iio_dev *idev = iio_priv_to_dev(st);
	int ret;

	input = input_allocate_device();
	if (!input) {
		dev_err(&idev->dev, "Failed to allocate TS device!\n");
		return -ENOMEM;
	}

	input->name = DRIVER_NAME;
	input->id.bustype = BUS_HOST;
	input->dev.parent = &pdev->dev;
	input->open = atmel_ts_open;
	input->close = atmel_ts_close;

	__set_bit(EV_ABS, input->evbit);
	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	input_set_abs_params(input, ABS_X, 0, (1 << MAX_POS_BITS) - 1, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, (1 << MAX_POS_BITS) - 1, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 0xffffff, 0, 0);

	st->ts_input = input;
	input_set_drvdata(input, st);

	ret = input_register_device(input);
	if (ret)
		input_free_device(st->ts_input);

	return ret;
}

static void at91_ts_unregister(struct at91_adc_state *st)
{
	input_unregister_device(st->ts_input);
}

static int at91_adc_probe(struct platform_device *pdev)
{
	unsigned int prsc, mstrclk, ticks, adc_clk, adc_clk_khz, shtim;
	int ret;
	struct iio_dev *idev;
	struct at91_adc_state *st;
	struct resource *res;
	u32 reg;

	idev = devm_iio_device_alloc(&pdev->dev, sizeof(struct at91_adc_state));
	if (!idev)
		return -ENOMEM;

	st = iio_priv(idev);

	if (pdev->dev.of_node)
		ret = at91_adc_probe_dt(st, pdev);
	else
		ret = at91_adc_probe_pdata(st, pdev);

	if (ret) {
		dev_err(&pdev->dev, "No platform data available.\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, idev);

	idev->dev.parent = &pdev->dev;
	idev->name = dev_name(&pdev->dev);
	idev->modes = INDIO_DIRECT_MODE;
	idev->info = &at91_adc_info;

	st->irq = platform_get_irq(pdev, 0);
	if (st->irq < 0) {
		dev_err(&pdev->dev, "No IRQ ID is designated\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	st->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(st->reg_base)) {
		return PTR_ERR(st->reg_base);
	}

	/*
	 * Disable all IRQs before setting up the handler
	 */
	at91_adc_writel(st, AT91_ADC_CR, AT91_ADC_SWRST);
	at91_adc_writel(st, AT91_ADC_IDR, 0xFFFFFFFF);
	ret = request_irq(st->irq,
			  at91_adc_interrupt,
			  0,
			  pdev->dev.driver->name,
			  idev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to allocate IRQ.\n");
		return ret;
	}

	st->clk = devm_clk_get(&pdev->dev, "adc_clk");
	if (IS_ERR(st->clk)) {
		dev_err(&pdev->dev, "Failed to get the clock.\n");
		ret = PTR_ERR(st->clk);
		goto error_free_irq;
	}

	ret = clk_prepare_enable(st->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Could not prepare or enable the clock.\n");
		goto error_free_irq;
	}

	st->adc_clk = devm_clk_get(&pdev->dev, "adc_op_clk");
	if (IS_ERR(st->adc_clk)) {
		dev_err(&pdev->dev, "Failed to get the ADC clock.\n");
		ret = PTR_ERR(st->adc_clk);
		goto error_disable_clk;
	}

	ret = clk_prepare_enable(st->adc_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Could not prepare or enable the ADC clock.\n");
		goto error_disable_clk;
	}

	/*
	 * Prescaler rate computation using the formula from the Atmel's
	 * datasheet : ADC Clock = MCK / ((Prescaler + 1) * 2), ADC Clock being
	 * specified by the electrical characteristics of the board.
	 */
	mstrclk = clk_get_rate(st->clk);
	adc_clk = clk_get_rate(st->adc_clk);
	adc_clk_khz = adc_clk / 1000;

	dev_dbg(&pdev->dev, "Master clock is set as: %d Hz, adc_clk should set as: %d Hz\n",
		mstrclk, adc_clk);

	prsc = (mstrclk / (2 * adc_clk)) - 1;

	if (!st->startup_time) {
		dev_err(&pdev->dev, "No startup time available.\n");
		ret = -EINVAL;
		goto error_disable_adc_clk;
	}
	ticks = (*st->caps->calc_startup_ticks)(st->startup_time, adc_clk_khz);

	/*
	 * a minimal Sample and Hold Time is necessary for the ADC to guarantee
	 * the best converted final value between two channels selection
	 * The formula thus is : Sample and Hold Time = (shtim + 1) / ADCClock
	 */
	if (st->sample_hold_time > 0)
		shtim = round_up((st->sample_hold_time * adc_clk_khz / 1000)
				 - 1, 1);
	else
		shtim = 0;

	reg = AT91_ADC_PRESCAL_(prsc) & st->registers->mr_prescal_mask;
	reg |= AT91_ADC_STARTUP_(ticks) & st->registers->mr_startup_mask;
	if (st->low_res)
		reg |= AT91_ADC_LOWRES;
	if (st->sleep_mode)
		reg |= AT91_ADC_SLEEP;
	reg |= AT91_ADC_SHTIM_(shtim) & AT91_ADC_SHTIM;
	at91_adc_writel(st, AT91_ADC_MR, reg);

	/* Setup the ADC channels available on the board */
	ret = at91_adc_channel_init(idev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't initialize the channels.\n");
		goto error_disable_adc_clk;
	}

	init_waitqueue_head(&st->wq_data_avail);
	mutex_init(&st->lock);

	/*
	 * Since touch screen will set trigger register as period trigger. So
	 * when touch screen is enabled, then we have to disable hardware
	 * trigger for classic adc.
	 */
	if (!st->touchscreen_type) {
		ret = at91_adc_buffer_init(idev);
		if (ret < 0) {
			dev_err(&pdev->dev, "Couldn't initialize the buffer.\n");
			goto error_disable_adc_clk;
		}

		ret = at91_adc_trigger_init(idev);
		if (ret < 0) {
			dev_err(&pdev->dev, "Couldn't setup the triggers.\n");
			at91_adc_buffer_remove(idev);
			goto error_disable_adc_clk;
		}
	} else {
		if (!st->caps->has_tsmr) {
			dev_err(&pdev->dev, "We don't support non-TSMR adc\n");
			ret = -ENODEV;
			goto error_disable_adc_clk;
		}

		ret = at91_ts_register(st, pdev);
		if (ret)
			goto error_disable_adc_clk;

		at91_ts_hw_init(st, adc_clk_khz);
	}

	ret = iio_device_register(idev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto error_iio_device_register;
	}

	return 0;

error_iio_device_register:
	if (!st->touchscreen_type) {
		at91_adc_trigger_remove(idev);
		at91_adc_buffer_remove(idev);
	} else {
		at91_ts_unregister(st);
	}
error_disable_adc_clk:
	clk_disable_unprepare(st->adc_clk);
error_disable_clk:
	clk_disable_unprepare(st->clk);
error_free_irq:
	free_irq(st->irq, idev);
	return ret;
}

static int at91_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *idev = platform_get_drvdata(pdev);
	struct at91_adc_state *st = iio_priv(idev);

	iio_device_unregister(idev);
	if (!st->touchscreen_type) {
		at91_adc_trigger_remove(idev);
		at91_adc_buffer_remove(idev);
	} else {
		at91_ts_unregister(st);
	}
	clk_disable_unprepare(st->adc_clk);
	clk_disable_unprepare(st->clk);
	free_irq(st->irq, idev);

	return 0;
}

static struct at91_adc_caps at91sam9260_caps = {
	.calc_startup_ticks = calc_startup_ticks_9260,
	.num_channels = 4,
	.registers = {
		.channel_base = AT91_ADC_CHR(0),
		.drdy_mask = AT91_ADC_DRDY,
		.status_register = AT91_ADC_SR,
		.trigger_register = AT91_ADC_TRGR_9260,
		.mr_prescal_mask = AT91_ADC_PRESCAL_9260,
		.mr_startup_mask = AT91_ADC_STARTUP_9260,
	},
};

static struct at91_adc_caps at91sam9g45_caps = {
	.has_ts = true,
	.calc_startup_ticks = calc_startup_ticks_9260,	/* same as 9260 */
	.num_channels = 8,
	.registers = {
		.channel_base = AT91_ADC_CHR(0),
		.drdy_mask = AT91_ADC_DRDY,
		.status_register = AT91_ADC_SR,
		.trigger_register = AT91_ADC_TRGR_9G45,
		.mr_prescal_mask = AT91_ADC_PRESCAL_9G45,
		.mr_startup_mask = AT91_ADC_STARTUP_9G45,
	},
};

static struct at91_adc_caps at91sam9x5_caps = {
	.has_ts = true,
	.has_tsmr = true,
	.ts_filter_average = 3,
	.ts_pen_detect_sensitivity = 2,
	.calc_startup_ticks = calc_startup_ticks_9x5,
	.num_channels = 12,
	.registers = {
		.channel_base = AT91_ADC_CDR0_9X5,
		.drdy_mask = AT91_ADC_SR_DRDY_9X5,
		.status_register = AT91_ADC_SR_9X5,
		.trigger_register = AT91_ADC_TRGR_9X5,
		/* prescal mask is same as 9G45 */
		.mr_prescal_mask = AT91_ADC_PRESCAL_9G45,
		.mr_startup_mask = AT91_ADC_STARTUP_9X5,
	},
};

static const struct of_device_id at91_adc_dt_ids[] = {
	{ .compatible = "atmel,at91sam9260-adc", .data = &at91sam9260_caps },
	{ .compatible = "atmel,at91sam9g45-adc", .data = &at91sam9g45_caps },
	{ .compatible = "atmel,at91sam9x5-adc", .data = &at91sam9x5_caps },
	{},
};
MODULE_DEVICE_TABLE(of, at91_adc_dt_ids);

static const struct platform_device_id at91_adc_ids[] = {
	{
		.name = "at91sam9260-adc",
		.driver_data = (unsigned long)&at91sam9260_caps,
	}, {
		.name = "at91sam9g45-adc",
		.driver_data = (unsigned long)&at91sam9g45_caps,
	}, {
		.name = "at91sam9x5-adc",
		.driver_data = (unsigned long)&at91sam9x5_caps,
	}, {
		/* terminator */
	}
};
MODULE_DEVICE_TABLE(platform, at91_adc_ids);

static struct platform_driver at91_adc_driver = {
	.probe = at91_adc_probe,
	.remove = at91_adc_remove,
	.id_table = at91_adc_ids,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = of_match_ptr(at91_adc_dt_ids),
	},
};

module_platform_driver(at91_adc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atmel AT91 ADC Driver");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
