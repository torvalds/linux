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

#define AMD_SFH_IDLE_LOOP	200

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
		report_size = get_input_report(sensor_index, report_id,
					       cli_data->input_report[current_index],
					       cli_data->sensor_virt_addr[current_index]);
		if (report_size)
			hid_input_report(cli_data->hid_sensor_hubs[current_index],
					 cli_data->report_type[current_index],
					 cli_data->input_report[current_index], report_size, 0);
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
	u8 report_size;
	int i;

	for (i = 0; i < cli_data->num_hid_devices; i++) {
		report_size = get_input_report(cli_data->sensor_idx[i], cli_data->report_id[i],
					       cli_data->input_report[i],
					       cli_data->sensor_virt_addr[i]);
		hid_input_report(cli_data->hid_sensor_hubs[i], HID_INPUT_REPORT,
				 cli_data->input_report[i], report_size, 0);
	}
	schedule_delayed_work(&cli_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
}

int amd_sfh_hid_client_init(struct amd_mp2_dev *privdata)
{
	struct amdtp_cl_data *cl_data = privdata->cl_data;
	struct amd_mp2_sensor_info info;
	struct device *dev;
	u32 feature_report_size;
	u32 input_report_size;
	u8 cl_idx;
	int rc, i;

	dev = &privdata->pdev->dev;
	cl_data = kzalloc(sizeof(*cl_data), GFP_KERNEL);
	if (!cl_data)
		return -ENOMEM;

	cl_data->num_hid_devices = amd_mp2_get_sensor_num(privdata, &cl_data->sensor_idx[0]);

	INIT_DELAYED_WORK(&cl_data->work, amd_sfh_work);
	INIT_DELAYED_WORK(&cl_data->work_buffer, amd_sfh_work_buffer);
	INIT_LIST_HEAD(&req_list.list);

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		cl_data->sensor_virt_addr[i] = dma_alloc_coherent(dev, sizeof(int) * 8,
								  &cl_data->sensor_dma_addr[i],
								  GFP_KERNEL);
		cl_data->sensor_sts[i] = 0;
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
		cl_data->feature_report[i] = kzalloc(feature_report_size, GFP_KERNEL);
		if (!cl_data->feature_report[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}
		cl_data->input_report[i] = kzalloc(input_report_size, GFP_KERNEL);
		if (!cl_data->input_report[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}
		info.period = msecs_to_jiffies(AMD_SFH_IDLE_LOOP);
		info.sensor_idx = cl_idx;
		info.dma_address = cl_data->sensor_dma_addr[i];

		cl_data->report_descr[i] = kzalloc(cl_data->report_descr_sz[i], GFP_KERNEL);
		if (!cl_data->report_descr[i]) {
			rc = -ENOMEM;
			goto cleanup;
		}
		rc = get_report_descriptor(cl_idx, cl_data->report_descr[i]);
		if (rc)
			return rc;
		rc = amdtp_hid_probe(cl_data->cur_hid_dev, cl_data);
		if (rc)
			return rc;
		amd_start_sensor(privdata, info);
		cl_data->sensor_sts[i] = 1;
	}
	privdata->cl_data = cl_data;
	schedule_delayed_work(&cl_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
	return 0;

cleanup:
	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_virt_addr[i]) {
			dma_free_coherent(&privdata->pdev->dev, 8 * sizeof(int),
					  cl_data->sensor_virt_addr[i],
					  cl_data->sensor_dma_addr[i]);
		}
		kfree(cl_data->feature_report[i]);
		kfree(cl_data->input_report[i]);
		kfree(cl_data->report_descr[i]);
	}
	kfree(cl_data);
	return rc;
}

int amd_sfh_hid_client_deinit(struct amd_mp2_dev *privdata)
{
	struct amdtp_cl_data *cl_data = privdata->cl_data;
	int i;

	for (i = 0; i < cl_data->num_hid_devices; i++)
		amd_stop_sensor(privdata, i);

	cancel_delayed_work_sync(&cl_data->work);
	cancel_delayed_work_sync(&cl_data->work_buffer);
	amdtp_hid_remove(cl_data);

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_virt_addr[i]) {
			dma_free_coherent(&privdata->pdev->dev, 8 * sizeof(int),
					  cl_data->sensor_virt_addr[i],
					  cl_data->sensor_dma_addr[i]);
		}
	}
	kfree(cl_data);
	return 0;
}
