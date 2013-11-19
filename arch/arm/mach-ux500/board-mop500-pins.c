/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/string.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>

#include <asm/mach-types.h>

#include "board-mop500.h"

/* These simply sets bias for pins */
#define BIAS(a,b) static unsigned long a[] = { b }

BIAS(abx500_out_lo, PIN_CONF_PACKED(PIN_CONFIG_OUTPUT, 0));
BIAS(abx500_in_pd, PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 1));
BIAS(abx500_in_nopull, PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 0));

#define AB8500_MUX_HOG(group, func) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-ab8500.0", group, func)
#define AB8500_PIN_HOG(pin, conf) \
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-ab8500.0", pin, abx500_##conf)

#define AB8500_MUX_STATE(group, func, dev, state) \
	PIN_MAP_MUX_GROUP(dev, state, "pinctrl-ab8500.0", group, func)
#define AB8500_PIN_STATE(pin, conf, dev, state) \
	PIN_MAP_CONFIGS_PIN(dev, state, "pinctrl-ab8500.0", pin, abx500_##conf)

#define AB8505_MUX_HOG(group, func) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-ab8505.0", group, func)
#define AB8505_PIN_HOG(pin, conf) \
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-ab8505.0", pin, abx500_##conf)

#define AB8505_MUX_STATE(group, func, dev, state) \
	PIN_MAP_MUX_GROUP(dev, state, "pinctrl-ab8505.0", group, func)
#define AB8505_PIN_STATE(pin, conf, dev, state) \
	PIN_MAP_CONFIGS_PIN(dev, state, "pinctrl-ab8505.0", pin, abx500_##conf)

static struct pinctrl_map __initdata ab8500_pinmap[] = {
	/* Sysclkreq2 */
	AB8500_MUX_STATE("sysclkreq2_d_1", "sysclkreq", "regulator.35", PINCTRL_STATE_DEFAULT),
	AB8500_PIN_STATE("GPIO1_T10", in_nopull, "regulator.35", PINCTRL_STATE_DEFAULT),
	/* sysclkreq2 disable, mux in gpio configured in input pulldown */
	AB8500_MUX_STATE("gpio1_a_1", "gpio", "regulator.35", PINCTRL_STATE_SLEEP),
	AB8500_PIN_STATE("GPIO1_T10", in_pd, "regulator.35", PINCTRL_STATE_SLEEP),

	/* pins 2 is muxed in GPIO, configured in INPUT PULL DOWN */
	AB8500_MUX_HOG("gpio2_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO2_T9", in_pd),

	/* Sysclkreq4 */
	AB8500_MUX_STATE("sysclkreq4_d_1", "sysclkreq", "regulator.36", PINCTRL_STATE_DEFAULT),
	AB8500_PIN_STATE("GPIO3_U9", in_nopull, "regulator.36", PINCTRL_STATE_DEFAULT),
	/* sysclkreq4 disable, mux in gpio configured in input pulldown */
	AB8500_MUX_STATE("gpio3_a_1", "gpio", "regulator.36", PINCTRL_STATE_SLEEP),
	AB8500_PIN_STATE("GPIO3_U9", in_pd, "regulator.36", PINCTRL_STATE_SLEEP),

	/* pins 4 is muxed in GPIO, configured in INPUT PULL DOWN */
	AB8500_MUX_HOG("gpio4_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO4_W2", in_pd),

	/*
	 * pins 6,7,8 and 9 are muxed in YCBCR0123
	 * configured in INPUT PULL UP
	 */
	AB8500_MUX_HOG("ycbcr0123_d_1", "ycbcr"),
	AB8500_PIN_HOG("GPIO6_Y18", in_nopull),
	AB8500_PIN_HOG("GPIO7_AA20", in_nopull),
	AB8500_PIN_HOG("GPIO8_W18", in_nopull),
	AB8500_PIN_HOG("GPIO9_AA19", in_nopull),

	/*
	 * pins 10,11,12 and 13 are muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio10_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO10_U17", in_pd),

	AB8500_MUX_HOG("gpio11_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO11_AA18", in_pd),

	AB8500_MUX_HOG("gpio12_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO12_U16", in_pd),

	AB8500_MUX_HOG("gpio13_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO13_W17", in_pd),

	/*
	 * pins 14,15 are muxed in PWM1 and PWM2
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("pwmout1_d_1", "pwmout"),
	AB8500_PIN_HOG("GPIO14_F14", in_pd),

	AB8500_MUX_HOG("pwmout2_d_1", "pwmout"),
	AB8500_PIN_HOG("GPIO15_B17", in_pd),

	/*
	 * pins 16 is muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio16_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO14_F14", in_pd),

	/*
	 * pins 17,18,19 and 20 are muxed in AUDIO interface 1
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("adi1_d_1", "adi1"),
	AB8500_PIN_HOG("GPIO17_P5", in_pd),
	AB8500_PIN_HOG("GPIO18_R5", in_pd),
	AB8500_PIN_HOG("GPIO19_U5", in_pd),
	AB8500_PIN_HOG("GPIO20_T5", in_pd),

	/*
	 * pins 21,22 and 23 are muxed in USB UICC
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("usbuicc_d_1", "usbuicc"),
	AB8500_PIN_HOG("GPIO21_H19", in_pd),
	AB8500_PIN_HOG("GPIO22_G20", in_pd),
	AB8500_PIN_HOG("GPIO23_G19", in_pd),

	/*
	 * pins 24,25 are muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio24_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO24_T14", in_pd),

	AB8500_MUX_HOG("gpio25_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO25_R16", in_pd),

	/*
	 * pins 26 is muxed in GPIO
	 * configured in OUTPUT LOW
	 */
	AB8500_MUX_HOG("gpio26_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO26_M16", out_lo),

	/*
	 * pins 27,28 are muxed in DMIC12
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("dmic12_d_1", "dmic"),
	AB8500_PIN_HOG("GPIO27_J6", in_pd),
	AB8500_PIN_HOG("GPIO28_K6", in_pd),

	/*
	 * pins 29,30 are muxed in DMIC34
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("dmic34_d_1", "dmic"),
	AB8500_PIN_HOG("GPIO29_G6", in_pd),
	AB8500_PIN_HOG("GPIO30_H6", in_pd),

	/*
	 * pins 31,32 are muxed in DMIC56
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("dmic56_d_1", "dmic"),
	AB8500_PIN_HOG("GPIO31_F5", in_pd),
	AB8500_PIN_HOG("GPIO32_G5", in_pd),

	/*
	 * pins 34 is muxed in EXTCPENA
	 * configured INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("extcpena_d_1", "extcpena"),
	AB8500_PIN_HOG("GPIO34_R17", in_pd),

	/*
	 * pins 35 is muxed in GPIO
	 * configured in OUTPUT LOW
	 */
	AB8500_MUX_HOG("gpio35_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO35_W15", in_pd),

	/*
	 * pins 36,37,38 and 39 are muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio36_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO36_A17", in_pd),

	AB8500_MUX_HOG("gpio37_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO37_E15", in_pd),

	AB8500_MUX_HOG("gpio38_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO38_C17", in_pd),

	AB8500_MUX_HOG("gpio39_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO39_E16", in_pd),

	/*
	 * pins 40 and 41 are muxed in MODCSLSDA
	 * configured INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("modsclsda_d_1", "modsclsda"),
	AB8500_PIN_HOG("GPIO40_T19", in_pd),
	AB8500_PIN_HOG("GPIO41_U19", in_pd),

	/*
	 * pins 42 is muxed in GPIO
	 * configured INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio42_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO42_U2", in_pd),
};

static struct pinctrl_map __initdata ab8505_pinmap[] = {
	/* Sysclkreq2 */
	AB8505_MUX_STATE("sysclkreq2_d_1", "sysclkreq", "regulator.36", PINCTRL_STATE_DEFAULT),
	AB8505_PIN_STATE("GPIO1_N4", in_nopull, "regulator.36", PINCTRL_STATE_DEFAULT),
	/* sysclkreq2 disable, mux in gpio configured in input pulldown */
	AB8505_MUX_STATE("gpio1_a_1", "gpio", "regulator.36", PINCTRL_STATE_SLEEP),
	AB8505_PIN_STATE("GPIO1_N4", in_pd, "regulator.36", PINCTRL_STATE_SLEEP),

	/* pins 2 is muxed in GPIO, configured in INPUT PULL DOWN */
	AB8505_MUX_HOG("gpio2_a_1", "gpio"),
	AB8505_PIN_HOG("GPIO2_R5", in_pd),

	/* Sysclkreq4 */
	AB8505_MUX_STATE("sysclkreq4_d_1", "sysclkreq", "regulator.37", PINCTRL_STATE_DEFAULT),
	AB8505_PIN_STATE("GPIO3_P5", in_nopull, "regulator.37", PINCTRL_STATE_DEFAULT),
	/* sysclkreq4 disable, mux in gpio configured in input pulldown */
	AB8505_MUX_STATE("gpio3_a_1", "gpio", "regulator.37", PINCTRL_STATE_SLEEP),
	AB8505_PIN_STATE("GPIO3_P5", in_pd, "regulator.37", PINCTRL_STATE_SLEEP),

	AB8505_MUX_HOG("gpio10_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO10_B16", in_pd),

	AB8505_MUX_HOG("gpio11_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO11_B17", in_pd),

	AB8505_MUX_HOG("gpio13_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO13_D17", in_nopull),

	AB8505_MUX_HOG("pwmout1_d_1", "pwmout"),
	AB8505_PIN_HOG("GPIO14_C16", in_pd),

	AB8505_MUX_HOG("adi2_d_1", "adi2"),
	AB8505_PIN_HOG("GPIO17_P2", in_pd),
	AB8505_PIN_HOG("GPIO18_N3", in_pd),
	AB8505_PIN_HOG("GPIO19_T1", in_pd),
	AB8505_PIN_HOG("GPIO20_P3", in_pd),

	AB8505_MUX_HOG("gpio34_a_1", "gpio"),
	AB8505_PIN_HOG("GPIO34_H14", in_pd),

	AB8505_MUX_HOG("modsclsda_d_1", "modsclsda"),
	AB8505_PIN_HOG("GPIO40_J15", in_pd),
	AB8505_PIN_HOG("GPIO41_J14", in_pd),

	AB8505_MUX_HOG("gpio50_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO50_L4", in_nopull),

	AB8505_MUX_HOG("resethw_d_1", "resethw"),
	AB8505_PIN_HOG("GPIO52_D16", in_pd),

	AB8505_MUX_HOG("service_d_1", "service"),
	AB8505_PIN_HOG("GPIO53_D15", in_pd),
};

void __init mop500_pinmaps_init(void)
{
	if (machine_is_u8520())
		pinctrl_register_mappings(ab8505_pinmap,
					  ARRAY_SIZE(ab8505_pinmap));
	else
		pinctrl_register_mappings(ab8500_pinmap,
					  ARRAY_SIZE(ab8500_pinmap));
}

void __init snowball_pinmaps_init(void)
{
	pinctrl_register_mappings(ab8500_pinmap,
				  ARRAY_SIZE(ab8500_pinmap));
}

void __init hrefv60_pinmaps_init(void)
{
	pinctrl_register_mappings(ab8500_pinmap,
				  ARRAY_SIZE(ab8500_pinmap));
}
