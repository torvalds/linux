// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>
#include <linux/pm_opp.h>
#include <linux/reset.h>

#include "iris_core.h"
#include "iris_vpu_common.h"
#include "iris_vpu_register_defines.h"

#define WRAPPER_TZ_BASE_OFFS			0x000C0000
#define AON_BASE_OFFS				0x000E0000

#define CPU_IC_BASE_OFFS			(CPU_BASE_OFFS)

#define CPU_CS_A2HSOFTINTCLR			(CPU_CS_BASE_OFFS + 0x1C)
#define CLEAR_XTENSA2HOST_INTR			BIT(0)

#define CTRL_INIT				(CPU_CS_BASE_OFFS + 0x48)
#define CTRL_STATUS				(CPU_CS_BASE_OFFS + 0x4C)

#define CTRL_INIT_IDLE_MSG_BMSK			0x40000000
#define CTRL_ERROR_STATUS__M			0xfe
#define CTRL_STATUS_PC_READY			0x100

#define QTBL_INFO				(CPU_CS_BASE_OFFS + 0x50)
#define QTBL_ENABLE				BIT(0)

#define QTBL_ADDR				(CPU_CS_BASE_OFFS + 0x54)
#define CPU_CS_SCIACMDARG3			(CPU_CS_BASE_OFFS + 0x58)
#define SFR_ADDR				(CPU_CS_BASE_OFFS + 0x5C)
#define UC_REGION_ADDR				(CPU_CS_BASE_OFFS + 0x64)
#define UC_REGION_SIZE				(CPU_CS_BASE_OFFS + 0x68)

#define CPU_CS_H2XSOFTINTEN			(CPU_CS_BASE_OFFS + 0x148)
#define HOST2XTENSA_INTR_ENABLE			BIT(0)

#define CPU_CS_X2RPMH				(CPU_CS_BASE_OFFS + 0x168)
#define MSK_SIGNAL_FROM_TENSILICA		BIT(0)
#define MSK_CORE_POWER_ON			BIT(1)

#define CPU_IC_SOFTINT				(CPU_IC_BASE_OFFS + 0x150)
#define CPU_IC_SOFTINT_H2A_SHFT			0x0

#define WRAPPER_INTR_STATUS			(WRAPPER_BASE_OFFS + 0x0C)
#define WRAPPER_INTR_STATUS_A2HWD_BMSK		BIT(3)
#define WRAPPER_INTR_STATUS_A2H_BMSK		BIT(2)

#define WRAPPER_INTR_MASK			(WRAPPER_BASE_OFFS + 0x10)
#define WRAPPER_INTR_MASK_A2HWD_BMSK		BIT(3)
#define WRAPPER_INTR_MASK_A2HCPU_BMSK		BIT(2)

#define WRAPPER_DEBUG_BRIDGE_LPI_CONTROL	(WRAPPER_BASE_OFFS + 0x54)
#define WRAPPER_DEBUG_BRIDGE_LPI_STATUS		(WRAPPER_BASE_OFFS + 0x58)
#define WRAPPER_IRIS_CPU_NOC_LPI_CONTROL	(WRAPPER_BASE_OFFS + 0x5C)
#define WRAPPER_IRIS_CPU_NOC_LPI_STATUS		(WRAPPER_BASE_OFFS + 0x60)

#define WRAPPER_TZ_CPU_STATUS			(WRAPPER_TZ_BASE_OFFS + 0x10)
#define WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG		(WRAPPER_TZ_BASE_OFFS + 0x14)
#define CTL_AXI_CLK_HALT			BIT(0)
#define CTL_CLK_HALT				BIT(1)

#define WRAPPER_TZ_QNS4PDXFIFO_RESET		(WRAPPER_TZ_BASE_OFFS + 0x18)
#define RESET_HIGH				BIT(0)

#define AON_WRAPPER_MVP_NOC_LPI_CONTROL		(AON_BASE_OFFS)
#define REQ_POWER_DOWN_PREP			BIT(0)

#define AON_WRAPPER_MVP_NOC_LPI_STATUS		(AON_BASE_OFFS + 0x4)

static void iris_vpu_interrupt_init(struct iris_core *core)
{
	u32 mask_val;

	mask_val = readl(core->reg_base + WRAPPER_INTR_MASK);
	mask_val &= ~(WRAPPER_INTR_MASK_A2HWD_BMSK |
		      WRAPPER_INTR_MASK_A2HCPU_BMSK);
	writel(mask_val, core->reg_base + WRAPPER_INTR_MASK);
}

static void iris_vpu_setup_ucregion_memory_map(struct iris_core *core)
{
	u32 queue_size, value;

	/* Iris hardware requires 4K queue alignment */
	queue_size = ALIGN(sizeof(struct iris_hfi_queue_table_header) +
		(IFACEQ_QUEUE_SIZE * IFACEQ_NUMQ), SZ_4K);

	value = (u32)core->iface_q_table_daddr;
	writel(value, core->reg_base + UC_REGION_ADDR);

	/* Iris hardware requires 1M queue alignment */
	value = ALIGN(SFR_SIZE + queue_size, SZ_1M);
	writel(value, core->reg_base + UC_REGION_SIZE);

	value = (u32)core->iface_q_table_daddr;
	writel(value, core->reg_base + QTBL_ADDR);

	writel(QTBL_ENABLE, core->reg_base + QTBL_INFO);

	if (core->sfr_daddr) {
		value = (u32)core->sfr_daddr + core->iris_platform_data->core_arch;
		writel(value, core->reg_base + SFR_ADDR);
	}
}

int iris_vpu_boot_firmware(struct iris_core *core)
{
	u32 ctrl_init = BIT(0), ctrl_status = 0, count = 0, max_tries = 1000;

	iris_vpu_setup_ucregion_memory_map(core);

	writel(ctrl_init, core->reg_base + CTRL_INIT);
	writel(0x1, core->reg_base + CPU_CS_SCIACMDARG3);

	while (!ctrl_status && count < max_tries) {
		ctrl_status = readl(core->reg_base + CTRL_STATUS);
		if ((ctrl_status & CTRL_ERROR_STATUS__M) == 0x4) {
			dev_err(core->dev, "invalid setting for uc_region\n");
			break;
		}

		usleep_range(50, 100);
		count++;
	}

	if (count >= max_tries) {
		dev_err(core->dev, "error booting up iris firmware\n");
		return -ETIME;
	}

	writel(HOST2XTENSA_INTR_ENABLE, core->reg_base + CPU_CS_H2XSOFTINTEN);
	writel(0x0, core->reg_base + CPU_CS_X2RPMH);

	return 0;
}

void iris_vpu_raise_interrupt(struct iris_core *core)
{
	writel(1 << CPU_IC_SOFTINT_H2A_SHFT, core->reg_base + CPU_IC_SOFTINT);
}

void iris_vpu_clear_interrupt(struct iris_core *core)
{
	u32 intr_status, mask;

	intr_status = readl(core->reg_base + WRAPPER_INTR_STATUS);
	mask = (WRAPPER_INTR_STATUS_A2H_BMSK |
		WRAPPER_INTR_STATUS_A2HWD_BMSK |
		CTRL_INIT_IDLE_MSG_BMSK);

	if (intr_status & mask)
		core->intr_status |= intr_status;

	writel(CLEAR_XTENSA2HOST_INTR, core->reg_base + CPU_CS_A2HSOFTINTCLR);
}

int iris_vpu_watchdog(struct iris_core *core, u32 intr_status)
{
	if (intr_status & WRAPPER_INTR_STATUS_A2HWD_BMSK) {
		dev_err(core->dev, "received watchdog interrupt\n");
		return -ETIME;
	}

	return 0;
}

int iris_vpu_prepare_pc(struct iris_core *core)
{
	u32 wfi_status, idle_status, pc_ready;
	u32 ctrl_status, val = 0;
	int ret;

	ctrl_status = readl(core->reg_base + CTRL_STATUS);
	pc_ready = ctrl_status & CTRL_STATUS_PC_READY;
	idle_status = ctrl_status & BIT(30);
	if (pc_ready)
		return 0;

	wfi_status = readl(core->reg_base + WRAPPER_TZ_CPU_STATUS);
	wfi_status &= BIT(0);
	if (!wfi_status || !idle_status)
		goto skip_power_off;

	ret = core->hfi_ops->sys_pc_prep(core);
	if (ret)
		goto skip_power_off;

	ret = readl_poll_timeout(core->reg_base + CTRL_STATUS, val,
				 val & CTRL_STATUS_PC_READY, 250, 2500);
	if (ret)
		goto skip_power_off;

	ret = readl_poll_timeout(core->reg_base + WRAPPER_TZ_CPU_STATUS,
				 val, val & BIT(0), 250, 2500);
	if (ret)
		goto skip_power_off;

	return 0;

skip_power_off:
	ctrl_status = readl(core->reg_base + CTRL_STATUS);
	wfi_status = readl(core->reg_base + WRAPPER_TZ_CPU_STATUS);
	wfi_status &= BIT(0);
	dev_err(core->dev, "skip power collapse, wfi=%#x, idle=%#x, pcr=%#x, ctrl=%#x)\n",
		wfi_status, idle_status, pc_ready, ctrl_status);

	return -EAGAIN;
}

static int iris_vpu_power_off_controller(struct iris_core *core)
{
	u32 val = 0;
	int ret;

	writel(MSK_SIGNAL_FROM_TENSILICA | MSK_CORE_POWER_ON, core->reg_base + CPU_CS_X2RPMH);

	writel(REQ_POWER_DOWN_PREP, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_LPI_STATUS,
				 val, val & BIT(0), 200, 2000);
	if (ret)
		goto disable_power;

	writel(REQ_POWER_DOWN_PREP, core->reg_base + WRAPPER_IRIS_CPU_NOC_LPI_CONTROL);

	ret = readl_poll_timeout(core->reg_base + WRAPPER_IRIS_CPU_NOC_LPI_STATUS,
				 val, val & BIT(0), 200, 2000);
	if (ret)
		goto disable_power;

	writel(0x0, core->reg_base + WRAPPER_DEBUG_BRIDGE_LPI_CONTROL);

	ret = readl_poll_timeout(core->reg_base + WRAPPER_DEBUG_BRIDGE_LPI_STATUS,
				 val, val == 0, 200, 2000);
	if (ret)
		goto disable_power;

	writel(CTL_AXI_CLK_HALT | CTL_CLK_HALT,
	       core->reg_base + WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG);
	writel(RESET_HIGH, core->reg_base + WRAPPER_TZ_QNS4PDXFIFO_RESET);
	writel(0x0, core->reg_base + WRAPPER_TZ_QNS4PDXFIFO_RESET);
	writel(0x0, core->reg_base + WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG);

disable_power:
	iris_disable_unprepare_clock(core, IRIS_CTRL_CLK);
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);

	return 0;
}

void iris_vpu_power_off_hw(struct iris_core *core)
{
	dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN], false);
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);
	iris_disable_unprepare_clock(core, IRIS_HW_CLK);
}

void iris_vpu_power_off(struct iris_core *core)
{
	dev_pm_opp_set_rate(core->dev, 0);
	core->iris_platform_data->vpu_ops->power_off_hw(core);
	iris_vpu_power_off_controller(core);
	iris_unset_icc_bw(core);

	if (!iris_vpu_watchdog(core, core->intr_status))
		disable_irq_nosync(core->irq);
}

static int iris_vpu_power_on_controller(struct iris_core *core)
{
	u32 rst_tbl_size = core->iris_platform_data->clk_rst_tbl_size;
	int ret;

	ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);
	if (ret)
		return ret;

	ret = reset_control_bulk_reset(rst_tbl_size, core->resets);
	if (ret)
		goto err_disable_power;

	ret = iris_prepare_enable_clock(core, IRIS_AXI_CLK);
	if (ret)
		goto err_disable_power;

	ret = iris_prepare_enable_clock(core, IRIS_CTRL_CLK);
	if (ret)
		goto err_disable_clock;

	return 0;

err_disable_clock:
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);
err_disable_power:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);

	return ret;
}

static int iris_vpu_power_on_hw(struct iris_core *core)
{
	int ret;

	ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);
	if (ret)
		return ret;

	ret = iris_prepare_enable_clock(core, IRIS_HW_CLK);
	if (ret)
		goto err_disable_power;

	ret = dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN], true);
	if (ret)
		goto err_disable_clock;

	return 0;

err_disable_clock:
	iris_disable_unprepare_clock(core, IRIS_HW_CLK);
err_disable_power:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);

	return ret;
}

int iris_vpu_power_on(struct iris_core *core)
{
	u32 freq;
	int ret;

	ret = iris_set_icc_bw(core, INT_MAX);
	if (ret)
		goto err;

	ret = iris_vpu_power_on_controller(core);
	if (ret)
		goto err_unvote_icc;

	ret = iris_vpu_power_on_hw(core);
	if (ret)
		goto err_power_off_ctrl;

	freq = core->power.clk_freq ? core->power.clk_freq :
				      (u32)ULONG_MAX;

	dev_pm_opp_set_rate(core->dev, freq);

	core->iris_platform_data->set_preset_registers(core);

	iris_vpu_interrupt_init(core);
	core->intr_status = 0;
	enable_irq(core->irq);

	return 0;

err_power_off_ctrl:
	iris_vpu_power_off_controller(core);
err_unvote_icc:
	iris_unset_icc_bw(core);
err:
	dev_err(core->dev, "power on failed\n");

	return ret;
}
