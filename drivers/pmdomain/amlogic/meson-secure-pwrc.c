// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Amlogic, Inc.
 * Author: Jianxin Pan <jianxin.pan@amlogic.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <dt-bindings/power/meson-a1-power.h>
#include <dt-bindings/power/amlogic,c3-pwrc.h>
#include <dt-bindings/power/meson-s4-power.h>
#include <dt-bindings/power/amlogic,t7-pwrc.h>
#include <dt-bindings/power/amlogic,a4-pwrc.h>
#include <dt-bindings/power/amlogic,a5-pwrc.h>
#include <dt-bindings/power/amlogic,s6-pwrc.h>
#include <dt-bindings/power/amlogic,s7-pwrc.h>
#include <dt-bindings/power/amlogic,s7d-pwrc.h>
#include <linux/arm-smccc.h>
#include <linux/firmware/meson/meson_sm.h>
#include <linux/module.h>

#define PWRC_ON		1
#define PWRC_OFF	0
#define PWRC_NO_PARENT	UINT_MAX

struct meson_secure_pwrc_domain {
	struct generic_pm_domain base;
	unsigned int index;
	unsigned int parent;
	struct meson_secure_pwrc *pwrc;
};

struct meson_secure_pwrc {
	struct meson_secure_pwrc_domain *domains;
	struct genpd_onecell_data xlate;
	struct meson_sm_firmware *fw;
};

struct meson_secure_pwrc_domain_desc {
	unsigned int index;
	unsigned int parent;
	unsigned int flags;
	char *name;
	bool (*is_off)(struct meson_secure_pwrc_domain *pwrc_domain);
};

struct meson_secure_pwrc_domain_data {
	unsigned int count;
	const struct meson_secure_pwrc_domain_desc *domains;
};

static bool pwrc_secure_is_off(struct meson_secure_pwrc_domain *pwrc_domain)
{
	int is_off = 1;

	if (meson_sm_call(pwrc_domain->pwrc->fw, SM_A1_PWRC_GET, &is_off,
			  pwrc_domain->index, 0, 0, 0, 0) < 0)
		pr_err("failed to get power domain status\n");

	return is_off;
}

static int meson_secure_pwrc_off(struct generic_pm_domain *domain)
{
	int ret = 0;
	struct meson_secure_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_secure_pwrc_domain, base);

	if (meson_sm_call(pwrc_domain->pwrc->fw, SM_A1_PWRC_SET, NULL,
			  pwrc_domain->index, PWRC_OFF, 0, 0, 0) < 0) {
		pr_err("failed to set power domain off\n");
		ret = -EINVAL;
	}

	return ret;
}

static int meson_secure_pwrc_on(struct generic_pm_domain *domain)
{
	int ret = 0;
	struct meson_secure_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_secure_pwrc_domain, base);

	if (meson_sm_call(pwrc_domain->pwrc->fw, SM_A1_PWRC_SET, NULL,
			  pwrc_domain->index, PWRC_ON, 0, 0, 0) < 0) {
		pr_err("failed to set power domain on\n");
		ret = -EINVAL;
	}

	return ret;
}

#define SEC_PD(__name, __flag)			\
[PWRC_##__name##_ID] =				\
{						\
	.name = #__name,			\
	.index = PWRC_##__name##_ID,		\
	.is_off = pwrc_secure_is_off,		\
	.flags = __flag,			\
	.parent = PWRC_NO_PARENT,		\
}

#define TOP_PD(__name, __flag, __parent)	\
[PWRC_##__name##_ID] =				\
{						\
	.name = #__name,			\
	.index = PWRC_##__name##_ID,		\
	.is_off = pwrc_secure_is_off,		\
	.flags = __flag,			\
	.parent = __parent,			\
}

static const struct meson_secure_pwrc_domain_desc a1_pwrc_domains[] = {
	SEC_PD(DSPA,	0),
	SEC_PD(DSPB,	0),
	/* UART should keep working in ATF after suspend and before resume */
	SEC_PD(UART,	GENPD_FLAG_ALWAYS_ON),
	/* DMC is for DDR PHY ana/dig and DMC, and should be always on */
	SEC_PD(DMC,	GENPD_FLAG_ALWAYS_ON),
	SEC_PD(I2C,	0),
	SEC_PD(PSRAM,	0),
	SEC_PD(ACODEC,	0),
	SEC_PD(AUDIO,	0),
	SEC_PD(OTP,	0),
	SEC_PD(DMA,	GENPD_FLAG_ALWAYS_ON | GENPD_FLAG_IRQ_SAFE),
	SEC_PD(SD_EMMC,	0),
	SEC_PD(RAMA,	0),
	/* SRAMB is used as ATF runtime memory, and should be always on */
	SEC_PD(RAMB,	GENPD_FLAG_ALWAYS_ON),
	SEC_PD(IR,	0),
	SEC_PD(SPICC,	0),
	SEC_PD(SPIFC,	0),
	SEC_PD(USB,	0),
	/* NIC is for the Arm NIC-400 interconnect, and should be always on */
	SEC_PD(NIC,	GENPD_FLAG_ALWAYS_ON),
	SEC_PD(PDMIN,	0),
	SEC_PD(RSA,	0),
};

static const struct meson_secure_pwrc_domain_desc a4_pwrc_domains[] = {
	SEC_PD(A4_AUDIO,	0),
	SEC_PD(A4_SDIOA,	0),
	SEC_PD(A4_EMMC,	0),
	SEC_PD(A4_USB_COMB,	0),
	SEC_PD(A4_ETH,		0),
	SEC_PD(A4_VOUT,		0),
	SEC_PD(A4_AUDIO_PDM,	0),
	/* DMC is for DDR PHY ana/dig and DMC, and should be always on */
	SEC_PD(A4_DMC,	GENPD_FLAG_ALWAYS_ON),
	/* WRAP is secure_top, a lot of modules are included, and should be always on */
	SEC_PD(A4_SYS_WRAP,	GENPD_FLAG_ALWAYS_ON),
	SEC_PD(A4_AO_I2C_S,	0),
	SEC_PD(A4_AO_UART,	0),
	/* IR is wake up trigger source, and should be always on */
	SEC_PD(A4_AO_IR,	GENPD_FLAG_ALWAYS_ON),
};

static const struct meson_secure_pwrc_domain_desc a5_pwrc_domains[] = {
	SEC_PD(A5_NNA,		0),
	SEC_PD(A5_AUDIO,	0),
	SEC_PD(A5_SDIOA,	0),
	SEC_PD(A5_EMMC,		0),
	SEC_PD(A5_USB_COMB,	0),
	SEC_PD(A5_ETH,		0),
	SEC_PD(A5_RSA,		0),
	SEC_PD(A5_AUDIO_PDM,	0),
	/* DMC is for DDR PHY ana/dig and DMC, and should be always on */
	SEC_PD(A5_DMC,		GENPD_FLAG_ALWAYS_ON),
	/* WRAP is secure_top, a lot of modules are included, and should be always on */
	SEC_PD(A5_SYS_WRAP,	GENPD_FLAG_ALWAYS_ON),
	SEC_PD(A5_DSPA,		0),
};

static const struct meson_secure_pwrc_domain_desc c3_pwrc_domains[] = {
	SEC_PD(C3_NNA,		0),
	SEC_PD(C3_AUDIO,	0),
	SEC_PD(C3_SDIOA,	0),
	SEC_PD(C3_EMMC,		0),
	SEC_PD(C3_USB_COMB,	0),
	SEC_PD(C3_SDCARD,	0),
	/* ETH is for ethernet online wakeup, and should be always on */
	SEC_PD(C3_ETH,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(C3_GE2D,		0),
	SEC_PD(C3_CVE,		0),
	SEC_PD(C3_GDC_WRAP,	0),
	SEC_PD(C3_ISP_TOP,	0),
	SEC_PD(C3_MIPI_ISP_WRAP, 0),
	SEC_PD(C3_VCODEC,	0),
};

static const struct meson_secure_pwrc_domain_desc s4_pwrc_domains[] = {
	SEC_PD(S4_DOS_HEVC,	0),
	SEC_PD(S4_DOS_VDEC,	0),
	SEC_PD(S4_VPU_HDMI,	0),
	SEC_PD(S4_USB_COMB,	0),
	SEC_PD(S4_GE2D,		0),
	/* ETH is for ethernet online wakeup, and should be always on */
	SEC_PD(S4_ETH,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(S4_DEMOD,	0),
	SEC_PD(S4_AUDIO,	0),
};

static const struct meson_secure_pwrc_domain_desc s6_pwrc_domains[] = {
	SEC_PD(S6_DSPA,		0),
	SEC_PD(S6_DOS_HEVC,	0),
	SEC_PD(S6_DOS_VDEC,	0),
	SEC_PD(S6_VPU_HDMI,	0),
	SEC_PD(S6_U2DRD,	0),
	SEC_PD(S6_U3DRD,	0),
	SEC_PD(S6_SD_EMMC_C,	0),
	SEC_PD(S6_GE2D,		0),
	SEC_PD(S6_AMFC,		0),
	SEC_PD(S6_VC9000E,	0),
	SEC_PD(S6_DEWARP,	0),
	SEC_PD(S6_VICP,		0),
	SEC_PD(S6_SD_EMMC_A,	0),
	SEC_PD(S6_SD_EMMC_B,	0),
	/* ETH is for ethernet online wakeup, and should be always on */
	SEC_PD(S6_ETH,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(S6_PCIE,		0),
	SEC_PD(S6_NNA_4T,	0),
	SEC_PD(S6_AUDIO,	0),
	SEC_PD(S6_AUCPU,	0),
	SEC_PD(S6_ADAPT,	0),
};

static const struct meson_secure_pwrc_domain_desc s7_pwrc_domains[] = {
	SEC_PD(S7_DOS_HEVC,	0),
	SEC_PD(S7_DOS_VDEC,	0),
	SEC_PD(S7_VPU_HDMI,	0),
	SEC_PD(S7_USB_COMB,	0),
	SEC_PD(S7_SD_EMMC_C,	0),
	SEC_PD(S7_GE2D,		0),
	SEC_PD(S7_SD_EMMC_A,	0),
	SEC_PD(S7_SD_EMMC_B,	0),
	/* ETH is for ethernet online wakeup, and should be always on */
	SEC_PD(S7_ETH,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(S7_AUCPU,	0),
	SEC_PD(S7_AUDIO,	0),
};

static const struct meson_secure_pwrc_domain_desc s7d_pwrc_domains[] = {
	SEC_PD(S7D_DOS_HCODEC,	0),
	SEC_PD(S7D_DOS_HEVC,	0),
	SEC_PD(S7D_DOS_VDEC,	0),
	SEC_PD(S7D_VPU_HDMI,	0),
	SEC_PD(S7D_USB_U2DRD,	0),
	SEC_PD(S7D_USB_U2H,	0),
	SEC_PD(S7D_SSD_EMMC_C,	0),
	SEC_PD(S7D_GE2D,	0),
	SEC_PD(S7D_AMFC,	0),
	SEC_PD(S7D_EMMC_A,	0),
	SEC_PD(S7D_EMMC_B,	0),
	/* ETH is for ethernet online wakeup, and should be always on */
	SEC_PD(S7D_ETH,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(S7D_AUCPU,	0),
	SEC_PD(S7D_AUDIO,	0),
	/* SRAMA is used as ATF runtime memory, and should be always on */
	SEC_PD(S7D_SRAMA,	GENPD_FLAG_ALWAYS_ON),
	/* DMC0 is for DDR PHY ana/dig and DMC, and should be always on */
	SEC_PD(S7D_DMC0,	GENPD_FLAG_ALWAYS_ON),
	/* DMC1 is for DDR PHY ana/dig and DMC, and should be always on */
	SEC_PD(S7D_DMC1,	GENPD_FLAG_ALWAYS_ON),
	/* DDR should be always on */
	SEC_PD(S7D_DDR,		GENPD_FLAG_ALWAYS_ON),
};

static const struct meson_secure_pwrc_domain_desc t7_pwrc_domains[] = {
	SEC_PD(T7_DSPA,		0),
	SEC_PD(T7_DSPB,		0),
	TOP_PD(T7_DOS_HCODEC,	0, PWRC_T7_NIC3_ID),
	TOP_PD(T7_DOS_HEVC,	0, PWRC_T7_NIC3_ID),
	TOP_PD(T7_DOS_VDEC,	0, PWRC_T7_NIC3_ID),
	TOP_PD(T7_DOS_WAVE,	0, PWRC_T7_NIC3_ID),
	SEC_PD(T7_VPU_HDMI,	0),
	SEC_PD(T7_USB_COMB,	0),
	SEC_PD(T7_PCIE,		0),
	TOP_PD(T7_GE2D,		0, PWRC_T7_NIC3_ID),
	/* SRAMA is used as ATF runtime memory, and should be always on */
	SEC_PD(T7_SRAMA,	GENPD_FLAG_ALWAYS_ON),
	/* SRAMB is used as ATF runtime memory, and should be always on */
	SEC_PD(T7_SRAMB,	GENPD_FLAG_ALWAYS_ON),
	SEC_PD(T7_HDMIRX,	0),
	SEC_PD(T7_VI_CLK1,	0),
	SEC_PD(T7_VI_CLK2,	0),
	/* ETH is for ethernet online wakeup, and should be always on */
	SEC_PD(T7_ETH,		GENPD_FLAG_ALWAYS_ON),
	TOP_PD(T7_ISP,		0, PWRC_T7_MIPI_ISP_ID),
	SEC_PD(T7_MIPI_ISP,	0),
	TOP_PD(T7_GDC,		0, PWRC_T7_NIC3_ID),
	TOP_PD(T7_DEWARP,	0, PWRC_T7_NIC3_ID),
	SEC_PD(T7_SDIO_A,	0),
	SEC_PD(T7_SDIO_B,	0),
	SEC_PD(T7_EMMC,		0),
	TOP_PD(T7_MALI_SC0,	0, PWRC_T7_NNA_TOP_ID),
	TOP_PD(T7_MALI_SC1,	0, PWRC_T7_NNA_TOP_ID),
	TOP_PD(T7_MALI_SC2,	0, PWRC_T7_NNA_TOP_ID),
	TOP_PD(T7_MALI_SC3,	0, PWRC_T7_NNA_TOP_ID),
	SEC_PD(T7_MALI_TOP,	0),
	TOP_PD(T7_NNA_CORE0,	0, PWRC_T7_NNA_TOP_ID),
	TOP_PD(T7_NNA_CORE1,	0, PWRC_T7_NNA_TOP_ID),
	TOP_PD(T7_NNA_CORE2,	0, PWRC_T7_NNA_TOP_ID),
	TOP_PD(T7_NNA_CORE3,	0, PWRC_T7_NNA_TOP_ID),
	SEC_PD(T7_NNA_TOP,	0),
	SEC_PD(T7_DDR0,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(T7_DDR1,		GENPD_FLAG_ALWAYS_ON),
	/* DMC0 is for DDR PHY ana/dig and DMC, and should be always on */
	SEC_PD(T7_DMC0,		GENPD_FLAG_ALWAYS_ON),
	/* DMC1 is for DDR PHY ana/dig and DMC, and should be always on */
	SEC_PD(T7_DMC1,		GENPD_FLAG_ALWAYS_ON),
	/* NOC is related to clk bus, and should be always on */
	SEC_PD(T7_NOC,		GENPD_FLAG_ALWAYS_ON),
	/* NIC is for the Arm NIC-400 interconnect, and should be always on */
	SEC_PD(T7_NIC2,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(T7_NIC3,		0),
	/* CPU accesses the interleave data to the ddr need cci, and should be always on */
	SEC_PD(T7_CCI,		GENPD_FLAG_ALWAYS_ON),
	SEC_PD(T7_MIPI_DSI0,	0),
	SEC_PD(T7_SPICC0,	0),
	SEC_PD(T7_SPICC1,	0),
	SEC_PD(T7_SPICC2,	0),
	SEC_PD(T7_SPICC3,	0),
	SEC_PD(T7_SPICC4,	0),
	SEC_PD(T7_SPICC5,	0),
	SEC_PD(T7_EDP0,		0),
	SEC_PD(T7_EDP1,		0),
	SEC_PD(T7_MIPI_DSI1,	0),
	SEC_PD(T7_AUDIO,	0),
};

static int meson_secure_pwrc_probe(struct platform_device *pdev)
{
	int i;
	struct device_node *sm_np;
	struct meson_secure_pwrc *pwrc;
	const struct meson_secure_pwrc_domain_data *match;

	match = of_device_get_match_data(&pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "failed to get match data\n");
		return -ENODEV;
	}

	sm_np = of_find_compatible_node(NULL, NULL, "amlogic,meson-gxbb-sm");
	if (!sm_np) {
		dev_err(&pdev->dev, "no secure-monitor node\n");
		return -ENODEV;
	}

	pwrc = devm_kzalloc(&pdev->dev, sizeof(*pwrc), GFP_KERNEL);
	if (!pwrc) {
		of_node_put(sm_np);
		return -ENOMEM;
	}

	pwrc->fw = meson_sm_get(sm_np);
	of_node_put(sm_np);
	if (!pwrc->fw)
		return -EPROBE_DEFER;

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
	platform_set_drvdata(pdev, pwrc);

	for (i = 0 ; i < match->count ; ++i) {
		struct meson_secure_pwrc_domain *dom = &pwrc->domains[i];

		if (!match->domains[i].name)
			continue;

		dom->pwrc = pwrc;
		dom->index = match->domains[i].index;
		dom->parent = match->domains[i].parent;
		dom->base.name = match->domains[i].name;
		dom->base.flags = match->domains[i].flags;
		dom->base.power_on = meson_secure_pwrc_on;
		dom->base.power_off = meson_secure_pwrc_off;

		if (match->domains[i].is_off(dom) && (dom->base.flags & GENPD_FLAG_ALWAYS_ON))
			meson_secure_pwrc_on(&dom->base);

		pm_genpd_init(&dom->base, NULL, match->domains[i].is_off(dom));

		pwrc->xlate.domains[i] = &dom->base;
	}

	for (i = 0; i < match->count; i++) {
		struct meson_secure_pwrc_domain *dom = pwrc->domains;

		if (!match->domains[i].name || match->domains[i].parent == PWRC_NO_PARENT)
			continue;

		pm_genpd_add_subdomain(&dom[dom[i].parent].base, &dom[i].base);
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node, &pwrc->xlate);
}

static const struct meson_secure_pwrc_domain_data meson_secure_a1_pwrc_data = {
	.domains = a1_pwrc_domains,
	.count = ARRAY_SIZE(a1_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data amlogic_secure_a4_pwrc_data = {
	.domains = a4_pwrc_domains,
	.count = ARRAY_SIZE(a4_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data amlogic_secure_a5_pwrc_data = {
	.domains = a5_pwrc_domains,
	.count = ARRAY_SIZE(a5_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data amlogic_secure_c3_pwrc_data = {
	.domains = c3_pwrc_domains,
	.count = ARRAY_SIZE(c3_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data meson_secure_s4_pwrc_data = {
	.domains = s4_pwrc_domains,
	.count = ARRAY_SIZE(s4_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data amlogic_secure_s6_pwrc_data = {
	.domains = s6_pwrc_domains,
	.count = ARRAY_SIZE(s6_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data amlogic_secure_s7_pwrc_data = {
	.domains = s7_pwrc_domains,
	.count = ARRAY_SIZE(s7_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data amlogic_secure_s7d_pwrc_data = {
	.domains = s7d_pwrc_domains,
	.count = ARRAY_SIZE(s7d_pwrc_domains),
};

static const struct meson_secure_pwrc_domain_data amlogic_secure_t7_pwrc_data = {
	.domains = t7_pwrc_domains,
	.count = ARRAY_SIZE(t7_pwrc_domains),
};

static const struct of_device_id meson_secure_pwrc_match_table[] = {
	{
		.compatible = "amlogic,meson-a1-pwrc",
		.data = &meson_secure_a1_pwrc_data,
	},
	{
		.compatible = "amlogic,a4-pwrc",
		.data = &amlogic_secure_a4_pwrc_data,
	},
	{
		.compatible = "amlogic,a5-pwrc",
		.data = &amlogic_secure_a5_pwrc_data,
	},
	{
		.compatible = "amlogic,c3-pwrc",
		.data = &amlogic_secure_c3_pwrc_data,
	},
	{
		.compatible = "amlogic,meson-s4-pwrc",
		.data = &meson_secure_s4_pwrc_data,
	},
	{
		.compatible = "amlogic,s6-pwrc",
		.data = &amlogic_secure_s6_pwrc_data,
	},
	{
		.compatible = "amlogic,s7-pwrc",
		.data = &amlogic_secure_s7_pwrc_data,
	},
	{
		.compatible = "amlogic,s7d-pwrc",
		.data = &amlogic_secure_s7d_pwrc_data,
	},
	{
		.compatible = "amlogic,t7-pwrc",
		.data = &amlogic_secure_t7_pwrc_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_secure_pwrc_match_table);

static struct platform_driver meson_secure_pwrc_driver = {
	.probe = meson_secure_pwrc_probe,
	.driver = {
		.name		= "meson_secure_pwrc",
		.of_match_table	= meson_secure_pwrc_match_table,
	},
};
module_platform_driver(meson_secure_pwrc_driver);
MODULE_DESCRIPTION("Amlogic Meson Secure Power Domains driver");
MODULE_LICENSE("Dual MIT/GPL");
