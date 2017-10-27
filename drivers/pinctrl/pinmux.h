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

int pinmux_validate_map(const struct pinctrl_map *map, int i);

int pinmux_request_gpio(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned pin, unsigned gpio);
void pinmux_free_gpio(struct pinctrl_dev *pctldev, unsigned pin,
		      struct pinctrl_gpio_range *range);
int pinmux_gpio_direction(struct pinctrl_dev *pctldev,
			  struct pinctrl_gpio_range *range,
			  unsigned pin, bool input);

int pinmux_map_to_setting(const struct pinctrl_map *map,
			  struct pinctrl_setting *setting);
void pinmux_free_setting(const struct pinctrl_setting *setting);
int pinmux_enable_setting(const struct pinctrl_setting *setting);
void pinmux_disable_setting(const struct pinctrl_setting *setting);

#else

static inline int pinmux_check_ops(struct pinctrl_dev *pctldev)
{
	return 0;
}

static inline int pinmux_validate_map(const struct pinctrl_map *map, int i)
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

static inline int pinmux_map_to_setting(const struct pinctrl_map *map,
			  struct pinctrl_setting *setting)
{
	return 0;
}

static inline void pinmux_free_setting(const struct pinctrl_setting *setting)
{
}

static inline int pinmux_enable_setting(const struct pinctrl_setting *setting)
{
	return 0;
}

static inline void pinmux_disable_setting(const struct pinctrl_setting *setting)
{
}

#endif

#if defined(CONFIG_PINMUX) && defined(CONFIG_DEBUG_FS)

void pinmux_show_map(struct seq_file *s, const struct pinctrl_map *map);
void pinmux_show_setting(struct seq_file *s,
			 const struct pinctrl_setting *setting);
void pinmux_init_device_debugfs(struct dentry *devroot,
				struct pinctrl_dev *pctldev);

#else

static inline void pinmux_show_map(struct seq_file *s,
				   const struct pinctrl_map *map)
{
}

static inline void pinmux_show_setting(struct seq_file *s,
				       const struct pinctrl_setting *setting)
{
}

static inline void pinmux_init_device_debugfs(struct dentry *devroot,
					      struct pinctrl_dev *pctldev)
{
}

#endif

#ifdef CONFIG_GENERIC_PINMUX_FUNCTIONS

/**
 * struct function_desc - generic function descriptor
 * @name: name of the function
 * @group_names: array of pin group names
 * @num_group_names: number of pin group names
 * @data: pin controller driver specific data
 */
struct function_desc {
	const char *name;
	const char **group_names;
	int num_group_names;
	void *data;
};

int pinmux_generic_get_function_count(struct pinctrl_dev *pctldev);

const char *
pinmux_generic_get_function_name(struct pinctrl_dev *pctldev,
				 unsigned int selector);

int pinmux_generic_get_function_groups(struct pinctrl_dev *pctldev,
				       unsigned int selector,
				       const char * const **groups,
				       unsigned * const num_groups);

struct function_desc *pinmux_generic_get_function(struct pinctrl_dev *pctldev,
						  unsigned int selector);

int pinmux_generic_add_function(struct pinctrl_dev *pctldev,
				const char *name,
				const char **groups,
				unsigned const num_groups,
				void *data);

int pinmux_generic_remove_function(struct pinctrl_dev *pctldev,
				   unsigned int selector);

static inline int
pinmux_generic_remove_last_function(struct pinctrl_dev *pctldev)
{
	return pinmux_generic_remove_function(pctldev,
					      pctldev->num_functions - 1);
}

void pinmux_generic_free_functions(struct pinctrl_dev *pctldev);

#else

static inline void pinmux_generic_free_functions(struct pinctrl_dev *pctldev)
{
}

#endif /* CONFIG_GENERIC_PINMUX_FUNCTIONS */
