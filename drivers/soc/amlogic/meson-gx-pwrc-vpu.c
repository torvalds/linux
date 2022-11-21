/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/module.h>

/* AO Offsets */

#define AO_RTI_GEN_PWR_SLEEP0		(0x3a << 2)

#define GEN_PWR_VPU_HDMI		BIT(8)
#define GEN_PWR_VPU_HDMI_ISO		BIT(9)

/* HHI Offsets */

#define HHI_MEM_PD_REG0			(0x40 << 2)
#define HHI_VPU_MEM_PD_REG0		(0x41 << 2)
#define HHI_VPU_MEM_PD_REG1		(0x42 << 2)
#define HHI_VPU_MEM_PD_REG2		(0x4d << 2)

struct meson_gx_pwrc_vpu {
	struct generic_pm_domain genpd;
	struct regmap *regmap_ao;
	struct regmap *regmap_hhi;
	struct reset_control *rstc;
	struct clk *vpu_clk;
	struct clk *vapb_clk;
};

static inline
struct meson_gx_pwrc_vpu *genpd_to_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct meson_gx_pwrc_vpu, genpd);
}

static int meson_gx_pwrc_vpu_power_off(struct generic_pm_domain *genpd)
{
	struct meson_gx_pwrc_vpu *pd = genpd_to_pd(genpd);
	int i;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI_ISO, GEN_PWR_VPU_HDMI_ISO);
	udelay(20);

	/* Power Down Memories */
	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG0,
				   0x3 << i, 0x3 << i);
		udelay(5);
	}
	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG1,
				   0x3 << i, 0x3 << i);
		udelay(5);
	}
	for (i = 8; i < 16; i++) {
		regmap_update_bits(pd->regmap_hhi, HHI_MEM_PD_REG0,
				   BIT(i), BIT(i));
		udelay(5);
	}
	udelay(20);

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI, GEN_PWR_VPU_HDMI);

	msleep(20);

	clk_disable_unprepare(pd->vpu_clk);
	clk_disable_unprepare(pd->vapb_clk);

	return 0;
}

static int meson_g12a_pwrc_vpu_power_off(struct generic_pm_domain *genpd)
{
	struct meson_gx_pwrc_vpu *pd = genpd_to_pd(genpd);
	int i;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI_ISO, GEN_PWR_VPU_HDMI_ISO);
	udelay(20);

	/* Power Down Memories */
	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG0,
				   0x3 << i, 0x3 << i);
		udelay(5);
	}
	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG1,
				   0x3 << i, 0x3 << i);
		udelay(5);
	}
	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG2,
				   0x3 << i, 0x3 << i);
		udelay(5);
	}
	for (i = 8; i < 16; i++) {
		regmap_update_bits(pd->regmap_hhi, HHI_MEM_PD_REG0,
				   BIT(i), BIT(i));
		udelay(5);
	}
	udelay(20);

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI, GEN_PWR_VPU_HDMI);

	msleep(20);

	clk_disable_unprepare(pd->vpu_clk);
	clk_disable_unprepare(pd->vapb_clk);

	return 0;
}

static int meson_gx_pwrc_vpu_setup_clk(struct meson_gx_pwrc_vpu *pd)
{
	int ret;

	ret = clk_prepare_enable(pd->vpu_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(pd->vapb_clk);
	if (ret)
		clk_disable_unprepare(pd->vpu_clk);

	return ret;
}

static int meson_gx_pwrc_vpu_power_on(struct generic_pm_domain *genpd)
{
	struct meson_gx_pwrc_vpu *pd = genpd_to_pd(genpd);
	int ret;
	int i;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI, 0);
	udelay(20);

	/* Power Up Memories */
	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG0,
				   0x3 << i, 0);
		udelay(5);
	}

	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG1,
				   0x3 << i, 0);
		udelay(5);
	}

	for (i = 8; i < 16; i++) {
		regmap_update_bits(pd->regmap_hhi, HHI_MEM_PD_REG0,
				   BIT(i), 0);
		udelay(5);
	}
	udelay(20);

	ret = reset_control_assert(pd->rstc);
	if (ret)
		return ret;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI_ISO, 0);

	ret = reset_control_deassert(pd->rstc);
	if (ret)
		return ret;

	ret = meson_gx_pwrc_vpu_setup_clk(pd);
	if (ret)
		return ret;

	return 0;
}

static int meson_g12a_pwrc_vpu_power_on(struct generic_pm_domain *genpd)
{
	struct meson_gx_pwrc_vpu *pd = genpd_to_pd(genpd);
	int ret;
	int i;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI, 0);
	udelay(20);

	/* Power Up Memories */
	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG0,
				   0x3 << i, 0);
		udelay(5);
	}

	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG1,
				   0x3 << i, 0);
		udelay(5);
	}

	for (i = 0; i < 32; i += 2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG2,
				   0x3 << i, 0);
		udelay(5);
	}

	for (i = 8; i < 16; i++) {
		regmap_update_bits(pd->regmap_hhi, HHI_MEM_PD_REG0,
				   BIT(i), 0);
		udelay(5);
	}
	udelay(20);

	ret = reset_control_assert(pd->rstc);
	if (ret)
		return ret;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
			   GEN_PWR_VPU_HDMI_ISO, 0);

	ret = reset_control_deassert(pd->rstc);
	if (ret)
		return ret;

	ret = meson_gx_pwrc_vpu_setup_clk(pd);
	if (ret)
		return ret;

	return 0;
}

static bool meson_gx_pwrc_vpu_get_power(struct meson_gx_pwrc_vpu *pd)
{
	u32 reg;

	regmap_read(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0, &reg);

	return (reg & GEN_PWR_VPU_HDMI);
}

static struct meson_gx_pwrc_vpu vpu_hdmi_pd = {
	.genpd = {
		.name = "vpu_hdmi",
		.power_off = meson_gx_pwrc_vpu_power_off,
		.power_on = meson_gx_pwrc_vpu_power_on,
	},
};

static struct meson_gx_pwrc_vpu vpu_hdmi_pd_g12a = {
	.genpd = {
		.name = "vpu_hdmi",
		.power_off = meson_g12a_pwrc_vpu_power_off,
		.power_on = meson_g12a_pwrc_vpu_power_on,
	},
};

static int meson_gx_pwrc_vpu_probe(struct platform_device *pdev)
{
	const struct meson_gx_pwrc_vpu *vpu_pd_match;
	struct regmap *regmap_ao, *regmap_hhi;
	struct meson_gx_pwrc_vpu *vpu_pd;
	struct reset_control *rstc;
	struct clk *vpu_clk;
	struct clk *vapb_clk;
	bool powered_off;
	int ret;

	vpu_pd_match = of_device_get_match_data(&pdev->dev);
	if (!vpu_pd_match) {
		dev_err(&pdev->dev, "failed to get match data\n");
		return -ENODEV;
	}

	vpu_pd = devm_kzalloc(&pdev->dev, sizeof(*vpu_pd), GFP_KERNEL);
	if (!vpu_pd)
		return -ENOMEM;

	memcpy(vpu_pd, vpu_pd_match, sizeof(*vpu_pd));

	regmap_ao = syscon_node_to_regmap(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(regmap_ao)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return PTR_ERR(regmap_ao);
	}

	regmap_hhi = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "amlogic,hhi-sysctrl");
	if (IS_ERR(regmap_hhi)) {
		dev_err(&pdev->dev, "failed to get HHI regmap\n");
		return PTR_ERR(regmap_hhi);
	}

	rstc = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(rstc)) {
		if (PTR_ERR(rstc) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get reset lines\n");
		return PTR_ERR(rstc);
	}

	vpu_clk = devm_clk_get(&pdev->dev, "vpu");
	if (IS_ERR(vpu_clk)) {
		dev_err(&pdev->dev, "vpu clock request failed\n");
		return PTR_ERR(vpu_clk);
	}

	vapb_clk = devm_clk_get(&pdev->dev, "vapb");
	if (IS_ERR(vapb_clk)) {
		dev_err(&pdev->dev, "vapb clock request failed\n");
		return PTR_ERR(vapb_clk);
	}

	vpu_pd->regmap_ao = regmap_ao;
	vpu_pd->regmap_hhi = regmap_hhi;
	vpu_pd->rstc = rstc;
	vpu_pd->vpu_clk = vpu_clk;
	vpu_pd->vapb_clk = vapb_clk;

	platform_set_drvdata(pdev, vpu_pd);

	powered_off = meson_gx_pwrc_vpu_get_power(vpu_pd);

	/* If already powered, sync the clock states */
	if (!powered_off) {
		ret = meson_gx_pwrc_vpu_setup_clk(vpu_pd);
		if (ret)
			return ret;
	}

	vpu_pd->genpd.flags = GENPD_FLAG_ALWAYS_ON;
	pm_genpd_init(&vpu_pd->genpd, NULL, powered_off);

	return of_genpd_add_provider_simple(pdev->dev.of_node,
					    &vpu_pd->genpd);
}

static void meson_gx_pwrc_vpu_shutdown(struct platform_device *pdev)
{
	struct meson_gx_pwrc_vpu *vpu_pd = platform_get_drvdata(pdev);
	bool powered_off;

	powered_off = meson_gx_pwrc_vpu_get_power(vpu_pd);
	if (!powered_off)
		vpu_pd->genpd.power_off(&vpu_pd->genpd);
}

static const struct of_device_id meson_gx_pwrc_vpu_match_table[] = {
	{ .compatible = "amlogic,meson-gx-pwrc-vpu", .data = &vpu_hdmi_pd },
	{
	  .compatible = "amlogic,meson-g12a-pwrc-vpu",
	  .data = &vpu_hdmi_pd_g12a
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_gx_pwrc_vpu_match_table);

static struct platform_driver meson_gx_pwrc_vpu_driver = {
	.probe	= meson_gx_pwrc_vpu_probe,
	.shutdown = meson_gx_pwrc_vpu_shutdown,
	.driver = {
		.name		= "meson_gx_pwrc_vpu",
		.of_match_table	= meson_gx_pwrc_vpu_match_table,
	},
};
module_platform_driver(meson_gx_pwrc_vpu_driver);
MODULE_LICENSE("GPL v2");
