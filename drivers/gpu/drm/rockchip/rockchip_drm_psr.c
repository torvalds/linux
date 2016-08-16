/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Yakir Yang <ykk@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_psr.h"

#define PSR_FLUSH_TIMEOUT	msecs_to_jiffies(3000) /* 3 seconds */
#define PSR_SET_DELAY_TIME	msecs_to_jiffies(10)

enum psr_state {
	PSR_FLUSH,
	PSR_ENABLE,
	PSR_DISABLE,
};

struct psr_drv {
	struct list_head	list;
	struct drm_encoder	*encoder;

	enum psr_state		request_state;
	enum psr_state		state;

	struct delayed_work	state_work;

	struct timer_list	flush_timer;

	void (*set)(struct drm_encoder *encoder, bool enable);
};

static struct psr_drv *find_psr_by_crtc(struct drm_crtc *crtc)
{
	struct rockchip_drm_private *drm_drv = crtc->dev->dev_private;
	struct psr_drv *psr;
	unsigned long flags;

	spin_lock_irqsave(&drm_drv->psr_list_lock, flags);
	list_for_each_entry(psr, &drm_drv->psr_list, list) {
		if (psr->encoder->crtc == crtc)
			goto out;
	}
	psr = ERR_PTR(-ENODEV);

out:
	spin_unlock_irqrestore(&drm_drv->psr_list_lock, flags);
	return psr;
}

static void psr_state_work(struct work_struct *work)
{
	struct psr_drv *psr = container_of(work, typeof(*psr), state_work.work);
	enum psr_state request_state = psr->request_state;

	/*
	 * Allowed finite state machine:
	 *
	 *   PSR_ENABLE  < = = = = = >  PSR_FLUSH
	  *      | ^                        |
	  *      | |                        |
	  *      v |                        |
	 *   PSR_DISABLE < - - - - - - - - -
	 */

	/* Forbid no state change */
	if (request_state == psr->state)
		return;

	/* Forbid DISABLE change to FLUSH */
	if (request_state == PSR_FLUSH && psr->state == PSR_DISABLE)
		return;

	/* Allow but no need hardware change, just need assign the state */
	if (request_state == PSR_DISABLE && psr->state == PSR_FLUSH) {
		psr->state = request_state;
		return;
	}

	/* Only wrote in this work, no need lock protection */
	psr->state = request_state;

	/* Refact to hardware state change */
	switch (request_state) {
	case PSR_ENABLE:
		psr->set(psr->encoder, true);
		break;

	case PSR_DISABLE:
	case PSR_FLUSH:
		psr->set(psr->encoder, false);
		break;
	}
}

static void psr_set_state(struct psr_drv *psr, enum psr_state state)
{
	psr->request_state = state;

	schedule_delayed_work(&psr->state_work, PSR_SET_DELAY_TIME);
}

static void psr_flush_handler(unsigned long data)
{
	struct psr_drv *psr = (struct psr_drv *)data;

	if (!psr)
		return;

	/* State changed between flush time, then keep it */
	if (psr->request_state != PSR_FLUSH)
		return;

	psr_set_state(psr, PSR_ENABLE);
}

/**
 * rockchip_drm_psr_enable - enable the encoder PSR which bind to given CRTC
 * @crtc: CRTC to obtain the PSR encoder
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int rockchip_drm_psr_enable(struct drm_crtc *crtc)
{
	struct psr_drv *psr = find_psr_by_crtc(crtc);

	if (IS_ERR(psr))
		return PTR_ERR(psr);

	psr_set_state(psr, PSR_ENABLE);
	return 0;
}
EXPORT_SYMBOL(rockchip_drm_psr_enable);

/**
 * rockchip_drm_psr_disable - disable the encoder PSR which bind to given CRTC
 * @crtc: CRTC to obtain the PSR encoder
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int rockchip_drm_psr_disable(struct drm_crtc *crtc)
{
	struct psr_drv *psr = find_psr_by_crtc(crtc);

	if (IS_ERR(psr))
		return PTR_ERR(psr);

	psr_set_state(psr, PSR_DISABLE);
	return 0;
}
EXPORT_SYMBOL(rockchip_drm_psr_disable);

/**
 * rockchip_drm_psr_flush - force to flush all registered PSR encoders
 * @dev: drm device
 *
 * Disable the PSR function for all registered encoders, and then enable the
 * PSR function back after PSR_FLUSH_TIMEOUT. If encoder PSR state have been
 * changed during flush time, then keep the state no change after flush
 * timeout.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
void rockchip_drm_psr_flush(struct drm_device *dev)
{
	struct rockchip_drm_private *drm_drv = dev->dev_private;
	struct psr_drv *psr;
	unsigned long flags;

	spin_lock_irqsave(&drm_drv->psr_list_lock, flags);
	list_for_each_entry(psr, &drm_drv->psr_list, list) {
		if (psr->request_state == PSR_DISABLE)
			continue;

		mod_timer(&psr->flush_timer,
			  round_jiffies_up(jiffies + PSR_FLUSH_TIMEOUT));

		psr_set_state(psr, PSR_FLUSH);
	}
	spin_unlock_irqrestore(&drm_drv->psr_list_lock, flags);
}
EXPORT_SYMBOL(rockchip_drm_psr_flush);

/**
 * rockchip_drm_psr_register - register encoder to psr driver
 * @encoder: encoder that obtain the PSR function
 * @psr_set: call back to set PSR state
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int rockchip_drm_psr_register(struct drm_encoder *encoder,
			void (*psr_set)(struct drm_encoder *, bool enable))
{
	struct rockchip_drm_private *drm_drv = encoder->dev->dev_private;
	struct psr_drv *psr;
	unsigned long flags;

	if (!encoder || !psr_set)
		return -EINVAL;

	psr = kzalloc(sizeof(struct psr_drv), GFP_KERNEL);
	if (!psr)
		return -ENOMEM;

	setup_timer(&psr->flush_timer, psr_flush_handler, (unsigned long)psr);

	INIT_DELAYED_WORK(&psr->state_work, psr_state_work);

	psr->state = PSR_DISABLE;
	psr->encoder = encoder;
	psr->set = psr_set;

	spin_lock_irqsave(&drm_drv->psr_list_lock, flags);
	list_add_tail(&psr->list, &drm_drv->psr_list);
	spin_unlock_irqrestore(&drm_drv->psr_list_lock, flags);

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_psr_register);

/**
 * rockchip_drm_psr_unregister - unregister encoder to psr driver
 * @encoder: encoder that obtain the PSR function
 * @psr_set: call back to set PSR state
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
void rockchip_drm_psr_unregister(struct drm_encoder *encoder)
{
	struct rockchip_drm_private *drm_drv = encoder->dev->dev_private;
	struct psr_drv *psr, *n;
	unsigned long flags;

	spin_lock_irqsave(&drm_drv->psr_list_lock, flags);
	list_for_each_entry_safe(psr, n, &drm_drv->psr_list, list) {
		if (psr->encoder == encoder) {
			del_timer(&psr->flush_timer);
			list_del(&psr->list);
			kfree(psr);
		}
	}
	spin_unlock_irqrestore(&drm_drv->psr_list_lock, flags);
}
EXPORT_SYMBOL(rockchip_drm_psr_unregister);
