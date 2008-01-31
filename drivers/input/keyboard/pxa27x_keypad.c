/*
 * linux/drivers/input/keyboard/pxa27x_keypad.c
 *
 * Driver for the pxa27x matrix keyboard controller.
 *
 * Created:	Feb 22, 2007
 * Author:	Rodolfo Giometti <giometti@linux.it>
 *
 * Based on a previous implementations by Kevin O'Connor
 * <kevin_at_koconnor.net> and Alex Osborne <bobofdoom@gmail.com> and
 * on some suggestions by Nicolas Pitre <nico@cam.org>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/irqs.h>
#include <asm/arch/pxa27x_keypad.h>

#define DRIVER_NAME		"pxa27x-keypad"

#define KPAS_MUKP(n)		(((n) >> 26) & 0x1f)
#define KPAS_RP(n)		(((n) >> 4) & 0xf)
#define KPAS_CP(n)		((n) & 0xf)

#define KPASMKP_MKC_MASK	(0xff)

#define MAX_MATRIX_KEY_NUM	(8 * 8)

struct pxa27x_keypad {
	struct pxa27x_keypad_platform_data *pdata;

	struct clk *clk;
	struct input_dev *input_dev;

	/* matrix key code map */
	unsigned int matrix_keycodes[MAX_MATRIX_KEY_NUM];

	/* state row bits of each column scan */
	uint32_t matrix_key_state[MAX_MATRIX_KEY_COLS];
};

static void pxa27x_keypad_build_keycode(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	struct input_dev *input_dev = keypad->input_dev;
	unsigned int *key;
	int i;

	key = &pdata->matrix_key_map[0];
	for (i = 0; i < pdata->matrix_key_map_size; i++, key++) {
		int row = ((*key) >> 28) & 0xf;
		int col = ((*key) >> 24) & 0xf;
		int code = (*key) & 0xffffff;

		keypad->matrix_keycodes[(row << 3) + col] = code;
		set_bit(code, input_dev->keybit);
	}
}

static inline unsigned int lookup_matrix_keycode(
		struct pxa27x_keypad *keypad, int row, int col)
{
	return keypad->matrix_keycodes[(row << 3) + col];
}

static void pxa27x_keypad_scan_matrix(struct pxa27x_keypad *keypad)
{
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	int row, col, num_keys_pressed = 0;
	uint32_t new_state[MAX_MATRIX_KEY_COLS];
	uint32_t kpas = KPAS;

	num_keys_pressed = KPAS_MUKP(kpas);

	memset(new_state, 0, sizeof(new_state));

	if (num_keys_pressed == 0)
		goto scan;

	if (num_keys_pressed == 1) {
		col = KPAS_CP(kpas);
		row = KPAS_RP(kpas);

		/* if invalid row/col, treat as no key pressed */
		if (col >= pdata->matrix_key_cols ||
		    row >= pdata->matrix_key_rows)
			goto scan;

		new_state[col] = (1 << row);
		goto scan;
	}

	if (num_keys_pressed > 1) {
		uint32_t kpasmkp0 = KPASMKP0;
		uint32_t kpasmkp1 = KPASMKP1;
		uint32_t kpasmkp2 = KPASMKP2;
		uint32_t kpasmkp3 = KPASMKP3;

		new_state[0] = kpasmkp0 & KPASMKP_MKC_MASK;
		new_state[1] = (kpasmkp0 >> 16) & KPASMKP_MKC_MASK;
		new_state[2] = kpasmkp1 & KPASMKP_MKC_MASK;
		new_state[3] = (kpasmkp1 >> 16) & KPASMKP_MKC_MASK;
		new_state[4] = kpasmkp2 & KPASMKP_MKC_MASK;
		new_state[5] = (kpasmkp2 >> 16) & KPASMKP_MKC_MASK;
		new_state[6] = kpasmkp3 & KPASMKP_MKC_MASK;
		new_state[7] = (kpasmkp3 >> 16) & KPASMKP_MKC_MASK;
	}
scan:
	for (col = 0; col < pdata->matrix_key_cols; col++) {
		uint32_t bits_changed;

		bits_changed = keypad->matrix_key_state[col] ^ new_state[col];
		if (bits_changed == 0)
			continue;

		for (row = 0; row < pdata->matrix_key_rows; row++) {
			if ((bits_changed & (1 << row)) == 0)
				continue;

			input_report_key(keypad->input_dev,
				lookup_matrix_keycode(keypad, row, col),
				new_state[col] & (1 << row));
		}
	}
	input_sync(keypad->input_dev);
	memcpy(keypad->matrix_key_state, new_state, sizeof(new_state));
}

static irqreturn_t pxa27x_keypad_irq_handler(int irq, void *dev_id)
{
	struct pxa27x_keypad *keypad = dev_id;
	struct input_dev *input_dev = keypad->input_dev;
	unsigned long kpc = KPC;
	int rel;

	if (kpc & KPC_DI) {
		unsigned long kpdk = KPDK;

		if (!(kpdk & KPDK_DKP)) {
			/* better luck next time */
		} else if (kpc & KPC_REE0) {
			unsigned long kprec = KPREC;
			KPREC = 0x7f;

			if (kprec & KPREC_OF0)
				rel = (kprec & 0xff) + 0x7f;
			else if (kprec & KPREC_UF0)
				rel = (kprec & 0xff) - 0x7f - 0xff;
			else
				rel = (kprec & 0xff) - 0x7f;

			if (rel) {
				input_report_rel(input_dev, REL_WHEEL, rel);
				input_sync(input_dev);
			}
		}
	}

	if (kpc & KPC_MI)
		pxa27x_keypad_scan_matrix(keypad);

	return IRQ_HANDLED;
}

static int pxa27x_keypad_open(struct input_dev *dev)
{
	struct pxa27x_keypad *keypad = input_get_drvdata(dev);

	/* Set keypad control register */
	KPC |= (KPC_ASACT |
		KPC_MS_ALL |
		(2 << 6) | KPC_REE0 | KPC_DK_DEB_SEL |
		KPC_ME | KPC_MIE | KPC_DE | KPC_DIE);

	KPC &= ~KPC_AS;         /* disable automatic scan */
	KPC &= ~KPC_IMKP;       /* do not ignore multiple keypresses */

	/* Set rotary count to mid-point value */
	KPREC = 0x7F;

	/* Enable unit clock */
	clk_enable(keypad->clk);

	return 0;
}

static void pxa27x_keypad_close(struct input_dev *dev)
{
	struct pxa27x_keypad *keypad = input_get_drvdata(dev);

	/* Disable clock unit */
	clk_disable(keypad->clk);
}

#ifdef CONFIG_PM
static int pxa27x_keypad_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;

	/* Save controller status */
	pdata->reg_kpc = KPC;
	pdata->reg_kprec = KPREC;

	return 0;
}

static int pxa27x_keypad_resume(struct platform_device *pdev)
{
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);
	struct pxa27x_keypad_platform_data *pdata = keypad->pdata;
	struct input_dev *input_dev = keypad->input_dev;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users) {
		/* Restore controller status */
		KPC = pdata->reg_kpc;
		KPREC = pdata->reg_kprec;

		/* Enable unit clock */
		clk_enable(keypad->clk);
	}

	mutex_unlock(&input_dev->mutex);

	return 0;
}
#else
#define pxa27x_keypad_suspend	NULL
#define pxa27x_keypad_resume	NULL
#endif

static int __devinit pxa27x_keypad_probe(struct platform_device *pdev)
{
	struct pxa27x_keypad *keypad;
	struct input_dev *input_dev;
	int col, error;

	keypad = kzalloc(sizeof(struct pxa27x_keypad), GFP_KERNEL);
	if (keypad == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	keypad->pdata = pdev->dev.platform_data;
	if (keypad->pdata == NULL) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto failed_free;
	}

	keypad->clk = clk_get(&pdev->dev, "KBDCLK");
	if (IS_ERR(keypad->clk)) {
		dev_err(&pdev->dev, "failed to get keypad clock\n");
		error = PTR_ERR(keypad->clk);
		goto failed_free;
	}

	/* Create and register the input driver. */
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		error = -ENOMEM;
		goto failed_put_clk;
	}

	input_dev->name = DRIVER_NAME;
	input_dev->id.bustype = BUS_HOST;
	input_dev->open = pxa27x_keypad_open;
	input_dev->close = pxa27x_keypad_close;
	input_dev->dev.parent = &pdev->dev;

	keypad->input_dev = input_dev;
	input_set_drvdata(input_dev, keypad);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP) |
		BIT_MASK(EV_REL);
	input_dev->relbit[BIT_WORD(REL_WHEEL)] = BIT_MASK(REL_WHEEL);

	pxa27x_keypad_build_keycode(keypad);

	error = request_irq(IRQ_KEYPAD, pxa27x_keypad_irq_handler, IRQF_DISABLED,
			    DRIVER_NAME, keypad);
	if (error) {
		printk(KERN_ERR "Cannot request keypad IRQ\n");
		goto err_free_dev;
	}

	platform_set_drvdata(pdev, keypad);

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error)
		goto err_free_irq;

	/*
	 * Store rows/cols info into keyboard registers.
	 */

	KPC |= (keypad->pdata->matrix_key_rows - 1) << 26;
	KPC |= (keypad->pdata->matrix_key_cols - 1) << 23;

	for (col = 0; col < keypad->pdata->matrix_key_cols; col++)
		KPC |= KPC_MS0 << col;

	return 0;

 err_free_irq:
	platform_set_drvdata(pdev, NULL);
	free_irq(IRQ_KEYPAD, pdev);
 err_free_dev:
	input_free_device(input_dev);
failed_put_clk:
	clk_put(keypad->clk);
failed_free:
	kfree(keypad);
	return error;
}

static int __devexit pxa27x_keypad_remove(struct platform_device *pdev)
{
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);

	free_irq(IRQ_KEYPAD, pdev);

	clk_disable(keypad->clk);
	clk_put(keypad->clk);

	input_unregister_device(keypad->input_dev);

	platform_set_drvdata(pdev, NULL);
	kfree(keypad);
	return 0;
}

static struct platform_driver pxa27x_keypad_driver = {
	.probe		= pxa27x_keypad_probe,
	.remove		= __devexit_p(pxa27x_keypad_remove),
	.suspend	= pxa27x_keypad_suspend,
	.resume		= pxa27x_keypad_resume,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __init pxa27x_keypad_init(void)
{
	return platform_driver_register(&pxa27x_keypad_driver);
}

static void __exit pxa27x_keypad_exit(void)
{
	platform_driver_unregister(&pxa27x_keypad_driver);
}

module_init(pxa27x_keypad_init);
module_exit(pxa27x_keypad_exit);

MODULE_DESCRIPTION("PXA27x Keypad Controller Driver");
MODULE_LICENSE("GPL");
