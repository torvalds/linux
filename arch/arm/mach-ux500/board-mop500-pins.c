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
#include <linux/platform_data/pinctrl-nomadik.h>

#include <asm/mach-types.h>

#include "board-mop500.h"

enum custom_pin_cfg_t {
	PINS_FOR_DEFAULT,
	PINS_FOR_U9500,
};

static enum custom_pin_cfg_t pinsfor;

/* These simply sets bias for pins */
#define BIAS(a,b) static unsigned long a[] = { b }

BIAS(pd, PIN_PULL_DOWN);
BIAS(in_pu, PIN_INPUT_PULLUP);
BIAS(in_pd, PIN_INPUT_PULLDOWN);
BIAS(out_lo, PIN_OUTPUT_LOW);

BIAS(abx500_out_lo, PIN_CONF_PACKED(PIN_CONFIG_OUTPUT, 0));
BIAS(abx500_in_pd, PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 1));
BIAS(abx500_in_nopull, PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 0));

/* These also force them into GPIO mode */
BIAS(gpio_in_pu, PIN_INPUT_PULLUP|PIN_GPIOMODE_ENABLED);
BIAS(gpio_in_pd, PIN_INPUT_PULLDOWN|PIN_GPIOMODE_ENABLED);
BIAS(gpio_in_pu_slpm_gpio_nopull, PIN_INPUT_PULLUP|PIN_GPIOMODE_ENABLED|PIN_SLPM_GPIO|PIN_SLPM_INPUT_NOPULL);
BIAS(gpio_in_pd_slpm_gpio_nopull, PIN_INPUT_PULLDOWN|PIN_GPIOMODE_ENABLED|PIN_SLPM_GPIO|PIN_SLPM_INPUT_NOPULL);
BIAS(gpio_out_hi, PIN_OUTPUT_HIGH|PIN_GPIOMODE_ENABLED);
BIAS(gpio_out_lo, PIN_OUTPUT_LOW|PIN_GPIOMODE_ENABLED);

/* We use these to define hog settings that are always done on boot */
#define DB8500_MUX_HOG(group,func) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-db8500", group, func)
#define DB8500_PIN_HOG(pin,conf) \
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-db8500", pin, conf)

/* These are default states associated with device and changed runtime */
#define DB8500_MUX(group,func,dev) \
	PIN_MAP_MUX_GROUP_DEFAULT(dev, "pinctrl-db8500", group, func)
#define DB8500_PIN(pin,conf,dev) \
	PIN_MAP_CONFIGS_PIN_DEFAULT(dev, "pinctrl-db8500", pin, conf)
#define DB8500_PIN_IDLE(pin, conf, dev) \
	PIN_MAP_CONFIGS_PIN(dev, PINCTRL_STATE_IDLE, "pinctrl-db8500",	\
			    pin, conf)
#define DB8500_PIN_SLEEP(pin, conf, dev) \
	PIN_MAP_CONFIGS_PIN(dev, PINCTRL_STATE_SLEEP, "pinctrl-db8500",	\
			    pin, conf)
#define DB8500_MUX_STATE(group, func, dev, state) \
	PIN_MAP_MUX_GROUP(dev, state, "pinctrl-db8500", group, func)
#define DB8500_PIN_STATE(pin, conf, dev, state) \
	PIN_MAP_CONFIGS_PIN(dev, state, "pinctrl-db8500", pin, conf)

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

/*
 * The HREFv60 series of platforms is using available pins on the DB8500
 * insteaf of the Toshiba I2C GPIO expander, reusing some pins like the SSP0
 * and SSP1 ports (previously connected to the AB8500) as generic GPIO lines.
 */
static struct pinctrl_map __initdata hrefv60_pinmap[] = {
	/* Drive WLAN_ENA low */
	DB8500_PIN_HOG("GPIO85_D5", gpio_out_lo), /* WLAN_ENA */
	/*
	 * XENON Flashgun on image processor GPIO (controlled from image
	 * processor firmware), mux in these image processor GPIO lines 0
	 * (XENON_FLASH_ID), 1 (XENON_READY) and there is an assistant
	 * LED on IP GPIO 4 (XENON_EN2) on altfunction C, that need bias
	 * from GPIO21 so pull up 0, 1 and drive 4 and GPIO21 low as output.
	 */
	DB8500_MUX_HOG("ipgpio0_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio1_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio4_c_1", "ipgpio"),
	DB8500_PIN_HOG("GPIO6_AF6", in_pu), /* XENON_FLASH_ID */
	DB8500_PIN_HOG("GPIO7_AG5", in_pu), /* XENON_READY */
	DB8500_PIN_HOG("GPIO21_AB3", gpio_out_lo), /* XENON_EN1 */
	DB8500_PIN_HOG("GPIO64_F3", out_lo), /* XENON_EN2 */
	/* Magnetometer uses GPIO 31 and 32, pull these up/down respectively */
	DB8500_PIN_HOG("GPIO31_V3", gpio_in_pu), /* EN1 */
	DB8500_PIN_HOG("GPIO32_V2", gpio_in_pd), /* DRDY */
	/*
	 * Display Interface 1 uses GPIO 65 for RST (reset).
	 * Display Interface 2 uses GPIO 66 for RST (reset).
	 * Drive DISP1 reset high (not reset), driver DISP2 reset low (reset)
	 */
	DB8500_PIN_HOG("GPIO65_F1", gpio_out_hi), /* DISP1 NO RST */
	DB8500_PIN_HOG("GPIO66_G3", gpio_out_lo), /* DISP2 RST */
	/*
	 * Touch screen uses GPIO 143 for RST1, GPIO 146 for RST2 and
	 * GPIO 67 for interrupts. Pull-up the IRQ line and drive both
	 * reset signals low.
	 */
	DB8500_PIN_HOG("GPIO143_D12", gpio_out_lo), /* TOUCH_RST1 */
	DB8500_PIN_HOG("GPIO67_G2", gpio_in_pu), /* TOUCH_INT2 */
	DB8500_PIN_HOG("GPIO146_D13", gpio_out_lo), /* TOUCH_RST2 */
	/*
	 * Drive D19-D23 for the ETM PTM trace interface low,
	 * (presumably pins are unconnected therefore grounded here,
	 * the "other alt C1" setting enables these pins)
	 */
	DB8500_PIN_HOG("GPIO70_G5", gpio_out_lo),
	DB8500_PIN_HOG("GPIO71_G4", gpio_out_lo),
	DB8500_PIN_HOG("GPIO72_H4", gpio_out_lo),
	DB8500_PIN_HOG("GPIO73_H3", gpio_out_lo),
	DB8500_PIN_HOG("GPIO74_J3", gpio_out_lo),
	/* NAHJ CTRL on GPIO 76 to low, CTRL_INV on GPIO216 to high */
	DB8500_PIN_HOG("GPIO76_J2", gpio_out_lo), /* CTRL */
	DB8500_PIN_HOG("GPIO216_AG12", gpio_out_hi), /* CTRL_INV */
	/* NFC ENA and RESET to low, pulldown IRQ line */
	DB8500_PIN_HOG("GPIO77_H1", gpio_out_lo), /* NFC_ENA */
	DB8500_PIN_HOG("GPIO144_B13", gpio_in_pd), /* NFC_IRQ */
	DB8500_PIN_HOG("GPIO142_C11", gpio_out_lo), /* NFC_RESET */
	DB8500_PIN_HOG("GPIO91_B6", gpio_in_pu), /* FORCE_SENSING_INT */
	DB8500_PIN_HOG("GPIO92_D6", gpio_out_lo), /* FORCE_SENSING_RST */
	DB8500_PIN_HOG("GPIO97_D9", gpio_out_lo), /* FORCE_SENSING_WU */
	/* DiPro Sensor interrupt */
	DB8500_PIN_HOG("GPIO139_C9", gpio_in_pu), /* DIPRO_INT */
	/* Audio Amplifier HF enable */
	DB8500_PIN_HOG("GPIO149_B14", gpio_out_hi), /* VAUDIO_HF_EN, enable MAX8968 */
	/* GBF interface, pull low to reset state */
	DB8500_PIN_HOG("GPIO171_D23", gpio_out_lo), /* GBF_ENA_RESET */
	/* MSP : HDTV INTERFACE GPIO line */
	DB8500_PIN_HOG("GPIO192_AJ27", gpio_in_pd),
	/* Accelerometer interrupt lines */
	DB8500_PIN_HOG("GPIO82_C1", gpio_in_pu), /* ACC_INT1 */
	DB8500_PIN_HOG("GPIO83_D3", gpio_in_pu), /* ACC_INT2 */
};

static struct pinctrl_map __initdata u9500_pinmap[] = {
	/* WLAN_IRQ line */
	DB8500_PIN_HOG("GPIO144_B13", gpio_in_pu),
	/* HSI */
	DB8500_MUX_HOG("hsir_a_1", "hsi"),
	DB8500_MUX_HOG("hsit_a_2", "hsi"),
	DB8500_PIN_HOG("GPIO219_AG10", in_pd), /* RX FLA0 */
	DB8500_PIN_HOG("GPIO220_AH10", in_pd), /* RX DAT0 */
	DB8500_PIN_HOG("GPIO221_AJ11", out_lo), /* RX RDY0 */
	DB8500_PIN_HOG("GPIO222_AJ9", out_lo), /* TX FLA0 */
	DB8500_PIN_HOG("GPIO223_AH9", out_lo), /* TX DAT0 */
	DB8500_PIN_HOG("GPIO224_AG9", in_pd), /* TX RDY0 */
	DB8500_PIN_HOG("GPIO225_AG8", in_pd), /* CAWAKE0 */
	DB8500_PIN_HOG("GPIO226_AF8", gpio_out_hi), /* ACWAKE0 */
};

static struct pinctrl_map __initdata u8500_pinmap[] = {
	DB8500_PIN_HOG("GPIO226_AF8", gpio_out_lo), /* WLAN_PMU_EN */
	DB8500_PIN_HOG("GPIO4_AH6", gpio_in_pu), /* WLAN_IRQ */
};

static struct pinctrl_map __initdata snowball_pinmap[] = {
	/* Mux in SSP0 connected to AB8500, pull down RXD pin */
	DB8500_MUX_HOG("ssp0_a_1", "ssp0"),
	DB8500_PIN_HOG("GPIO145_C13", pd),
	/* Mux in "SM" which is used for the SMSC911x Ethernet adapter */
	DB8500_MUX_HOG("sm_b_1", "sm"),
	/* User LED */
	DB8500_PIN_HOG("GPIO142_C11", gpio_out_hi),
	/* Drive RSTn_LAN high */
	DB8500_PIN_HOG("GPIO141_C12", gpio_out_hi),
	/*  Accelerometer/Magnetometer */
	DB8500_PIN_HOG("GPIO163_C20", gpio_in_pu), /* ACCEL_IRQ1 */
	DB8500_PIN_HOG("GPIO164_B21", gpio_in_pu), /* ACCEL_IRQ2 */
	DB8500_PIN_HOG("GPIO165_C21", gpio_in_pu), /* MAG_DRDY */
	/* WLAN/GBF */
	DB8500_PIN_HOG("GPIO161_D21", gpio_out_lo), /* WLAN_PMU_EN */
	DB8500_PIN_HOG("GPIO171_D23", gpio_out_hi), /* GBF_ENA */
	DB8500_PIN_HOG("GPIO215_AH13", gpio_out_lo), /* WLAN_ENA */
	DB8500_PIN_HOG("GPIO216_AG12", gpio_in_pu), /* WLAN_IRQ */
};

/*
 * passing "pinsfor=" in kernel cmdline allows for custom
 * configuration of GPIOs on u8500 derived boards.
 */
static int __init early_pinsfor(char *p)
{
	pinsfor = PINS_FOR_DEFAULT;

	if (strcmp(p, "u9500-21") == 0)
		pinsfor = PINS_FOR_U9500;

	return 0;
}
early_param("pinsfor", early_pinsfor);

int pins_for_u9500(void)
{
	if (pinsfor == PINS_FOR_U9500)
		return 1;

	return 0;
}

static void __init mop500_href_family_pinmaps_init(void)
{
	switch (pinsfor) {
	case PINS_FOR_U9500:
		pinctrl_register_mappings(u9500_pinmap,
					  ARRAY_SIZE(u9500_pinmap));
		break;
	case PINS_FOR_DEFAULT:
		pinctrl_register_mappings(u8500_pinmap,
					  ARRAY_SIZE(u8500_pinmap));
	default:
		break;
	}
}

void __init mop500_pinmaps_init(void)
{
	mop500_href_family_pinmaps_init();
	if (machine_is_u8520())
		pinctrl_register_mappings(ab8505_pinmap,
					  ARRAY_SIZE(ab8505_pinmap));
	else
		pinctrl_register_mappings(ab8500_pinmap,
					  ARRAY_SIZE(ab8500_pinmap));
}

void __init snowball_pinmaps_init(void)
{
	pinctrl_register_mappings(snowball_pinmap,
				  ARRAY_SIZE(snowball_pinmap));
	pinctrl_register_mappings(u8500_pinmap,
				  ARRAY_SIZE(u8500_pinmap));
	pinctrl_register_mappings(ab8500_pinmap,
				  ARRAY_SIZE(ab8500_pinmap));
}

void __init hrefv60_pinmaps_init(void)
{
	pinctrl_register_mappings(hrefv60_pinmap,
				  ARRAY_SIZE(hrefv60_pinmap));
	mop500_href_family_pinmaps_init();
	pinctrl_register_mappings(ab8500_pinmap,
				  ARRAY_SIZE(ab8500_pinmap));
}
