// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>

#include "iris_instance.h"
#include "iris_vpu_common.h"
#include "iris_vpu_register_defines.h"

#define AON_MVP_NOC_RESET			0x0001F000

#define WRAPPER_CORE_CLOCK_CONFIG		(WRAPPER_BASE_OFFS + 0x88)
#define CORE_CLK_RUN				0x0

#define CPU_CS_AHB_BRIDGE_SYNC_RESET		(CPU_CS_BASE_OFFS + 0x160)
#define CORE_BRIDGE_SW_RESET			BIT(0)
#define CORE_BRIDGE_HW_RESET_DISABLE		BIT(1)

#define AON_WRAPPER_MVP_NOC_RESET_REQ		(AON_MVP_NOC_RESET + 0x000)
#define VIDEO_NOC_RESET_REQ			(BIT(0) | BIT(1))

#define AON_WRAPPER_MVP_NOC_RESET_ACK		(AON_MVP_NOC_RESET + 0x004)

#define VCODEC_SS_IDLE_STATUSN			(VCODEC_BASE_OFFS + 0x70)

static bool iris_vpu3_hw_power_collapsed(struct iris_core *core)
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

	if (iris_vpu3_hw_power_collapsed(core))
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

static u64 iris_vpu3_calculate_frequency(struct iris_inst *inst, size_t data_size)
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
	.calc_freq = iris_vpu3_calculate_frequency,
};
