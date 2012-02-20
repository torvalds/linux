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

int pinmux_check_ops(struct pinctrl_dev *pctldev);
int pinmux_request_gpio(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned pin, unsigned gpio);
void pinmux_free_gpio(struct pinctrl_dev *pctldev, unsigned pin,
		      struct pinctrl_gpio_range *range);
int pinmux_gpio_direction(struct pinctrl_dev *pctldev,
			  struct pinctrl_gpio_range *range,
			  unsigned pin, bool input);
static inline void pinmux_init_pinctrl_handle(struct pinctrl *p)
{
	p->func_selector = UINT_MAX;
	INIT_LIST_HEAD(&p->groups);
}
int pinmux_apply_muxmap(struct pinctrl_dev *pctldev,
			struct pinctrl *p,
			struct device *dev,
			const char *devname,
			struct pinctrl_map const *map);
void pinmux_put(struct pinctrl *p);
int pinmux_enable(struct pinctrl *p);
void pinmux_disable(struct pinctrl *p);
void pinmux_init_device_debugfs(struct dentry *devroot,
				struct pinctrl_dev *pctldev);
void pinmux_dbg_show(struct seq_file *s, struct pinctrl *p);

#else

static inline int pinmux_check_ops(struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline int pinmux_request_gpio(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned pin, unsigned gpio)
{
	return 0;
}

static inline void pinmux_free_gpio(struct pinctrl_dev *pctldev,
				    unsigned pin,
				    struct pinctrl_gpio_range *range)
{
}

static inline int pinmux_gpio_direction(struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned pin, bool input)
{
	return 0;
}

static inline void pinmux_init_pinctrl_handle(struct pinctrl *p)
{
}

static inline int pinmux_apply_muxmap(struct pinctrl_dev *pctldev,
				      struct pinctrl *p,
				      struct device *dev,
				      const char *devname,
				      struct pinctrl_map const *map)
{
	return 0;
}

static inline void pinmux_put(struct pinctrl *p)
{
}

static inline int pinmux_enable(struct pinctrl *p)
{
}

static inline void pinmux_disable(struct pinctrl *p)
{
}

static inline void pinmux_init_device_debugfs(struct dentry *devroot,
					      struct pinctrl_dev *pctldev)
{
}

static inline void pinmux_dbg_show(struct seq_file *s, struct pinctrl *p)
{
}

#endif
