/*
 * Touchscreen driver for Dialog Semiconductor DA9034
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 *	Fengwei Yin <fengwei.yin@marvell.com>
 *	Bin Yang  <bin.yang@marvell.com>
 *	Eric Miao <eric.miao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/mfd/da903x.h>
#include <linux/slab.h>

#define DA9034_MANUAL_CTRL	0x50
#define DA9034_LDO_ADC_EN	(1 << 4)

#define DA9034_AUTO_CTRL1	0x51

#define DA9034_AUTO_CTRL2	0x52
#define DA9034_AUTO_TSI_EN	(1 << 3)
#define DA9034_PEN_DETECT	(1 << 4)

#define DA9034_TSI_CTRL1	0x53
#define DA9034_TSI_CTRL2	0x54
#define DA9034_TSI_X_MSB	0x6c
#define DA9034_TSI_Y_MSB	0x6d
#define DA9034_TSI_XY_LSB	0x6e

enum {
	STATE_IDLE,	/* wait for pendown */
	STATE_BUSY,	/* TSI busy sampling */
	STATE_STOP,	/* sample available */
	STATE_WAIT,	/* Wait to start next sample */
};

enum {
	EVENT_PEN_DOWN,
	EVENT_PEN_UP,
	EVENT_TSI_READY,
	EVENT_TIMEDOUT,
};

struct da9034_touch {
	struct device		*da9034_dev;
	struct input_dev	*input_dev;

	struct delayed_work	tsi_work;
	struct notifier_block	notifier;

	int	state;

	int	interval_ms;
	int	x_inverted;
	int	y_inverted;

	int	last_x;
	int	last_y;
};

static inline int is_pen_down(struct da9034_touch *touch)
{
	return da903x_query_status(touch->da9034_dev, DA9034_STATUS_PEN_DOWN);
}

static inline int detect_pen_down(struct da9034_touch *touch, int on)
{
	if (on)
		return da903x_set_bits(touch->da9034_dev,
				DA9034_AUTO_CTRL2, DA9034_PEN_DETECT);
	else
		return da903x_clr_bits(touch->da9034_dev,
				DA9034_AUTO_CTRL2, DA9034_PEN_DETECT);
}

static int read_tsi(struct da9034_touch *touch)
{
	uint8_t _x, _y, _v;
	int ret;

	ret = da903x_read(touch->da9034_dev, DA9034_TSI_X_MSB, &_x);
	if (ret)
		return ret;

	ret = da903x_read(touch->da9034_dev, DA9034_TSI_Y_MSB, &_y);
	if (ret)
		return ret;

	ret = da903x_read(touch->da9034_dev, DA9034_TSI_XY_LSB, &_v);
	if (ret)
		return ret;

	touch->last_x = ((_x << 2) & 0x3fc) | (_v & 0x3);
	touch->last_y = ((_y << 2) & 0x3fc) | ((_v & 0xc) >> 2);

	return 0;
}

static inline int start_tsi(struct da9034_touch *touch)
{
	return da903x_set_bits(touch->da9034_dev,
			DA9034_AUTO_CTRL2, DA9034_AUTO_TSI_EN);
}

static inline int stop_tsi(struct da9034_touch *touch)
{
	return da903x_clr_bits(touch->da9034_dev,
			DA9034_AUTO_CTRL2, DA9034_AUTO_TSI_EN);
}

static inline void report_pen_down(struct da9034_touch *touch)
{
	int x = touch->last_x;
	int y = touch->last_y;

	x &= 0xfff;
	if (touch->x_inverted)
		x = 1024 - x;
	y &= 0xfff;
	if (touch->y_inverted)
		y = 1024 - y;

	input_report_abs(touch->input_dev, ABS_X, x);
	input_report_abs(touch->input_dev, ABS_Y, y);
	input_report_key(touch->input_dev, BTN_TOUCH, 1);

	input_sync(touch->input_dev);
}

static inline void report_pen_up(struct da9034_touch *touch)
{
	input_report_key(touch->input_dev, BTN_TOUCH, 0);
	input_sync(touch->input_dev);
}

static void da9034_event_handler(struct da9034_touch *touch, int event)
{
	int err;

	switch (touch->state) {
	case STATE_IDLE:
		if (event != EVENT_PEN_DOWN)
			break;

		/* Enable auto measurement of the TSI, this will
		 * automatically disable pen down detection
		 */
		err = start_tsi(touch);
		if (err)
			goto err_reset;

		touch->state = STATE_BUSY;
		break;

	case STATE_BUSY:
		if (event != EVENT_TSI_READY)
			break;

		err = read_tsi(touch);
		if (err)
			goto err_reset;

		/* Disable auto measurement of the TSI, so that
		 * pen down status will be available
		 */
		err = stop_tsi(touch);
		if (err)
			goto err_reset;

		touch->state = STATE_STOP;

		/* FIXME: PEN_{UP/DOWN} events are expected to be
		 * available by stopping TSI, but this is found not
		 * always true, delay and simulate such an event
		 * here is more reliable
		 */
		mdelay(1);
		da9034_event_handler(touch,
				     is_pen_down(touch) ? EVENT_PEN_DOWN :
							  EVENT_PEN_UP);
		break;

	case STATE_STOP:
		if (event == EVENT_PEN_DOWN) {
			report_pen_down(touch);
			schedule_delayed_work(&touch->tsi_work,
				msecs_to_jiffies(touch->interval_ms));
			touch->state = STATE_WAIT;
		}

		if (event == EVENT_PEN_UP) {
			report_pen_up(touch);
			touch->state = STATE_IDLE;
		}
		break;

	case STATE_WAIT:
		if (event != EVENT_TIMEDOUT)
			break;

		if (is_pen_down(touch)) {
			start_tsi(touch);
			touch->state = STATE_BUSY;
		} else {
			report_pen_up(touch);
			touch->state = STATE_IDLE;
		}
		break;
	}
	return;

err_reset:
	touch->state = STATE_IDLE;
	stop_tsi(touch);
	detect_pen_down(touch, 1);
}

static void da9034_tsi_work(struct work_struct *work)
{
	struct da9034_touch *touch =
		container_of(work, struct da9034_touch, tsi_work.work);

	da9034_event_handler(touch, EVENT_TIMEDOUT);
}

static int da9034_touch_notifier(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct da9034_touch *touch =
		container_of(nb, struct da9034_touch, notifier);

	if (event & DA9034_EVENT_TSI_READY)
		da9034_event_handler(touch, EVENT_TSI_READY);

	if ((event & DA9034_EVENT_PEN_DOWN) && touch->state == STATE_IDLE)
		da9034_event_handler(touch, EVENT_PEN_DOWN);

	return 0;
}

static int da9034_touch_open(struct input_dev *dev)
{
	struct da9034_touch *touch = input_get_drvdata(dev);
	int ret;

	ret = da903x_register_notifier(touch->da9034_dev, &touch->notifier,
			DA9034_EVENT_PEN_DOWN | DA9034_EVENT_TSI_READY);
	if (ret)
		return -EBUSY;

	/* Enable ADC LDO */
	ret = da903x_set_bits(touch->da9034_dev,
			DA9034_MANUAL_CTRL, DA9034_LDO_ADC_EN);
	if (ret)
		return ret;

	/* TSI_DELAY: 3 slots, TSI_SKIP: 3 slots */
	ret = da903x_write(touch->da9034_dev, DA9034_TSI_CTRL1, 0x1b);
	if (ret)
		return ret;

	ret = da903x_write(touch->da9034_dev, DA9034_TSI_CTRL2, 0x00);
	if (ret)
		return ret;

	touch->state = STATE_IDLE;
	detect_pen_down(touch, 1);

	return 0;
}

static void da9034_touch_close(struct input_dev *dev)
{
	struct da9034_touch *touch = input_get_drvdata(dev);

	da903x_unregister_notifier(touch->da9034_dev, &touch->notifier,
			DA9034_EVENT_PEN_DOWN | DA9034_EVENT_TSI_READY);

	cancel_delayed_work_sync(&touch->tsi_work);

	touch->state = STATE_IDLE;
	stop_tsi(touch);
	detect_pen_down(touch, 0);

	/* Disable ADC LDO */
	da903x_clr_bits(touch->da9034_dev,
			DA9034_MANUAL_CTRL, DA9034_LDO_ADC_EN);
}


static int da9034_touch_probe(struct platform_device *pdev)
{
	struct da9034_touch_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct da9034_touch *touch;
	struct input_dev *input_dev;
	int ret;

	touch = kzalloc(sizeof(struct da9034_touch), GFP_KERNEL);
	if (touch == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	touch->da9034_dev = pdev->dev.parent;

	if (pdata) {
		touch->interval_ms	= pdata->interval_ms;
		touch->x_inverted	= pdata->x_inverted;
		touch->y_inverted	= pdata->y_inverted;
	} else
		/* fallback into default */
		touch->interval_ms	= 10;

	INIT_DELAYED_WORK(&touch->tsi_work, da9034_tsi_work);
	touch->notifier.notifier_call = da9034_touch_notifier;

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		ret = -ENOMEM;
		goto err_free_touch;
	}

	input_dev->name		= pdev->name;
	input_dev->open		= da9034_touch_open;
	input_dev->close	= da9034_touch_close;
	input_dev->dev.parent	= &pdev->dev;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(ABS_X, input_dev->absbit);
	__set_bit(ABS_Y, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_X, 0, 1023, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 1023, 0, 0);

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

	touch->input_dev = input_dev;
	input_set_drvdata(input_dev, touch);

	ret = input_register_device(input_dev);
	if (ret)
		goto err_free_input;

	platform_set_drvdata(pdev, touch);
	return 0;

err_free_input:
	input_free_device(input_dev);
err_free_touch:
	kfree(touch);
	return ret;
}

static int da9034_touch_remove(struct platform_device *pdev)
{
	struct da9034_touch *touch = platform_get_drvdata(pdev);

	input_unregister_device(touch->input_dev);
	kfree(touch);

	return 0;
}

static struct platform_driver da9034_touch_driver = {
	.driver	= {
		.name	= "da9034-touch",
		.owner	= THIS_MODULE,
	},
	.probe		= da9034_touch_probe,
	.remove		= da9034_touch_remove,
};
module_platform_driver(da9034_touch_driver);

MODULE_DESCRIPTION("Touchscreen driver for Dialog Semiconductor DA9034");
MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>, Bin Yang <bin.yang@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9034-touch");
