// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Amlogic, Inc.
 * Author: Jianxin Pan <jianxin.pan@amlogic.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <dt-bindings/power/meson-a1-power.h>
#include <linux/arm-smccc.h>
#include <linux/firmware/meson/meson_sm.h>

#define PWRC_ON		1
#define PWRC_OFF	0

struct meson_secure_pwrc_domain {
	struct generic_pm_domain base;
	unsigned int index;
	struct meson_secure_pwrc *pwrc;
};

struct meson_secure_pwrc {
	struct meson_secure_pwrc_domain *domains;
	struct genpd_onecell_data xlate;
	struct meson_sm_firmware *fw;
};

struct meson_secure_pwrc_domain_desc {
	unsigned int index;
	unsigned int flags;
	char *name;
	bool (*is_off)(struct meson_secure_pwrc_domain *pwrc_domain);
};

struct meson_secure_pwrc_domain_data {
	unsigned int count;
	struct meson_secure_pwrc_domain_desc *domains;
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
	.is_off = pwrc_secure_is_off,	\
	.flags = __flag,			\
}

static struct meson_secure_pwrc_domain_desc a1_pwrc_domains[] = {
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
	SEC_PD(DMA,	0),
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

		if (!match->domains[i].index)
			continue;

		dom->pwrc = pwrc;
		dom->index = match->domains[i].index;
		dom->base.name = match->domains[i].name;
		dom->base.flags = match->domains[i].flags;
		dom->base.power_on = meson_secure_pwrc_on;
		dom->base.power_off = meson_secure_pwrc_off;

		pm_genpd_init(&dom->base, NULL, match->domains[i].is_off(dom));

		pwrc->xlate.domains[i] = &dom->base;
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node, &pwrc->xlate);
}

static struct meson_secure_pwrc_domain_data meson_secure_a1_pwrc_data = {
	.domains = a1_pwrc_domains,
	.count = ARRAY_SIZE(a1_pwrc_domains),
};

static const struct of_device_id meson_secure_pwrc_match_table[] = {
	{
		.compatible = "amlogic,meson-a1-pwrc",
		.data = &meson_secure_a1_pwrc_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver meson_secure_pwrc_driver = {
	.probe = meson_secure_pwrc_probe,
	.driver = {
		.name		= "meson_secure_pwrc",
		.of_match_table	= meson_secure_pwrc_match_table,
	},
};
builtin_platform_driver(meson_secure_pwrc_driver);
