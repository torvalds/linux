/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Patrice Chotard <patrice.chotard@stericsson.com> for ST-Ericsson.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/mfd/abx500/ab8500.h>
#include "pinctrl-abx500.h"

/* All the pins that can be used for GPIO and some other functions */
#define ABX500_GPIO(offset)	(offset)

#define AB8505_PIN_N4		ABX500_GPIO(1)
#define AB8505_PIN_R5		ABX500_GPIO(2)
#define AB8505_PIN_P5		ABX500_GPIO(3)
/* hole */
#define AB8505_PIN_B16		ABX500_GPIO(10)
#define AB8505_PIN_B17		ABX500_GPIO(11)
/* hole */
#define AB8505_PIN_D17		ABX500_GPIO(13)
#define AB8505_PIN_C16		ABX500_GPIO(14)
/* hole */
#define AB8505_PIN_P2		ABX500_GPIO(17)
#define AB8505_PIN_N3		ABX500_GPIO(18)
#define AB8505_PIN_T1		ABX500_GPIO(19)
#define AB8505_PIN_P3		ABX500_GPIO(20)
/* hole */
#define AB8505_PIN_H14		ABX500_GPIO(34)
/* hole */
#define AB8505_PIN_J15		ABX500_GPIO(40)
#define AB8505_PIN_J14		ABX500_GPIO(41)
/* hole */
#define AB8505_PIN_L4		ABX500_GPIO(50)
/* hole */
#define AB8505_PIN_D16		ABX500_GPIO(52)
#define AB8505_PIN_D15		ABX500_GPIO(53)

/* indicates the higher GPIO number */
#define AB8505_GPIO_MAX_NUMBER	53

/*
 * The names of the pins are denoted by GPIO number and ball name, even
 * though they can be used for other things than GPIO, this is the first
 * column in the table of the data sheet and often used on schematics and
 * such.
 */
static const struct pinctrl_pin_desc ab8505_pins[] = {
	PINCTRL_PIN(AB8505_PIN_N4, "GPIO1_N4"),
	PINCTRL_PIN(AB8505_PIN_R5, "GPIO2_R5"),
	PINCTRL_PIN(AB8505_PIN_P5, "GPIO3_P5"),
/* hole */
	PINCTRL_PIN(AB8505_PIN_B16, "GPIO10_B16"),
	PINCTRL_PIN(AB8505_PIN_B17, "GPIO11_B17"),
/* hole */
	PINCTRL_PIN(AB8505_PIN_D17, "GPIO13_D17"),
	PINCTRL_PIN(AB8505_PIN_C16, "GPIO14_C16"),
/* hole */
	PINCTRL_PIN(AB8505_PIN_P2, "GPIO17_P2"),
	PINCTRL_PIN(AB8505_PIN_N3, "GPIO18_N3"),
	PINCTRL_PIN(AB8505_PIN_T1, "GPIO19_T1"),
	PINCTRL_PIN(AB8505_PIN_P3, "GPIO20_P3"),
/* hole */
	PINCTRL_PIN(AB8505_PIN_H14, "GPIO34_H14"),
/* hole */
	PINCTRL_PIN(AB8505_PIN_J15, "GPIO40_J15"),
	PINCTRL_PIN(AB8505_PIN_J14, "GPIO41_J14"),
/* hole */
	PINCTRL_PIN(AB8505_PIN_L4, "GPIO50_L4"),
/* hole */
	PINCTRL_PIN(AB8505_PIN_D16, "GPIO52_D16"),
	PINCTRL_PIN(AB8505_PIN_D15, "GPIO53_D15"),
};

/*
 * Maps local GPIO offsets to local pin numbers
 */
static const struct abx500_pinrange ab8505_pinranges[] = {
	ABX500_PINRANGE(1, 3, ABX500_ALT_A),
	ABX500_PINRANGE(10, 2, ABX500_DEFAULT),
	ABX500_PINRANGE(13, 1, ABX500_DEFAULT),
	ABX500_PINRANGE(14, 1, ABX500_ALT_A),
	ABX500_PINRANGE(17, 4, ABX500_ALT_A),
	ABX500_PINRANGE(34, 1, ABX500_ALT_A),
	ABX500_PINRANGE(40, 2, ABX500_ALT_A),
	ABX500_PINRANGE(50, 1, ABX500_DEFAULT),
	ABX500_PINRANGE(52, 2, ABX500_ALT_A),
};

/*
 * Read the pin group names like this:
 * sysclkreq2_d_1 = first groups of pins for sysclkreq2 on default function
 *
 * The groups are arranged as sets per altfunction column, so we can
 * mux in one group at a time by selecting the same altfunction for them
 * all. When functions require pins on different altfunctions, you need
 * to combine several groups.
 */

/* default column */
static const unsigned sysclkreq2_d_1_pins[] = { AB8505_PIN_N4 };
static const unsigned sysclkreq3_d_1_pins[] = { AB8505_PIN_R5 };
static const unsigned sysclkreq4_d_1_pins[] = { AB8505_PIN_P5 };
static const unsigned gpio10_d_1_pins[] = { AB8505_PIN_B16 };
static const unsigned gpio11_d_1_pins[] = { AB8505_PIN_B17 };
static const unsigned gpio13_d_1_pins[] = { AB8505_PIN_D17 };
static const unsigned pwmout1_d_1_pins[] = { AB8505_PIN_C16 };
/* audio data interface 2*/
static const unsigned adi2_d_1_pins[] = { AB8505_PIN_P2, AB8505_PIN_N3,
					AB8505_PIN_T1, AB8505_PIN_P3 };
static const unsigned extcpena_d_1_pins[] = { AB8505_PIN_H14 };
/* modem SDA/SCL */
static const unsigned modsclsda_d_1_pins[] = { AB8505_PIN_J15, AB8505_PIN_J14 };
static const unsigned gpio50_d_1_pins[] = { AB8505_PIN_L4 };
static const unsigned resethw_d_1_pins[] = { AB8505_PIN_D16 };
static const unsigned service_d_1_pins[] = { AB8505_PIN_D15 };

/* Altfunction A column */
static const unsigned gpio1_a_1_pins[] = { AB8505_PIN_N4 };
static const unsigned gpio2_a_1_pins[] = { AB8505_PIN_R5 };
static const unsigned gpio3_a_1_pins[] = { AB8505_PIN_P5 };
static const unsigned hiqclkena_a_1_pins[] = { AB8505_PIN_B16 };
static const unsigned pdmclk_a_1_pins[] = { AB8505_PIN_B17 };
static const unsigned uarttxdata_a_1_pins[] = { AB8505_PIN_D17 };
static const unsigned gpio14_a_1_pins[] = { AB8505_PIN_C16 };
static const unsigned gpio17_a_1_pins[] = { AB8505_PIN_P2 };
static const unsigned gpio18_a_1_pins[] = { AB8505_PIN_N3 };
static const unsigned gpio19_a_1_pins[] = { AB8505_PIN_T1 };
static const unsigned gpio20_a_1_pins[] = { AB8505_PIN_P3 };
static const unsigned gpio34_a_1_pins[] = { AB8505_PIN_H14 };
static const unsigned gpio40_a_1_pins[] = { AB8505_PIN_J15 };
static const unsigned gpio41_a_1_pins[] = { AB8505_PIN_J14 };
static const unsigned uartrxdata_a_1_pins[] = { AB8505_PIN_J14 };
static const unsigned gpio50_a_1_pins[] = { AB8505_PIN_L4 };
static const unsigned gpio52_a_1_pins[] = { AB8505_PIN_D16 };
static const unsigned gpio53_a_1_pins[] = { AB8505_PIN_D15 };

/* Altfunction B colum */
static const unsigned pdmdata_b_1_pins[] = { AB8505_PIN_B16 };
static const unsigned extvibrapwm1_b_1_pins[] = { AB8505_PIN_D17 };
static const unsigned extvibrapwm2_b_1_pins[] = { AB8505_PIN_L4 };

/* Altfunction C column */
static const unsigned usbvdat_c_1_pins[] = { AB8505_PIN_D17 };

#define AB8505_PIN_GROUP(a, b) { .name = #a, .pins = a##_pins,		\
			.npins = ARRAY_SIZE(a##_pins), .altsetting = b }

static const struct abx500_pingroup ab8505_groups[] = {
	AB8505_PIN_GROUP(sysclkreq2_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(sysclkreq3_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(sysclkreq4_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(gpio10_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(gpio11_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(gpio13_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(pwmout1_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(adi2_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(extcpena_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(modsclsda_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(gpio50_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(resethw_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(service_d_1, ABX500_DEFAULT),
	AB8505_PIN_GROUP(gpio1_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio2_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio3_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(hiqclkena_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(pdmclk_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(uarttxdata_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio14_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio17_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio18_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio19_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio20_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio34_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio40_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio41_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(uartrxdata_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio52_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(gpio53_a_1, ABX500_ALT_A),
	AB8505_PIN_GROUP(pdmdata_b_1, ABX500_ALT_B),
	AB8505_PIN_GROUP(extvibrapwm1_b_1, ABX500_ALT_B),
	AB8505_PIN_GROUP(extvibrapwm2_b_1, ABX500_ALT_B),
	AB8505_PIN_GROUP(usbvdat_c_1, ABX500_ALT_C),
};

/* We use this macro to define the groups applicable to a function */
#define AB8505_FUNC_GROUPS(a, b...)	   \
static const char * const a##_groups[] = { b };

AB8505_FUNC_GROUPS(sysclkreq, "sysclkreq2_d_1", "sysclkreq3_d_1",
		"sysclkreq4_d_1");
AB8505_FUNC_GROUPS(gpio, "gpio1_a_1", "gpio2_a_1", "gpio3_a_1",
		"gpio10_d_1", "gpio11_d_1", "gpio13_d_1", "gpio14_a_1",
		"gpio17_a_1", "gpio18_a_1", "gpio19_a_1", "gpio20_a_1",
		"gpio34_a_1", "gpio40_a_1", "gpio41_a_1", "gpio50_d_1",
		"gpio52_a_1", "gpio53_a_1");
AB8505_FUNC_GROUPS(pwmout, "pwmout1_d_1");
AB8505_FUNC_GROUPS(adi2, "adi2_d_1");
AB8505_FUNC_GROUPS(extcpena, "extcpena_d_1");
AB8505_FUNC_GROUPS(modsclsda, "modsclsda_d_1");
AB8505_FUNC_GROUPS(resethw, "resethw_d_1");
AB8505_FUNC_GROUPS(service, "service_d_1");
AB8505_FUNC_GROUPS(hiqclkena, "hiqclkena_a_1");
AB8505_FUNC_GROUPS(pdm, "pdmclk_a_1", "pdmdata_b_1");
AB8505_FUNC_GROUPS(uartdata, "uarttxdata_a_1", "uartrxdata_a_1");
AB8505_FUNC_GROUPS(extvibra, "extvibrapwm1_b_1", "extvibrapwm2_b_1");
AB8505_FUNC_GROUPS(usbvdat, "usbvdat_c_1");

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

static const struct abx500_function ab8505_functions[] = {
	FUNCTION(sysclkreq),
	FUNCTION(gpio),
	FUNCTION(pwmout),
	FUNCTION(adi2),
	FUNCTION(extcpena),
	FUNCTION(modsclsda),
	FUNCTION(resethw),
	FUNCTION(service),
	FUNCTION(hiqclkena),
	FUNCTION(pdm),
	FUNCTION(uartdata),
	FUNCTION(extvibra),
	FUNCTION(extvibra),
	FUNCTION(usbvdat),
};

/*
 * this table translates what's is in the AB8505 specification regarding the
 * balls alternate functions (as for DB, default, ALT_A, ALT_B and ALT_C).
 * ALTERNATE_FUNCTIONS(GPIO_NUMBER, GPIOSEL bit, ALTERNATFUNC bit1,
 * ALTERNATEFUNC bit2, ALTA val, ALTB val, ALTC val),
 *
 * example :
 *
 *	ALTERNATE_FUNCTIONS(13,     4,      3,      4, 1, 0, 2),
 *	means that pin AB8505_PIN_D18 (pin 13) supports 4 mux (default/ALT_A,
 *	ALT_B and ALT_C), so GPIOSEL and ALTERNATFUNC registers are used to
 *	select the mux. ALTA, ALTB and ALTC val indicates values to write in
 *	ALTERNATFUNC register. We need to specifies these values as SOC
 *	designers didn't apply the same logic on how to select mux in the
 *	ABx500 family.
 *
 *	As this pins supports at least ALT_B mux, default mux is
 *	selected by writing 1 in GPIOSEL bit :
 *
 *		| GPIOSEL bit=4 | alternatfunc bit2=4 | alternatfunc bit1=3
 *	default	|       1       |          0          |          0
 *	alt_A	|       0       |          0          |          1
 *	alt_B	|       0       |          0          |          0
 *	alt_C	|       0       |          1          |          0
 *
 *	ALTERNATE_FUNCTIONS(1,      0, UNUSED, UNUSED),
 *	means that pin AB9540_PIN_R4 (pin 1) supports 2 mux, so only GPIOSEL
 *	register is used to select the mux. As this pins doesn't support at
 *	least ALT_B mux, default mux is by writing 0 in GPIOSEL bit :
 *
 *		| GPIOSEL bit=0 | alternatfunc bit2=  | alternatfunc bit1=
 *	default	|       0       |          0          |          0
 *	alt_A	|       1       |          0          |          0
 */

static struct
alternate_functions ab8505_alternate_functions[AB8505_GPIO_MAX_NUMBER + 1] = {
	ALTERNATE_FUNCTIONS(0, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO0 */
	ALTERNATE_FUNCTIONS(1,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO1, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(2,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO2, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(3,      2, UNUSED, UNUSED, 0, 0, 0), /* GPIO3, altA controlled by bit 2*/
	ALTERNATE_FUNCTIONS(4, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO4, bit 3 reserved */
	ALTERNATE_FUNCTIONS(5, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO5, bit 4 reserved */
	ALTERNATE_FUNCTIONS(6, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO6, bit 5 reserved */
	ALTERNATE_FUNCTIONS(7, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO7, bit 6 reserved */
	ALTERNATE_FUNCTIONS(8, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO8, bit 7 reserved */

	ALTERNATE_FUNCTIONS(9, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO9, bit 0 reserved */
	ALTERNATE_FUNCTIONS(10,      1,      0, UNUSED, 1, 0, 0), /* GPIO10, altA and altB controlled by bit 0 */
	ALTERNATE_FUNCTIONS(11,      2,      1, UNUSED, 0, 0, 0), /* GPIO11, altA controlled by bit 2 */
	ALTERNATE_FUNCTIONS(12, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO12, bit3 reserved */
	ALTERNATE_FUNCTIONS(13,      4,      3,      4, 1, 0, 2), /* GPIO13, altA altB and altC controlled by bit 3 and 4 */
	ALTERNATE_FUNCTIONS(14,      5, UNUSED, UNUSED, 0, 0, 0), /* GPIO14, altA controlled by bit 5 */
	ALTERNATE_FUNCTIONS(15, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO15, bit 6 reserved */
	ALTERNATE_FUNCTIONS(16, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO15, bit 7 reserved  */
	/*
	 * pins 17 to 20 are special case, only bit 0 is used to select
	 * alternate function for these 4 pins.
	 * bits 1 to 3 are reserved
	 */
	ALTERNATE_FUNCTIONS(17,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO17, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(18,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO18, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(19,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO19, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(20,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO20, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(21, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO21, bit 4 reserved */
	ALTERNATE_FUNCTIONS(22, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO22, bit 5 reserved */
	ALTERNATE_FUNCTIONS(23, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO23, bit 6 reserved */
	ALTERNATE_FUNCTIONS(24, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO24, bit 7 reserved */

	ALTERNATE_FUNCTIONS(25, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO25, bit 0 reserved */
	ALTERNATE_FUNCTIONS(26, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO26, bit 1 reserved */
	ALTERNATE_FUNCTIONS(27, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO27, bit 2 reserved */
	ALTERNATE_FUNCTIONS(28, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO28, bit 3 reserved */
	ALTERNATE_FUNCTIONS(29, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO29, bit 4 reserved */
	ALTERNATE_FUNCTIONS(30, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO30, bit 5 reserved */
	ALTERNATE_FUNCTIONS(31, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO31, bit 6 reserved */
	ALTERNATE_FUNCTIONS(32, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO32, bit 7 reserved */

	ALTERNATE_FUNCTIONS(33, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO33, bit 0 reserved */
	ALTERNATE_FUNCTIONS(34,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO34, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(35, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO35, bit 2 reserved */
	ALTERNATE_FUNCTIONS(36, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO36, bit 2 reserved */
	ALTERNATE_FUNCTIONS(37, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO37, bit 2 reserved */
	ALTERNATE_FUNCTIONS(38, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO38, bit 2 reserved */
	ALTERNATE_FUNCTIONS(39, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO39, bit 2 reserved */
	ALTERNATE_FUNCTIONS(40,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO40, altA controlled by bit 7*/

	ALTERNATE_FUNCTIONS(41,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO41, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(42, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO42, bit 1 reserved */
	ALTERNATE_FUNCTIONS(43, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO43, bit 2 reserved */
	ALTERNATE_FUNCTIONS(44, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO44, bit 3 reserved */
	ALTERNATE_FUNCTIONS(45, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO45, bit 4 reserved */
	ALTERNATE_FUNCTIONS(46, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO46, bit 5 reserved */
	ALTERNATE_FUNCTIONS(47, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO47, bit 6 reserved */
	ALTERNATE_FUNCTIONS(48, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO48, bit 7 reserved */

	ALTERNATE_FUNCTIONS(49, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO49, bit 0 reserved */
	ALTERNATE_FUNCTIONS(50,	     1,      2, UNUSED, 1, 0, 0), /* GPIO50, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(51, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO49, bit 0 reserved */
	ALTERNATE_FUNCTIONS(52,	     3, UNUSED, UNUSED, 0, 0, 0), /* GPIO52, altA controlled by bit 3 */
	ALTERNATE_FUNCTIONS(53,	     4, UNUSED, UNUSED, 0, 0, 0), /* GPIO53, altA controlled by bit 4 */
};

/*
 * For AB8505 Only some GPIOs are interrupt capable, and they are
 * organized in discontiguous clusters:
 *
 *	GPIO10 to GPIO11
 *	GPIO13
 *	GPIO40 and GPIO41
 *	GPIO50
 *	GPIO52 to GPIO53
 */
static struct abx500_gpio_irq_cluster ab8505_gpio_irq_cluster[] = {
	GPIO_IRQ_CLUSTER(10, 11, AB8500_INT_GPIO10R),
	GPIO_IRQ_CLUSTER(13, 13, AB8500_INT_GPIO13R),
	GPIO_IRQ_CLUSTER(40, 41, AB8500_INT_GPIO40R),
	GPIO_IRQ_CLUSTER(50, 50, AB9540_INT_GPIO50R),
	GPIO_IRQ_CLUSTER(52, 53, AB9540_INT_GPIO52R),
};

static struct abx500_pinctrl_soc_data ab8505_soc = {
	.gpio_ranges = ab8505_pinranges,
	.gpio_num_ranges = ARRAY_SIZE(ab8505_pinranges),
	.pins = ab8505_pins,
	.npins = ARRAY_SIZE(ab8505_pins),
	.functions = ab8505_functions,
	.nfunctions = ARRAY_SIZE(ab8505_functions),
	.groups = ab8505_groups,
	.ngroups = ARRAY_SIZE(ab8505_groups),
	.alternate_functions = ab8505_alternate_functions,
	.gpio_irq_cluster = ab8505_gpio_irq_cluster,
	.ngpio_irq_cluster = ARRAY_SIZE(ab8505_gpio_irq_cluster),
	.irq_gpio_rising_offset = AB8500_INT_GPIO6R,
	.irq_gpio_falling_offset = AB8500_INT_GPIO6F,
	.irq_gpio_factor = 1,
};

void
abx500_pinctrl_ab8505_init(struct abx500_pinctrl_soc_data **soc)
{
	*soc = &ab8505_soc;
}
