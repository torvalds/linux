// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Patrice Chotard <patrice.chotard@stericsson.com> for ST-Ericsson.
 */

#include <linux/kernel.h>
#include <linux/gpio/driver.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/mfd/abx500/ab8500.h>
#include "pinctrl-abx500.h"

/* All the pins that can be used for GPIO and some other functions */
#define ABX500_GPIO(offset)		(offset)

#define AB8500_PIN_T10		ABX500_GPIO(1)
#define AB8500_PIN_T9		ABX500_GPIO(2)
#define AB8500_PIN_U9		ABX500_GPIO(3)
#define AB8500_PIN_W2		ABX500_GPIO(4)
/* hole */
#define AB8500_PIN_Y18		ABX500_GPIO(6)
#define AB8500_PIN_AA20		ABX500_GPIO(7)
#define AB8500_PIN_W18		ABX500_GPIO(8)
#define AB8500_PIN_AA19		ABX500_GPIO(9)
#define AB8500_PIN_U17		ABX500_GPIO(10)
#define AB8500_PIN_AA18		ABX500_GPIO(11)
#define AB8500_PIN_U16		ABX500_GPIO(12)
#define AB8500_PIN_W17		ABX500_GPIO(13)
#define AB8500_PIN_F14		ABX500_GPIO(14)
#define AB8500_PIN_B17		ABX500_GPIO(15)
#define AB8500_PIN_F15		ABX500_GPIO(16)
#define AB8500_PIN_P5		ABX500_GPIO(17)
#define AB8500_PIN_R5		ABX500_GPIO(18)
#define AB8500_PIN_U5		ABX500_GPIO(19)
#define AB8500_PIN_T5		ABX500_GPIO(20)
#define AB8500_PIN_H19		ABX500_GPIO(21)
#define AB8500_PIN_G20		ABX500_GPIO(22)
#define AB8500_PIN_G19		ABX500_GPIO(23)
#define AB8500_PIN_T14		ABX500_GPIO(24)
#define AB8500_PIN_R16		ABX500_GPIO(25)
#define AB8500_PIN_M16		ABX500_GPIO(26)
#define AB8500_PIN_J6		ABX500_GPIO(27)
#define AB8500_PIN_K6		ABX500_GPIO(28)
#define AB8500_PIN_G6		ABX500_GPIO(29)
#define AB8500_PIN_H6		ABX500_GPIO(30)
#define AB8500_PIN_F5		ABX500_GPIO(31)
#define AB8500_PIN_G5		ABX500_GPIO(32)
/* hole */
#define AB8500_PIN_R17		ABX500_GPIO(34)
#define AB8500_PIN_W15		ABX500_GPIO(35)
#define AB8500_PIN_A17		ABX500_GPIO(36)
#define AB8500_PIN_E15		ABX500_GPIO(37)
#define AB8500_PIN_C17		ABX500_GPIO(38)
#define AB8500_PIN_E16		ABX500_GPIO(39)
#define AB8500_PIN_T19		ABX500_GPIO(40)
#define AB8500_PIN_U19		ABX500_GPIO(41)
#define AB8500_PIN_U2		ABX500_GPIO(42)

/* indicates the highest GPIO number */
#define AB8500_GPIO_MAX_NUMBER	42

/*
 * The names of the pins are denoted by GPIO number and ball name, even
 * though they can be used for other things than GPIO, this is the first
 * column in the table of the data sheet and often used on schematics and
 * such.
 */
static const struct pinctrl_pin_desc ab8500_pins[] = {
	PINCTRL_PIN(AB8500_PIN_T10, "GPIO1_T10"),
	PINCTRL_PIN(AB8500_PIN_T9, "GPIO2_T9"),
	PINCTRL_PIN(AB8500_PIN_U9, "GPIO3_U9"),
	PINCTRL_PIN(AB8500_PIN_W2, "GPIO4_W2"),
	/* hole */
	PINCTRL_PIN(AB8500_PIN_Y18, "GPIO6_Y18"),
	PINCTRL_PIN(AB8500_PIN_AA20, "GPIO7_AA20"),
	PINCTRL_PIN(AB8500_PIN_W18, "GPIO8_W18"),
	PINCTRL_PIN(AB8500_PIN_AA19, "GPIO9_AA19"),
	PINCTRL_PIN(AB8500_PIN_U17, "GPIO10_U17"),
	PINCTRL_PIN(AB8500_PIN_AA18, "GPIO11_AA18"),
	PINCTRL_PIN(AB8500_PIN_U16, "GPIO12_U16"),
	PINCTRL_PIN(AB8500_PIN_W17, "GPIO13_W17"),
	PINCTRL_PIN(AB8500_PIN_F14, "GPIO14_F14"),
	PINCTRL_PIN(AB8500_PIN_B17, "GPIO15_B17"),
	PINCTRL_PIN(AB8500_PIN_F15, "GPIO16_F15"),
	PINCTRL_PIN(AB8500_PIN_P5, "GPIO17_P5"),
	PINCTRL_PIN(AB8500_PIN_R5, "GPIO18_R5"),
	PINCTRL_PIN(AB8500_PIN_U5, "GPIO19_U5"),
	PINCTRL_PIN(AB8500_PIN_T5, "GPIO20_T5"),
	PINCTRL_PIN(AB8500_PIN_H19, "GPIO21_H19"),
	PINCTRL_PIN(AB8500_PIN_G20, "GPIO22_G20"),
	PINCTRL_PIN(AB8500_PIN_G19, "GPIO23_G19"),
	PINCTRL_PIN(AB8500_PIN_T14, "GPIO24_T14"),
	PINCTRL_PIN(AB8500_PIN_R16, "GPIO25_R16"),
	PINCTRL_PIN(AB8500_PIN_M16, "GPIO26_M16"),
	PINCTRL_PIN(AB8500_PIN_J6, "GPIO27_J6"),
	PINCTRL_PIN(AB8500_PIN_K6, "GPIO28_K6"),
	PINCTRL_PIN(AB8500_PIN_G6, "GPIO29_G6"),
	PINCTRL_PIN(AB8500_PIN_H6, "GPIO30_H6"),
	PINCTRL_PIN(AB8500_PIN_F5, "GPIO31_F5"),
	PINCTRL_PIN(AB8500_PIN_G5, "GPIO32_G5"),
	/* hole */
	PINCTRL_PIN(AB8500_PIN_R17, "GPIO34_R17"),
	PINCTRL_PIN(AB8500_PIN_W15, "GPIO35_W15"),
	PINCTRL_PIN(AB8500_PIN_A17, "GPIO36_A17"),
	PINCTRL_PIN(AB8500_PIN_E15, "GPIO37_E15"),
	PINCTRL_PIN(AB8500_PIN_C17, "GPIO38_C17"),
	PINCTRL_PIN(AB8500_PIN_E16, "GPIO39_E16"),
	PINCTRL_PIN(AB8500_PIN_T19, "GPIO40_T19"),
	PINCTRL_PIN(AB8500_PIN_U19, "GPIO41_U19"),
	PINCTRL_PIN(AB8500_PIN_U2, "GPIO42_U2"),
};

/*
 * Maps local GPIO offsets to local pin numbers
 */
static const struct abx500_pinrange ab8500_pinranges[] = {
	ABX500_PINRANGE(1, 4, ABX500_ALT_A),
	ABX500_PINRANGE(6, 4, ABX500_ALT_A),
	ABX500_PINRANGE(10, 4, ABX500_DEFAULT),
	ABX500_PINRANGE(14, 12, ABX500_ALT_A),
	ABX500_PINRANGE(26, 1, ABX500_DEFAULT),
	ABX500_PINRANGE(27, 6, ABX500_ALT_A),
	ABX500_PINRANGE(34, 1, ABX500_ALT_A),
	ABX500_PINRANGE(35, 1, ABX500_DEFAULT),
	ABX500_PINRANGE(36, 7, ABX500_ALT_A),
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
static const unsigned sysclkreq2_d_1_pins[] = { AB8500_PIN_T10 };
static const unsigned sysclkreq3_d_1_pins[] = { AB8500_PIN_T9 };
static const unsigned sysclkreq4_d_1_pins[] = { AB8500_PIN_U9 };
static const unsigned sysclkreq6_d_1_pins[] = { AB8500_PIN_W2 };
static const unsigned ycbcr0123_d_1_pins[] = { AB8500_PIN_Y18, AB8500_PIN_AA20,
					AB8500_PIN_W18, AB8500_PIN_AA19};
static const unsigned gpio10_d_1_pins[] = { AB8500_PIN_U17 };
static const unsigned gpio11_d_1_pins[] = { AB8500_PIN_AA18 };
static const unsigned gpio12_d_1_pins[] = { AB8500_PIN_U16 };
static const unsigned gpio13_d_1_pins[] = { AB8500_PIN_W17 };
static const unsigned pwmout1_d_1_pins[] = { AB8500_PIN_F14 };
static const unsigned pwmout2_d_1_pins[] = { AB8500_PIN_B17 };
static const unsigned pwmout3_d_1_pins[] = { AB8500_PIN_F15 };

/* audio data interface 1*/
static const unsigned adi1_d_1_pins[] = { AB8500_PIN_P5, AB8500_PIN_R5,
					AB8500_PIN_U5, AB8500_PIN_T5 };
/* USBUICC */
static const unsigned usbuicc_d_1_pins[] = { AB8500_PIN_H19, AB8500_PIN_G20,
					AB8500_PIN_G19 };
static const unsigned sysclkreq7_d_1_pins[] = { AB8500_PIN_T14 };
static const unsigned sysclkreq8_d_1_pins[] = { AB8500_PIN_R16 };
static const unsigned gpio26_d_1_pins[] = { AB8500_PIN_M16 };
/* Digital microphone 1 and 2 */
static const unsigned dmic12_d_1_pins[] = { AB8500_PIN_J6, AB8500_PIN_K6 };
/* Digital microphone 3 and 4 */
static const unsigned dmic34_d_1_pins[] = { AB8500_PIN_G6, AB8500_PIN_H6 };
/* Digital microphone 5 and 6 */
static const unsigned dmic56_d_1_pins[] = { AB8500_PIN_F5, AB8500_PIN_G5 };
static const unsigned extcpena_d_1_pins[] = { AB8500_PIN_R17 };
static const unsigned gpio35_d_1_pins[] = { AB8500_PIN_W15 };
/* APE SPI */
static const unsigned apespi_d_1_pins[] = { AB8500_PIN_A17, AB8500_PIN_E15,
					AB8500_PIN_C17, AB8500_PIN_E16};
/* modem SDA/SCL */
static const unsigned modsclsda_d_1_pins[] = { AB8500_PIN_T19, AB8500_PIN_U19 };
static const unsigned sysclkreq5_d_1_pins[] = { AB8500_PIN_U2 };

/* Altfunction A column */
static const unsigned gpio1_a_1_pins[] = { AB8500_PIN_T10 };
static const unsigned gpio2_a_1_pins[] = { AB8500_PIN_T9 };
static const unsigned gpio3_a_1_pins[] = { AB8500_PIN_U9 };
static const unsigned gpio4_a_1_pins[] = { AB8500_PIN_W2 };
static const unsigned gpio6_a_1_pins[] = { AB8500_PIN_Y18 };
static const unsigned gpio7_a_1_pins[] = { AB8500_PIN_AA20 };
static const unsigned gpio8_a_1_pins[] = { AB8500_PIN_W18 };
static const unsigned gpio9_a_1_pins[] = { AB8500_PIN_AA19 };
/* YCbCr4 YCbCr5 YCbCr6 YCbCr7*/
static const unsigned ycbcr4567_a_1_pins[] = { AB8500_PIN_U17, AB8500_PIN_AA18,
					AB8500_PIN_U16, AB8500_PIN_W17};
static const unsigned gpio14_a_1_pins[] = { AB8500_PIN_F14 };
static const unsigned gpio15_a_1_pins[] = { AB8500_PIN_B17 };
static const unsigned gpio16_a_1_pins[] = { AB8500_PIN_F15 };
static const unsigned gpio17_a_1_pins[] = { AB8500_PIN_P5 };
static const unsigned gpio18_a_1_pins[] = { AB8500_PIN_R5 };
static const unsigned gpio19_a_1_pins[] = { AB8500_PIN_U5 };
static const unsigned gpio20_a_1_pins[] = { AB8500_PIN_T5 };
static const unsigned gpio21_a_1_pins[] = { AB8500_PIN_H19 };
static const unsigned gpio22_a_1_pins[] = { AB8500_PIN_G20 };
static const unsigned gpio23_a_1_pins[] = { AB8500_PIN_G19 };
static const unsigned gpio24_a_1_pins[] = { AB8500_PIN_T14 };
static const unsigned gpio25_a_1_pins[] = { AB8500_PIN_R16 };
static const unsigned gpio27_a_1_pins[] = { AB8500_PIN_J6 };
static const unsigned gpio28_a_1_pins[] = { AB8500_PIN_K6 };
static const unsigned gpio29_a_1_pins[] = { AB8500_PIN_G6 };
static const unsigned gpio30_a_1_pins[] = { AB8500_PIN_H6 };
static const unsigned gpio31_a_1_pins[] = { AB8500_PIN_F5 };
static const unsigned gpio32_a_1_pins[] = { AB8500_PIN_G5 };
static const unsigned gpio34_a_1_pins[] = { AB8500_PIN_R17 };
static const unsigned gpio36_a_1_pins[] = { AB8500_PIN_A17 };
static const unsigned gpio37_a_1_pins[] = { AB8500_PIN_E15 };
static const unsigned gpio38_a_1_pins[] = { AB8500_PIN_C17 };
static const unsigned gpio39_a_1_pins[] = { AB8500_PIN_E16 };
static const unsigned gpio40_a_1_pins[] = { AB8500_PIN_T19 };
static const unsigned gpio41_a_1_pins[] = { AB8500_PIN_U19 };
static const unsigned gpio42_a_1_pins[] = { AB8500_PIN_U2 };

/* Altfunction B colum */
static const unsigned hiqclkena_b_1_pins[] = { AB8500_PIN_U17 };
static const unsigned usbuiccpd_b_1_pins[] = { AB8500_PIN_AA18 };
static const unsigned i2ctrig1_b_1_pins[] = { AB8500_PIN_U16 };
static const unsigned i2ctrig2_b_1_pins[] = { AB8500_PIN_W17 };

/* Altfunction C column */
static const unsigned usbvdat_c_1_pins[] = { AB8500_PIN_W17 };


#define AB8500_PIN_GROUP(a, b) { .name = #a, .pins = a##_pins,		\
			.npins = ARRAY_SIZE(a##_pins), .altsetting = b }

static const struct abx500_pingroup ab8500_groups[] = {
	/* default column */
	AB8500_PIN_GROUP(sysclkreq2_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(sysclkreq3_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(sysclkreq4_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(sysclkreq6_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(ycbcr0123_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(gpio10_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(gpio11_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(gpio12_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(gpio13_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(pwmout1_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(pwmout2_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(pwmout3_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(adi1_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(usbuicc_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(sysclkreq7_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(sysclkreq8_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(gpio26_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(dmic12_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(dmic34_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(dmic56_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(extcpena_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(gpio35_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(apespi_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(modsclsda_d_1, ABX500_DEFAULT),
	AB8500_PIN_GROUP(sysclkreq5_d_1, ABX500_DEFAULT),
	/* Altfunction A column */
	AB8500_PIN_GROUP(gpio1_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio2_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio3_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio4_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio6_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio7_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio8_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio9_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(ycbcr4567_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio14_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio15_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio16_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio17_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio18_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio19_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio20_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio21_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio22_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio23_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio24_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio25_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio27_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio28_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio29_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio30_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio31_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio32_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio34_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio36_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio37_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio38_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio39_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio40_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio41_a_1, ABX500_ALT_A),
	AB8500_PIN_GROUP(gpio42_a_1, ABX500_ALT_A),
	/* Altfunction B column */
	AB8500_PIN_GROUP(hiqclkena_b_1, ABX500_ALT_B),
	AB8500_PIN_GROUP(usbuiccpd_b_1, ABX500_ALT_B),
	AB8500_PIN_GROUP(i2ctrig1_b_1, ABX500_ALT_B),
	AB8500_PIN_GROUP(i2ctrig2_b_1, ABX500_ALT_B),
	/* Altfunction C column */
	AB8500_PIN_GROUP(usbvdat_c_1, ABX500_ALT_C),
};

/* We use this macro to define the groups applicable to a function */
#define AB8500_FUNC_GROUPS(a, b...)	   \
static const char * const a##_groups[] = { b };

AB8500_FUNC_GROUPS(sysclkreq, "sysclkreq2_d_1", "sysclkreq3_d_1",
		"sysclkreq4_d_1", "sysclkreq5_d_1", "sysclkreq6_d_1",
		"sysclkreq7_d_1", "sysclkreq8_d_1");
AB8500_FUNC_GROUPS(ycbcr, "ycbcr0123_d_1", "ycbcr4567_a_1");
AB8500_FUNC_GROUPS(gpio, "gpio1_a_1", "gpio2_a_1", "gpio3_a_1", "gpio4_a_1",
		"gpio6_a_1", "gpio7_a_1", "gpio8_a_1", "gpio9_a_1",
		"gpio10_d_1", "gpio11_d_1", "gpio12_d_1", "gpio13_d_1",
		"gpio14_a_1", "gpio15_a_1", "gpio16_a_1", "gpio17_a_1",
		"gpio18_a_1", "gpio19_a_1", "gpio20_a_1", "gpio21_a_1",
		"gpio22_a_1", "gpio23_a_1", "gpio24_a_1", "gpio25_a_1",
		"gpio26_d_1", "gpio27_a_1", "gpio28_a_1", "gpio29_a_1",
		"gpio30_a_1", "gpio31_a_1", "gpio32_a_1", "gpio34_a_1",
		"gpio35_d_1", "gpio36_a_1", "gpio37_a_1", "gpio38_a_1",
		"gpio39_a_1", "gpio40_a_1", "gpio41_a_1", "gpio42_a_1");
AB8500_FUNC_GROUPS(pwmout, "pwmout1_d_1", "pwmout2_d_1", "pwmout3_d_1");
AB8500_FUNC_GROUPS(adi1, "adi1_d_1");
AB8500_FUNC_GROUPS(usbuicc, "usbuicc_d_1", "usbuiccpd_b_1");
AB8500_FUNC_GROUPS(dmic, "dmic12_d_1", "dmic34_d_1", "dmic56_d_1");
AB8500_FUNC_GROUPS(extcpena, "extcpena_d_1");
AB8500_FUNC_GROUPS(apespi, "apespi_d_1");
AB8500_FUNC_GROUPS(modsclsda, "modsclsda_d_1");
AB8500_FUNC_GROUPS(hiqclkena, "hiqclkena_b_1");
AB8500_FUNC_GROUPS(i2ctrig, "i2ctrig1_b_1", "i2ctrig2_b_1");
AB8500_FUNC_GROUPS(usbvdat, "usbvdat_c_1");

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

static const struct abx500_function ab8500_functions[] = {
	FUNCTION(sysclkreq),
	FUNCTION(ycbcr),
	FUNCTION(gpio),
	FUNCTION(pwmout),
	FUNCTION(adi1),
	FUNCTION(usbuicc),
	FUNCTION(dmic),
	FUNCTION(extcpena),
	FUNCTION(apespi),
	FUNCTION(modsclsda),
	FUNCTION(hiqclkena),
	FUNCTION(i2ctrig),
	FUNCTION(usbvdat),
};

/*
 * this table translates what's is in the AB8500 specification regarding the
 * balls alternate functions (as for DB, default, ALT_A, ALT_B and ALT_C).
 * ALTERNATE_FUNCTIONS(GPIO_NUMBER, GPIOSEL bit, ALTERNATFUNC bit1,
 * ALTERNATEFUNC bit2, ALTA val, ALTB val, ALTC val),
 *
 * example :
 *
 *	ALTERNATE_FUNCTIONS(13,     4,      3,      4, 0, 1 ,2),
 *	means that pin AB8500_PIN_W17 (pin 13) supports 4 mux (default/ALT_A,
 *	ALT_B and ALT_C), so GPIOSEL and ALTERNATFUNC registers are used to
 *	select the mux.  ALTA, ALTB and ALTC val indicates values to write in
 *	ALTERNATFUNC register. We need to specifies these values as SOC
 *	designers didn't apply the same logic on how to select mux in the
 *	ABx500 family.
 *
 *	As this pins supports at least ALT_B mux, default mux is
 *	selected by writing 1 in GPIOSEL bit :
 *
 *		| GPIOSEL bit=4 | alternatfunc bit2=4 | alternatfunc bit1=3
 *	default	|       1       |          0          |          0
 *	alt_A	|       0       |          0          |          0
 *	alt_B	|       0       |          0          |          1
 *	alt_C	|       0       |          1          |          0
 *
 *	ALTERNATE_FUNCTIONS(8,      7, UNUSED, UNUSED),
 *	means that pin AB8500_PIN_W18 (pin 8) supports 2 mux, so only GPIOSEL
 *	register is used to select the mux. As this pins doesn't support at
 *	least ALT_B mux, default mux is by writing 0 in GPIOSEL bit :
 *
 *		| GPIOSEL bit=7 | alternatfunc bit2=  | alternatfunc bit1=
 *	default	|       0       |          0          |          0
 *	alt_A	|       1       |          0          |          0
 */

static struct
alternate_functions ab8500_alternate_functions[AB8500_GPIO_MAX_NUMBER + 1] = {
	ALTERNATE_FUNCTIONS(0, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO0 */
	ALTERNATE_FUNCTIONS(1,	    0, UNUSED, UNUSED, 0, 0, 0), /* GPIO1, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(2,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO2, altA controlled by bit 1 */
	ALTERNATE_FUNCTIONS(3,      2, UNUSED, UNUSED, 0, 0, 0), /* GPIO3, altA controlled by bit 2*/
	ALTERNATE_FUNCTIONS(4,      3, UNUSED, UNUSED, 0, 0, 0), /* GPIO4, altA controlled by bit 3*/
	/* bit 4 reserved */
	ALTERNATE_FUNCTIONS(5, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO5 */
	ALTERNATE_FUNCTIONS(6,      5, UNUSED, UNUSED, 0, 0, 0), /* GPIO6, altA controlled by bit 5*/
	ALTERNATE_FUNCTIONS(7,      6, UNUSED, UNUSED, 0, 0, 0), /* GPIO7, altA controlled by bit 6*/
	ALTERNATE_FUNCTIONS(8,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO8, altA controlled by bit 7*/

	ALTERNATE_FUNCTIONS(9,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO9, altA controlled by bit 0*/
	ALTERNATE_FUNCTIONS(10,     1,      0, UNUSED, 0, 1, 0), /* GPIO10, altA and altB controlled by bit 0 */
	ALTERNATE_FUNCTIONS(11,     2,      1, UNUSED, 0, 1, 0), /* GPIO11, altA and altB controlled by bit 1 */
	ALTERNATE_FUNCTIONS(12,     3,      2, UNUSED, 0, 1, 0), /* GPIO12, altA and altB controlled by bit 2 */
	ALTERNATE_FUNCTIONS(13,     4,      3,      4, 0, 1, 2), /* GPIO13, altA altB and altC controlled by bit 3 and 4 */
	ALTERNATE_FUNCTIONS(14,     5, UNUSED, UNUSED, 0, 0, 0), /* GPIO14, altA controlled by bit 5 */
	ALTERNATE_FUNCTIONS(15,     6, UNUSED, UNUSED, 0, 0, 0), /* GPIO15, altA controlled by bit 6 */
	ALTERNATE_FUNCTIONS(16,     7, UNUSED, UNUSED, 0, 0, 0), /* GPIO16, altA controlled by bit 7 */
	/*
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

	ALTERNATE_FUNCTIONS(25,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO25, altA controlled by bit 0 */
	/* pin 26 special case, no alternate function, bit 1 reserved */
	ALTERNATE_FUNCTIONS(26, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* GPIO26 */
	ALTERNATE_FUNCTIONS(27,      2, UNUSED, UNUSED, 0, 0, 0), /* GPIO27, altA controlled by bit 2 */
	ALTERNATE_FUNCTIONS(28,      3, UNUSED, UNUSED, 0, 0, 0), /* GPIO28, altA controlled by bit 3 */
	ALTERNATE_FUNCTIONS(29,      4, UNUSED, UNUSED, 0, 0, 0), /* GPIO29, altA controlled by bit 4 */
	ALTERNATE_FUNCTIONS(30,      5, UNUSED, UNUSED, 0, 0, 0), /* GPIO30, altA controlled by bit 5 */
	ALTERNATE_FUNCTIONS(31,      6, UNUSED, UNUSED, 0, 0, 0), /* GPIO31, altA controlled by bit 6 */
	ALTERNATE_FUNCTIONS(32,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO32, altA controlled by bit 7 */

	ALTERNATE_FUNCTIONS(33, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* no GPIO33 */
	ALTERNATE_FUNCTIONS(34,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO34, altA controlled by bit 1 */
	/* pin 35 special case, no alternate function, bit 2 reserved */
	ALTERNATE_FUNCTIONS(35, UNUSED, UNUSED, UNUSED, 0, 0, 0), /* GPIO35 */
	ALTERNATE_FUNCTIONS(36,      3, UNUSED, UNUSED, 0, 0, 0), /* GPIO36, altA controlled by bit 3 */
	ALTERNATE_FUNCTIONS(37,      4, UNUSED, UNUSED, 0, 0, 0), /* GPIO37, altA controlled by bit 4 */
	ALTERNATE_FUNCTIONS(38,      5, UNUSED, UNUSED, 0, 0, 0), /* GPIO38, altA controlled by bit 5 */
	ALTERNATE_FUNCTIONS(39,      6, UNUSED, UNUSED, 0, 0, 0), /* GPIO39, altA controlled by bit 6 */
	ALTERNATE_FUNCTIONS(40,      7, UNUSED, UNUSED, 0, 0, 0), /* GPIO40, altA controlled by bit 7 */

	ALTERNATE_FUNCTIONS(41,      0, UNUSED, UNUSED, 0, 0, 0), /* GPIO41, altA controlled by bit 0 */
	ALTERNATE_FUNCTIONS(42,      1, UNUSED, UNUSED, 0, 0, 0), /* GPIO42, altA controlled by bit 1 */
};

/*
 * Only some GPIOs are interrupt capable, and they are
 * organized in discontiguous clusters:
 *
 *	GPIO6 to GPIO13
 *	GPIO24 and GPIO25
 *	GPIO36 to GPIO41
 */
static struct abx500_gpio_irq_cluster ab8500_gpio_irq_cluster[] = {
	GPIO_IRQ_CLUSTER(6,  13, AB8500_INT_GPIO6R),
	GPIO_IRQ_CLUSTER(24, 25, AB8500_INT_GPIO24R),
	GPIO_IRQ_CLUSTER(36, 41, AB8500_INT_GPIO36R),
};

static struct abx500_pinctrl_soc_data ab8500_soc = {
	.gpio_ranges = ab8500_pinranges,
	.gpio_num_ranges = ARRAY_SIZE(ab8500_pinranges),
	.pins = ab8500_pins,
	.npins = ARRAY_SIZE(ab8500_pins),
	.functions = ab8500_functions,
	.nfunctions = ARRAY_SIZE(ab8500_functions),
	.groups = ab8500_groups,
	.ngroups = ARRAY_SIZE(ab8500_groups),
	.alternate_functions = ab8500_alternate_functions,
	.gpio_irq_cluster = ab8500_gpio_irq_cluster,
	.ngpio_irq_cluster = ARRAY_SIZE(ab8500_gpio_irq_cluster),
	.irq_gpio_rising_offset = AB8500_INT_GPIO6R,
	.irq_gpio_falling_offset = AB8500_INT_GPIO6F,
	.irq_gpio_factor = 1,
};

void abx500_pinctrl_ab8500_init(struct abx500_pinctrl_soc_data **soc)
{
	*soc = &ab8500_soc;
}
