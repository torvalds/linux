// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP2 1.1 communication driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#include <linux/delay.h>
#include <linux/hid.h>

#include "amd_sfh_init.h"
#include "amd_sfh_interface.h"
#include "../hid_descriptor/amd_sfh_hid_desc.h"

static int amd_sfh_get_sensor_num(struct amd_mp2_dev *mp2, u8 *sensor_id)
{
	struct sfh_sensor_list *slist;
	struct sfh_base_info binfo;
	int num_of_sensors = 0;
	int i;

	memcpy_fromio(&binfo, mp2->vsbase, sizeof(struct sfh_base_info));
	slist = &binfo.sbase.s_list;

	for (i = 0; i < MAX_IDX; i++) {
		switch (i) {
		case ACCEL_IDX:
		case GYRO_IDX:
		case MAG_IDX:
		case ALS_IDX:
		case HPD_IDX:
			if (BIT(i) & slist->sl.sensors)
				sensor_id[num_of_sensors++] = i;
			break;
		}
	}

	return num_of_sensors;
}

static u32 amd_sfh_wait_for_response(struct amd_mp2_dev *mp2, u8 sid, u32 cmd_id)
{
	if (mp2->mp2_ops->response)
		return mp2->mp2_ops->response(mp2, sid, cmd_id);

	return 0;
}

static const char *get_sensor_name(int idx)
{
	switch (idx) {
	case ACCEL_IDX:
		return "accelerometer";
	case GYRO_IDX:
		return "gyroscope";
	case MAG_IDX:
		return "magnetometer";
	case ALS_IDX:
		return "ALS";
	case HPD_IDX:
		return "HPD";
	default:
		return "unknown sensor type";
	}
}

static int amd_sfh_hid_client_deinit(struct amd_mp2_dev *privdata)
{
	struct amdtp_cl_data *cl_data = privdata->cl_data;
	int i, status;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_sts[i] == SENSOR_ENABLED) {
			privdata->mp2_ops->stop(privdata, cl_data->sensor_idx[i]);
			status = amd_sfh_wait_for_response
					(privdata, cl_data->sensor_idx[i], DISABLE_SENSOR);
			if (status == 0)
				cl_data->sensor_sts[i] = SENSOR_DISABLED;
			dev_dbg(&privdata->pdev->dev, "stopping sid 0x%x (%s) status 0x%x\n",
				cl_data->sensor_idx[i], get_sensor_name(cl_data->sensor_idx[i]),
				cl_data->sensor_sts[i]);
		}
	}

	cancel_delayed_work_sync(&cl_data->work);
	cancel_delayed_work_sync(&cl_data->work_buffer);
	amdtp_hid_remove(cl_data);

	return 0;
}

static int amd_sfh1_1_hid_client_init(struct amd_mp2_dev *privdata)
{
	struct amd_input_data *in_data = &privdata->in_data;
	struct amdtp_cl_data *cl_data = privdata->cl_data;
	struct amd_mp2_ops *mp2_ops = privdata->mp2_ops;
	struct amd_mp2_sensor_info info;
	struct request_list *req_list;
	u32 feature_report_size;
	u32 input_report_size;
	struct device *dev;
	int rc, i, status;
	u8 cl_idx;

	req_list = &cl_data->req_list;
	dev = &privdata->pdev->dev;
	amd_sfh1_1_set_desc_ops(mp2_ops);

	cl_data->num_hid_devices = amd_sfh_get_sensor_num(privdata, &cl_data->sensor_idx[0]);
	if (cl_data->num_hid_devices == 0)
		return -ENODEV;
	cl_data->is_any_sensor_enabled = false;

	INIT_DELAYED_WORK(&cl_data->work, amd_sfh_work);
	INIT_DELAYED_WORK(&cl_data->work_buffer, amd_sfh_work_buffer);
	INIT_LIST_HEAD(&req_list->list);
	cl_data->in_data = in_data;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		cl_data->sensor_sts[i] = SENSOR_DISABLED;
		cl_data->sensor_requested_cnt[i] = 0;
		cl_data->cur_hid_dev = i;
		cl_idx = cl_data->sensor_idx[i];

		cl_data->report_descr_sz[i] = mp2_ops->get_desc_sz(cl_idx, descr_size);
		if (!cl_data->report_descr_sz[i]) {
			rc = -EINVAL;
			goto cleanup;
		}
		feature_report_size = mp2_ops->get_desc_sz(cl_idx, feature_size);
		if (!feature_report_size) {
			rc = -EINVAL;
			goto cleanup;
		}
		input_report_size =  mp2_ops->get_desc_sz(cl_idx, input_size);
		if (!input_report_size) {
			rc = -EINVAL;
			goto cleanup;
		}
		cl_data->feature_report[i] = devm_kzalloc(dev, feature_report_size, GFP_KERNEL);
		if (!cl_data->feature_report[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}
		in_data->input_report[i] = devm_kzalloc(dev, input_report_size, GFP_KERNEL);
		if (!in_data->input_report[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}

		info.sensor_idx = cl_idx;

		cl_data->report_descr[i] =
			devm_kzalloc(dev, cl_data->report_descr_sz[i], GFP_KERNEL);
		if (!cl_data->report_descr[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}
		rc = mp2_ops->get_rep_desc(cl_idx, cl_data->report_descr[i]);
		if (rc)
			goto cleanup;

		writel(0, privdata->mmio + AMD_P2C_MSG(0));
		mp2_ops->start(privdata, info);
		status = amd_sfh_wait_for_response
				(privdata, cl_data->sensor_idx[i], ENABLE_SENSOR);

		cl_data->sensor_sts[i] = (status == 0) ? SENSOR_ENABLED : SENSOR_DISABLED;
	}

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		cl_data->cur_hid_dev = i;
		if (cl_data->sensor_sts[i] == SENSOR_ENABLED) {
			cl_data->is_any_sensor_enabled = true;
			rc = amdtp_hid_probe(i, cl_data);
			if (rc)
				goto cleanup;
		}
		dev_dbg(dev, "sid 0x%x (%s) status 0x%x\n",
			cl_data->sensor_idx[i], get_sensor_name(cl_data->sensor_idx[i]),
			cl_data->sensor_sts[i]);
	}

	if (!cl_data->is_any_sensor_enabled) {
		dev_warn(dev, "Failed to discover, sensors not enabled is %d\n",
			 cl_data->is_any_sensor_enabled);
		rc = -EOPNOTSUPP;
		goto cleanup;
	}

	schedule_delayed_work(&cl_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
	return 0;

cleanup:
	amd_sfh_hid_client_deinit(privdata);
	for (i = 0; i < cl_data->num_hid_devices; i++) {
		devm_kfree(dev, cl_data->feature_report[i]);
		devm_kfree(dev, in_data->input_report[i]);
		devm_kfree(dev, cl_data->report_descr[i]);
	}
	return rc;
}

static void amd_sfh_resume(struct amd_mp2_dev *mp2)
{
	struct amdtp_cl_data *cl_data = mp2->cl_data;
	struct amd_mp2_sensor_info info;
	int i, status;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_sts[i] == SENSOR_DISABLED) {
			info.sensor_idx = cl_data->sensor_idx[i];
			mp2->mp2_ops->start(mp2, info);
			status = amd_sfh_wait_for_response
					(mp2, cl_data->sensor_idx[i], ENABLE_SENSOR);
			if (status == 0)
				status = SENSOR_ENABLED;
			if (status == SENSOR_ENABLED)
				cl_data->sensor_sts[i] = SENSOR_ENABLED;
			dev_dbg(&mp2->pdev->dev, "resume sid 0x%x (%s) status 0x%x\n",
				cl_data->sensor_idx[i], get_sensor_name(cl_data->sensor_idx[i]),
				cl_data->sensor_sts[i]);
		}
	}

	schedule_delayed_work(&cl_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
	amd_sfh_clear_intr(mp2);
}

static void amd_sfh_suspend(struct amd_mp2_dev *mp2)
{
	struct amdtp_cl_data *cl_data = mp2->cl_data;
	int i, status;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_idx[i] != HPD_IDX &&
		    cl_data->sensor_sts[i] == SENSOR_ENABLED) {
			mp2->mp2_ops->stop(mp2, cl_data->sensor_idx[i]);
			status = amd_sfh_wait_for_response
					(mp2, cl_data->sensor_idx[i], DISABLE_SENSOR);
			if (status == 0)
				status = SENSOR_DISABLED;
			if (status != SENSOR_ENABLED)
				cl_data->sensor_sts[i] = SENSOR_DISABLED;
			dev_dbg(&mp2->pdev->dev, "suspend sid 0x%x (%s) status 0x%x\n",
				cl_data->sensor_idx[i], get_sensor_name(cl_data->sensor_idx[i]),
				cl_data->sensor_sts[i]);
		}
	}

	cancel_delayed_work_sync(&cl_data->work_buffer);
	amd_sfh_clear_intr(mp2);
}

static void amd_mp2_pci_remove(void *privdata)
{
	struct amd_mp2_dev *mp2 = privdata;

	amd_sfh_hid_client_deinit(privdata);
	mp2->mp2_ops->stop_all(mp2);
	pci_intx(mp2->pdev, false);
	amd_sfh_clear_intr(mp2);
}

static void amd_sfh_set_ops(struct amd_mp2_dev *mp2)
{
	struct amd_mp2_ops *mp2_ops;

	sfh_interface_init(mp2);
	mp2_ops = mp2->mp2_ops;
	mp2_ops->clear_intr = amd_sfh_clear_intr_v2,
	mp2_ops->init_intr = amd_sfh_irq_init_v2,
	mp2_ops->suspend = amd_sfh_suspend;
	mp2_ops->resume = amd_sfh_resume;
	mp2_ops->remove = amd_mp2_pci_remove;
}

int amd_sfh1_1_init(struct amd_mp2_dev *mp2)
{
	u32 phy_base = readl(mp2->mmio + AMD_C2P_MSG(22));
	struct device *dev = &mp2->pdev->dev;
	struct sfh_base_info binfo;
	int rc;

	phy_base <<= 21;
	if (!devm_request_mem_region(dev, phy_base, 128 * 1024, "amd_sfh")) {
		dev_dbg(dev, "can't reserve mmio registers\n");
		return -ENOMEM;
	}

	mp2->vsbase = devm_ioremap(dev, phy_base, 128 * 1024);
	if (!mp2->vsbase) {
		dev_dbg(dev, "failed to remap vsbase\n");
		return -ENOMEM;
	}

	/* Before accessing give time for SFH firmware for processing configuration */
	msleep(5000);

	memcpy_fromio(&binfo, mp2->vsbase, sizeof(struct sfh_base_info));
	if (binfo.sbase.fw_info.fw_ver == 0 || binfo.sbase.s_list.sl.sensors == 0) {
		dev_dbg(dev, "failed to get sensors\n");
		return -EOPNOTSUPP;
	}
	dev_dbg(dev, "firmware version 0x%x\n", binfo.sbase.fw_info.fw_ver);

	amd_sfh_set_ops(mp2);

	rc = amd_sfh_irq_init(mp2);
	if (rc) {
		dev_err(dev, "amd_sfh_irq_init failed\n");
		return rc;
	}

	rc = amd_sfh1_1_hid_client_init(mp2);
	if (rc) {
		dev_err(dev, "amd_sfh1_1_hid_client_init failed\n");
		return rc;
	}

	return rc;
}
