// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, Intel Corporation
 * stmmac EST(802.3 Qbv) handling
 */
#include <linux/iopoll.h>
#include <linux/types.h>
#include "stmmac.h"
#include "stmmac_est.h"

static int est_write(void __iomem *est_addr, u32 reg, u32 val, bool gcl)
{
	u32 ctrl;

	writel(val, est_addr + EST_GCL_DATA);

	ctrl = (reg << EST_ADDR_SHIFT);
	ctrl |= gcl ? 0 : EST_GCRR;
	writel(ctrl, est_addr + EST_GCL_CONTROL);

	ctrl |= EST_SRWO;
	writel(ctrl, est_addr + EST_GCL_CONTROL);

	return readl_poll_timeout(est_addr + EST_GCL_CONTROL, ctrl,
				  !(ctrl & EST_SRWO), 100, 5000);
}

static int est_configure(struct stmmac_priv *priv, struct stmmac_est *cfg,
			 unsigned int ptp_rate)
{
	void __iomem *est_addr = priv->estaddr;
	int i, ret = 0;
	u32 ctrl;

	if (!ptp_rate) {
		netdev_warn(priv->dev, "Invalid PTP rate");
		return -EINVAL;
	}

	ret |= est_write(est_addr, EST_BTR_LOW, cfg->btr[0], false);
	ret |= est_write(est_addr, EST_BTR_HIGH, cfg->btr[1], false);
	ret |= est_write(est_addr, EST_TER, cfg->ter, false);
	ret |= est_write(est_addr, EST_LLR, cfg->gcl_size, false);
	ret |= est_write(est_addr, EST_CTR_LOW, cfg->ctr[0], false);
	ret |= est_write(est_addr, EST_CTR_HIGH, cfg->ctr[1], false);
	if (ret)
		return ret;

	for (i = 0; i < cfg->gcl_size; i++) {
		ret = est_write(est_addr, i, cfg->gcl[i], true);
		if (ret)
			return ret;
	}

	ctrl = readl(est_addr + EST_CONTROL);
	if (priv->plat->has_xgmac) {
		ctrl &= ~EST_XGMAC_PTOV;
		ctrl |= ((NSEC_PER_SEC / ptp_rate) * EST_XGMAC_PTOV_MUL) <<
			 EST_XGMAC_PTOV_SHIFT;
	} else {
		ctrl &= ~EST_GMAC5_PTOV;
		ctrl |= ((NSEC_PER_SEC / ptp_rate) * EST_GMAC5_PTOV_MUL) <<
			 EST_GMAC5_PTOV_SHIFT;
	}
	if (cfg->enable)
		ctrl |= EST_EEST | EST_SSWL;
	else
		ctrl &= ~EST_EEST;

	writel(ctrl, est_addr + EST_CONTROL);

	/* Configure EST interrupt */
	if (cfg->enable)
		ctrl = EST_IECGCE | EST_IEHS | EST_IEHF | EST_IEBE | EST_IECC;
	else
		ctrl = 0;

	writel(ctrl, est_addr + EST_INT_EN);

	return 0;
}

static void est_irq_status(struct stmmac_priv *priv, struct net_device *dev,
			   struct stmmac_extra_stats *x, u32 txqcnt)
{
	u32 status, value, feqn, hbfq, hbfs, btrl, btrl_max;
	void __iomem *est_addr = priv->estaddr;
	u32 txqcnt_mask = BIT(txqcnt) - 1;
	int i;

	status = readl(est_addr + EST_STATUS);

	value = EST_CGCE | EST_HLBS | EST_HLBF | EST_BTRE | EST_SWLC;

	/* Return if there is no error */
	if (!(status & value))
		return;

	if (status & EST_CGCE) {
		/* Clear Interrupt */
		writel(EST_CGCE, est_addr + EST_STATUS);

		x->mtl_est_cgce++;
	}

	if (status & EST_HLBS) {
		value = readl(est_addr + EST_SCH_ERR);
		value &= txqcnt_mask;

		x->mtl_est_hlbs++;

		/* Clear Interrupt */
		writel(value, est_addr + EST_SCH_ERR);

		/* Collecting info to shows all the queues that has HLBS
		 * issue. The only way to clear this is to clear the
		 * statistic
		 */
		if (net_ratelimit())
			netdev_err(dev, "EST: HLB(sched) Queue 0x%x\n", value);
	}

	if (status & EST_HLBF) {
		value = readl(est_addr + EST_FRM_SZ_ERR);
		feqn = value & txqcnt_mask;

		value = readl(est_addr + EST_FRM_SZ_CAP);
		hbfq = (value & EST_SZ_CAP_HBFQ_MASK(txqcnt)) >>
			EST_SZ_CAP_HBFQ_SHIFT;
		hbfs = value & EST_SZ_CAP_HBFS_MASK;

		x->mtl_est_hlbf++;

		for (i = 0; i < txqcnt; i++) {
			if (feqn & BIT(i))
				x->mtl_est_txq_hlbf[i]++;
		}

		/* Clear Interrupt */
		writel(feqn, est_addr + EST_FRM_SZ_ERR);

		if (net_ratelimit())
			netdev_err(dev, "EST: HLB(size) Queue %u Size %u\n",
				   hbfq, hbfs);
	}

	if (status & EST_BTRE) {
		if (priv->plat->has_xgmac) {
			btrl = FIELD_GET(EST_XGMAC_BTRL, status);
			btrl_max = FIELD_MAX(EST_XGMAC_BTRL);
		} else {
			btrl = FIELD_GET(EST_GMAC5_BTRL, status);
			btrl_max = FIELD_MAX(EST_GMAC5_BTRL);
		}
		if (btrl == btrl_max)
			x->mtl_est_btrlm++;
		else
			x->mtl_est_btre++;

		if (net_ratelimit())
			netdev_info(dev, "EST: BTR Error Loop Count %u\n",
				    btrl);

		writel(EST_BTRE, est_addr + EST_STATUS);
	}

	if (status & EST_SWLC) {
		writel(EST_SWLC, est_addr + EST_STATUS);
		netdev_info(dev, "EST: SWOL has been switched\n");
	}
}

const struct stmmac_est_ops dwmac510_est_ops = {
	.configure = est_configure,
	.irq_status = est_irq_status,
};
