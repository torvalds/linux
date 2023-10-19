// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/drivers/input/keyboard/omap-keypad.c
 *
 * OMAP Keypad Driver
 *
 * Copyright (C) 2003 Nokia Corporation
 * Written by Timo Teräs <ext-timo.teras@nokia.com>
 *
 * Added support for H2 & H3 Keypad
 * Copyright (C) 2004 Texas Instruments
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/platform_data/gpio-omap.h>
#include <linux/platform_data/keypad-omap.h>
#include <linux/soc/ti/omap1-io.h>

#undef NEW_BOARD_LEARNING_MODE

static void omap_kp_tasklet(unsigned long);
static void omap_kp_timer(struct timer_list *);

static unsigned char keypad_state[8];
static DEFINE_MUTEX(kp_enable_mutex);
static int kp_enable = 1;
static int kp_cur_group = -1;

struct omap_kp {
	struct input_dev *input;
	struct timer_list timer;
	int irq;
	unsigned int rows;
	unsigned int cols;
	unsigned long delay;
	unsigned int debounce;
	unsigned short keymap[];
};

static DECLARE_TASKLET_DISABLED_OLD(kp_tasklet, omap_kp_tasklet);

static unsigned int *row_gpios;
static unsigned int *col_gpios;

static irqreturn_t omap_kp_interrupt(int irq, void *dev_id)
{
	/* disable keyboard interrupt and schedule for handling */
	omap_writew(1, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBD_MASKIT);

	tasklet_schedule(&kp_tasklet);

	return IRQ_HANDLED;
}

static void omap_kp_timer(struct timer_list *unused)
{
	tasklet_schedule(&kp_tasklet);
}

static void omap_kp_scan_keypad(struct omap_kp *omap_kp, unsigned char *state)
{
	int col = 0;

	/* disable keyboard interrupt and schedule for handling */
	omap_writew(1, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBD_MASKIT);

	/* read the keypad status */
	omap_writew(0xff, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBC);
	for (col = 0; col < omap_kp->cols; col++) {
		omap_writew(~(1 << col) & 0xff,
			    OMAP1_MPUIO_BASE + OMAP_MPUIO_KBC);

		udelay(omap_kp->delay);

		state[col] = ~omap_readw(OMAP1_MPUIO_BASE +
					 OMAP_MPUIO_KBR_LATCH) & 0xff;
	}
	omap_writew(0x00, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBC);
	udelay(2);
}

static void omap_kp_tasklet(unsigned long data)
{
	struct omap_kp *omap_kp_data = (struct omap_kp *) data;
	unsigned short *keycodes = omap_kp_data->input->keycode;
	unsigned int row_shift = get_count_order(omap_kp_data->cols);
	unsigned char new_state[8], changed, key_down = 0;
	int col, row;

	/* check for any changes */
	omap_kp_scan_keypad(omap_kp_data, new_state);

	/* check for changes and print those */
	for (col = 0; col < omap_kp_data->cols; col++) {
		changed = new_state[col] ^ keypad_state[col];
		key_down |= new_state[col];
		if (changed == 0)
			continue;

		for (row = 0; row < omap_kp_data->rows; row++) {
			int key;
			if (!(changed & (1 << row)))
				continue;
#ifdef NEW_BOARD_LEARNING_MODE
			printk(KERN_INFO "omap-keypad: key %d-%d %s\n", col,
			       row, (new_state[col] & (1 << row)) ?
			       "pressed" : "released");
#else
			key = keycodes[MATRIX_SCAN_CODE(row, col, row_shift)];

			if (!(kp_cur_group == (key & GROUP_MASK) ||
			      kp_cur_group == -1))
				continue;

			kp_cur_group = key & GROUP_MASK;
			input_report_key(omap_kp_data->input, key & ~GROUP_MASK,
					 new_state[col] & (1 << row));
#endif
		}
	}
	input_sync(omap_kp_data->input);
	memcpy(keypad_state, new_state, sizeof(keypad_state));

	if (key_down) {
		/* some key is pressed - keep irq disabled and use timer
		 * to poll the keypad */
		mod_timer(&omap_kp_data->timer, jiffies + HZ / 20);
	} else {
		/* enable interrupts */
		omap_writew(0, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBD_MASKIT);
		kp_cur_group = -1;
	}
}

static ssize_t omap_kp_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", kp_enable);
}

static ssize_t omap_kp_enable_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct omap_kp *omap_kp = dev_get_drvdata(dev);
	int state;

	if (sscanf(buf, "%u", &state) != 1)
		return -EINVAL;

	if ((state != 1) && (state != 0))
		return -EINVAL;

	mutex_lock(&kp_enable_mutex);
	if (state != kp_enable) {
		if (state)
			enable_irq(omap_kp->irq);
		else
			disable_irq(omap_kp->irq);
		kp_enable = state;
	}
	mutex_unlock(&kp_enable_mutex);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, omap_kp_enable_show, omap_kp_enable_store);

static int omap_kp_probe(struct platform_device *pdev)
{
	struct omap_kp *omap_kp;
	struct input_dev *input_dev;
	struct omap_kp_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int i, col_idx, row_idx, ret;
	unsigned int row_shift, keycodemax;

	if (!pdata->rows || !pdata->cols || !pdata->keymap_data) {
		printk(KERN_ERR "No rows, cols or keymap_data from pdata\n");
		return -EINVAL;
	}

	row_shift = get_count_order(pdata->cols);
	keycodemax = pdata->rows << row_shift;

	omap_kp = kzalloc(struct_size(omap_kp, keymap, keycodemax), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!omap_kp || !input_dev) {
		kfree(omap_kp);
		input_free_device(input_dev);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, omap_kp);

	omap_kp->input = input_dev;

	/* Disable the interrupt for the MPUIO keyboard */
	omap_writew(1, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBD_MASKIT);

	if (pdata->delay)
		omap_kp->delay = pdata->delay;

	if (pdata->row_gpios && pdata->col_gpios) {
		row_gpios = pdata->row_gpios;
		col_gpios = pdata->col_gpios;
	}

	omap_kp->rows = pdata->rows;
	omap_kp->cols = pdata->cols;

	col_idx = 0;
	row_idx = 0;

	timer_setup(&omap_kp->timer, omap_kp_timer, 0);

	/* get the irq and init timer*/
	kp_tasklet.data = (unsigned long) omap_kp;
	tasklet_enable(&kp_tasklet);

	ret = device_create_file(&pdev->dev, &dev_attr_enable);
	if (ret < 0)
		goto err2;

	/* setup input device */
	input_dev->name = "omap-keypad";
	input_dev->phys = "omap-keypad/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	if (pdata->rep)
		__set_bit(EV_REP, input_dev->evbit);

	ret = matrix_keypad_build_keymap(pdata->keymap_data, NULL,
					 pdata->rows, pdata->cols,
					 omap_kp->keymap, input_dev);
	if (ret < 0)
		goto err3;

	ret = input_register_device(omap_kp->input);
	if (ret < 0) {
		printk(KERN_ERR "Unable to register omap-keypad input device\n");
		goto err3;
	}

	if (pdata->dbounce)
		omap_writew(0xff, OMAP1_MPUIO_BASE + OMAP_MPUIO_GPIO_DEBOUNCING);

	/* scan current status and enable interrupt */
	omap_kp_scan_keypad(omap_kp, keypad_state);
	omap_kp->irq = platform_get_irq(pdev, 0);
	if (omap_kp->irq >= 0) {
		if (request_irq(omap_kp->irq, omap_kp_interrupt, 0,
				"omap-keypad", omap_kp) < 0)
			goto err4;
	}
	omap_writew(0, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBD_MASKIT);

	return 0;

err4:
	input_unregister_device(omap_kp->input);
	input_dev = NULL;
err3:
	device_remove_file(&pdev->dev, &dev_attr_enable);
err2:
	for (i = row_idx - 1; i >= 0; i--)
		gpio_free(row_gpios[i]);
	for (i = col_idx - 1; i >= 0; i--)
		gpio_free(col_gpios[i]);

	kfree(omap_kp);
	input_free_device(input_dev);

	return -EINVAL;
}

static int omap_kp_remove(struct platform_device *pdev)
{
	struct omap_kp *omap_kp = platform_get_drvdata(pdev);

	/* disable keypad interrupt handling */
	tasklet_disable(&kp_tasklet);
	omap_writew(1, OMAP1_MPUIO_BASE + OMAP_MPUIO_KBD_MASKIT);
	free_irq(omap_kp->irq, omap_kp);

	del_timer_sync(&omap_kp->timer);
	tasklet_kill(&kp_tasklet);

	/* unregister everything */
	input_unregister_device(omap_kp->input);

	kfree(omap_kp);

	return 0;
}

static struct platform_driver omap_kp_driver = {
	.probe		= omap_kp_probe,
	.remove		= omap_kp_remove,
	.driver		= {
		.name	= "omap-keypad",
	},
};
module_platform_driver(omap_kp_driver);

MODULE_AUTHOR("Timo Teräs");
MODULE_DESCRIPTION("OMAP Keypad Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap-keypad");
