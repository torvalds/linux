/*
 * Internal interface between the core pin control system and the
 * pin config portions
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifdef CONFIG_PINCONF

int pinconf_check_ops(struct pinctrl_dev *pctldev);
void pinconf_init_device_debugfs(struct dentry *devroot,
				 struct pinctrl_dev *pctldev);
int pin_config_get_for_pin(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *config);
int pin_config_set_for_pin(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long config);

#else

static inline int pinconf_check_ops(struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline void pinconf_init_device_debugfs(struct dentry *devroot,
					       struct pinctrl_dev *pctldev)
{
}

#endif
