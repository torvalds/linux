// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/rpmsg.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/workqueue.h>
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
	MSG_SETUP,
	MSG_SETUP_ACK,
	MSG_MAX
};

struct virtio_glink_bridge_msg {
	__virtio32 type;
	__virtio32 label;
	__virtio32 address;
	__virtio32 size;
};

struct virtio_glink_bridge_rsp {
	__virtio32 type;
	__virtio32 label;
	__virtio32 status;
};

struct virtio_glink_bridge_dsp_info {
	int label;
	const char *label_str;
	struct device_node *np;

	struct glink_cma_config config;
	struct qcom_glink *glink;

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

static void virtio_glink_bridge_rx_work(struct work_struct *work)
{
	struct virtio_glink_bridge *vgbridge = container_of(work, struct virtio_glink_bridge,
								rx_work);
	struct virtio_glink_bridge_dsp_info *dsp_info;
	struct virtio_device *vdev = vgbridge->vdev;
	struct virtio_glink_bridge_msg *msg;
	struct virtio_glink_bridge_rsp *rsp;
	struct device *dev = &vdev->dev;
	struct glink_cma_config *config;
	u32 type, label, address, size;
	bool dsp_found = false;
	struct scatterlist sg;
	unsigned int len;
	int rc;

	msg = (struct virtio_glink_bridge_msg *)virtqueue_get_buf(vgbridge->vq, &len);
	if (!msg || len != sizeof(*msg)) {
		dev_err(dev, "fail to get virtqueue buffer\n");
		label = VIRTIO_GLINK_BRIDGE_NO_LABEL;
		rc = VIRTIO_GLINK_BRIDGE_EINVAL;
		goto out;
	}

	type = virtio32_to_cpu(vdev, msg->type);
	label = virtio32_to_cpu(vdev, msg->label);
	address = virtio32_to_cpu(vdev, msg->address);
	size = virtio32_to_cpu(vdev, msg->size);

	list_for_each_entry(dsp_info, &vgbridge->dsp_infos, node) {
		if (dsp_info->label == label) {
			dsp_found = true;
			break;
		}
	}
	if (!dsp_found) {
		dev_err(dev, "fail to find dsp_info\n");
		rc = VIRTIO_GLINK_BRIDGE_ENODEV;
		goto out;
	}
	if (dsp_info->glink) {
		dev_err(dev, "DSP already registered\n");
		rc = VIRTIO_GLINK_BRIDGE_EINVAL;
		goto out;
	}
	config = &dsp_info->config;

	config->base = devm_memremap(dev, address, size, MEMREMAP_WC);
	if (IS_ERR(config->base)) {
		dev_err(dev, "memremap fail\n");
		config->base = NULL;
		rc = VIRTIO_GLINK_BRIDGE_ENOMEM;
		goto out;
	}
	config->size = size;

	dsp_info->glink = qcom_glink_cma_register(dev, dsp_info->np, config);
	if (IS_ERR(dsp_info->glink)) {
		dev_err(dev, "fail to register with GLINK CMA core\n");
		dsp_info->glink = NULL;
		rc = VIRTIO_GLINK_BRIDGE_EINVAL;
		goto out;
	}

	rc = VIRTIO_GLINK_BRIDGE_SUCCESS;
out:
	rsp = vgbridge->buf;
	rsp->type = cpu_to_virtio32(vdev, MSG_SETUP_ACK);
	rsp->label = cpu_to_virtio32(vdev, label);
	rsp->status = cpu_to_virtio32(vdev, rc);
	sg_init_one(&sg, rsp, sizeof(*rsp));

	/* BE will hold on to the buffer for the next message to send */
	rc = virtqueue_add_inbuf(vgbridge->vq, &sg, 1, rsp, GFP_KERNEL);
	if (rc)
		dev_err(dev, "fail to add input buffer\n");

	virtqueue_kick(vgbridge->vq);
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

		dsp_info->np = child_np;
		list_add_tail(&dsp_info->node, &vgbridge->dsp_infos);
	}
out:

	return rc;
}

static int virtio_glink_bridge_probe(struct virtio_device *vdev)
{
	struct virtio_glink_bridge *vgbridge;
	struct virtio_glink_bridge_msg *msg;
	struct device *dev = &vdev->dev;
	struct scatterlist sg[1];
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

	msg = vgbridge->buf;
	msg->type = MSG_SETUP;
	sg_init_one(sg, msg, sizeof(*msg));
	rc = virtqueue_add_inbuf(vgbridge->vq, sg, 1, msg, GFP_KERNEL);
	if (rc) {
		dev_err(dev, "fail to add to input buffer\n");
		goto err;
	}

	virtqueue_kick(vgbridge->vq);

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
