/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * derived from the omap-rpmsg implementation.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/imx_rpmsg.h>

struct imx_rpmsg_vproc {
	struct virtio_device vdev;
	unsigned int vring[2];
	char *rproc_name;
	struct mutex lock;
	struct notifier_block nb;
	struct virtqueue *vq[2];
	int base_vq_id;
	int num_of_vqs;
};

/*
 * For now, allocate 256 buffers of 512 bytes for each side. each buffer
 * will then have 16B for the msg header and 496B for the payload.
 * This will require a total space of 256KB for the buffers themselves, and
 * 3 pages for every vring (the size of the vring depends on the number of
 * buffers it supports).
 */
#define RPMSG_NUM_BUFS		(512)
#define RPMSG_BUF_SIZE		(512)
#define RPMSG_BUFS_SPACE	(RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)

/*
 * The alignment between the consumer and producer parts of the vring.
 * Note: this is part of the "wire" protocol. If you change this, you need
 * to update your BIOS image as well
 */
#define RPMSG_VRING_ALIGN	(4096)

/* With 256 buffers, our vring will occupy 3 pages */
#define RPMSG_RING_SIZE	((DIV_ROUND_UP(vring_size(RPMSG_NUM_BUFS / 2, \
				RPMSG_VRING_ALIGN), PAGE_SIZE)) * PAGE_SIZE)

#define to_imx_rpdev(vd) container_of(vd, struct imx_rpmsg_vproc, vdev)

struct imx_rpmsg_vq_info {
	__u16 num;	/* number of entries in the virtio_ring */
	__u16 vq_id;	/* a globaly unique index of this virtqueue */
	void *addr;	/* address where we mapped the virtio ring */
	struct imx_rpmsg_vproc *rpdev;
};

static u64 imx_rpmsg_get_features(struct virtio_device *vdev)
{
	return 1 << VIRTIO_RPMSG_F_NS;
}

static int imx_rpmsg_finalize_features(struct virtio_device *vdev)
{
	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);
	return 0;
}

/* kick the remote processor, and let it know which virtqueue to poke at */
static bool imx_rpmsg_notify(struct virtqueue *vq)
{
	int ret;
	unsigned int mu_rpmsg = 0;
	struct imx_rpmsg_vq_info *rpvq = vq->priv;

	mu_rpmsg = rpvq->vq_id << 16;
	mutex_lock(&rpvq->rpdev->lock);
	/* send the index of the triggered virtqueue as the mu payload */
	ret = imx_mu_rpmsg_send(mu_rpmsg);
	mutex_unlock(&rpvq->rpdev->lock);
	if (ret) {
		pr_err("ugh, imx_mu_rpmsg_send() failed: %d\n", ret);
		return false;
	}

	return true;
}

static int imx_mu_rpmsg_callback(struct notifier_block *this,
					unsigned long index, void *data)
{
	u32 mu_msg = (u32) data;
	struct imx_rpmsg_vproc *rpdev;

	rpdev = container_of(this, struct imx_rpmsg_vproc, nb);

	pr_debug("%s mu_msg: 0x%x\n", __func__, mu_msg);

	/* ignore vq indices which are clearly not for us */
	mu_msg = mu_msg >> 16;
	if (mu_msg < rpdev->base_vq_id)
		pr_err("mu_msg: 0x%x is invalid\n", mu_msg);

	mu_msg -= rpdev->base_vq_id;

	/*
	 * Currently both PENDING_MSG and explicit-virtqueue-index
	 * messaging are supported.
	 * Whatever approach is taken, at this point 'mu_msg' contains
	 * the index of the vring which was just triggered.
	 */
	if (mu_msg < rpdev->num_of_vqs)
		vring_interrupt(mu_msg, rpdev->vq[mu_msg]);

	return NOTIFY_DONE;
}

static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned index,
				    void (*callback)(struct virtqueue *vq),
				    const char *name)
{
	struct imx_rpmsg_vproc *rpdev = to_imx_rpdev(vdev);
	struct imx_rpmsg_vq_info *rpvq;
	struct virtqueue *vq;
	int err;

	rpvq = kmalloc(sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	/* ioremap'ing normal memory, so we cast away sparse's complaints */
	rpvq->addr = (__force void *) ioremap_nocache(rpdev->vring[index],
							RPMSG_RING_SIZE);
	if (!rpvq->addr) {
		err = -ENOMEM;
		goto free_rpvq;
	}

	memset(rpvq->addr, 0, RPMSG_RING_SIZE);

	pr_debug("vring%d: phys 0x%x, virt 0x%x\n", index, rpdev->vring[index],
					(unsigned int) rpvq->addr);

	vq = vring_new_virtqueue(index, RPMSG_NUM_BUFS / 2, RPMSG_VRING_ALIGN,
			vdev, true, rpvq->addr, imx_rpmsg_notify, callback,
			name);
	if (!vq) {
		pr_err("vring_new_virtqueue failed\n");
		err = -ENOMEM;
		goto unmap_vring;
	}

	rpdev->vq[index] = vq;
	vq->priv = rpvq;
	/* system-wide unique id for this virtqueue */
	rpvq->vq_id = rpdev->base_vq_id + index;
	rpvq->rpdev = rpdev;
	mutex_init(&rpdev->lock);

	return vq;

unmap_vring:
	/* iounmap normal memory, so make sparse happy */
	iounmap((__force void __iomem *) rpvq->addr);
free_rpvq:
	kfree(rpvq);
	return ERR_PTR(err);
}

static void imx_rpmsg_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct imx_rpmsg_vproc *rpdev = to_imx_rpdev(vdev);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct imx_rpmsg_vq_info *rpvq = vq->priv;
		iounmap(rpvq->addr);
		vring_del_virtqueue(vq);
		kfree(rpvq);
	}

	if (&rpdev->nb)
		imx_mu_rpmsg_unregister_nb((const char *)rpdev->rproc_name,
				&rpdev->nb);
}

static int imx_rpmsg_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char *names[])
{
	struct imx_rpmsg_vproc *rpdev = to_imx_rpdev(vdev);
	int i, err;

	/* we maintain two virtqueues per remote processor (for RX and TX) */
	if (nvqs != 2)
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rp_find_vq(vdev, i, callbacks[i], names[i]);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	rpdev->num_of_vqs = nvqs;

	rpdev->nb.notifier_call = imx_mu_rpmsg_callback;
	imx_mu_rpmsg_register_nb((const char *)rpdev->rproc_name, &rpdev->nb);

	return 0;

error:
	imx_rpmsg_del_vqs(vdev);
	return err;
}

static void imx_rpmsg_reset(struct virtio_device *vdev)
{
	dev_dbg(&vdev->dev, "reset !\n");
}

static u8 imx_rpmsg_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void imx_rpmsg_set_status(struct virtio_device *vdev, u8 status)
{
	dev_dbg(&vdev->dev, "%s new status: %d\n", __func__, status);
}

static void imx_rpmsg_vproc_release(struct device *dev)
{
	/* this handler is provided so driver core doesn't yell at us */
}

static struct virtio_config_ops imx_rpmsg_config_ops = {
	.get_features	= imx_rpmsg_get_features,
	.finalize_features = imx_rpmsg_finalize_features,
	.find_vqs	= imx_rpmsg_find_vqs,
	.del_vqs	= imx_rpmsg_del_vqs,
	.reset		= imx_rpmsg_reset,
	.set_status	= imx_rpmsg_set_status,
	.get_status	= imx_rpmsg_get_status,
};

static struct imx_rpmsg_vproc imx_rpmsg_vprocs[] = {
	{
		.vdev.id.device	= VIRTIO_ID_RPMSG,
		.vdev.config	= &imx_rpmsg_config_ops,
		.rproc_name	= "m4",
		.base_vq_id	= 0,
	},
};

static const struct of_device_id imx_rpmsg_dt_ids[] = {
	{ .compatible = "fsl,imx6sx-rpmsg", },
	{ .compatible = "fsl,imx7d-rpmsg", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_rpmsg_dt_ids);

static int imx_rpmsg_probe(struct platform_device *pdev)
{
	int i, ret = 0;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	resource_size_t size;

	for (i = 0; i < ARRAY_SIZE(imx_rpmsg_vprocs); i++) {
		struct imx_rpmsg_vproc *rpdev = &imx_rpmsg_vprocs[i];

		if (!strcmp(rpdev->rproc_name, "m4")) {
			ret = of_device_is_compatible(np, "fsl,imx7d-rpmsg");
			ret |= of_device_is_compatible(np, "fsl,imx6sx-rpmsg");
			if (ret) {
				res = platform_get_resource(pdev,
							    IORESOURCE_MEM, 0);

				if (res) {
					size = resource_size(res);
					rpdev->vring[0] = res->start;
					rpdev->vring[1] = res->start + size;
				} else {
					/* hardcodes here now. */
					rpdev->vring[0] = 0xBFFF0000;
					rpdev->vring[1] = 0xBFFF8000;
				}
			}
		} else {
			break;
		}

		pr_debug("%s rpdev%d: vring0 0x%x, vring1 0x%x\n", __func__,
				i, rpdev->vring[0], rpdev->vring[1]);

		rpdev->vdev.dev.parent = &pdev->dev;
		rpdev->vdev.dev.release = imx_rpmsg_vproc_release;

		ret = register_virtio_device(&rpdev->vdev);
		if (ret) {
			pr_err("%s failed to register rpdev: %d\n",
					__func__, ret);
			break;
		}
	}

	return ret;
}

static int imx_rpmsg_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(imx_rpmsg_vprocs); i++) {
		struct imx_rpmsg_vproc *rpdev = &imx_rpmsg_vprocs[i];

		unregister_virtio_device(&rpdev->vdev);
	}
	return 0;
}

static struct platform_driver imx_rpmsg_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "imx-rpmsg",
		   .of_match_table = imx_rpmsg_dt_ids,
		   },
	.probe = imx_rpmsg_probe,
	.remove = imx_rpmsg_remove,
};

static int __init imx_rpmsg_init(void)
{
	int ret;

	ret = platform_driver_register(&imx_rpmsg_driver);
	if (ret)
		pr_err("Unable to initialize rpmsg driver\n");
	else
		pr_info("imx rpmsg driver is registered.\n");

	return ret;
}

static void __exit imx_rpmsg_exit(void)
{
	pr_info("imx rpmsg driver is unregistered.\n");
	platform_driver_unregister(&imx_rpmsg_driver);
}

module_exit(imx_rpmsg_exit);
module_init(imx_rpmsg_init);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("iMX remote processor messaging virtio device");
MODULE_LICENSE("GPL v2");
