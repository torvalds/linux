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

#define WRAPPER_TZ_BASE_OFFS			0x000C0000
#define AON_BASE_OFFS				0x000E0000
#define AON_MVP_NOC_RESET			0x0001F000

#define WRAPPER_DEBUG_BRIDGE_LPI_CONTROL	(WRAPPER_BASE_OFFS + 0x54)
#define WRAPPER_DEBUG_BRIDGE_LPI_STATUS		(WRAPPER_BASE_OFFS + 0x58)
#define WRAPPER_IRIS_CPU_NOC_LPI_CONTROL	(WRAPPER_BASE_OFFS + 0x5C)
#define REQ_POWER_DOWN_PREP			BIT(0)
#define WRAPPER_IRIS_CPU_NOC_LPI_STATUS		(WRAPPER_BASE_OFFS + 0x60)
#define NOC_LPI_STATUS_DONE			BIT(0) /* Indicates the NOC handshake is complete */
#define NOC_LPI_STATUS_DENY			BIT(1) /* Indicates the NOC handshake is denied */
#define NOC_LPI_STATUS_ACTIVE		BIT(2) /* Indicates the NOC is active */
#define WRAPPER_CORE_CLOCK_CONFIG		(WRAPPER_BASE_OFFS + 0x88)
#define CORE_CLK_RUN				0x0
/* VPU v3.5 */
#define WRAPPER_IRIS_VCODEC_VPU_WRAPPER_SPARE_0	(WRAPPER_BASE_OFFS + 0x78)

#define WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG		(WRAPPER_TZ_BASE_OFFS + 0x14)
#define CTL_AXI_CLK_HALT			BIT(0)
#define CTL_CLK_HALT				BIT(1)

#define WRAPPER_TZ_QNS4PDXFIFO_RESET		(WRAPPER_TZ_BASE_OFFS + 0x18)
#define RESET_HIGH				BIT(0)

#define CPU_CS_AHB_BRIDGE_SYNC_RESET		(CPU_CS_BASE_OFFS + 0x160)
#define CORE_BRIDGE_SW_RESET			BIT(0)
#define CORE_BRIDGE_HW_RESET_DISABLE		BIT(1)

#define CPU_CS_X2RPMH				(CPU_CS_BASE_OFFS + 0x168)
#define MSK_SIGNAL_FROM_TENSILICA		BIT(0)
#define MSK_CORE_POWER_ON			BIT(1)

#define AON_WRAPPER_MVP_NOC_RESET_REQ		(AON_MVP_NOC_RESET + 0x000)
#define VIDEO_NOC_RESET_REQ			(BIT(0) | BIT(1))

#define AON_WRAPPER_MVP_NOC_RESET_ACK		(AON_MVP_NOC_RESET + 0x004)

#define VCODEC_SS_IDLE_STATUSN			(VCODEC_BASE_OFFS + 0x70)

#define AON_WRAPPER_MVP_NOC_LPI_CONTROL		(AON_BASE_OFFS)
#define AON_WRAPPER_MVP_NOC_LPI_STATUS		(AON_BASE_OFFS + 0x4)

#define AON_WRAPPER_MVP_NOC_CORE_SW_RESET	(AON_BASE_OFFS + 0x18)
#define SW_RESET				BIT(0)
#define AON_WRAPPER_MVP_NOC_CORE_CLK_CONTROL	(AON_BASE_OFFS + 0x20)
#define NOC_HALT				BIT(0)
#define AON_WRAPPER_SPARE			(AON_BASE_OFFS + 0x28)
#define AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_CONTROL	(AON_BASE_OFFS + 0x2C)
#define AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_STATUS	(AON_BASE_OFFS + 0x30)

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

static int iris_vpu35_power_off_controller(struct iris_core *core)
{
	u32 clk_rst_tbl_size = core->iris_platform_data->clk_rst_tbl_size;
	unsigned int count = 0;
	u32 val = 0;
	bool handshake_done, handshake_busy;
	int ret;

	writel(MSK_SIGNAL_FROM_TENSILICA | MSK_CORE_POWER_ON, core->reg_base + CPU_CS_X2RPMH);

	writel(REQ_POWER_DOWN_PREP, core->reg_base + WRAPPER_IRIS_CPU_NOC_LPI_CONTROL);

	ret = readl_poll_timeout(core->reg_base + WRAPPER_IRIS_CPU_NOC_LPI_STATUS,
				 val, val & BIT(0), 200, 2000);
	if (ret)
		goto disable_power;

	writel(0, core->reg_base + WRAPPER_IRIS_CPU_NOC_LPI_CONTROL);

	/* Retry up to 1000 times as recommended by hardware documentation */
	do {
		/* set MNoC to low power */
		writel(REQ_POWER_DOWN_PREP, core->reg_base + AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_CONTROL);

		udelay(15);

		val = readl(core->reg_base + AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_STATUS);

		handshake_done = val & NOC_LPI_STATUS_DONE;
		handshake_busy = val & (NOC_LPI_STATUS_DENY | NOC_LPI_STATUS_ACTIVE);

		if (handshake_done || !handshake_busy)
			break;

		writel(0, core->reg_base + AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_CONTROL);

		udelay(15);

	} while (++count < 1000);

	if (!handshake_done && handshake_busy)
		dev_err(core->dev, "LPI handshake timeout\n");

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_STATUS,
				 val, val & BIT(0), 200, 2000);
	if (ret)
		goto disable_power;

	writel(0, core->reg_base + AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_CONTROL);

	writel(0, core->reg_base + WRAPPER_DEBUG_BRIDGE_LPI_CONTROL);

	ret = readl_poll_timeout(core->reg_base + WRAPPER_DEBUG_BRIDGE_LPI_STATUS,
				 val, val == 0, 200, 2000);
	if (ret)
		goto disable_power;

disable_power:
	iris_disable_unprepare_clock(core, IRIS_CTRL_CLK);
	iris_disable_unprepare_clock(core, IRIS_CTRL_FREERUN_CLK);
	iris_disable_unprepare_clock(core, IRIS_AXI1_CLK);

	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);

	reset_control_bulk_reset(clk_rst_tbl_size, core->resets);

	return 0;
}

static int iris_vpu35_power_on_controller(struct iris_core *core)
{
	int ret;

	ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);
	if (ret)
		return ret;

	ret = iris_prepare_enable_clock(core, IRIS_AXI1_CLK);
	if (ret)
		goto err_disable_power;

	ret = iris_prepare_enable_clock(core, IRIS_CTRL_FREERUN_CLK);
	if (ret)
		goto err_disable_axi1_clk;

	ret = iris_prepare_enable_clock(core, IRIS_CTRL_CLK);
	if (ret)
		goto err_disable_ctrl_free_clk;

	return 0;

err_disable_ctrl_free_clk:
	iris_disable_unprepare_clock(core, IRIS_CTRL_FREERUN_CLK);
err_disable_axi1_clk:
	iris_disable_unprepare_clock(core, IRIS_AXI1_CLK);
err_disable_power:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_CTRL_POWER_DOMAIN]);

	return ret;
}

static void iris_vpu35_program_bootup_registers(struct iris_core *core)
{
	writel(0x1, core->reg_base + WRAPPER_IRIS_VCODEC_VPU_WRAPPER_SPARE_0);
}

static u64 iris_vpu3x_calculate_frequency(struct iris_inst *inst, size_t data_size)
{
	struct platform_inst_caps *caps = inst->core->iris_platform_data->inst_caps;
	struct v4l2_format *inp_f = inst->fmt_src;
	u32 height, width, mbs_per_second, mbpf;
	u64 fw_cycles, fw_vpp_cycles;
	u64 vsp_cycles, vpp_cycles;
	u32 fps = DEFAULT_FPS;

	width = max(inp_f->fmt.pix_mp.width, inst->crop.width);
	height = max(inp_f->fmt.pix_mp.height, inst->crop.height);

	mbpf = NUM_MBS_PER_FRAME(height, width);
	mbs_per_second = mbpf * fps;

	fw_cycles = fps * caps->mb_cycles_fw;
	fw_vpp_cycles = fps * caps->mb_cycles_fw_vpp;

	vpp_cycles = mult_frac(mbs_per_second, caps->mb_cycles_vpp, (u32)inst->fw_caps[PIPE].value);
	/* 21 / 20 is minimum overhead factor */
	vpp_cycles += max(div_u64(vpp_cycles, 20), fw_vpp_cycles);

	/* 1.059 is multi-pipe overhead */
	if (inst->fw_caps[PIPE].value > 1)
		vpp_cycles += div_u64(vpp_cycles * 59, 1000);

	vsp_cycles = fps * data_size * 8;
	vsp_cycles = div_u64(vsp_cycles, 2);
	/* VSP FW overhead 1.05 */
	vsp_cycles = div_u64(vsp_cycles * 21, 20);

	if (inst->fw_caps[STAGE].value == STAGE_1)
		vsp_cycles = vsp_cycles * 3;

	return max3(vpp_cycles, vsp_cycles, fw_cycles);
}

const struct vpu_ops iris_vpu3_ops = {
	.power_off_hw = iris_vpu3_power_off_hardware,
	.power_on_hw = iris_vpu_power_on_hw,
	.power_off_controller = iris_vpu_power_off_controller,
	.power_on_controller = iris_vpu_power_on_controller,
	.calc_freq = iris_vpu3x_calculate_frequency,
};

const struct vpu_ops iris_vpu33_ops = {
	.power_off_hw = iris_vpu33_power_off_hardware,
	.power_on_hw = iris_vpu_power_on_hw,
	.power_off_controller = iris_vpu33_power_off_controller,
	.power_on_controller = iris_vpu_power_on_controller,
	.calc_freq = iris_vpu3x_calculate_frequency,
};

const struct vpu_ops iris_vpu35_ops = {
	.power_off_hw = iris_vpu35_power_off_hw,
	.power_on_hw = iris_vpu35_power_on_hw,
	.power_off_controller = iris_vpu35_power_off_controller,
	.power_on_controller = iris_vpu35_power_on_controller,
	.program_bootup_registers = iris_vpu35_program_bootup_registers,
	.calc_freq = iris_vpu3x_calculate_frequency,
};
