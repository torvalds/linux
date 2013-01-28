#ifndef PINCTRL_PINCTRL_ABx5O0_H
#define PINCTRL_PINCTRL_ABx500_H

/* Package definitions */
#define PINCTRL_AB8500	0
#define PINCTRL_AB8540	1
#define PINCTRL_AB9540	2
#define PINCTRL_AB8505	3

/* pins alternate function */
enum abx500_pin_func {
	ABX500_DEFAULT,
	ABX500_ALT_A,
	ABX500_ALT_B,
	ABX500_ALT_C,
};

/**
 * struct abx500_function - ABx500 pinctrl mux function
 * @name: The name of the function, exported to pinctrl core.
 * @groups: An array of pin groups that may select this function.
 * @ngroups: The number of entries in @groups.
 */
struct abx500_function {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
};

/**
 * struct abx500_pingroup - describes a ABx500 pin group
 * @name: the name of this specific pin group
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @num_pins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @altsetting: the altsetting to apply to all pins in this group to
 *	configure them to be used by a function
 */
struct abx500_pingroup {
	const char *name;
	const unsigned int *pins;
	const unsigned npins;
	int altsetting;
};

#define ALTERNATE_FUNCTIONS(pin, sel_bit, alt1, alt2, alta, altb, altc)	\
{									\
	.pin_number = pin,						\
	.gpiosel_bit = sel_bit,						\
	.alt_bit1 = alt1,						\
	.alt_bit2 = alt2,						\
	.alta_val = alta,						\
	.altb_val = altb,						\
	.altc_val = altc,						\
}

#define UNUSED -1
/**
 * struct alternate_functions
 * @pin_number:		The pin number
 * @gpiosel_bit:	Control bit in GPIOSEL register,
 * @alt_bit1:		First AlternateFunction bit used to select the
 *			alternate function
 * @alt_bit2:		Second AlternateFunction bit used to select the
 *			alternate function
 *
 *			these 3 following fields are necessary due to none
 *			coherency on how to select the altA, altB and altC
 *			function between the ABx500 SOC family when using
 *			alternatfunc register.
 * @alta_val:		value to write in alternatfunc to select altA function
 * @altb_val:		value to write in alternatfunc to select altB function
 * @altc_val:		value to write in alternatfunc to select altC function
 */
struct alternate_functions {
	unsigned pin_number;
	s8 gpiosel_bit;
	s8 alt_bit1;
	s8 alt_bit2;
	u8 alta_val;
	u8 altb_val;
	u8 altc_val;
};

/**
 * struct pullud - specific pull up/down feature
 * @first_pin:		The pin number of the first pins which support
 *			specific pull up/down
 * @last_pin:		The pin number of the last pins
 */
struct pullud {
	unsigned first_pin;
	unsigned last_pin;
};

#define GPIO_IRQ_CLUSTER(a, b, c)	\
{					\
	.start = a,			\
	.end = b,			\
	.offset = c,			\
}

/**
 * struct abx500_gpio_irq_cluster - indicates GPIOs which are interrupt
 *			capable
 * @start:		The pin number of the first pin interrupt capable
 * @end:		The pin number of the last pin interrupt capable
 * @offset:		offset used to compute specific setting strategy of
 *			the interrupt line
 */

struct abx500_gpio_irq_cluster {
	int start;
	int end;
	int offset;
};

/**
 * struct abx500_pinrange - map pin numbers to GPIO offsets
 * @offset:		offset into the GPIO local numberspace, incidentally
 *			identical to the offset into the local pin numberspace
 * @npins:		number of pins to map from both offsets
 * @altfunc:		altfunc setting to be used to enable GPIO on a pin in
 *			this range (may vary)
 */
struct abx500_pinrange {
	unsigned int offset;
	unsigned int npins;
	int altfunc;
};

#define ABX500_PINRANGE(a, b, c) { .offset = a, .npins = b, .altfunc = c }

/**
 * struct abx500_pinctrl_soc_data - ABx500 pin controller per-SoC configuration
 * @gpio_ranges:	An array of GPIO ranges for this SoC
 * @gpio_num_ranges:	The number of GPIO ranges for this SoC
 * @pins:		An array describing all pins the pin controller affects.
 *			All pins which are also GPIOs must be listed first within the
 *			array, and be numbered identically to the GPIO controller's
 *			numbering.
 * @npins:		The number of entries in @pins.
 * @functions:		The functions supported on this SoC.
 * @nfunction:		The number of entries in @functions.
 * @groups:		An array describing all pin groups the pin SoC supports.
 * @ngroups:		The number of entries in @groups.
 * @alternate_functions: array describing pins which supports alternate and
 *			how to set it.
 * @pullud:		array describing pins which supports pull up/down
 *			specific registers.
 * @gpio_irq_cluster:	An array of GPIO interrupt capable for this SoC
 * @ngpio_irq_cluster:	The number of GPIO inetrrupt capable for this SoC
 * @irq_gpio_rising_offset: Interrupt offset used as base to compute specific
 *			setting strategy of the rising interrupt line
 * @irq_gpio_falling_offset: Interrupt offset used as base to compute specific
 *			setting strategy of the falling interrupt line
 * @irq_gpio_factor:	Factor used to compute specific setting strategy of
 *			the interrupt line
 */

struct abx500_pinctrl_soc_data {
	const struct abx500_pinrange *gpio_ranges;
	unsigned gpio_num_ranges;
	const struct pinctrl_pin_desc *pins;
	unsigned npins;
	const struct abx500_function *functions;
	unsigned nfunctions;
	const struct abx500_pingroup *groups;
	unsigned ngroups;
	struct alternate_functions *alternate_functions;
	struct pullud *pullud;
	struct abx500_gpio_irq_cluster *gpio_irq_cluster;
	unsigned ngpio_irq_cluster;
	int irq_gpio_rising_offset;
	int irq_gpio_falling_offset;
	int irq_gpio_factor;
};

#ifdef CONFIG_PINCTRL_AB8500

void abx500_pinctrl_ab8500_init(struct abx500_pinctrl_soc_data **soc);

#else

static inline void
abx500_pinctrl_ab8500_init(struct abx500_pinctrl_soc_data **soc)
{
}

#endif

#ifdef CONFIG_PINCTRL_AB8540

void abx500_pinctrl_ab8540_init(struct abx500_pinctrl_soc_data **soc);

#else

static inline void
abx500_pinctrl_ab8540_init(struct abx500_pinctrl_soc_data **soc)
{
}

#endif

#ifdef CONFIG_PINCTRL_AB9540

void abx500_pinctrl_ab9540_init(struct abx500_pinctrl_soc_data **soc);

#else

static inline void
abx500_pinctrl_ab9540_init(struct abx500_pinctrl_soc_data **soc)
{
}

#endif

#ifdef CONFIG_PINCTRL_AB8505

void abx500_pinctrl_ab8505_init(struct abx500_pinctrl_soc_data **soc);

#else

static inline void
abx500_pinctrl_ab8505_init(struct abx500_pinctrl_soc_data **soc)
{
}

#endif

#endif /* PINCTRL_PINCTRL_ABx500_H */
