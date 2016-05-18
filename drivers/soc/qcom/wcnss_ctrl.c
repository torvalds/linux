/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smd.h>

#define WCNSS_REQUEST_TIMEOUT	(5 * HZ)

#define NV_FRAGMENT_SIZE	3072
#define NVBIN_FILE		"wlan/prima/WCNSS_qcom_wlan_nv.bin"

/**
 * struct wcnss_ctrl - driver context
 * @dev:	device handle
 * @channel:	SMD channel handle
 * @ack:	completion for outstanding requests
 * @ack_status:	status of the outstanding request
 * @download_nv_work: worker for uploading nv binary
 */
struct wcnss_ctrl {
	struct device *dev;
	struct qcom_smd_channel *channel;

	struct completion ack;
	int ack_status;

	struct work_struct download_nv_work;
};

/* message types */
enum {
	WCNSS_VERSION_REQ = 0x01000000,
	WCNSS_VERSION_RESP,
	WCNSS_DOWNLOAD_NV_REQ,
	WCNSS_DOWNLOAD_NV_RESP,
	WCNSS_UPLOAD_CAL_REQ,
	WCNSS_UPLOAD_CAL_RESP,
	WCNSS_DOWNLOAD_CAL_REQ,
	WCNSS_DOWNLOAD_CAL_RESP,
};

/**
 * struct wcnss_msg_hdr - common packet header for requests and responses
 * @type:	packet message type
 * @len:	total length of the packet, including this header
 */
struct wcnss_msg_hdr {
	u32 type;
	u32 len;
} __packed;

/**
 * struct wcnss_version_resp - version request response
 * @hdr:	common packet wcnss_msg_hdr header
 */
struct wcnss_version_resp {
	struct wcnss_msg_hdr hdr;
	u8 major;
	u8 minor;
	u8 version;
	u8 revision;
} __packed;

/**
 * struct wcnss_download_nv_req - firmware fragment request
 * @hdr:	common packet wcnss_msg_hdr header
 * @seq:	sequence number of this fragment
 * @last:	boolean indicator of this being the last fragment of the binary
 * @frag_size:	length of this fragment
 * @fragment:	fragment data
 */
struct wcnss_download_nv_req {
	struct wcnss_msg_hdr hdr;
	u16 seq;
	u16 last;
	u32 frag_size;
	u8 fragment[];
} __packed;

/**
 * struct wcnss_download_nv_resp - firmware download response
 * @hdr:	common packet wcnss_msg_hdr header
 * @status:	boolean to indicate success of the download
 */
struct wcnss_download_nv_resp {
	struct wcnss_msg_hdr hdr;
	u8 status;
} __packed;

/**
 * wcnss_ctrl_smd_callback() - handler from SMD responses
 * @channel:	smd channel handle
 * @data:	pointer to the incoming data packet
 * @count:	size of the incoming data packet
 *
 * Handles any incoming packets from the remote WCNSS_CTRL service.
 */
static int wcnss_ctrl_smd_callback(struct qcom_smd_channel *channel,
				   const void *data,
				   size_t count)
{
	struct wcnss_ctrl *wcnss = qcom_smd_get_drvdata(channel);
	const struct wcnss_download_nv_resp *nvresp;
	const struct wcnss_version_resp *version;
	const struct wcnss_msg_hdr *hdr = data;

	switch (hdr->type) {
	case WCNSS_VERSION_RESP:
		if (count != sizeof(*version)) {
			dev_err(wcnss->dev,
				"invalid size of version response\n");
			break;
		}

		version = data;
		dev_info(wcnss->dev, "WCNSS Version %d.%d %d.%d\n",
			 version->major, version->minor,
			 version->version, version->revision);

		schedule_work(&wcnss->download_nv_work);
		break;
	case WCNSS_DOWNLOAD_NV_RESP:
		if (count != sizeof(*nvresp)) {
			dev_err(wcnss->dev,
				"invalid size of download response\n");
			break;
		}

		nvresp = data;
		wcnss->ack_status = nvresp->status;
		complete(&wcnss->ack);
		break;
	default:
		dev_info(wcnss->dev, "unknown message type %d\n", hdr->type);
		break;
	}

	return 0;
}

/**
 * wcnss_request_version() - send a version request to WCNSS
 * @wcnss:	wcnss ctrl driver context
 */
static int wcnss_request_version(struct wcnss_ctrl *wcnss)
{
	struct wcnss_msg_hdr msg;

	msg.type = WCNSS_VERSION_REQ;
	msg.len = sizeof(msg);

	return qcom_smd_send(wcnss->channel, &msg, sizeof(msg));
}

/**
 * wcnss_download_nv() - send nv binary to WCNSS
 * @work:	work struct to acquire wcnss context
 */
static void wcnss_download_nv(struct work_struct *work)
{
	struct wcnss_ctrl *wcnss = container_of(work, struct wcnss_ctrl, download_nv_work);
	struct wcnss_download_nv_req *req;
	const struct firmware *fw;
	const void *data;
	ssize_t left;
	int ret;

	req = kzalloc(sizeof(*req) + NV_FRAGMENT_SIZE, GFP_KERNEL);
	if (!req)
		return;

	ret = request_firmware(&fw, NVBIN_FILE, wcnss->dev);
	if (ret) {
		dev_err(wcnss->dev, "Failed to load nv file %s: %d\n",
			NVBIN_FILE, ret);
		goto free_req;
	}

	data = fw->data;
	left = fw->size;

	req->hdr.type = WCNSS_DOWNLOAD_NV_REQ;
	req->hdr.len = sizeof(*req) + NV_FRAGMENT_SIZE;

	req->last = 0;
	req->frag_size = NV_FRAGMENT_SIZE;

	req->seq = 0;
	do {
		if (left <= NV_FRAGMENT_SIZE) {
			req->last = 1;
			req->frag_size = left;
			req->hdr.len = sizeof(*req) + left;
		}

		memcpy(req->fragment, data, req->frag_size);

		ret = qcom_smd_send(wcnss->channel, req, req->hdr.len);
		if (ret) {
			dev_err(wcnss->dev, "failed to send smd packet\n");
			goto release_fw;
		}

		/* Increment for next fragment */
		req->seq++;

		data += req->hdr.len;
		left -= NV_FRAGMENT_SIZE;
	} while (left > 0);

	ret = wait_for_completion_timeout(&wcnss->ack, WCNSS_REQUEST_TIMEOUT);
	if (!ret)
		dev_err(wcnss->dev, "timeout waiting for nv upload ack\n");
	else if (wcnss->ack_status != 1)
		dev_err(wcnss->dev, "nv upload response failed err: %d\n",
			wcnss->ack_status);

release_fw:
	release_firmware(fw);
free_req:
	kfree(req);
}

static int wcnss_ctrl_probe(struct qcom_smd_device *sdev)
{
	struct wcnss_ctrl *wcnss;

	wcnss = devm_kzalloc(&sdev->dev, sizeof(*wcnss), GFP_KERNEL);
	if (!wcnss)
		return -ENOMEM;

	wcnss->dev = &sdev->dev;
	wcnss->channel = sdev->channel;

	init_completion(&wcnss->ack);
	INIT_WORK(&wcnss->download_nv_work, wcnss_download_nv);

	qcom_smd_set_drvdata(sdev->channel, wcnss);

	return wcnss_request_version(wcnss);
}

static const struct qcom_smd_id wcnss_ctrl_smd_match[] = {
	{ .name = "WCNSS_CTRL" },
	{}
};

static struct qcom_smd_driver wcnss_ctrl_driver = {
	.probe = wcnss_ctrl_probe,
	.callback = wcnss_ctrl_smd_callback,
	.smd_match_table = wcnss_ctrl_smd_match,
	.driver  = {
		.name  = "qcom_wcnss_ctrl",
		.owner = THIS_MODULE,
	},
};

module_qcom_smd_driver(wcnss_ctrl_driver);

MODULE_DESCRIPTION("Qualcomm WCNSS control driver");
MODULE_LICENSE("GPL v2");
