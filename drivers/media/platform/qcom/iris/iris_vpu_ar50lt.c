// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bits.h>
#include <linux/iopoll.h>
#include <linux/reset.h>

#include "iris_instance.h"
#include "iris_vpu_common.h"

#include "iris_vpu_register_defines.h"

#define WRAPPER_INTR_MASK_A2HVCODEC_BMSK_AR50LT BIT(3)

#define WRAPPER_VCODEC0_CLOCK_CONFIG_AR50LT		0xb0080

#define CPU_CS_VCICMD					0xa0020
#define CPU_CS_VCICMD_ARP_OFF			0x1

static void iris_vpu_ar50lt_set_preset_registers(struct iris_core *core)
{
	writel(0x0, core->reg_base + WRAPPER_VCODEC0_CLOCK_CONFIG_AR50LT);
}

static void iris_vpu_ar50lt_interrupt_init(struct iris_core *core)
{
	writel(WRAPPER_INTR_MASK_A2HVCODEC_BMSK_AR50LT, core->reg_base + WRAPPER_INTR_MASK);
}

static void iris_vpu_ar50lt_disable_arp(struct iris_core *core)
{
	writel(CPU_CS_VCICMD_ARP_OFF, core->reg_base + CPU_CS_VCICMD);
}

static int iris_vpu_ar50lt_power_off_controller(struct iris_core *core)
{
	iris_disable_unprepare_clock(core, IRIS_AHB_CLK);
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);
	iris_disable_unprepare_clock(core, IRIS_CTRL_CLK);
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);

	return 0;
}

static void iris_vpu_ar50lt_power_off_hw(struct iris_core *core)
{
	dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN], false);
	iris_disable_unprepare_clock(core, IRIS_THROTTLE_CLK);
	iris_disable_unprepare_clock(core, IRIS_HW_AHB_CLK);
	iris_disable_unprepare_clock(core, IRIS_HW_CLK);
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);
}

static int iris_vpu_ar50lt_power_on_controller(struct iris_core *core)
{
	int ret;

	ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);
	if (ret)
		return ret;

	ret = iris_prepare_enable_clock(core, IRIS_CTRL_CLK);
	if (ret)
		goto err_disable_power;

	ret = iris_prepare_enable_clock(core, IRIS_AXI_CLK);
	if (ret && ret != -ENOENT)
		goto err_disable_ctrl_clock;

	ret = iris_prepare_enable_clock(core, IRIS_AHB_CLK);
	if (ret)
		goto err_disable_axi_clock;

	return 0;

err_disable_axi_clock:
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);
err_disable_ctrl_clock:
	iris_disable_unprepare_clock(core, IRIS_CTRL_CLK);
err_disable_power:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);

	return ret;
}

static int iris_vpu_ar50lt_power_on_hw(struct iris_core *core)
{
	int ret;

	ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);
	if (ret)
		return ret;

	ret = iris_prepare_enable_clock(core, IRIS_HW_CLK);
	if (ret)
		goto err_disable_power;

	ret = iris_prepare_enable_clock(core, IRIS_HW_AHB_CLK);
	if (ret)
		goto err_disable_hw_clock;

	ret = iris_prepare_enable_clock(core, IRIS_THROTTLE_CLK);
	if (ret)
		goto err_disable_hw_ahb_clock;

	return 0;

err_disable_hw_ahb_clock:
	iris_disable_unprepare_clock(core, IRIS_HW_AHB_CLK);
err_disable_hw_clock:
	iris_disable_unprepare_clock(core, IRIS_HW_CLK);
err_disable_power:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);

	return ret;
}

const struct vpu_ops iris_vpu_ar50lt_ops = {
	.power_off_hw = iris_vpu_ar50lt_power_off_hw,
	.power_on_hw = iris_vpu_ar50lt_power_on_hw,
	.power_off_controller = iris_vpu_ar50lt_power_off_controller,
	.power_on_controller = iris_vpu_ar50lt_power_on_controller,
	.calc_freq = iris_vpu2_calculate_frequency,
	.set_hwmode = iris_vpu_set_hwmode,
	.set_preset_registers = iris_vpu_ar50lt_set_preset_registers,
	.interrupt_init = iris_vpu_ar50lt_interrupt_init,
	.disable_arp = iris_vpu_ar50lt_disable_arp,
};
