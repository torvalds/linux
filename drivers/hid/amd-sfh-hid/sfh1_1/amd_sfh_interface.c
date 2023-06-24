// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP2 1.1 communication interfaces
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iopoll.h>

#include "amd_sfh_interface.h"

static int amd_sfh_wait_response(struct amd_mp2_dev *mp2, u8 sid, u32 cmd_id)
{
	struct sfh_cmd_response cmd_resp;

	/* Get response with status within a max of 1600 ms timeout */
	if (!readl_poll_timeout(mp2->mmio + AMD_P2C_MSG(0), cmd_resp.resp,
				(cmd_resp.response.response == 0 &&
				cmd_resp.response.cmd_id == cmd_id && (sid == 0xff ||
				cmd_resp.response.sensor_id == sid)), 500, 1600000))
		return cmd_resp.response.response;

	return -1;
}

static void amd_start_sensor(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info)
{
	struct sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd.cmd_id = ENABLE_SENSOR;
	cmd_base.cmd.intr_disable = 0;
	cmd_base.cmd.sensor_id = info.sensor_idx;

	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG(0));
}

static void amd_stop_sensor(struct amd_mp2_dev *privdata, u16 sensor_idx)
{
	struct sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd.cmd_id = DISABLE_SENSOR;
	cmd_base.cmd.intr_disable = 0;
	cmd_base.cmd.sensor_id = sensor_idx;

	writeq(0x0, privdata->mmio + AMD_C2P_MSG(1));
	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG(0));
}

static void amd_stop_all_sensor(struct amd_mp2_dev *privdata)
{
	struct sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd.cmd_id = STOP_ALL_SENSORS;
	cmd_base.cmd.intr_disable = 0;

	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG(0));
}

static struct amd_mp2_ops amd_sfh_ops = {
	.start = amd_start_sensor,
	.stop = amd_stop_sensor,
	.stop_all = amd_stop_all_sensor,
	.response = amd_sfh_wait_response,
};

void sfh_interface_init(struct amd_mp2_dev *mp2)
{
	mp2->mp2_ops = &amd_sfh_ops;
}
