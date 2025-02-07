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

const struct vpu_ops iris_vpu3_ops = {
	.power_off_hw = iris_vpu3_power_off_hardware,
};
