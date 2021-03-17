// SPDX-License-Identifier: GPL-2.0
#include "bcm47xx_private.h"

#include <linux/gpio.h>
#include <bcm47xx_board.h>
#include <bcm47xx.h>

static void __init bcm47xx_workarounds_netgear_wnr3500l(void)
{
	const int usb_power = 12;
	int err;

	err = gpio_request_one(usb_power, GPIOF_OUT_INIT_HIGH, "usb_power");
	if (err)
		pr_err("Failed to request USB power gpio: %d\n", err);
	else
		gpio_free(usb_power);
}

void __init bcm47xx_workarounds(void)
{
	enum bcm47xx_board board = bcm47xx_board_get();

	switch (board) {
	case BCM47XX_BOARD_NETGEAR_WNR3500L:
		bcm47xx_workarounds_netgear_wnr3500l();
		break;
	default:
		/* No workaround(s) needed */
		break;
	}
}
