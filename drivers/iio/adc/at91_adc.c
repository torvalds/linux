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

	if (idev->scan_timestamp) {
		s64 *timestamp = (s64 *)((u8 *)st->buffer +
					ALIGN(j, sizeof(s64)));
		*timestamp = pf->timestamp;
	}

	iio_push_to_buffers(idev, (u8 *)st->buffer);

	iio_trigger_notify_done(idev->trig);

	/* Needed to ACK the DRDY interruption */
	at91_adc_readl(st, AT91_ADC_LCDR);

	enable_irq(st->irq);

	return IRQ_HANDLED;
}

static irqreturn_t at91_adc_eoc_trigger(int irq, void *private)
{
	struct iio_dev *idev = private;
	struct at91_adc_state *st = iio_priv(idev);
	u32 status = at91_adc_readl(st, st->registers->status_register);

	if (!(status & st->registers->drdy_mask))
		return IRQ_HANDLED;

	if (iio_buffer_enabled(idev)) {
		disable_irq_nosync(irq);
		iio_trigger_poll(idev->trig, iio_get_time_ns());
	} else {
		st->last_value = at91_adc_readl(st, AT91_ADC_LCDR);
		st->done = true;
		wake_up_interruptible(&st->wq_data_avail);
	}

	return IRQ_HANDLED;
}

static int at91_adc_channel_init(struct iio_dev *idev)
{
	struct at91_adc_state *st = iio_priv(idev);
	struct iio_chan_spec *chan_array, *timestamp;
	int bit, idx = 0;

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
				st->trigger_number * sizeof(st->trig),
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
		*val = (st->vref_mv * 1000) >> chan->scan_type.realbits;
		*val2 = 0;
		return IIO_VAL_INT_PLUS_MICRO;
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

	st->use_external = of_property_read_bool(node, "atmel,adc-use-external-triggers");

	if (of_property_read_u32(node, "atmel,adc-channels-used", &prop)) {
		dev_err(&idev->dev, "Missing adc-channels-used property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->channels_mask = prop;

	if (of_property_read_u32(node, "atmel,adc-num-channels", &prop)) {
		dev_err(&idev->dev, "Missing adc-num-channels property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->num_channels = prop;

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

	st->registers = devm_kzalloc(&idev->dev,
				     sizeof(struct at91_adc_reg_desc),
				     GFP_KERNEL);
	if (!st->registers) {
		dev_err(&idev->dev, "Could not allocate register memory.\n");
		ret = -ENOMEM;
		goto error_ret;
	}

	if (of_property_read_u32(node, "atmel,adc-channel-base", &prop)) {
		dev_err(&idev->dev, "Missing adc-channel-base property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->registers->channel_base = prop;

	if (of_property_read_u32(node, "atmel,adc-drdy-mask", &prop)) {
		dev_err(&idev->dev, "Missing adc-drdy-mask property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->registers->drdy_mask = prop;

	if (of_property_read_u32(node, "atmel,adc-status-register", &prop)) {
		dev_err(&idev->dev, "Missing adc-status-register property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->registers->status_register = prop;

	if (of_property_read_u32(node, "atmel,adc-trigger-register", &prop)) {
		dev_err(&idev->dev, "Missing adc-trigger-register property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	st->registers->trigger_register = prop;

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

	st->use_external = pdata->use_external_triggers;
	st->vref_mv = pdata->vref;
	st->channels_mask = pdata->channels_used;
	st->num_channels = pdata->num_channels;
	st->startup_time = pdata->startup_time;
	st->trigger_number = pdata->trigger_number;
	st->trigger_list = pdata->trigger_list;
	st->registers = pdata->registers;

	return 0;
}

static const struct iio_info at91_adc_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &at91_adc_read_raw,
};

static int at91_adc_probe(struct platform_device *pdev)
{
	unsigned int prsc, mstrclk, ticks, adc_clk, shtim;
	int ret;
	struct iio_dev *idev;
	struct at91_adc_state *st;
	struct resource *res;
	u32 reg;

	idev = iio_device_alloc(sizeof(struct at91_adc_state));
	if (idev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	st = iio_priv(idev);

	if (pdev->dev.of_node)
		ret = at91_adc_probe_dt(st, pdev);
	else
		ret = at91_adc_probe_pdata(st, pdev);

	if (ret) {
		dev_err(&pdev->dev, "No platform data available.\n");
		ret = -EINVAL;
		goto error_free_device;
	}

	platform_set_drvdata(pdev, idev);

	idev->dev.parent = &pdev->dev;
	idev->name = dev_name(&pdev->dev);
	idev->modes = INDIO_DIRECT_MODE;
	idev->info = &at91_adc_info;

	st->irq = platform_get_irq(pdev, 0);
	if (st->irq < 0) {
		dev_err(&pdev->dev, "No IRQ ID is designated\n");
		ret = -ENODEV;
		goto error_free_device;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	st->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(st->reg_base)) {
		ret = PTR_ERR(st->reg_base);
		goto error_free_device;
	}

	/*
	 * Disable all IRQs before setting up the handler
	 */
	at91_adc_writel(st, AT91_ADC_CR, AT91_ADC_SWRST);
	at91_adc_writel(st, AT91_ADC_IDR, 0xFFFFFFFF);
	ret = request_irq(st->irq,
			  at91_adc_eoc_trigger,
			  0,
			  pdev->dev.driver->name,
			  idev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to allocate IRQ.\n");
		goto error_free_device;
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
	prsc = (mstrclk / (2 * adc_clk)) - 1;

	if (!st->startup_time) {
		dev_err(&pdev->dev, "No startup time available.\n");
		ret = -EINVAL;
		goto error_disable_adc_clk;
	}

	/*
	 * Number of ticks needed to cover the startup time of the ADC as
	 * defined in the electrical characteristics of the board, divided by 8.
	 * The formula thus is : Startup Time = (ticks + 1) * 8 / ADC Clock
	 */
	ticks = round_up((st->startup_time * adc_clk /
			  1000000) - 1, 8) / 8;
	/*
	 * a minimal Sample and Hold Time is necessary for the ADC to guarantee
	 * the best converted final value between two channels selection
	 * The formula thus is : Sample and Hold Time = (shtim + 1) / ADCClock
	 */
	shtim = round_up((st->sample_hold_time * adc_clk /
			  1000000) - 1, 1);

	reg = AT91_ADC_PRESCAL_(prsc) & AT91_ADC_PRESCAL;
	reg |= AT91_ADC_STARTUP_(ticks) & AT91_ADC_STARTUP;
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

	ret = at91_adc_buffer_init(idev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't initialize the buffer.\n");
		goto error_disable_adc_clk;
	}

	ret = at91_adc_trigger_init(idev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't setup the triggers.\n");
		goto error_unregister_buffer;
	}

	ret = iio_device_register(idev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto error_remove_triggers;
	}

	return 0;

error_remove_triggers:
	at91_adc_trigger_remove(idev);
error_unregister_buffer:
	at91_adc_buffer_remove(idev);
error_disable_adc_clk:
	clk_disable_unprepare(st->adc_clk);
error_disable_clk:
	clk_disable_unprepare(st->clk);
error_free_irq:
	free_irq(st->irq, idev);
error_free_device:
	iio_device_free(idev);
error_ret:
	return ret;
}

static int at91_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *idev = platform_get_drvdata(pdev);
	struct at91_adc_state *st = iio_priv(idev);

	iio_device_unregister(idev);
	at91_adc_trigger_remove(idev);
	at91_adc_buffer_remove(idev);
	clk_disable_unprepare(st->adc_clk);
	clk_disable_unprepare(st->clk);
	free_irq(st->irq, idev);
	iio_device_free(idev);

	return 0;
}

static const struct of_device_id at91_adc_dt_ids[] = {
	{ .compatible = "atmel,at91sam9260-adc" },
	{},
};
MODULE_DEVICE_TABLE(of, at91_adc_dt_ids);

static struct platform_driver at91_adc_driver = {
	.probe = at91_adc_probe,
	.remove = at91_adc_remove,
	.driver = {
		   .name = "at91_adc",
		   .of_match_table = of_match_ptr(at91_adc_dt_ids),
	},
};

module_platform_driver(at91_adc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atmel AT91 ADC Driver");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
