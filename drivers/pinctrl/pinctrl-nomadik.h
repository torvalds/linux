#ifndef PINCTRL_PINCTRL_NOMADIK_H
#define PINCTRL_PINCTRL_NOMADIK_H

#include <plat/gpio-nomadik.h>

/* Package definitions */
#define PINCTRL_NMK_STN8815	0
#define PINCTRL_NMK_DB8500	1

/**
 * struct nmk_function - Nomadik pinctrl mux function
 * @name: The name of the function, exported to pinctrl core.
 * @groups: An array of pin groups that may select this function.
 * @ngroups: The number of entries in @groups.
 */
struct nmk_function {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
};

/**
 * struct nmk_pingroup - describes a Nomadik pin group
 * @name: the name of this specific pin group
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @num_pins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @altsetting: the altsetting to apply to all pins in this group to
 *	configure them to be used by a function
 */
struct nmk_pingroup {
	const char *name;
	const unsigned int *pins;
	const unsigned npins;
	int altsetting;
};

/**
 * struct nmk_pinctrl_soc_data - Nomadik pin controller per-SoC configuration
 * @gpio_ranges: An array of GPIO ranges for this SoC
 * @gpio_num_ranges: The number of GPIO ranges for this SoC
 * @pins:	An array describing all pins the pin controller affects.
 *		All pins which are also GPIOs must be listed first within the
 *		array, and be numbered identically to the GPIO controller's
 *		numbering.
 * @npins:	The number of entries in @pins.
 * @functions:	The functions supported on this SoC.
 * @nfunction:	The number of entries in @functions.
 * @groups:	An array describing all pin groups the pin SoC supports.
 * @ngroups:	The number of entries in @groups.
 */
struct nmk_pinctrl_soc_data {
	struct pinctrl_gpio_range *gpio_ranges;
	unsigned gpio_num_ranges;
	const struct pinctrl_pin_desc *pins;
	unsigned npins;
	const struct nmk_function *functions;
	unsigned nfunctions;
	const struct nmk_pingroup *groups;
	unsigned ngroups;
};

#ifdef CONFIG_PINCTRL_DB8500

void nmk_pinctrl_db8500_init(const struct nmk_pinctrl_soc_data **soc);

#else

static inline void
nmk_pinctrl_db8500_init(const struct nmk_pinctrl_soc_data **soc)
{
}

#endif

#endif /* PINCTRL_PINCTRL_NOMADIK_H */
