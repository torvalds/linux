/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MDP_KMS_H__
#define __MDP_KMS_H__

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "mdp_common.xml.h"

struct mdp_kms;

struct mdp_kms_funcs {
	struct msm_kms_funcs base;
	void (*set_irqmask)(struct mdp_kms *mdp_kms, uint32_t irqmask);
};

struct mdp_kms {
	struct msm_kms base;

	const struct mdp_kms_funcs *funcs;

	/* irq handling: */
	bool in_irq;
	struct list_head irq_list;    /* list of mdp4_irq */
	uint32_t vblank_mask;         /* irq bits set for userspace vblank */
};
#define to_mdp_kms(x) container_of(x, struct mdp_kms, base)

static inline void mdp_kms_init(struct mdp_kms *mdp_kms,
		const struct mdp_kms_funcs *funcs)
{
	mdp_kms->funcs = funcs;
	INIT_LIST_HEAD(&mdp_kms->irq_list);
	msm_kms_init(&mdp_kms->base, &funcs->base);
}

/*
 * irq helpers:
 */

/* For transiently registering for different MDP irqs that various parts
 * of the KMS code need during setup/configuration.  These are not
 * necessarily the same as what drm_vblank_get/put() are requesting, and
 * the hysteresis in drm_vblank_put() is not necessarily desirable for
 * internal housekeeping related irq usage.
 */
struct mdp_irq {
	struct list_head node;
	uint32_t irqmask;
	bool registered;
	void (*irq)(struct mdp_irq *irq, uint32_t irqstatus);
};

void mdp_dispatch_irqs(struct mdp_kms *mdp_kms, uint32_t status);
void mdp_update_vblank_mask(struct mdp_kms *mdp_kms, uint32_t mask, bool enable);
void mdp_irq_wait(struct mdp_kms *mdp_kms, uint32_t irqmask);
void mdp_irq_register(struct mdp_kms *mdp_kms, struct mdp_irq *irq);
void mdp_irq_unregister(struct mdp_kms *mdp_kms, struct mdp_irq *irq);
void mdp_irq_update(struct mdp_kms *mdp_kms);

/*
 * pixel format helpers:
 */

struct mdp_format {
	struct msm_format base;
	enum mdp_bpc bpc_r, bpc_g, bpc_b;
	enum mdp_bpc_alpha bpc_a;
	uint8_t unpack[4];
	bool alpha_enable, unpack_tight;
	uint8_t cpp, unpack_count;
};
#define to_mdp_format(x) container_of(x, struct mdp_format, base)

uint32_t mdp_get_formats(uint32_t *formats, uint32_t max_formats);
const struct msm_format *mdp_get_format(struct msm_kms *kms, uint32_t format);

#endif /* __MDP_KMS_H__ */
