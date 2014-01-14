#include "bcm47xx_private.h"

#include <linux/leds.h>
#include <bcm47xx_board.h>

static const struct gpio_led
bcm47xx_leds_netgear_wndr4500_v1_leds[] __initconst = {
	{
		.name		= "bcm47xx:green:wps",
		.gpio		= 1,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_KEEP,
	},
	{
		.name		= "bcm47xx:green:power",
		.gpio		= 2,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_KEEP,
	},
	{
		.name		= "bcm47xx:orange:power",
		.gpio		= 3,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_KEEP,
	},
	{
		.name		= "bcm47xx:green:usb1",
		.gpio		= 8,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_KEEP,
	},
	{
		.name		= "bcm47xx:green:2ghz",
		.gpio		= 9,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_KEEP,
	},
	{
		.name		= "bcm47xx:blue:5ghz",
		.gpio		= 11,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_KEEP,
	},
	{
		.name		= "bcm47xx:green:usb2",
		.gpio		= 14,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_KEEP,
	},
};

static struct gpio_led_platform_data bcm47xx_leds_pdata;

#define bcm47xx_set_pdata(dev_leds) do {				\
	bcm47xx_leds_pdata.leds = dev_leds;				\
	bcm47xx_leds_pdata.num_leds = ARRAY_SIZE(dev_leds);		\
} while (0)

void __init bcm47xx_leds_register(void)
{
	enum bcm47xx_board board = bcm47xx_board_get();

	switch (board) {
	case BCM47XX_BOARD_NETGEAR_WNDR4500V1:
		bcm47xx_set_pdata(bcm47xx_leds_netgear_wndr4500_v1_leds);
		break;
	default:
		pr_debug("No LEDs configuration found for this device\n");
		return;
	}

	gpio_led_register_device(-1, &bcm47xx_leds_pdata);
}
