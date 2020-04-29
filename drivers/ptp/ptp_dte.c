/*
 * Copyright 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/types.h>

#define DTE_NCO_LOW_TIME_REG	0x00
#define DTE_NCO_TIME_REG	0x04
#define DTE_NCO_OVERFLOW_REG	0x08
#define DTE_NCO_INC_REG		0x0c

#define DTE_NCO_SUM2_MASK	0xffffffff
#define DTE_NCO_SUM2_SHIFT	4ULL

#define DTE_NCO_SUM3_MASK	0xff
#define DTE_NCO_SUM3_SHIFT	36ULL
#define DTE_NCO_SUM3_WR_SHIFT	8

#define DTE_NCO_TS_WRAP_MASK	0xfff
#define DTE_NCO_TS_WRAP_LSHIFT	32

#define DTE_NCO_INC_DEFAULT	0x80000000
#define DTE_NUM_REGS_TO_RESTORE	4

/* Full wrap around is 44bits in ns (~4.887 hrs) */
#define DTE_WRAP_AROUND_NSEC_SHIFT 44

/* 44 bits NCO */
#define DTE_NCO_MAX_NS	0xFFFFFFFFFFFLL

/* 125MHz with 3.29 reg cfg */
#define DTE_PPB_ADJ(ppb) (u32)(div64_u64((((u64)abs(ppb) * BIT(28)) +\
				      62500000ULL), 125000000ULL))

/* ptp dte priv structure */
struct ptp_dte {
	void __iomem *regs;
	struct ptp_clock *ptp_clk;
	struct ptp_clock_info caps;
	struct device *dev;
	u32 ts_ovf_last;
	u32 ts_wrap_cnt;
	spinlock_t lock;
	u32 reg_val[DTE_NUM_REGS_TO_RESTORE];
};

static void dte_write_nco(void __iomem *regs, s64 ns)
{
	u32 sum2, sum3;

	sum2 = (u32)((ns >> DTE_NCO_SUM2_SHIFT) & DTE_NCO_SUM2_MASK);
	/* compensate for ignoring sum1 */
	if (sum2 != DTE_NCO_SUM2_MASK)
		sum2++;

	/* to write sum3, bits [15:8] needs to be written */
	sum3 = (u32)(((ns >> DTE_NCO_SUM3_SHIFT) & DTE_NCO_SUM3_MASK) <<
		     DTE_NCO_SUM3_WR_SHIFT);

	writel(0, (regs + DTE_NCO_LOW_TIME_REG));
	writel(sum2, (regs + DTE_NCO_TIME_REG));
	writel(sum3, (regs + DTE_NCO_OVERFLOW_REG));
}

static s64 dte_read_nco(void __iomem *regs)
{
	u32 sum2, sum3;
	s64 ns;

	/*
	 * ignoring sum1 (4 bits) gives a 16ns resolution, which
	 * works due to the async register read.
	 */
	sum3 = readl(regs + DTE_NCO_OVERFLOW_REG) & DTE_NCO_SUM3_MASK;
	sum2 = readl(regs + DTE_NCO_TIME_REG);
	ns = ((s64)sum3 << DTE_NCO_SUM3_SHIFT) |
		 ((s64)sum2 << DTE_NCO_SUM2_SHIFT);

	return ns;
}

static void dte_write_nco_delta(struct ptp_dte *ptp_dte, s64 delta)
{
	s64 ns;

	ns = dte_read_nco(ptp_dte->regs);

	/* handle wraparound conditions */
	if ((delta < 0) && (abs(delta) > ns)) {
		if (ptp_dte->ts_wrap_cnt) {
			ns += DTE_NCO_MAX_NS + delta;
			ptp_dte->ts_wrap_cnt--;
		} else {
			ns = 0;
		}
	} else {
		ns += delta;
		if (ns > DTE_NCO_MAX_NS) {
			ptp_dte->ts_wrap_cnt++;
			ns -= DTE_NCO_MAX_NS;
		}
	}

	dte_write_nco(ptp_dte->regs, ns);

	ptp_dte->ts_ovf_last = (ns >> DTE_NCO_TS_WRAP_LSHIFT) &
			DTE_NCO_TS_WRAP_MASK;
}

static s64 dte_read_nco_with_ovf(struct ptp_dte *ptp_dte)
{
	u32 ts_ovf;
	s64 ns = 0;

	ns = dte_read_nco(ptp_dte->regs);

	/*Timestamp overflow: 8 LSB bits of sum3, 4 MSB bits of sum2 */
	ts_ovf = (ns >> DTE_NCO_TS_WRAP_LSHIFT) & DTE_NCO_TS_WRAP_MASK;

	/* Check for wrap around */
	if (ts_ovf < ptp_dte->ts_ovf_last)
		ptp_dte->ts_wrap_cnt++;

	ptp_dte->ts_ovf_last = ts_ovf;

	/* adjust for wraparounds */
	ns += (s64)(BIT_ULL(DTE_WRAP_AROUND_NSEC_SHIFT) * ptp_dte->ts_wrap_cnt);

	return ns;
}

static int ptp_dte_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	u32 nco_incr;
	unsigned long flags;
	struct ptp_dte *ptp_dte = container_of(ptp, struct ptp_dte, caps);

	if (abs(ppb) > ptp_dte->caps.max_adj) {
		dev_err(ptp_dte->dev, "ppb adj too big\n");
		return -EINVAL;
	}

	if (ppb < 0)
		nco_incr = DTE_NCO_INC_DEFAULT - DTE_PPB_ADJ(ppb);
	else
		nco_incr = DTE_NCO_INC_DEFAULT + DTE_PPB_ADJ(ppb);

	spin_lock_irqsave(&ptp_dte->lock, flags);
	writel(nco_incr, ptp_dte->regs + DTE_NCO_INC_REG);
	spin_unlock_irqrestore(&ptp_dte->lock, flags);

	return 0;
}

static int ptp_dte_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	unsigned long flags;
	struct ptp_dte *ptp_dte = container_of(ptp, struct ptp_dte, caps);

	spin_lock_irqsave(&ptp_dte->lock, flags);
	dte_write_nco_delta(ptp_dte, delta);
	spin_unlock_irqrestore(&ptp_dte->lock, flags);

	return 0;
}

static int ptp_dte_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	unsigned long flags;
	struct ptp_dte *ptp_dte = container_of(ptp, struct ptp_dte, caps);

	spin_lock_irqsave(&ptp_dte->lock, flags);
	*ts = ns_to_timespec64(dte_read_nco_with_ovf(ptp_dte));
	spin_unlock_irqrestore(&ptp_dte->lock, flags);

	return 0;
}

static int ptp_dte_settime(struct ptp_clock_info *ptp,
			     const struct timespec64 *ts)
{
	unsigned long flags;
	struct ptp_dte *ptp_dte = container_of(ptp, struct ptp_dte, caps);

	spin_lock_irqsave(&ptp_dte->lock, flags);

	/* Disable nco increment */
	writel(0, ptp_dte->regs + DTE_NCO_INC_REG);

	dte_write_nco(ptp_dte->regs, timespec64_to_ns(ts));

	/* reset overflow and wrap counter */
	ptp_dte->ts_ovf_last = 0;
	ptp_dte->ts_wrap_cnt = 0;

	/* Enable nco increment */
	writel(DTE_NCO_INC_DEFAULT, ptp_dte->regs + DTE_NCO_INC_REG);

	spin_unlock_irqrestore(&ptp_dte->lock, flags);

	return 0;
}

static int ptp_dte_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static const struct ptp_clock_info ptp_dte_caps = {
	.owner		= THIS_MODULE,
	.name		= "DTE PTP timer",
	.max_adj	= 50000000,
	.n_ext_ts	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= ptp_dte_adjfreq,
	.adjtime	= ptp_dte_adjtime,
	.gettime64	= ptp_dte_gettime,
	.settime64	= ptp_dte_settime,
	.enable		= ptp_dte_enable,
};

static int ptp_dte_probe(struct platform_device *pdev)
{
	struct ptp_dte *ptp_dte;
	struct device *dev = &pdev->dev;

	ptp_dte = devm_kzalloc(dev, sizeof(struct ptp_dte), GFP_KERNEL);
	if (!ptp_dte)
		return -ENOMEM;

	ptp_dte->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ptp_dte->regs))
		return PTR_ERR(ptp_dte->regs);

	spin_lock_init(&ptp_dte->lock);

	ptp_dte->dev = dev;
	ptp_dte->caps = ptp_dte_caps;
	ptp_dte->ptp_clk = ptp_clock_register(&ptp_dte->caps, &pdev->dev);
	if (IS_ERR(ptp_dte->ptp_clk)) {
		dev_err(dev,
			"%s: Failed to register ptp clock\n", __func__);
		return PTR_ERR(ptp_dte->ptp_clk);
	}

	platform_set_drvdata(pdev, ptp_dte);

	dev_info(dev, "ptp clk probe done\n");

	return 0;
}

static int ptp_dte_remove(struct platform_device *pdev)
{
	struct ptp_dte *ptp_dte = platform_get_drvdata(pdev);
	u8 i;

	ptp_clock_unregister(ptp_dte->ptp_clk);

	for (i = 0; i < DTE_NUM_REGS_TO_RESTORE; i++)
		writel(0, ptp_dte->regs + (i * sizeof(u32)));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ptp_dte_suspend(struct device *dev)
{
	struct ptp_dte *ptp_dte = dev_get_drvdata(dev);
	u8 i;

	for (i = 0; i < DTE_NUM_REGS_TO_RESTORE; i++) {
		ptp_dte->reg_val[i] =
			readl(ptp_dte->regs + (i * sizeof(u32)));
	}

	/* disable the nco */
	writel(0, ptp_dte->regs + DTE_NCO_INC_REG);

	return 0;
}

static int ptp_dte_resume(struct device *dev)
{
	struct ptp_dte *ptp_dte = dev_get_drvdata(dev);
	u8 i;

	for (i = 0; i < DTE_NUM_REGS_TO_RESTORE; i++) {
		if ((i * sizeof(u32)) != DTE_NCO_OVERFLOW_REG)
			writel(ptp_dte->reg_val[i],
				(ptp_dte->regs + (i * sizeof(u32))));
		else
			writel(((ptp_dte->reg_val[i] &
				DTE_NCO_SUM3_MASK) << DTE_NCO_SUM3_WR_SHIFT),
				(ptp_dte->regs + (i * sizeof(u32))));
	}

	return 0;
}

static const struct dev_pm_ops ptp_dte_pm_ops = {
	.suspend = ptp_dte_suspend,
	.resume = ptp_dte_resume
};

#define PTP_DTE_PM_OPS	(&ptp_dte_pm_ops)
#else
#define PTP_DTE_PM_OPS	NULL
#endif

static const struct of_device_id ptp_dte_of_match[] = {
	{ .compatible = "brcm,ptp-dte", },
	{},
};
MODULE_DEVICE_TABLE(of, ptp_dte_of_match);

static struct platform_driver ptp_dte_driver = {
	.driver = {
		.name = "ptp-dte",
		.pm = PTP_DTE_PM_OPS,
		.of_match_table = ptp_dte_of_match,
	},
	.probe    = ptp_dte_probe,
	.remove   = ptp_dte_remove,
};
module_platform_driver(ptp_dte_driver);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Broadcom DTE PTP Clock driver");
MODULE_LICENSE("GPL v2");
