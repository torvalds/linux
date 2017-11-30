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
	/* swap global atomic state: */
	void (*swap_state)(struct msm_kms *kms, struct drm_atomic_state *state);
	/* modeset, bracketing atomic_commit(): */
	void (*prepare_commit)(struct msm_kms *kms, struct drm_atomic_state *state);
	void (*complete_commit)(struct msm_kms *kms, struct drm_atomic_state *state);
	/* functions to wait for atomic commit completed on each CRTC */
	void (*wait_for_crtc_commit_done)(struct msm_kms *kms,
					struct drm_crtc *crtc);
	/* misc: */
	const struct msm_format *(*get_format)(struct msm_kms *kms, uint32_t format);
	long (*round_pixclk)(struct msm_kms *kms, unsigned long rate,
			struct drm_encoder *encoder);
	int (*set_split_display)(struct msm_kms *kms,
			struct drm_encoder *encoder,
			struct drm_encoder *slave_encoder,
			bool is_cmd_mode);
	void (*set_encoder_mode)(struct msm_kms *kms,
				 struct drm_encoder *encoder,
				 bool cmd_mode);
	/* cleanup: */
	void (*destroy)(struct msm_kms *kms);
#ifdef CONFIG_DEBUG_FS
	/* debugfs: */
	int (*debugfs_init)(struct msm_kms *kms, struct drm_minor *minor);
#endif
};

struct msm_kms {
	const struct msm_kms_funcs *funcs;

	/* irq number to be passed on to drm_irq_install */
	int irq;

	/* mapper-id used to request GEM buffer mapped for scanout: */
	struct msm_gem_address_space *aspace;
};

/**
 * Subclass of drm_atomic_state, to allow kms backend to have driver
 * private global state.  The kms backend can do whatever it wants
 * with the ->state ptr.  On ->atomic_state_clear() the ->state ptr
 * is kfree'd and set back to NULL.
 */
struct msm_kms_state {
	struct drm_atomic_state base;
	void *state;
};
#define to_kms_state(x) container_of(x, struct msm_kms_state, base)

static inline void msm_kms_init(struct msm_kms *kms,
		const struct msm_kms_funcs *funcs)
{
	kms->funcs = funcs;
}

struct msm_kms *mdp4_kms_init(struct drm_device *dev);
struct msm_kms *mdp5_kms_init(struct drm_device *dev);
int msm_mdss_init(struct drm_device *dev);
void msm_mdss_destroy(struct drm_device *dev);
int msm_mdss_enable(struct msm_mdss *mdss);
int msm_mdss_disable(struct msm_mdss *mdss);

#endif /* __MSM_KMS_H__ */
