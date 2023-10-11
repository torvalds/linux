// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl / GPIO driver for StarFive JH7110 SoC aon controller
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/pinctrl/starfive,jh7110-pinctrl.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"
#include "pinctrl-starfive-jh7110.h"

#define JH7110_AON_NGPIO		4
#define JH7110_AON_GC_BASE		64

/* registers */
#define JH7110_AON_DOEN			0x0
#define JH7110_AON_DOUT			0x4
#define JH7110_AON_GPI			0x8
#define JH7110_AON_GPIOIN		0x2c

#define JH7110_AON_GPIOEN		0xc
#define JH7110_AON_GPIOIS		0x10
#define JH7110_AON_GPIOIC		0x14
#define JH7110_AON_GPIOIBE		0x18
#define JH7110_AON_GPIOIEV		0x1c
#define JH7110_AON_GPIOIE		0x20
#define JH7110_AON_GPIORIS		0x28
#define JH7110_AON_GPIOMIS		0x28

#define JH7110_AON_GPO_PDA_0_5_CFG	0x30

static const struct pinctrl_pin_desc jh7110_aon_pins[] = {
	PINCTRL_PIN(PAD_TESTEN,		"TESTEN"),
	PINCTRL_PIN(PAD_RGPIO0,		"RGPIO0"),
	PINCTRL_PIN(PAD_RGPIO1,		"RGPIO1"),
	PINCTRL_PIN(PAD_RGPIO2,		"RGPIO2"),
	PINCTRL_PIN(PAD_RGPIO3,		"RGPIO3"),
	PINCTRL_PIN(PAD_RSTN,		"RSTN"),
	PINCTRL_PIN(PAD_GMAC0_MDC,	"GMAC0_MDC"),
	PINCTRL_PIN(PAD_GMAC0_MDIO,	"GMAC0_MDIO"),
	PINCTRL_PIN(PAD_GMAC0_RXD0,	"GMAC0_RXD0"),
	PINCTRL_PIN(PAD_GMAC0_RXD1,	"GMAC0_RXD1"),
	PINCTRL_PIN(PAD_GMAC0_RXD2,	"GMAC0_RXD2"),
	PINCTRL_PIN(PAD_GMAC0_RXD3,	"GMAC0_RXD3"),
	PINCTRL_PIN(PAD_GMAC0_RXDV,	"GMAC0_RXDV"),
	PINCTRL_PIN(PAD_GMAC0_RXC,	"GMAC0_RXC"),
	PINCTRL_PIN(PAD_GMAC0_TXD0,	"GMAC0_TXD0"),
	PINCTRL_PIN(PAD_GMAC0_TXD1,	"GMAC0_TXD1"),
	PINCTRL_PIN(PAD_GMAC0_TXD2,	"GMAC0_TXD2"),
	PINCTRL_PIN(PAD_GMAC0_TXD3,	"GMAC0_TXD3"),
	PINCTRL_PIN(PAD_GMAC0_TXEN,	"GMAC0_TXEN"),
	PINCTRL_PIN(PAD_GMAC0_TXC,	"GMAC0_TXC"),
};

static int jh7110_aon_set_one_pin_mux(struct jh7110_pinctrl *sfp,
				      unsigned int pin,
				      unsigned int din, u32 dout,
				      u32 doen, u32 func)
{
	if (pin < sfp->gc.ngpio && func == 0)
		jh7110_set_gpiomux(sfp, pin, din, dout, doen);

	return 0;
}

static int jh7110_aon_get_padcfg_base(struct jh7110_pinctrl *sfp,
				      unsigned int pin)
{
	if (pin < PAD_GMAC0_MDC)
		return JH7110_AON_GPO_PDA_0_5_CFG;

	return -1;
}

static void jh7110_aon_irq_handler(struct irq_desc *desc)
{
	struct jh7110_pinctrl *sfp = jh7110_from_irq_desc(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long mis;
	unsigned int pin;

	chained_irq_enter(chip, desc);

	mis = readl_relaxed(sfp->base + JH7110_AON_GPIOMIS);
	for_each_set_bit(pin, &mis, JH7110_AON_NGPIO)
		generic_handle_domain_irq(sfp->gc.irq.domain, pin);

	chained_irq_exit(chip, desc);
}

static int jh7110_aon_init_hw(struct gpio_chip *gc)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);

	/* mask all GPIO interrupts */
	writel_relaxed(0, sfp->base + JH7110_AON_GPIOIE);
	/* clear edge interrupt flags */
	writel_relaxed(0, sfp->base + JH7110_AON_GPIOIC);
	writel_relaxed(0x0f, sfp->base + JH7110_AON_GPIOIC);
	/* enable GPIO interrupts */
	writel_relaxed(1, sfp->base + JH7110_AON_GPIOEN);
	return 0;
}

static const struct jh7110_gpio_irq_reg jh7110_aon_irq_reg = {
	.is_reg_base	= JH7110_AON_GPIOIS,
	.ic_reg_base	= JH7110_AON_GPIOIC,
	.ibe_reg_base	= JH7110_AON_GPIOIBE,
	.iev_reg_base	= JH7110_AON_GPIOIEV,
	.ie_reg_base	= JH7110_AON_GPIOIE,
	.ris_reg_base	= JH7110_AON_GPIORIS,
	.mis_reg_base	= JH7110_AON_GPIOMIS,
};

static const struct jh7110_pinctrl_soc_info jh7110_aon_pinctrl_info = {
	.pins		= jh7110_aon_pins,
	.npins		= ARRAY_SIZE(jh7110_aon_pins),
	.ngpios		= JH7110_AON_NGPIO,
	.gc_base	= JH7110_AON_GC_BASE,
	.dout_reg_base	= JH7110_AON_DOUT,
	.dout_mask	= GENMASK(3, 0),
	.doen_reg_base	= JH7110_AON_DOEN,
	.doen_mask	= GENMASK(2, 0),
	.gpi_reg_base	= JH7110_AON_GPI,
	.gpi_mask	= GENMASK(3, 0),
	.gpioin_reg_base	   = JH7110_AON_GPIOIN,
	.irq_reg		   = &jh7110_aon_irq_reg,
	.jh7110_set_one_pin_mux  = jh7110_aon_set_one_pin_mux,
	.jh7110_get_padcfg_base  = jh7110_aon_get_padcfg_base,
	.jh7110_gpio_irq_handler = jh7110_aon_irq_handler,
	.jh7110_gpio_init_hw	 = jh7110_aon_init_hw,
};

static const struct of_device_id jh7110_aon_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jh7110-aon-pinctrl",
		.data = &jh7110_aon_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jh7110_aon_pinctrl_of_match);

static struct platform_driver jh7110_aon_pinctrl_driver = {
	.probe = jh7110_pinctrl_probe,
	.driver = {
		.name = "starfive-jh7110-aon-pinctrl",
		.of_match_table = jh7110_aon_pinctrl_of_match,
	},
};
module_platform_driver(jh7110_aon_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for the StarFive JH7110 SoC aon controller");
MODULE_AUTHOR("Jianlong Huang <jianlong.huang@starfivetech.com>");
MODULE_LICENSE("GPL");
