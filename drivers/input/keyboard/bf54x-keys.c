/*
 * File:         drivers/input/keyboard/bf54x-keys.c
 * Based on:
 * Author:       Michael Hennerich <hennerich@blackfin.uclinux.org>
 *
 * Created:
 * Description:  keypad driver for Analog Devices Blackfin BF54x Processors
 *
 *
 * Modified:
 *               Copyright 2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/version.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>

#include <asm/portmux.h>
#include <asm/mach/bf54x_keys.h>

#define DRV_NAME	"bf54x-keys"
#define TIME_SCALE	100	/* 100 ns */
#define	MAX_MULT	(0xFF * TIME_SCALE)
#define MAX_RC		8	/* Max Row/Col */

static const u16 per_rows[] = {
	P_KEY_ROW7,
	P_KEY_ROW6,
	P_KEY_ROW5,
	P_KEY_ROW4,
	P_KEY_ROW3,
	P_KEY_ROW2,
	P_KEY_ROW1,
	P_KEY_ROW0,
	0
};

static const u16 per_cols[] = {
	P_KEY_COL7,
	P_KEY_COL6,
	P_KEY_COL5,
	P_KEY_COL4,
	P_KEY_COL3,
	P_KEY_COL2,
	P_KEY_COL1,
	P_KEY_COL0,
	0
};

struct bf54x_kpad {
	struct input_dev *input;
	int irq;
	unsigned short lastkey;
	unsigned short *keycode;
	struct timer_list timer;
	unsigned int keyup_test_jiffies;
};

static inline int bfin_kpad_find_key(struct bf54x_kpad *bf54x_kpad,
			struct input_dev *input, u16 keyident)
{
	u16 i;

	for (i = 0; i < input->keycodemax; i++)
		if (bf54x_kpad->keycode[i + input->keycodemax] == keyident)
			return bf54x_kpad->keycode[i];
	return -1;
}

static inline void bfin_keycodecpy(unsigned short *keycode,
			const unsigned int *pdata_kc,
			unsigned short keymapsize)
{
	unsigned int i;

	for (i = 0; i < keymapsize; i++) {
		keycode[i] = pdata_kc[i] & 0xffff;
		keycode[i + keymapsize] = pdata_kc[i] >> 16;
	}
}

static inline u16 bfin_kpad_get_prescale(u32 timescale)
{
	u32 sclk = get_sclk();

	return ((((sclk / 1000) * timescale) / 1024) - 1);
}

static inline u16 bfin_kpad_get_keypressed(struct bf54x_kpad *bf54x_kpad)
{
	return (bfin_read_KPAD_STAT() & KPAD_PRESSED);
}

static inline void bfin_kpad_clear_irq(void)
{
	bfin_write_KPAD_STAT(0xFFFF);
	bfin_write_KPAD_ROWCOL(0xFFFF);
}

static void bfin_kpad_timer(unsigned long data)
{
	struct platform_device *pdev =  (struct platform_device *) data;
	struct bf54x_kpad *bf54x_kpad = platform_get_drvdata(pdev);

	if (bfin_kpad_get_keypressed(bf54x_kpad)) {
		/* Try again later */
		mod_timer(&bf54x_kpad->timer,
			  jiffies + bf54x_kpad->keyup_test_jiffies);
		return;
	}

	input_report_key(bf54x_kpad->input, bf54x_kpad->lastkey, 0);
	input_sync(bf54x_kpad->input);

	/* Clear IRQ Status */

	bfin_kpad_clear_irq();
	enable_irq(bf54x_kpad->irq);
}

static irqreturn_t bfin_kpad_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct bf54x_kpad *bf54x_kpad = platform_get_drvdata(pdev);
	struct input_dev *input = bf54x_kpad->input;
	int key;
	u16 rowcol = bfin_read_KPAD_ROWCOL();

	key = bfin_kpad_find_key(bf54x_kpad, input, rowcol);

	input_report_key(input, key, 1);
	input_sync(input);

	if (bfin_kpad_get_keypressed(bf54x_kpad)) {
		disable_irq(bf54x_kpad->irq);
		bf54x_kpad->lastkey = key;
		mod_timer(&bf54x_kpad->timer,
			  jiffies + bf54x_kpad->keyup_test_jiffies);
	} else {
		input_report_key(input, key, 0);
		input_sync(input);

		bfin_kpad_clear_irq();
	}

	return IRQ_HANDLED;
}

static int __devinit bfin_kpad_probe(struct platform_device *pdev)
{
	struct bf54x_kpad *bf54x_kpad;
	struct bfin_kpad_platform_data *pdata = pdev->dev.platform_data;
	struct input_dev *input;
	int i, error;

	if (!pdata->rows || !pdata->cols || !pdata->keymap) {
		printk(KERN_ERR DRV_NAME
			": No rows, cols or keymap from pdata\n");
		return -EINVAL;
	}

	if (!pdata->keymapsize ||
	    pdata->keymapsize > (pdata->rows * pdata->cols)) {
		printk(KERN_ERR DRV_NAME ": Invalid keymapsize\n");
		return -EINVAL;
	}

	bf54x_kpad = kzalloc(sizeof(struct bf54x_kpad), GFP_KERNEL);
	if (!bf54x_kpad)
		return -ENOMEM;

	platform_set_drvdata(pdev, bf54x_kpad);

	/* Allocate memory for keymap followed by private LUT */
	bf54x_kpad->keycode = kmalloc(pdata->keymapsize *
					sizeof(unsigned short) * 2, GFP_KERNEL);
	if (!bf54x_kpad->keycode) {
		error = -ENOMEM;
		goto out;
	}

	if (!pdata->debounce_time || !pdata->debounce_time > MAX_MULT ||
	    !pdata->coldrive_time || !pdata->coldrive_time > MAX_MULT) {
		printk(KERN_ERR DRV_NAME
			": Invalid Debounce/Columdrive Time from pdata\n");
		bfin_write_KPAD_MSEL(0xFF0);	/* Default MSEL	*/
	} else {
		bfin_write_KPAD_MSEL(
			((pdata->debounce_time / TIME_SCALE)
						& DBON_SCALE) |
			(((pdata->coldrive_time / TIME_SCALE) << 8)
						& COLDRV_SCALE));

	}

	if (!pdata->keyup_test_interval)
		bf54x_kpad->keyup_test_jiffies = msecs_to_jiffies(50);
	else
		bf54x_kpad->keyup_test_jiffies =
			msecs_to_jiffies(pdata->keyup_test_interval);

	if (peripheral_request_list((u16 *)&per_rows[MAX_RC - pdata->rows],
				    DRV_NAME)) {
		printk(KERN_ERR DRV_NAME
			": Requesting Peripherals failed\n");
		error = -EFAULT;
		goto out0;
	}

	if (peripheral_request_list((u16 *)&per_cols[MAX_RC - pdata->cols],
				    DRV_NAME)) {
		printk(KERN_ERR DRV_NAME
			": Requesting Peripherals failed\n");
		error = -EFAULT;
		goto out1;
	}

	bf54x_kpad->irq = platform_get_irq(pdev, 0);
	if (bf54x_kpad->irq < 0) {
		error = -ENODEV;
		goto out2;
	}

	error = request_irq(bf54x_kpad->irq, bfin_kpad_isr,
				 IRQF_SAMPLE_RANDOM, DRV_NAME, pdev);
	if (error) {
		printk(KERN_ERR DRV_NAME
			": unable to claim irq %d; error %d\n",
			bf54x_kpad->irq, error);
		goto out2;
	}

	input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto out3;
	}

	bf54x_kpad->input = input;

	input->name = pdev->name;
	input->phys = "bf54x-keys/input0";
	input->dev.parent = &pdev->dev;

	input_set_drvdata(input, bf54x_kpad);

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	input->keycodesize = sizeof(unsigned short);
	input->keycodemax = pdata->keymapsize;
	input->keycode = bf54x_kpad->keycode;

	bfin_keycodecpy(bf54x_kpad->keycode, pdata->keymap, pdata->keymapsize);

	/* setup input device */
	__set_bit(EV_KEY, input->evbit);

	if (pdata->repeat)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < input->keycodemax; i++)
		__set_bit(bf54x_kpad->keycode[i] & KEY_MAX, input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	error = input_register_device(input);
	if (error) {
		printk(KERN_ERR DRV_NAME
			": Unable to register input device (%d)\n", error);
		goto out4;
	}

	/* Init Keypad Key Up/Release test timer */

	setup_timer(&bf54x_kpad->timer, bfin_kpad_timer, (unsigned long) pdev);

	bfin_write_KPAD_PRESCALE(bfin_kpad_get_prescale(TIME_SCALE));

	bfin_write_KPAD_CTL((((pdata->cols - 1) << 13) & KPAD_COLEN) |
				(((pdata->rows - 1) << 10) & KPAD_ROWEN) |
				(2 & KPAD_IRQMODE));

	bfin_write_KPAD_CTL(bfin_read_KPAD_CTL() | KPAD_EN);

	device_init_wakeup(&pdev->dev, 1);

	printk(KERN_ERR DRV_NAME
		": Blackfin BF54x Keypad registered IRQ %d\n", bf54x_kpad->irq);

	return 0;

out4:
	input_free_device(input);
out3:
	free_irq(bf54x_kpad->irq, pdev);
out2:
	peripheral_free_list((u16 *)&per_cols[MAX_RC - pdata->cols]);
out1:
	peripheral_free_list((u16 *)&per_rows[MAX_RC - pdata->rows]);
out0:
	kfree(bf54x_kpad->keycode);
out:
	kfree(bf54x_kpad);
	platform_set_drvdata(pdev, NULL);

	return error;
}

static int __devexit bfin_kpad_remove(struct platform_device *pdev)
{
	struct bfin_kpad_platform_data *pdata = pdev->dev.platform_data;
	struct bf54x_kpad *bf54x_kpad = platform_get_drvdata(pdev);

	del_timer_sync(&bf54x_kpad->timer);
	free_irq(bf54x_kpad->irq, pdev);

	input_unregister_device(bf54x_kpad->input);

	peripheral_free_list((u16 *)&per_rows[MAX_RC - pdata->rows]);
	peripheral_free_list((u16 *)&per_cols[MAX_RC - pdata->cols]);

	kfree(bf54x_kpad->keycode);
	kfree(bf54x_kpad);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int bfin_kpad_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct bf54x_kpad *bf54x_kpad = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(bf54x_kpad->irq);

	return 0;
}

static int bfin_kpad_resume(struct platform_device *pdev)
{
	struct bf54x_kpad *bf54x_kpad = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(bf54x_kpad->irq);

	return 0;
}
#else
# define bfin_kpad_suspend NULL
# define bfin_kpad_resume  NULL
#endif

struct platform_driver bfin_kpad_device_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= bfin_kpad_probe,
	.remove		= __devexit_p(bfin_kpad_remove),
	.suspend	= bfin_kpad_suspend,
	.resume		= bfin_kpad_resume,
};

static int __init bfin_kpad_init(void)
{
	return platform_driver_register(&bfin_kpad_device_driver);
}

static void __exit bfin_kpad_exit(void)
{
	platform_driver_unregister(&bfin_kpad_device_driver);
}

module_init(bfin_kpad_init);
module_exit(bfin_kpad_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Keypad driver for BF54x Processors");
MODULE_ALIAS("platform:bf54x-keys");
