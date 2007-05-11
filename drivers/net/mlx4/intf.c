/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mlx4/driver.h>

#include "mlx4.h"

struct mlx4_device_context {
	struct list_head	list;
	struct mlx4_interface  *intf;
	void		       *context;
};

static LIST_HEAD(intf_list);
static LIST_HEAD(dev_list);
static DEFINE_MUTEX(intf_mutex);

static void mlx4_add_device(struct mlx4_interface *intf, struct mlx4_priv *priv)
{
	struct mlx4_device_context *dev_ctx;

	dev_ctx = kmalloc(sizeof *dev_ctx, GFP_KERNEL);
	if (!dev_ctx)
		return;

	dev_ctx->intf    = intf;
	dev_ctx->context = intf->add(&priv->dev);

	if (dev_ctx->context) {
		spin_lock_irq(&priv->ctx_lock);
		list_add_tail(&dev_ctx->list, &priv->ctx_list);
		spin_unlock_irq(&priv->ctx_lock);
	} else
		kfree(dev_ctx);
}

static void mlx4_remove_device(struct mlx4_interface *intf, struct mlx4_priv *priv)
{
	struct mlx4_device_context *dev_ctx;

	list_for_each_entry(dev_ctx, &priv->ctx_list, list)
		if (dev_ctx->intf == intf) {
			spin_lock_irq(&priv->ctx_lock);
			list_del(&dev_ctx->list);
			spin_unlock_irq(&priv->ctx_lock);

			intf->remove(&priv->dev, dev_ctx->context);
			kfree(dev_ctx);
			return;
		}
}

int mlx4_register_interface(struct mlx4_interface *intf)
{
	struct mlx4_priv *priv;

	if (!intf->add || !intf->remove)
		return -EINVAL;

	mutex_lock(&intf_mutex);

	list_add_tail(&intf->list, &intf_list);
	list_for_each_entry(priv, &dev_list, dev_list)
		mlx4_add_device(intf, priv);

	mutex_unlock(&intf_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_register_interface);

void mlx4_unregister_interface(struct mlx4_interface *intf)
{
	struct mlx4_priv *priv;

	mutex_lock(&intf_mutex);

	list_for_each_entry(priv, &dev_list, dev_list)
		mlx4_remove_device(intf, priv);

	list_del(&intf->list);

	mutex_unlock(&intf_mutex);
}
EXPORT_SYMBOL_GPL(mlx4_unregister_interface);

void mlx4_dispatch_event(struct mlx4_dev *dev, enum mlx4_event type,
			 int subtype, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_device_context *dev_ctx;
	unsigned long flags;

	spin_lock_irqsave(&priv->ctx_lock, flags);

	list_for_each_entry(dev_ctx, &priv->ctx_list, list)
		if (dev_ctx->intf->event)
			dev_ctx->intf->event(dev, dev_ctx->context, type,
					     subtype, port);

	spin_unlock_irqrestore(&priv->ctx_lock, flags);
}

int mlx4_register_device(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_interface *intf;

	INIT_LIST_HEAD(&priv->ctx_list);
	spin_lock_init(&priv->ctx_lock);

	mutex_lock(&intf_mutex);

	list_add_tail(&priv->dev_list, &dev_list);
	list_for_each_entry(intf, &intf_list, list)
		mlx4_add_device(intf, priv);

	mutex_unlock(&intf_mutex);

	return 0;
}

void mlx4_unregister_device(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_interface *intf;

	mutex_lock(&intf_mutex);

	list_for_each_entry(intf, &intf_list, list)
		mlx4_remove_device(intf, priv);

	list_del(&priv->dev_list);

	mutex_unlock(&intf_mutex);
}
