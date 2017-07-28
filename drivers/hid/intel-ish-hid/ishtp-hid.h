/*
 * ISHTP-HID glue driver's definitions.
 *
 * Copyright (c) 2014-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */
#ifndef ISHTP_HID__H
#define	ISHTP_HID__H

/* The fixed ISH product and vendor id */
#define	ISH_HID_VENDOR	0x8086
#define	ISH_HID_PRODUCT	0x22D8
#define	ISH_HID_VERSION	0x0200

#define	CMD_MASK	0x7F
#define	IS_RESPONSE	0x80

/* Used to dump to Linux trace buffer, if enabled */
#define hid_ishtp_trace(client, ...)	\
	client->cl_device->ishtp_dev->print_log(\
		client->cl_device->ishtp_dev, __VA_ARGS__)

/* ISH Transport protocol (ISHTP in short) GUID */
static const uuid_le hid_ishtp_guid = UUID_LE(0x33AECD58, 0xB679, 0x4E54,
					      0x9B, 0xD9, 0xA0, 0x4D, 0x34,
					      0xF0, 0xC2, 0x26);

/* ISH HID message structure */
struct hostif_msg_hdr {
	uint8_t	command; /* Bit 7: is_response */
	uint8_t	device_id;
	uint8_t	status;
	uint8_t	flags;
	uint16_t size;
} __packed;

struct hostif_msg {
	struct hostif_msg_hdr	hdr;
} __packed;

struct hostif_msg_to_sensor {
	struct hostif_msg_hdr	hdr;
	uint8_t	report_id;
} __packed;

struct device_info {
	uint32_t dev_id;
	uint8_t dev_class;
	uint16_t pid;
	uint16_t vid;
} __packed;

struct ishtp_version {
	uint8_t	major;
	uint8_t	minor;
	uint8_t	hotfix;
	uint16_t build;
} __packed;

/* struct for ISHTP aggregated input data */
struct report_list {
	uint16_t total_size;
	uint8_t	num_of_reports;
	uint8_t	flags;
	struct {
		uint16_t	size_of_report;
		uint8_t report[1];
	} __packed reports[1];
} __packed;

/* HOSTIF commands */
#define	HOSTIF_HID_COMMAND_BASE			0
#define	HOSTIF_GET_HID_DESCRIPTOR		0
#define	HOSTIF_GET_REPORT_DESCRIPTOR		1
#define HOSTIF_GET_FEATURE_REPORT		2
#define	HOSTIF_SET_FEATURE_REPORT		3
#define	HOSTIF_GET_INPUT_REPORT			4
#define	HOSTIF_PUBLISH_INPUT_REPORT		5
#define	HOSTIF_PUBLISH_INPUT_REPORT_LIST	6
#define	HOSTIF_DM_COMMAND_BASE			32
#define	HOSTIF_DM_ENUM_DEVICES			33
#define	HOSTIF_DM_ADD_DEVICE			34

#define	MAX_HID_DEVICES				32

/**
 * struct ishtp_cl_data - Encapsulate per ISH TP HID Client
 * @enum_device_done:	Enum devices response complete flag
 * @hid_descr_done:	HID descriptor complete flag
 * @report_descr_done:	Get report descriptor complete flag
 * @init_done:		Init process completed successfully
 * @suspended:		System is under suspend state or in progress
 * @num_hid_devices:	Number of HID devices enumerated in this client
 * @cur_hid_dev:	This keeps track of the device index for which
 *			initialization and registration with HID core
 *			in progress.
 * @hid_devices:	Store vid/pid/devid for each enumerated HID device
 * @report_descr:	Stores the raw report descriptors for each HID device
 * @report_descr_size:	Report description of size of above repo_descr[]
 * @hid_sensor_hubs:	Pointer to hid_device for all HID device, so that
 *			when clients are removed, they can be freed
 * @hid_descr:		Pointer to hid descriptor for each enumerated hid
 *			device
 * @hid_descr_size:	Size of each above report descriptor
 * @init_wait:		Wait queue to wait during initialization, where the
 *			client send message to ISH FW and wait for response
 * @ishtp_hid_wait:	The wait for get report during wait callback from hid
 *			core
 * @bad_recv_cnt:	Running count of packets received with error
 * @multi_packet_cnt:	Count of fragmented packet count
 *
 * This structure is used to store completion flags and per client data like
 * like report description, number of HID devices etc.
 */
struct ishtp_cl_data {
	/* completion flags */
	bool enum_devices_done;
	bool hid_descr_done;
	bool report_descr_done;
	bool init_done;
	bool suspended;

	unsigned int num_hid_devices;
	unsigned int cur_hid_dev;
	unsigned int hid_dev_count;

	struct device_info *hid_devices;
	unsigned char *report_descr[MAX_HID_DEVICES];
	int report_descr_size[MAX_HID_DEVICES];
	struct hid_device *hid_sensor_hubs[MAX_HID_DEVICES];
	unsigned char *hid_descr[MAX_HID_DEVICES];
	int hid_descr_size[MAX_HID_DEVICES];

	wait_queue_head_t init_wait;
	wait_queue_head_t ishtp_resume_wait;
	struct ishtp_cl *hid_ishtp_cl;

	/* Statistics */
	unsigned int bad_recv_cnt;
	int multi_packet_cnt;

	struct work_struct work;
	struct ishtp_cl_device *cl_device;
};

/**
 * struct ishtp_hid_data - Per instance HID data
 * @index:		Device index in the order of enumeration
 * @request_done:	Get Feature/Input report complete flag
 *			used during get/set request from hid core
 * @client_data:	Link to the client instance
 * @hid_wait:		Completion waitq
 *
 * Used to tie hid hid->driver data to driver client instance
 */
struct ishtp_hid_data {
	int index;
	bool request_done;
	struct ishtp_cl_data *client_data;
	wait_queue_head_t hid_wait;
};

/* Interface functions between HID LL driver and ISH TP client */
void hid_ishtp_set_feature(struct hid_device *hid, char *buf, unsigned int len,
			   int report_id);
void hid_ishtp_get_report(struct hid_device *hid, int report_id,
			  int report_type);
int ishtp_hid_probe(unsigned int cur_hid_dev,
		    struct ishtp_cl_data *client_data);
void ishtp_hid_remove(struct ishtp_cl_data *client_data);
int ishtp_hid_link_ready_wait(struct ishtp_cl_data *client_data);
void ishtp_hid_wakeup(struct hid_device *hid);

#endif	/* ISHTP_HID__H */
