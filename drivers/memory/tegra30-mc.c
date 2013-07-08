/*
 * Tegra30 Memory Controller
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define DRV_NAME "tegra30-mc"

#define MC_INTSTATUS			0x0
#define MC_INTMASK			0x4

#define MC_INT_ERR_SHIFT		6
#define MC_INT_ERR_MASK			(0x1f << MC_INT_ERR_SHIFT)
#define MC_INT_DECERR_EMEM		BIT(MC_INT_ERR_SHIFT)
#define MC_INT_SECURITY_VIOLATION	BIT(MC_INT_ERR_SHIFT + 2)
#define MC_INT_ARBITRATION_EMEM		BIT(MC_INT_ERR_SHIFT + 3)
#define MC_INT_INVALID_SMMU_PAGE	BIT(MC_INT_ERR_SHIFT + 4)

#define MC_ERR_STATUS			0x8
#define MC_ERR_ADR			0xc

#define MC_ERR_TYPE_SHIFT		28
#define MC_ERR_TYPE_MASK		(7 << MC_ERR_TYPE_SHIFT)
#define MC_ERR_TYPE_DECERR_EMEM		2
#define MC_ERR_TYPE_SECURITY_TRUSTZONE	3
#define MC_ERR_TYPE_SECURITY_CARVEOUT	4
#define MC_ERR_TYPE_INVALID_SMMU_PAGE	6

#define MC_ERR_INVALID_SMMU_PAGE_SHIFT	25
#define MC_ERR_INVALID_SMMU_PAGE_MASK	(7 << MC_ERR_INVALID_SMMU_PAGE_SHIFT)
#define MC_ERR_RW_SHIFT			16
#define MC_ERR_RW			BIT(MC_ERR_RW_SHIFT)
#define MC_ERR_SECURITY			BIT(MC_ERR_RW_SHIFT + 1)

#define SECURITY_VIOLATION_TYPE		BIT(30)	/* 0=TRUSTZONE, 1=CARVEOUT */

#define MC_EMEM_ARB_CFG			0x90
#define MC_EMEM_ARB_OUTSTANDING_REQ	0x94
#define MC_EMEM_ARB_TIMING_RCD		0x98
#define MC_EMEM_ARB_TIMING_RP		0x9c
#define MC_EMEM_ARB_TIMING_RC		0xa0
#define MC_EMEM_ARB_TIMING_RAS		0xa4
#define MC_EMEM_ARB_TIMING_FAW		0xa8
#define MC_EMEM_ARB_TIMING_RRD		0xac
#define MC_EMEM_ARB_TIMING_RAP2PRE	0xb0
#define MC_EMEM_ARB_TIMING_WAP2PRE	0xb4
#define MC_EMEM_ARB_TIMING_R2R		0xb8
#define MC_EMEM_ARB_TIMING_W2W		0xbc
#define MC_EMEM_ARB_TIMING_R2W		0xc0
#define MC_EMEM_ARB_TIMING_W2R		0xc4

#define MC_EMEM_ARB_DA_TURNS		0xd0
#define MC_EMEM_ARB_DA_COVERS		0xd4
#define MC_EMEM_ARB_MISC0		0xd8
#define MC_EMEM_ARB_MISC1		0xdc

#define MC_EMEM_ARB_RING3_THROTTLE	0xe4
#define MC_EMEM_ARB_OVERRIDE		0xe8

#define MC_TIMING_CONTROL		0xfc

#define MC_CLIENT_ID_MASK		0x7f

#define NUM_MC_REG_BANKS		4

struct tegra30_mc {
	void __iomem *regs[NUM_MC_REG_BANKS];
	struct device *dev;
	u32 ctx[0];
};

static inline u32 mc_readl(struct tegra30_mc *mc, u32 offs)
{
	u32 val = 0;

	if (offs < 0x10)
		val = readl(mc->regs[0] + offs);
	else if (offs < 0x1f0)
		val = readl(mc->regs[1] + offs - 0x3c);
	else if (offs < 0x228)
		val = readl(mc->regs[2] + offs - 0x200);
	else if (offs < 0x400)
		val = readl(mc->regs[3] + offs - 0x284);

	return val;
}

static inline void mc_writel(struct tegra30_mc *mc, u32 val, u32 offs)
{
	if (offs < 0x10)
		writel(val, mc->regs[0] + offs);
	else if (offs < 0x1f0)
		writel(val, mc->regs[1] + offs - 0x3c);
	else if (offs < 0x228)
		writel(val, mc->regs[2] + offs - 0x200);
	else if (offs < 0x400)
		writel(val, mc->regs[3] + offs - 0x284);
}

static const char * const tegra30_mc_client[] = {
	"csr_ptcr",
	"cbr_display0a",
	"cbr_display0ab",
	"cbr_display0b",
	"cbr_display0bb",
	"cbr_display0c",
	"cbr_display0cb",
	"cbr_display1b",
	"cbr_display1bb",
	"cbr_eppup",
	"cbr_g2pr",
	"cbr_g2sr",
	"cbr_mpeunifbr",
	"cbr_viruv",
	"csr_afir",
	"csr_avpcarm7r",
	"csr_displayhc",
	"csr_displayhcb",
	"csr_fdcdrd",
	"csr_fdcdrd2",
	"csr_g2dr",
	"csr_hdar",
	"csr_host1xdmar",
	"csr_host1xr",
	"csr_idxsrd",
	"csr_idxsrd2",
	"csr_mpe_ipred",
	"csr_mpeamemrd",
	"csr_mpecsrd",
	"csr_ppcsahbdmar",
	"csr_ppcsahbslvr",
	"csr_satar",
	"csr_texsrd",
	"csr_texsrd2",
	"csr_vdebsevr",
	"csr_vdember",
	"csr_vdemcer",
	"csr_vdetper",
	"csr_mpcorelpr",
	"csr_mpcorer",
	"cbw_eppu",
	"cbw_eppv",
	"cbw_eppy",
	"cbw_mpeunifbw",
	"cbw_viwsb",
	"cbw_viwu",
	"cbw_viwv",
	"cbw_viwy",
	"ccw_g2dw",
	"csw_afiw",
	"csw_avpcarm7w",
	"csw_fdcdwr",
	"csw_fdcdwr2",
	"csw_hdaw",
	"csw_host1xw",
	"csw_ispw",
	"csw_mpcorelpw",
	"csw_mpcorew",
	"csw_mpecswr",
	"csw_ppcsahbdmaw",
	"csw_ppcsahbslvw",
	"csw_sataw",
	"csw_vdebsevw",
	"csw_vdedbgw",
	"csw_vdembew",
	"csw_vdetpmw",
};

static void tegra30_mc_decode(struct tegra30_mc *mc, int n)
{
	u32 err, addr;
	const char * const mc_int_err[] = {
		"MC_DECERR",
		"Unknown",
		"MC_SECURITY_ERR",
		"MC_ARBITRATION_EMEM",
		"MC_SMMU_ERR",
	};
	const char * const err_type[] = {
		"Unknown",
		"Unknown",
		"DECERR_EMEM",
		"SECURITY_TRUSTZONE",
		"SECURITY_CARVEOUT",
		"Unknown",
		"INVALID_SMMU_PAGE",
		"Unknown",
	};
	char attr[6];
	int cid, perm, type, idx;
	const char *client = "Unknown";

	idx = n - MC_INT_ERR_SHIFT;
	if ((idx < 0) || (idx >= ARRAY_SIZE(mc_int_err)) || (idx == 1)) {
		dev_err_ratelimited(mc->dev, "Unknown interrupt status %08lx\n",
				    BIT(n));
		return;
	}

	err = mc_readl(mc, MC_ERR_STATUS);

	type = (err & MC_ERR_TYPE_MASK) >> MC_ERR_TYPE_SHIFT;
	perm = (err & MC_ERR_INVALID_SMMU_PAGE_MASK) >>
		MC_ERR_INVALID_SMMU_PAGE_SHIFT;
	if (type == MC_ERR_TYPE_INVALID_SMMU_PAGE)
		sprintf(attr, "%c-%c-%c",
			(perm & BIT(2)) ? 'R' : '-',
			(perm & BIT(1)) ? 'W' : '-',
			(perm & BIT(0)) ? 'S' : '-');
	else
		attr[0] = '\0';

	cid = err & MC_CLIENT_ID_MASK;
	if (cid < ARRAY_SIZE(tegra30_mc_client))
		client = tegra30_mc_client[cid];

	addr = mc_readl(mc, MC_ERR_ADR);

	dev_err_ratelimited(mc->dev, "%s (0x%08x): 0x%08x %s (%s %s %s %s)\n",
			   mc_int_err[idx], err, addr, client,
			   (err & MC_ERR_SECURITY) ? "secure" : "non-secure",
			   (err & MC_ERR_RW) ? "write" : "read",
			   err_type[type], attr);
}

static const u32 tegra30_mc_ctx[] = {
	MC_EMEM_ARB_CFG,
	MC_EMEM_ARB_OUTSTANDING_REQ,
	MC_EMEM_ARB_TIMING_RCD,
	MC_EMEM_ARB_TIMING_RP,
	MC_EMEM_ARB_TIMING_RC,
	MC_EMEM_ARB_TIMING_RAS,
	MC_EMEM_ARB_TIMING_FAW,
	MC_EMEM_ARB_TIMING_RRD,
	MC_EMEM_ARB_TIMING_RAP2PRE,
	MC_EMEM_ARB_TIMING_WAP2PRE,
	MC_EMEM_ARB_TIMING_R2R,
	MC_EMEM_ARB_TIMING_W2W,
	MC_EMEM_ARB_TIMING_R2W,
	MC_EMEM_ARB_TIMING_W2R,
	MC_EMEM_ARB_DA_TURNS,
	MC_EMEM_ARB_DA_COVERS,
	MC_EMEM_ARB_MISC0,
	MC_EMEM_ARB_MISC1,
	MC_EMEM_ARB_RING3_THROTTLE,
	MC_EMEM_ARB_OVERRIDE,
	MC_INTMASK,
};

#ifdef CONFIG_PM
static int tegra30_mc_suspend(struct device *dev)
{
	int i;
	struct tegra30_mc *mc = dev_get_drvdata(dev);

	for (i = 0; i < ARRAY_SIZE(tegra30_mc_ctx); i++)
		mc->ctx[i] = mc_readl(mc, tegra30_mc_ctx[i]);
	return 0;
}

static int tegra30_mc_resume(struct device *dev)
{
	int i;
	struct tegra30_mc *mc = dev_get_drvdata(dev);

	for (i = 0; i < ARRAY_SIZE(tegra30_mc_ctx); i++)
		mc_writel(mc, mc->ctx[i], tegra30_mc_ctx[i]);

	mc_writel(mc, 1, MC_TIMING_CONTROL);
	/* Read-back to ensure that write reached */
	mc_readl(mc, MC_TIMING_CONTROL);
	return 0;
}
#endif

static UNIVERSAL_DEV_PM_OPS(tegra30_mc_pm,
			    tegra30_mc_suspend,
			    tegra30_mc_resume, NULL);

static const struct of_device_id tegra30_mc_of_match[] = {
	{ .compatible = "nvidia,tegra30-mc", },
	{},
};

static irqreturn_t tegra30_mc_isr(int irq, void *data)
{
	u32 stat, mask, bit;
	struct tegra30_mc *mc = data;

	stat = mc_readl(mc, MC_INTSTATUS);
	mask = mc_readl(mc, MC_INTMASK);
	mask &= stat;
	if (!mask)
		return IRQ_NONE;
	while ((bit = ffs(mask)) != 0) {
		tegra30_mc_decode(mc, bit - 1);
		mask &= ~BIT(bit - 1);
	}

	mc_writel(mc, stat, MC_INTSTATUS);
	return IRQ_HANDLED;
}

static int tegra30_mc_probe(struct platform_device *pdev)
{
	struct resource *irq;
	struct tegra30_mc *mc;
	size_t bytes;
	int err, i;
	u32 intmask;

	bytes = sizeof(*mc) + sizeof(u32) * ARRAY_SIZE(tegra30_mc_ctx);
	mc = devm_kzalloc(&pdev->dev, bytes, GFP_KERNEL);
	if (!mc)
		return -ENOMEM;
	mc->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(mc->regs); i++) {
		struct resource *res;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			return -ENODEV;
		mc->regs[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(mc->regs[i]))
			return PTR_ERR(mc->regs[i]);
	}

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq)
		return -ENODEV;
	err = devm_request_irq(&pdev->dev, irq->start, tegra30_mc_isr,
			       IRQF_SHARED, dev_name(&pdev->dev), mc);
	if (err)
		return -ENODEV;

	platform_set_drvdata(pdev, mc);

	intmask = MC_INT_INVALID_SMMU_PAGE |
		MC_INT_DECERR_EMEM | MC_INT_SECURITY_VIOLATION;
	mc_writel(mc, intmask, MC_INTMASK);
	return 0;
}

static struct platform_driver tegra30_mc_driver = {
	.probe = tegra30_mc_probe,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tegra30_mc_of_match,
		.pm = &tegra30_mc_pm,
	},
};
module_platform_driver(tegra30_mc_driver);

MODULE_AUTHOR("Hiroshi DOYU <hdoyu@nvidia.com>");
MODULE_DESCRIPTION("Tegra30 MC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
