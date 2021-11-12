// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
 *
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 **************************************************************************/

#include <linux/spinlock.h>

#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_reg.h"

static void psb_lid_timer_func(struct timer_list *t)
{
	struct drm_psb_private *dev_priv = from_timer(dev_priv, t, lid_timer);
	struct drm_device *dev = (struct drm_device *)&dev_priv->dev;
	struct timer_list *lid_timer = &dev_priv->lid_timer;
	unsigned long irq_flags;
	u32 __iomem *lid_state = dev_priv->opregion.lid_state;
	u32 pp_status;

	if (readl(lid_state) == dev_priv->lid_last_state)
		goto lid_timer_schedule;

	if ((readl(lid_state)) & 0x01) {
		/*lid state is open*/
		REG_WRITE(PP_CONTROL, REG_READ(PP_CONTROL) | POWER_TARGET_ON);
		do {
			pp_status = REG_READ(PP_STATUS);
		} while ((pp_status & PP_ON) == 0 &&
			 (pp_status & PP_SEQUENCE_MASK) != 0);

		if (REG_READ(PP_STATUS) & PP_ON) {
			/*FIXME: should be backlight level before*/
			psb_intel_lvds_set_brightness(dev, 100);
		} else {
			DRM_DEBUG("LVDS panel never powered up");
			return;
		}
	} else {
		psb_intel_lvds_set_brightness(dev, 0);

		REG_WRITE(PP_CONTROL, REG_READ(PP_CONTROL) & ~POWER_TARGET_ON);
		do {
			pp_status = REG_READ(PP_STATUS);
		} while ((pp_status & PP_ON) == 0);
	}
	dev_priv->lid_last_state =  readl(lid_state);

lid_timer_schedule:
	spin_lock_irqsave(&dev_priv->lid_lock, irq_flags);
	if (!timer_pending(lid_timer)) {
		lid_timer->expires = jiffies + PSB_LID_DELAY;
		add_timer(lid_timer);
	}
	spin_unlock_irqrestore(&dev_priv->lid_lock, irq_flags);
}

void psb_lid_timer_init(struct drm_psb_private *dev_priv)
{
	struct timer_list *lid_timer = &dev_priv->lid_timer;
	unsigned long irq_flags;

	spin_lock_init(&dev_priv->lid_lock);
	spin_lock_irqsave(&dev_priv->lid_lock, irq_flags);

	timer_setup(lid_timer, psb_lid_timer_func, 0);

	lid_timer->expires = jiffies + PSB_LID_DELAY;

	add_timer(lid_timer);
	spin_unlock_irqrestore(&dev_priv->lid_lock, irq_flags);
}

void psb_lid_timer_takedown(struct drm_psb_private *dev_priv)
{
	del_timer_sync(&dev_priv->lid_timer);
}

