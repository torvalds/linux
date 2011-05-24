/*
 * Copyright (C) 2010 Linaro Limited
 *
 * based on code from the following
 * Copyright 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2009-2010 Pegatron Corporation. All Rights Reserved.
 * Copyright 2009-2010 Genesi USA, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/mfd/mc13892.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"
#include "devices.h"
#include "efika.h"

#define EFIKAMX_PCBID0		IMX_GPIO_NR(3, 16)
#define EFIKAMX_PCBID1		IMX_GPIO_NR(3, 17)
#define EFIKAMX_PCBID2		IMX_GPIO_NR(3, 11)

#define EFIKAMX_BLUE_LED	IMX_GPIO_NR(3, 13)
#define EFIKAMX_GREEN_LED	IMX_GPIO_NR(3, 14)
#define EFIKAMX_RED_LED		IMX_GPIO_NR(3, 15)

#define EFIKAMX_POWER_KEY	IMX_GPIO_NR(2, 31)

/* board 1.1 doesn't have same reset gpio */
#define EFIKAMX_RESET1_1	IMX_GPIO_NR(3, 2)
#define EFIKAMX_RESET		IMX_GPIO_NR(1, 4)

#define EFIKAMX_POWEROFF	IMX_GPIO_NR(4, 13)

#define EFIKAMX_PMIC		IMX_GPIO_NR(1, 6)

/* the pci ids pin have pull up. they're driven low according to board id */
#define MX51_PAD_PCBID0	IOMUX_PAD(0x518, 0x130, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PCBID1	IOMUX_PAD(0x51C, 0x134, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PCBID2	IOMUX_PAD(0x504, 0x128, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PWRKEY	IOMUX_PAD(0x48c, 0x0f8, 1, 0x0,   0, PAD_CTL_PUS_100K_UP | PAD_CTL_PKE)

static iomux_v3_cfg_t mx51efikamx_pads[] = {
	/* board id */
	MX51_PAD_PCBID0,
	MX51_PAD_PCBID1,
	MX51_PAD_PCBID2,

	/* leds */
	MX51_PAD_CSI1_D9__GPIO3_13,
	MX51_PAD_CSI1_VSYNC__GPIO3_14,
	MX51_PAD_CSI1_HSYNC__GPIO3_15,

	/* power key */
	MX51_PAD_PWRKEY,

	/* reset */
	MX51_PAD_DI1_PIN13__GPIO3_2,
	MX51_PAD_GPIO1_4__GPIO1_4,

	/* power off */
	MX51_PAD_CSI2_VSYNC__GPIO4_13,
};

/*   PCBID2  PCBID1 PCBID0  STATE
	1       1      1    ER1:rev1.1
	1       1      0    ER2:rev1.2
	1       0      1    ER3:rev1.3
	1       0      0    ER4:rev1.4
*/
static void __init mx51_efikamx_board_id(void)
{
	int id;

	/* things are taking time to settle */
	msleep(150);

	gpio_request(EFIKAMX_PCBID0, "pcbid0");
	gpio_direction_input(EFIKAMX_PCBID0);
	gpio_request(EFIKAMX_PCBID1, "pcbid1");
	gpio_direction_input(EFIKAMX_PCBID1);
	gpio_request(EFIKAMX_PCBID2, "pcbid2");
	gpio_direction_input(EFIKAMX_PCBID2);

	id = gpio_get_value(EFIKAMX_PCBID0);
	id |= gpio_get_value(EFIKAMX_PCBID1) << 1;
	id |= gpio_get_value(EFIKAMX_PCBID2) << 2;

	switch (id) {
	case 7:
		system_rev = 0x11;
		break;
	case 6:
		system_rev = 0x12;
		break;
	case 5:
		system_rev = 0x13;
		break;
	case 4:
		system_rev = 0x14;
		break;
	default:
		system_rev = 0x10;
		break;
	}

	if ((system_rev == 0x10)
		|| (system_rev == 0x12)
		|| (system_rev == 0x14)) {
		printk(KERN_WARNING
			"EfikaMX: Unsupported board revision 1.%u!\n",
			system_rev & 0xf);
	}
}

static struct gpio_led mx51_efikamx_leds[] = {
	{
		.name = "efikamx:green",
		.default_trigger = "default-on",
		.gpio = EFIKAMX_GREEN_LED,
	},
	{
		.name = "efikamx:red",
		.default_trigger = "ide-disk",
		.gpio = EFIKAMX_RED_LED,
	},
	{
		.name = "efikamx:blue",
		.default_trigger = "mmc0",
		.gpio = EFIKAMX_BLUE_LED,
	},
};

static struct gpio_led_platform_data mx51_efikamx_leds_data = {
	.leds = mx51_efikamx_leds,
	.num_leds = ARRAY_SIZE(mx51_efikamx_leds),
};

static struct platform_device mx51_efikamx_leds_device = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &mx51_efikamx_leds_data,
	},
};

static struct gpio_keys_button mx51_efikamx_powerkey[] = {
	{
		.code = KEY_POWER,
		.gpio = EFIKAMX_POWER_KEY,
		.type = EV_PWR,
		.desc = "Power Button (CM)",
		.wakeup = 1,
		.debounce_interval = 10, /* ms */
	},
};

static const struct gpio_keys_platform_data mx51_efikamx_powerkey_data __initconst = {
	.buttons = mx51_efikamx_powerkey,
	.nbuttons = ARRAY_SIZE(mx51_efikamx_powerkey),
};

void mx51_efikamx_reset(void)
{
	if (system_rev == 0x11)
		gpio_direction_output(EFIKAMX_RESET1_1, 0);
	else
		gpio_direction_output(EFIKAMX_RESET, 0);
}

static struct regulator *pwgt1, *pwgt2, *coincell;

static void mx51_efikamx_power_off(void)
{
	if (!IS_ERR(coincell))
		regulator_disable(coincell);

	if (!IS_ERR(pwgt1) && !IS_ERR(pwgt2)) {
		regulator_disable(pwgt2);
		regulator_disable(pwgt1);
	}
	gpio_direction_output(EFIKAMX_POWEROFF, 1);
}

static int __init mx51_efikamx_power_init(void)
{
	if (machine_is_mx51_efikamx()) {
		pwgt1 = regulator_get(NULL, "pwgt1");
		pwgt2 = regulator_get(NULL, "pwgt2");
		if (!IS_ERR(pwgt1) && !IS_ERR(pwgt2)) {
			regulator_enable(pwgt1);
			regulator_enable(pwgt2);
		}
		gpio_request(EFIKAMX_POWEROFF, "poweroff");
		pm_power_off = mx51_efikamx_power_off;

		/* enable coincell charger. maybe need a small power driver ? */
		coincell = regulator_get(NULL, "coincell");
		if (!IS_ERR(coincell)) {
			regulator_set_voltage(coincell, 3000000, 3000000);
			regulator_enable(coincell);
		}

		regulator_has_full_constraints();
	}

	return 0;
}
late_initcall(mx51_efikamx_power_init);

static void __init mx51_efikamx_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx51efikamx_pads,
					ARRAY_SIZE(mx51efikamx_pads));
	efika_board_common_init();

	mx51_efikamx_board_id();

	/* on < 1.2 boards both SD controllers are used */
	if (system_rev < 0x12) {
		imx51_add_sdhci_esdhc_imx(1, NULL);
		mx51_efikamx_leds[2].default_trigger = "mmc1";
	}

	platform_device_register(&mx51_efikamx_leds_device);
	imx_add_gpio_keys(&mx51_efikamx_powerkey_data);

	if (system_rev == 0x11) {
		gpio_request(EFIKAMX_RESET1_1, "reset");
		gpio_direction_output(EFIKAMX_RESET1_1, 1);
	} else {
		gpio_request(EFIKAMX_RESET, "reset");
		gpio_direction_output(EFIKAMX_RESET, 1);
	}

	/*
	 * enable wifi by default only on mx
	 * sb and mx have same wlan pin but the value to enable it are
	 * different :/
	 */
	gpio_request(EFIKA_WLAN_EN, "wlan_en");
	gpio_direction_output(EFIKA_WLAN_EN, 0);
	msleep(10);

	gpio_request(EFIKA_WLAN_RESET, "wlan_rst");
	gpio_direction_output(EFIKA_WLAN_RESET, 0);
	msleep(10);
	gpio_set_value(EFIKA_WLAN_RESET, 1);
}

static void __init mx51_efikamx_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 24576000);
}

static struct sys_timer mx51_efikamx_timer = {
	.init = mx51_efikamx_timer_init,
};

MACHINE_START(MX51_EFIKAMX, "Genesi EfikaMX nettop")
	/* Maintainer: Amit Kucheria <amit.kucheria@linaro.org> */
	.boot_params = MX51_PHYS_OFFSET + 0x100,
	.map_io = mx51_map_io,
	.init_early = imx51_init_early,
	.init_irq = mx51_init_irq,
	.timer = &mx51_efikamx_timer,
	.init_machine = mx51_efikamx_init,
MACHINE_END
