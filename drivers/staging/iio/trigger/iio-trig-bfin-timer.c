/*
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <asm/gptimers.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

struct bfin_timer {
	unsigned short id, bit;
	unsigned long irqbit;
	int irq;
};

/*
 * this covers all hardware timer configurations on
 * all Blackfin derivatives out there today
 */

static struct bfin_timer iio_bfin_timer_code[MAX_BLACKFIN_GPTIMERS] = {
	{TIMER0_id,  TIMER0bit,  TIMER_STATUS_TIMIL0,  IRQ_TIMER0},
	{TIMER1_id,  TIMER1bit,  TIMER_STATUS_TIMIL1,  IRQ_TIMER1},
	{TIMER2_id,  TIMER2bit,  TIMER_STATUS_TIMIL2,  IRQ_TIMER2},
#if (MAX_BLACKFIN_GPTIMERS > 3)
	{TIMER3_id,  TIMER3bit,  TIMER_STATUS_TIMIL3,  IRQ_TIMER3},
	{TIMER4_id,  TIMER4bit,  TIMER_STATUS_TIMIL4,  IRQ_TIMER4},
	{TIMER5_id,  TIMER5bit,  TIMER_STATUS_TIMIL5,  IRQ_TIMER5},
	{TIMER6_id,  TIMER6bit,  TIMER_STATUS_TIMIL6,  IRQ_TIMER6},
	{TIMER7_id,  TIMER7bit,  TIMER_STATUS_TIMIL7,  IRQ_TIMER7},
#endif
#if (MAX_BLACKFIN_GPTIMERS > 8)
	{TIMER8_id,  TIMER8bit,  TIMER_STATUS_TIMIL8,  IRQ_TIMER8},
	{TIMER9_id,  TIMER9bit,  TIMER_STATUS_TIMIL9,  IRQ_TIMER9},
	{TIMER10_id, TIMER10bit, TIMER_STATUS_TIMIL10, IRQ_TIMER10},
#if (MAX_BLACKFIN_GPTIMERS > 11)
	{TIMER11_id, TIMER11bit, TIMER_STATUS_TIMIL11, IRQ_TIMER11},
#endif
#endif
};

struct bfin_tmr_state {
	struct iio_trigger *trig;
	struct bfin_timer *t;
	unsigned timer_num;
	int irq;
};

static ssize_t iio_bfin_tmr_frequency_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_trigger *trig = dev_get_drvdata(dev);
	struct bfin_tmr_state *st = trig->private_data;
	long val;
	int ret;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		goto error_ret;

	if (val > 100000) {
		ret = -EINVAL;
		goto error_ret;
	}

	disable_gptimers(st->t->bit);

	if (!val)
		goto error_ret;

	val = get_sclk() / val;
	if (val <= 4) {
		ret = -EINVAL;
		goto error_ret;
	}

	set_gptimer_period(st->t->id, val);
	set_gptimer_pwidth(st->t->id, 1);
	enable_gptimers(st->t->bit);

error_ret:
	return ret ? ret : count;
}

static ssize_t iio_bfin_tmr_frequency_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_trigger *trig = dev_get_drvdata(dev);
	struct bfin_tmr_state *st = trig->private_data;

	return sprintf(buf, "%lu\n",
			get_sclk() / get_gptimer_period(st->t->id));
}

static DEVICE_ATTR(frequency, S_IRUGO | S_IWUSR, iio_bfin_tmr_frequency_show,
		   iio_bfin_tmr_frequency_store);

static struct attribute *iio_bfin_tmr_trigger_attrs[] = {
	&dev_attr_frequency.attr,
	NULL,
};

static const struct attribute_group iio_bfin_tmr_trigger_attr_group = {
	.attrs = iio_bfin_tmr_trigger_attrs,
};

static const struct attribute_group *iio_bfin_tmr_trigger_attr_groups[] = {
	&iio_bfin_tmr_trigger_attr_group,
	NULL
};


static irqreturn_t iio_bfin_tmr_trigger_isr(int irq, void *devid)
{
	struct bfin_tmr_state *st = devid;

	clear_gptimer_intr(st->t->id);
	iio_trigger_poll(st->trig, 0);

	return IRQ_HANDLED;
}

static int iio_bfin_tmr_get_number(int irq)
{
	int i;

	for (i = 0; i < MAX_BLACKFIN_GPTIMERS; i++)
		if (iio_bfin_timer_code[i].irq == irq)
			return i;

	return -ENODEV;
}

static const struct iio_trigger_ops iio_bfin_tmr_trigger_ops = {
	.owner = THIS_MODULE,
};

static int __devinit iio_bfin_tmr_trigger_probe(struct platform_device *pdev)
{
	struct bfin_tmr_state *st;
	int ret;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	st->irq = platform_get_irq(pdev, 0);
	if (!st->irq) {
		dev_err(&pdev->dev, "No IRQs specified");
		ret = -ENODEV;
		goto out1;
	}

	ret = iio_bfin_tmr_get_number(st->irq);
	if (ret < 0)
		goto out1;

	st->timer_num = ret;
	st->t = &iio_bfin_timer_code[st->timer_num];

	st->trig = iio_trigger_alloc("bfintmr%d", st->timer_num);
	if (!st->trig) {
		ret = -ENOMEM;
		goto out1;
	}

	st->trig->private_data = st;
	st->trig->ops = &iio_bfin_tmr_trigger_ops;
	st->trig->dev.groups = iio_bfin_tmr_trigger_attr_groups;
	ret = iio_trigger_register(st->trig);
	if (ret)
		goto out2;

	ret = request_irq(st->irq, iio_bfin_tmr_trigger_isr,
			  0, st->trig->name, st);
	if (ret) {
		dev_err(&pdev->dev,
			"request IRQ-%d failed", st->irq);
		goto out4;
	}

	set_gptimer_config(st->t->id, OUT_DIS | PWM_OUT | PERIOD_CNT | IRQ_ENA);

	dev_info(&pdev->dev, "iio trigger Blackfin TMR%d, IRQ-%d",
		 st->timer_num, st->irq);
	platform_set_drvdata(pdev, st);

	return 0;
out4:
	iio_trigger_unregister(st->trig);
out2:
	iio_trigger_put(st->trig);
out1:
	kfree(st);
out:
	return ret;
}

static int __devexit iio_bfin_tmr_trigger_remove(struct platform_device *pdev)
{
	struct bfin_tmr_state *st = platform_get_drvdata(pdev);

	disable_gptimers(st->t->bit);
	free_irq(st->irq, st);
	iio_trigger_unregister(st->trig);
	iio_trigger_put(st->trig);
	kfree(st);

	return 0;
}

static struct platform_driver iio_bfin_tmr_trigger_driver = {
	.driver = {
		.name = "iio_bfin_tmr_trigger",
		.owner = THIS_MODULE,
	},
	.probe = iio_bfin_tmr_trigger_probe,
	.remove = __devexit_p(iio_bfin_tmr_trigger_remove),
};

module_platform_driver(iio_bfin_tmr_trigger_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Blackfin system timer based trigger for the iio subsystem");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:iio-trig-bfin-timer");
