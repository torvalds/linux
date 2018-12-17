// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2017 Impinj, Inc
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * Based on the code of analogus driver:
 *
 * Copyright 2015-2017 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <dt-bindings/power/imx7-power.h>
#include <dt-bindings/power/imx8mq-power.h>

#define GPC_LPCR_A_CORE_BSC			0x000

#define GPC_PGC_CPU_MAPPING		0x0ec

#define IMX7_USB_HSIC_PHY_A_CORE_DOMAIN		BIT(6)
#define IMX7_USB_OTG2_PHY_A_CORE_DOMAIN		BIT(5)
#define IMX7_USB_OTG1_PHY_A_CORE_DOMAIN		BIT(4)
#define IMX7_PCIE_PHY_A_CORE_DOMAIN		BIT(3)
#define IMX7_MIPI_PHY_A_CORE_DOMAIN		BIT(2)

#define IMX8M_PCIE2_A53_DOMAIN			BIT(15)
#define IMX8M_MIPI_CSI2_A53_DOMAIN		BIT(14)
#define IMX8M_MIPI_CSI1_A53_DOMAIN		BIT(13)
#define IMX8M_DISP_A53_DOMAIN			BIT(12)
#define IMX8M_HDMI_A53_DOMAIN			BIT(11)
#define IMX8M_VPU_A53_DOMAIN			BIT(10)
#define IMX8M_GPU_A53_DOMAIN			BIT(9)
#define IMX8M_DDR2_A53_DOMAIN			BIT(8)
#define IMX8M_DDR1_A53_DOMAIN			BIT(7)
#define IMX8M_OTG2_A53_DOMAIN			BIT(5)
#define IMX8M_OTG1_A53_DOMAIN			BIT(4)
#define IMX8M_PCIE1_A53_DOMAIN			BIT(3)
#define IMX8M_MIPI_A53_DOMAIN			BIT(2)

#define GPC_PU_PGC_SW_PUP_REQ		0x0f8
#define GPC_PU_PGC_SW_PDN_REQ		0x104

#define IMX7_USB_HSIC_PHY_SW_Pxx_REQ		BIT(4)
#define IMX7_USB_OTG2_PHY_SW_Pxx_REQ		BIT(3)
#define IMX7_USB_OTG1_PHY_SW_Pxx_REQ		BIT(2)
#define IMX7_PCIE_PHY_SW_Pxx_REQ		BIT(1)
#define IMX7_MIPI_PHY_SW_Pxx_REQ		BIT(0)

#define IMX8M_PCIE2_SW_Pxx_REQ			BIT(13)
#define IMX8M_MIPI_CSI2_SW_Pxx_REQ		BIT(12)
#define IMX8M_MIPI_CSI1_SW_Pxx_REQ		BIT(11)
#define IMX8M_DISP_SW_Pxx_REQ			BIT(10)
#define IMX8M_HDMI_SW_Pxx_REQ			BIT(9)
#define IMX8M_VPU_SW_Pxx_REQ			BIT(8)
#define IMX8M_GPU_SW_Pxx_REQ			BIT(7)
#define IMX8M_DDR2_SW_Pxx_REQ			BIT(6)
#define IMX8M_DDR1_SW_Pxx_REQ			BIT(5)
#define IMX8M_OTG2_SW_Pxx_REQ			BIT(3)
#define IMX8M_OTG1_SW_Pxx_REQ			BIT(2)
#define IMX8M_PCIE1_SW_Pxx_REQ			BIT(1)
#define IMX8M_MIPI_SW_Pxx_REQ			BIT(0)

#define GPC_M4_PU_PDN_FLG		0x1bc

#define GPC_PU_PWRHSK			0x1fc

#define IMX8M_GPU_HSK_PWRDNREQN			BIT(6)
#define IMX8M_VPU_HSK_PWRDNREQN			BIT(5)
#define IMX8M_DISP_HSK_PWRDNREQN		BIT(4)

/*
 * The PGC offset values in Reference Manual
 * (Rev. 1, 01/2018 and the older ones) GPC chapter's
 * GPC_PGC memory map are incorrect, below offset
 * values are from design RTL.
 */
#define IMX7_PGC_MIPI			16
#define IMX7_PGC_PCIE			17
#define IMX7_PGC_USB_HSIC		20

#define IMX8M_PGC_MIPI			16
#define IMX8M_PGC_PCIE1			17
#define IMX8M_PGC_OTG1			18
#define IMX8M_PGC_OTG2			19
#define IMX8M_PGC_DDR1			21
#define IMX8M_PGC_GPU			23
#define IMX8M_PGC_VPU			24
#define IMX8M_PGC_DISP			26
#define IMX8M_PGC_MIPI_CSI1		27
#define IMX8M_PGC_MIPI_CSI2		28
#define IMX8M_PGC_PCIE2			29

#define GPC_PGC_CTRL(n)			(0x800 + (n) * 0x40)
#define GPC_PGC_SR(n)			(GPC_PGC_CTRL(n) + 0xc)

#define GPC_PGC_CTRL_PCR		BIT(0)

struct imx_pgc_domain {
	struct generic_pm_domain genpd;
	struct regmap *regmap;
	struct regulator *regulator;

	unsigned int pgc;

	const struct {
		u32 pxx;
		u32 map;
		u32 hsk;
	} bits;

	const int voltage;
	struct device *dev;
};

struct imx_pgc_domain_data {
	const struct imx_pgc_domain *domains;
	size_t domains_num;
	const struct regmap_access_table *reg_access_table;
};

static int imx_gpc_pu_pgc_sw_pxx_req(struct generic_pm_domain *genpd,
				      bool on)
{
	struct imx_pgc_domain *domain = container_of(genpd,
						      struct imx_pgc_domain,
						      genpd);
	unsigned int offset = on ?
		GPC_PU_PGC_SW_PUP_REQ : GPC_PU_PGC_SW_PDN_REQ;
	const bool enable_power_control = !on;
	const bool has_regulator = !IS_ERR(domain->regulator);
	unsigned long deadline;
	int ret = 0;

	regmap_update_bits(domain->regmap, GPC_PGC_CPU_MAPPING,
			   domain->bits.map, domain->bits.map);

	if (has_regulator && on) {
		ret = regulator_enable(domain->regulator);
		if (ret) {
			dev_err(domain->dev, "failed to enable regulator\n");
			goto unmap;
		}
	}

	if (enable_power_control)
		regmap_update_bits(domain->regmap, GPC_PGC_CTRL(domain->pgc),
				   GPC_PGC_CTRL_PCR, GPC_PGC_CTRL_PCR);

	if (domain->bits.hsk)
		regmap_update_bits(domain->regmap, GPC_PU_PWRHSK,
				   domain->bits.hsk, on ? domain->bits.hsk : 0);

	regmap_update_bits(domain->regmap, offset,
			   domain->bits.pxx, domain->bits.pxx);

	/*
	 * As per "5.5.9.4 Example Code 4" in IMX7DRM.pdf wait
	 * for PUP_REQ/PDN_REQ bit to be cleared
	 */
	deadline = jiffies + msecs_to_jiffies(1);
	while (true) {
		u32 pxx_req;

		regmap_read(domain->regmap, offset, &pxx_req);

		if (!(pxx_req & domain->bits.pxx))
			break;

		if (time_after(jiffies, deadline)) {
			dev_err(domain->dev, "falied to command PGC\n");
			ret = -ETIMEDOUT;
			/*
			 * If we were in a process of enabling a
			 * domain and failed we might as well disable
			 * the regulator we just enabled. And if it
			 * was the opposite situation and we failed to
			 * power down -- keep the regulator on
			 */
			on = !on;
			break;
		}

		cpu_relax();
	}

	if (enable_power_control)
		regmap_update_bits(domain->regmap, GPC_PGC_CTRL(domain->pgc),
				   GPC_PGC_CTRL_PCR, 0);

	if (has_regulator && !on) {
		int err;

		err = regulator_disable(domain->regulator);
		if (err)
			dev_err(domain->dev,
				"failed to disable regulator: %d\n", ret);
		/* Preserve earlier error code */
		ret = ret ?: err;
	}
unmap:
	regmap_update_bits(domain->regmap, GPC_PGC_CPU_MAPPING,
			   domain->bits.map, 0);
	return ret;
}

static int imx_gpc_pu_pgc_sw_pup_req(struct generic_pm_domain *genpd)
{
	return imx_gpc_pu_pgc_sw_pxx_req(genpd, true);
}

static int imx_gpc_pu_pgc_sw_pdn_req(struct generic_pm_domain *genpd)
{
	return imx_gpc_pu_pgc_sw_pxx_req(genpd, false);
}

static const struct imx_pgc_domain imx7_pgc_domains[] = {
	[IMX7_POWER_DOMAIN_MIPI_PHY] = {
		.genpd = {
			.name      = "mipi-phy",
		},
		.bits  = {
			.pxx = IMX7_MIPI_PHY_SW_Pxx_REQ,
			.map = IMX7_MIPI_PHY_A_CORE_DOMAIN,
		},
		.voltage   = 1000000,
		.pgc	   = IMX7_PGC_MIPI,
	},

	[IMX7_POWER_DOMAIN_PCIE_PHY] = {
		.genpd = {
			.name      = "pcie-phy",
		},
		.bits  = {
			.pxx = IMX7_PCIE_PHY_SW_Pxx_REQ,
			.map = IMX7_PCIE_PHY_A_CORE_DOMAIN,
		},
		.voltage   = 1000000,
		.pgc	   = IMX7_PGC_PCIE,
	},

	[IMX7_POWER_DOMAIN_USB_HSIC_PHY] = {
		.genpd = {
			.name      = "usb-hsic-phy",
		},
		.bits  = {
			.pxx = IMX7_USB_HSIC_PHY_SW_Pxx_REQ,
			.map = IMX7_USB_HSIC_PHY_A_CORE_DOMAIN,
		},
		.voltage   = 1200000,
		.pgc	   = IMX7_PGC_USB_HSIC,
	},
};

static const struct regmap_range imx7_yes_ranges[] = {
		regmap_reg_range(GPC_LPCR_A_CORE_BSC,
				 GPC_M4_PU_PDN_FLG),
		regmap_reg_range(GPC_PGC_CTRL(IMX7_PGC_MIPI),
				 GPC_PGC_SR(IMX7_PGC_MIPI)),
		regmap_reg_range(GPC_PGC_CTRL(IMX7_PGC_PCIE),
				 GPC_PGC_SR(IMX7_PGC_PCIE)),
		regmap_reg_range(GPC_PGC_CTRL(IMX7_PGC_USB_HSIC),
				 GPC_PGC_SR(IMX7_PGC_USB_HSIC)),
};

static const struct regmap_access_table imx7_access_table = {
	.yes_ranges	= imx7_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(imx7_yes_ranges),
};

static const struct imx_pgc_domain_data imx7_pgc_domain_data = {
	.domains = imx7_pgc_domains,
	.domains_num = ARRAY_SIZE(imx7_pgc_domains),
	.reg_access_table = &imx7_access_table,
};

static const struct imx_pgc_domain imx8m_pgc_domains[] = {
	[IMX8M_POWER_DOMAIN_MIPI] = {
		.genpd = {
			.name      = "mipi",
		},
		.bits  = {
			.pxx = IMX8M_MIPI_SW_Pxx_REQ,
			.map = IMX8M_MIPI_A53_DOMAIN,
		},
		.pgc	   = IMX8M_PGC_MIPI,
	},

	[IMX8M_POWER_DOMAIN_PCIE1] = {
		.genpd = {
			.name = "pcie1",
		},
		.bits  = {
			.pxx = IMX8M_PCIE1_SW_Pxx_REQ,
			.map = IMX8M_PCIE1_A53_DOMAIN,
		},
		.pgc   = IMX8M_PGC_PCIE1,
	},

	[IMX8M_POWER_DOMAIN_USB_OTG1] = {
		.genpd = {
			.name = "usb-otg1",
		},
		.bits  = {
			.pxx = IMX8M_OTG1_SW_Pxx_REQ,
			.map = IMX8M_OTG1_A53_DOMAIN,
		},
		.pgc   = IMX8M_PGC_OTG1,
	},

	[IMX8M_POWER_DOMAIN_USB_OTG2] = {
		.genpd = {
			.name = "usb-otg2",
		},
		.bits  = {
			.pxx = IMX8M_OTG2_SW_Pxx_REQ,
			.map = IMX8M_OTG2_A53_DOMAIN,
		},
		.pgc   = IMX8M_PGC_OTG2,
	},

	[IMX8M_POWER_DOMAIN_DDR1] = {
		.genpd = {
			.name = "ddr1",
		},
		.bits  = {
			.pxx = IMX8M_DDR1_SW_Pxx_REQ,
			.map = IMX8M_DDR2_A53_DOMAIN,
		},
		.pgc   = IMX8M_PGC_DDR1,
	},

	[IMX8M_POWER_DOMAIN_GPU] = {
		.genpd = {
			.name = "gpu",
		},
		.bits  = {
			.pxx = IMX8M_GPU_SW_Pxx_REQ,
			.map = IMX8M_GPU_A53_DOMAIN,
			.hsk = IMX8M_GPU_HSK_PWRDNREQN,
		},
		.pgc   = IMX8M_PGC_GPU,
	},

	[IMX8M_POWER_DOMAIN_VPU] = {
		.genpd = {
			.name = "vpu",
		},
		.bits  = {
			.pxx = IMX8M_VPU_SW_Pxx_REQ,
			.map = IMX8M_VPU_A53_DOMAIN,
			.hsk = IMX8M_VPU_HSK_PWRDNREQN,
		},
		.pgc   = IMX8M_PGC_VPU,
	},

	[IMX8M_POWER_DOMAIN_DISP] = {
		.genpd = {
			.name = "disp",
		},
		.bits  = {
			.pxx = IMX8M_DISP_SW_Pxx_REQ,
			.map = IMX8M_DISP_A53_DOMAIN,
			.hsk = IMX8M_DISP_HSK_PWRDNREQN,
		},
		.pgc   = IMX8M_PGC_DISP,
	},

	[IMX8M_POWER_DOMAIN_MIPI_CSI1] = {
		.genpd = {
			.name = "mipi-csi1",
		},
		.bits  = {
			.pxx = IMX8M_MIPI_CSI1_SW_Pxx_REQ,
			.map = IMX8M_MIPI_CSI1_A53_DOMAIN,
		},
		.pgc   = IMX8M_PGC_MIPI_CSI1,
	},

	[IMX8M_POWER_DOMAIN_MIPI_CSI2] = {
		.genpd = {
			.name = "mipi-csi2",
		},
		.bits  = {
			.pxx = IMX8M_MIPI_CSI2_SW_Pxx_REQ,
			.map = IMX8M_MIPI_CSI2_A53_DOMAIN,
		},
		.pgc   = IMX8M_PGC_MIPI_CSI2,
	},

	[IMX8M_POWER_DOMAIN_PCIE2] = {
		.genpd = {
			.name = "pcie2",
		},
		.bits  = {
			.pxx = IMX8M_PCIE2_SW_Pxx_REQ,
			.map = IMX8M_PCIE2_A53_DOMAIN,
		},
		.pgc   = IMX8M_PGC_PCIE2,
	},
};

static const struct regmap_range imx8m_yes_ranges[] = {
		regmap_reg_range(GPC_LPCR_A_CORE_BSC,
				 GPC_PU_PWRHSK),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_MIPI),
				 GPC_PGC_SR(IMX8M_PGC_MIPI)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_PCIE1),
				 GPC_PGC_SR(IMX8M_PGC_PCIE1)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_OTG1),
				 GPC_PGC_SR(IMX8M_PGC_OTG1)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_OTG2),
				 GPC_PGC_SR(IMX8M_PGC_OTG2)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_DDR1),
				 GPC_PGC_SR(IMX8M_PGC_DDR1)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_GPU),
				 GPC_PGC_SR(IMX8M_PGC_GPU)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_VPU),
				 GPC_PGC_SR(IMX8M_PGC_VPU)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_DISP),
				 GPC_PGC_SR(IMX8M_PGC_DISP)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_MIPI_CSI1),
				 GPC_PGC_SR(IMX8M_PGC_MIPI_CSI1)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_MIPI_CSI2),
				 GPC_PGC_SR(IMX8M_PGC_MIPI_CSI2)),
		regmap_reg_range(GPC_PGC_CTRL(IMX8M_PGC_PCIE2),
				 GPC_PGC_SR(IMX8M_PGC_PCIE2)),
};

static const struct regmap_access_table imx8m_access_table = {
	.yes_ranges	= imx8m_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(imx8m_yes_ranges),
};

static const struct imx_pgc_domain_data imx8m_pgc_domain_data = {
	.domains = imx8m_pgc_domains,
	.domains_num = ARRAY_SIZE(imx8m_pgc_domains),
	.reg_access_table = &imx8m_access_table,
};

static int imx_pgc_domain_probe(struct platform_device *pdev)
{
	struct imx_pgc_domain *domain = pdev->dev.platform_data;
	int ret;

	domain->dev = &pdev->dev;

	domain->regulator = devm_regulator_get_optional(domain->dev, "power");
	if (IS_ERR(domain->regulator)) {
		if (PTR_ERR(domain->regulator) != -ENODEV) {
			if (PTR_ERR(domain->regulator) != -EPROBE_DEFER)
				dev_err(domain->dev, "Failed to get domain's regulator\n");
			return PTR_ERR(domain->regulator);
		}
	} else if (domain->voltage) {
		regulator_set_voltage(domain->regulator,
				      domain->voltage, domain->voltage);
	}

	ret = pm_genpd_init(&domain->genpd, NULL, true);
	if (ret) {
		dev_err(domain->dev, "Failed to init power domain\n");
		return ret;
	}

	ret = of_genpd_add_provider_simple(domain->dev->of_node,
					   &domain->genpd);
	if (ret) {
		dev_err(domain->dev, "Failed to add genpd provider\n");
		pm_genpd_remove(&domain->genpd);
	}

	return ret;
}

static int imx_pgc_domain_remove(struct platform_device *pdev)
{
	struct imx_pgc_domain *domain = pdev->dev.platform_data;

	of_genpd_del_provider(domain->dev->of_node);
	pm_genpd_remove(&domain->genpd);

	return 0;
}

static const struct platform_device_id imx_pgc_domain_id[] = {
	{ "imx-pgc-domain", },
	{ },
};

static struct platform_driver imx_pgc_domain_driver = {
	.driver = {
		.name = "imx-pgc",
	},
	.probe    = imx_pgc_domain_probe,
	.remove   = imx_pgc_domain_remove,
	.id_table = imx_pgc_domain_id,
};
builtin_platform_driver(imx_pgc_domain_driver)

static int imx_gpcv2_probe(struct platform_device *pdev)
{
	const struct imx_pgc_domain_data *domain_data =
			of_device_get_match_data(&pdev->dev);

	struct regmap_config regmap_config = {
		.reg_bits	= 32,
		.val_bits	= 32,
		.reg_stride	= 4,
		.rd_table	= domain_data->reg_access_table,
		.wr_table	= domain_data->reg_access_table,
		.max_register   = SZ_4K,
	};
	struct device *dev = &pdev->dev;
	struct device_node *pgc_np, *np;
	struct regmap *regmap;
	struct resource *res;
	void __iomem *base;
	int ret;

	pgc_np = of_get_child_by_name(dev->of_node, "pgc");
	if (!pgc_np) {
		dev_err(dev, "No power domains specified in DT\n");
		return -EINVAL;
	}

	res  = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "failed to init regmap (%d)\n", ret);
		return ret;
	}

	for_each_child_of_node(pgc_np, np) {
		struct platform_device *pd_pdev;
		struct imx_pgc_domain *domain;
		u32 domain_index;

		ret = of_property_read_u32(np, "reg", &domain_index);
		if (ret) {
			dev_err(dev, "Failed to read 'reg' property\n");
			of_node_put(np);
			return ret;
		}

		if (domain_index >= domain_data->domains_num) {
			dev_warn(dev,
				 "Domain index %d is out of bounds\n",
				 domain_index);
			continue;
		}

		pd_pdev = platform_device_alloc("imx-pgc-domain",
						domain_index);
		if (!pd_pdev) {
			dev_err(dev, "Failed to allocate platform device\n");
			of_node_put(np);
			return -ENOMEM;
		}

		ret = platform_device_add_data(pd_pdev,
					       &domain_data->domains[domain_index],
					       sizeof(domain_data->domains[domain_index]));
		if (ret) {
			platform_device_put(pd_pdev);
			of_node_put(np);
			return ret;
		}

		domain = pd_pdev->dev.platform_data;
		domain->regmap = regmap;
		domain->genpd.power_on  = imx_gpc_pu_pgc_sw_pup_req;
		domain->genpd.power_off = imx_gpc_pu_pgc_sw_pdn_req;

		pd_pdev->dev.parent = dev;
		pd_pdev->dev.of_node = np;

		ret = platform_device_add(pd_pdev);
		if (ret) {
			platform_device_put(pd_pdev);
			of_node_put(np);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id imx_gpcv2_dt_ids[] = {
	{ .compatible = "fsl,imx7d-gpc", .data = &imx7_pgc_domain_data, },
	{ .compatible = "fsl,imx8mq-gpc", .data = &imx8m_pgc_domain_data, },
	{ }
};

static struct platform_driver imx_gpc_driver = {
	.driver = {
		.name = "imx-gpcv2",
		.of_match_table = imx_gpcv2_dt_ids,
	},
	.probe = imx_gpcv2_probe,
};
builtin_platform_driver(imx_gpc_driver)
