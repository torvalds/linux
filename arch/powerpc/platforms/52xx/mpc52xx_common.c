/*
 *
 * Utility functions for the Freescale MPC52xx.
 *
 * Copyright (C) 2006 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#undef DEBUG

#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/export.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/mpc52xx.h>

/* MPC5200 device tree match tables */
static const struct of_device_id mpc52xx_xlb_ids[] __initconst = {
	{ .compatible = "fsl,mpc5200-xlb", },
	{ .compatible = "mpc5200-xlb", },
	{}
};
static const struct of_device_id mpc52xx_bus_ids[] __initconst = {
	{ .compatible = "fsl,mpc5200-immr", },
	{ .compatible = "fsl,mpc5200b-immr", },
	{ .compatible = "simple-bus", },

	/* depreciated matches; shouldn't be used in new device trees */
	{ .compatible = "fsl,lpb", },
	{ .type = "builtin", .compatible = "mpc5200", }, /* efika */
	{ .type = "soc", .compatible = "mpc5200", }, /* lite5200 */
	{}
};

/*
 * This variable is mapped in mpc52xx_map_wdt() and used in mpc52xx_restart().
 * Permanent mapping is required because mpc52xx_restart() can be called
 * from interrupt context while node mapping (which calls ioremap())
 * cannot be used at such point.
 */
static DEFINE_SPINLOCK(mpc52xx_lock);
static struct mpc52xx_gpt __iomem *mpc52xx_wdt;
static struct mpc52xx_cdm __iomem *mpc52xx_cdm;

/*
 * Configure the XLB arbiter settings to match what Linux expects.
 */
void __init
mpc5200_setup_xlb_arbiter(void)
{
	struct device_node *np;
	struct mpc52xx_xlb  __iomem *xlb;

	np = of_find_matching_node(NULL, mpc52xx_xlb_ids);
	xlb = of_iomap(np, 0);
	of_node_put(np);
	if (!xlb) {
		printk(KERN_ERR __FILE__ ": "
			"Error mapping XLB in mpc52xx_setup_cpu(). "
			"Expect some abnormal behavior\n");
		return;
	}

	/* Configure the XLB Arbiter priorities */
	out_be32(&xlb->master_pri_enable, 0xff);
	out_be32(&xlb->master_priority, 0x11111111);

	/*
	 * Disable XLB pipelining
	 * (cfr errate 292. We could do this only just before ATA PIO
	 *  transaction and re-enable it afterwards ...)
	 * Not needed on MPC5200B.
	 */
	if ((mfspr(SPRN_SVR) & MPC5200_SVR_MASK) == MPC5200_SVR)
		out_be32(&xlb->config, in_be32(&xlb->config) | MPC52xx_XLB_CFG_PLDIS);

	iounmap(xlb);
}

/*
 * This variable is mapped in mpc52xx_map_common_devices and
 * used in mpc5200_psc_ac97_gpio_reset().
 */
static DEFINE_SPINLOCK(gpio_lock);
struct mpc52xx_gpio __iomem *simple_gpio;
struct mpc52xx_gpio_wkup __iomem *wkup_gpio;

/**
 * mpc52xx_declare_of_platform_devices: register internal devices and children
 *					of the localplus bus to the of_platform
 *					bus.
 */
void __init mpc52xx_declare_of_platform_devices(void)
{
	/* Find all the 'platform' devices and register them. */
	if (of_platform_populate(NULL, mpc52xx_bus_ids, NULL, NULL))
		pr_err(__FILE__ ": Error while populating devices from DT\n");
}

/*
 * match tables used by mpc52xx_map_common_devices()
 */
static const struct of_device_id mpc52xx_gpt_ids[] __initconst = {
	{ .compatible = "fsl,mpc5200-gpt", },
	{ .compatible = "mpc5200-gpt", }, /* old */
	{}
};
static const struct of_device_id mpc52xx_cdm_ids[] __initconst = {
	{ .compatible = "fsl,mpc5200-cdm", },
	{ .compatible = "mpc5200-cdm", }, /* old */
	{}
};
static const struct of_device_id mpc52xx_gpio_simple[] __initconst = {
	{ .compatible = "fsl,mpc5200-gpio", },
	{}
};
static const struct of_device_id mpc52xx_gpio_wkup[] __initconst = {
	{ .compatible = "fsl,mpc5200-gpio-wkup", },
	{}
};


/**
 * mpc52xx_map_common_devices: iomap devices required by common code
 */
void __init
mpc52xx_map_common_devices(void)
{
	struct device_node *np;

	/* mpc52xx_wdt is mapped here and used in mpc52xx_restart,
	 * possibly from a interrupt context. wdt is only implement
	 * on a gpt0, so check has-wdt property before mapping.
	 */
	for_each_matching_node(np, mpc52xx_gpt_ids) {
		if (of_get_property(np, "fsl,has-wdt", NULL) ||
		    of_get_property(np, "has-wdt", NULL)) {
			mpc52xx_wdt = of_iomap(np, 0);
			of_node_put(np);
			break;
		}
	}

	/* Clock Distribution Module, used by PSC clock setting function */
	np = of_find_matching_node(NULL, mpc52xx_cdm_ids);
	mpc52xx_cdm = of_iomap(np, 0);
	of_node_put(np);

	/* simple_gpio registers */
	np = of_find_matching_node(NULL, mpc52xx_gpio_simple);
	simple_gpio = of_iomap(np, 0);
	of_node_put(np);

	/* wkup_gpio registers */
	np = of_find_matching_node(NULL, mpc52xx_gpio_wkup);
	wkup_gpio = of_iomap(np, 0);
	of_node_put(np);
}

/**
 * mpc52xx_set_psc_clkdiv: Set clock divider in the CDM for PSC ports
 *
 * @psc_id: id of psc port; must be 1,2,3 or 6
 * @clkdiv: clock divider value to put into CDM PSC register.
 */
int mpc52xx_set_psc_clkdiv(int psc_id, int clkdiv)
{
	unsigned long flags;
	u16 __iomem *reg;
	u32 val;
	u32 mask;
	u32 mclken_div;

	if (!mpc52xx_cdm)
		return -ENODEV;

	mclken_div = 0x8000 | (clkdiv & 0x1FF);
	switch (psc_id) {
	case 1: reg = &mpc52xx_cdm->mclken_div_psc1; mask = 0x20; break;
	case 2: reg = &mpc52xx_cdm->mclken_div_psc2; mask = 0x40; break;
	case 3: reg = &mpc52xx_cdm->mclken_div_psc3; mask = 0x80; break;
	case 6: reg = &mpc52xx_cdm->mclken_div_psc6; mask = 0x10; break;
	default:
		return -ENODEV;
	}

	/* Set the rate and enable the clock */
	spin_lock_irqsave(&mpc52xx_lock, flags);
	out_be16(reg, mclken_div);
	val = in_be32(&mpc52xx_cdm->clk_enables);
	out_be32(&mpc52xx_cdm->clk_enables, val | mask);
	spin_unlock_irqrestore(&mpc52xx_lock, flags);

	return 0;
}
EXPORT_SYMBOL(mpc52xx_set_psc_clkdiv);

/**
 * mpc52xx_get_xtal_freq - Get SYS_XTAL_IN frequency for a device
 *
 * @node: device node
 *
 * Returns the frequency of the external oscillator clock connected
 * to the SYS_XTAL_IN pin, or 0 if it cannot be determined.
 */
unsigned int mpc52xx_get_xtal_freq(struct device_node *node)
{
	u32 val;
	unsigned int freq;

	if (!mpc52xx_cdm)
		return 0;

	freq = mpc5xxx_get_bus_frequency(node);
	if (!freq)
		return 0;

	if (in_8(&mpc52xx_cdm->ipb_clk_sel) & 0x1)
		freq *= 2;

	val  = in_be32(&mpc52xx_cdm->rstcfg);
	if (val & (1 << 5))
		freq *= 8;
	else
		freq *= 4;
	if (val & (1 << 6))
		freq /= 12;
	else
		freq /= 16;

	return freq;
}
EXPORT_SYMBOL(mpc52xx_get_xtal_freq);

/**
 * mpc52xx_restart: ppc_md->restart hook for mpc5200 using the watchdog timer
 */
void
mpc52xx_restart(char *cmd)
{
	local_irq_disable();

	/* Turn on the watchdog and wait for it to expire.
	 * It effectively does a reset. */
	if (mpc52xx_wdt) {
		out_be32(&mpc52xx_wdt->mode, 0x00000000);
		out_be32(&mpc52xx_wdt->count, 0x000000ff);
		out_be32(&mpc52xx_wdt->mode, 0x00009004);
	} else
		printk(KERN_ERR __FILE__ ": "
			"mpc52xx_restart: Can't access wdt. "
			"Restart impossible, system halted.\n");

	while (1);
}

#define PSC1_RESET     0x1
#define PSC1_SYNC      0x4
#define PSC1_SDATA_OUT 0x1
#define PSC2_RESET     0x2
#define PSC2_SYNC      (0x4<<4)
#define PSC2_SDATA_OUT (0x1<<4)
#define MPC52xx_GPIO_PSC1_MASK 0x7
#define MPC52xx_GPIO_PSC2_MASK (0x7<<4)

/**
 * mpc5200_psc_ac97_gpio_reset: Use gpio pins to reset the ac97 bus
 *
 * @psc: psc number to reset (only psc 1 and 2 support ac97)
 */
int mpc5200_psc_ac97_gpio_reset(int psc_number)
{
	unsigned long flags;
	u32 gpio;
	u32 mux;
	int out;
	int reset;
	int sync;

	if ((!simple_gpio) || (!wkup_gpio))
		return -ENODEV;

	switch (psc_number) {
	case 0:
		reset   = PSC1_RESET;           /* AC97_1_RES */
		sync    = PSC1_SYNC;            /* AC97_1_SYNC */
		out     = PSC1_SDATA_OUT;       /* AC97_1_SDATA_OUT */
		gpio    = MPC52xx_GPIO_PSC1_MASK;
		break;
	case 1:
		reset   = PSC2_RESET;           /* AC97_2_RES */
		sync    = PSC2_SYNC;            /* AC97_2_SYNC */
		out     = PSC2_SDATA_OUT;       /* AC97_2_SDATA_OUT */
		gpio    = MPC52xx_GPIO_PSC2_MASK;
		break;
	default:
		pr_err(__FILE__ ": Unable to determine PSC, no ac97 "
		       "cold-reset will be performed\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	/* Reconfiure pin-muxing to gpio */
	mux = in_be32(&simple_gpio->port_config);
	out_be32(&simple_gpio->port_config, mux & (~gpio));

	/* enable gpio pins for output */
	setbits8(&wkup_gpio->wkup_gpioe, reset);
	setbits32(&simple_gpio->simple_gpioe, sync | out);

	setbits8(&wkup_gpio->wkup_ddr, reset);
	setbits32(&simple_gpio->simple_ddr, sync | out);

	/* Assert cold reset */
	clrbits32(&simple_gpio->simple_dvo, sync | out);
	clrbits8(&wkup_gpio->wkup_dvo, reset);

	/* wait for 1 us */
	udelay(1);

	/* Deassert reset */
	setbits8(&wkup_gpio->wkup_dvo, reset);

	/* wait at least 200ns */
	/* 7 ~= (200ns * timebase) / ns2sec */
	__delay(7);

	/* Restore pin-muxing */
	out_be32(&simple_gpio->port_config, mux);

	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(mpc5200_psc_ac97_gpio_reset);
