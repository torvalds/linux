/*
 * Tegra20 Memory Controller
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define DRV_NAME "tegra20-mc"

#define MC_INTSTATUS			0x0
#define MC_INTMASK			0x4

#define MC_INT_ERR_SHIFT		6
#define MC_INT_ERR_MASK			(0x1f << MC_INT_ERR_SHIFT)
#define MC_INT_DECERR_EMEM		BIT(MC_INT_ERR_SHIFT)
#define MC_INT_INVALID_GART_PAGE	BIT(MC_INT_ERR_SHIFT + 1)
#define MC_INT_SECURITY_VIOLATION	BIT(MC_INT_ERR_SHIFT + 2)
#define MC_INT_ARBITRATION_EMEM		BIT(MC_INT_ERR_SHIFT + 3)

#define MC_GART_ERROR_REQ		0x30
#define MC_DECERR_EMEM_OTHERS_STATUS	0x58
#define MC_SECURITY_VIOLATION_STATUS	0x74

#define SECURITY_VIOLATION_TYPE		BIT(30)	/* 0=TRUSTZONE, 1=CARVEOUT */

#define MC_CLIENT_ID_MASK		0x3f

#define NUM_MC_REG_BANKS		2

struct tegra20_mc {
	void __iomem *regs[NUM_MC_REG_BANKS];
	struct device *dev;
};

static inline u32 mc_readl(struct tegra20_mc *mc, u32 offs)
{
	u32 val = 0;

	if (offs < 0x24)
		val = readl(mc->regs[0] + offs);
	else if (offs < 0x400)
		val = readl(mc->regs[1] + offs - 0x3c);

	return val;
}

static inline void mc_writel(struct tegra20_mc *mc, u32 val, u32 offs)
{
	if (offs < 0x24)
		writel(val, mc->regs[0] + offs);
	else if (offs < 0x400)
		writel(val, mc->regs[1] + offs - 0x3c);
}

static const char * const tegra20_mc_client[] = {
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
	"csr_avpcarm7r",
	"csr_displayhc",
	"csr_displayhcb",
	"csr_fdcdrd",
	"csr_g2dr",
	"csr_host1xdmar",
	"csr_host1xr",
	"csr_idxsrd",
	"csr_mpcorer",
	"csr_mpe_ipred",
	"csr_mpeamemrd",
	"csr_mpecsrd",
	"csr_ppcsahbdmar",
	"csr_ppcsahbslvr",
	"csr_texsrd",
	"csr_vdebsevr",
	"csr_vdember",
	"csr_vdemcer",
	"csr_vdetper",
	"cbw_eppu",
	"cbw_eppv",
	"cbw_eppy",
	"cbw_mpeunifbw",
	"cbw_viwsb",
	"cbw_viwu",
	"cbw_viwv",
	"cbw_viwy",
	"ccw_g2dw",
	"csw_avpcarm7w",
	"csw_fdcdwr",
	"csw_host1xw",
	"csw_ispw",
	"csw_mpcorew",
	"csw_mpecswr",
	"csw_ppcsahbdmaw",
	"csw_ppcsahbslvw",
	"csw_vdebsevw",
	"csw_vdembew",
	"csw_vdetpmw",
};

static void tegra20_mc_decode(struct tegra20_mc *mc, int n)
{
	u32 addr, req;
	const char *client = "Unknown";
	int idx, cid;
	const struct reg_info {
		u32 offset;
		u32 write_bit;	/* 0=READ, 1=WRITE */
		int cid_shift;
		char *message;
	} reg[] = {
		{
			.offset = MC_DECERR_EMEM_OTHERS_STATUS,
			.write_bit = 31,
			.message = "MC_DECERR",
		},
		{
			.offset	= MC_GART_ERROR_REQ,
			.cid_shift = 1,
			.message = "MC_GART_ERR",

		},
		{
			.offset = MC_SECURITY_VIOLATION_STATUS,
			.write_bit = 31,
			.message = "MC_SECURITY_ERR",
		},
	};

	idx = n - MC_INT_ERR_SHIFT;
	if ((idx < 0) || (idx >= ARRAY_SIZE(reg))) {
		dev_err_ratelimited(mc->dev, "Unknown interrupt status %08lx\n",
				    BIT(n));
		return;
	}

	req = mc_readl(mc, reg[idx].offset);
	cid = (req >> reg[idx].cid_shift) & MC_CLIENT_ID_MASK;
	if (cid < ARRAY_SIZE(tegra20_mc_client))
		client = tegra20_mc_client[cid];

	addr = mc_readl(mc, reg[idx].offset + sizeof(u32));

	dev_err_ratelimited(mc->dev, "%s (0x%08x): 0x%08x %s (%s %s)\n",
			   reg[idx].message, req, addr, client,
			   (req & BIT(reg[idx].write_bit)) ? "write" : "read",
			   (reg[idx].offset == MC_SECURITY_VIOLATION_STATUS) ?
			   ((req & SECURITY_VIOLATION_TYPE) ?
			    "carveout" : "trustzone") : "");
}

static const struct of_device_id tegra20_mc_of_match[] __devinitconst = {
	{ .compatible = "nvidia,tegra20-mc", },
	{},
};

static irqreturn_t tegra20_mc_isr(int irq, void *data)
{
	u32 stat, mask, bit;
	struct tegra20_mc *mc = data;

	stat = mc_readl(mc, MC_INTSTATUS);
	mask = mc_readl(mc, MC_INTMASK);
	mask &= stat;
	if (!mask)
		return IRQ_NONE;
	while ((bit = ffs(mask)) != 0)
		tegra20_mc_decode(mc, bit - 1);
	mc_writel(mc, stat, MC_INTSTATUS);
	return IRQ_HANDLED;
}

static int __devinit tegra20_mc_probe(struct platform_device *pdev)
{
	struct resource *irq;
	struct tegra20_mc *mc;
	int i, err;
	u32 intmask;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;
	mc->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(mc->regs); i++) {
		struct resource *res;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			return -ENODEV;
		mc->regs[i] = devm_request_and_ioremap(&pdev->dev, res);
		if (!mc->regs[i])
			return -EBUSY;
	}

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq)
		return -ENODEV;
	err = devm_request_irq(&pdev->dev, irq->start, tegra20_mc_isr,
			       IRQF_SHARED, dev_name(&pdev->dev), mc);
	if (err)
		return -ENODEV;

	platform_set_drvdata(pdev, mc);

	intmask = MC_INT_INVALID_GART_PAGE |
		MC_INT_DECERR_EMEM | MC_INT_SECURITY_VIOLATION;
	mc_writel(mc, intmask, MC_INTMASK);
	return 0;
}

static struct platform_driver tegra20_mc_driver = {
	.probe = tegra20_mc_probe,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tegra20_mc_of_match,
	},
};
module_platform_driver(tegra20_mc_driver);

MODULE_AUTHOR("Hiroshi DOYU <hdoyu@nvidia.com>");
MODULE_DESCRIPTION("Tegra20 MC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
