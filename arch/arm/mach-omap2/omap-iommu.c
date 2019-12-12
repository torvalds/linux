// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP IOMMU quirks for various TI SoCs
 *
 * Copyright (C) 2015-2019 Texas Instruments Incorporated - http://www.ti.com/
 *      Suman Anna <s-anna@ti.com>
 */

#include <linux/platform_device.h>
#include <linux/err.h>

#include "omap_hwmod.h"
#include "omap_device.h"
#include "clockdomain.h"
#include "powerdomain.h"

static void omap_iommu_dra7_emu_swsup_config(struct platform_device *pdev,
					     bool enable)
{
	static struct clockdomain *emu_clkdm;
	static DEFINE_SPINLOCK(emu_lock);
	static atomic_t count;
	struct device_node *np = pdev->dev.of_node;

	if (!of_device_is_compatible(np, "ti,dra7-dsp-iommu"))
		return;

	if (!emu_clkdm) {
		emu_clkdm = clkdm_lookup("emu_clkdm");
		if (WARN_ON_ONCE(!emu_clkdm))
			return;
	}

	spin_lock(&emu_lock);

	if (enable && (atomic_inc_return(&count) == 1))
		clkdm_deny_idle(emu_clkdm);
	else if (!enable && (atomic_dec_return(&count) == 0))
		clkdm_allow_idle(emu_clkdm);

	spin_unlock(&emu_lock);
}

int omap_iommu_set_pwrdm_constraint(struct platform_device *pdev, bool request,
				    u8 *pwrst)
{
	struct powerdomain *pwrdm;
	struct omap_device *od;
	u8 next_pwrst;
	int ret = 0;

	od = to_omap_device(pdev);
	if (!od)
		return -ENODEV;

	if (od->hwmods_cnt != 1)
		return -EINVAL;

	pwrdm = omap_hwmod_get_pwrdm(od->hwmods[0]);
	if (!pwrdm)
		return -EINVAL;

	if (request) {
		*pwrst = pwrdm_read_next_pwrst(pwrdm);
		omap_iommu_dra7_emu_swsup_config(pdev, true);
	}

	if (*pwrst > PWRDM_POWER_RET)
		goto out;

	next_pwrst = request ? PWRDM_POWER_ON : *pwrst;

	ret = pwrdm_set_next_pwrst(pwrdm, next_pwrst);

out:
	if (!request)
		omap_iommu_dra7_emu_swsup_config(pdev, false);

	return ret;
}
