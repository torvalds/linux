/*
 * Copyright (C) 2017 ZTE Ltd.
 *
 * Author: Baoyou Xie <baoyou.xie@linaro.org>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <dt-bindings/soc/zte,pm_domains.h>
#include "zx2967_pm_domains.h"

static u16 zx296718_offsets[REG_ARRAY_SIZE] = {
	[REG_CLKEN] = 0x18,
	[REG_ISOEN] = 0x1c,
	[REG_RSTEN] = 0x20,
	[REG_PWREN] = 0x24,
	[REG_ACK_SYNC] = 0x28,
};

enum {
	PCU_DM_VOU = 0,
	PCU_DM_SAPPU,
	PCU_DM_VDE,
	PCU_DM_VCE,
	PCU_DM_HDE,
	PCU_DM_VIU,
	PCU_DM_USB20,
	PCU_DM_USB21,
	PCU_DM_USB30,
	PCU_DM_HSIC,
	PCU_DM_GMAC,
	PCU_DM_TS,
};

static struct zx2967_pm_domain vou_domain = {
	.dm = {
		.name		= "vou_domain",
	},
	.bit = PCU_DM_VOU,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain sappu_domain = {
	.dm = {
		.name		= "sappu_domain",
	},
	.bit = PCU_DM_SAPPU,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain vde_domain = {
	.dm = {
		.name		= "vde_domain",
	},
	.bit = PCU_DM_VDE,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain vce_domain = {
	.dm = {
		.name		= "vce_domain",
	},
	.bit = PCU_DM_VCE,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain hde_domain = {
	.dm = {
		.name		= "hde_domain",
	},
	.bit = PCU_DM_HDE,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain viu_domain = {
	.dm = {
		.name		= "viu_domain",
	},
	.bit = PCU_DM_VIU,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain usb20_domain = {
	.dm = {
		.name		= "usb20_domain",
	},
	.bit = PCU_DM_USB20,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain usb21_domain = {
	.dm = {
		.name		= "usb21_domain",
	},
	.bit = PCU_DM_USB21,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain usb30_domain = {
	.dm = {
		.name		= "usb30_domain",
	},
	.bit = PCU_DM_USB30,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain hsic_domain = {
	.dm = {
		.name		= "hsic_domain",
	},
	.bit = PCU_DM_HSIC,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain gmac_domain = {
	.dm = {
		.name		= "gmac_domain",
	},
	.bit = PCU_DM_GMAC,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct zx2967_pm_domain ts_domain = {
	.dm = {
		.name		= "ts_domain",
	},
	.bit = PCU_DM_TS,
	.polarity = PWREN,
	.reg_offset = zx296718_offsets,
};

static struct generic_pm_domain *zx296718_pm_domains[] = {
	[DM_ZX296718_VOU] = &vou_domain.dm,
	[DM_ZX296718_SAPPU] = &sappu_domain.dm,
	[DM_ZX296718_VDE] = &vde_domain.dm,
	[DM_ZX296718_VCE] = &vce_domain.dm,
	[DM_ZX296718_HDE] = &hde_domain.dm,
	[DM_ZX296718_VIU] = &viu_domain.dm,
	[DM_ZX296718_USB20] = &usb20_domain.dm,
	[DM_ZX296718_USB21] = &usb21_domain.dm,
	[DM_ZX296718_USB30] = &usb30_domain.dm,
	[DM_ZX296718_HSIC] = &hsic_domain.dm,
	[DM_ZX296718_GMAC] = &gmac_domain.dm,
	[DM_ZX296718_TS] = &ts_domain.dm,
};

static int zx296718_pd_probe(struct platform_device *pdev)
{
	return zx2967_pd_probe(pdev,
			  zx296718_pm_domains,
			  ARRAY_SIZE(zx296718_pm_domains));
}

static const struct of_device_id zx296718_pm_domain_matches[] = {
	{ .compatible = "zte,zx296718-pcu", },
	{ },
};

static struct platform_driver zx296718_pd_driver = {
	.driver = {
		.name = "zx296718-powerdomain",
		.of_match_table = zx296718_pm_domain_matches,
	},
	.probe = zx296718_pd_probe,
};

static int __init zx296718_pd_init(void)
{
	return platform_driver_register(&zx296718_pd_driver);
}
subsys_initcall(zx296718_pd_init);
