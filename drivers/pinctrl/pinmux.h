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

int pinmux_validate_map(struct pinctrl_map const *map, int i);

int pinmux_request_gpio(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned pin, unsigned gpio);
void pinmux_free_gpio(struct pinctrl_dev *pctldev, unsigned pin,
		      struct pinctrl_gpio_range *range);
int pinmux_gpio_direction(struct pinctrl_dev *pctldev,
			  struct pinctrl_gpio_range *range,
			  unsigned pin, bool input);

int pinmux_map_to_setting(struct pinctrl_map const *map,
			  struct pinctrl_setting *setting);
void pinmux_free_setting(struct pinctrl_setting const *setting);
int pinmux_enable_setting(struct pinctrl_setting const *setting);
void pinmux_disable_setting(struct pinctrl_setting const *setting);

void pinmux_show_map(struct seq_file *s, struct pinctrl_map const *map);
void pinmux_show_setting(struct seq_file *s,
			 struct pinctrl_setting const *setting);
void pinmux_init_device_debugfs(struct dentry *devroot,
				struct pinctrl_dev *pctldev);

#else

static inline int pinmux_check_ops(struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline int pinmux_validate_map(struct pinctrl_map const *map, int i)
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

static inline int pinmux_map_to_setting(struct pinctrl_map const *map,
			  struct pinctrl_setting *setting)
{
	return 0;
}

static inline void pinmux_free_setting(struct pinctrl_setting const *setting)
{
}

static inline int pinmux_enable_setting(struct pinctrl_setting const *setting)
{
	return 0;
}

static inline void pinmux_disable_setting(
			struct pinctrl_setting const *setting)
{
}

static inline void pinmux_show_map(struct seq_file *s,
				   struct pinctrl_map const *map)
{
}

static inline void pinmux_show_setting(struct seq_file *s,
				       struct pinctrl_setting const *setting)
{
}

static inline void pinmux_init_device_debugfs(struct dentry *devroot,
					      struct pinctrl_dev *pctldev)
{
}

#endif
