// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  AMD SFH Client Layer
 *  Copyright 2020 Advanced Micro Devices, Inc.
 *  Authors: Nehal Bakulchandra Shah <Nehal-Bakulchandra.Shah@amd.com>
 *	     Sandeep Singh <Sandeep.singh@amd.com>
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


struct request_list {
	struct hid_device *hid;
	struct list_head list;
	u8 report_id;
	u8 sensor_idx;
	u8 report_type;
	u8 current_index;
};

static struct request_list req_list;

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
			list_add(&new->list, &req_list.list);
			break;
		}
	}
	schedule_delayed_work(&cli_data->work, 0);
	return 0;
}

static void amd_sfh_work(struct work_struct *work)
{
	struct amdtp_cl_data *cli_data = container_of(work, struct amdtp_cl_data, work.work);
	struct amd_input_data *in_data = cli_data->in_data;
	struct request_list *req_node;
	u8 current_index, sensor_index;
	u8 report_id, node_type;
	u8 report_size = 0;

	req_node = list_last_entry(&req_list.list, struct request_list, list);
	list_del(&req_node->list);
	current_index = req_node->current_index;
	sensor_index = req_node->sensor_idx;
	report_id = req_node->report_id;
	node_type = req_node->report_type;
	kfree(req_node);

	if (node_type == HID_FEATURE_REPORT) {
		report_size = get_feature_report(sensor_index, report_id,
						 cli_data->feature_report[current_index]);
		if (report_size)
			hid_input_report(cli_data->hid_sensor_hubs[current_index],
					 cli_data->report_type[current_index],
					 cli_data->feature_report[current_index], report_size, 0);
		else
			pr_err("AMDSFH: Invalid report size\n");

	} else if (node_type == HID_INPUT_REPORT) {
		report_size = get_input_report(current_index, sensor_index, report_id, in_data);
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

static void amd_sfh_work_buffer(struct work_struct *work)
{
	struct amdtp_cl_data *cli_data = container_of(work, struct amdtp_cl_data, work_buffer.work);
	struct amd_input_data *in_data = cli_data->in_data;
	u8 report_size;
	int i;

	for (i = 0; i < cli_data->num_hid_devices; i++) {
		if (cli_data->sensor_sts[i] == SENSOR_ENABLED) {
			report_size = get_input_report
				(i, cli_data->sensor_idx[i], cli_data->report_id[i], in_data);
			hid_input_report(cli_data->hid_sensor_hubs[i], HID_INPUT_REPORT,
					 in_data->input_report[i], report_size, 0);
		}
	}
	schedule_delayed_work(&cli_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
}

u32 amd_sfh_wait_for_response(struct amd_mp2_dev *mp2, u8 sid, u32 sensor_sts)
{
	if (mp2->mp2_ops->response)
		sensor_sts = mp2->mp2_ops->response(mp2, sid, sensor_sts);

	return sensor_sts;
}

int amd_sfh_hid_client_init(struct amd_mp2_dev *privdata)
{
	struct amd_input_data *in_data = &privdata->in_data;
	struct amdtp_cl_data *cl_data = privdata->cl_data;
	struct amd_mp2_sensor_info info;
	struct device *dev;
	u32 feature_report_size;
	u32 input_report_size;
	int rc, i, status;
	u8 cl_idx;

	dev = &privdata->pdev->dev;

	cl_data->num_hid_devices = amd_mp2_get_sensor_num(privdata, &cl_data->sensor_idx[0]);

	INIT_DELAYED_WORK(&cl_data->work, amd_sfh_work);
	INIT_DELAYED_WORK(&cl_data->work_buffer, amd_sfh_work_buffer);
	INIT_LIST_HEAD(&req_list.list);
	cl_data->in_data = in_data;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		in_data->sensor_virt_addr[i] = dma_alloc_coherent(dev, sizeof(int) * 8,
								  &cl_data->sensor_dma_addr[i],
								  GFP_KERNEL);
		cl_data->sensor_sts[i] = SENSOR_DISABLED;
		cl_data->sensor_requested_cnt[i] = 0;
		cl_data->cur_hid_dev = i;
		cl_idx = cl_data->sensor_idx[i];
		cl_data->report_descr_sz[i] = get_descr_sz(cl_idx, descr_size);
		if (!cl_data->report_descr_sz[i]) {
			rc = -EINVAL;
			goto cleanup;
		}
		feature_report_size = get_descr_sz(cl_idx, feature_size);
		if (!feature_report_size) {
			rc = -EINVAL;
			goto cleanup;
		}
		input_report_size =  get_descr_sz(cl_idx, input_size);
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
		rc = get_report_descriptor(cl_idx, cl_data->report_descr[i]);
		if (rc)
			return rc;
		privdata->mp2_ops->start(privdata, info);
		status = amd_sfh_wait_for_response
				(privdata, cl_data->sensor_idx[i], SENSOR_ENABLED);
		if (status == SENSOR_ENABLED) {
			cl_data->sensor_sts[i] = SENSOR_ENABLED;
			rc = amdtp_hid_probe(cl_data->cur_hid_dev, cl_data);
			if (rc) {
				privdata->mp2_ops->stop(privdata, cl_data->sensor_idx[i]);
				status = amd_sfh_wait_for_response
					(privdata, cl_data->sensor_idx[i], SENSOR_DISABLED);
				if (status != SENSOR_ENABLED)
					cl_data->sensor_sts[i] = SENSOR_DISABLED;
				dev_dbg(dev, "sid 0x%x status 0x%x\n",
					cl_data->sensor_idx[i], cl_data->sensor_sts[i]);
				goto cleanup;
			}
		}
		dev_dbg(dev, "sid 0x%x status 0x%x\n",
			cl_data->sensor_idx[i], cl_data->sensor_sts[i]);
	}
	if (privdata->mp2_ops->discovery_status &&
	    privdata->mp2_ops->discovery_status(privdata) == 0) {
		amd_sfh_hid_client_deinit(privdata);
		for (i = 0; i < cl_data->num_hid_devices; i++) {
			devm_kfree(dev, cl_data->feature_report[i]);
			devm_kfree(dev, in_data->input_report[i]);
			devm_kfree(dev, cl_data->report_descr[i]);
		}
		dev_warn(dev, "Failed to discover, sensors not enabled\n");
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
			dev_dbg(&privdata->pdev->dev, "stopping sid 0x%x status 0x%x\n",
				cl_data->sensor_idx[i], cl_data->sensor_sts[i]);
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
