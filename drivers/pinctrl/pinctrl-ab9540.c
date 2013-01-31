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
#define ABX500_GPIO(offset)		(offset)

#define AB9540_PIN_R4		ABX500_GPIO(1)
#define AB9540_PIN_V3		ABX500_GPIO(2)
#define AB9540_PIN_T4		ABX500_GPIO(3)
#define AB9540_PIN_T5		ABX500_GPIO(4)
/* hole */
#define AB9540_PIN_B18		ABX500_GPIO(10)
#define AB9540_PIN_C18		ABX500_GPIO(11)
/* hole */
#define AB9540_PIN_D18		ABX500_GPIO(13)
#define AB9540_PIN_B19		ABX500_GPIO(14)
#define AB9540_PIN_C19		ABX500_GPIO(15)
#define AB9540_PIN_D19		ABX500_GPIO(16)
#define AB9540_PIN_R3		ABX500_GPIO(17)
#define AB9540_PIN_T2		ABX500_GPIO(18)
#define AB9540_PIN_U2		ABX500_GPIO(19)
#define AB9540_PIN_V2		ABX500_GPIO(20)
#define AB9540_PIN_N17		ABX500_GPIO(21)
#define AB9540_PIN_N16		ABX500_GPIO(22)
#define AB9540_PIN_M19		ABX500_GPIO(23)
#define AB9540_PIN_T3		ABX500_GPIO(24)
#define AB9540_PIN_W2		ABX500_GPIO(25)
/* hole */
#define AB9540_PIN_H4		ABX500_GPIO(27)
#define AB9540_PIN_F1		ABX500_GPIO(28)
#define AB9540_PIN_F4		ABX500_GPIO(29)
#define AB9540_PIN_F2		ABX500_GPIO(30)
#define AB9540_PIN_E4		ABX500_GPIO(31)
#define AB9540_PIN_F3		ABX500_GPIO(32)
/* hole */
#define AB9540_PIN_J13		ABX500_GPIO(34)
/* hole */
#define AB9540_PIN_L17		ABX500_GPIO(40)
#define AB9540_PIN_L16		ABX500_GPIO(41)
#define AB9540_PIN_W3		ABX500_GPIO(42)
#define AB9540_PIN_N4		ABX500_GPIO(50)
#define AB9540_PIN_G12		ABX500_GPIO(51)
#define AB9540_PIN_E17		ABX500_GPIO(52)
#define AB9540_PIN_D11		ABX500_GPIO(53)
#define AB9540_PIN_M18		ABX500_GPIO(54)

/* indicates the highest GPIO number */
#define AB9540_GPIO_MAX_NUMBER	54

/*
 * The names of the pins are denoted by GPIO number and ball name, even
 * though they can be used for other things than GPIO, this is the first
 * column in the table of the data sheet and often used on schematics and
 * such.
 */
static const struct pinctrl_pin_desc ab9540_pins[] = {
	PINCTRL_PIN(AB9540_PIN_R4, "GPIO1_R4"),
	PINCTRL_PIN(AB9540_PIN_V3, "GPIO2_V3"),
	PINCTRL_PIN(AB9540_PIN_T4, "GPIO3_T4"),
	PINCTRL_PIN(AB9540_PIN_T5, "GPIO4_T5"),
	/* hole */
	PINCTRL_PIN(AB9540_PIN_B18, "GPIO10_B18"),
	PINCTRL_PIN(AB9540_PIN_C18, "GPIO11_C18"),
	/* hole */
	PINCTRL_PIN(AB9540_PIN_D18, "GPIO13_D18"),
	PINCTRL_PIN(AB9540_PIN_B19, "GPIO14_B19"),
	PINCTRL_PIN(AB9540_PIN_C19, "GPIO15_C19"),
	PINCTRL_PIN(AB9540_PIN_D19, "GPIO16_D19"),
	PINCTRL_PIN(AB9540_PIN_R3, "GPIO17_R3"),
	PINCTRL_PIN(AB9540_PIN_T2, "GPIO18_T2"),
	PINCTRL_PIN(AB9540_PIN_U2, "GPIO19_U2"),
	PINCTRL_PIN(AB9540_PIN_V2, "GPIO20_V2"),
	PINCTRL_PIN(AB9540_PIN_N17, "GPIO21_N17"),
	PINCTRL_PIN(AB9540_PIN_N16, "GPIO22_N16"),
	PINCTRL_PIN(AB9540_PIN_M19, "GPIO23_M19"),
	PINCTRL_PIN(AB9540_PIN_T3, "GPIO24_T3"),
	PINCTRL_PIN(AB9540_PIN_W2, "GPIO25_W2"),
	/* hole */
	PINCTRL_PIN(AB9540_PIN_H4, "GPIO27_H4"),
	PINCTRL_PIN(AB9540_PIN_F1, "GPIO28_F1"),
	PINCTRL_PIN(AB9540_PIN_F4, "GPIO29_F4"),
	PINCTRL_PIN(AB9540_PIN_F2, "GPIO30_F2"),
	PINCTRL_PIN(AB9540_PIN_E4, "GPIO31_E4"),
	PINCTRL_PIN(AB9540_PIN_F3, "GPIO32_F3"),
	/* hole */
	PINCTRL_PIN(AB9540_PIN_J13, "GPIO34_J13"),
	/* hole */
	PINCTRL_PIN(AB9540_PIN_L17, "GPIO40_L17"),
	PINCTRL_PIN(AB9540_PIN_L16, "GPIO41_L16"),
	PINCTRL_PIN(AB9540_PIN_W3, "GPIO42_W3"),
	PINCTRL_PIN(AB9540_PIN_N4, "GPIO50_N4"),
	PINCTRL_PIN(AB9540_PIN_G12, "GPIO51_G12"),
	PINCTRL_PIN(AB9540_PIN_E17, "GPIO52_E17"),
	PINCTRL_PIN(AB9540_PIN_D11, "GPIO53_D11"),
	PINCTRL_PIN(AB9540_PIN_M18, "GPIO60_M18"),
};

/*
 * Maps local GPIO offsets to local pin numbers
 */
static const struct abx500_pinrange ab9540_pinranges[] = {
	ABX500_PINRANGE(1, 4, ABX500_ALT_A),
	ABX500_PINRANGE(10, 2, ABX500_DEFAULT),
	ABX500_PINRANGE(13, 1, ABX500_DEFAULT),
	ABX500_PINRANGE(14, 12, ABX500_ALT_A),
	ABX500_PINRANGE(27, 6, ABX500_ALT_A),
	ABX500_PINRANGE(34, 1, ABX500_ALT_A),
	ABX500_PINRANGE(40, 3, ABX500_ALT_A),
	ABX500_PINRANGE(50, 1, ABX500_DEFAULT),
	ABX500_PINRANGE(51, 3, ABX500_ALT_A),
	ABX500_PINRANGE(54, 1, ABX500_DEFAULT),
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
static const unsigned sysclkreq2_d_1_pins[] = { AB9540_PIN_R4 };
static const unsigned sysclkreq3_d_1_pins[] = { AB9540_PIN_V3 };
static const unsigned sysclkreq4_d_1_pins[] = { AB9540_PIN_T4 };
static const unsigned sysclkreq6_d_1_pins[] = { AB9540_PIN_T5 };
static const unsigned gpio10_d_1_pins[] = { AB9540_PIN_B18 };
static const unsigned gpio11_d_1_pins[] = { AB9540_PIN_C18 };
static const unsigned gpio13_d_1_pins[] = { AB9540_PIN_D18 };
static const unsigned pwmout1_d_1_pins[] = { AB9540_PIN_B19 };
static const unsigned pwmout2_d_1_pins[] = { AB9540_PIN_C19 };
static const unsigned pwmout3_d_1_pins[] = { AB9540_PIN_D19 };
/* audio data interface 1*/
static const unsigned adi1_d_1_pins[] = { AB9540_PIN_R3, AB9540_PIN_T2,
					AB9540_PIN_U2, AB9540_PIN_V2 };
/* USBUICC */
static const unsigned usbuicc_d_1_pins[] = { AB9540_PIN_N17, AB9540_PIN_N16,
					AB9540_PIN_M19 };
static const unsigned sysclkreq7_d_1_pins[] = { AB9540_PIN_T3 };
static const unsigned sysclkreq8_d_1_pins[] = { AB9540_PIN_W2 };
/* Digital microphone 1 and 2 */
static const unsigned dmic12_d_1_pins[] = { AB9540_PIN_H4, AB9540_PIN_F1 };
/* Digital microphone 3 and 4 */
static const unsigned dmic34_d_1_pins[] = { AB9540_PIN_F4, AB9540_PIN_F2 };
/* Digital microphone 5 and 6 */
static const unsigned dmic56_d_1_pins[] = { AB9540_PIN_E4, AB9540_PIN_F3 };
static const unsigned extcpena_d_1_pins[] = { AB9540_PIN_J13 };
/* modem SDA/SCL */
static const unsigned modsclsda_d_1_pins[] = { AB9540_PIN_L17, AB9540_PIN_L16 };
static const unsigned sysclkreq5_d_1_pins[] = { AB9540_PIN_W3 };
static const unsigned gpio50_d_1_pins[] = { AB9540_PIN_N4 };
static const unsigned batremn_d_1_pins[] = { AB9540_PIN_G12 };
static const unsigned resethw_d_1_pins[] = { AB9540_PIN_E17 };
static const unsigned service_d_1_pins[] = { AB9540_PIN_D11 };
static const unsigned gpio60_d_1_pins[] = { AB9540_PIN_M18 };

/* Altfunction A column */
static const unsigned gpio1_a_1_pins[] = { AB9540_PIN_R4 };
static const unsigned gpio2_a_1_pins[] = { AB9540_PIN_V3 };
static const unsigned gpio3_a_1_pins[] = { AB9540_PIN_T4 };
static const unsigned gpio4_a_1_pins[] = { AB9540_PIN_T5 };
static const unsigned hiqclkena_a_1_pins[] = { AB9540_PIN_B18 };
static const unsigned pdmclk_a_1_pins[] = { AB9540_PIN_C18 };
static const unsigned uartdata_a_1_pins[] = { AB9540_PIN_D18, AB9540_PIN_N4 };
static const unsigned gpio14_a_1_pins[] = { AB9540_PIN_B19 };
static const unsigned gpio15_a_1_pins[] = { AB9540_PIN_C19 };
static const unsigned gpio16_a_1_pins[] = { AB9540_PIN_D19 };
static const unsigned gpio17_a_1_pins[] = { AB9540_PIN_R3 };
static const unsigned gpio18_a_1_pins[] = { AB9540_PIN_T2 };
static const unsigned gpio19_a_1_pins[] = { AB9540_PIN_U2 };
static const unsigned gpio20_a_1_pins[] = { AB9540_PIN_V2 };
static const unsigned gpio21_a_1_pins[] = { AB9540_PIN_N17 };
static const unsigned gpio22_a_1_pins[] = { AB9540_PIN_N16 };
static const unsigned gpio23_a_1_pins[] = { AB9540_PIN_M19 };
static const unsigned gpio24_a_1_pins[] = { AB9540_PIN_T3 };
static const unsigned gpio25_a_1_pins[] = { AB9540_PIN_W2 };
static const unsigned gpio27_a_1_pins[] = { AB9540_PIN_H4 };
static const unsigned gpio28_a_1_pins[] = { AB9540_PIN_F1 };
static const unsigned gpio29_a_1_pins[] = { AB9540_PIN_F4 };
static const unsigned gpio30_a_1_pins[] = { AB9540_PIN_F2 };
static const unsigned gpio31_a_1_pins[] = { AB9540_PIN_E4 };
static const unsigned gpio32_a_1_pins[] = { AB9540_PIN_F3 };
static const unsigned gpio34_a_1_pins[] = { AB9540_PIN_J13 };
static const unsigned gpio40_a_1_pins[] = { AB9540_PIN_L17 };
static const unsigned gpio41_a_1_pins[] = { AB9540_PIN_L16 };
static const unsigned gpio42_a_1_pins[] = { AB9540_PIN_W3 };
static const unsigned gpio51_a_1_pins[] = { AB9540_PIN_G12 };
static const unsigned gpio52_a_1_pins[] = { AB9540_PIN_E17 };
static const unsigned gpio53_a_1_pins[] = { AB9540_PIN_D11 };
static const unsigned usbuiccpd_a_1_pins[] = { AB9540_PIN_M18 };

/* Altfunction B colum */
static const unsigned pdmdata_b_1_pins[] = { AB9540_PIN_B18 };
static const unsigned pwmextvibra1_b_1_pins[] = { AB9540_PIN_D18 };
static const unsigned pwmextvibra2_b_1_pins[] = { AB9540_PIN_N4 };

/* Altfunction C column */
static const unsigned usbvdat_c_1_pins[] = { AB9540_PIN_D18 };

#define AB9540_PIN_GROUP(a, b) { .name = #a, .pins = a##_pins,		\
			.npins = ARRAY_SIZE(a##_pins), .altsetting = b }

static const struct abx500_pingroup ab9540_groups[] = {
	/* default column */
	AB9540_PIN_GROUP(sysclkreq2_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(sysclkreq3_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(sysclkreq4_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(sysclkreq6_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(gpio10_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(gpio11_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(gpio13_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(pwmout1_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(pwmout2_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(pwmout3_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(adi1_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(usbuicc_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(sysclkreq7_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(sysclkreq8_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(dmic12_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(dmic34_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(dmic56_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(extcpena_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(modsclsda_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(sysclkreq5_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(gpio50_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(batremn_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(resethw_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(service_d_1, ABX500_DEFAULT),
	AB9540_PIN_GROUP(gpio60_d_1, ABX500_DEFAULT),

	/* Altfunction A column */
	AB9540_PIN_GROUP(gpio1_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio2_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio3_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio4_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(hiqclkena_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(pdmclk_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(uartdata_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio14_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio15_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio16_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio17_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio18_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio19_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio20_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio21_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio22_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio23_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio24_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio25_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio27_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio28_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio29_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio30_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio31_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio32_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio34_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio40_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio41_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio42_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio51_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio52_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(gpio53_a_1, ABX500_ALT_A),
	AB9540_PIN_GROUP(usbuiccpd_a_1, ABX500_ALT_A),

	/* Altfunction B column */
	AB9540_PIN_GROUP(pdmdata_b_1, ABX500_ALT_B),
	AB9540_PIN_GROUP(pwmextvibra1_b_1, ABX500_ALT_B),
	AB9540_PIN_GROUP(pwmextvibra2_b_1, ABX500_ALT_B),

	/* Altfunction C column */
	AB9540_PIN_GROUP(usbvdat_c_1, ABX500_ALT_C),
};

/* We use this macro to define the groups applicable to a function */
#define AB9540_FUNC_GROUPS(a, b...)	   \
static const char * const a##_groups[] = { b };

AB9540_FUNC_GROUPS(sysclkreq, "sysclkreq2_d_1", "sysclkreq3_d_1",
		"sysclkreq4_d_1", "sysclkreq5_d_1", "sysclkreq6_d_1",
		"sysclkreq7_d_1", "sysclkreq8_d_1");
AB9540_FUNC_GROUPS(gpio, "gpio1_a_1", "gpio2_a_1", "gpio3_a_1", "gpio4_a_1",
		"gpio10_d_1", "gpio11_d_1", "gpio13_d_1", "gpio14_a_1",
		"gpio15_a_1", "gpio16_a_1", "gpio17_a_1", "gpio18_a_1",
		"gpio19_a_1", "gpio20_a_1", "gpio21_a_1", "gpio22_a_1",
		"gpio23_a_1", "gpio24_a_1", "gpio25_a_1", "gpio27_a_1",
		"gpio28_a_1", "gpio29_a_1", "gpio30_a_1", "gpio31_a_1",
		"gpio32_a_1", "gpio34_a_1", "gpio40_a_1", "gpio41_a_1",
		"gpio42_a_1", "gpio50_d_1", "gpio51_a_1", "gpio52_a_1",
		"gpio53_a_1", "gpio60_d_1");
AB9540_FUNC_GROUPS(pwmout, "pwmout1_d_1", "pwmout2_d_1", "pwmout3_d_1");
AB9540_FUNC_GROUPS(adi1, "adi1_d_1");
AB9540_FUNC_GROUPS(usbuicc, "usbuicc_d_1", "usbuiccpd_a_1");
AB9540_FUNC_GROUPS(dmic, "dmic12_d_1", "dmic34_d_1", "dmic56_d_1");
AB9540_FUNC_GROUPS(extcpena, "extcpena_d_1");
AB9540_FUNC_GROUPS(modsclsda, "modsclsda_d_1");
AB9540_FUNC_GROUPS(batremn, "batremn_d_1");
AB9540_FUNC_GROUPS(resethw, "resethw_d_1");
AB9540_FUNC_GROUPS(service, "service_d_1");
AB9540_FUNC_GROUPS(hiqclkena, "hiqclkena_a_1");
AB9540_FUNC_GROUPS(pdm, "pdmdata_b_1", "pdmclk_a_1");
AB9540_FUNC_GROUPS(uartdata, "uartdata_a_1");
AB9540_FUNC_GROUPS(pwmextvibra, "pwmextvibra1_b_1", "pwmextvibra2_b_1");
AB9540_FUNC_GROUPS(usbvdat, "usbvdat_c_1");

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

static const struct abx500_function ab9540_functions[] = {
	FUNCTION(sysclkreq),
	FUNCTION(gpio),
	FUNCTION(pwmout),
	FUNCTION(adi1),
	FUNCTION(usbuicc),
	FUNCTION(dmic),
	FUNCTION(extcpena),
	FUNCTION(modsclsda),
	FUNCTION(batremn),
	FUNCTION(resethw),
	FUNCTION(service),
	FUNCTION(hiqclkena),
	FUNCTION(pdm),
	FUNCTION(uartdata),
	FUNCTION(pwmextvibra),
	FUNCTION(usbvdat),
};

/*
 * this table translates what's is in the AB9540 specification regarding the
 * balls alternate functions (as for DB, default, ALT_A, ALT_B and ALT_C).
 * ALTERNATE_FUNCTIONS(GPIO_NUMBER, GPIOSEL bit, ALTERNATFUNC bit1,
 * ALTERNATEFUNC bit2, ALTA val, ALTB val, ALTC val),
 *
 * example :
 *
 *	ALTERNATE_FUNCTIONS(13,     4,      3,      4, 1, 0, 2),
 *	means that pin AB9540_PIN_D18 (pin 13) supports 4 mux (default/ALT_A,
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

struct alternate_functions ab9540alternate_functions[AB9540_GPIO_MAX_NUMBER + 1] = {
	/* GPIOSEL1 - bits 4-7 are reserved */
	ALTERNATE_FUNCTIONS(0, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO0 */
	ALTERNATE_FUNCTIONS(1,	    0, UNUSED, UNUSED, 0, 0, 0), /* GPIO1, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(2,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO2, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(3,      2, UNUSED, UNUSED, 0, 0, 0), /* GPIO3, altA controlled by bit 2*/
	ALTERNATE_FUNCTIONS(4,      3, UNUSED, UNUSED, 0, 0, 0), /* GPIO4, altA controlled by bit 3*/
	ALTERNATE_FUNCTIONS(5, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO5 */
	ALTERNATE_FUNCTIONS(6, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO6 */
	ALTERNATE_FUNCTIONS(7, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO7 */
	ALTERNATE_FUNCTIONS(8, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO8 */
	/* GPIOSEL2 - bits 0 and 3 are reserved */
	ALTERNATE_FUNCTIONS(9, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO9 */
	ALTERNATE_FUNCTIONS(10,      1,      0, UNUSED, 1, 0, 0), /* GPIO10, altA and altB controlled by bit 0 */
	ALTERNATE_FUNCTIONS(11,      2, UNUSED, UNUSED, 0, 0, 0), /* GPIO11, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(12, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO12 */
	ALTERNATE_FUNCTIONS(13,      4,      3,      4, 1, 0, 2), /* GPIO13, altA altB and altC controlled by bit 3 and 4 */
	ALTERNATE_FUNCTIONS(14,      5, UNUSED, UNUSED, 0, 0, 0), /* GPIO14, altA controlled by bit 5 */
	ALTERNATE_FUNCTIONS(15,      6, UNUSED, UNUSED, 0, 0, 0), /* GPIO15, altA controlled by bit 6 */
	ALTERNATE_FUNCTIONS(16,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO16, altA controlled by bit 7 */
	/* GPIOSEL3 - bit 1-3 reserved
	 * pins 17 to 20 are special case, only bit 0 is used to select
	 * alternate function for these 4 pins.
	 * bits 1 to 3 are reserved
	 */
	ALTERNATE_FUNCTIONS(17,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO17, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(18,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO18, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(19,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO19, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(20,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO20, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(21,      4, UNUSED, UNUSED, 0, 0, 0), /* GPIO21, altA controlled by bit 4 */
	ALTERNATE_FUNCTIONS(22,      5, UNUSED, UNUSED, 0, 0, 0), /* GPIO22, altA controlled by bit 5 */
	ALTERNATE_FUNCTIONS(23,      6, UNUSED, UNUSED, 0, 0, 0), /* GPIO23, altA controlled by bit 6 */
	ALTERNATE_FUNCTIONS(24,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO24, altA controlled by bit 7 */
	/* GPIOSEL4 - bit 1 reserved */
	ALTERNATE_FUNCTIONS(25,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO25, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(26, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO26 */
	ALTERNATE_FUNCTIONS(27,      2, UNUSED, UNUSED, 0, 0, 0), /* GPIO27, altA controlled by bit 2 */
	ALTERNATE_FUNCTIONS(28,      3, UNUSED, UNUSED, 0, 0, 0), /* GPIO28, altA controlled by bit 3 */
	ALTERNATE_FUNCTIONS(29,      4, UNUSED, UNUSED, 0, 0, 0), /* GPIO29, altA controlled by bit 4 */
	ALTERNATE_FUNCTIONS(30,      5, UNUSED, UNUSED, 0, 0, 0), /* GPIO30, altA controlled by bit 5 */
	ALTERNATE_FUNCTIONS(31,      6, UNUSED, UNUSED, 0, 0, 0), /* GPIO31, altA controlled by bit 6 */
	ALTERNATE_FUNCTIONS(32,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO32, altA controlled by bit 7 */
	/* GPIOSEL5 - bit 0, 2-6 are reserved */
	ALTERNATE_FUNCTIONS(33, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO33 */
	ALTERNATE_FUNCTIONS(34,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO34, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(35, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO35 */
	ALTERNATE_FUNCTIONS(36, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO36 */
	ALTERNATE_FUNCTIONS(37, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO37 */
	ALTERNATE_FUNCTIONS(38, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO38 */
	ALTERNATE_FUNCTIONS(39, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO39 */
	ALTERNATE_FUNCTIONS(40,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO40, altA controlled by bit 7 */
	/* GPIOSEL6 - bit 2-7 are reserved */
	ALTERNATE_FUNCTIONS(41,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO41, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(42,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO42, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(43, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO43 */
	ALTERNATE_FUNCTIONS(44, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO44 */
	ALTERNATE_FUNCTIONS(45, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO45 */
	ALTERNATE_FUNCTIONS(46, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO46 */
	ALTERNATE_FUNCTIONS(47, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO47 */
	ALTERNATE_FUNCTIONS(48, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO48 */
	/*
	 * GPIOSEL7 - bit 0 and 6-7 are reserved
	 * special case with GPIO60, wich is located at offset 5 of gpiosel7
	 * don't know why it has been called GPIO60 in AB9540 datasheet,
	 * GPIO54 would be logical..., so at SOC point of view we consider
	 * GPIO60 = GPIO54
	 */
	ALTERNATE_FUNCTIONS(49,      0, UNUSED, UNUSED, 0, 0, 0), /* no GPIO49 */
	ALTERNATE_FUNCTIONS(50,      1,	     2, UNUSED, 1, 0, 0), /* GPIO50, altA and altB controlled by bit 1 */
	ALTERNATE_FUNCTIONS(51,	     2, UNUSED, UNUSED, 0, 0, 0), /* GPIO51, altA controlled by bit 2 */
	ALTERNATE_FUNCTIONS(52,      3, UNUSED, UNUSED, 0, 0, 0), /* GPIO52, altA controlled by bit 3 */
	ALTERNATE_FUNCTIONS(53,	     4, UNUSED, UNUSED, 0, 0, 0), /* GPIO53, altA controlled by bit 4 */
	ALTERNATE_FUNCTIONS(54,	     5, UNUSED, UNUSED, 0, 0, 0), /* GPIO54 = GPIO60, altA controlled by bit 5 */
};

struct abx500_gpio_irq_cluster ab9540_gpio_irq_cluster[] = {
	GPIO_IRQ_CLUSTER(10, 13, 0),
	GPIO_IRQ_CLUSTER(24, 25, 0),
	GPIO_IRQ_CLUSTER(40, 41, 0),
	GPIO_IRQ_CLUSTER(50, 54, 0),
};

static struct abx500_pinctrl_soc_data ab9540_soc = {
	.gpio_ranges = ab9540_pinranges,
	.gpio_num_ranges = ARRAY_SIZE(ab9540_pinranges),
	.pins = ab9540_pins,
	.npins = ARRAY_SIZE(ab9540_pins),
	.functions = ab9540_functions,
	.nfunctions = ARRAY_SIZE(ab9540_functions),
	.groups = ab9540_groups,
	.ngroups = ARRAY_SIZE(ab9540_groups),
	.alternate_functions = ab9540alternate_functions,
	.gpio_irq_cluster = ab9540_gpio_irq_cluster,
	.ngpio_irq_cluster = ARRAY_SIZE(ab9540_gpio_irq_cluster),
	.irq_gpio_rising_offset = AB8500_INT_GPIO6R,
	.irq_gpio_falling_offset = AB8500_INT_GPIO6F,
	.irq_gpio_factor = 1,
};

void
abx500_pinctrl_ab9540_init(struct abx500_pinctrl_soc_data **soc)
{
	*soc = &ab9540_soc;
}
