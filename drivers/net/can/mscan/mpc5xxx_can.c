/*
 * CAN bus driver for the Freescale MPC5xxx embedded CPU.
 *
 * Copyright (C) 2004-2005 Andrey Volkov <avolkov@varma-el.com>,
 *                         Varma Electronics Oy
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2009 Wolfram Sang, Pengutronix <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/can/dev.h>
#include <linux/of_platform.h>
#include <sysdev/fsl_soc.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <asm/mpc52xx.h>

#include "mscan.h"

#define DRV_NAME "mpc5xxx_can"

struct mpc5xxx_can_data {
	unsigned int type;
	u32 (*get_clock)(struct platform_device *ofdev, const char *clock_name,
			 int *mscan_clksrc);
	void (*put_clock)(struct platform_device *ofdev);
};

#ifdef CONFIG_PPC_MPC52xx
static struct of_device_id mpc52xx_cdm_ids[] = {
	{ .compatible = "fsl,mpc5200-cdm", },
	{}
};

static u32 mpc52xx_can_get_clock(struct platform_device *ofdev,
				 const char *clock_name, int *mscan_clksrc)
{
	unsigned int pvr;
	struct mpc52xx_cdm  __iomem *cdm;
	struct device_node *np_cdm;
	unsigned int freq;
	u32 val;

	pvr = mfspr(SPRN_PVR);

	/*
	 * Either the oscillator clock (SYS_XTAL_IN) or the IP bus clock
	 * (IP_CLK) can be selected as MSCAN clock source. According to
	 * the MPC5200 user's manual, the oscillator clock is the better
	 * choice as it has less jitter. For this reason, it is selected
	 * by default. Unfortunately, it can not be selected for the old
	 * MPC5200 Rev. A chips due to a hardware bug (check errata).
	 */
	if (clock_name && strcmp(clock_name, "ip") == 0)
		*mscan_clksrc = MSCAN_CLKSRC_BUS;
	else
		*mscan_clksrc = MSCAN_CLKSRC_XTAL;

	freq = mpc5xxx_get_bus_frequency(ofdev->dev.of_node);
	if (!freq)
		return 0;

	if (*mscan_clksrc == MSCAN_CLKSRC_BUS || pvr == 0x80822011)
		return freq;

	/* Determine SYS_XTAL_IN frequency from the clock domain settings */
	np_cdm = of_find_matching_node(NULL, mpc52xx_cdm_ids);
	if (!np_cdm) {
		dev_err(&ofdev->dev, "can't get clock node!\n");
		return 0;
	}
	cdm = of_iomap(np_cdm, 0);

	if (in_8(&cdm->ipb_clk_sel) & 0x1)
		freq *= 2;
	val = in_be32(&cdm->rstcfg);

	freq *= (val & (1 << 5)) ? 8 : 4;
	freq /= (val & (1 << 6)) ? 12 : 16;

	of_node_put(np_cdm);
	iounmap(cdm);

	return freq;
}
#else /* !CONFIG_PPC_MPC52xx */
static u32 mpc52xx_can_get_clock(struct platform_device *ofdev,
				 const char *clock_name, int *mscan_clksrc)
{
	return 0;
}
#endif /* CONFIG_PPC_MPC52xx */

#ifdef CONFIG_PPC_MPC512x

#if IS_ENABLED(CONFIG_COMMON_CLK)

static u32 mpc512x_can_get_clock(struct platform_device *ofdev,
				 const char *clock_source, int *mscan_clksrc)
{
	struct device_node *np;
	u32 clockdiv;
	enum {
		CLK_FROM_AUTO,
		CLK_FROM_IPS,
		CLK_FROM_SYS,
		CLK_FROM_REF,
	} clk_from;
	struct clk *clk_in, *clk_can;
	unsigned long freq_calc;
	struct mscan_priv *priv;
	struct clk *clk_ipg;

	/* the caller passed in the clock source spec that was read from
	 * the device tree, get the optional clock divider as well
	 */
	np = ofdev->dev.of_node;
	clockdiv = 1;
	of_property_read_u32(np, "fsl,mscan-clock-divider", &clockdiv);
	dev_dbg(&ofdev->dev, "device tree specs: clk src[%s] div[%d]\n",
		clock_source ? clock_source : "<NULL>", clockdiv);

	/* when clock-source is 'ip', the CANCTL1[CLKSRC] bit needs to
	 * get set, and the 'ips' clock is the input to the MSCAN
	 * component
	 *
	 * for clock-source values of 'ref' or 'sys' the CANCTL1[CLKSRC]
	 * bit needs to get cleared, an optional clock-divider may have
	 * been specified (the default value is 1), the appropriate
	 * MSCAN related MCLK is the input to the MSCAN component
	 *
	 * in the absence of a clock-source spec, first an optimal clock
	 * gets determined based on the 'sys' clock, if that fails the
	 * 'ref' clock is used
	 */
	clk_from = CLK_FROM_AUTO;
	if (clock_source) {
		/* interpret the device tree's spec for the clock source */
		if (!strcmp(clock_source, "ip"))
			clk_from = CLK_FROM_IPS;
		else if (!strcmp(clock_source, "sys"))
			clk_from = CLK_FROM_SYS;
		else if (!strcmp(clock_source, "ref"))
			clk_from = CLK_FROM_REF;
		else
			goto err_invalid;
		dev_dbg(&ofdev->dev, "got a clk source spec[%d]\n", clk_from);
	}
	if (clk_from == CLK_FROM_AUTO) {
		/* no spec so far, try the 'sys' clock; round to the
		 * next MHz and see if we can get a multiple of 16MHz
		 */
		dev_dbg(&ofdev->dev, "no clk source spec, trying SYS\n");
		clk_in = devm_clk_get(&ofdev->dev, "sys");
		if (IS_ERR(clk_in))
			goto err_notavail;
		freq_calc = clk_get_rate(clk_in);
		freq_calc +=  499999;
		freq_calc /= 1000000;
		freq_calc *= 1000000;
		if ((freq_calc % 16000000) == 0) {
			clk_from = CLK_FROM_SYS;
			clockdiv = freq_calc / 16000000;
			dev_dbg(&ofdev->dev,
				"clk fit, sys[%lu] div[%d] freq[%lu]\n",
				freq_calc, clockdiv, freq_calc / clockdiv);
		}
	}
	if (clk_from == CLK_FROM_AUTO) {
		/* no spec so far, use the 'ref' clock */
		dev_dbg(&ofdev->dev, "no clk source spec, trying REF\n");
		clk_in = devm_clk_get(&ofdev->dev, "ref");
		if (IS_ERR(clk_in))
			goto err_notavail;
		clk_from = CLK_FROM_REF;
		freq_calc = clk_get_rate(clk_in);
		dev_dbg(&ofdev->dev,
			"clk fit, ref[%lu] (no div) freq[%lu]\n",
			freq_calc, freq_calc);
	}

	/* select IPS or MCLK as the MSCAN input (returned to the caller),
	 * setup the MCLK mux source and rate if applicable, apply the
	 * optionally specified or derived above divider, and determine
	 * the actual resulting clock rate to return to the caller
	 */
	switch (clk_from) {
	case CLK_FROM_IPS:
		clk_can = devm_clk_get(&ofdev->dev, "ips");
		if (IS_ERR(clk_can))
			goto err_notavail;
		priv = netdev_priv(dev_get_drvdata(&ofdev->dev));
		priv->clk_can = clk_can;
		freq_calc = clk_get_rate(clk_can);
		*mscan_clksrc = MSCAN_CLKSRC_IPS;
		dev_dbg(&ofdev->dev, "clk from IPS, clksrc[%d] freq[%lu]\n",
			*mscan_clksrc, freq_calc);
		break;
	case CLK_FROM_SYS:
	case CLK_FROM_REF:
		clk_can = devm_clk_get(&ofdev->dev, "mclk");
		if (IS_ERR(clk_can))
			goto err_notavail;
		priv = netdev_priv(dev_get_drvdata(&ofdev->dev));
		priv->clk_can = clk_can;
		if (clk_from == CLK_FROM_SYS)
			clk_in = devm_clk_get(&ofdev->dev, "sys");
		if (clk_from == CLK_FROM_REF)
			clk_in = devm_clk_get(&ofdev->dev, "ref");
		if (IS_ERR(clk_in))
			goto err_notavail;
		clk_set_parent(clk_can, clk_in);
		freq_calc = clk_get_rate(clk_in);
		freq_calc /= clockdiv;
		clk_set_rate(clk_can, freq_calc);
		freq_calc = clk_get_rate(clk_can);
		*mscan_clksrc = MSCAN_CLKSRC_BUS;
		dev_dbg(&ofdev->dev, "clk from MCLK, clksrc[%d] freq[%lu]\n",
			*mscan_clksrc, freq_calc);
		break;
	default:
		goto err_invalid;
	}

	/* the above clk_can item is used for the bitrate, access to
	 * the peripheral's register set needs the clk_ipg item
	 */
	clk_ipg = devm_clk_get(&ofdev->dev, "ipg");
	if (IS_ERR(clk_ipg))
		goto err_notavail_ipg;
	if (clk_prepare_enable(clk_ipg))
		goto err_notavail_ipg;
	priv = netdev_priv(dev_get_drvdata(&ofdev->dev));
	priv->clk_ipg = clk_ipg;

	/* return the determined clock source rate */
	return freq_calc;

err_invalid:
	dev_err(&ofdev->dev, "invalid clock source specification\n");
	/* clock source rate could not get determined */
	return 0;

err_notavail:
	dev_err(&ofdev->dev, "cannot acquire or setup bitrate clock source\n");
	/* clock source rate could not get determined */
	return 0;

err_notavail_ipg:
	dev_err(&ofdev->dev, "cannot acquire or setup register clock\n");
	/* clock source rate could not get determined */
	return 0;
}

static void mpc512x_can_put_clock(struct platform_device *ofdev)
{
	struct mscan_priv *priv;

	priv = netdev_priv(dev_get_drvdata(&ofdev->dev));
	if (priv->clk_ipg)
		clk_disable_unprepare(priv->clk_ipg);
}

#else	/* COMMON_CLK */

struct mpc512x_clockctl {
	u32 spmr;		/* System PLL Mode Reg */
	u32 sccr[2];		/* System Clk Ctrl Reg 1 & 2 */
	u32 scfr1;		/* System Clk Freq Reg 1 */
	u32 scfr2;		/* System Clk Freq Reg 2 */
	u32 reserved;
	u32 bcr;		/* Bread Crumb Reg */
	u32 pccr[12];		/* PSC Clk Ctrl Reg 0-11 */
	u32 spccr;		/* SPDIF Clk Ctrl Reg */
	u32 cccr;		/* CFM Clk Ctrl Reg */
	u32 dccr;		/* DIU Clk Cnfg Reg */
	u32 mccr[4];		/* MSCAN Clk Ctrl Reg 1-3 */
};

static struct of_device_id mpc512x_clock_ids[] = {
	{ .compatible = "fsl,mpc5121-clock", },
	{}
};

static u32 mpc512x_can_get_clock(struct platform_device *ofdev,
				 const char *clock_name, int *mscan_clksrc)
{
	struct mpc512x_clockctl __iomem *clockctl;
	struct device_node *np_clock;
	struct clk *sys_clk, *ref_clk;
	int plen, clockidx, clocksrc = -1;
	u32 sys_freq, val, clockdiv = 1, freq = 0;
	const u32 *pval;

	np_clock = of_find_matching_node(NULL, mpc512x_clock_ids);
	if (!np_clock) {
		dev_err(&ofdev->dev, "couldn't find clock node\n");
		return 0;
	}
	clockctl = of_iomap(np_clock, 0);
	if (!clockctl) {
		dev_err(&ofdev->dev, "couldn't map clock registers\n");
		goto exit_put;
	}

	/* Determine the MSCAN device index from the peripheral's
	 * physical address. Register address offsets against the
	 * IMMR base are:  0x1300, 0x1380, 0x2300, 0x2380
	 */
	pval = of_get_property(ofdev->dev.of_node, "reg", &plen);
	BUG_ON(!pval || plen < sizeof(*pval));
	clockidx = (*pval & 0x80) ? 1 : 0;
	if (*pval & 0x2000)
		clockidx += 2;

	/*
	 * Clock source and divider selection: 3 different clock sources
	 * can be selected: "ip", "ref" or "sys". For the latter two, a
	 * clock divider can be defined as well. If the clock source is
	 * not specified by the device tree, we first try to find an
	 * optimal CAN source clock based on the system clock. If that
	 * is not posslible, the reference clock will be used.
	 */
	if (clock_name && !strcmp(clock_name, "ip")) {
		*mscan_clksrc = MSCAN_CLKSRC_IPS;
		freq = mpc5xxx_get_bus_frequency(ofdev->dev.of_node);
	} else {
		*mscan_clksrc = MSCAN_CLKSRC_BUS;

		pval = of_get_property(ofdev->dev.of_node,
				       "fsl,mscan-clock-divider", &plen);
		if (pval && plen == sizeof(*pval))
			clockdiv = *pval;
		if (!clockdiv)
			clockdiv = 1;

		if (!clock_name || !strcmp(clock_name, "sys")) {
			sys_clk = devm_clk_get(&ofdev->dev, "sys_clk");
			if (IS_ERR(sys_clk)) {
				dev_err(&ofdev->dev, "couldn't get sys_clk\n");
				goto exit_unmap;
			}
			/* Get and round up/down sys clock rate */
			sys_freq = 1000000 *
				((clk_get_rate(sys_clk) + 499999) / 1000000);

			if (!clock_name) {
				/* A multiple of 16 MHz would be optimal */
				if ((sys_freq % 16000000) == 0) {
					clocksrc = 0;
					clockdiv = sys_freq / 16000000;
					freq = sys_freq / clockdiv;
				}
			} else {
				clocksrc = 0;
				freq = sys_freq / clockdiv;
			}
		}

		if (clocksrc < 0) {
			ref_clk = devm_clk_get(&ofdev->dev, "ref_clk");
			if (IS_ERR(ref_clk)) {
				dev_err(&ofdev->dev, "couldn't get ref_clk\n");
				goto exit_unmap;
			}
			clocksrc = 1;
			freq = clk_get_rate(ref_clk) / clockdiv;
		}
	}

	/* Disable clock */
	out_be32(&clockctl->mccr[clockidx], 0x0);
	if (clocksrc >= 0) {
		/* Set source and divider */
		val = (clocksrc << 14) | ((clockdiv - 1) << 17);
		out_be32(&clockctl->mccr[clockidx], val);
		/* Enable clock */
		out_be32(&clockctl->mccr[clockidx], val | 0x10000);
	}

	/* Enable MSCAN clock domain */
	val = in_be32(&clockctl->sccr[1]);
	if (!(val & (1 << 25)))
		out_be32(&clockctl->sccr[1], val | (1 << 25));

	dev_dbg(&ofdev->dev, "using '%s' with frequency divider %d\n",
		*mscan_clksrc == MSCAN_CLKSRC_IPS ? "ips_clk" :
		clocksrc == 1 ? "ref_clk" : "sys_clk", clockdiv);

exit_unmap:
	iounmap(clockctl);
exit_put:
	of_node_put(np_clock);
	return freq;
}

#define mpc512x_can_put_clock NULL

#endif	/* COMMON_CLK */

#else /* !CONFIG_PPC_MPC512x */
static u32 mpc512x_can_get_clock(struct platform_device *ofdev,
				 const char *clock_name, int *mscan_clksrc)
{
	return 0;
}
#define mpc512x_can_put_clock NULL
#endif /* CONFIG_PPC_MPC512x */

static const struct of_device_id mpc5xxx_can_table[];
static int mpc5xxx_can_probe(struct platform_device *ofdev)
{
	const struct of_device_id *match;
	const struct mpc5xxx_can_data *data;
	struct device_node *np = ofdev->dev.of_node;
	struct net_device *dev;
	struct mscan_priv *priv;
	void __iomem *base;
	const char *clock_name = NULL;
	int irq, mscan_clksrc = 0;
	int err = -ENOMEM;

	match = of_match_device(mpc5xxx_can_table, &ofdev->dev);
	if (!match)
		return -EINVAL;
	data = match->data;

	base = of_iomap(np, 0);
	if (!base) {
		dev_err(&ofdev->dev, "couldn't ioremap\n");
		return err;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		dev_err(&ofdev->dev, "no irq found\n");
		err = -ENODEV;
		goto exit_unmap_mem;
	}

	dev = alloc_mscandev();
	if (!dev)
		goto exit_dispose_irq;
	platform_set_drvdata(ofdev, dev);
	SET_NETDEV_DEV(dev, &ofdev->dev);

	priv = netdev_priv(dev);
	priv->reg_base = base;
	dev->irq = irq;

	clock_name = of_get_property(np, "fsl,mscan-clock-source", NULL);

	BUG_ON(!data);
	priv->type = data->type;
	priv->can.clock.freq = data->get_clock(ofdev, clock_name,
					       &mscan_clksrc);
	if (!priv->can.clock.freq) {
		dev_err(&ofdev->dev, "couldn't get MSCAN clock properties\n");
		goto exit_free_mscan;
	}

	err = register_mscandev(dev, mscan_clksrc);
	if (err) {
		dev_err(&ofdev->dev, "registering %s failed (err=%d)\n",
			DRV_NAME, err);
		goto exit_free_mscan;
	}

	dev_info(&ofdev->dev, "MSCAN at 0x%p, irq %d, clock %d Hz\n",
		 priv->reg_base, dev->irq, priv->can.clock.freq);

	return 0;

exit_free_mscan:
	free_candev(dev);
exit_dispose_irq:
	irq_dispose_mapping(irq);
exit_unmap_mem:
	iounmap(base);

	return err;
}

static int mpc5xxx_can_remove(struct platform_device *ofdev)
{
	const struct of_device_id *match;
	const struct mpc5xxx_can_data *data;
	struct net_device *dev = platform_get_drvdata(ofdev);
	struct mscan_priv *priv = netdev_priv(dev);

	match = of_match_device(mpc5xxx_can_table, &ofdev->dev);
	data = match ? match->data : NULL;

	unregister_mscandev(dev);
	if (data && data->put_clock)
		data->put_clock(ofdev);
	iounmap(priv->reg_base);
	irq_dispose_mapping(dev->irq);
	free_candev(dev);

	return 0;
}

#ifdef CONFIG_PM
static struct mscan_regs saved_regs;
static int mpc5xxx_can_suspend(struct platform_device *ofdev, pm_message_t state)
{
	struct net_device *dev = platform_get_drvdata(ofdev);
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs *regs = (struct mscan_regs *)priv->reg_base;

	_memcpy_fromio(&saved_regs, regs, sizeof(*regs));

	return 0;
}

static int mpc5xxx_can_resume(struct platform_device *ofdev)
{
	struct net_device *dev = platform_get_drvdata(ofdev);
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs *regs = (struct mscan_regs *)priv->reg_base;

	regs->canctl0 |= MSCAN_INITRQ;
	while (!(regs->canctl1 & MSCAN_INITAK))
		udelay(10);

	regs->canctl1 = saved_regs.canctl1;
	regs->canbtr0 = saved_regs.canbtr0;
	regs->canbtr1 = saved_regs.canbtr1;
	regs->canidac = saved_regs.canidac;

	/* restore masks, buffers etc. */
	_memcpy_toio(&regs->canidar1_0, (void *)&saved_regs.canidar1_0,
		     sizeof(*regs) - offsetof(struct mscan_regs, canidar1_0));

	regs->canctl0 &= ~MSCAN_INITRQ;
	regs->cantbsel = saved_regs.cantbsel;
	regs->canrier = saved_regs.canrier;
	regs->cantier = saved_regs.cantier;
	regs->canctl0 = saved_regs.canctl0;

	return 0;
}
#endif

static const struct mpc5xxx_can_data mpc5200_can_data = {
	.type = MSCAN_TYPE_MPC5200,
	.get_clock = mpc52xx_can_get_clock,
	/* .put_clock not applicable */
};

static const struct mpc5xxx_can_data mpc5121_can_data = {
	.type = MSCAN_TYPE_MPC5121,
	.get_clock = mpc512x_can_get_clock,
	.put_clock = mpc512x_can_put_clock,
};

static const struct of_device_id mpc5xxx_can_table[] = {
	{ .compatible = "fsl,mpc5200-mscan", .data = &mpc5200_can_data, },
	/* Note that only MPC5121 Rev. 2 (and later) is supported */
	{ .compatible = "fsl,mpc5121-mscan", .data = &mpc5121_can_data, },
	{},
};
MODULE_DEVICE_TABLE(of, mpc5xxx_can_table);

static struct platform_driver mpc5xxx_can_driver = {
	.driver = {
		.name = "mpc5xxx_can",
		.owner = THIS_MODULE,
		.of_match_table = mpc5xxx_can_table,
	},
	.probe = mpc5xxx_can_probe,
	.remove = mpc5xxx_can_remove,
#ifdef CONFIG_PM
	.suspend = mpc5xxx_can_suspend,
	.resume = mpc5xxx_can_resume,
#endif
};

module_platform_driver(mpc5xxx_can_driver);

MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");
MODULE_DESCRIPTION("Freescale MPC5xxx CAN driver");
MODULE_LICENSE("GPL v2");
