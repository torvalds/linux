// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Linaro Ltd.
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 */
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/soc/qcom/wcnss_ctrl.h>

#define WCNSS_REQUEST_TIMEOUT	(5 * HZ)
#define WCNSS_CBC_TIMEOUT	(10 * HZ)

#define WCNSS_ACK_DONE_BOOTING	1
#define WCNSS_ACK_COLD_BOOTING	2

#define NV_FRAGMENT_SIZE	3072
#define NVBIN_FILE		"wlan/prima/WCNSS_qcom_wlan_nv.bin"

/**
 * struct wcnss_ctrl - driver context
 * @dev:	device handle
 * @channel:	SMD channel handle
 * @ack:	completion for outstanding requests
 * @cbc:	completion for cbc complete indication
 * @ack_status:	status of the outstanding request
 * @probe_work: worker for uploading nv binary
 */
struct wcnss_ctrl {
	struct device *dev;
	struct rpmsg_endpoint *channel;

	struct completion ack;
	struct completion cbc;
	int ack_status;

	struct work_struct probe_work;
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
	WCNSS_VBAT_LEVEL_IND,
	WCNSS_BUILD_VERSION_REQ,
	WCNSS_BUILD_VERSION_RESP,
	WCNSS_PM_CONFIG_REQ,
	WCNSS_CBC_COMPLETE_IND,
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

/*
 * struct wcnss_version_resp - version request response
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
 * @rpdev:	remote processor message device pointer
 * @data:	pointer to the incoming data packet
 * @count:	size of the incoming data packet
 * @priv:	unused
 * @addr:	unused
 *
 * Handles any incoming packets from the remote WCNSS_CTRL service.
 */
static int wcnss_ctrl_smd_callback(struct rpmsg_device *rpdev,
				   void *data,
				   int count,
				   void *priv,
				   u32 addr)
{
	struct wcnss_ctrl *wcnss = dev_get_drvdata(&rpdev->dev);
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

		complete(&wcnss->ack);
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
	case WCNSS_CBC_COMPLETE_IND:
		dev_dbg(wcnss->dev, "cold boot complete\n");
		complete(&wcnss->cbc);
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
	int ret;

	msg.type = WCNSS_VERSION_REQ;
	msg.len = sizeof(msg);
	ret = rpmsg_send(wcnss->channel, &msg, sizeof(msg));
	if (ret < 0)
		return ret;

	ret = wait_for_completion_timeout(&wcnss->ack, WCNSS_CBC_TIMEOUT);
	if (!ret) {
		dev_err(wcnss->dev, "timeout waiting for version response\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * wcnss_download_nv() - send nv binary to WCNSS
 * @wcnss:	wcnss_ctrl state handle
 * @expect_cbc:	indicator to caller that an cbc event is expected
 *
 * Returns 0 on success. Negative errno on failure.
 */
static int wcnss_download_nv(struct wcnss_ctrl *wcnss, bool *expect_cbc)
{
	struct wcnss_download_nv_req *req;
	const struct firmware *fw;
	struct device *dev = wcnss->dev;
	const char *nvbin = NVBIN_FILE;
	const void *data;
	ssize_t left;
	int ret;

	req = kzalloc(sizeof(*req) + NV_FRAGMENT_SIZE, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	ret = of_property_read_string(dev->of_node, "firmware-name", &nvbin);
	if (ret < 0 && ret != -EINVAL)
		goto free_req;

	ret = request_firmware(&fw, nvbin, dev);
	if (ret < 0) {
		dev_err(dev, "Failed to load nv file %s: %d\n", nvbin, ret);
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

		ret = rpmsg_send(wcnss->channel, req, req->hdr.len);
		if (ret < 0) {
			dev_err(dev, "failed to send smd packet\n");
			goto release_fw;
		}

		/* Increment for next fragment */
		req->seq++;

		data += NV_FRAGMENT_SIZE;
		left -= NV_FRAGMENT_SIZE;
	} while (left > 0);

	ret = wait_for_completion_timeout(&wcnss->ack, WCNSS_REQUEST_TIMEOUT);
	if (!ret) {
		dev_err(dev, "timeout waiting for nv upload ack\n");
		ret = -ETIMEDOUT;
	} else {
		*expect_cbc = wcnss->ack_status == WCNSS_ACK_COLD_BOOTING;
		ret = 0;
	}

release_fw:
	release_firmware(fw);
free_req:
	kfree(req);

	return ret;
}

/**
 * qcom_wcnss_open_channel() - open additional SMD channel to WCNSS
 * @wcnss:	wcnss handle, retrieved from drvdata
 * @name:	SMD channel name
 * @cb:		callback to handle incoming data on the channel
 * @priv:	private data for use in the call-back
 */
struct rpmsg_endpoint *qcom_wcnss_open_channel(void *wcnss, const char *name, rpmsg_rx_cb_t cb, void *priv)
{
	struct rpmsg_channel_info chinfo;
	struct wcnss_ctrl *_wcnss = wcnss;

	strscpy(chinfo.name, name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = RPMSG_ADDR_ANY;

	return rpmsg_create_ept(_wcnss->channel->rpdev, cb, priv, chinfo);
}
EXPORT_SYMBOL(qcom_wcnss_open_channel);

static void wcnss_async_probe(struct work_struct *work)
{
	struct wcnss_ctrl *wcnss = container_of(work, struct wcnss_ctrl, probe_work);
	bool expect_cbc;
	int ret;

	ret = wcnss_request_version(wcnss);
	if (ret < 0)
		return;

	ret = wcnss_download_nv(wcnss, &expect_cbc);
	if (ret < 0)
		return;

	/* Wait for pending cold boot completion if indicated by the nv downloader */
	if (expect_cbc) {
		ret = wait_for_completion_timeout(&wcnss->cbc, WCNSS_REQUEST_TIMEOUT);
		if (!ret)
			dev_err(wcnss->dev, "expected cold boot completion\n");
	}

	of_platform_populate(wcnss->dev->of_node, NULL, NULL, wcnss->dev);
}

static int wcnss_ctrl_probe(struct rpmsg_device *rpdev)
{
	struct wcnss_ctrl *wcnss;

	wcnss = devm_kzalloc(&rpdev->dev, sizeof(*wcnss), GFP_KERNEL);
	if (!wcnss)
		return -ENOMEM;

	wcnss->dev = &rpdev->dev;
	wcnss->channel = rpdev->ept;

	init_completion(&wcnss->ack);
	init_completion(&wcnss->cbc);
	INIT_WORK(&wcnss->probe_work, wcnss_async_probe);

	dev_set_drvdata(&rpdev->dev, wcnss);

	schedule_work(&wcnss->probe_work);

	return 0;
}

static void wcnss_ctrl_remove(struct rpmsg_device *rpdev)
{
	struct wcnss_ctrl *wcnss = dev_get_drvdata(&rpdev->dev);

	cancel_work_sync(&wcnss->probe_work);
	of_platform_depopulate(&rpdev->dev);
}

static const struct of_device_id wcnss_ctrl_of_match[] = {
	{ .compatible = "qcom,wcnss", },
	{}
};
MODULE_DEVICE_TABLE(of, wcnss_ctrl_of_match);

static struct rpmsg_driver wcnss_ctrl_driver = {
	.probe = wcnss_ctrl_probe,
	.remove = wcnss_ctrl_remove,
	.callback = wcnss_ctrl_smd_callback,
	.drv  = {
		.name  = "qcom_wcnss_ctrl",
		.owner = THIS_MODULE,
		.of_match_table = wcnss_ctrl_of_match,
	},
};

module_rpmsg_driver(wcnss_ctrl_driver);

MODULE_DESCRIPTION("Qualcomm WCNSS control driver");
MODULE_LICENSE("GPL v2");
