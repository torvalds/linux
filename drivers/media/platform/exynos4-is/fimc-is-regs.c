/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2012 - 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Younghwan Joo <yhwan.joo@samsung.com>
 *          Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>

#include "fimc-is.h"
#include "fimc-is-command.h"
#include "fimc-is-regs.h"
#include "fimc-is-sensor.h"

void fimc_is_fw_clear_irq1(struct fimc_is *is, unsigned int nr)
{
	mcuctl_write(1UL << nr, is, MCUCTL_REG_INTCR1);
}

void fimc_is_fw_clear_irq2(struct fimc_is *is)
{
	u32 cfg = mcuctl_read(is, MCUCTL_REG_INTSR2);
	mcuctl_write(cfg, is, MCUCTL_REG_INTCR2);
}

void fimc_is_hw_set_intgr0_gd0(struct fimc_is *is)
{
	mcuctl_write(INTGR0_INTGD(0), is, MCUCTL_REG_INTGR0);
}

int fimc_is_hw_wait_intsr0_intsd0(struct fimc_is *is)
{
	unsigned int timeout = 2000;
	u32 cfg, status;

	cfg = mcuctl_read(is, MCUCTL_REG_INTSR0);
	status = INTSR0_GET_INTSD(0, cfg);

	while (status) {
		cfg = mcuctl_read(is, MCUCTL_REG_INTSR0);
		status = INTSR0_GET_INTSD(0, cfg);
		if (timeout == 0) {
			dev_warn(&is->pdev->dev, "%s timeout\n",
				 __func__);
			return -ETIME;
		}
		timeout--;
		udelay(1);
	}
	return 0;
}

int fimc_is_hw_wait_intmsr0_intmsd0(struct fimc_is *is)
{
	unsigned int timeout = 2000;
	u32 cfg, status;

	cfg = mcuctl_read(is, MCUCTL_REG_INTMSR0);
	status = INTMSR0_GET_INTMSD(0, cfg);

	while (status) {
		cfg = mcuctl_read(is, MCUCTL_REG_INTMSR0);
		status = INTMSR0_GET_INTMSD(0, cfg);
		if (timeout == 0) {
			dev_warn(&is->pdev->dev, "%s timeout\n",
				 __func__);
			return -ETIME;
		}
		timeout--;
		udelay(1);
	}
	return 0;
}

int fimc_is_hw_set_param(struct fimc_is *is)
{
	struct chain_config *config = &is->config[is->config_index];
	unsigned int param_count = __get_pending_param_count(is);

	fimc_is_hw_wait_intmsr0_intmsd0(is);

	mcuctl_write(HIC_SET_PARAMETER, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	mcuctl_write(is->config_index, is, MCUCTL_REG_ISSR(2));

	mcuctl_write(param_count, is, MCUCTL_REG_ISSR(3));
	mcuctl_write(config->p_region_index[0], is, MCUCTL_REG_ISSR(4));
	mcuctl_write(config->p_region_index[1], is, MCUCTL_REG_ISSR(5));

	fimc_is_hw_set_intgr0_gd0(is);
	return 0;
}

int fimc_is_hw_set_tune(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);

	mcuctl_write(HIC_SET_TUNE, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	mcuctl_write(is->h2i_cmd.entry_id, is, MCUCTL_REG_ISSR(2));

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
			is->i2h_cmd.args[i] = mcuctl_read(is,
					MCUCTL_REG_ISSR(12 + i));
		else
			is->i2h_cmd.args[i] = 0;
	}
	return 0;
}

void fimc_is_hw_set_sensor_num(struct fimc_is *is)
{
	pr_debug("setting sensor index to: %d\n", is->sensor_index);

	mcuctl_write(IH_REPLY_DONE, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	mcuctl_write(IHC_GET_SENSOR_NUM, is, MCUCTL_REG_ISSR(2));
	mcuctl_write(FIMC_IS_SENSOR_NUM, is, MCUCTL_REG_ISSR(3));
}

void fimc_is_hw_close_sensor(struct fimc_is *is, unsigned int index)
{
	if (is->sensor_index != index)
		return;

	fimc_is_hw_wait_intmsr0_intmsd0(is);
	mcuctl_write(HIC_CLOSE_SENSOR, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(2));
	fimc_is_hw_set_intgr0_gd0(is);
}

void fimc_is_hw_get_setfile_addr(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	mcuctl_write(HIC_GET_SET_FILE_ADDR, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	fimc_is_hw_set_intgr0_gd0(is);
}

void fimc_is_hw_load_setfile(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	mcuctl_write(HIC_LOAD_SET_FILE, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	fimc_is_hw_set_intgr0_gd0(is);
}

int fimc_is_hw_change_mode(struct fimc_is *is)
{
	const u8 cmd[] = {
		HIC_PREVIEW_STILL, HIC_PREVIEW_VIDEO,
		HIC_CAPTURE_STILL, HIC_CAPTURE_VIDEO,
	};

	if (WARN_ON(is->config_index >= ARRAY_SIZE(cmd)))
		return -EINVAL;

	mcuctl_write(cmd[is->config_index], is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	mcuctl_write(is->setfile.sub_index, is, MCUCTL_REG_ISSR(2));
	fimc_is_hw_set_intgr0_gd0(is);
	return 0;
}

void fimc_is_hw_stream_on(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	mcuctl_write(HIC_STREAM_ON, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	mcuctl_write(0, is, MCUCTL_REG_ISSR(2));
	fimc_is_hw_set_intgr0_gd0(is);
}

void fimc_is_hw_stream_off(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	mcuctl_write(HIC_STREAM_OFF, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	fimc_is_hw_set_intgr0_gd0(is);
}

void fimc_is_hw_subip_power_off(struct fimc_is *is)
{
	fimc_is_hw_wait_intmsr0_intmsd0(is);
	mcuctl_write(HIC_POWER_DOWN, is, MCUCTL_REG_ISSR(0));
	mcuctl_write(is->sensor_index, is, MCUCTL_REG_ISSR(1));
	fimc_is_hw_set_intgr0_gd0(is);
}

int fimc_is_itf_s_param(struct fimc_is *is, bool update)
{
	int ret;

	if (update)
		__is_hw_update_params(is);

	fimc_is_mem_barrier();

	clear_bit(IS_ST_BLOCK_CMD_CLEARED, &is->state);
	fimc_is_hw_set_param(is);
	ret = fimc_is_wait_event(is, IS_ST_BLOCK_CMD_CLEARED, 1,
				FIMC_IS_CONFIG_TIMEOUT);
	if (ret < 0)
		dev_err(&is->pdev->dev, "%s() timeout\n", __func__);

	return ret;
}

int fimc_is_itf_mode_change(struct fimc_is *is)
{
	int ret;

	clear_bit(IS_ST_CHANGE_MODE, &is->state);
	fimc_is_hw_change_mode(is);
	ret = fimc_is_wait_event(is, IS_ST_CHANGE_MODE, 1,
				FIMC_IS_CONFIG_TIMEOUT);
	if (!ret < 0)
		dev_err(&is->pdev->dev, "%s(): mode change (%d) timeout\n",
			__func__, is->config_index);
	return ret;
}
