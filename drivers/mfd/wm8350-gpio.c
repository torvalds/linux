// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm8350-core.c  --  Device access for Wolfson WM8350
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>

#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/gpio.h>
#include <linux/mfd/wm8350/pmic.h>

static int gpio_set_dir(struct wm8350 *wm8350, int gpio, int dir)
{
	int ret;

	wm8350_reg_unlock(wm8350);
	if (dir == WM8350_GPIO_DIR_OUT)
		ret = wm8350_clear_bits(wm8350,
					WM8350_GPIO_CONFIGURATION_I_O,
					1 << gpio);
	else
		ret = wm8350_set_bits(wm8350,
				      WM8350_GPIO_CONFIGURATION_I_O,
				      1 << gpio);
	wm8350_reg_lock(wm8350);
	return ret;
}

static int wm8350_gpio_set_debounce(struct wm8350 *wm8350, int gpio, int db)
{
	if (db == WM8350_GPIO_DEBOUNCE_ON)
		return wm8350_set_bits(wm8350, WM8350_GPIO_DEBOUNCE,
				       1 << gpio);
	else
		return wm8350_clear_bits(wm8350,
					 WM8350_GPIO_DEBOUNCE, 1 << gpio);
}

static int gpio_set_func(struct wm8350 *wm8350, int gpio, int func)
{
	u16 reg;

	wm8350_reg_unlock(wm8350);
	switch (gpio) {
	case 0:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_1)
		    & ~WM8350_GP0_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_1,
				 reg | ((func & 0xf) << 0));
		break;
	case 1:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_1)
		    & ~WM8350_GP1_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_1,
				 reg | ((func & 0xf) << 4));
		break;
	case 2:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_1)
		    & ~WM8350_GP2_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_1,
				 reg | ((func & 0xf) << 8));
		break;
	case 3:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_1)
		    & ~WM8350_GP3_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_1,
				 reg | ((func & 0xf) << 12));
		break;
	case 4:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_2)
		    & ~WM8350_GP4_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_2,
				 reg | ((func & 0xf) << 0));
		break;
	case 5:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_2)
		    & ~WM8350_GP5_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_2,
				 reg | ((func & 0xf) << 4));
		break;
	case 6:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_2)
		    & ~WM8350_GP6_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_2,
				 reg | ((func & 0xf) << 8));
		break;
	case 7:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_2)
		    & ~WM8350_GP7_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_2,
				 reg | ((func & 0xf) << 12));
		break;
	case 8:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_3)
		    & ~WM8350_GP8_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_3,
				 reg | ((func & 0xf) << 0));
		break;
	case 9:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_3)
		    & ~WM8350_GP9_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_3,
				 reg | ((func & 0xf) << 4));
		break;
	case 10:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_3)
		    & ~WM8350_GP10_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_3,
				 reg | ((func & 0xf) << 8));
		break;
	case 11:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_3)
		    & ~WM8350_GP11_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_3,
				 reg | ((func & 0xf) << 12));
		break;
	case 12:
		reg = wm8350_reg_read(wm8350, WM8350_GPIO_FUNCTION_SELECT_4)
		    & ~WM8350_GP12_FN_MASK;
		wm8350_reg_write(wm8350, WM8350_GPIO_FUNCTION_SELECT_4,
				 reg | ((func & 0xf) << 0));
		break;
	default:
		wm8350_reg_lock(wm8350);
		return -EINVAL;
	}

	wm8350_reg_lock(wm8350);
	return 0;
}

static int gpio_set_pull_up(struct wm8350 *wm8350, int gpio, int up)
{
	if (up)
		return wm8350_set_bits(wm8350,
				       WM8350_GPIO_PIN_PULL_UP_CONTROL,
				       1 << gpio);
	else
		return wm8350_clear_bits(wm8350,
					 WM8350_GPIO_PIN_PULL_UP_CONTROL,
					 1 << gpio);
}

static int gpio_set_pull_down(struct wm8350 *wm8350, int gpio, int down)
{
	if (down)
		return wm8350_set_bits(wm8350,
				       WM8350_GPIO_PULL_DOWN_CONTROL,
				       1 << gpio);
	else
		return wm8350_clear_bits(wm8350,
					 WM8350_GPIO_PULL_DOWN_CONTROL,
					 1 << gpio);
}

static int gpio_set_polarity(struct wm8350 *wm8350, int gpio, int pol)
{
	if (pol == WM8350_GPIO_ACTIVE_HIGH)
		return wm8350_set_bits(wm8350,
				       WM8350_GPIO_PIN_POLARITY_TYPE,
				       1 << gpio);
	else
		return wm8350_clear_bits(wm8350,
					 WM8350_GPIO_PIN_POLARITY_TYPE,
					 1 << gpio);
}

static int gpio_set_invert(struct wm8350 *wm8350, int gpio, int invert)
{
	if (invert == WM8350_GPIO_INVERT_ON)
		return wm8350_set_bits(wm8350, WM8350_GPIO_INT_MODE, 1 << gpio);
	else
		return wm8350_clear_bits(wm8350,
					 WM8350_GPIO_INT_MODE, 1 << gpio);
}

int wm8350_gpio_config(struct wm8350 *wm8350, int gpio, int dir, int func,
		       int pol, int pull, int invert, int debounce)
{
	/* make sure we never pull up and down at the same time */
	if (pull == WM8350_GPIO_PULL_NONE) {
		if (gpio_set_pull_up(wm8350, gpio, 0))
			goto err;
		if (gpio_set_pull_down(wm8350, gpio, 0))
			goto err;
	} else if (pull == WM8350_GPIO_PULL_UP) {
		if (gpio_set_pull_down(wm8350, gpio, 0))
			goto err;
		if (gpio_set_pull_up(wm8350, gpio, 1))
			goto err;
	} else if (pull == WM8350_GPIO_PULL_DOWN) {
		if (gpio_set_pull_up(wm8350, gpio, 0))
			goto err;
		if (gpio_set_pull_down(wm8350, gpio, 1))
			goto err;
	}

	if (gpio_set_invert(wm8350, gpio, invert))
		goto err;
	if (gpio_set_polarity(wm8350, gpio, pol))
		goto err;
	if (wm8350_gpio_set_debounce(wm8350, gpio, debounce))
		goto err;
	if (gpio_set_dir(wm8350, gpio, dir))
		goto err;
	return gpio_set_func(wm8350, gpio, func);

err:
	return -EIO;
}
EXPORT_SYMBOL_GPL(wm8350_gpio_config);
