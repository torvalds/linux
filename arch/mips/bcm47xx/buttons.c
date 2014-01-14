#include "bcm47xx_private.h"

#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/interrupt.h>
#include <linux/ssb/ssb_embedded.h>
#include <bcm47xx_board.h>
#include <bcm47xx.h>

/**************************************************
 * Database
 **************************************************/

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wndr4500_v1[] __initconst = {
	{
		.code		= KEY_WPS_BUTTON,
		.gpio		= 4,
		.active_low	= 1,
	},
	{
		.code		= KEY_RFKILL,
		.gpio		= 5,
		.active_low	= 1,
	},
	{
		.code		= KEY_RESTART,
		.gpio		= 6,
		.active_low	= 1,
	},
};

/**************************************************
 * Init
 **************************************************/

static struct gpio_keys_platform_data bcm47xx_button_pdata;

static struct platform_device bcm47xx_buttons_gpio_keys = {
	.name = "gpio-keys",
	.dev = {
		.platform_data = &bcm47xx_button_pdata,
	}
};

/* Copy data from __initconst */
static int __init bcm47xx_buttons_copy(const struct gpio_keys_button *buttons,
				       size_t nbuttons)
{
	size_t size = nbuttons * sizeof(*buttons);

	bcm47xx_button_pdata.buttons = kmalloc(size, GFP_KERNEL);
	if (!bcm47xx_button_pdata.buttons)
		return -ENOMEM;
	memcpy(bcm47xx_button_pdata.buttons, buttons, size);
	bcm47xx_button_pdata.nbuttons = nbuttons;

	return 0;
}

#define bcm47xx_copy_bdata(dev_buttons)					\
	bcm47xx_buttons_copy(dev_buttons, ARRAY_SIZE(dev_buttons));

int __init bcm47xx_buttons_register(void)
{
	enum bcm47xx_board board = bcm47xx_board_get();
	int err;

#ifdef CONFIG_BCM47XX_SSB
	if (bcm47xx_bus_type == BCM47XX_BUS_TYPE_SSB) {
		pr_debug("Buttons on SSB are not supported yet.\n");
		return -ENOTSUPP;
	}
#endif

	switch (board) {
	case BCM47XX_BOARD_NETGEAR_WNDR4500V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wndr4500_v1);
		break;
	default:
		pr_debug("No buttons configuration found for this device\n");
		return -ENOTSUPP;
	}

	if (err)
		return -ENOMEM;

	err = platform_device_register(&bcm47xx_buttons_gpio_keys);
	if (err) {
		pr_err("Failed to register platform device: %d\n", err);
		return err;
	}

	return 0;
}
