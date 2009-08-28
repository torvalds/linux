/*
 *  GPIO driven matrix keyboard driver
 *
 *  Copyright (c) 2008 Marek Vasut <marek.vasut@gmail.com>
 *
 *  Based on corgikbd.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/input/matrix_keypad.h>

struct matrix_keypad {
	const struct matrix_keypad_platform_data *pdata;
	struct input_dev *input_dev;
	unsigned short *keycodes;
	unsigned int row_shift;

	uint32_t last_key_state[MATRIX_MAX_COLS];
	struct delayed_work work;
	bool scan_pending;
	bool stopped;
	spinlock_t lock;
};

/*
 * NOTE: normally the GPIO has to be put into HiZ when de-activated to cause
 * minmal side effect when scanning other columns, here it is configured to
 * be input, and it should work on most platforms.
 */
static void __activate_col(const struct matrix_keypad_platform_data *pdata,
			   int col, bool on)
{
	bool level_on = !pdata->active_low;

	if (on) {
		gpio_direction_output(pdata->col_gpios[col], level_on);
	} else {
		gpio_set_value_cansleep(pdata->col_gpios[col], !level_on);
		gpio_direction_input(pdata->col_gpios[col]);
	}
}

static void activate_col(const struct matrix_keypad_platform_data *pdata,
			 int col, bool on)
{
	__activate_col(pdata, col, on);

	if (on && pdata->col_scan_delay_us)
		udelay(pdata->col_scan_delay_us);
}

static void activate_all_cols(const struct matrix_keypad_platform_data *pdata,
			      bool on)
{
	int col;

	for (col = 0; col < pdata->num_col_gpios; col++)
		__activate_col(pdata, col, on);
}

static bool row_asserted(const struct matrix_keypad_platform_data *pdata,
			 int row)
{
	return gpio_get_value_cansleep(pdata->row_gpios[row]) ?
			!pdata->active_low : pdata->active_low;
}

static void enable_row_irqs(struct matrix_keypad *keypad)
{
	const struct matrix_keypad_platform_data *pdata = keypad->pdata;
	int i;

	for (i = 0; i < pdata->num_row_gpios; i++)
		enable_irq(gpio_to_irq(pdata->row_gpios[i]));
}

static void disable_row_irqs(struct matrix_keypad *keypad)
{
	const struct matrix_keypad_platform_data *pdata = keypad->pdata;
	int i;

	for (i = 0; i < pdata->num_row_gpios; i++)
		disable_irq_nosync(gpio_to_irq(pdata->row_gpios[i]));
}

/*
 * This gets the keys from keyboard and reports it to input subsystem
 */
static void matrix_keypad_scan(struct work_struct *work)
{
	struct matrix_keypad *keypad =
		container_of(work, struct matrix_keypad, work.work);
	struct input_dev *input_dev = keypad->input_dev;
	const struct matrix_keypad_platform_data *pdata = keypad->pdata;
	uint32_t new_state[MATRIX_MAX_COLS];
	int row, col, code;

	/* de-activate all columns for scanning */
	activate_all_cols(pdata, false);

	memset(new_state, 0, sizeof(new_state));

	/* assert each column and read the row status out */
	for (col = 0; col < pdata->num_col_gpios; col++) {

		activate_col(pdata, col, true);

		for (row = 0; row < pdata->num_row_gpios; row++)
			new_state[col] |=
				row_asserted(pdata, row) ? (1 << row) : 0;

		activate_col(pdata, col, false);
	}

	for (col = 0; col < pdata->num_col_gpios; col++) {
		uint32_t bits_changed;

		bits_changed = keypad->last_key_state[col] ^ new_state[col];
		if (bits_changed == 0)
			continue;

		for (row = 0; row < pdata->num_row_gpios; row++) {
			if ((bits_changed & (1 << row)) == 0)
				continue;

			code = MATRIX_SCAN_CODE(row, col, keypad->row_shift);
			input_event(input_dev, EV_MSC, MSC_SCAN, code);
			input_report_key(input_dev,
					 keypad->keycodes[code],
					 new_state[col] & (1 << row));
		}
	}
	input_sync(input_dev);

	memcpy(keypad->last_key_state, new_state, sizeof(new_state));

	activate_all_cols(pdata, true);

	/* Enable IRQs again */
	spin_lock_irq(&keypad->lock);
	keypad->scan_pending = false;
	enable_row_irqs(keypad);
	spin_unlock_irq(&keypad->lock);
}

static irqreturn_t matrix_keypad_interrupt(int irq, void *id)
{
	struct matrix_keypad *keypad = id;
	unsigned long flags;

	spin_lock_irqsave(&keypad->lock, flags);

	/*
	 * See if another IRQ beaten us to it and scheduled the
	 * scan already. In that case we should not try to
	 * disable IRQs again.
	 */
	if (unlikely(keypad->scan_pending || keypad->stopped))
		goto out;

	disable_row_irqs(keypad);
	keypad->scan_pending = true;
	schedule_delayed_work(&keypad->work,
		msecs_to_jiffies(keypad->pdata->debounce_ms));

out:
	spin_unlock_irqrestore(&keypad->lock, flags);
	return IRQ_HANDLED;
}

static int matrix_keypad_start(struct input_dev *dev)
{
	struct matrix_keypad *keypad = input_get_drvdata(dev);

	keypad->stopped = false;
	mb();

	/*
	 * Schedule an immediate key scan to capture current key state;
	 * columns will be activated and IRQs be enabled after the scan.
	 */
	schedule_delayed_work(&keypad->work, 0);

	return 0;
}

static void matrix_keypad_stop(struct input_dev *dev)
{
	struct matrix_keypad *keypad = input_get_drvdata(dev);

	keypad->stopped = true;
	mb();
	flush_work(&keypad->work.work);
	/*
	 * matrix_keypad_scan() will leave IRQs enabled;
	 * we should disable them now.
	 */
	disable_row_irqs(keypad);
}

#ifdef CONFIG_PM
static int matrix_keypad_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct matrix_keypad *keypad = platform_get_drvdata(pdev);
	const struct matrix_keypad_platform_data *pdata = keypad->pdata;
	int i;

	matrix_keypad_stop(keypad->input_dev);

	if (device_may_wakeup(&pdev->dev))
		for (i = 0; i < pdata->num_row_gpios; i++)
			enable_irq_wake(gpio_to_irq(pdata->row_gpios[i]));

	return 0;
}

static int matrix_keypad_resume(struct platform_device *pdev)
{
	struct matrix_keypad *keypad = platform_get_drvdata(pdev);
	const struct matrix_keypad_platform_data *pdata = keypad->pdata;
	int i;

	if (device_may_wakeup(&pdev->dev))
		for (i = 0; i < pdata->num_row_gpios; i++)
			disable_irq_wake(gpio_to_irq(pdata->row_gpios[i]));

	matrix_keypad_start(keypad->input_dev);

	return 0;
}
#else
#define matrix_keypad_suspend	NULL
#define matrix_keypad_resume	NULL
#endif

static int __devinit init_matrix_gpio(struct platform_device *pdev,
					struct matrix_keypad *keypad)
{
	const struct matrix_keypad_platform_data *pdata = keypad->pdata;
	int i, err = -EINVAL;

	/* initialized strobe lines as outputs, activated */
	for (i = 0; i < pdata->num_col_gpios; i++) {
		err = gpio_request(pdata->col_gpios[i], "matrix_kbd_col");
		if (err) {
			dev_err(&pdev->dev,
				"failed to request GPIO%d for COL%d\n",
				pdata->col_gpios[i], i);
			goto err_free_cols;
		}

		gpio_direction_output(pdata->col_gpios[i], !pdata->active_low);
	}

	for (i = 0; i < pdata->num_row_gpios; i++) {
		err = gpio_request(pdata->row_gpios[i], "matrix_kbd_row");
		if (err) {
			dev_err(&pdev->dev,
				"failed to request GPIO%d for ROW%d\n",
				pdata->row_gpios[i], i);
			goto err_free_rows;
		}

		gpio_direction_input(pdata->row_gpios[i]);
	}

	for (i = 0; i < pdata->num_row_gpios; i++) {
		err = request_irq(gpio_to_irq(pdata->row_gpios[i]),
				matrix_keypad_interrupt,
				IRQF_DISABLED |
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"matrix-keypad", keypad);
		if (err) {
			dev_err(&pdev->dev,
				"Unable to acquire interrupt for GPIO line %i\n",
				pdata->row_gpios[i]);
			goto err_free_irqs;
		}
	}

	/* initialized as disabled - enabled by input->open */
	disable_row_irqs(keypad);
	return 0;

err_free_irqs:
	while (--i >= 0)
		free_irq(gpio_to_irq(pdata->row_gpios[i]), keypad);
	i = pdata->num_row_gpios;
err_free_rows:
	while (--i >= 0)
		gpio_free(pdata->row_gpios[i]);
	i = pdata->num_col_gpios;
err_free_cols:
	while (--i >= 0)
		gpio_free(pdata->col_gpios[i]);

	return err;
}

static int __devinit matrix_keypad_probe(struct platform_device *pdev)
{
	const struct matrix_keypad_platform_data *pdata;
	const struct matrix_keymap_data *keymap_data;
	struct matrix_keypad *keypad;
	struct input_dev *input_dev;
	unsigned short *keycodes;
	unsigned int row_shift;
	int i;
	int err;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	keymap_data = pdata->keymap_data;
	if (!keymap_data) {
		dev_err(&pdev->dev, "no keymap data defined\n");
		return -EINVAL;
	}

	row_shift = get_count_order(pdata->num_col_gpios);

	keypad = kzalloc(sizeof(struct matrix_keypad), GFP_KERNEL);
	keycodes = kzalloc((pdata->num_row_gpios << row_shift) *
				sizeof(*keycodes),
			   GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!keypad || !keycodes || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	keypad->input_dev = input_dev;
	keypad->pdata = pdata;
	keypad->keycodes = keycodes;
	keypad->row_shift = row_shift;
	keypad->stopped = true;
	INIT_DELAYED_WORK(&keypad->work, matrix_keypad_scan);
	spin_lock_init(&keypad->lock);

	input_dev->name		= pdev->name;
	input_dev->id.bustype	= BUS_HOST;
	input_dev->dev.parent	= &pdev->dev;
	input_dev->evbit[0]	= BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_dev->open		= matrix_keypad_start;
	input_dev->close	= matrix_keypad_stop;

	input_dev->keycode	= keycodes;
	input_dev->keycodesize	= sizeof(*keycodes);
	input_dev->keycodemax	= pdata->num_row_gpios << keypad->row_shift;

	for (i = 0; i < keymap_data->keymap_size; i++) {
		unsigned int key = keymap_data->keymap[i];
		unsigned int row = KEY_ROW(key);
		unsigned int col = KEY_COL(key);
		unsigned short code = KEY_VAL(key);

		keycodes[MATRIX_SCAN_CODE(row, col, row_shift)] = code;
		__set_bit(code, input_dev->keybit);
	}
	__clear_bit(KEY_RESERVED, input_dev->keybit);

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);
	input_set_drvdata(input_dev, keypad);

	err = init_matrix_gpio(pdev, keypad);
	if (err)
		goto err_free_mem;

	err = input_register_device(keypad->input_dev);
	if (err)
		goto err_free_mem;

	device_init_wakeup(&pdev->dev, pdata->wakeup);
	platform_set_drvdata(pdev, keypad);

	return 0;

err_free_mem:
	input_free_device(input_dev);
	kfree(keycodes);
	kfree(keypad);
	return err;
}

static int __devexit matrix_keypad_remove(struct platform_device *pdev)
{
	struct matrix_keypad *keypad = platform_get_drvdata(pdev);
	const struct matrix_keypad_platform_data *pdata = keypad->pdata;
	int i;

	device_init_wakeup(&pdev->dev, 0);

	for (i = 0; i < pdata->num_row_gpios; i++) {
		free_irq(gpio_to_irq(pdata->row_gpios[i]), keypad);
		gpio_free(pdata->row_gpios[i]);
	}

	for (i = 0; i < pdata->num_col_gpios; i++)
		gpio_free(pdata->col_gpios[i]);

	input_unregister_device(keypad->input_dev);
	platform_set_drvdata(pdev, NULL);
	kfree(keypad->keycodes);
	kfree(keypad);

	return 0;
}

static struct platform_driver matrix_keypad_driver = {
	.probe		= matrix_keypad_probe,
	.remove		= __devexit_p(matrix_keypad_remove),
	.suspend	= matrix_keypad_suspend,
	.resume		= matrix_keypad_resume,
	.driver		= {
		.name	= "matrix-keypad",
		.owner	= THIS_MODULE,
	},
};

static int __init matrix_keypad_init(void)
{
	return platform_driver_register(&matrix_keypad_driver);
}

static void __exit matrix_keypad_exit(void)
{
	platform_driver_unregister(&matrix_keypad_driver);
}

module_init(matrix_keypad_init);
module_exit(matrix_keypad_exit);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("GPIO Driven Matrix Keypad Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:matrix-keypad");
