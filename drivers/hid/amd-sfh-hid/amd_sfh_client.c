// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  AMD SFH Client Layer
 *  Copyright 2020-2021 Advanced Micro Devices, Inc.
 *  Authors: Nehal Bakulchandra Shah <Nehal-Bakulchandra.Shah@amd.com>
 *	     Sandeep Singh <Sandeep.singh@amd.com>
 *	     Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#include <linux/dma-mapping.h>
#include <linux/hid.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/errno.h>

#include "hid_descriptor/amd_sfh_hid_desc.h"
#include "amd_sfh_pcie.h"
#include "amd_sfh_hid.h"

void amd_sfh_set_report(struct hid_device *hid, int report_id,
			int report_type)
{
	struct amdtp_hid_data *hid_data = hid->driver_data;
	struct amdtp_cl_data *cli_data = hid_data->cli_data;
	int i;

	for (i = 0; i < cli_data->num_hid_devices; i++) {
		if (cli_data->hid_sensor_hubs[i] == hid) {
			cli_data->cur_hid_dev = i;
			break;
		}
	}
	amdtp_hid_wakeup(hid);
}

int amd_sfh_get_report(struct hid_device *hid, int report_id, int report_type)
{
	struct amdtp_hid_data *hid_data = hid->driver_data;
	struct amdtp_cl_data *cli_data = hid_data->cli_data;
	struct request_list *req_list = &cli_data->req_list;
	int i;

	for (i = 0; i < cli_data->num_hid_devices; i++) {
		if (cli_data->hid_sensor_hubs[i] == hid) {
			struct request_list *new = kzalloc(sizeof(*new), GFP_KERNEL);

			if (!new)
				return -ENOMEM;

			new->current_index = i;
			new->sensor_idx = cli_data->sensor_idx[i];
			new->hid = hid;
			new->report_type = report_type;
			new->report_id = report_id;
			cli_data->report_id[i] = report_id;
			cli_data->request_done[i] = false;
			list_add(&new->list, &req_list->list);
			break;
		}
	}
	schedule_delayed_work(&cli_data->work, 0);
	return 0;
}

void amd_sfh_work(struct work_struct *work)
{
	struct amdtp_cl_data *cli_data = container_of(work, struct amdtp_cl_data, work.work);
	struct request_list *req_list = &cli_data->req_list;
	struct amd_input_data *in_data = cli_data->in_data;
	struct request_list *req_node;
	u8 current_index, sensor_index;
	struct amd_mp2_ops *mp2_ops;
	struct amd_mp2_dev *mp2;
	u8 report_id, node_type;
	u8 report_size = 0;

	req_node = list_last_entry(&req_list->list, struct request_list, list);
	list_del(&req_node->list);
	current_index = req_node->current_index;
	sensor_index = req_node->sensor_idx;
	report_id = req_node->report_id;
	node_type = req_node->report_type;
	kfree(req_node);

	mp2 = container_of(in_data, struct amd_mp2_dev, in_data);
	mp2_ops = mp2->mp2_ops;
	if (node_type == HID_FEATURE_REPORT) {
		report_size = mp2_ops->get_feat_rep(sensor_index, report_id,
						    cli_data->feature_report[current_index]);
		if (report_size)
			hid_input_report(cli_data->hid_sensor_hubs[current_index],
					 cli_data->report_type[current_index],
					 cli_data->feature_report[current_index], report_size, 0);
		else
			pr_err("AMDSFH: Invalid report size\n");

	} else if (node_type == HID_INPUT_REPORT) {
		report_size = mp2_ops->get_in_rep(current_index, sensor_index, report_id, in_data);
		if (report_size)
			hid_input_report(cli_data->hid_sensor_hubs[current_index],
					 cli_data->report_type[current_index],
					 in_data->input_report[current_index], report_size, 0);
		else
			pr_err("AMDSFH: Invalid report size\n");
	}
	cli_data->cur_hid_dev = current_index;
	cli_data->sensor_requested_cnt[current_index] = 0;
	amdtp_hid_wakeup(cli_data->hid_sensor_hubs[current_index]);
}

void amd_sfh_work_buffer(struct work_struct *work)
{
	struct amdtp_cl_data *cli_data = container_of(work, struct amdtp_cl_data, work_buffer.work);
	struct amd_input_data *in_data = cli_data->in_data;
	struct amd_mp2_dev *mp2;
	u8 report_size;
	int i;

	for (i = 0; i < cli_data->num_hid_devices; i++) {
		if (cli_data->sensor_sts[i] == SENSOR_ENABLED) {
			mp2 = container_of(in_data, struct amd_mp2_dev, in_data);
			report_size = mp2->mp2_ops->get_in_rep(i, cli_data->sensor_idx[i],
							       cli_data->report_id[i], in_data);
			hid_input_report(cli_data->hid_sensor_hubs[i], HID_INPUT_REPORT,
					 in_data->input_report[i], report_size, 0);
		}
	}
	schedule_delayed_work(&cli_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
}

static u32 amd_sfh_wait_for_response(struct amd_mp2_dev *mp2, u8 sid, u32 sensor_sts)
{
	if (mp2->mp2_ops->response)
		sensor_sts = mp2->mp2_ops->response(mp2, sid, sensor_sts);

	return sensor_sts;
}

static const char *get_sensor_name(int idx)
{
	switch (idx) {
	case accel_idx:
		return "accelerometer";
	case gyro_idx:
		return "gyroscope";
	case mag_idx:
		return "magnetometer";
	case als_idx:
	case ACS_IDX: /* ambient color sensor */
		return "ALS";
	case HPD_IDX:
		return "HPD";
	default:
		return "unknown sensor type";
	}
}

static void amd_sfh_resume(struct amd_mp2_dev *mp2)
{
	struct amdtp_cl_data *cl_data = mp2->cl_data;
	struct amd_mp2_sensor_info info;
	int i, status;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_sts[i] == SENSOR_DISABLED) {
			info.period = AMD_SFH_IDLE_LOOP;
			info.sensor_idx = cl_data->sensor_idx[i];
			info.dma_address = cl_data->sensor_dma_addr[i];
			mp2->mp2_ops->start(mp2, info);
			status = amd_sfh_wait_for_response
					(mp2, cl_data->sensor_idx[i], SENSOR_ENABLED);
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
					(mp2, cl_data->sensor_idx[i], SENSOR_DISABLED);
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

int amd_sfh_hid_client_init(struct amd_mp2_dev *privdata)
{
	struct amd_input_data *in_data = &privdata->in_data;
	struct amdtp_cl_data *cl_data = privdata->cl_data;
	struct amd_mp2_ops *mp2_ops = privdata->mp2_ops;
	struct amd_mp2_sensor_info info;
	struct request_list *req_list;
	struct device *dev;
	u32 feature_report_size;
	u32 input_report_size;
	int rc, i, status;
	u8 cl_idx;

	req_list = &cl_data->req_list;
	dev = &privdata->pdev->dev;
	amd_sfh_set_desc_ops(mp2_ops);

	mp2_ops->suspend = amd_sfh_suspend;
	mp2_ops->resume = amd_sfh_resume;

	cl_data->num_hid_devices = amd_mp2_get_sensor_num(privdata, &cl_data->sensor_idx[0]);
	if (cl_data->num_hid_devices == 0)
		return -ENODEV;
	cl_data->is_any_sensor_enabled = false;

	INIT_DELAYED_WORK(&cl_data->work, amd_sfh_work);
	INIT_DELAYED_WORK(&cl_data->work_buffer, amd_sfh_work_buffer);
	INIT_LIST_HEAD(&req_list->list);
	cl_data->in_data = in_data;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		in_data->sensor_virt_addr[i] = dma_alloc_coherent(dev, sizeof(int) * 8,
								  &cl_data->sensor_dma_addr[i],
								  GFP_KERNEL);
		if (!in_data->sensor_virt_addr[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}
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
		info.period = AMD_SFH_IDLE_LOOP;
		info.sensor_idx = cl_idx;
		info.dma_address = cl_data->sensor_dma_addr[i];

		cl_data->report_descr[i] =
			devm_kzalloc(dev, cl_data->report_descr_sz[i], GFP_KERNEL);
		if (!cl_data->report_descr[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}
		rc = mp2_ops->get_rep_desc(cl_idx, cl_data->report_descr[i]);
		if (rc)
			goto cleanup;
		mp2_ops->start(privdata, info);
		status = amd_sfh_wait_for_response
				(privdata, cl_data->sensor_idx[i], SENSOR_ENABLED);
		if (status == SENSOR_ENABLED) {
			cl_data->is_any_sensor_enabled = true;
			cl_data->sensor_sts[i] = SENSOR_ENABLED;
			rc = amdtp_hid_probe(cl_data->cur_hid_dev, cl_data);
			if (rc) {
				mp2_ops->stop(privdata, cl_data->sensor_idx[i]);
				status = amd_sfh_wait_for_response
					(privdata, cl_data->sensor_idx[i], SENSOR_DISABLED);
				if (status != SENSOR_ENABLED)
					cl_data->sensor_sts[i] = SENSOR_DISABLED;
				dev_dbg(dev, "sid 0x%x (%s) status 0x%x\n",
					cl_data->sensor_idx[i],
					get_sensor_name(cl_data->sensor_idx[i]),
					cl_data->sensor_sts[i]);
				goto cleanup;
			}
		} else {
			cl_data->sensor_sts[i] = SENSOR_DISABLED;
			dev_dbg(dev, "sid 0x%x (%s) status 0x%x\n",
				cl_data->sensor_idx[i],
				get_sensor_name(cl_data->sensor_idx[i]),
				cl_data->sensor_sts[i]);
		}
		dev_dbg(dev, "sid 0x%x (%s) status 0x%x\n",
			cl_data->sensor_idx[i], get_sensor_name(cl_data->sensor_idx[i]),
			cl_data->sensor_sts[i]);
	}
	if (!cl_data->is_any_sensor_enabled ||
	   (mp2_ops->discovery_status && mp2_ops->discovery_status(privdata) == 0)) {
		amd_sfh_hid_client_deinit(privdata);
		for (i = 0; i < cl_data->num_hid_devices; i++) {
			devm_kfree(dev, cl_data->feature_report[i]);
			devm_kfree(dev, in_data->input_report[i]);
			devm_kfree(dev, cl_data->report_descr[i]);
		}
		dev_warn(dev, "Failed to discover, sensors not enabled is %d\n", cl_data->is_any_sensor_enabled);
		return -EOPNOTSUPP;
	}
	schedule_delayed_work(&cl_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
	return 0;

cleanup:
	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (in_data->sensor_virt_addr[i]) {
			dma_free_coherent(&privdata->pdev->dev, 8 * sizeof(int),
					  in_data->sensor_virt_addr[i],
					  cl_data->sensor_dma_addr[i]);
		}
		devm_kfree(dev, cl_data->feature_report[i]);
		devm_kfree(dev, in_data->input_report[i]);
		devm_kfree(dev, cl_data->report_descr[i]);
	}
	return rc;
}

int amd_sfh_hid_client_deinit(struct amd_mp2_dev *privdata)
{
	struct amdtp_cl_data *cl_data = privdata->cl_data;
	struct amd_input_data *in_data = cl_data->in_data;
	int i, status;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_sts[i] == SENSOR_ENABLED) {
			privdata->mp2_ops->stop(privdata, cl_data->sensor_idx[i]);
			status = amd_sfh_wait_for_response
					(privdata, cl_data->sensor_idx[i], SENSOR_DISABLED);
			if (status != SENSOR_ENABLED)
				cl_data->sensor_sts[i] = SENSOR_DISABLED;
			dev_dbg(&privdata->pdev->dev, "stopping sid 0x%x (%s) status 0x%x\n",
				cl_data->sensor_idx[i], get_sensor_name(cl_data->sensor_idx[i]),
				cl_data->sensor_sts[i]);
		}
	}

	cancel_delayed_work_sync(&cl_data->work);
	cancel_delayed_work_sync(&cl_data->work_buffer);
	amdtp_hid_remove(cl_data);

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (in_data->sensor_virt_addr[i]) {
			dma_free_coherent(&privdata->pdev->dev, 8 * sizeof(int),
					  in_data->sensor_virt_addr[i],
					  cl_data->sensor_dma_addr[i]);
		}
	}
	return 0;
}
