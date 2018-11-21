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

#define PSR_FLUSH_TIMEOUT_MS	100

enum psr_state {
	PSR_FLUSH,
	PSR_ENABLE,
	PSR_DISABLE,
};

struct psr_drv {
	struct list_head	list;
	struct drm_encoder	*encoder;

	struct mutex		lock;
	bool			active;
	enum psr_state		state;

	struct delayed_work	flush_work;

	void (*set)(struct drm_encoder *encoder, bool enable);
};

static struct psr_drv *find_psr_by_crtc(struct drm_crtc *crtc)
{
	struct rockchip_drm_private *drm_drv = crtc->dev->dev_private;
	struct psr_drv *psr;

	mutex_lock(&drm_drv->psr_list_lock);
	list_for_each_entry(psr, &drm_drv->psr_list, list) {
		if (psr->encoder->crtc == crtc)
			goto out;
	}
	psr = ERR_PTR(-ENODEV);

out:
	mutex_unlock(&drm_drv->psr_list_lock);
	return psr;
}

static struct psr_drv *find_psr_by_encoder(struct drm_encoder *encoder)
{
	struct rockchip_drm_private *drm_drv = encoder->dev->dev_private;
	struct psr_drv *psr;

	mutex_lock(&drm_drv->psr_list_lock);
	list_for_each_entry(psr, &drm_drv->psr_list, list) {
		if (psr->encoder == encoder)
			goto out;
	}
	psr = ERR_PTR(-ENODEV);

out:
	mutex_unlock(&drm_drv->psr_list_lock);
	return psr;
}

static void psr_set_state_locked(struct psr_drv *psr, enum psr_state state)
{
	/*
	 * Allowed finite state machine:
	 *
	 *   PSR_ENABLE  < = = = = = >  PSR_FLUSH
	 *       | ^                        |
	 *       | |                        |
	 *       v |                        |
	 *   PSR_DISABLE < - - - - - - - - -
	 */
	if (state == psr->state || !psr->active)
		return;

	/* Already disabled in flush, change the state, but not the hardware */
	if (state == PSR_DISABLE && psr->state == PSR_FLUSH) {
		psr->state = state;
		return;
	}

	psr->state = state;

	/* Actually commit the state change to hardware */
	switch (psr->state) {
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
	mutex_lock(&psr->lock);
	psr_set_state_locked(psr, state);
	mutex_unlock(&psr->lock);
}

static void psr_flush_handler(struct work_struct *work)
{
	struct psr_drv *psr = container_of(to_delayed_work(work),
					   struct psr_drv, flush_work);

	/* If the state has changed since we initiated the flush, do nothing */
	mutex_lock(&psr->lock);
	if (psr->state == PSR_FLUSH)
		psr_set_state_locked(psr, PSR_ENABLE);
	mutex_unlock(&psr->lock);
}

/**
 * rockchip_drm_psr_activate - activate PSR on the given pipe
 * @encoder: encoder to obtain the PSR encoder
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int rockchip_drm_psr_activate(struct drm_encoder *encoder)
{
	struct psr_drv *psr = find_psr_by_encoder(encoder);

	if (IS_ERR(psr))
		return PTR_ERR(psr);

	mutex_lock(&psr->lock);
	psr->active = true;
	mutex_unlock(&psr->lock);

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_psr_activate);

/**
 * rockchip_drm_psr_deactivate - deactivate PSR on the given pipe
 * @encoder: encoder to obtain the PSR encoder
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int rockchip_drm_psr_deactivate(struct drm_encoder *encoder)
{
	struct psr_drv *psr = find_psr_by_encoder(encoder);

	if (IS_ERR(psr))
		return PTR_ERR(psr);

	mutex_lock(&psr->lock);
	psr->active = false;
	mutex_unlock(&psr->lock);
	cancel_delayed_work_sync(&psr->flush_work);

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_psr_deactivate);

static void rockchip_drm_do_flush(struct psr_drv *psr)
{
	psr_set_state(psr, PSR_FLUSH);
	mod_delayed_work(system_wq, &psr->flush_work, PSR_FLUSH_TIMEOUT_MS);
}

/**
 * rockchip_drm_psr_flush - flush a single pipe
 * @crtc: CRTC of the pipe to flush
 *
 * Returns:
 * 0 on success, -errno on fail
 */
int rockchip_drm_psr_flush(struct drm_crtc *crtc)
{
	struct psr_drv *psr = find_psr_by_crtc(crtc);
	if (IS_ERR(psr))
		return PTR_ERR(psr);

	rockchip_drm_do_flush(psr);
	return 0;
}
EXPORT_SYMBOL(rockchip_drm_psr_flush);

/**
 * rockchip_drm_psr_flush_all - force to flush all registered PSR encoders
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
void rockchip_drm_psr_flush_all(struct drm_device *dev)
{
	struct rockchip_drm_private *drm_drv = dev->dev_private;
	struct psr_drv *psr;

	mutex_lock(&drm_drv->psr_list_lock);
	list_for_each_entry(psr, &drm_drv->psr_list, list)
		rockchip_drm_do_flush(psr);
	mutex_unlock(&drm_drv->psr_list_lock);
}
EXPORT_SYMBOL(rockchip_drm_psr_flush_all);

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

	if (!encoder || !psr_set)
		return -EINVAL;

	psr = kzalloc(sizeof(struct psr_drv), GFP_KERNEL);
	if (!psr)
		return -ENOMEM;

	INIT_DELAYED_WORK(&psr->flush_work, psr_flush_handler);
	mutex_init(&psr->lock);

	psr->active = true;
	psr->state = PSR_DISABLE;
	psr->encoder = encoder;
	psr->set = psr_set;

	mutex_lock(&drm_drv->psr_list_lock);
	list_add_tail(&psr->list, &drm_drv->psr_list);
	mutex_unlock(&drm_drv->psr_list_lock);

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

	mutex_lock(&drm_drv->psr_list_lock);
	list_for_each_entry_safe(psr, n, &drm_drv->psr_list, list) {
		if (psr->encoder == encoder) {
			cancel_delayed_work_sync(&psr->flush_work);
			list_del(&psr->list);
			kfree(psr);
		}
	}
	mutex_unlock(&drm_drv->psr_list_lock);
}
EXPORT_SYMBOL(rockchip_drm_psr_unregister);
