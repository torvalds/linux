/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Jayeeta Banerjee <jayeeta.banerjee@stericsson.com>
 * Author: Sundar Iyer <sundar.iyer@stericsson.com>
 *
 * License Terms: GNU General Public License, version 2
 *
 * TC35893 MFD Keypad Controller driver
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/input/matrix_keypad.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mfd/tc3589x.h>

/* Maximum supported keypad matrix row/columns size */
#define TC3589x_MAX_KPROW               8
#define TC3589x_MAX_KPCOL               12

/* keypad related Constants */
#define TC3589x_MAX_DEBOUNCE_SETTLE     0xFF
#define DEDICATED_KEY_VAL		0xFF

/* Pull up/down masks */
#define TC3589x_NO_PULL_MASK		0x0
#define TC3589x_PULL_DOWN_MASK		0x1
#define TC3589x_PULL_UP_MASK		0x2
#define TC3589x_PULLUP_ALL_MASK		0xAA
#define TC3589x_IO_PULL_VAL(index, mask)	((mask)<<((index)%4)*2))

/* Bit masks for IOCFG register */
#define IOCFG_BALLCFG		0x01
#define IOCFG_IG		0x08

#define KP_EVCODE_COL_MASK	0x0F
#define KP_EVCODE_ROW_MASK	0x70
#define KP_RELEASE_EVT_MASK	0x80

#define KP_ROW_SHIFT		4

#define KP_NO_VALID_KEY_MASK	0x7F

/* bit masks for RESTCTRL register */
#define TC3589x_KBDRST		0x2
#define TC3589x_IRQRST		0x10
#define TC3589x_RESET_ALL	0x1B

/* KBDMFS register bit mask */
#define TC3589x_KBDMFS_EN	0x1

/* CLKEN register bitmask */
#define KPD_CLK_EN		0x1

/* RSTINTCLR register bit mask */
#define IRQ_CLEAR		0x1

/* bit masks for keyboard interrupts*/
#define TC3589x_EVT_LOSS_INT	0x8
#define TC3589x_EVT_INT		0x4
#define TC3589x_KBD_LOSS_INT	0x2
#define TC3589x_KBD_INT		0x1

/* bit masks for keyboard interrupt clear*/
#define TC3589x_EVT_INT_CLR	0x2
#define TC3589x_KBD_INT_CLR	0x1

/**
 * struct tc_keypad - data structure used by keypad driver
 * @tc3589x:    pointer to tc35893
 * @input:      pointer to input device object
 * @board:      keypad platform device
 * @krow:	number of rows
 * @kcol:	number of columns
 * @keymap:     matrix scan code table for keycodes
 * @keypad_stopped: holds keypad status
 */
struct tc_keypad {
	struct tc3589x *tc3589x;
	struct input_dev *input;
	const struct tc3589x_keypad_platform_data *board;
	unsigned int krow;
	unsigned int kcol;
	unsigned short *keymap;
	bool keypad_stopped;
};

static int tc3589x_keypad_init_key_hardware(struct tc_keypad *keypad)
{
	int ret;
	struct tc3589x *tc3589x = keypad->tc3589x;
	const struct tc3589x_keypad_platform_data *board = keypad->board;

	/* validate platform configuration */
	if (board->kcol > TC3589x_MAX_KPCOL || board->krow > TC3589x_MAX_KPROW)
		return -EINVAL;

	/* configure KBDSIZE 4 LSbits for cols and 4 MSbits for rows */
	ret = tc3589x_reg_write(tc3589x, TC3589x_KBDSIZE,
			(board->krow << KP_ROW_SHIFT) | board->kcol);
	if (ret < 0)
		return ret;

	/* configure dedicated key config, no dedicated key selected */
	ret = tc3589x_reg_write(tc3589x, TC3589x_KBCFG_LSB, DEDICATED_KEY_VAL);
	if (ret < 0)
		return ret;

	ret = tc3589x_reg_write(tc3589x, TC3589x_KBCFG_MSB, DEDICATED_KEY_VAL);
	if (ret < 0)
		return ret;

	/* Configure settle time */
	ret = tc3589x_reg_write(tc3589x, TC3589x_KBDSETTLE_REG,
				board->settle_time);
	if (ret < 0)
		return ret;

	/* Configure debounce time */
	ret = tc3589x_reg_write(tc3589x, TC3589x_KBDBOUNCE,
				board->debounce_period);
	if (ret < 0)
		return ret;

	/* Start of initialise keypad GPIOs */
	ret = tc3589x_set_bits(tc3589x, TC3589x_IOCFG, 0x0, IOCFG_IG);
	if (ret < 0)
		return ret;

	/* Configure pull-up resistors for all row GPIOs */
	ret = tc3589x_reg_write(tc3589x, TC3589x_IOPULLCFG0_LSB,
					TC3589x_PULLUP_ALL_MASK);
	if (ret < 0)
		return ret;

	ret = tc3589x_reg_write(tc3589x, TC3589x_IOPULLCFG0_MSB,
					TC3589x_PULLUP_ALL_MASK);
	if (ret < 0)
		return ret;

	/* Configure pull-up resistors for all column GPIOs */
	ret = tc3589x_reg_write(tc3589x, TC3589x_IOPULLCFG1_LSB,
			TC3589x_PULLUP_ALL_MASK);
	if (ret < 0)
		return ret;

	ret = tc3589x_reg_write(tc3589x, TC3589x_IOPULLCFG1_MSB,
			TC3589x_PULLUP_ALL_MASK);
	if (ret < 0)
		return ret;

	ret = tc3589x_reg_write(tc3589x, TC3589x_IOPULLCFG2_LSB,
			TC3589x_PULLUP_ALL_MASK);

	return ret;
}

#define TC35893_DATA_REGS		4
#define TC35893_KEYCODE_FIFO_EMPTY	0x7f
#define TC35893_KEYCODE_FIFO_CLEAR	0xff
#define TC35893_KEYPAD_ROW_SHIFT	0x3

static irqreturn_t tc3589x_keypad_irq(int irq, void *dev)
{
	struct tc_keypad *keypad = dev;
	struct tc3589x *tc3589x = keypad->tc3589x;
	u8 i, row_index, col_index, kbd_code, up;
	u8 code;

	for (i = 0; i < TC35893_DATA_REGS * 2; i++) {
		kbd_code = tc3589x_reg_read(tc3589x, TC3589x_EVTCODE_FIFO);

		/* loop till fifo is empty and no more keys are pressed */
		if (kbd_code == TC35893_KEYCODE_FIFO_EMPTY ||
				kbd_code == TC35893_KEYCODE_FIFO_CLEAR)
			continue;

		/* valid key is found */
		col_index = kbd_code & KP_EVCODE_COL_MASK;
		row_index = (kbd_code & KP_EVCODE_ROW_MASK) >> KP_ROW_SHIFT;
		code = MATRIX_SCAN_CODE(row_index, col_index,
						TC35893_KEYPAD_ROW_SHIFT);
		up = kbd_code & KP_RELEASE_EVT_MASK;

		input_event(keypad->input, EV_MSC, MSC_SCAN, code);
		input_report_key(keypad->input, keypad->keymap[code], !up);
		input_sync(keypad->input);
	}

	/* clear IRQ */
	tc3589x_set_bits(tc3589x, TC3589x_KBDIC,
			0x0, TC3589x_EVT_INT_CLR | TC3589x_KBD_INT_CLR);
	/* enable IRQ */
	tc3589x_set_bits(tc3589x, TC3589x_KBDMSK,
			0x0, TC3589x_EVT_LOSS_INT | TC3589x_EVT_INT);

	return IRQ_HANDLED;
}

static int tc3589x_keypad_enable(struct tc_keypad *keypad)
{
	struct tc3589x *tc3589x = keypad->tc3589x;
	int ret;

	/* pull the keypad module out of reset */
	ret = tc3589x_set_bits(tc3589x, TC3589x_RSTCTRL, TC3589x_KBDRST, 0x0);
	if (ret < 0)
		return ret;

	/* configure KBDMFS */
	ret = tc3589x_set_bits(tc3589x, TC3589x_KBDMFS, 0x0, TC3589x_KBDMFS_EN);
	if (ret < 0)
		return ret;

	/* enable the keypad clock */
	ret = tc3589x_set_bits(tc3589x, TC3589x_CLKEN, 0x0, KPD_CLK_EN);
	if (ret < 0)
		return ret;

	/* clear pending IRQs */
	ret =  tc3589x_set_bits(tc3589x, TC3589x_RSTINTCLR, 0x0, 0x1);
	if (ret < 0)
		return ret;

	/* enable the IRQs */
	ret = tc3589x_set_bits(tc3589x, TC3589x_KBDMSK, 0x0,
					TC3589x_EVT_LOSS_INT | TC3589x_EVT_INT);
	if (ret < 0)
		return ret;

	keypad->keypad_stopped = false;

	return ret;
}

static int tc3589x_keypad_disable(struct tc_keypad *keypad)
{
	struct tc3589x *tc3589x = keypad->tc3589x;
	int ret;

	/* clear IRQ */
	ret = tc3589x_set_bits(tc3589x, TC3589x_KBDIC,
			0x0, TC3589x_EVT_INT_CLR | TC3589x_KBD_INT_CLR);
	if (ret < 0)
		return ret;

	/* disable all interrupts */
	ret = tc3589x_set_bits(tc3589x, TC3589x_KBDMSK,
			~(TC3589x_EVT_LOSS_INT | TC3589x_EVT_INT), 0x0);
	if (ret < 0)
		return ret;

	/* disable the keypad module */
	ret = tc3589x_set_bits(tc3589x, TC3589x_CLKEN, 0x1, 0x0);
	if (ret < 0)
		return ret;

	/* put the keypad module into reset */
	ret = tc3589x_set_bits(tc3589x, TC3589x_RSTCTRL, TC3589x_KBDRST, 0x1);

	keypad->keypad_stopped = true;

	return ret;
}

static int tc3589x_keypad_open(struct input_dev *input)
{
	int error;
	struct tc_keypad *keypad = input_get_drvdata(input);

	/* enable the keypad module */
	error = tc3589x_keypad_enable(keypad);
	if (error < 0) {
		dev_err(&input->dev, "failed to enable keypad module\n");
		return error;
	}

	error = tc3589x_keypad_init_key_hardware(keypad);
	if (error < 0) {
		dev_err(&input->dev, "failed to configure keypad module\n");
		return error;
	}

	return 0;
}

static void tc3589x_keypad_close(struct input_dev *input)
{
	struct tc_keypad *keypad = input_get_drvdata(input);

	/* disable the keypad module */
	tc3589x_keypad_disable(keypad);
}

static int tc3589x_keypad_probe(struct platform_device *pdev)
{
	struct tc3589x *tc3589x = dev_get_drvdata(pdev->dev.parent);
	struct tc_keypad *keypad;
	struct input_dev *input;
	const struct tc3589x_keypad_platform_data *plat;
	int error, irq;

	plat = tc3589x->pdata->keypad;
	if (!plat) {
		dev_err(&pdev->dev, "invalid keypad platform data\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	keypad = kzalloc(sizeof(struct tc_keypad), GFP_KERNEL);
	input = input_allocate_device();
	if (!keypad || !input) {
		dev_err(&pdev->dev, "failed to allocate keypad memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	keypad->board = plat;
	keypad->input = input;
	keypad->tc3589x = tc3589x;

	input->id.bustype = BUS_I2C;
	input->name = pdev->name;
	input->dev.parent = &pdev->dev;

	input->open = tc3589x_keypad_open;
	input->close = tc3589x_keypad_close;

	error = matrix_keypad_build_keymap(plat->keymap_data, NULL,
					   TC3589x_MAX_KPROW, TC3589x_MAX_KPCOL,
					   NULL, input);
	if (error) {
		dev_err(&pdev->dev, "Failed to build keymap\n");
		goto err_free_mem;
	}

	keypad->keymap = input->keycode;

	input_set_capability(input, EV_MSC, MSC_SCAN);
	if (!plat->no_autorepeat)
		__set_bit(EV_REP, input->evbit);

	input_set_drvdata(input, keypad);

	error = request_threaded_irq(irq, NULL,
			tc3589x_keypad_irq, plat->irqtype,
			"tc3589x-keypad", keypad);
	if (error < 0) {
		dev_err(&pdev->dev,
				"Could not allocate irq %d,error %d\n",
				irq, error);
		goto err_free_mem;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "Could not register input device\n");
		goto err_free_irq;
	}

	/* let platform decide if keypad is a wakeup source or not */
	device_init_wakeup(&pdev->dev, plat->enable_wakeup);
	device_set_wakeup_capable(&pdev->dev, plat->enable_wakeup);

	platform_set_drvdata(pdev, keypad);

	return 0;

err_free_irq:
	free_irq(irq, keypad);
err_free_mem:
	input_free_device(input);
	kfree(keypad);
	return error;
}

static int tc3589x_keypad_remove(struct platform_device *pdev)
{
	struct tc_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (!keypad->keypad_stopped)
		tc3589x_keypad_disable(keypad);

	free_irq(irq, keypad);

	input_unregister_device(keypad->input);

	kfree(keypad);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tc3589x_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tc_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	/* keypad is already off; we do nothing */
	if (keypad->keypad_stopped)
		return 0;

	/* if device is not a wakeup source, disable it for powersave */
	if (!device_may_wakeup(&pdev->dev))
		tc3589x_keypad_disable(keypad);
	else
		enable_irq_wake(irq);

	return 0;
}

static int tc3589x_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tc_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (!keypad->keypad_stopped)
		return 0;

	/* enable the device to resume normal operations */
	if (!device_may_wakeup(&pdev->dev))
		tc3589x_keypad_enable(keypad);
	else
		disable_irq_wake(irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tc3589x_keypad_dev_pm_ops,
			 tc3589x_keypad_suspend, tc3589x_keypad_resume);

static struct platform_driver tc3589x_keypad_driver = {
	.driver	= {
		.name	= "tc3589x-keypad",
		.owner	= THIS_MODULE,
		.pm	= &tc3589x_keypad_dev_pm_ops,
	},
	.probe	= tc3589x_keypad_probe,
	.remove	= tc3589x_keypad_remove,
};
module_platform_driver(tc3589x_keypad_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jayeeta Banerjee/Sundar Iyer");
MODULE_DESCRIPTION("TC35893 Keypad Driver");
MODULE_ALIAS("platform:tc3589x-keypad");
