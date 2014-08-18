/*
 * AEMIF support for DaVinci SoCs
 *
 * Copyright (C) 2010 Texas Instruments Incorporated. http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/time.h>

#include <linux/platform_data/mtd-davinci-aemif.h>
#include <linux/platform_data/mtd-davinci.h>

/* Timing value configuration */

#define TA(x)		((x) << 2)
#define RHOLD(x)	((x) << 4)
#define RSTROBE(x)	((x) << 7)
#define RSETUP(x)	((x) << 13)
#define WHOLD(x)	((x) << 17)
#define WSTROBE(x)	((x) << 20)
#define WSETUP(x)	((x) << 26)

#define TA_MAX		0x3
#define RHOLD_MAX	0x7
#define RSTROBE_MAX	0x3f
#define RSETUP_MAX	0xf
#define WHOLD_MAX	0x7
#define WSTROBE_MAX	0x3f
#define WSETUP_MAX	0xf

#define TIMING_MASK	(TA(TA_MAX) | \
				RHOLD(RHOLD_MAX) | \
				RSTROBE(RSTROBE_MAX) |	\
				RSETUP(RSETUP_MAX) | \
				WHOLD(WHOLD_MAX) | \
				WSTROBE(WSTROBE_MAX) | \
				WSETUP(WSETUP_MAX))

static inline unsigned int davinci_aemif_readl(void __iomem *base, int offset)
{
	return readl_relaxed(base + offset);
}

static inline void davinci_aemif_writel(void __iomem *base,
					int offset, unsigned long value)
{
	writel_relaxed(value, base + offset);
}

/*
 * aemif_calc_rate - calculate timing data.
 * @wanted: The cycle time needed in nanoseconds.
 * @clk: The input clock rate in kHz.
 * @max: The maximum divider value that can be programmed.
 *
 * On success, returns the calculated timing value minus 1 for easy
 * programming into AEMIF timing registers, else negative errno.
 */
static int aemif_calc_rate(int wanted, unsigned long clk, int max)
{
	int result;

	result = DIV_ROUND_UP((wanted * clk), NSEC_PER_MSEC) - 1;

	pr_debug("%s: result %d from %ld, %d\n", __func__, result, clk, wanted);

	/* It is generally OK to have a more relaxed timing than requested... */
	if (result < 0)
		result = 0;

	/* ... But configuring tighter timings is not an option. */
	else if (result > max)
		result = -EINVAL;

	return result;
}

/**
 * davinci_aemif_setup_timing - setup timing values for a given AEMIF interface
 * @t: timing values to be progammed
 * @base: The virtual base address of the AEMIF interface
 * @cs: chip-select to program the timing values for
 * @clkrate: the AEMIF clkrate
 *
 * This function programs the given timing values (in real clock) into the
 * AEMIF registers taking the AEMIF clock into account.
 *
 * This function does not use any locking while programming the AEMIF
 * because it is expected that there is only one user of a given
 * chip-select.
 *
 * Returns 0 on success, else negative errno.
 */
static int davinci_aemif_setup_timing(struct davinci_aemif_timing *t,
					void __iomem *base, unsigned cs,
					unsigned long clkrate)
{
	unsigned set, val;
	int ta, rhold, rstrobe, rsetup, whold, wstrobe, wsetup;
	unsigned offset = A1CR_OFFSET + cs * 4;

	if (!t)
		return 0;	/* Nothing to do */

	clkrate /= 1000;	/* turn clock into kHz for ease of use */

	ta	= aemif_calc_rate(t->ta, clkrate, TA_MAX);
	rhold	= aemif_calc_rate(t->rhold, clkrate, RHOLD_MAX);
	rstrobe	= aemif_calc_rate(t->rstrobe, clkrate, RSTROBE_MAX);
	rsetup	= aemif_calc_rate(t->rsetup, clkrate, RSETUP_MAX);
	whold	= aemif_calc_rate(t->whold, clkrate, WHOLD_MAX);
	wstrobe	= aemif_calc_rate(t->wstrobe, clkrate, WSTROBE_MAX);
	wsetup	= aemif_calc_rate(t->wsetup, clkrate, WSETUP_MAX);

	if (ta < 0 || rhold < 0 || rstrobe < 0 || rsetup < 0 ||
			whold < 0 || wstrobe < 0 || wsetup < 0) {
		pr_err("%s: cannot get suitable timings\n", __func__);
		return -EINVAL;
	}

	set = TA(ta) | RHOLD(rhold) | RSTROBE(rstrobe) | RSETUP(rsetup) |
		WHOLD(whold) | WSTROBE(wstrobe) | WSETUP(wsetup);

	val = __raw_readl(base + offset);
	val &= ~TIMING_MASK;
	val |= set;
	__raw_writel(val, base + offset);

	return 0;
}

/**
 * davinci_aemif_setup - setup AEMIF interface by davinci_nand_pdata
 * @pdev - link to platform device to setup settings for
 *
 * This function does not use any locking while programming the AEMIF
 * because it is expected that there is only one user of a given
 * chip-select.
 *
 * Returns 0 on success, else negative errno.
 */
int davinci_aemif_setup(struct platform_device *pdev)
{
	struct davinci_nand_pdata *pdata = dev_get_platdata(&pdev->dev);
	uint32_t val;
	unsigned long clkrate;
	struct resource	*res;
	void __iomem *base;
	struct clk *clk;
	int ret = 0;

	clk = clk_get(&pdev->dev, "aemif");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_dbg(&pdev->dev, "unable to get AEMIF clock, err %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(clk);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "unable to enable AEMIF clock, err %d\n",
			ret);
		goto err_put;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "cannot get IORESOURCE_MEM\n");
		ret = -ENOMEM;
		goto err;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		dev_err(&pdev->dev, "ioremap failed for resource %pR\n", res);
		ret = -ENOMEM;
		goto err;
	}

	/*
	 * Setup Async configuration register in case we did not boot
	 * from NAND and so bootloader did not bother to set it up.
	 */
	val = davinci_aemif_readl(base, A1CR_OFFSET + pdev->id * 4);
	/*
	 * Extended Wait is not valid and Select Strobe mode is not
	 * used
	 */
	val &= ~(ACR_ASIZE_MASK | ACR_EW_MASK | ACR_SS_MASK);
	if (pdata->options & NAND_BUSWIDTH_16)
		val |= 0x1;

	davinci_aemif_writel(base, A1CR_OFFSET + pdev->id * 4, val);

	clkrate = clk_get_rate(clk);

	if (pdata->timing)
		ret = davinci_aemif_setup_timing(pdata->timing, base, pdev->id,
						 clkrate);

	if (ret < 0)
		dev_dbg(&pdev->dev, "NAND timing values setup fail\n");

	iounmap(base);
err:
	clk_disable_unprepare(clk);
err_put:
	clk_put(clk);
	return ret;
}
