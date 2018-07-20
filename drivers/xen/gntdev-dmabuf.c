// SPDX-License-Identifier: GPL-2.0

/*
 * Xen dma-buf functionality for gntdev.
 *
 * Copyright (c) 2018 Oleksandr Andrushchenko, EPAM Systems Inc.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <xen/xen.h>
#include <xen/grant_table.h>

#include "gntdev-common.h"
#include "gntdev-dmabuf.h"

struct gntdev_dmabuf_priv {
	/* List of exported DMA buffers. */
	struct list_head exp_list;
	/* List of wait objects. */
	struct list_head exp_wait_list;
	/* This is the lock which protects dma_buf_xxx lists. */
	struct mutex lock;
};

/* DMA buffer export support. */

/* Implementation of wait for exported DMA buffer to be released. */

static int dmabuf_exp_wait_released(struct gntdev_dmabuf_priv *priv, int fd,
				    int wait_to_ms)
{
	return -EINVAL;
}

static int dmabuf_exp_from_refs(struct gntdev_priv *priv, int flags,
				int count, u32 domid, u32 *refs, u32 *fd)
{
	*fd = -1;
	return -EINVAL;
}

/* DMA buffer import support. */

static struct gntdev_dmabuf *
dmabuf_imp_to_refs(struct gntdev_dmabuf_priv *priv, struct device *dev,
		   int fd, int count, int domid)
{
	return ERR_PTR(-ENOMEM);
}

static u32 *dmabuf_imp_get_refs(struct gntdev_dmabuf *gntdev_dmabuf)
{
	return NULL;
}

static int dmabuf_imp_release(struct gntdev_dmabuf_priv *priv, u32 fd)
{
	return -EINVAL;
}

/* DMA buffer IOCTL support. */

long gntdev_ioctl_dmabuf_exp_from_refs(struct gntdev_priv *priv, int use_ptemod,
				       struct ioctl_gntdev_dmabuf_exp_from_refs __user *u)
{
	struct ioctl_gntdev_dmabuf_exp_from_refs op;
	u32 *refs;
	long ret;

	if (use_ptemod) {
		pr_debug("Cannot provide dma-buf: use_ptemode %d\n",
			 use_ptemod);
		return -EINVAL;
	}

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	if (unlikely(op.count <= 0))
		return -EINVAL;

	refs = kcalloc(op.count, sizeof(*refs), GFP_KERNEL);
	if (!refs)
		return -ENOMEM;

	if (copy_from_user(refs, u->refs, sizeof(*refs) * op.count) != 0) {
		ret = -EFAULT;
		goto out;
	}

	ret = dmabuf_exp_from_refs(priv, op.flags, op.count,
				   op.domid, refs, &op.fd);
	if (ret)
		goto out;

	if (copy_to_user(u, &op, sizeof(op)) != 0)
		ret = -EFAULT;

out:
	kfree(refs);
	return ret;
}

long gntdev_ioctl_dmabuf_exp_wait_released(struct gntdev_priv *priv,
					   struct ioctl_gntdev_dmabuf_exp_wait_released __user *u)
{
	struct ioctl_gntdev_dmabuf_exp_wait_released op;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	return dmabuf_exp_wait_released(priv->dmabuf_priv, op.fd,
					op.wait_to_ms);
}

long gntdev_ioctl_dmabuf_imp_to_refs(struct gntdev_priv *priv,
				     struct ioctl_gntdev_dmabuf_imp_to_refs __user *u)
{
	struct ioctl_gntdev_dmabuf_imp_to_refs op;
	struct gntdev_dmabuf *gntdev_dmabuf;
	long ret;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	if (unlikely(op.count <= 0))
		return -EINVAL;

	gntdev_dmabuf = dmabuf_imp_to_refs(priv->dmabuf_priv,
					   priv->dma_dev, op.fd,
					   op.count, op.domid);
	if (IS_ERR(gntdev_dmabuf))
		return PTR_ERR(gntdev_dmabuf);

	if (copy_to_user(u->refs, dmabuf_imp_get_refs(gntdev_dmabuf),
			 sizeof(*u->refs) * op.count) != 0) {
		ret = -EFAULT;
		goto out_release;
	}
	return 0;

out_release:
	dmabuf_imp_release(priv->dmabuf_priv, op.fd);
	return ret;
}

long gntdev_ioctl_dmabuf_imp_release(struct gntdev_priv *priv,
				     struct ioctl_gntdev_dmabuf_imp_release __user *u)
{
	struct ioctl_gntdev_dmabuf_imp_release op;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	return dmabuf_imp_release(priv->dmabuf_priv, op.fd);
}

struct gntdev_dmabuf_priv *gntdev_dmabuf_init(void)
{
	struct gntdev_dmabuf_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	return priv;
}

void gntdev_dmabuf_fini(struct gntdev_dmabuf_priv *priv)
{
	kfree(priv);
}
