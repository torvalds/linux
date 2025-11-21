// SPDX-License-Identifier: GPL-2.0
/* Renesas R-Car Gen4 gPTP device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "rcar_gen4_ptp.h"

#define PTPTMEC_REG		0x0010
#define PTPTMDC_REG		0x0014
#define PTPTIVC0_REG		0x0020
#define PTPTOVC00_REG		0x0030
#define PTPTOVC10_REG		0x0034
#define PTPTOVC20_REG		0x0038
#define PTPGPTPTM00_REG		0x0050
#define PTPGPTPTM10_REG		0x0054
#define PTPGPTPTM20_REG		0x0058

#define ptp_to_priv(ptp)	container_of(ptp, struct rcar_gen4_ptp_private, info)

static int rcar_gen4_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct rcar_gen4_ptp_private *ptp_priv = ptp_to_priv(ptp);
	s64 addend = ptp_priv->default_addend;
	bool neg_adj = scaled_ppm < 0;
	s64 diff;

	if (neg_adj)
		scaled_ppm = -scaled_ppm;
	diff = div_s64(addend * scaled_ppm_to_ppb(scaled_ppm), NSEC_PER_SEC);
	addend = neg_adj ? addend - diff : addend + diff;

	iowrite32(addend, ptp_priv->addr + PTPTIVC0_REG);

	return 0;
}

static void _rcar_gen4_ptp_gettime(struct ptp_clock_info *ptp,
				   struct timespec64 *ts)
{
	struct rcar_gen4_ptp_private *ptp_priv = ptp_to_priv(ptp);

	lockdep_assert_held(&ptp_priv->lock);

	ts->tv_nsec = ioread32(ptp_priv->addr + PTPGPTPTM00_REG);
	ts->tv_sec = ioread32(ptp_priv->addr + PTPGPTPTM10_REG) |
		     ((s64)ioread32(ptp_priv->addr + PTPGPTPTM20_REG) << 32);
}

static int rcar_gen4_ptp_gettime(struct ptp_clock_info *ptp,
				 struct timespec64 *ts)
{
	struct rcar_gen4_ptp_private *ptp_priv = ptp_to_priv(ptp);
	unsigned long flags;

	spin_lock_irqsave(&ptp_priv->lock, flags);
	_rcar_gen4_ptp_gettime(ptp, ts);
	spin_unlock_irqrestore(&ptp_priv->lock, flags);

	return 0;
}

/* Caller must hold the lock */
static void _rcar_gen4_ptp_settime(struct ptp_clock_info *ptp,
				   const struct timespec64 *ts)
{
	struct rcar_gen4_ptp_private *ptp_priv = ptp_to_priv(ptp);

	iowrite32(1, ptp_priv->addr + PTPTMDC_REG);
	iowrite32(0, ptp_priv->addr + PTPTOVC20_REG);
	iowrite32(0, ptp_priv->addr + PTPTOVC10_REG);
	iowrite32(0, ptp_priv->addr + PTPTOVC00_REG);
	iowrite32(1, ptp_priv->addr + PTPTMEC_REG);
	iowrite32(ts->tv_sec >> 32, ptp_priv->addr + PTPTOVC20_REG);
	iowrite32(ts->tv_sec, ptp_priv->addr + PTPTOVC10_REG);
	iowrite32(ts->tv_nsec, ptp_priv->addr + PTPTOVC00_REG);
}

static int rcar_gen4_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct rcar_gen4_ptp_private *ptp_priv = ptp_to_priv(ptp);
	unsigned long flags;

	spin_lock_irqsave(&ptp_priv->lock, flags);
	_rcar_gen4_ptp_settime(ptp, ts);
	spin_unlock_irqrestore(&ptp_priv->lock, flags);

	return 0;
}

static int rcar_gen4_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct rcar_gen4_ptp_private *ptp_priv = ptp_to_priv(ptp);
	struct timespec64 ts;
	unsigned long flags;
	s64 now;

	spin_lock_irqsave(&ptp_priv->lock, flags);
	_rcar_gen4_ptp_gettime(ptp, &ts);
	now = ktime_to_ns(timespec64_to_ktime(ts));
	ts = ns_to_timespec64(now + delta);
	_rcar_gen4_ptp_settime(ptp, &ts);
	spin_unlock_irqrestore(&ptp_priv->lock, flags);

	return 0;
}

static int rcar_gen4_ptp_enable(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info rcar_gen4_ptp_info = {
	.owner = THIS_MODULE,
	.name = "rcar_gen4_ptp",
	.max_adj = 50000000,
	.adjfine = rcar_gen4_ptp_adjfine,
	.adjtime = rcar_gen4_ptp_adjtime,
	.gettime64 = rcar_gen4_ptp_gettime,
	.settime64 = rcar_gen4_ptp_settime,
	.enable = rcar_gen4_ptp_enable,
};

static s64 rcar_gen4_ptp_rate_to_increment(u32 rate)
{
	/* Timer increment in ns.
	 * bit[31:27] - integer
	 * bit[26:0]  - decimal
	 * increment[ns] = perid[ns] * 2^27 => (1ns * 2^27) / rate[hz]
	 */
	return div_s64(1000000000LL << 27, rate);
}

int rcar_gen4_ptp_register(struct rcar_gen4_ptp_private *ptp_priv, u32 rate)
{
	if (ptp_priv->initialized)
		return 0;

	spin_lock_init(&ptp_priv->lock);

	ptp_priv->default_addend = rcar_gen4_ptp_rate_to_increment(rate);
	iowrite32(ptp_priv->default_addend, ptp_priv->addr + PTPTIVC0_REG);
	ptp_priv->clock = ptp_clock_register(&ptp_priv->info, NULL);
	if (IS_ERR(ptp_priv->clock))
		return PTR_ERR(ptp_priv->clock);

	iowrite32(0x01, ptp_priv->addr + PTPTMEC_REG);
	ptp_priv->initialized = true;

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_gen4_ptp_register);

int rcar_gen4_ptp_unregister(struct rcar_gen4_ptp_private *ptp_priv)
{
	iowrite32(1, ptp_priv->addr + PTPTMDC_REG);

	return ptp_clock_unregister(ptp_priv->clock);
}
EXPORT_SYMBOL_GPL(rcar_gen4_ptp_unregister);

struct rcar_gen4_ptp_private *rcar_gen4_ptp_alloc(struct platform_device *pdev)
{
	struct rcar_gen4_ptp_private *ptp;

	ptp = devm_kzalloc(&pdev->dev, sizeof(*ptp), GFP_KERNEL);
	if (!ptp)
		return NULL;

	ptp->info = rcar_gen4_ptp_info;

	return ptp;
}
EXPORT_SYMBOL_GPL(rcar_gen4_ptp_alloc);

MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_DESCRIPTION("Renesas R-Car Gen4 gPTP driver");
MODULE_LICENSE("GPL");
