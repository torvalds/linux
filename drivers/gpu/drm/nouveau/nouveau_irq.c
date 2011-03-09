/*
 * Copyright (C) 2006 Ben Skeggs.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Authors:
 *   Ben Skeggs <darktama@iinet.net.au>
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drm.h"
#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "nouveau_ramht.h"
#include "nouveau_util.h"

void
nouveau_irq_preinstall(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* Master disable */
	nv_wr32(dev, NV03_PMC_INTR_EN_0, 0);

	INIT_LIST_HEAD(&dev_priv->vbl_waiting);
}

int
nouveau_irq_postinstall(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* Master enable */
	nv_wr32(dev, NV03_PMC_INTR_EN_0, NV_PMC_INTR_EN_0_MASTER_ENABLE);
	if (dev_priv->msi_enabled)
		nv_wr08(dev, 0x00088068, 0xff);

	return 0;
}

void
nouveau_irq_uninstall(struct drm_device *dev)
{
	/* Master disable */
	nv_wr32(dev, NV03_PMC_INTR_EN_0, 0);
}

irqreturn_t
nouveau_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;
	u32 stat;
	int i;

	stat = nv_rd32(dev, NV03_PMC_INTR_0);
	if (!stat)
		return IRQ_NONE;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	for (i = 0; i < 32 && stat; i++) {
		if (!(stat & (1 << i)) || !dev_priv->irq_handler[i])
			continue;

		dev_priv->irq_handler[i](dev);
		stat &= ~(1 << i);
	}

	if (dev_priv->msi_enabled)
		nv_wr08(dev, 0x00088068, 0xff);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	if (stat && nouveau_ratelimit())
		NV_ERROR(dev, "PMC - unhandled INTR 0x%08x\n", stat);
	return IRQ_HANDLED;
}

int
nouveau_irq_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if (nouveau_msi != 0 && dev_priv->card_type >= NV_50) {
		ret = pci_enable_msi(dev->pdev);
		if (ret == 0) {
			NV_INFO(dev, "enabled MSI\n");
			dev_priv->msi_enabled = true;
		}
	}

	return drm_irq_install(dev);
}

void
nouveau_irq_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	drm_irq_uninstall(dev);
	if (dev_priv->msi_enabled)
		pci_disable_msi(dev->pdev);
}

void
nouveau_irq_register(struct drm_device *dev, int status_bit,
		     void (*handler)(struct drm_device *))
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	dev_priv->irq_handler[status_bit] = handler;
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
}

void
nouveau_irq_unregister(struct drm_device *dev, int status_bit)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	dev_priv->irq_handler[status_bit] = NULL;
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
}
