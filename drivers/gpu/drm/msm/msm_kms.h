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

#ifndef __MSM_KMS_H__
#define __MSM_KMS_H__

#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include "msm_drv.h"

#define MAX_PLANE	4

/* As there are different display controller blocks depending on the
 * snapdragon version, the kms support is split out and the appropriate
 * implementation is loaded at runtime.  The kms module is responsible
 * for constructing the appropriate planes/crtcs/encoders/connectors.
 */
struct msm_kms_funcs {
	/* hw initialization: */
	int (*hw_init)(struct msm_kms *kms);
	/* irq handling: */
	void (*irq_preinstall)(struct msm_kms *kms);
	int (*irq_postinstall)(struct msm_kms *kms);
	void (*irq_uninstall)(struct msm_kms *kms);
	irqreturn_t (*irq)(struct msm_kms *kms);
	int (*enable_vblank)(struct msm_kms *kms, struct drm_crtc *crtc);
	void (*disable_vblank)(struct msm_kms *kms, struct drm_crtc *crtc);
	/* modeset, bracketing atomic_commit(): */
	void (*prepare_commit)(struct msm_kms *kms, struct drm_atomic_state *state);
	void (*complete_commit)(struct msm_kms *kms, struct drm_atomic_state *state);
	/* misc: */
	const struct msm_format *(*get_format)(struct msm_kms *kms, uint32_t format);
	long (*round_pixclk)(struct msm_kms *kms, unsigned long rate,
			struct drm_encoder *encoder);
	/* cleanup: */
	void (*preclose)(struct msm_kms *kms, struct drm_file *file);
	void (*destroy)(struct msm_kms *kms);
};

struct msm_kms {
	const struct msm_kms_funcs *funcs;

	/* irq handling: */
	bool in_irq;
	struct list_head irq_list;    /* list of mdp4_irq */
	uint32_t vblank_mask;         /* irq bits set for userspace vblank */
};

static inline void msm_kms_init(struct msm_kms *kms,
		const struct msm_kms_funcs *funcs)
{
	kms->funcs = funcs;
}

struct msm_kms *mdp4_kms_init(struct drm_device *dev);
struct msm_kms *mdp5_kms_init(struct drm_device *dev);

#endif /* __MSM_KMS_H__ */
