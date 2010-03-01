/*
 * Rotary counter driver for Analog Devices Blackfin Processors
 *
 * Copyright 2008-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/input.h>

#include <asm/portmux.h>
#include <asm/bfin_rotary.h>

static const u16 per_cnt[] = {
	P_CNT_CUD,
	P_CNT_CDG,
	P_CNT_CZM,
	0
};

struct bfin_rot {
	struct input_dev *input;
	int irq;
	unsigned int up_key;
	unsigned int down_key;
	unsigned int button_key;
	unsigned int rel_code;
	unsigned short cnt_config;
	unsigned short cnt_imask;
	unsigned short cnt_debounce;
};

static void report_key_event(struct input_dev *input, int keycode)
{
	/* simulate a press-n-release */
	input_report_key(input, keycode, 1);
	input_sync(input);
	input_report_key(input, keycode, 0);
	input_sync(input);
}

static void report_rotary_event(struct bfin_rot *rotary, int delta)
{
	struct input_dev *input = rotary->input;

	if (rotary->up_key) {
		report_key_event(input,
				 delta > 0 ? rotary->up_key : rotary->down_key);
	} else {
		input_report_rel(input, rotary->rel_code, delta);
		input_sync(input);
	}
}

static irqreturn_t bfin_rotary_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct bfin_rot *rotary = platform_get_drvdata(pdev);
	int delta;

	switch (bfin_read_CNT_STATUS()) {

	case ICII:
		break;

	case UCII:
	case DCII:
		delta = bfin_read_CNT_COUNTER();
		if (delta)
			report_rotary_event(rotary, delta);
		break;

	case CZMII:
		report_key_event(rotary->input, rotary->button_key);
		break;

	default:
		break;
	}

	bfin_write_CNT_COMMAND(W1LCNT_ZERO);	/* Clear COUNTER */
	bfin_write_CNT_STATUS(-1);	/* Clear STATUS */

	return IRQ_HANDLED;
}

static int __devinit bfin_rotary_probe(struct platform_device *pdev)
{
	struct bfin_rotary_platform_data *pdata = pdev->dev.platform_data;
	struct bfin_rot *rotary;
	struct input_dev *input;
	int error;

	/* Basic validation */
	if ((pdata->rotary_up_key && !pdata->rotary_down_key) ||
	    (!pdata->rotary_up_key && pdata->rotary_down_key)) {
		return -EINVAL;
	}

	error = peripheral_request_list(per_cnt, dev_name(&pdev->dev));
	if (error) {
		dev_err(&pdev->dev, "requesting peripherals failed\n");
		return error;
	}

	rotary = kzalloc(sizeof(struct bfin_rot), GFP_KERNEL);
	input = input_allocate_device();
	if (!rotary || !input) {
		error = -ENOMEM;
		goto out1;
	}

	rotary->input = input;

	rotary->up_key = pdata->rotary_up_key;
	rotary->down_key = pdata->rotary_down_key;
	rotary->button_key = pdata->rotary_button_key;
	rotary->rel_code = pdata->rotary_rel_code;

	error = rotary->irq = platform_get_irq(pdev, 0);
	if (error < 0)
		goto out1;

	input->name = pdev->name;
	input->phys = "bfin-rotary/input0";
	input->dev.parent = &pdev->dev;

	input_set_drvdata(input, rotary);

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	if (rotary->up_key) {
		__set_bit(EV_KEY, input->evbit);
		__set_bit(rotary->up_key, input->keybit);
		__set_bit(rotary->down_key, input->keybit);
	} else {
		__set_bit(EV_REL, input->evbit);
		__set_bit(rotary->rel_code, input->relbit);
	}

	if (rotary->button_key) {
		__set_bit(EV_KEY, input->evbit);
		__set_bit(rotary->button_key, input->keybit);
	}

	error = request_irq(rotary->irq, bfin_rotary_isr,
			    0, dev_name(&pdev->dev), pdev);
	if (error) {
		dev_err(&pdev->dev,
			"unable to claim irq %d; error %d\n",
			rotary->irq, error);
		goto out1;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev,
			"unable to register input device (%d)\n", error);
		goto out2;
	}

	if (pdata->rotary_button_key)
		bfin_write_CNT_IMASK(CZMIE);

	if (pdata->mode & ROT_DEBE)
		bfin_write_CNT_DEBOUNCE(pdata->debounce & DPRESCALE);

	if (pdata->mode)
		bfin_write_CNT_CONFIG(bfin_read_CNT_CONFIG() |
					(pdata->mode & ~CNTE));

	bfin_write_CNT_IMASK(bfin_read_CNT_IMASK() | UCIE | DCIE);
	bfin_write_CNT_CONFIG(bfin_read_CNT_CONFIG() | CNTE);

	platform_set_drvdata(pdev, rotary);
	device_init_wakeup(&pdev->dev, 1);

	return 0;

out2:
	free_irq(rotary->irq, pdev);
out1:
	input_free_device(input);
	kfree(rotary);
	peripheral_free_list(per_cnt);

	return error;
}

static int __devexit bfin_rotary_remove(struct platform_device *pdev)
{
	struct bfin_rot *rotary = platform_get_drvdata(pdev);

	bfin_write_CNT_CONFIG(0);
	bfin_write_CNT_IMASK(0);

	free_irq(rotary->irq, pdev);
	input_unregister_device(rotary->input);
	peripheral_free_list(per_cnt);

	kfree(rotary);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int bfin_rotary_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bfin_rot *rotary = platform_get_drvdata(pdev);

	rotary->cnt_config = bfin_read_CNT_CONFIG();
	rotary->cnt_imask = bfin_read_CNT_IMASK();
	rotary->cnt_debounce = bfin_read_CNT_DEBOUNCE();

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(rotary->irq);

	return 0;
}

static int bfin_rotary_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bfin_rot *rotary = platform_get_drvdata(pdev);

	bfin_write_CNT_DEBOUNCE(rotary->cnt_debounce);
	bfin_write_CNT_IMASK(rotary->cnt_imask);
	bfin_write_CNT_CONFIG(rotary->cnt_config & ~CNTE);

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(rotary->irq);

	if (rotary->cnt_config & CNTE)
		bfin_write_CNT_CONFIG(rotary->cnt_config);

	return 0;
}

static const struct dev_pm_ops bfin_rotary_pm_ops = {
	.suspend	= bfin_rotary_suspend,
	.resume		= bfin_rotary_resume,
};
#endif

static struct platform_driver bfin_rotary_device_driver = {
	.probe		= bfin_rotary_probe,
	.remove		= __devexit_p(bfin_rotary_remove),
	.driver		= {
		.name	= "bfin-rotary",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &bfin_rotary_pm_ops,
#endif
	},
};

static int __init bfin_rotary_init(void)
{
	return platform_driver_register(&bfin_rotary_device_driver);
}
module_init(bfin_rotary_init);

static void __exit bfin_rotary_exit(void)
{
	platform_driver_unregister(&bfin_rotary_device_driver);
}
module_exit(bfin_rotary_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Rotary Counter driver for Blackfin Processors");
MODULE_ALIAS("platform:bfin-rotary");
