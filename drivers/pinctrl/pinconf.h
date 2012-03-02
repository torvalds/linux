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

int pinconf_validate_map(struct pinctrl_map const *map, int i);

int pinconf_map_to_setting(struct pinctrl_map const *map,
			  struct pinctrl_setting *setting);
void pinconf_free_setting(struct pinctrl_setting const *setting);
int pinconf_apply_setting(struct pinctrl_setting const *setting);

void pinconf_show_map(struct seq_file *s, struct pinctrl_map const *map);
void pinconf_show_setting(struct seq_file *s,
			  struct pinctrl_setting const *setting);
void pinconf_init_device_debugfs(struct dentry *devroot,
				 struct pinctrl_dev *pctldev);

#else

static inline int pinconf_check_ops(struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline int pinconf_validate_map(struct pinctrl_map const *map, int i)
{
	return 0;
}

static inline int pinconf_map_to_setting(struct pinctrl_map const *map,
			  struct pinctrl_setting *setting)
{
	return 0;
}

static inline void pinconf_free_setting(struct pinctrl_setting const *setting)
{
}

static inline int pinconf_apply_setting(struct pinctrl_setting const *setting)
{
	return 0;
}

static inline void pinconf_show_map(struct seq_file *s,
				    struct pinctrl_map const *map)
{
}

static inline void pinconf_show_setting(struct seq_file *s,
			  struct pinctrl_setting const *setting)
{
}

static inline void pinconf_init_device_debugfs(struct dentry *devroot,
					       struct pinctrl_dev *pctldev)
{
}

#endif
