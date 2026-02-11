// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2025 Linaro Ltd
 */

#include <linux/iopoll.h>
#include <linux/reset.h>

#include "iris_instance.h"
#include "iris_vpu_common.h"
#include "iris_vpu_register_defines.h"

#define AON_WRAPPER_MVP_NOC_CORE_SW_RESET	(AON_BASE_OFFS + 0x18)
#define SW_RESET				BIT(0)
#define AON_WRAPPER_MVP_NOC_CORE_CLK_CONTROL	(AON_BASE_OFFS + 0x20)
#define NOC_HALT				BIT(0)
#define AON_WRAPPER_SPARE			(AON_BASE_OFFS + 0x28)

static bool iris_vpu3x_hw_power_collapsed(struct iris_core *core)
{
	u32 value, pwr_status;

	value = readl(core->reg_base + WRAPPER_CORE_POWER_STATUS);
	pwr_status = value & BIT(1);

	return pwr_status ? false : true;
}

static void iris_vpu3_power_off_hardware(struct iris_core *core)
{
	u32 reg_val = 0, value, i;
	int ret;

	if (iris_vpu3x_hw_power_collapsed(core))
		goto disable_power;

	dev_err(core->dev, "video hw is power on\n");

	value = readl(core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);
	if (value)
		writel(CORE_CLK_RUN, core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	for (i = 0; i < core->iris_platform_data->num_vpp_pipe; i++) {
		ret = readl_poll_timeout(core->reg_base + VCODEC_SS_IDLE_STATUSN + 4 * i,
					 reg_val, reg_val & 0x400000, 2000, 20000);
		if (ret)
			goto disable_power;
	}

	writel(VIDEO_NOC_RESET_REQ, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
				 reg_val, reg_val & 0x3, 200, 2000);
	if (ret)
		goto disable_power;

	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
				 reg_val, !(reg_val & 0x3), 200, 2000);
	if (ret)
		goto disable_power;

	writel(CORE_BRIDGE_SW_RESET | CORE_BRIDGE_HW_RESET_DISABLE,
	       core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);

disable_power:
	iris_vpu_power_off_hw(core);
}

static void iris_vpu33_power_off_hardware(struct iris_core *core)
{
	bool handshake_done = false, handshake_busy = false;
	u32 reg_val = 0, value, i;
	u32 count = 0;
	int ret;

	if (iris_vpu3x_hw_power_collapsed(core))
		goto disable_power;

	dev_err(core->dev, "video hw is power on\n");

	value = readl(core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);
	if (value)
		writel(CORE_CLK_RUN, core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	for (i = 0; i < core->iris_platform_data->num_vpp_pipe; i++) {
		ret = readl_poll_timeout(core->reg_base + VCODEC_SS_IDLE_STATUSN + 4 * i,
					 reg_val, reg_val & 0x400000, 2000, 20000);
		if (ret)
			goto disable_power;
	}

	/* Retry up to 1000 times as recommended by hardware documentation */
	do {
		/* set MNoC to low power */
		writel(REQ_POWER_DOWN_PREP, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);

		udelay(15);

		value = readl(core->reg_base + AON_WRAPPER_MVP_NOC_LPI_STATUS);

		handshake_done = value & NOC_LPI_STATUS_DONE;
		handshake_busy = value & (NOC_LPI_STATUS_DENY | NOC_LPI_STATUS_ACTIVE);

		if (handshake_done || !handshake_busy)
			break;

		writel(0, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);

		udelay(15);

	} while (++count < 1000);

	if (!handshake_done && handshake_busy)
		dev_err(core->dev, "LPI handshake timeout\n");

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_LPI_STATUS,
				 reg_val, reg_val & BIT(0), 200, 2000);
	if (ret)
		goto disable_power;

	writel(0, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);

	writel(CORE_BRIDGE_SW_RESET | CORE_BRIDGE_HW_RESET_DISABLE,
	       core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);

disable_power:
	iris_vpu_power_off_hw(core);
}

static int iris_vpu33_power_off_controller(struct iris_core *core)
{
	u32 xo_rst_tbl_size = core->iris_platform_data->controller_rst_tbl_size;
	u32 clk_rst_tbl_size = core->iris_platform_data->clk_rst_tbl_size;
	u32 val = 0;
	int ret;

	writel(MSK_SIGNAL_FROM_TENSILICA | MSK_CORE_POWER_ON, core->reg_base + CPU_CS_X2RPMH);

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

	reset_control_bulk_reset(clk_rst_tbl_size, core->resets);

	/* Disable MVP NoC clock */
	val = readl(core->reg_base + AON_WRAPPER_MVP_NOC_CORE_CLK_CONTROL);
	val |= NOC_HALT;
	writel(val, core->reg_base + AON_WRAPPER_MVP_NOC_CORE_CLK_CONTROL);

	/* enable MVP NoC reset */
	val = readl(core->reg_base + AON_WRAPPER_MVP_NOC_CORE_SW_RESET);
	val |= SW_RESET;
	writel(val, core->reg_base + AON_WRAPPER_MVP_NOC_CORE_SW_RESET);

	/* poll AON spare register bit0 to become zero with 50ms timeout */
	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_SPARE,
				 val, (val & BIT(0)) == 0, 1000, 50000);
	if (ret)
		goto disable_power;

	/* enable bit(1) to avoid cvp noc xo reset */
	val = readl(core->reg_base + AON_WRAPPER_SPARE);
	val |= BIT(1);
	writel(val, core->reg_base + AON_WRAPPER_SPARE);

	reset_control_bulk_assert(xo_rst_tbl_size, core->controller_resets);

	/* De-assert MVP NoC reset */
	val = readl(core->reg_base + AON_WRAPPER_MVP_NOC_CORE_SW_RESET);
	val &= ~SW_RESET;
	writel(val, core->reg_base + AON_WRAPPER_MVP_NOC_CORE_SW_RESET);

	usleep_range(80, 100);

	reset_control_bulk_deassert(xo_rst_tbl_size, core->controller_resets);

	/* reset AON spare register */
	writel(0, core->reg_base + AON_WRAPPER_SPARE);

	/* Enable MVP NoC clock */
	val = readl(core->reg_base + AON_WRAPPER_MVP_NOC_CORE_CLK_CONTROL);
	val &= ~NOC_HALT;
	writel(val, core->reg_base + AON_WRAPPER_MVP_NOC_CORE_CLK_CONTROL);

	iris_disable_unprepare_clock(core, IRIS_CTRL_CLK);

disable_power:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);

	return 0;
}

static int iris_vpu35_power_on_hw(struct iris_core *core)
{
	int ret;

	ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);
	if (ret)
		return ret;

	ret = iris_prepare_enable_clock(core, IRIS_AXI_CLK);
	if (ret)
		goto err_disable_power;

	ret = iris_prepare_enable_clock(core, IRIS_HW_FREERUN_CLK);
	if (ret)
		goto err_disable_axi_clk;

	ret = iris_prepare_enable_clock(core, IRIS_HW_CLK);
	if (ret)
		goto err_disable_hw_free_clk;

	ret = dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN], true);
	if (ret)
		goto err_disable_hw_clk;

	return 0;

err_disable_hw_clk:
	iris_disable_unprepare_clock(core, IRIS_HW_CLK);
err_disable_hw_free_clk:
	iris_disable_unprepare_clock(core, IRIS_HW_FREERUN_CLK);
err_disable_axi_clk:
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);
err_disable_power:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);

	return ret;
}

static void iris_vpu35_power_off_hw(struct iris_core *core)
{
	iris_vpu33_power_off_hardware(core);

	iris_disable_unprepare_clock(core, IRIS_HW_FREERUN_CLK);
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);
}

const struct vpu_ops iris_vpu3_ops = {
	.power_off_hw = iris_vpu3_power_off_hardware,
	.power_on_hw = iris_vpu_power_on_hw,
	.power_off_controller = iris_vpu_power_off_controller,
	.power_on_controller = iris_vpu_power_on_controller,
	.calc_freq = iris_vpu3x_vpu4x_calculate_frequency,
};

const struct vpu_ops iris_vpu33_ops = {
	.power_off_hw = iris_vpu33_power_off_hardware,
	.power_on_hw = iris_vpu_power_on_hw,
	.power_off_controller = iris_vpu33_power_off_controller,
	.power_on_controller = iris_vpu_power_on_controller,
	.calc_freq = iris_vpu3x_vpu4x_calculate_frequency,
};

const struct vpu_ops iris_vpu35_ops = {
	.power_off_hw = iris_vpu35_power_off_hw,
	.power_on_hw = iris_vpu35_power_on_hw,
	.power_off_controller = iris_vpu35_vpu4x_power_off_controller,
	.power_on_controller = iris_vpu35_vpu4x_power_on_controller,
	.program_bootup_registers = iris_vpu35_vpu4x_program_bootup_registers,
	.calc_freq = iris_vpu3x_vpu4x_calculate_frequency,
};
