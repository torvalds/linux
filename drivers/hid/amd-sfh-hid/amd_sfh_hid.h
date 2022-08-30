/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD MP2 Sensors transport driver
 *
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
 * Authors: Nehal Bakulchandra Shah <Nehal-bakulchandra.shah@amd.com>
 *	    Sandeep Singh <sandeep.singh@amd.com>
 *	    Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#ifndef AMDSFH_HID_H
#define AMDSFH_HID_H

#define MAX_HID_DEVICES		5
#define AMD_SFH_HID_VENDOR	0x1022
#define AMD_SFH_HID_PRODUCT	0x0001

struct request_list {
	struct hid_device *hid;
	struct list_head list;
	u8 report_id;
	u8 sensor_idx;
	u8 report_type;
	u8 current_index;
};

struct amd_input_data {
	u32 *sensor_virt_addr[MAX_HID_DEVICES];
	u8 *input_report[MAX_HID_DEVICES];
};

struct amdtp_cl_data {
	u8 init_done;
	u32 cur_hid_dev;
	u32 hid_dev_count;
	u32 num_hid_devices;
	struct device_info *hid_devices;
	u8  *report_descr[MAX_HID_DEVICES];
	int report_descr_sz[MAX_HID_DEVICES];
	struct hid_device *hid_sensor_hubs[MAX_HID_DEVICES];
	u8 *hid_descr[MAX_HID_DEVICES];
	int hid_descr_size[MAX_HID_DEVICES];
	phys_addr_t phys_addr_base;
	dma_addr_t sensor_dma_addr[MAX_HID_DEVICES];
	u32 sensor_sts[MAX_HID_DEVICES];
	u32 sensor_requested_cnt[MAX_HID_DEVICES];
	u8 report_type[MAX_HID_DEVICES];
	u8 report_id[MAX_HID_DEVICES];
	u8 sensor_idx[MAX_HID_DEVICES];
	u8 *feature_report[MAX_HID_DEVICES];
	u8 request_done[MAX_HID_DEVICES];
	struct amd_input_data *in_data;
	struct delayed_work work;
	struct delayed_work work_buffer;
	struct request_list req_list;
};

/**
 * struct amdtp_hid_data - Per instance HID data
 * @index:		Device index in the order of enumeration
 * @request_done:	Get Feature/Input report complete flag
 *			used during get/set request from hid core
 * @cli_data:		Link to the client instance
 * @hid_wait:		Completion waitq
 *
 * Used to tie hid->driver data to driver client instance
 */
struct amdtp_hid_data {
	int index;
	struct amdtp_cl_data *cli_data;
	wait_queue_head_t hid_wait;
};

/* Interface functions between HID LL driver and AMD SFH client */
void hid_amdtp_set_feature(struct hid_device *hid, char *buf, u32 len, int report_id);
void hid_amdtp_get_report(struct hid_device *hid, int report_id, int report_type);
int amdtp_hid_probe(u32 cur_hid_dev, struct amdtp_cl_data *cli_data);
void amdtp_hid_remove(struct amdtp_cl_data *cli_data);
int amd_sfh_get_report(struct hid_device *hid, int report_id, int report_type);
void amd_sfh_set_report(struct hid_device *hid, int report_id, int report_type);
void amdtp_hid_wakeup(struct hid_device *hid);
#endif
