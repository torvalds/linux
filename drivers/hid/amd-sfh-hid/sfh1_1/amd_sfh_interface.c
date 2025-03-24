// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP2 1.1 communication interfaces
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */
#include <linux/amd-pmf-io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iopoll.h>

#include "amd_sfh_interface.h"

static struct amd_mp2_dev *emp2;

static int amd_sfh_wait_response(struct amd_mp2_dev *mp2, u8 sid, u32 cmd_id)
{
	struct sfh_cmd_response cmd_resp;

	/* Get response with status within a max of 10000 ms timeout */
	if (!readl_poll_timeout(mp2->mmio + amd_get_p2c_val(mp2, 0), cmd_resp.resp,
				(cmd_resp.response.response == 0 &&
				cmd_resp.response.cmd_id == cmd_id && (sid == 0xff ||
				cmd_resp.response.sensor_id == sid)), 500, 10000000))
		return cmd_resp.response.response;

	return -1;
}

static void amd_start_sensor(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info)
{
	struct sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd.cmd_id = ENABLE_SENSOR;
	cmd_base.cmd.intr_disable = 0;
	cmd_base.cmd.sub_cmd_value = 1;
	cmd_base.cmd.sensor_id = info.sensor_idx;

	writel(cmd_base.ul, privdata->mmio + amd_get_c2p_val(privdata, 0));
}

static void amd_stop_sensor(struct amd_mp2_dev *privdata, u16 sensor_idx)
{
	struct sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd.cmd_id = DISABLE_SENSOR;
	cmd_base.cmd.intr_disable = 0;
	cmd_base.cmd.sub_cmd_value = 1;
	cmd_base.cmd.sensor_id = sensor_idx;

	writeq(0x0, privdata->mmio + amd_get_c2p_val(privdata, 1));
	writel(cmd_base.ul, privdata->mmio + amd_get_c2p_val(privdata, 0));
}

static void amd_stop_all_sensor(struct amd_mp2_dev *privdata)
{
	struct sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd.cmd_id = DISABLE_SENSOR;
	cmd_base.cmd.intr_disable = 0;
	/* 0xf indicates all sensors */
	cmd_base.cmd.sensor_id = 0xf;

	writel(cmd_base.ul, privdata->mmio + amd_get_c2p_val(privdata, 0));
}

static struct amd_mp2_ops amd_sfh_ops = {
	.start = amd_start_sensor,
	.stop = amd_stop_sensor,
	.stop_all = amd_stop_all_sensor,
	.response = amd_sfh_wait_response,
};

void sfh_deinit_emp2(void)
{
	emp2 = NULL;
}

void sfh_interface_init(struct amd_mp2_dev *mp2)
{
	mp2->mp2_ops = &amd_sfh_ops;
	emp2 = mp2;
}

static int amd_sfh_mode_info(u32 *platform_type, u32 *laptop_placement)
{
	struct sfh_op_mode mode;

	if (!platform_type || !laptop_placement)
		return -EINVAL;

	if (!emp2 || !emp2->dev_en.is_sra_present)
		return -ENODEV;

	mode.val = readl(emp2->mmio + amd_get_c2p_val(emp2, 3));

	*platform_type = mode.op_mode.devicemode;

	if (mode.op_mode.ontablestate == 1) {
		*laptop_placement = ON_TABLE;
	} else if (mode.op_mode.ontablestate == 2) {
		*laptop_placement = ON_LAP_MOTION;
	} else if (mode.op_mode.inbagstate == 1) {
		*laptop_placement = IN_BAG;
	} else if (mode.op_mode.outbagstate == 1) {
		*laptop_placement = OUT_OF_BAG;
	} else if (mode.op_mode.ontablestate == 0 || mode.op_mode.inbagstate == 0 ||
		 mode.op_mode.outbagstate == 0) {
		*laptop_placement = LP_UNKNOWN;
		pr_warn_once("Unknown laptop placement\n");
	} else if (mode.op_mode.ontablestate == 3 || mode.op_mode.inbagstate == 3 ||
		 mode.op_mode.outbagstate == 3) {
		*laptop_placement = LP_UNDEFINED;
		pr_warn_once("Undefined laptop placement\n");
	}

	return 0;
}

static int amd_sfh_hpd_info(u8 *user_present)
{
	struct hpd_status hpdstatus;

	if (!user_present)
		return -EINVAL;

	if (!emp2 || !emp2->dev_en.is_hpd_present)
		return -ENODEV;

	hpdstatus.val = readl(emp2->mmio + amd_get_c2p_val(emp2, 4));
	*user_present = hpdstatus.shpd.presence;

	return 0;
}

static int amd_sfh_als_info(u32 *ambient_light)
{
	struct sfh_als_data als_data;
	void __iomem *sensoraddr;

	if (!ambient_light)
		return -EINVAL;

	if (!emp2 || !emp2->dev_en.is_als_present)
		return -ENODEV;

	sensoraddr = emp2->vsbase +
		(ALS_IDX * SENSOR_DATA_MEM_SIZE_DEFAULT) +
		OFFSET_SENSOR_DATA_DEFAULT;
	memcpy_fromio(&als_data, sensoraddr, sizeof(struct sfh_als_data));
	*ambient_light = amd_sfh_float_to_int(als_data.lux);

	return 0;
}

int amd_get_sfh_info(struct amd_sfh_info *sfh_info, enum sfh_message_type op)
{
	if (sfh_info) {
		switch (op) {
		case MT_HPD:
			return amd_sfh_hpd_info(&sfh_info->user_present);
		case MT_ALS:
			return amd_sfh_als_info(&sfh_info->ambient_light);
		case MT_SRA:
			return amd_sfh_mode_info(&sfh_info->platform_type,
						 &sfh_info->laptop_placement);
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(amd_get_sfh_info);
