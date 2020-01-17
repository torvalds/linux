// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"BATTERY_DBG: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/soc/qcom/pmic_glink.h>

/* owner/type/opcode for battery debug */
#define MSG_OWNER_BD		32781
#define MSG_TYPE_REQ_RESP	1
#define BD_QBG_DUMP_REQ		0x36

/* Generic definitions */
#define MAX_BUF_LEN		(560 * sizeof(u32))
#define BD_WAIT_TIME_MS		1000

struct qbg_context_req_msg {
	struct pmic_glink_hdr hdr;
};

struct qbg_context_resp_msg {
	struct pmic_glink_hdr		hdr;
	u32				length;
	u8				buf[MAX_BUF_LEN];
};

struct battery_dbg_dev {
	struct device			*dev;
	struct pmic_glink_client	*client;
	struct mutex			lock;
	struct completion		ack;
	struct qbg_context_resp_msg	qbg_dump;
	struct dentry			*debugfs_dir;
	struct debugfs_blob_wrapper	qbg_blob;
};

static int battery_dbg_write(struct battery_dbg_dev *bd, void *data, size_t len)
{
	int rc;

	mutex_lock(&bd->lock);
	reinit_completion(&bd->ack);
	rc = pmic_glink_write(bd->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bd->ack,
					msecs_to_jiffies(BD_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			mutex_unlock(&bd->lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bd->lock);

	return rc;
}

static void handle_qbg_dump_message(struct battery_dbg_dev *bd,
				    struct qbg_context_resp_msg *resp_msg,
				    size_t len)
{
	u32 buf_len;

	if (len > sizeof(bd->qbg_dump)) {
		pr_err("Incorrect length received: %zu expected: %u\n", len,
			sizeof(bd->qbg_dump));
		return;
	}

	buf_len = resp_msg->length;
	if (buf_len > sizeof(bd->qbg_dump.buf)) {
		pr_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}

	pr_debug("buf length: %u\n", buf_len);
	memcpy(bd->qbg_dump.buf, resp_msg->buf, buf_len);
	bd->qbg_blob.size = buf_len;

	complete(&bd->ack);
}

static int battery_dbg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct battery_dbg_dev *bd = priv;

	pr_debug("owner: %u type: %u opcode: %#x len: %zu\n", hdr->owner,
		hdr->type, hdr->opcode, len);

	switch (hdr->opcode) {
	case BD_QBG_DUMP_REQ:
		handle_qbg_dump_message(bd, data, len);
		break;
	default:
		pr_err("Unknown opcode %u\n", hdr->opcode);
		break;
	}

	return 0;
}

static int get_qbg_context_write(void *data, u64 val)
{
	struct battery_dbg_dev *bd = data;
	struct qbg_context_req_msg req_msg = { { 0 } };

	req_msg.hdr.owner = MSG_OWNER_BD;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BD_QBG_DUMP_REQ;

	return battery_dbg_write(bd, &req_msg, sizeof(req_msg));
}

DEFINE_DEBUGFS_ATTRIBUTE(get_qbg_context_debugfs_ops, NULL,
			get_qbg_context_write, "%llu\n");

static int battery_dbg_add_debugfs(struct battery_dbg_dev *bd)
{
	struct dentry *bd_dir, *file;

	bd_dir = debugfs_create_dir("battery_debug", NULL);
	if (!bd_dir) {
		pr_err("Failed to create battery debugfs directory\n");
		return -ENOMEM;
	}

	file = debugfs_create_file_unsafe("get_qbg_context", 0200, bd_dir, bd,
				&get_qbg_context_debugfs_ops);
	if (!file) {
		pr_err("Failed to create get_qbg_context debugfs file\n");
		goto error;
	}

	bd->qbg_blob.data = bd->qbg_dump.buf;
	bd->qbg_blob.size = 0;
	file = debugfs_create_blob("qbg_context", 0444, bd_dir, &bd->qbg_blob);
	if (!file) {
		pr_err("Failed to create qbg_context debugfs file\n");
		goto error;
	}

	bd->debugfs_dir = bd_dir;

	return 0;
error:
	debugfs_remove_recursive(bd_dir);
	return -ENOMEM;
}

static int battery_dbg_probe(struct platform_device *pdev)
{
	struct battery_dbg_dev *bd;
	struct pmic_glink_client_data client_data = { };
	int rc;

	bd = devm_kzalloc(&pdev->dev, sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	bd->dev = &pdev->dev;
	client_data.id = MSG_OWNER_BD;
	client_data.name = "battery_debug";
	client_data.msg_cb = battery_dbg_callback;
	client_data.priv = bd;

	bd->client = pmic_glink_register_client(bd->dev, &client_data);
	if (IS_ERR(bd->client)) {
		rc = PTR_ERR(bd->client);
		if (rc != -EPROBE_DEFER)
			dev_err(bd->dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	mutex_init(&bd->lock);
	init_completion(&bd->ack);
	platform_set_drvdata(pdev, bd);

	rc = battery_dbg_add_debugfs(bd);
	if (rc < 0)
		goto out;

	return 0;
out:
	pmic_glink_unregister_client(bd->client);
	return rc;
}

static int battery_dbg_remove(struct platform_device *pdev)
{
	struct battery_dbg_dev *bd = platform_get_drvdata(pdev);
	int rc;

	debugfs_remove_recursive(bd->debugfs_dir);
	rc = pmic_glink_unregister_client(bd->client);
	if (rc < 0) {
		pr_err("Error unregistering from pmic_glink, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static const struct of_device_id battery_dbg_match_table[] = {
	{ .compatible = "qcom,battery-debug" },
	{},
};

static struct platform_driver battery_dbg_driver = {
	.driver	= {
		.name = "qti_battery_debug",
		.of_match_table = battery_dbg_match_table,
	},
	.probe	= battery_dbg_probe,
	.remove	= battery_dbg_remove,
};
module_platform_driver(battery_dbg_driver);

MODULE_DESCRIPTION("QTI Glink battery debug driver");
MODULE_LICENSE("GPL v2");
