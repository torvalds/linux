// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2019 Google LLC.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/rpmsg/mtk_rpmsg.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "rpmsg_internal.h"

struct mtk_rpmsg_rproc_subdev {
	struct platform_device *pdev;
	struct mtk_rpmsg_info *info;
	struct rpmsg_endpoint *ns_ept;
	struct rproc_subdev subdev;

	struct work_struct register_work;
	struct list_head channels;
	struct mutex channels_lock;
};

#define to_mtk_subdev(d) container_of(d, struct mtk_rpmsg_rproc_subdev, subdev)

struct mtk_rpmsg_channel_info {
	struct rpmsg_channel_info info;
	bool registered;
	struct list_head list;
};

/**
 * struct rpmsg_ns_msg - dynamic name service announcement message
 * @name: name of remote service that is published
 * @addr: address of remote service that is published
 *
 * This message is sent across to publish a new service. When we receive these
 * messages, an appropriate rpmsg channel (i.e device) is created. In turn, the
 * ->probe() handler of the appropriate rpmsg driver will be invoked
 *  (if/as-soon-as one is registered).
 */
struct rpmsg_ns_msg {
	char name[RPMSG_NAME_SIZE];
	u32 addr;
} __packed;

struct mtk_rpmsg_device {
	struct rpmsg_device rpdev;
	struct mtk_rpmsg_rproc_subdev *mtk_subdev;
};

struct mtk_rpmsg_endpoint {
	struct rpmsg_endpoint ept;
	struct mtk_rpmsg_rproc_subdev *mtk_subdev;
};

#define to_mtk_rpmsg_device(r) container_of(r, struct mtk_rpmsg_device, rpdev)
#define to_mtk_rpmsg_endpoint(r) container_of(r, struct mtk_rpmsg_endpoint, ept)

static const struct rpmsg_endpoint_ops mtk_rpmsg_endpoint_ops;

static void __mtk_ept_release(struct kref *kref)
{
	struct rpmsg_endpoint *ept = container_of(kref, struct rpmsg_endpoint,
						  refcount);
	kfree(to_mtk_rpmsg_endpoint(ept));
}

static void mtk_rpmsg_ipi_handler(void *data, unsigned int len, void *priv)
{
	struct mtk_rpmsg_endpoint *mept = priv;
	struct rpmsg_endpoint *ept = &mept->ept;
	int ret;

	ret = (*ept->cb)(ept->rpdev, data, len, ept->priv, ept->addr);
	if (ret)
		dev_warn(&ept->rpdev->dev, "rpmsg handler return error = %d",
			 ret);
}

static struct rpmsg_endpoint *
__mtk_create_ept(struct mtk_rpmsg_rproc_subdev *mtk_subdev,
		 struct rpmsg_device *rpdev, rpmsg_rx_cb_t cb, void *priv,
		 u32 id)
{
	struct mtk_rpmsg_endpoint *mept;
	struct rpmsg_endpoint *ept;
	struct platform_device *pdev = mtk_subdev->pdev;
	int ret;

	mept = kzalloc(sizeof(*mept), GFP_KERNEL);
	if (!mept)
		return NULL;
	mept->mtk_subdev = mtk_subdev;

	ept = &mept->ept;
	kref_init(&ept->refcount);

	ept->rpdev = rpdev;
	ept->cb = cb;
	ept->priv = priv;
	ept->ops = &mtk_rpmsg_endpoint_ops;
	ept->addr = id;

	ret = mtk_subdev->info->register_ipi(pdev, id, mtk_rpmsg_ipi_handler,
					     mept);
	if (ret) {
		dev_err(&pdev->dev, "IPI register failed, id = %d", id);
		kref_put(&ept->refcount, __mtk_ept_release);
		return NULL;
	}

	return ept;
}

static struct rpmsg_endpoint *
mtk_rpmsg_create_ept(struct rpmsg_device *rpdev, rpmsg_rx_cb_t cb, void *priv,
		     struct rpmsg_channel_info chinfo)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev =
		to_mtk_rpmsg_device(rpdev)->mtk_subdev;

	return __mtk_create_ept(mtk_subdev, rpdev, cb, priv, chinfo.src);
}

static void mtk_rpmsg_destroy_ept(struct rpmsg_endpoint *ept)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev =
		to_mtk_rpmsg_endpoint(ept)->mtk_subdev;

	mtk_subdev->info->unregister_ipi(mtk_subdev->pdev, ept->addr);
	kref_put(&ept->refcount, __mtk_ept_release);
}

static int mtk_rpmsg_send(struct rpmsg_endpoint *ept, void *data, int len)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev =
		to_mtk_rpmsg_endpoint(ept)->mtk_subdev;

	return mtk_subdev->info->send_ipi(mtk_subdev->pdev, ept->addr, data,
					  len, 0);
}

static int mtk_rpmsg_trysend(struct rpmsg_endpoint *ept, void *data, int len)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev =
		to_mtk_rpmsg_endpoint(ept)->mtk_subdev;

	/*
	 * TODO: This currently is same as mtk_rpmsg_send, and wait until SCP
	 * received the last command.
	 */
	return mtk_subdev->info->send_ipi(mtk_subdev->pdev, ept->addr, data,
					  len, 0);
}

static const struct rpmsg_endpoint_ops mtk_rpmsg_endpoint_ops = {
	.destroy_ept = mtk_rpmsg_destroy_ept,
	.send = mtk_rpmsg_send,
	.trysend = mtk_rpmsg_trysend,
};

static void mtk_rpmsg_release_device(struct device *dev)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);
	struct mtk_rpmsg_device *mdev = to_mtk_rpmsg_device(rpdev);

	kfree(mdev);
}

static const struct rpmsg_device_ops mtk_rpmsg_device_ops = {
	.create_ept = mtk_rpmsg_create_ept,
};

static struct device_node *
mtk_rpmsg_match_device_subnode(struct device_node *node, const char *channel)
{
	struct device_node *child;
	const char *name;
	int ret;

	for_each_available_child_of_node(node, child) {
		ret = of_property_read_string(child, "mediatek,rpmsg-name", &name);
		if (ret)
			continue;

		if (strcmp(name, channel) == 0)
			return child;
	}

	return NULL;
}

static int mtk_rpmsg_register_device(struct mtk_rpmsg_rproc_subdev *mtk_subdev,
				     struct rpmsg_channel_info *info)
{
	struct rpmsg_device *rpdev;
	struct mtk_rpmsg_device *mdev;
	struct platform_device *pdev = mtk_subdev->pdev;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->mtk_subdev = mtk_subdev;

	rpdev = &mdev->rpdev;
	rpdev->ops = &mtk_rpmsg_device_ops;
	rpdev->src = info->src;
	rpdev->dst = info->dst;
	strscpy(rpdev->id.name, info->name, RPMSG_NAME_SIZE);

	rpdev->dev.of_node =
		mtk_rpmsg_match_device_subnode(pdev->dev.of_node, info->name);
	rpdev->dev.parent = &pdev->dev;
	rpdev->dev.release = mtk_rpmsg_release_device;

	return rpmsg_register_device(rpdev);
}

static void mtk_register_device_work_function(struct work_struct *register_work)
{
	struct mtk_rpmsg_rproc_subdev *subdev = container_of(
		register_work, struct mtk_rpmsg_rproc_subdev, register_work);
	struct platform_device *pdev = subdev->pdev;
	struct mtk_rpmsg_channel_info *info;
	int ret;

	mutex_lock(&subdev->channels_lock);
	list_for_each_entry(info, &subdev->channels, list) {
		if (info->registered)
			continue;

		mutex_unlock(&subdev->channels_lock);
		ret = mtk_rpmsg_register_device(subdev, &info->info);
		mutex_lock(&subdev->channels_lock);
		if (ret) {
			dev_err(&pdev->dev, "Can't create rpmsg_device\n");
			continue;
		}

		info->registered = true;
	}
	mutex_unlock(&subdev->channels_lock);
}

static int mtk_rpmsg_create_device(struct mtk_rpmsg_rproc_subdev *mtk_subdev,
				   char *name, u32 addr)
{
	struct mtk_rpmsg_channel_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	strscpy(info->info.name, name, RPMSG_NAME_SIZE);
	info->info.src = addr;
	info->info.dst = RPMSG_ADDR_ANY;
	mutex_lock(&mtk_subdev->channels_lock);
	list_add(&info->list, &mtk_subdev->channels);
	mutex_unlock(&mtk_subdev->channels_lock);

	schedule_work(&mtk_subdev->register_work);
	return 0;
}

static int mtk_rpmsg_ns_cb(struct rpmsg_device *rpdev, void *data, int len,
			   void *priv, u32 src)
{
	struct rpmsg_ns_msg *msg = data;
	struct mtk_rpmsg_rproc_subdev *mtk_subdev = priv;
	struct device *dev = &mtk_subdev->pdev->dev;

	int ret;

	if (len != sizeof(*msg)) {
		dev_err(dev, "malformed ns msg (%d)\n", len);
		return -EINVAL;
	}

	/*
	 * the name service ept does _not_ belong to a real rpmsg channel,
	 * and is handled by the rpmsg bus itself.
	 * for sanity reasons, make sure a valid rpdev has _not_ sneaked
	 * in somehow.
	 */
	if (rpdev) {
		dev_err(dev, "anomaly: ns ept has an rpdev handle\n");
		return -EINVAL;
	}

	/* don't trust the remote processor for null terminating the name */
	msg->name[RPMSG_NAME_SIZE - 1] = '\0';

	dev_info(dev, "creating channel %s addr 0x%x\n", msg->name, msg->addr);

	ret = mtk_rpmsg_create_device(mtk_subdev, msg->name, msg->addr);
	if (ret) {
		dev_err(dev, "create rpmsg device failed\n");
		return ret;
	}

	return 0;
}

static int mtk_rpmsg_prepare(struct rproc_subdev *subdev)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev = to_mtk_subdev(subdev);

	/* a dedicated endpoint handles the name service msgs */
	if (mtk_subdev->info->ns_ipi_id >= 0) {
		mtk_subdev->ns_ept =
			__mtk_create_ept(mtk_subdev, NULL, mtk_rpmsg_ns_cb,
					 mtk_subdev,
					 mtk_subdev->info->ns_ipi_id);
		if (!mtk_subdev->ns_ept) {
			dev_err(&mtk_subdev->pdev->dev,
				"failed to create name service endpoint\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static void mtk_rpmsg_unprepare(struct rproc_subdev *subdev)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev = to_mtk_subdev(subdev);

	if (mtk_subdev->ns_ept) {
		mtk_rpmsg_destroy_ept(mtk_subdev->ns_ept);
		mtk_subdev->ns_ept = NULL;
	}
}

static void mtk_rpmsg_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct mtk_rpmsg_channel_info *info, *next;
	struct mtk_rpmsg_rproc_subdev *mtk_subdev = to_mtk_subdev(subdev);
	struct device *dev = &mtk_subdev->pdev->dev;

	/*
	 * Destroy the name service endpoint here, to avoid new channel being
	 * created after the rpmsg_unregister_device loop below.
	 */
	if (mtk_subdev->ns_ept) {
		mtk_rpmsg_destroy_ept(mtk_subdev->ns_ept);
		mtk_subdev->ns_ept = NULL;
	}

	cancel_work_sync(&mtk_subdev->register_work);

	mutex_lock(&mtk_subdev->channels_lock);
	list_for_each_entry(info, &mtk_subdev->channels, list) {
		if (!info->registered)
			continue;
		if (rpmsg_unregister_device(dev, &info->info)) {
			dev_warn(
				dev,
				"rpmsg_unregister_device failed for %s.%d.%d\n",
				info->info.name, info->info.src,
				info->info.dst);
		}
	}

	list_for_each_entry_safe(info, next,
				 &mtk_subdev->channels, list) {
		list_del(&info->list);
		kfree(info);
	}
	mutex_unlock(&mtk_subdev->channels_lock);
}

struct rproc_subdev *
mtk_rpmsg_create_rproc_subdev(struct platform_device *pdev,
			      struct mtk_rpmsg_info *info)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev;

	mtk_subdev = kzalloc(sizeof(*mtk_subdev), GFP_KERNEL);
	if (!mtk_subdev)
		return NULL;

	mtk_subdev->pdev = pdev;
	mtk_subdev->subdev.prepare = mtk_rpmsg_prepare;
	mtk_subdev->subdev.stop = mtk_rpmsg_stop;
	mtk_subdev->subdev.unprepare = mtk_rpmsg_unprepare;
	mtk_subdev->info = info;
	INIT_LIST_HEAD(&mtk_subdev->channels);
	INIT_WORK(&mtk_subdev->register_work,
		  mtk_register_device_work_function);
	mutex_init(&mtk_subdev->channels_lock);

	return &mtk_subdev->subdev;
}
EXPORT_SYMBOL_GPL(mtk_rpmsg_create_rproc_subdev);

void mtk_rpmsg_destroy_rproc_subdev(struct rproc_subdev *subdev)
{
	struct mtk_rpmsg_rproc_subdev *mtk_subdev = to_mtk_subdev(subdev);

	kfree(mtk_subdev);
}
EXPORT_SYMBOL_GPL(mtk_rpmsg_destroy_rproc_subdev);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek scp rpmsg driver");
