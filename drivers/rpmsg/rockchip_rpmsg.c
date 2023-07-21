// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Remote Processors Messaging Platform Support.
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Author: Steven Liu <steven.liu@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/rpmsg/rockchip_rpmsg.h>
#include <linux/slab.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <soc/rockchip/rockchip-mailbox.h>

#include "rpmsg_internal.h"

enum rk_rpmsg_chip {
	RK3562,
	RK3568,
};

struct rk_virtio_dev {
	struct virtio_device vdev;
	unsigned int vring[2];
	struct virtqueue *vq[2];
	unsigned int base_queue_id;
	int num_of_vqs;
	struct rk_rpmsg_dev *rpdev;
};

#define to_rk_rpvdev(vd)	container_of(vd, struct rk_virtio_dev, vdev)

struct rk_rpmsg_dev {
	struct platform_device *pdev;
	enum rk_rpmsg_chip chip;
	int vdev_nums;
	unsigned int link_id;
	int first_notify;
	u32 flags;
	struct mbox_client mbox_cl;
	struct mbox_chan *mbox_rx_chan;
	struct mbox_chan *mbox_tx_chan;
	struct rk_virtio_dev *rpvdev[RPMSG_MAX_INSTANCE_NUM];
};

struct rk_rpmsg_vq_info {
	u32 queue_id;
	void *vring_addr;
	struct rk_rpmsg_dev *rpdev;
};

static void rk_rpmsg_rx_callback(struct mbox_client *client, void *message)
{
	u32 link_id;
	struct rk_virtio_dev *rpvdev;
	struct rk_rpmsg_dev *rpdev = container_of(client, struct rk_rpmsg_dev, mbox_cl);
	struct platform_device *pdev = rpdev->pdev;
	struct device *dev = &pdev->dev;
	struct rockchip_mbox_msg *rx_msg;

	rx_msg = message;
	dev_dbg(dev, "rpmsg master: receive cmd=0x%x data=0x%x\n",
		rx_msg->cmd, rx_msg->data);
	if (rx_msg->data != RPMSG_MBOX_MAGIC)
		dev_err(dev, "rpmsg master: mailbox data error!\n");
	link_id = rx_msg->cmd & 0xFFU;
	/* TODO: only support one remote core now */
	rpvdev = rpdev->rpvdev[0];
	rpdev->flags |= RPMSG_REMOTE_IS_READY;
	dev_dbg(dev, "rpmsg master: rx link_id=0x%x flag=0x%x\n", link_id, rpdev->flags);
	vring_interrupt(0, rpvdev->vq[0]);
}

static bool rk_rpmsg_notify(struct virtqueue *vq)
{
	struct rk_rpmsg_vq_info *rpvq = vq->priv;
	struct rk_rpmsg_dev *rpdev = rpvq->rpdev;
	struct platform_device *pdev = rpdev->pdev;
	struct device *dev = &pdev->dev;
	u32 link_id;
	int ret;
	struct rockchip_mbox_msg tx_msg;

	memset(&tx_msg, 0, sizeof(tx_msg));
	dev_dbg(dev, "queue_id-0x%x virt_vring_addr 0x%p\n",
		rpvq->queue_id, rpvq->vring_addr);

	link_id = rpdev->link_id;
	tx_msg.cmd = link_id & 0xFFU;
	tx_msg.data = RPMSG_MBOX_MAGIC;

	if ((rpdev->first_notify == 0) && (rpvq->queue_id % 2 == 0)) {
		/* first_notify is used in the master init handshake phase. */
		dev_dbg(dev, "rpmsg first_notify\n");
		rpdev->first_notify++;
	} else if (rpvq->queue_id % 2 == 0) {
		/* tx done is not supported, so ignored */
		return true;
	}
	ret = mbox_send_message(rpdev->mbox_tx_chan, &tx_msg);
	if (ret < 0) {
		dev_err(dev, "mbox send failed!\n");
		return false;
	}

	return true;
}

static struct virtqueue *rk_rpmsg_find_vq(struct virtio_device *vdev,
					  unsigned int index,
					  void (*callback)(struct virtqueue *vq),
					  const char *name,
					  bool ctx)
{
	struct rk_virtio_dev *rpvdev = to_rk_rpvdev(vdev);
	struct rk_rpmsg_dev *rpdev = rpvdev->rpdev;
	struct platform_device *pdev = rpdev->pdev;
	struct device *dev = &pdev->dev;
	struct rk_rpmsg_vq_info *rpvq;
	struct virtqueue *vq;
	int ret;

	rpvq = kmalloc(sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	rpdev->flags &= (~RPMSG_CACHED_VRING);
	rpvq->vring_addr = (__force void *) ioremap(rpvdev->vring[index], RPMSG_VRING_SIZE);
	if (!rpvq->vring_addr) {
		ret = -ENOMEM;
		goto free_rpvq;
	}
	dev_dbg(dev, "vring%d: phys 0x%x, virt 0x%p\n", index,
		rpvdev->vring[index], rpvq->vring_addr);

	memset_io(rpvq->vring_addr, 0, RPMSG_VRING_SIZE);

	vq = vring_new_virtqueue(index, RPMSG_BUF_COUNT, RPMSG_VRING_ALIGN, vdev, true, ctx,
				 rpvq->vring_addr, rk_rpmsg_notify, callback, name);
	if (!vq) {
		dev_err(dev, "vring_new_virtqueue failed\n");
		ret = -ENOMEM;
		goto unmap_vring;
	}

	rpvdev->vq[index] = vq;
	vq->priv = rpvq;

	rpvq->queue_id = rpvdev->base_queue_id + index;
	rpvq->rpdev = rpdev;

	return vq;

unmap_vring:
	iounmap((__force void __iomem *) rpvq->vring_addr);
free_rpvq:
	kfree(rpvq);
	return ERR_PTR(ret);
}

static u8 rk_rpmsg_get_status(struct virtio_device *vdev)
{
	/* TODO: */
	return 0;
}

static void rk_rpmsg_set_status(struct virtio_device *vdev, u8 status)
{
	/* TODO: */
}

static void rk_rpmsg_reset(struct virtio_device *vdev)
{
	/* TODO: */
}

static void rk_rpmsg_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct rk_rpmsg_vq_info *rpvq = vq->priv;

		iounmap(rpvq->vring_addr);
		vring_del_virtqueue(vq);
		kfree(rpvq);
	}
}

static int rk_rpmsg_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
			     struct virtqueue *vqs[],
			     vq_callback_t *callbacks[],
			     const char * const names[],
			     const bool *ctx,
			     struct irq_affinity *desc)
{
	struct rk_virtio_dev *rpvdev = to_rk_rpvdev(vdev);
	int i, ret;

	/* Each rpmsg instance has two virtqueues. vqs[0] is rvq and vqs[1] is tvq */
	if (nvqs != 2)
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rk_rpmsg_find_vq(vdev, i, callbacks[i], names[i],
					  ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			ret = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	rpvdev->num_of_vqs = nvqs;

	return 0;

error:
	rk_rpmsg_del_vqs(vdev);

	return ret;
}

static u64 rk_rpmsg_get_features(struct virtio_device *vdev)
{
	return RPMSG_VIRTIO_RPMSG_F_NS;
}

static int rk_rpmsg_finalize_features(struct virtio_device *vdev)
{
	vring_transport_features(vdev);

	return 0;
}

static void rk_rpmsg_vdev_release(struct device *dev)
{
}

static struct virtio_config_ops rk_rpmsg_config_ops = {
	.get_status	= rk_rpmsg_get_status,
	.set_status	= rk_rpmsg_set_status,
	.reset		= rk_rpmsg_reset,
	.find_vqs	= rk_rpmsg_find_vqs,
	.del_vqs	= rk_rpmsg_del_vqs,
	.get_features	= rk_rpmsg_get_features,
	.finalize_features = rk_rpmsg_finalize_features,
};

static int rk_set_vring_phy_buf(struct platform_device *pdev,
				struct rk_rpmsg_dev *rpdev, int vdev_nums)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	resource_size_t size;
	unsigned int start, end;
	int i, ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		size = resource_size(res);
		start = res->start;
		end = res->start + size;
		for (i = 0; i < vdev_nums; i++) {
			rpdev->rpvdev[i] = devm_kzalloc(dev, sizeof(struct rk_virtio_dev),
							GFP_KERNEL);
			if (!rpdev->rpvdev[i])
				return -ENOMEM;

			rpdev->rpvdev[i]->vring[0] = start;
			rpdev->rpvdev[i]->vring[1] = start + RPMSG_VRING_SIZE;
			start += RPMSG_VRING_OVERHEAD;
			if (start > end) {
				dev_err(dev, "Too small memory size %x!\n", (u32)size);
				ret = -EINVAL;
				break;
			}
		}
	} else {
		return -ENOMEM;
	}

	return ret;
}

static int rockchip_rpmsg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_rpmsg_dev *rpdev = NULL;
	struct mbox_client *cl;
	int i, ret = 0;

	rpdev = devm_kzalloc(dev, sizeof(*rpdev), GFP_KERNEL);
	if (!rpdev)
		return -ENOMEM;

	dev_info(dev, "rockchip rpmsg platform probe.\n");
	rpdev->pdev = pdev;
	rpdev->chip = (enum rk_rpmsg_chip)device_get_match_data(dev);
	rpdev->first_notify = 0;

	cl = &rpdev->mbox_cl;
	cl->dev = dev;
	cl->rx_callback = rk_rpmsg_rx_callback;

	rpdev->mbox_rx_chan = mbox_request_channel_byname(cl, "rpmsg-rx");
	if (IS_ERR(rpdev->mbox_rx_chan)) {
		ret = PTR_ERR(rpdev->mbox_rx_chan);
		dev_err(dev, "failed to request mbox rx chan, ret %d\n", ret);
		return ret;
	}
	rpdev->mbox_tx_chan = mbox_request_channel_byname(cl, "rpmsg-tx");
	if (IS_ERR(rpdev->mbox_tx_chan)) {
		ret = PTR_ERR(rpdev->mbox_tx_chan);
		dev_err(dev, "failed to request mbox tx chan, ret %d\n", ret);
		return ret;
	}

	ret = device_property_read_u32(dev, "rockchip,link-id", &rpdev->link_id);
	if (ret) {
		dev_err(dev, "failed to get link_id, ret %d\n", ret);
		goto free_channel;
	}
	ret = device_property_read_u32(dev, "rockchip,vdev-nums", &rpdev->vdev_nums);
	if (ret) {
		dev_info(dev, "vdev-nums default 1\n");
		rpdev->vdev_nums = 1;
	}
	if (rpdev->vdev_nums > RPMSG_MAX_INSTANCE_NUM) {
		dev_err(dev, "vdev-nums exceed the max %d\n", RPMSG_MAX_INSTANCE_NUM);
		ret = -EINVAL;
		goto free_channel;
	}

	ret = rk_set_vring_phy_buf(pdev, rpdev, rpdev->vdev_nums);
	if (ret) {
		dev_err(dev, "No vring buffer.\n");
		ret = -ENOMEM;
		goto free_channel;
	}
	if (of_reserved_mem_device_init(dev)) {
		dev_info(dev, "No shared DMA pool.\n");
		rpdev->flags &= (~RPMSG_SHARED_DMA_POOL);
	} else {
		rpdev->flags |= RPMSG_SHARED_DMA_POOL;
	}

	for (i = 0; i < rpdev->vdev_nums; i++) {
		dev_info(dev, "rpdev vdev%d: vring0 0x%x, vring1 0x%x\n",
			 i, rpdev->rpvdev[i]->vring[0], rpdev->rpvdev[i]->vring[1]);
		rpdev->rpvdev[i]->vdev.id.device = VIRTIO_ID_RPMSG;
		rpdev->rpvdev[i]->vdev.config = &rk_rpmsg_config_ops;
		rpdev->rpvdev[i]->vdev.dev.parent = dev;
		rpdev->rpvdev[i]->vdev.dev.release = rk_rpmsg_vdev_release;
		rpdev->rpvdev[i]->base_queue_id = i * 2;
		rpdev->rpvdev[i]->rpdev = rpdev;

		ret = register_virtio_device(&rpdev->rpvdev[i]->vdev);
		if (ret) {
			dev_err(dev, "fail to register rpvdev: %d\n", ret);
			goto free_reserved_mem;
		}
	}

	platform_set_drvdata(pdev, rpdev);

	return ret;

free_reserved_mem:
	if (rpdev->flags & RPMSG_SHARED_DMA_POOL)
		of_reserved_mem_device_release(dev);

free_channel:
	mbox_free_channel(rpdev->mbox_rx_chan);
	mbox_free_channel(rpdev->mbox_tx_chan);

	return ret;
}

static int rockchip_rpmsg_remove(struct platform_device *pdev)
{
	struct rk_rpmsg_dev *rpdev = platform_get_drvdata(pdev);

	mbox_free_channel(rpdev->mbox_rx_chan);
	mbox_free_channel(rpdev->mbox_tx_chan);

	return 0;
}

static const struct of_device_id rockchip_rpmsg_match[] = {
	{ .compatible = "rockchip,rk3562-rpmsg", .data = (void *)RK3562, },
	{ .compatible = "rockchip,rk3568-rpmsg", .data = (void *)RK3568, },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, rockchip_rpmsg_match);

static struct platform_driver rockchip_rpmsg_driver = {
	.probe = rockchip_rpmsg_probe,
	.remove = rockchip_rpmsg_remove,
	.driver = {
		.name = "rockchip-rpmsg",
		.of_match_table = rockchip_rpmsg_match,
	},
};
module_platform_driver(rockchip_rpmsg_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rockchip Remote Processors Messaging Platform Support");
MODULE_AUTHOR("Steven Liu <steven.liu@rock-chips.com>");

