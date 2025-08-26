/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Xen dma-buf functionality for gntdev.
 *
 * Copyright (c) 2018 Oleksandr Andrushchenko, EPAM Systems Inc.
 */

#ifndef _GNTDEV_DMABUF_H
#define _GNTDEV_DMABUF_H

#include <xen/gntdev.h>

struct gntdev_dmabuf_priv;
struct gntdev_priv;

struct gntdev_dmabuf_priv *gntdev_dmabuf_init(struct file *filp);

void gntdev_dmabuf_fini(struct gntdev_dmabuf_priv *priv);

long gntdev_ioctl_dmabuf_exp_from_refs(struct gntdev_priv *priv,
				       struct ioctl_gntdev_dmabuf_exp_from_refs __user *u);

long gntdev_ioctl_dmabuf_exp_wait_released(struct gntdev_priv *priv,
					   struct ioctl_gntdev_dmabuf_exp_wait_released __user *u);

long gntdev_ioctl_dmabuf_imp_to_refs(struct gntdev_priv *priv,
				     struct ioctl_gntdev_dmabuf_imp_to_refs __user *u);

long gntdev_ioctl_dmabuf_imp_release(struct gntdev_priv *priv,
				     struct ioctl_gntdev_dmabuf_imp_release __user *u);

#endif
