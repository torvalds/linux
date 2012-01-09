/*
 * Internal interface between the core pin control system and the
 * pinmux portions
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifdef CONFIG_PINMUX

int pinmux_check_ops(const struct pinmux_ops *ops);
void pinmux_init_device_debugfs(struct dentry *devroot,
				struct pinctrl_dev *pctldev);
void pinmux_init_debugfs(struct dentry *subsys_root);
int pinmux_hog_maps(struct pinctrl_dev *pctldev);
void pinmux_unhog_maps(struct pinctrl_dev *pctldev);

#else

static inline int pinmux_check_ops(const struct pinmux_ops *ops)
{
	return 0;
}

static inline void pinmux_init_device_debugfs(struct dentry *devroot,
					      struct pinctrl_dev *pctldev)
{
}

static inline void pinmux_init_debugfs(struct dentry *subsys_root)
{
}

static inline int pinmux_hog_maps(struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline void pinmux_unhog_maps(struct pinctrl_dev *pctldev)
{
}

#endif
