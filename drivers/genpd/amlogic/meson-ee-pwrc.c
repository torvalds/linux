// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <dt-bindings/power/meson8-power.h>
#include <dt-bindings/power/meson-axg-power.h>
#include <dt-bindings/power/meson-g12a-power.h>
#include <dt-bindings/power/meson-gxbb-power.h>
#include <dt-bindings/power/meson-sm1-power.h>

/* AO Offsets */

#define GX_AO_RTI_GEN_PWR_SLEEP0	(0x3a << 2)
#define GX_AO_RTI_GEN_PWR_ISO0		(0x3b << 2)

/*
 * Meson8/Meson8b/Meson8m2 only expose the power management registers of the
 * AO-bus as syscon. 0x3a from GX translates to 0x02, 0x3b translates to 0x03
 * and so on.
 */
#define MESON8_AO_RTI_GEN_PWR_SLEEP0	(0x02 << 2)
#define MESON8_AO_RTI_GEN_PWR_ISO0	(0x03 << 2)

/* HHI Offsets */

#define HHI_MEM_PD_REG0			(0x40 << 2)
#define HHI_VPU_MEM_PD_REG0		(0x41 << 2)
#define HHI_VPU_MEM_PD_REG1		(0x42 << 2)
#define HHI_VPU_MEM_PD_REG3		(0x43 << 2)
#define HHI_VPU_MEM_PD_REG4		(0x44 << 2)
#define HHI_AUDIO_MEM_PD_REG0		(0x45 << 2)
#define HHI_NANOQ_MEM_PD_REG0		(0x46 << 2)
#define HHI_NANOQ_MEM_PD_REG1		(0x47 << 2)
#define HHI_VPU_MEM_PD_REG2		(0x4d << 2)

#define G12A_HHI_NANOQ_MEM_PD_REG0	(0x43 << 2)
#define G12A_HHI_NANOQ_MEM_PD_REG1	(0x44 << 2)

struct meson_ee_pwrc;
struct meson_ee_pwrc_domain;

struct meson_ee_pwrc_mem_domain {
	unsigned int reg;
	unsigned int mask;
};

struct meson_ee_pwrc_top_domain {
	unsigned int sleep_reg;
	unsigned int sleep_mask;
	unsigned int iso_reg;
	unsigned int iso_mask;
};

struct meson_ee_pwrc_domain_desc {
	char *name;
	unsigned int reset_names_count;
	unsigned int clk_names_count;
	struct meson_ee_pwrc_top_domain *top_pd;
	unsigned int mem_pd_count;
	struct meson_ee_pwrc_mem_domain *mem_pd;
	bool (*is_powered_off)(struct meson_ee_pwrc_domain *pwrc_domain);
};

struct meson_ee_pwrc_domain_data {
	unsigned int count;
	struct meson_ee_pwrc_domain_desc *domains;
};

/* TOP Power Domains */

static struct meson_ee_pwrc_top_domain gx_pwrc_vpu = {
	.sleep_reg = GX_AO_RTI_GEN_PWR_SLEEP0,
	.sleep_mask = BIT(8),
	.iso_reg = GX_AO_RTI_GEN_PWR_SLEEP0,
	.iso_mask = BIT(9),
};

static struct meson_ee_pwrc_top_domain meson8_pwrc_vpu = {
	.sleep_reg = MESON8_AO_RTI_GEN_PWR_SLEEP0,
	.sleep_mask = BIT(8),
	.iso_reg = MESON8_AO_RTI_GEN_PWR_SLEEP0,
	.iso_mask = BIT(9),
};

#define SM1_EE_PD(__bit)					\
	{							\
		.sleep_reg = GX_AO_RTI_GEN_PWR_SLEEP0, 		\
		.sleep_mask = BIT(__bit), 			\
		.iso_reg = GX_AO_RTI_GEN_PWR_ISO0, 		\
		.iso_mask = BIT(__bit), 			\
	}

static struct meson_ee_pwrc_top_domain sm1_pwrc_vpu = SM1_EE_PD(8);
static struct meson_ee_pwrc_top_domain sm1_pwrc_nna = SM1_EE_PD(16);
static struct meson_ee_pwrc_top_domain sm1_pwrc_usb = SM1_EE_PD(17);
static struct meson_ee_pwrc_top_domain sm1_pwrc_pci = SM1_EE_PD(18);
static struct meson_ee_pwrc_top_domain sm1_pwrc_ge2d = SM1_EE_PD(19);

static struct meson_ee_pwrc_top_domain g12a_pwrc_nna = {
	.sleep_reg = GX_AO_RTI_GEN_PWR_SLEEP0,
	.sleep_mask = BIT(16) | BIT(17),
	.iso_reg = GX_AO_RTI_GEN_PWR_ISO0,
	.iso_mask = BIT(16) | BIT(17),
};

/* Memory PD Domains */

#define VPU_MEMPD(__reg)					\
	{ __reg, GENMASK(1, 0) },				\
	{ __reg, GENMASK(3, 2) },				\
	{ __reg, GENMASK(5, 4) },				\
	{ __reg, GENMASK(7, 6) },				\
	{ __reg, GENMASK(9, 8) },				\
	{ __reg, GENMASK(11, 10) },				\
	{ __reg, GENMASK(13, 12) },				\
	{ __reg, GENMASK(15, 14) },				\
	{ __reg, GENMASK(17, 16) },				\
	{ __reg, GENMASK(19, 18) },				\
	{ __reg, GENMASK(21, 20) },				\
	{ __reg, GENMASK(23, 22) },				\
	{ __reg, GENMASK(25, 24) },				\
	{ __reg, GENMASK(27, 26) },				\
	{ __reg, GENMASK(29, 28) },				\
	{ __reg, GENMASK(31, 30) }

#define VPU_HHI_MEMPD(__reg)					\
	{ __reg, BIT(8) },					\
	{ __reg, BIT(9) },					\
	{ __reg, BIT(10) },					\
	{ __reg, BIT(11) },					\
	{ __reg, BIT(12) },					\
	{ __reg, BIT(13) },					\
	{ __reg, BIT(14) },					\
	{ __reg, BIT(15) }

static struct meson_ee_pwrc_mem_domain axg_pwrc_mem_vpu[] = {
	VPU_MEMPD(HHI_VPU_MEM_PD_REG0),
	VPU_HHI_MEMPD(HHI_MEM_PD_REG0),
};

static struct meson_ee_pwrc_mem_domain g12a_pwrc_mem_vpu[] = {
	VPU_MEMPD(HHI_VPU_MEM_PD_REG0),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG1),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG2),
	VPU_HHI_MEMPD(HHI_MEM_PD_REG0),
};

static struct meson_ee_pwrc_mem_domain gxbb_pwrc_mem_vpu[] = {
	VPU_MEMPD(HHI_VPU_MEM_PD_REG0),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG1),
	VPU_HHI_MEMPD(HHI_MEM_PD_REG0),
};

static struct meson_ee_pwrc_mem_domain meson_pwrc_mem_eth[] = {
	{ HHI_MEM_PD_REG0, GENMASK(3, 2) },
};

static struct meson_ee_pwrc_mem_domain meson8_pwrc_audio_dsp_mem[] = {
	{ HHI_MEM_PD_REG0, GENMASK(1, 0) },
};

static struct meson_ee_pwrc_mem_domain meson8_pwrc_mem_vpu[] = {
	VPU_MEMPD(HHI_VPU_MEM_PD_REG0),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG1),
	VPU_HHI_MEMPD(HHI_MEM_PD_REG0),
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_vpu[] = {
	VPU_MEMPD(HHI_VPU_MEM_PD_REG0),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG1),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG2),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG3),
	{ HHI_VPU_MEM_PD_REG4, GENMASK(1, 0) },
	{ HHI_VPU_MEM_PD_REG4, GENMASK(3, 2) },
	{ HHI_VPU_MEM_PD_REG4, GENMASK(5, 4) },
	{ HHI_VPU_MEM_PD_REG4, GENMASK(7, 6) },
	VPU_HHI_MEMPD(HHI_MEM_PD_REG0),
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_nna[] = {
	{ HHI_NANOQ_MEM_PD_REG0, 0xff },
	{ HHI_NANOQ_MEM_PD_REG1, 0xff },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_usb[] = {
	{ HHI_MEM_PD_REG0, GENMASK(31, 30) },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_pcie[] = {
	{ HHI_MEM_PD_REG0, GENMASK(29, 26) },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_ge2d[] = {
	{ HHI_MEM_PD_REG0, GENMASK(25, 18) },
};

static struct meson_ee_pwrc_mem_domain axg_pwrc_mem_audio[] = {
	{ HHI_MEM_PD_REG0, GENMASK(5, 4) },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_audio[] = {
	{ HHI_MEM_PD_REG0, GENMASK(5, 4) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(1, 0) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(3, 2) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(5, 4) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(7, 6) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(13, 12) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(15, 14) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(17, 16) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(19, 18) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(21, 20) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(23, 22) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(25, 24) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(27, 26) },
};

static struct meson_ee_pwrc_mem_domain g12a_pwrc_mem_nna[] = {
	{ G12A_HHI_NANOQ_MEM_PD_REG0, GENMASK(31, 0) },
	{ G12A_HHI_NANOQ_MEM_PD_REG1, GENMASK(23, 0) },
};

#define VPU_PD(__name, __top_pd, __mem, __is_pwr_off, __resets, __clks)	\
	{								\
		.name = __name,						\
		.reset_names_count = __resets,				\
		.clk_names_count = __clks,				\
		.top_pd = __top_pd,					\
		.mem_pd_count = ARRAY_SIZE(__mem),			\
		.mem_pd = __mem,					\
		.is_powered_off = __is_pwr_off,				\
	}

#define TOP_PD(__name, __top_pd, __mem, __is_pwr_off)			\
	{								\
		.name = __name,						\
		.top_pd = __top_pd,					\
		.mem_pd_count = ARRAY_SIZE(__mem),			\
		.mem_pd = __mem,					\
		.is_powered_off = __is_pwr_off,				\
	}

#define MEM_PD(__name, __mem)						\
	TOP_PD(__name, NULL, __mem, NULL)

static bool pwrc_ee_is_powered_off(struct meson_ee_pwrc_domain *pwrc_domain);

static struct meson_ee_pwrc_domain_desc axg_pwrc_domains[] = {
	[PWRC_AXG_VPU_ID]  = VPU_PD("VPU", &gx_pwrc_vpu, axg_pwrc_mem_vpu,
				     pwrc_ee_is_powered_off, 5, 2),
	[PWRC_AXG_ETHERNET_MEM_ID] = MEM_PD("ETH", meson_pwrc_mem_eth),
	[PWRC_AXG_AUDIO_ID] = MEM_PD("AUDIO", axg_pwrc_mem_audio),
};

static struct meson_ee_pwrc_domain_desc g12a_pwrc_domains[] = {
	[PWRC_G12A_VPU_ID]  = VPU_PD("VPU", &gx_pwrc_vpu, g12a_pwrc_mem_vpu,
				     pwrc_ee_is_powered_off, 11, 2),
	[PWRC_G12A_ETH_ID] = MEM_PD("ETH", meson_pwrc_mem_eth),
	[PWRC_G12A_NNA_ID] = TOP_PD("NNA", &g12a_pwrc_nna, g12a_pwrc_mem_nna,
				    pwrc_ee_is_powered_off),
};

static struct meson_ee_pwrc_domain_desc gxbb_pwrc_domains[] = {
	[PWRC_GXBB_VPU_ID]  = VPU_PD("VPU", &gx_pwrc_vpu, gxbb_pwrc_mem_vpu,
				     pwrc_ee_is_powered_off, 12, 2),
	[PWRC_GXBB_ETHERNET_MEM_ID] = MEM_PD("ETH", meson_pwrc_mem_eth),
};

static struct meson_ee_pwrc_domain_desc meson8_pwrc_domains[] = {
	[PWRC_MESON8_VPU_ID]  = VPU_PD("VPU", &meson8_pwrc_vpu,
				       meson8_pwrc_mem_vpu,
				       pwrc_ee_is_powered_off, 0, 1),
	[PWRC_MESON8_ETHERNET_MEM_ID] = MEM_PD("ETHERNET_MEM",
					       meson_pwrc_mem_eth),
	[PWRC_MESON8_AUDIO_DSP_MEM_ID] = MEM_PD("AUDIO_DSP_MEM",
						meson8_pwrc_audio_dsp_mem),
};

static struct meson_ee_pwrc_domain_desc meson8b_pwrc_domains[] = {
	[PWRC_MESON8_VPU_ID]  = VPU_PD("VPU", &meson8_pwrc_vpu,
				       meson8_pwrc_mem_vpu,
				       pwrc_ee_is_powered_off, 11, 1),
	[PWRC_MESON8_ETHERNET_MEM_ID] = MEM_PD("ETHERNET_MEM",
					       meson_pwrc_mem_eth),
	[PWRC_MESON8_AUDIO_DSP_MEM_ID] = MEM_PD("AUDIO_DSP_MEM",
						meson8_pwrc_audio_dsp_mem),
};

static struct meson_ee_pwrc_domain_desc sm1_pwrc_domains[] = {
	[PWRC_SM1_VPU_ID]  = VPU_PD("VPU", &sm1_pwrc_vpu, sm1_pwrc_mem_vpu,
				    pwrc_ee_is_powered_off, 11, 2),
	[PWRC_SM1_NNA_ID]  = TOP_PD("NNA", &sm1_pwrc_nna, sm1_pwrc_mem_nna,
				    pwrc_ee_is_powered_off),
	[PWRC_SM1_USB_ID]  = TOP_PD("USB", &sm1_pwrc_usb, sm1_pwrc_mem_usb,
				    pwrc_ee_is_powered_off),
	[PWRC_SM1_PCIE_ID] = TOP_PD("PCI", &sm1_pwrc_pci, sm1_pwrc_mem_pcie,
				    pwrc_ee_is_powered_off),
	[PWRC_SM1_GE2D_ID] = TOP_PD("GE2D", &sm1_pwrc_ge2d, sm1_pwrc_mem_ge2d,
				    pwrc_ee_is_powered_off),
	[PWRC_SM1_AUDIO_ID] = MEM_PD("AUDIO", sm1_pwrc_mem_audio),
	[PWRC_SM1_ETH_ID] = MEM_PD("ETH", meson_pwrc_mem_eth),
};

struct meson_ee_pwrc_domain {
	struct generic_pm_domain base;
	bool enabled;
	struct meson_ee_pwrc *pwrc;
	struct meson_ee_pwrc_domain_desc desc;
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *rstc;
	int num_rstc;
};

struct meson_ee_pwrc {
	struct regmap *regmap_ao;
	struct regmap *regmap_hhi;
	struct meson_ee_pwrc_domain *domains;
	struct genpd_onecell_data xlate;
};

static bool pwrc_ee_is_powered_off(struct meson_ee_pwrc_domain *pwrc_domain)
{
	u32 reg;

	regmap_read(pwrc_domain->pwrc->regmap_ao,
		    pwrc_domain->desc.top_pd->sleep_reg, &reg);

	return (reg & pwrc_domain->desc.top_pd->sleep_mask);
}

static int meson_ee_pwrc_off(struct generic_pm_domain *domain)
{
	struct meson_ee_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_ee_pwrc_domain, base);
	int i;

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->sleep_reg,
				   pwrc_domain->desc.top_pd->sleep_mask,
				   pwrc_domain->desc.top_pd->sleep_mask);
	udelay(20);

	for (i = 0 ; i < pwrc_domain->desc.mem_pd_count ; ++i)
		regmap_update_bits(pwrc_domain->pwrc->regmap_hhi,
				   pwrc_domain->desc.mem_pd[i].reg,
				   pwrc_domain->desc.mem_pd[i].mask,
				   pwrc_domain->desc.mem_pd[i].mask);

	udelay(20);

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->iso_reg,
				   pwrc_domain->desc.top_pd->iso_mask,
				   pwrc_domain->desc.top_pd->iso_mask);

	if (pwrc_domain->num_clks) {
		msleep(20);
		clk_bulk_disable_unprepare(pwrc_domain->num_clks,
					   pwrc_domain->clks);
	}

	return 0;
}

static int meson_ee_pwrc_on(struct generic_pm_domain *domain)
{
	struct meson_ee_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_ee_pwrc_domain, base);
	int i, ret;

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->sleep_reg,
				   pwrc_domain->desc.top_pd->sleep_mask, 0);
	udelay(20);

	for (i = 0 ; i < pwrc_domain->desc.mem_pd_count ; ++i)
		regmap_update_bits(pwrc_domain->pwrc->regmap_hhi,
				   pwrc_domain->desc.mem_pd[i].reg,
				   pwrc_domain->desc.mem_pd[i].mask, 0);

	udelay(20);

	ret = reset_control_assert(pwrc_domain->rstc);
	if (ret)
		return ret;

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->iso_reg,
				   pwrc_domain->desc.top_pd->iso_mask, 0);

	ret = reset_control_deassert(pwrc_domain->rstc);
	if (ret)
		return ret;

	return clk_bulk_prepare_enable(pwrc_domain->num_clks,
				       pwrc_domain->clks);
}

static int meson_ee_pwrc_init_domain(struct platform_device *pdev,
				     struct meson_ee_pwrc *pwrc,
				     struct meson_ee_pwrc_domain *dom)
{
	int ret;

	dom->pwrc = pwrc;
	dom->num_rstc = dom->desc.reset_names_count;
	dom->num_clks = dom->desc.clk_names_count;

	if (dom->num_rstc) {
		int count = reset_control_get_count(&pdev->dev);

		if (count != dom->num_rstc)
			dev_warn(&pdev->dev, "Invalid resets count %d for domain %s\n",
				 count, dom->desc.name);

		dom->rstc = devm_reset_control_array_get_exclusive(&pdev->dev);
		if (IS_ERR(dom->rstc))
			return PTR_ERR(dom->rstc);
	}

	if (dom->num_clks) {
		int ret = devm_clk_bulk_get_all(&pdev->dev, &dom->clks);
		if (ret < 0)
			return ret;

		if (dom->num_clks != ret) {
			dev_warn(&pdev->dev, "Invalid clocks count %d for domain %s\n",
				 ret, dom->desc.name);
			dom->num_clks = ret;
		}
	}

	dom->base.name = dom->desc.name;
	dom->base.power_on = meson_ee_pwrc_on;
	dom->base.power_off = meson_ee_pwrc_off;

	/*
         * TOFIX: This is a special case for the VPU power domain, which can
	 * be enabled previously by the bootloader. In this case the VPU
         * pipeline may be functional but no driver maybe never attach
         * to this power domain, and if the domain is disabled it could
         * cause system errors. This is why the pm_domain_always_on_gov
         * is used here.
         * For the same reason, the clocks should be enabled in case
         * we need to power the domain off, otherwise the internal clocks
         * prepare/enable counters won't be in sync.
         */
	if (dom->num_clks && dom->desc.is_powered_off && !dom->desc.is_powered_off(dom)) {
		ret = clk_bulk_prepare_enable(dom->num_clks, dom->clks);
		if (ret)
			return ret;

		dom->base.flags = GENPD_FLAG_ALWAYS_ON;
		ret = pm_genpd_init(&dom->base, NULL, false);
		if (ret)
			return ret;
	} else {
		ret = pm_genpd_init(&dom->base, NULL,
				    (dom->desc.is_powered_off ?
				     dom->desc.is_powered_off(dom) : true));
		if (ret)
			return ret;
	}

	return 0;
}

static int meson_ee_pwrc_probe(struct platform_device *pdev)
{
	const struct meson_ee_pwrc_domain_data *match;
	struct regmap *regmap_ao, *regmap_hhi;
	struct device_node *parent_np;
	struct meson_ee_pwrc *pwrc;
	int i, ret;

	match = of_device_get_match_data(&pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "failed to get match data\n");
		return -ENODEV;
	}

	pwrc = devm_kzalloc(&pdev->dev, sizeof(*pwrc), GFP_KERNEL);
	if (!pwrc)
		return -ENOMEM;

	pwrc->xlate.domains = devm_kcalloc(&pdev->dev, match->count,
					   sizeof(*pwrc->xlate.domains),
					   GFP_KERNEL);
	if (!pwrc->xlate.domains)
		return -ENOMEM;

	pwrc->domains = devm_kcalloc(&pdev->dev, match->count,
				     sizeof(*pwrc->domains), GFP_KERNEL);
	if (!pwrc->domains)
		return -ENOMEM;

	pwrc->xlate.num_domains = match->count;

	parent_np = of_get_parent(pdev->dev.of_node);
	regmap_hhi = syscon_node_to_regmap(parent_np);
	of_node_put(parent_np);
	if (IS_ERR(regmap_hhi)) {
		dev_err(&pdev->dev, "failed to get HHI regmap\n");
		return PTR_ERR(regmap_hhi);
	}

	regmap_ao = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						    "amlogic,ao-sysctrl");
	if (IS_ERR(regmap_ao)) {
		dev_err(&pdev->dev, "failed to get AO regmap\n");
		return PTR_ERR(regmap_ao);
	}

	pwrc->regmap_ao = regmap_ao;
	pwrc->regmap_hhi = regmap_hhi;

	platform_set_drvdata(pdev, pwrc);

	for (i = 0 ; i < match->count ; ++i) {
		struct meson_ee_pwrc_domain *dom = &pwrc->domains[i];

		memcpy(&dom->desc, &match->domains[i], sizeof(dom->desc));

		ret = meson_ee_pwrc_init_domain(pdev, pwrc, dom);
		if (ret)
			return ret;

		pwrc->xlate.domains[i] = &dom->base;
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node, &pwrc->xlate);
}

static void meson_ee_pwrc_shutdown(struct platform_device *pdev)
{
	struct meson_ee_pwrc *pwrc = platform_get_drvdata(pdev);
	int i;

	for (i = 0 ; i < pwrc->xlate.num_domains ; ++i) {
		struct meson_ee_pwrc_domain *dom = &pwrc->domains[i];

		if (dom->desc.is_powered_off && !dom->desc.is_powered_off(dom))
			meson_ee_pwrc_off(&dom->base);
	}
}

static struct meson_ee_pwrc_domain_data meson_ee_g12a_pwrc_data = {
	.count = ARRAY_SIZE(g12a_pwrc_domains),
	.domains = g12a_pwrc_domains,
};

static struct meson_ee_pwrc_domain_data meson_ee_axg_pwrc_data = {
	.count = ARRAY_SIZE(axg_pwrc_domains),
	.domains = axg_pwrc_domains,
};

static struct meson_ee_pwrc_domain_data meson_ee_gxbb_pwrc_data = {
	.count = ARRAY_SIZE(gxbb_pwrc_domains),
	.domains = gxbb_pwrc_domains,
};

static struct meson_ee_pwrc_domain_data meson_ee_m8_pwrc_data = {
	.count = ARRAY_SIZE(meson8_pwrc_domains),
	.domains = meson8_pwrc_domains,
};

static struct meson_ee_pwrc_domain_data meson_ee_m8b_pwrc_data = {
	.count = ARRAY_SIZE(meson8b_pwrc_domains),
	.domains = meson8b_pwrc_domains,
};

static struct meson_ee_pwrc_domain_data meson_ee_sm1_pwrc_data = {
	.count = ARRAY_SIZE(sm1_pwrc_domains),
	.domains = sm1_pwrc_domains,
};

static const struct of_device_id meson_ee_pwrc_match_table[] = {
	{
		.compatible = "amlogic,meson8-pwrc",
		.data = &meson_ee_m8_pwrc_data,
	},
	{
		.compatible = "amlogic,meson8b-pwrc",
		.data = &meson_ee_m8b_pwrc_data,
	},
	{
		.compatible = "amlogic,meson8m2-pwrc",
		.data = &meson_ee_m8b_pwrc_data,
	},
	{
		.compatible = "amlogic,meson-axg-pwrc",
		.data = &meson_ee_axg_pwrc_data,
	},
	{
		.compatible = "amlogic,meson-gxbb-pwrc",
		.data = &meson_ee_gxbb_pwrc_data,
	},
	{
		.compatible = "amlogic,meson-g12a-pwrc",
		.data = &meson_ee_g12a_pwrc_data,
	},
	{
		.compatible = "amlogic,meson-sm1-pwrc",
		.data = &meson_ee_sm1_pwrc_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_ee_pwrc_match_table);

static struct platform_driver meson_ee_pwrc_driver = {
	.probe = meson_ee_pwrc_probe,
	.shutdown = meson_ee_pwrc_shutdown,
	.driver = {
		.name		= "meson_ee_pwrc",
		.of_match_table	= meson_ee_pwrc_match_table,
	},
};
module_platform_driver(meson_ee_pwrc_driver);
MODULE_LICENSE("GPL v2");
