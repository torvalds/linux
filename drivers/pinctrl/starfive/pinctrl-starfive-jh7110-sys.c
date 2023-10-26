// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl / GPIO driver for StarFive JH7110 SoC sys controller
 *
 * Copyright (C) 2022 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/starfive,jh7110-pinctrl.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "../pinconf.h"
#include "pinctrl-starfive-jh7110.h"

#define JH7110_SYS_NGPIO		64
#define JH7110_SYS_GC_BASE		0

#define JH7110_SYS_REGS_NUM		174

/* registers */
#define JH7110_SYS_DOEN			0x000
#define JH7110_SYS_DOUT			0x040
#define JH7110_SYS_GPI			0x080
#define JH7110_SYS_GPIOIN		0x118

#define JH7110_SYS_GPIOEN		0x0dc
#define JH7110_SYS_GPIOIS0		0x0e0
#define JH7110_SYS_GPIOIS1		0x0e4
#define JH7110_SYS_GPIOIC0		0x0e8
#define JH7110_SYS_GPIOIC1		0x0ec
#define JH7110_SYS_GPIOIBE0		0x0f0
#define JH7110_SYS_GPIOIBE1		0x0f4
#define JH7110_SYS_GPIOIEV0		0x0f8
#define JH7110_SYS_GPIOIEV1		0x0fc
#define JH7110_SYS_GPIOIE0		0x100
#define JH7110_SYS_GPIOIE1		0x104
#define JH7110_SYS_GPIORIS0		0x108
#define JH7110_SYS_GPIORIS1		0x10c
#define JH7110_SYS_GPIOMIS0		0x110
#define JH7110_SYS_GPIOMIS1		0x114

#define JH7110_SYS_GPO_PDA_0_74_CFG	0x120
#define JH7110_SYS_GPO_PDA_89_94_CFG	0x284

static const struct pinctrl_pin_desc jh7110_sys_pins[] = {
	PINCTRL_PIN(PAD_GPIO0,		"GPIO0"),
	PINCTRL_PIN(PAD_GPIO1,		"GPIO1"),
	PINCTRL_PIN(PAD_GPIO2,		"GPIO2"),
	PINCTRL_PIN(PAD_GPIO3,		"GPIO3"),
	PINCTRL_PIN(PAD_GPIO4,		"GPIO4"),
	PINCTRL_PIN(PAD_GPIO5,		"GPIO5"),
	PINCTRL_PIN(PAD_GPIO6,		"GPIO6"),
	PINCTRL_PIN(PAD_GPIO7,		"GPIO7"),
	PINCTRL_PIN(PAD_GPIO8,		"GPIO8"),
	PINCTRL_PIN(PAD_GPIO9,		"GPIO9"),
	PINCTRL_PIN(PAD_GPIO10,		"GPIO10"),
	PINCTRL_PIN(PAD_GPIO11,		"GPIO11"),
	PINCTRL_PIN(PAD_GPIO12,		"GPIO12"),
	PINCTRL_PIN(PAD_GPIO13,		"GPIO13"),
	PINCTRL_PIN(PAD_GPIO14,		"GPIO14"),
	PINCTRL_PIN(PAD_GPIO15,		"GPIO15"),
	PINCTRL_PIN(PAD_GPIO16,		"GPIO16"),
	PINCTRL_PIN(PAD_GPIO17,		"GPIO17"),
	PINCTRL_PIN(PAD_GPIO18,		"GPIO18"),
	PINCTRL_PIN(PAD_GPIO19,		"GPIO19"),
	PINCTRL_PIN(PAD_GPIO20,		"GPIO20"),
	PINCTRL_PIN(PAD_GPIO21,		"GPIO21"),
	PINCTRL_PIN(PAD_GPIO22,		"GPIO22"),
	PINCTRL_PIN(PAD_GPIO23,		"GPIO23"),
	PINCTRL_PIN(PAD_GPIO24,		"GPIO24"),
	PINCTRL_PIN(PAD_GPIO25,		"GPIO25"),
	PINCTRL_PIN(PAD_GPIO26,		"GPIO26"),
	PINCTRL_PIN(PAD_GPIO27,		"GPIO27"),
	PINCTRL_PIN(PAD_GPIO28,		"GPIO28"),
	PINCTRL_PIN(PAD_GPIO29,		"GPIO29"),
	PINCTRL_PIN(PAD_GPIO30,		"GPIO30"),
	PINCTRL_PIN(PAD_GPIO31,		"GPIO31"),
	PINCTRL_PIN(PAD_GPIO32,		"GPIO32"),
	PINCTRL_PIN(PAD_GPIO33,		"GPIO33"),
	PINCTRL_PIN(PAD_GPIO34,		"GPIO34"),
	PINCTRL_PIN(PAD_GPIO35,		"GPIO35"),
	PINCTRL_PIN(PAD_GPIO36,		"GPIO36"),
	PINCTRL_PIN(PAD_GPIO37,		"GPIO37"),
	PINCTRL_PIN(PAD_GPIO38,		"GPIO38"),
	PINCTRL_PIN(PAD_GPIO39,		"GPIO39"),
	PINCTRL_PIN(PAD_GPIO40,		"GPIO40"),
	PINCTRL_PIN(PAD_GPIO41,		"GPIO41"),
	PINCTRL_PIN(PAD_GPIO42,		"GPIO42"),
	PINCTRL_PIN(PAD_GPIO43,		"GPIO43"),
	PINCTRL_PIN(PAD_GPIO44,		"GPIO44"),
	PINCTRL_PIN(PAD_GPIO45,		"GPIO45"),
	PINCTRL_PIN(PAD_GPIO46,		"GPIO46"),
	PINCTRL_PIN(PAD_GPIO47,		"GPIO47"),
	PINCTRL_PIN(PAD_GPIO48,		"GPIO48"),
	PINCTRL_PIN(PAD_GPIO49,		"GPIO49"),
	PINCTRL_PIN(PAD_GPIO50,		"GPIO50"),
	PINCTRL_PIN(PAD_GPIO51,		"GPIO51"),
	PINCTRL_PIN(PAD_GPIO52,		"GPIO52"),
	PINCTRL_PIN(PAD_GPIO53,		"GPIO53"),
	PINCTRL_PIN(PAD_GPIO54,		"GPIO54"),
	PINCTRL_PIN(PAD_GPIO55,		"GPIO55"),
	PINCTRL_PIN(PAD_GPIO56,		"GPIO56"),
	PINCTRL_PIN(PAD_GPIO57,		"GPIO57"),
	PINCTRL_PIN(PAD_GPIO58,		"GPIO58"),
	PINCTRL_PIN(PAD_GPIO59,		"GPIO59"),
	PINCTRL_PIN(PAD_GPIO60,		"GPIO60"),
	PINCTRL_PIN(PAD_GPIO61,		"GPIO61"),
	PINCTRL_PIN(PAD_GPIO62,		"GPIO62"),
	PINCTRL_PIN(PAD_GPIO63,		"GPIO63"),
	PINCTRL_PIN(PAD_SD0_CLK,	"SD0_CLK"),
	PINCTRL_PIN(PAD_SD0_CMD,	"SD0_CMD"),
	PINCTRL_PIN(PAD_SD0_DATA0,	"SD0_DATA0"),
	PINCTRL_PIN(PAD_SD0_DATA1,	"SD0_DATA1"),
	PINCTRL_PIN(PAD_SD0_DATA2,	"SD0_DATA2"),
	PINCTRL_PIN(PAD_SD0_DATA3,	"SD0_DATA3"),
	PINCTRL_PIN(PAD_SD0_DATA4,	"SD0_DATA4"),
	PINCTRL_PIN(PAD_SD0_DATA5,	"SD0_DATA5"),
	PINCTRL_PIN(PAD_SD0_DATA6,	"SD0_DATA6"),
	PINCTRL_PIN(PAD_SD0_DATA7,	"SD0_DATA7"),
	PINCTRL_PIN(PAD_SD0_STRB,	"SD0_STRB"),
	PINCTRL_PIN(PAD_GMAC1_MDC,	"GMAC1_MDC"),
	PINCTRL_PIN(PAD_GMAC1_MDIO,	"GMAC1_MDIO"),
	PINCTRL_PIN(PAD_GMAC1_RXD0,	"GMAC1_RXD0"),
	PINCTRL_PIN(PAD_GMAC1_RXD1,	"GMAC1_RXD1"),
	PINCTRL_PIN(PAD_GMAC1_RXD2,	"GMAC1_RXD2"),
	PINCTRL_PIN(PAD_GMAC1_RXD3,	"GMAC1_RXD3"),
	PINCTRL_PIN(PAD_GMAC1_RXDV,	"GMAC1_RXDV"),
	PINCTRL_PIN(PAD_GMAC1_RXC,	"GMAC1_RXC"),
	PINCTRL_PIN(PAD_GMAC1_TXD0,	"GMAC1_TXD0"),
	PINCTRL_PIN(PAD_GMAC1_TXD1,	"GMAC1_TXD1"),
	PINCTRL_PIN(PAD_GMAC1_TXD2,	"GMAC1_TXD2"),
	PINCTRL_PIN(PAD_GMAC1_TXD3,	"GMAC1_TXD3"),
	PINCTRL_PIN(PAD_GMAC1_TXEN,	"GMAC1_TXEN"),
	PINCTRL_PIN(PAD_GMAC1_TXC,	"GMAC1_TXC"),
	PINCTRL_PIN(PAD_QSPI_SCLK,	"QSPI_SCLK"),
	PINCTRL_PIN(PAD_QSPI_CS0,	"QSPI_CS0"),
	PINCTRL_PIN(PAD_QSPI_DATA0,	"QSPI_DATA0"),
	PINCTRL_PIN(PAD_QSPI_DATA1,	"QSPI_DATA1"),
	PINCTRL_PIN(PAD_QSPI_DATA2,	"QSPI_DATA2"),
	PINCTRL_PIN(PAD_QSPI_DATA3,	"QSPI_DATA3"),
};

struct jh7110_func_sel {
	u16 offset;
	u8 shift;
	u8 max;
};

static const struct jh7110_func_sel
	jh7110_sys_func_sel[ARRAY_SIZE(jh7110_sys_pins)] = {
	[PAD_GMAC1_RXC] = { 0x29c,  0, 1 },
	[PAD_GPIO10]    = { 0x29c,  2, 3 },
	[PAD_GPIO11]    = { 0x29c,  5, 3 },
	[PAD_GPIO12]    = { 0x29c,  8, 3 },
	[PAD_GPIO13]    = { 0x29c, 11, 3 },
	[PAD_GPIO14]    = { 0x29c, 14, 3 },
	[PAD_GPIO15]    = { 0x29c, 17, 3 },
	[PAD_GPIO16]    = { 0x29c, 20, 3 },
	[PAD_GPIO17]    = { 0x29c, 23, 3 },
	[PAD_GPIO18]    = { 0x29c, 26, 3 },
	[PAD_GPIO19]    = { 0x29c, 29, 3 },

	[PAD_GPIO20]    = { 0x2a0,  0, 3 },
	[PAD_GPIO21]    = { 0x2a0,  3, 3 },
	[PAD_GPIO22]    = { 0x2a0,  6, 3 },
	[PAD_GPIO23]    = { 0x2a0,  9, 3 },
	[PAD_GPIO24]    = { 0x2a0, 12, 3 },
	[PAD_GPIO25]    = { 0x2a0, 15, 3 },
	[PAD_GPIO26]    = { 0x2a0, 18, 3 },
	[PAD_GPIO27]    = { 0x2a0, 21, 3 },
	[PAD_GPIO28]    = { 0x2a0, 24, 3 },
	[PAD_GPIO29]    = { 0x2a0, 27, 3 },

	[PAD_GPIO30]    = { 0x2a4,  0, 3 },
	[PAD_GPIO31]    = { 0x2a4,  3, 3 },
	[PAD_GPIO32]    = { 0x2a4,  6, 3 },
	[PAD_GPIO33]    = { 0x2a4,  9, 3 },
	[PAD_GPIO34]    = { 0x2a4, 12, 3 },
	[PAD_GPIO35]    = { 0x2a4, 15, 3 },
	[PAD_GPIO36]    = { 0x2a4, 17, 3 },
	[PAD_GPIO37]    = { 0x2a4, 20, 3 },
	[PAD_GPIO38]    = { 0x2a4, 23, 3 },
	[PAD_GPIO39]    = { 0x2a4, 26, 3 },
	[PAD_GPIO40]    = { 0x2a4, 29, 3 },

	[PAD_GPIO41]    = { 0x2a8,  0, 3 },
	[PAD_GPIO42]    = { 0x2a8,  3, 3 },
	[PAD_GPIO43]    = { 0x2a8,  6, 3 },
	[PAD_GPIO44]    = { 0x2a8,  9, 3 },
	[PAD_GPIO45]    = { 0x2a8, 12, 3 },
	[PAD_GPIO46]    = { 0x2a8, 15, 3 },
	[PAD_GPIO47]    = { 0x2a8, 18, 3 },
	[PAD_GPIO48]    = { 0x2a8, 21, 3 },
	[PAD_GPIO49]    = { 0x2a8, 24, 3 },
	[PAD_GPIO50]    = { 0x2a8, 27, 3 },
	[PAD_GPIO51]    = { 0x2a8, 30, 3 },

	[PAD_GPIO52]    = { 0x2ac,  0, 3 },
	[PAD_GPIO53]    = { 0x2ac,  2, 3 },
	[PAD_GPIO54]    = { 0x2ac,  4, 3 },
	[PAD_GPIO55]    = { 0x2ac,  6, 3 },
	[PAD_GPIO56]    = { 0x2ac,  9, 3 },
	[PAD_GPIO57]    = { 0x2ac, 12, 3 },
	[PAD_GPIO58]    = { 0x2ac, 15, 3 },
	[PAD_GPIO59]    = { 0x2ac, 18, 3 },
	[PAD_GPIO60]    = { 0x2ac, 21, 3 },
	[PAD_GPIO61]    = { 0x2ac, 24, 3 },
	[PAD_GPIO62]    = { 0x2ac, 27, 3 },
	[PAD_GPIO63]    = { 0x2ac, 30, 3 },

	[PAD_GPIO6]     = { 0x2b0,  0, 3 },
	[PAD_GPIO7]     = { 0x2b0,  2, 3 },
	[PAD_GPIO8]     = { 0x2b0,  5, 3 },
	[PAD_GPIO9]     = { 0x2b0,  8, 3 },
};

struct jh7110_vin_group_sel {
	u16 offset;
	u8 shift;
	u8 group;
};

static const struct jh7110_vin_group_sel
	jh7110_sys_vin_group_sel[ARRAY_SIZE(jh7110_sys_pins)] = {
	[PAD_GPIO6]     = { 0x2b4, 21, 0 },
	[PAD_GPIO7]     = { 0x2b4, 18, 0 },
	[PAD_GPIO8]     = { 0x2b4, 15, 0 },
	[PAD_GPIO9]     = { 0x2b0, 11, 0 },
	[PAD_GPIO10]    = { 0x2b0, 20, 0 },
	[PAD_GPIO11]    = { 0x2b0, 23, 0 },
	[PAD_GPIO12]    = { 0x2b0, 26, 0 },
	[PAD_GPIO13]    = { 0x2b0, 29, 0 },
	[PAD_GPIO14]    = { 0x2b4,  0, 0 },
	[PAD_GPIO15]    = { 0x2b4,  3, 0 },
	[PAD_GPIO16]    = { 0x2b4,  6, 0 },
	[PAD_GPIO17]    = { 0x2b4,  9, 0 },
	[PAD_GPIO18]    = { 0x2b4, 12, 0 },
	[PAD_GPIO19]    = { 0x2b0, 14, 0 },
	[PAD_GPIO20]    = { 0x2b0, 17, 0 },

	[PAD_GPIO21]    = { 0x2b4, 21, 1 },
	[PAD_GPIO22]    = { 0x2b4, 18, 1 },
	[PAD_GPIO23]    = { 0x2b4, 15, 1 },
	[PAD_GPIO24]    = { 0x2b0, 11, 1 },
	[PAD_GPIO25]    = { 0x2b0, 20, 1 },
	[PAD_GPIO26]    = { 0x2b0, 23, 1 },
	[PAD_GPIO27]    = { 0x2b0, 26, 1 },
	[PAD_GPIO28]    = { 0x2b0, 29, 1 },
	[PAD_GPIO29]    = { 0x2b4,  0, 1 },
	[PAD_GPIO30]    = { 0x2b4,  3, 1 },
	[PAD_GPIO31]    = { 0x2b4,  6, 1 },
	[PAD_GPIO32]    = { 0x2b4,  9, 1 },
	[PAD_GPIO33]    = { 0x2b4, 12, 1 },
	[PAD_GPIO34]    = { 0x2b0, 14, 1 },
	[PAD_GPIO35]    = { 0x2b0, 17, 1 },

	[PAD_GPIO36]    = { 0x2b4, 21, 2 },
	[PAD_GPIO37]    = { 0x2b4, 18, 2 },
	[PAD_GPIO38]    = { 0x2b4, 15, 2 },
	[PAD_GPIO39]    = { 0x2b0, 11, 2 },
	[PAD_GPIO40]    = { 0x2b0, 20, 2 },
	[PAD_GPIO41]    = { 0x2b0, 23, 2 },
	[PAD_GPIO42]    = { 0x2b0, 26, 2 },
	[PAD_GPIO43]    = { 0x2b0, 29, 2 },
	[PAD_GPIO44]    = { 0x2b4,  0, 2 },
	[PAD_GPIO45]    = { 0x2b4,  3, 2 },
	[PAD_GPIO46]    = { 0x2b4,  6, 2 },
	[PAD_GPIO47]    = { 0x2b4,  9, 2 },
	[PAD_GPIO48]    = { 0x2b4, 12, 2 },
	[PAD_GPIO49]    = { 0x2b0, 14, 2 },
	[PAD_GPIO50]    = { 0x2b0, 17, 2 },
};

static void jh7110_set_function(struct jh7110_pinctrl *sfp,
				unsigned int pin, u32 func)
{
	const struct jh7110_func_sel *fs = &jh7110_sys_func_sel[pin];
	unsigned long flags;
	void __iomem *reg;
	u32 mask;

	if (!fs->offset)
		return;

	if (func > fs->max)
		return;

	reg = sfp->base + fs->offset;
	func = func << fs->shift;
	mask = 0x3U << fs->shift;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	func |= readl_relaxed(reg) & ~mask;
	writel_relaxed(func, reg);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static void jh7110_set_vin_group(struct jh7110_pinctrl *sfp,
				 unsigned int pin)
{
	const struct jh7110_vin_group_sel *gs = &jh7110_sys_vin_group_sel[pin];
	unsigned long flags;
	void __iomem *reg;
	u32 mask;
	u32 grp;

	if (!gs->offset)
		return;

	reg = sfp->base + gs->offset;
	grp = gs->group << gs->shift;
	mask = 0x3U << gs->shift;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	grp |= readl_relaxed(reg) & ~mask;
	writel_relaxed(grp, reg);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static int jh7110_sys_set_one_pin_mux(struct jh7110_pinctrl *sfp,
				      unsigned int pin,
				      unsigned int din, u32 dout,
				      u32 doen, u32 func)
{
	if (pin < sfp->gc.ngpio && func == 0)
		jh7110_set_gpiomux(sfp, pin, din, dout, doen);

	jh7110_set_function(sfp, pin, func);

	if (pin < sfp->gc.ngpio && func == 2)
		jh7110_set_vin_group(sfp, pin);

	return 0;
}

static int jh7110_sys_get_padcfg_base(struct jh7110_pinctrl *sfp,
				      unsigned int pin)
{
	if (pin < PAD_GMAC1_MDC)
		return JH7110_SYS_GPO_PDA_0_74_CFG;
	else if (pin > PAD_GMAC1_TXC && pin <= PAD_QSPI_DATA3)
		return JH7110_SYS_GPO_PDA_89_94_CFG;
	else
		return -1;
}

static void jh7110_sys_irq_handler(struct irq_desc *desc)
{
	struct jh7110_pinctrl *sfp = jh7110_from_irq_desc(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long mis;
	unsigned int pin;

	chained_irq_enter(chip, desc);

	mis = readl_relaxed(sfp->base + JH7110_SYS_GPIOMIS0);
	for_each_set_bit(pin, &mis, 32)
		generic_handle_domain_irq(sfp->gc.irq.domain, pin);

	mis = readl_relaxed(sfp->base + JH7110_SYS_GPIOMIS1);
	for_each_set_bit(pin, &mis, 32)
		generic_handle_domain_irq(sfp->gc.irq.domain, pin + 32);

	chained_irq_exit(chip, desc);
}

static int jh7110_sys_init_hw(struct gpio_chip *gc)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);

	/* mask all GPIO interrupts */
	writel(0U, sfp->base + JH7110_SYS_GPIOIE0);
	writel(0U, sfp->base + JH7110_SYS_GPIOIE1);
	/* clear edge interrupt flags */
	writel(~0U, sfp->base + JH7110_SYS_GPIOIC0);
	writel(~0U, sfp->base + JH7110_SYS_GPIOIC1);
	/* enable GPIO interrupts */
	writel(1U, sfp->base + JH7110_SYS_GPIOEN);
	return 0;
}

static const struct jh7110_gpio_irq_reg jh7110_sys_irq_reg = {
	.is_reg_base	= JH7110_SYS_GPIOIS0,
	.ic_reg_base	= JH7110_SYS_GPIOIC0,
	.ibe_reg_base	= JH7110_SYS_GPIOIBE0,
	.iev_reg_base	= JH7110_SYS_GPIOIEV0,
	.ie_reg_base	= JH7110_SYS_GPIOIE0,
	.ris_reg_base	= JH7110_SYS_GPIORIS0,
	.mis_reg_base	= JH7110_SYS_GPIOMIS0,
};

static const struct jh7110_pinctrl_soc_info jh7110_sys_pinctrl_info = {
	.pins		= jh7110_sys_pins,
	.npins		= ARRAY_SIZE(jh7110_sys_pins),
	.ngpios		= JH7110_SYS_NGPIO,
	.gc_base	= JH7110_SYS_GC_BASE,
	.dout_reg_base	= JH7110_SYS_DOUT,
	.dout_mask	= GENMASK(6, 0),
	.doen_reg_base	= JH7110_SYS_DOEN,
	.doen_mask	= GENMASK(5, 0),
	.gpi_reg_base	= JH7110_SYS_GPI,
	.gpi_mask	= GENMASK(6, 0),
	.gpioin_reg_base	   = JH7110_SYS_GPIOIN,
	.irq_reg		   = &jh7110_sys_irq_reg,
	.nsaved_regs		   = JH7110_SYS_REGS_NUM,
	.jh7110_set_one_pin_mux  = jh7110_sys_set_one_pin_mux,
	.jh7110_get_padcfg_base  = jh7110_sys_get_padcfg_base,
	.jh7110_gpio_irq_handler = jh7110_sys_irq_handler,
	.jh7110_gpio_init_hw	 = jh7110_sys_init_hw,
};

static const struct of_device_id jh7110_sys_pinctrl_of_match[] = {
	{
		.compatible = "starfive,jh7110-sys-pinctrl",
		.data = &jh7110_sys_pinctrl_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jh7110_sys_pinctrl_of_match);

static struct platform_driver jh7110_sys_pinctrl_driver = {
	.probe = jh7110_pinctrl_probe,
	.driver = {
		.name = "starfive-jh7110-sys-pinctrl",
		.of_match_table = jh7110_sys_pinctrl_of_match,
		.pm = pm_sleep_ptr(&jh7110_pinctrl_pm_ops),
	},
};
module_platform_driver(jh7110_sys_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for the StarFive JH7110 SoC sys controller");
MODULE_AUTHOR("Emil Renner Berthing <kernel@esmil.dk>");
MODULE_AUTHOR("Jianlong Huang <jianlong.huang@starfivetech.com>");
MODULE_LICENSE("GPL");
