/*
 * Copyright (c) 2015 Pengutronix, Sascha Hauer <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/soc/mediatek/infracfg.h>
#include <dt-bindings/power/mt8173-power.h>

#define SPM_VDE_PWR_CON			0x0210
#define SPM_MFG_PWR_CON			0x0214
#define SPM_VEN_PWR_CON			0x0230
#define SPM_ISP_PWR_CON			0x0238
#define SPM_DIS_PWR_CON			0x023c
#define SPM_VEN2_PWR_CON		0x0298
#define SPM_AUDIO_PWR_CON		0x029c
#define SPM_MFG_2D_PWR_CON		0x02c0
#define SPM_MFG_ASYNC_PWR_CON		0x02c4
#define SPM_USB_PWR_CON			0x02cc
#define SPM_PWR_STATUS			0x060c
#define SPM_PWR_STATUS_2ND		0x0610

#define PWR_RST_B_BIT			BIT(0)
#define PWR_ISO_BIT			BIT(1)
#define PWR_ON_BIT			BIT(2)
#define PWR_ON_2ND_BIT			BIT(3)
#define PWR_CLK_DIS_BIT			BIT(4)

#define PWR_STATUS_DISP			BIT(3)
#define PWR_STATUS_MFG			BIT(4)
#define PWR_STATUS_ISP			BIT(5)
#define PWR_STATUS_VDEC			BIT(7)
#define PWR_STATUS_VENC_LT		BIT(20)
#define PWR_STATUS_VENC			BIT(21)
#define PWR_STATUS_MFG_2D		BIT(22)
#define PWR_STATUS_MFG_ASYNC		BIT(23)
#define PWR_STATUS_AUDIO		BIT(24)
#define PWR_STATUS_USB			BIT(25)

enum clk_id {
	MT8173_CLK_MM,
	MT8173_CLK_MFG,
	MT8173_CLK_NONE,
	MT8173_CLK_MAX = MT8173_CLK_NONE,
};

struct scp_domain_data {
	const char *name;
	u32 sta_mask;
	int ctl_offs;
	u32 sram_pdn_bits;
	u32 sram_pdn_ack_bits;
	u32 bus_prot_mask;
	enum clk_id clk_id;
};

static const struct scp_domain_data scp_domain_data[] __initconst = {
	[MT8173_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = PWR_STATUS_VDEC,
		.ctl_offs = SPM_VDE_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.clk_id = MT8173_CLK_MM,
	},
	[MT8173_POWER_DOMAIN_VENC] = {
		.name = "venc",
		.sta_mask = PWR_STATUS_VENC,
		.ctl_offs = SPM_VEN_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.clk_id = MT8173_CLK_MM,
	},
	[MT8173_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.clk_id = MT8173_CLK_MM,
	},
	[MT8173_POWER_DOMAIN_MM] = {
		.name = "mm",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = SPM_DIS_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.clk_id = MT8173_CLK_MM,
		.bus_prot_mask = MT8173_TOP_AXI_PROT_EN_MM_M0 |
			MT8173_TOP_AXI_PROT_EN_MM_M1,
	},
	[MT8173_POWER_DOMAIN_VENC_LT] = {
		.name = "venc_lt",
		.sta_mask = PWR_STATUS_VENC_LT,
		.ctl_offs = SPM_VEN2_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.clk_id = MT8173_CLK_MM,
	},
	[MT8173_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = PWR_STATUS_AUDIO,
		.ctl_offs = SPM_AUDIO_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.clk_id = MT8173_CLK_NONE,
	},
	[MT8173_POWER_DOMAIN_USB] = {
		.name = "usb",
		.sta_mask = PWR_STATUS_USB,
		.ctl_offs = SPM_USB_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.clk_id = MT8173_CLK_NONE,
	},
	[MT8173_POWER_DOMAIN_MFG_ASYNC] = {
		.name = "mfg_async",
		.sta_mask = PWR_STATUS_MFG_ASYNC,
		.ctl_offs = SPM_MFG_ASYNC_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = 0,
		.clk_id = MT8173_CLK_MFG,
	},
	[MT8173_POWER_DOMAIN_MFG_2D] = {
		.name = "mfg_2d",
		.sta_mask = PWR_STATUS_MFG_2D,
		.ctl_offs = SPM_MFG_2D_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.clk_id = MT8173_CLK_NONE,
	},
	[MT8173_POWER_DOMAIN_MFG] = {
		.name = "mfg",
		.sta_mask = PWR_STATUS_MFG,
		.ctl_offs = SPM_MFG_PWR_CON,
		.sram_pdn_bits = GENMASK(13, 8),
		.sram_pdn_ack_bits = GENMASK(21, 16),
		.clk_id = MT8173_CLK_NONE,
		.bus_prot_mask = MT8173_TOP_AXI_PROT_EN_MFG_S |
			MT8173_TOP_AXI_PROT_EN_MFG_M0 |
			MT8173_TOP_AXI_PROT_EN_MFG_M1 |
			MT8173_TOP_AXI_PROT_EN_MFG_SNOOP_OUT,
	},
};

#define NUM_DOMAINS	ARRAY_SIZE(scp_domain_data)

struct scp;

struct scp_domain {
	struct generic_pm_domain genpd;
	struct scp *scp;
	struct clk *clk;
	u32 sta_mask;
	void __iomem *ctl_addr;
	u32 sram_pdn_bits;
	u32 sram_pdn_ack_bits;
	u32 bus_prot_mask;
};

struct scp {
	struct scp_domain domains[NUM_DOMAINS];
	struct genpd_onecell_data pd_data;
	struct device *dev;
	void __iomem *base;
	struct regmap *infracfg;
};

static int scpsys_domain_is_on(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;

	u32 status = readl(scp->base + SPM_PWR_STATUS) & scpd->sta_mask;
	u32 status2 = readl(scp->base + SPM_PWR_STATUS_2ND) & scpd->sta_mask;

	/*
	 * A domain is on when both status bits are set. If only one is set
	 * return an error. This happens while powering up a domain
	 */

	if (status && status2)
		return true;
	if (!status && !status2)
		return false;

	return -EINVAL;
}

static int scpsys_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	unsigned long timeout;
	bool expired;
	void __iomem *ctl_addr = scpd->ctl_addr;
	u32 sram_pdn_ack = scpd->sram_pdn_ack_bits;
	u32 val;
	int ret;

	if (scpd->clk) {
		ret = clk_prepare_enable(scpd->clk);
		if (ret)
			goto err_clk;
	}

	val = readl(ctl_addr);
	val |= PWR_ON_BIT;
	writel(val, ctl_addr);
	val |= PWR_ON_2ND_BIT;
	writel(val, ctl_addr);

	/* wait until PWR_ACK = 1 */
	timeout = jiffies + HZ;
	expired = false;
	while (1) {
		ret = scpsys_domain_is_on(scpd);
		if (ret > 0)
			break;

		if (expired) {
			ret = -ETIMEDOUT;
			goto err_pwr_ack;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	val &= ~PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ISO_BIT;
	writel(val, ctl_addr);

	val |= PWR_RST_B_BIT;
	writel(val, ctl_addr);

	val &= ~scpd->sram_pdn_bits;
	writel(val, ctl_addr);

	/* wait until SRAM_PDN_ACK all 0 */
	timeout = jiffies + HZ;
	expired = false;
	while (sram_pdn_ack && (readl(ctl_addr) & sram_pdn_ack)) {

		if (expired) {
			ret = -ETIMEDOUT;
			goto err_pwr_ack;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	if (scpd->bus_prot_mask) {
		ret = mtk_infracfg_clear_bus_protection(scp->infracfg,
				scpd->bus_prot_mask);
		if (ret)
			goto err_pwr_ack;
	}

	return 0;

err_pwr_ack:
	clk_disable_unprepare(scpd->clk);
err_clk:
	dev_err(scp->dev, "Failed to power on domain %s\n", genpd->name);

	return ret;
}

static int scpsys_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	unsigned long timeout;
	bool expired;
	void __iomem *ctl_addr = scpd->ctl_addr;
	u32 pdn_ack = scpd->sram_pdn_ack_bits;
	u32 val;
	int ret;

	if (scpd->bus_prot_mask) {
		ret = mtk_infracfg_set_bus_protection(scp->infracfg,
				scpd->bus_prot_mask);
		if (ret)
			goto out;
	}

	val = readl(ctl_addr);
	val |= scpd->sram_pdn_bits;
	writel(val, ctl_addr);

	/* wait until SRAM_PDN_ACK all 1 */
	timeout = jiffies + HZ;
	expired = false;
	while (pdn_ack && (readl(ctl_addr) & pdn_ack) != pdn_ack) {
		if (expired) {
			ret = -ETIMEDOUT;
			goto out;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	val |= PWR_ISO_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_RST_B_BIT;
	writel(val, ctl_addr);

	val |= PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ON_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ON_2ND_BIT;
	writel(val, ctl_addr);

	/* wait until PWR_ACK = 0 */
	timeout = jiffies + HZ;
	expired = false;
	while (1) {
		ret = scpsys_domain_is_on(scpd);
		if (ret == 0)
			break;

		if (expired) {
			ret = -ETIMEDOUT;
			goto out;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	if (scpd->clk)
		clk_disable_unprepare(scpd->clk);

	return 0;

out:
	dev_err(scp->dev, "Failed to power off domain %s\n", genpd->name);

	return ret;
}

static int __init scpsys_probe(struct platform_device *pdev)
{
	struct genpd_onecell_data *pd_data;
	struct resource *res;
	int i, ret;
	struct scp *scp;
	struct clk *clk[MT8173_CLK_MAX];

	scp = devm_kzalloc(&pdev->dev, sizeof(*scp), GFP_KERNEL);
	if (!scp)
		return -ENOMEM;

	scp->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	scp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(scp->base))
		return PTR_ERR(scp->base);

	pd_data = &scp->pd_data;

	pd_data->domains = devm_kzalloc(&pdev->dev,
			sizeof(*pd_data->domains) * NUM_DOMAINS, GFP_KERNEL);
	if (!pd_data->domains)
		return -ENOMEM;

	clk[MT8173_CLK_MM] = devm_clk_get(&pdev->dev, "mm");
	if (IS_ERR(clk[MT8173_CLK_MM]))
		return PTR_ERR(clk[MT8173_CLK_MM]);

	clk[MT8173_CLK_MFG] = devm_clk_get(&pdev->dev, "mfg");
	if (IS_ERR(clk[MT8173_CLK_MFG]))
		return PTR_ERR(clk[MT8173_CLK_MFG]);

	scp->infracfg = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
			"infracfg");
	if (IS_ERR(scp->infracfg)) {
		dev_err(&pdev->dev, "Cannot find infracfg controller: %ld\n",
				PTR_ERR(scp->infracfg));
		return PTR_ERR(scp->infracfg);
	}

	pd_data->num_domains = NUM_DOMAINS;

	for (i = 0; i < NUM_DOMAINS; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		struct generic_pm_domain *genpd = &scpd->genpd;
		const struct scp_domain_data *data = &scp_domain_data[i];

		pd_data->domains[i] = genpd;
		scpd->scp = scp;

		scpd->sta_mask = data->sta_mask;
		scpd->ctl_addr = scp->base + data->ctl_offs;
		scpd->sram_pdn_bits = data->sram_pdn_bits;
		scpd->sram_pdn_ack_bits = data->sram_pdn_ack_bits;
		scpd->bus_prot_mask = data->bus_prot_mask;
		if (data->clk_id != MT8173_CLK_NONE)
			scpd->clk = clk[data->clk_id];

		genpd->name = data->name;
		genpd->power_off = scpsys_power_off;
		genpd->power_on = scpsys_power_on;

		/*
		 * Initially turn on all domains to make the domains usable
		 * with !CONFIG_PM and to get the hardware in sync with the
		 * software.  The unused domains will be switched off during
		 * late_init time.
		 */
		genpd->power_on(genpd);

		pm_genpd_init(genpd, NULL, false);
	}

	/*
	 * We are not allowed to fail here since there is no way to unregister
	 * a power domain. Once registered above we have to keep the domains
	 * valid.
	 */

	ret = pm_genpd_add_subdomain(pd_data->domains[MT8173_POWER_DOMAIN_MFG_ASYNC],
		pd_data->domains[MT8173_POWER_DOMAIN_MFG_2D]);
	if (ret && IS_ENABLED(CONFIG_PM))
		dev_err(&pdev->dev, "Failed to add subdomain: %d\n", ret);

	ret = pm_genpd_add_subdomain(pd_data->domains[MT8173_POWER_DOMAIN_MFG_2D],
		pd_data->domains[MT8173_POWER_DOMAIN_MFG]);
	if (ret && IS_ENABLED(CONFIG_PM))
		dev_err(&pdev->dev, "Failed to add subdomain: %d\n", ret);

	ret = of_genpd_add_provider_onecell(pdev->dev.of_node, pd_data);
	if (ret)
		dev_err(&pdev->dev, "Failed to add OF provider: %d\n", ret);

	return 0;
}

static const struct of_device_id of_scpsys_match_tbl[] = {
	{
		.compatible = "mediatek,mt8173-scpsys",
	}, {
		/* sentinel */
	}
};

static struct platform_driver scpsys_drv = {
	.driver = {
		.name = "mtk-scpsys",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_scpsys_match_tbl),
	},
};

module_platform_driver_probe(scpsys_drv, scpsys_probe);
