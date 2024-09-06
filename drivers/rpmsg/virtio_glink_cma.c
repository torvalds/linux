// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/rpmsg.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/workqueue.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rpmsg/qcom_glink.h>

#include "qcom_glink_cma.h"

#define VIRTIO_GLINK_BRIDGE_SUCCESS (0)
#define VIRTIO_GLINK_BRIDGE_ENOMEM (-1)
#define VIRTIO_GLINK_BRIDGE_ENODEV (-2)
#define VIRTIO_GLINK_BRIDGE_EINVAL (-3)

#define VIRTIO_GLINK_BRIDGE_NO_LABEL (0xff)

/* 0xC00A  */
#define VIRTIO_ID_GLINK_BRIDGE (49162)

enum {
	CDSP0,
	CDSP1,
	DSP_MAX,
	DSP_ERR = 0xff
};

static const char * const to_dsp_str[DSP_MAX] = {
	[CDSP0] = "cdsp",
	[CDSP1] = "cdsp1",
};

#define DSP_LABEL_TO_STR(dsp) (((dsp) >= DSP_MAX) ? "INVALID DSP" : to_dsp_str[(dsp)])

enum {
	MSG_SETUP,		/* inbound / outbound */
	MSG_SETUP_ACK,		/* outbound */
	MSG_SSR_AFTER_POWERUP,	/* outbound */
	MSG_SSR_SETUP,		/* inbound */
	MSG_SSR_SETUP_ACK,	/* outbound */
	MSG_INBUF_RECLAIM,	/* inbound */
	MSG_MAX,
	MSG_ERR = 0xff,
};

struct virtio_glink_bridge_msg {
	__virtio32 type;
	__virtio32 label;
	__virtio32 address;
	__virtio32 size;
};

struct virtio_glink_bridge_msg_ack {
	__virtio32 type;
	__virtio32 label;
	__virtio32 status;
};

struct virtio_glink_bridge_dsp_info {
	int label;
	const char *label_str;
	struct device_node *np;
	struct virtio_glink_bridge *vgbridge;

	struct glink_cma_config config;
	struct qcom_glink *glink;

	struct notifier_block nb;
	void *notifier_handle;
	struct mutex ssr_lock;

	struct list_head node;
};

struct virtio_glink_bridge {
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct list_head dsp_infos;
	struct work_struct rx_work;
	void *buf;
};

static int virtio_glink_bridge_dsp_str_to_label(const char * const label_str)
{
	int i;

	for (i = CDSP0; i < DSP_MAX; i++) {
		if (!strcmp(label_str, DSP_LABEL_TO_STR(i)))
			return i;
	}

	return -EINVAL;
}

static int virtio_glink_bridge_to_msg_ack_type(u32 msg_type)
{
	switch (msg_type) {
	case MSG_SETUP:
		return MSG_SETUP_ACK;
	case MSG_SSR_SETUP:
		return MSG_SSR_SETUP_ACK;
	default:
		return MSG_ERR;
	}
}

static int virtio_glink_bridge_msg_type_supported(u32 msg_type)
{
	switch (msg_type) {
	case MSG_SETUP:
	case MSG_SSR_SETUP:
	case MSG_INBUF_RECLAIM:
		return true;
	default:
		return false;
	}
}

static int virtio_glink_bridge_send_msg(struct virtio_glink_bridge *vgbridge,
					u32 msg_type, u32 label)
{
	struct virtio_device *vdev = vgbridge->vdev;
	struct virtio_glink_bridge_msg *msg;
	struct scatterlist sg;
	int rc;

	msg = vgbridge->buf;
	memset(msg, 0, sizeof(*msg));
	msg->type = cpu_to_virtio32(vdev, msg_type);
	msg->label = cpu_to_virtio32(vdev, label);
	sg_init_one(&sg, msg, sizeof(struct virtio_glink_bridge_msg));

	rc = virtqueue_add_inbuf(vgbridge->vq, &sg, 1, msg, GFP_KERNEL);
	if (rc) {
		dev_err(&vdev->dev, "fail to add input buffer\n");
		return rc;
	}

	virtqueue_kick(vgbridge->vq);

	return 0;
}

static int virtio_glink_bridge_send_msg_ack(struct virtio_glink_bridge *vgbridge,
						u32 msg_type, u32 label, int status)
{
	struct virtio_device *vdev = vgbridge->vdev;
	struct virtio_glink_bridge_msg_ack *ack;
	struct scatterlist sg;
	int rc;

	ack = vgbridge->buf;
	memset(ack, 0, sizeof(*ack));
	ack->type = cpu_to_virtio32(vdev, msg_type);
	ack->label = cpu_to_virtio32(vdev, label);
	ack->status = cpu_to_virtio32(vdev, status);
	sg_init_one(&sg, ack, sizeof(struct virtio_glink_bridge_msg));

	rc = virtqueue_add_inbuf(vgbridge->vq, &sg, 1, ack, GFP_KERNEL);
	if (rc) {
		dev_err(&vdev->dev, "fail to add input buffer\n");
		return rc;
	}

	virtqueue_kick(vgbridge->vq);

	return 0;
}

static void virtio_glink_bridge_ssr_after_powerup(struct virtio_glink_bridge_dsp_info *dsp_info)
{
	virtio_glink_bridge_send_msg(dsp_info->vgbridge, MSG_SSR_AFTER_POWERUP, dsp_info->label);
}

static void virtio_glink_bridge_ssr_after_shutdown(struct virtio_glink_bridge_dsp_info *dsp_info)
{
	qcom_glink_cma_unregister(dsp_info->glink);
	dsp_info->glink = NULL;
}

static int virtio_glink_bridge_ssr_cb(struct notifier_block *nb,
					unsigned long state, void *data)
{
	struct virtio_glink_bridge_dsp_info *dsp_info;
	struct device *dev;

	dsp_info = container_of(nb, struct virtio_glink_bridge_dsp_info, nb);

	mutex_lock(&dsp_info->ssr_lock);
	dev = &dsp_info->vgbridge->vdev->dev;

	dev_info(dev, "received cb state %ld for %s\n", state, dsp_info->label);

	switch (state) {
	case QCOM_SSR_AFTER_POWERUP:
		virtio_glink_bridge_ssr_after_powerup(dsp_info);
		break;
	case QCOM_SSR_AFTER_SHUTDOWN:
		virtio_glink_bridge_ssr_after_shutdown(dsp_info);
		break;
	default:
		break;
	}
	mutex_unlock(&dsp_info->ssr_lock);

	return NOTIFY_DONE;
}

static struct virtio_glink_bridge_dsp_info *virtio_glink_bridge_get_dsp_info(
							struct virtio_glink_bridge *vgbridge,
							u32 label)
{
	struct virtio_glink_bridge_dsp_info *dsp_info;

	list_for_each_entry(dsp_info, &vgbridge->dsp_infos, node) {
		if (dsp_info->label == label)
			return dsp_info;
	}

	return NULL;
}

static void virtio_glink_bridge_rx_work(struct work_struct *work)
{
	struct virtio_glink_bridge *vgbridge = container_of(work, struct virtio_glink_bridge,
								rx_work);
	struct virtio_glink_bridge_dsp_info *dsp_info;
	struct virtio_device *vdev = vgbridge->vdev;
	struct virtio_glink_bridge_msg *msg;
	struct device *dev = &vdev->dev;
	struct glink_cma_config *config;
	u32 msg_type, msg_ack_type;
	u32 label, address, size;
	unsigned int len;
	void *handle;
	int rc;

	msg = (struct virtio_glink_bridge_msg *)virtqueue_get_buf(vgbridge->vq, &len);
	if (!msg || len != sizeof(*msg)) {
		dev_err(dev, "fail to get virtqueue buffer\n");
		msg_ack_type = MSG_ERR;
		label = VIRTIO_GLINK_BRIDGE_NO_LABEL;
		rc = VIRTIO_GLINK_BRIDGE_EINVAL;
		goto out;
	}

	label = virtio32_to_cpu(vdev, msg->label);
	msg_type = virtio32_to_cpu(vdev, msg->type);
	if (!virtio_glink_bridge_msg_type_supported(msg_type)) {
		dev_err(dev, "unsupported msg type %u\n", msg_type);
		msg_ack_type = MSG_ERR;
		rc = VIRTIO_GLINK_BRIDGE_EINVAL;
		goto out;
	}

	if (msg_type == MSG_INBUF_RECLAIM)
		return;

	msg_ack_type = virtio_glink_bridge_to_msg_ack_type(msg_type);

	dsp_info = virtio_glink_bridge_get_dsp_info(vgbridge, label);
	if (!dsp_info) {
		dev_err(dev, "fail to find dsp_info\n");
		rc = VIRTIO_GLINK_BRIDGE_ENODEV;
		goto out;
	}

	mutex_lock(&dsp_info->ssr_lock);

	switch (msg_type) {
	case MSG_SETUP:
		if (dsp_info->glink) {
			dev_err(dev, "DSP already registered\n");
			rc = VIRTIO_GLINK_BRIDGE_EINVAL;
			goto unlock;
		}

		dsp_info->vgbridge = vgbridge;

		address = virtio32_to_cpu(vdev, msg->address);
		size = virtio32_to_cpu(vdev, msg->size);

		config = &dsp_info->config;

		config->base = devm_memremap(dev, address, size, MEMREMAP_WC);
		if (IS_ERR(config->base)) {
			dev_err(dev, "memremap fail\n");
			config->base = NULL;
			rc = VIRTIO_GLINK_BRIDGE_ENOMEM;
			goto unlock;
		}
		config->size = size;

		dsp_info->glink = qcom_glink_cma_register(dev, dsp_info->np, config);
		if (IS_ERR(dsp_info->glink)) {
			dev_err(dev, "fail to register with GLINK CMA core\n");
			dsp_info->glink = NULL;
			rc = VIRTIO_GLINK_BRIDGE_EINVAL;
			goto unlock;
		}

		dsp_info->nb.notifier_call = virtio_glink_bridge_ssr_cb;

		handle = qcom_register_ssr_notifier(dsp_info->label_str, &dsp_info->nb);
		if (IS_ERR_OR_NULL(handle)) {
			dev_err(dev, "fail to register with SSR notifier for %s\n",
				dsp_info->label);
			rc = VIRTIO_GLINK_BRIDGE_EINVAL;
			goto unlock;
		}

		dsp_info->notifier_handle = handle;
		break;
	case MSG_SSR_SETUP:
		dsp_info->glink = qcom_glink_cma_register(dev, dsp_info->np, &dsp_info->config);
		if (IS_ERR(dsp_info->glink)) {
			dev_err(dev, "fail to register with GLINK CMA core\n");
			dsp_info->glink = NULL;
			rc = VIRTIO_GLINK_BRIDGE_EINVAL;
			goto unlock;
		}
		break;
	}

	rc = VIRTIO_GLINK_BRIDGE_SUCCESS;
unlock:
	virtio_glink_bridge_send_msg_ack(vgbridge, msg_ack_type, label, rc);
	mutex_unlock(&dsp_info->ssr_lock);
	return;
out:
	virtio_glink_bridge_send_msg_ack(vgbridge, msg_ack_type, label, rc);
}

static void virtio_glink_bridge_isr(struct virtqueue *vq)
{
	struct virtio_glink_bridge *vgbridge = vq->vdev->priv;

	schedule_work(&vgbridge->rx_work);
}

static int virtio_glink_bridge_init_vqs(struct virtio_glink_bridge *vgbridge)
{
	vgbridge->vq = virtio_find_single_vq(vgbridge->vdev, virtio_glink_bridge_isr,
						"glink_bridge");

	return PTR_ERR_OR_ZERO(vgbridge->vq);
}

static int virtio_glink_bridge_of_parse(struct virtio_glink_bridge *vgbridge)
{
	struct virtio_glink_bridge_dsp_info *dsp_info;
	struct device *dev = &vgbridge->vdev->dev;
	struct device_node *parent_np, *child_np;
	int rc = 0;

	parent_np = dev->parent->of_node->child;

	for_each_child_of_node(parent_np, child_np) {
		if (of_find_property(child_np, "compatible", NULL))
			continue;

		dsp_info = devm_kzalloc(dev, sizeof(*dsp_info), GFP_KERNEL);
		if (!dsp_info) {
			rc = -ENOMEM;
			goto out;
		}

		rc = of_property_read_string(child_np, "label", &dsp_info->label_str);
		if (rc)
			goto out;

		dsp_info->label = virtio_glink_bridge_dsp_str_to_label(dsp_info->label_str);
		if (dsp_info->label < 0) {
			rc = -EINVAL;
			goto out;
		}

		mutex_init(&dsp_info->ssr_lock);
		dsp_info->np = child_np;
		list_add_tail(&dsp_info->node, &vgbridge->dsp_infos);
	}
out:

	return rc;
}

static int virtio_glink_bridge_probe(struct virtio_device *vdev)
{
	struct virtio_glink_bridge *vgbridge;
	struct device *dev = &vdev->dev;
	int rc;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	vgbridge = devm_kzalloc(dev, sizeof(struct virtio_glink_bridge), GFP_KERNEL);
	if (!vgbridge)
		return -ENOMEM;

	vgbridge->buf = devm_kzalloc(dev, sizeof(struct virtio_glink_bridge_msg), GFP_KERNEL);
	if (!vgbridge->buf)
		return -ENOMEM;

	vdev->priv = vgbridge;
	vgbridge->vdev = vdev;
	INIT_LIST_HEAD(&vgbridge->dsp_infos);
	INIT_WORK(&vgbridge->rx_work, virtio_glink_bridge_rx_work);

	rc = virtio_glink_bridge_of_parse(vgbridge);
	if (rc) {
		dev_err(dev, "fail to set up dsp_infos %d\n", rc);
		return rc;
	}

	rc = virtio_glink_bridge_init_vqs(vgbridge);
	if (rc) {
		dev_err(dev, "fail to initialize virtqueue %d\n", rc);
		return rc;
	}

	virtio_device_ready(vdev);

	rc = virtio_glink_bridge_send_msg(vgbridge, MSG_SETUP, VIRTIO_GLINK_BRIDGE_NO_LABEL);
	if (rc)
		goto err;

	return 0;
err:
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	return rc;
}

static void virtio_glink_bridge_remove(struct virtio_device *vdev)
{
	struct virtio_glink_bridge *vgbridge = vdev->priv;
	struct virtio_glink_bridge_dsp_info *dsp_info;

	list_for_each_entry(dsp_info, &vgbridge->dsp_infos, node) {
		qcom_glink_cma_unregister(dsp_info->glink);
		dsp_info->glink = NULL;
	}

	cancel_work_sync(&vgbridge->rx_work);

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GLINK_BRIDGE, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
};

static struct virtio_driver virtio_glink_bridge_driver = {
	.feature_table			= features,
	.feature_table_size		= ARRAY_SIZE(features),
	.driver.name			= KBUILD_MODNAME,
	.driver.owner			= THIS_MODULE,
	.id_table			= id_table,
	.probe				= virtio_glink_bridge_probe,
	.remove				= virtio_glink_bridge_remove,
};

module_virtio_driver(virtio_glink_bridge_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio glink cma driver");
MODULE_LICENSE("GPL");
