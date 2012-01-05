/*
 * arch/arm/mach-orion5x/lsmini-setup.c
 *
 * Maintainer: Alexey Kopytko <alexey@kopytko.ru>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/system.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * Linkstation Mini Info
 ****************************************************************************/

/*
 * 256K NOR flash Device bus boot chip select
 */

#define LSMINI_NOR_BOOT_BASE	0xf4000000
#define LSMINI_NOR_BOOT_SIZE	SZ_256K

/*****************************************************************************
 * 256KB NOR Flash on BOOT Device
 ****************************************************************************/

static struct physmap_flash_data lsmini_nor_flash_data = {
	.width		= 1,
};

static struct resource lsmini_nor_flash_resource = {
	.flags	= IORESOURCE_MEM,
	.start	= LSMINI_NOR_BOOT_BASE,
	.end	= LSMINI_NOR_BOOT_BASE + LSMINI_NOR_BOOT_SIZE - 1,
};

static struct platform_device lsmini_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &lsmini_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &lsmini_nor_flash_resource,
};

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data lsmini_eth_data = {
	.phy_addr	= 8,
};

/*****************************************************************************
 * RTC 5C372a on I2C bus
 ****************************************************************************/

static struct i2c_board_info __initdata lsmini_i2c_rtc = {
	I2C_BOARD_INFO("rs5c372a", 0x32),
};

/*****************************************************************************
 * LEDs attached to GPIO
 ****************************************************************************/

#define LSMINI_GPIO_LED_ALARM	2
#define LSMINI_GPIO_LED_INFO	3
#define LSMINI_GPIO_LED_FUNC	9
#define LSMINI_GPIO_LED_PWR	14

static struct gpio_led lsmini_led_pins[] = {
	{
		.name	   = "alarm:red",
		.gpio	   = LSMINI_GPIO_LED_ALARM,
		.active_low     = 1,
	}, {
		.name	   = "info:amber",
		.gpio	   = LSMINI_GPIO_LED_INFO,
		.active_low     = 1,
	}, {
		.name	   = "func:blue:top",
		.gpio	   = LSMINI_GPIO_LED_FUNC,
		.active_low     = 1,
	}, {
		.name	   = "power:blue:bottom",
		.gpio	   = LSMINI_GPIO_LED_PWR,
	},
};

static struct gpio_led_platform_data lsmini_led_data = {
	.leds	   = lsmini_led_pins,
	.num_leds       = ARRAY_SIZE(lsmini_led_pins),
};

static struct platform_device lsmini_leds = {
	.name   = "leds-gpio",
	.id     = -1,
	.dev    = {
		.platform_data  = &lsmini_led_data,
	},
};

/****************************************************************************
 * GPIO Attached Keys
 ****************************************************************************/

#define LSMINI_GPIO_KEY_FUNC       15
#define LSMINI_GPIO_KEY_POWER	   18
#define LSMINI_GPIO_KEY_AUTOPOWER 17

#define LSMINI_SW_POWER		0x00
#define LSMINI_SW_AUTOPOWER	0x01

static struct gpio_keys_button lsmini_buttons[] = {
	{
		.code	   = KEY_OPTION,
		.gpio	   = LSMINI_GPIO_KEY_FUNC,
		.desc	   = "Function Button",
		.active_low     = 1,
	}, {
		.type		= EV_SW,
		.code	   = LSMINI_SW_POWER,
		.gpio	   = LSMINI_GPIO_KEY_POWER,
		.desc	   = "Power-on Switch",
		.active_low     = 1,
	}, {
		.type		= EV_SW,
		.code	   = LSMINI_SW_AUTOPOWER,
		.gpio	   = LSMINI_GPIO_KEY_AUTOPOWER,
		.desc	   = "Power-auto Switch",
		.active_low     = 1,
	},
};

static struct gpio_keys_platform_data lsmini_button_data = {
	.buttons	= lsmini_buttons,
	.nbuttons       = ARRAY_SIZE(lsmini_buttons),
};

static struct platform_device lsmini_button_device = {
	.name	   = "gpio-keys",
	.id	     = -1,
	.num_resources  = 0,
	.dev	    = {
		.platform_data  = &lsmini_button_data,
	},
};


/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct mv_sata_platform_data lsmini_sata_data = {
	.n_ports	= 2,
};


/*****************************************************************************
 * Linkstation Mini specific power off method: reboot
 ****************************************************************************/
/*
 * On the Linkstation Mini, the shutdown process is following:
 * - Userland monitors key events until the power switch goes to off position
 * - The board reboots
 * - U-boot starts and goes into an idle mode waiting for the user
 *   to move the switch to ON position
 */

static void lsmini_power_off(void)
{
	orion5x_restart('h', NULL);
}


/*****************************************************************************
 * General Setup
 ****************************************************************************/

#define LSMINI_GPIO_USB_POWER	16
#define LSMINI_GPIO_AUTO_POWER	17
#define LSMINI_GPIO_POWER	18

#define LSMINI_GPIO_HDD_POWER0	1
#define LSMINI_GPIO_HDD_POWER1	19

static unsigned int lsmini_mpp_modes[] __initdata = {
	MPP0_UNUSED, /* LED_RESERVE1 (unused) */
	MPP1_GPIO, /* HDD_PWR */
	MPP2_GPIO, /* LED_ALARM */
	MPP3_GPIO, /* LED_INFO */
	MPP4_UNUSED,
	MPP5_UNUSED,
	MPP6_UNUSED,
	MPP7_UNUSED,
	MPP8_UNUSED,
	MPP9_GPIO, /* LED_FUNC */
	MPP10_UNUSED,
	MPP11_UNUSED, /* LED_ETH (dummy) */
	MPP12_UNUSED,
	MPP13_UNUSED,
	MPP14_GPIO, /* LED_PWR */
	MPP15_GPIO, /* FUNC */
	MPP16_GPIO, /* USB_PWR */
	MPP17_GPIO, /* AUTO_POWER */
	MPP18_GPIO, /* POWER */
	MPP19_GPIO, /* HDD_PWR1 */
	0,
};

static void __init lsmini_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(lsmini_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_ehci1_init();
	orion5x_eth_init(&lsmini_eth_data);
	orion5x_i2c_init();
	orion5x_sata_init(&lsmini_sata_data);
	orion5x_uart0_init();
	orion5x_xor_init();

	orion5x_setup_dev_boot_win(LSMINI_NOR_BOOT_BASE,
				   LSMINI_NOR_BOOT_SIZE);
	platform_device_register(&lsmini_nor_flash);

	platform_device_register(&lsmini_button_device);

	platform_device_register(&lsmini_leds);

	i2c_register_board_info(0, &lsmini_i2c_rtc, 1);

	/* enable USB power */
	gpio_set_value(LSMINI_GPIO_USB_POWER, 1);

	/* register power-off method */
	pm_power_off = lsmini_power_off;

	pr_info("%s: finished\n", __func__);
}

#ifdef CONFIG_MACH_LINKSTATION_MINI
MACHINE_START(LINKSTATION_MINI, "Buffalo Linkstation Mini")
	/* Maintainer: Alexey Kopytko <alexey@kopytko.ru> */
	.atag_offset	= 0x100,
	.init_machine	= lsmini_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
	.restart	= orion5x_restart,
MACHINE_END
#endif
