/*
 * Copyright (C) 2010 <LW@KARO-electronics.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/fec.h>
#include <linux/gpio.h>

#include <mach/iomux-mx28.h>
#include "../devices-mx28.h"

#include "module-tx28.h"

#define TX28_FEC_PHY_POWER	MXS_GPIO_NR(3, 29)
#define TX28_FEC_PHY_RESET	MXS_GPIO_NR(4, 13)

static const iomux_cfg_t tx28_fec_gpio_pads[] __initconst = {
	/* PHY POWER */
	MX28_PAD_PWM4__GPIO_3_29 |
		MXS_PAD_4MA | MXS_PAD_NOPULL | MXS_PAD_3V3,
	/* PHY RESET */
	MX28_PAD_ENET0_RX_CLK__GPIO_4_13 |
		MXS_PAD_4MA | MXS_PAD_NOPULL | MXS_PAD_3V3,
	/* Mode strap pins 0-2 */
	MX28_PAD_ENET0_RXD0__GPIO_4_3 |
		MXS_PAD_8MA | MXS_PAD_PULLUP | MXS_PAD_3V3,
	MX28_PAD_ENET0_RXD1__GPIO_4_4 |
		MXS_PAD_8MA | MXS_PAD_PULLUP | MXS_PAD_3V3,
	MX28_PAD_ENET0_RX_EN__GPIO_4_2 |
		MXS_PAD_8MA | MXS_PAD_PULLUP | MXS_PAD_3V3,
	/* nINT */
	MX28_PAD_ENET0_TX_CLK__GPIO_4_5 |
		MXS_PAD_4MA | MXS_PAD_NOPULL | MXS_PAD_3V3,

	MX28_PAD_ENET0_MDC__GPIO_4_0,
	MX28_PAD_ENET0_MDIO__GPIO_4_1,
	MX28_PAD_ENET0_TX_EN__GPIO_4_6,
	MX28_PAD_ENET0_TXD0__GPIO_4_7,
	MX28_PAD_ENET0_TXD1__GPIO_4_8,
	MX28_PAD_ENET_CLK__GPIO_4_16,
};

#define FEC_MODE (MXS_PAD_8MA | MXS_PAD_PULLUP | MXS_PAD_3V3)
static const iomux_cfg_t tx28_fec0_pads[] __initconst = {
	MX28_PAD_ENET0_MDC__ENET0_MDC | FEC_MODE,
	MX28_PAD_ENET0_MDIO__ENET0_MDIO | FEC_MODE,
	MX28_PAD_ENET0_RX_EN__ENET0_RX_EN | FEC_MODE,
	MX28_PAD_ENET0_RXD0__ENET0_RXD0 | FEC_MODE,
	MX28_PAD_ENET0_RXD1__ENET0_RXD1 | FEC_MODE,
	MX28_PAD_ENET0_TX_EN__ENET0_TX_EN | FEC_MODE,
	MX28_PAD_ENET0_TXD0__ENET0_TXD0 | FEC_MODE,
	MX28_PAD_ENET0_TXD1__ENET0_TXD1 | FEC_MODE,
	MX28_PAD_ENET_CLK__CLKCTRL_ENET | FEC_MODE,
};

static const iomux_cfg_t tx28_fec1_pads[] __initconst = {
	MX28_PAD_ENET0_RXD2__ENET1_RXD0,
	MX28_PAD_ENET0_RXD3__ENET1_RXD1,
	MX28_PAD_ENET0_TXD2__ENET1_TXD0,
	MX28_PAD_ENET0_TXD3__ENET1_TXD1,
	MX28_PAD_ENET0_COL__ENET1_TX_EN,
	MX28_PAD_ENET0_CRS__ENET1_RX_EN,
};

static struct fec_platform_data tx28_fec0_data = {
	.phy = PHY_INTERFACE_MODE_RMII,
};

static struct fec_platform_data tx28_fec1_data = {
	.phy = PHY_INTERFACE_MODE_RMII,
};

int __init tx28_add_fec0(void)
{
	int i, ret;

	pr_debug("%s: Switching FEC PHY power off\n", __func__);
	ret = mxs_iomux_setup_multiple_pads(tx28_fec_gpio_pads,
			ARRAY_SIZE(tx28_fec_gpio_pads));
	for (i = 0; i < ARRAY_SIZE(tx28_fec_gpio_pads); i++) {
		unsigned int gpio = MXS_GPIO_NR(PAD_BANK(tx28_fec_gpio_pads[i]),
			PAD_PIN(tx28_fec_gpio_pads[i]));

		ret = gpio_request(gpio, "FEC");
		if (ret) {
			pr_err("Failed to request GPIO_%d_%d: %d\n",
				PAD_BANK(tx28_fec_gpio_pads[i]),
				PAD_PIN(tx28_fec_gpio_pads[i]), ret);
			goto free_gpios;
		}
		ret = gpio_direction_output(gpio, 0);
		if (ret) {
			pr_err("Failed to set direction of GPIO_%d_%d to output: %d\n",
					gpio / 32 + 1, gpio % 32, ret);
			goto free_gpios;
		}
	}

	/* Power up fec phy */
	pr_debug("%s: Switching FEC PHY power on\n", __func__);
	ret = gpio_direction_output(TX28_FEC_PHY_POWER, 1);
	if (ret) {
		pr_err("Failed to power on PHY: %d\n", ret);
		goto free_gpios;
	}
	mdelay(26); /* 25ms according to data sheet */

	/* nINT */
	gpio_direction_input(MXS_GPIO_NR(4, 5));
	/* Mode strap pins */
	gpio_direction_output(MXS_GPIO_NR(4, 2), 1);
	gpio_direction_output(MXS_GPIO_NR(4, 3), 1);
	gpio_direction_output(MXS_GPIO_NR(4, 4), 1);

	udelay(100); /* minimum assertion time for nRST */

	pr_debug("%s: Deasserting FEC PHY RESET\n", __func__);
	gpio_set_value(TX28_FEC_PHY_RESET, 1);

	ret = mxs_iomux_setup_multiple_pads(tx28_fec0_pads,
			ARRAY_SIZE(tx28_fec0_pads));
	if (ret) {
		pr_debug("%s: mxs_iomux_setup_multiple_pads() failed with rc: %d\n",
				__func__, ret);
		goto free_gpios;
	}
	pr_debug("%s: Registering FEC0 device\n", __func__);
	mx28_add_fec(0, &tx28_fec0_data);
	return 0;

free_gpios:
	while (--i >= 0) {
		unsigned int gpio = MXS_GPIO_NR(PAD_BANK(tx28_fec_gpio_pads[i]),
			PAD_PIN(tx28_fec_gpio_pads[i]));

		gpio_free(gpio);
	}

	return ret;
}

int __init tx28_add_fec1(void)
{
	int ret;

	ret = mxs_iomux_setup_multiple_pads(tx28_fec1_pads,
			ARRAY_SIZE(tx28_fec1_pads));
	if (ret) {
		pr_debug("%s: mxs_iomux_setup_multiple_pads() failed with rc: %d\n",
				__func__, ret);
		return ret;
	}
	pr_debug("%s: Registering FEC1 device\n", __func__);
	mx28_add_fec(1, &tx28_fec1_data);
	return 0;
}
