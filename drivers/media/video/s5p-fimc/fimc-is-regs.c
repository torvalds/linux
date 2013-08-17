/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * Register setting functions
 *
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd
 * Younghwan Joo <yhwan.joo@samsung.com>
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include "fimc-is.h"
#include "fimc-is-regs.h"
#include "fimc-is-cmd.h"

void fimc_is_fw_clear_irq1(struct fimc_is *is, unsigned int intr_pos)
{
	writel(1 << intr_pos, is->regs + INTCR1);
}

void fimc_is_fw_clear_irq2(struct fimc_is *is)
{
	u32 cfg = readl(is->regs + INTSR2);
	writel(cfg, is->regs + INTCR2);
}

void fimc_is_hw_set_intgr0_gd0(struct fimc_is *is)
{
	writel(INTGR0_INTGD(0), is->regs + INTGR0);
}

int fimc_is_hw_wait_intsr0_intsd0(struct fimc_is *is)
{
	u32 cfg = readl(is->regs + INTSR0);
	u32 status = INTSR0_GET_INTSD(0, cfg);
	u32 timeout = 2000;

	while (status) {
		pr_info("checking status\n");

		cfg = readl(is->regs + INTSR0);
		status = INTSR0_GET_INTSD(0, cfg);
		if (timeout == 0) {
			pr_info("timeout\n");
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(1);
	}
	return 0;
}

int fimc_is_hw_wait_intmsr0_intmsd0(struct fimc_is *is)
{
	u32 cfg, status, timeout;

	cfg = readl(is->regs + INTMSR0);
	status = INTMSR0_GET_INTMSD(0, cfg);
	timeout = 2000;

	while (status) {
		pr_info("checking status\n");

		cfg = readl(is->regs + INTMSR0);
		status = INTMSR0_GET_INTMSD(0, cfg);
		if (timeout == 0) {
			pr_info("timeout\n");
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(1);
	}
	return 0;
}

int fimc_is_hw_set_param(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);

	writel(HIC_SET_PARAMETER, is->regs + ISSR(0));
	writel(0, is->regs + ISSR(1));
	writel(is->scenario_id, is->regs + ISSR(2));

	writel(atomic_read(&is->cfg_param[is->scenario_id].p_region_num),
							is->regs + ISSR(3));

	writel(is->cfg_param[is->scenario_id].p_region_index1,
							is->regs + ISSR(4));
	writel(is->cfg_param[is->scenario_id].p_region_index2,
							is->regs + ISSR(5));
	fimc_is_hw_set_intgr0_gd0(is);
	return 0;
}

int fimc_is_hw_set_tune(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);

	writel(HIC_SET_TUNE, is->regs + ISSR(0));
	writel(0, is->regs + ISSR(1));
	writel(is->h2i_cmd.entry_id, is->regs + ISSR(2));

	fimc_is_hw_set_intgr0_gd0(is);
	return 0;
}

#define FIMC_IS_MAX_PARAMS	4

int fimc_is_hw_get_params(struct fimc_is *is, unsigned int num_args)
{
	int i;

	if (num_args > FIMC_IS_MAX_PARAMS)
		return -EINVAL;

	is->i2h_cmd.num_args = num_args;

	for (i = 0; i < FIMC_IS_MAX_PARAMS; i++) {
		if (i < num_args)
			is->i2h_cmd.args[i] = readl(is->regs + ISSR(12 + i));
		else
			is->i2h_cmd.args[i] = 0;
	}

	return 0;
}

void fimc_is_hw_set_sensor_num(struct fimc_is *is)
{
	writel(ISR_DONE, is->regs + ISSR(0));
	writel(0, is->regs + ISSR(1));
	/* param 1 */
	writel(IHC_GET_SENSOR_NUMBER, is->regs + ISSR(2));
	/* param 2 */
	writel(FIMC_IS_SENSOR_NUM, is->regs + ISSR(3));
}

void fimc_is_hw_close_sensor(struct fimc_is *is, u32 id)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	writel(HIC_CLOSE_SENSOR, is->regs + ISSR(0));
	writel(0, is->regs + ISSR(1));
	writel(0, is->regs + ISSR(2));
	fimc_is_hw_set_intgr0_gd0(is);
}

void fimc_is_hw_get_setfile_addr(struct fimc_is *is)
{
	/* 1. Get FIMC-IS setfile address */
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	writel(HIC_GET_SET_FILE_ADDR, is->regs + ISSR(0));
	writel(0, is->regs + ISSR(1));
	fimc_is_hw_set_intgr0_gd0(is);
}

void fimc_is_hw_load_setfile(struct fimc_is *is)
{
	/* 1. Make FIMC-IS power-off state */
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	writel(HIC_LOAD_SET_FILE, is->regs + ISSR(0));
	writel(0, is->regs + ISSR(1));
	fimc_is_hw_set_intgr0_gd0(is);
}

void fimc_is_hw_change_mode(struct fimc_is *is)
{
	switch (is->scenario_id) {
	case ISS_PREVIEW_STILL:
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		writel(HIC_PREVIEW_STILL, is->regs + ISSR(0));
		writel(0, is->regs + ISSR(1));
		writel(is->setfile.sub_index, is->regs + ISSR(2));
		fimc_is_hw_set_intgr0_gd0(is);
		break;
	case ISS_PREVIEW_VIDEO:
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		writel(HIC_PREVIEW_VIDEO, is->regs + ISSR(0));
		writel(0, is->regs + ISSR(1));
		writel(is->setfile.sub_index, is->regs + ISSR(2));
		fimc_is_hw_set_intgr0_gd0(is);
		break;
	case ISS_CAPTURE_STILL:
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		writel(HIC_CAPTURE_STILL, is->regs + ISSR(0));
		writel(0, is->regs + ISSR(1));
		writel(is->setfile.sub_index, is->regs + ISSR(2));
		fimc_is_hw_set_intgr0_gd0(is);
		break;
	case ISS_CAPTURE_VIDEO:
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		writel(HIC_CAPTURE_VIDEO, is->regs + ISSR(0));
		writel(0, is->regs + ISSR(1));
		writel(is->setfile.sub_index, is->regs + ISSR(2));
		fimc_is_hw_set_intgr0_gd0(is);
		break;
	default:
		WARN(1, "Not implemented\n");
	}
}

void fimc_is_hw_set_stream(struct fimc_is *is, int on)
{
	if (on) {
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		writel(HIC_STREAM_ON, is->regs + ISSR(0));
		writel(0, is->regs + ISSR(1));
		writel(0, is->regs + ISSR(2));
		fimc_is_hw_set_intgr0_gd0(is);
	} else {
		fimc_is_hw_wait_intmsr0_intmsd0(is);
		writel(HIC_STREAM_OFF, is->regs + ISSR(0));
		writel(0, is->regs + ISSR(1));
		fimc_is_hw_set_intgr0_gd0(is);
	}
}

void fimc_is_hw_subip_power_off(struct fimc_is *is)
{
	/* Switch FIMC-IS to power-off state */
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	writel(HIC_POWER_DOWN, is->regs + ISSR(0));
	writel(0, is->regs + ISSR(1));
	fimc_is_hw_set_intgr0_gd0(is);
}

int fimc_is_itf_s_param(struct fimc_is *is, bool update_flg)
{
	int ret;

	if (update_flg)
		_is_hw_update_param(is);
#ifdef VIDEOBUF2_DMA_CONTIG
	fimc_is_mem_cache_clean(is->is_p_region, IS_PARAM_SIZE);
#else
	fimc_is_region_flush(is);
#endif

	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &is->state);
	fimc_is_hw_set_param(is);
	ret = wait_event_timeout(is->irq_queue,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &is->state),
				FIMC_IS_CONFIG_TIMEOUT);
	if (!ret) {
		pr_err("wait timeout : %s\n", __func__);
		return -EINVAL;
	}
	return 0;
}

int fimc_is_itf_mode_change(struct fimc_is *is)
{
	int ret;

	clear_bit(IS_ST_CHANGE_MODE, &is->state);
	fimc_is_hw_change_mode(is);
	ret = wait_event_timeout(is->irq_queue,
				test_bit(IS_ST_CHANGE_MODE, &is->state),
				FIMC_IS_CONFIG_TIMEOUT);
	if (!ret) {
		pr_err("Mode change timeout - %d!!\n", is->scenario_id);
		return -EINVAL;
	}
	return 0;
}

