/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _KOMEDA_KMS_H_
#define _KOMEDA_KMS_H_

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_writeback.h>

/** struct komeda_plane - komeda instance of drm_plane */
struct komeda_plane {
	/** @base: &drm_plane */
	struct drm_plane base;
	/**
	 * @layer:
	 *
	 * represents available layer input pipelines for this plane.
	 *
	 * NOTE:
	 * the layer is not for a specific Layer, but indicate a group of
	 * Layers with same capabilities.
	 */
	struct komeda_layer *layer;
};

/**
 * struct komeda_plane_state
 *
 * The plane_state can be split into two data flow (left/right) and handled
 * by two layers &komeda_plane.layer and &komeda_plane.layer.right
 */
struct komeda_plane_state {
	/** @base: &drm_plane_state */
	struct drm_plane_state base;

	/* private properties */
};

/**
 * struct komeda_wb_connector
 */
struct komeda_wb_connector {
	/** @base: &drm_writeback_connector */
	struct drm_writeback_connector base;

	/** @wb_layer: represents associated writeback pipeline of komeda */
	struct komeda_layer *wb_layer;
};

/**
 * struct komeda_crtc
 */
struct komeda_crtc {
	/** @base: &drm_crtc */
	struct drm_crtc base;
	/** @master: only master has display output */
	struct komeda_pipeline *master;
	/**
	 * @slave: optional
	 *
	 * Doesn't have its own display output, the handled data flow will
	 * merge into the master.
	 */
	struct komeda_pipeline *slave;
};

/** struct komeda_crtc_state */
struct komeda_crtc_state {
	/** @base: &drm_crtc_state */
	struct drm_crtc_state base;

	/* private properties */

	/* computed state which are used by validate/check */
	u32 affected_pipes;
	u32 active_pipes;
};

/** struct komeda_kms_dev - for gather KMS related things */
struct komeda_kms_dev {
	/** @base: &drm_device */
	struct drm_device base;

	/** @n_crtcs: valid numbers of crtcs in &komeda_kms_dev.crtcs */
	int n_crtcs;
	/** @crtcs: crtcs list */
	struct komeda_crtc crtcs[KOMEDA_MAX_PIPELINES];
};

#define to_kplane(p)	container_of(p, struct komeda_plane, base)
#define to_kplane_st(p)	container_of(p, struct komeda_plane_state, base)
#define to_kconn(p)	container_of(p, struct komeda_wb_connector, base)
#define to_kcrtc(p)	container_of(p, struct komeda_crtc, base)
#define to_kcrtc_st(p)	container_of(p, struct komeda_crtc_state, base)
#define to_kdev(p)	container_of(p, struct komeda_kms_dev, base)

int komeda_kms_setup_crtcs(struct komeda_kms_dev *kms, struct komeda_dev *mdev);

int komeda_kms_add_crtcs(struct komeda_kms_dev *kms, struct komeda_dev *mdev);
int komeda_kms_add_planes(struct komeda_kms_dev *kms, struct komeda_dev *mdev);
int komeda_kms_add_private_objs(struct komeda_kms_dev *kms,
				struct komeda_dev *mdev);
void komeda_kms_cleanup_private_objs(struct komeda_dev *mdev);

struct komeda_kms_dev *komeda_kms_attach(struct komeda_dev *mdev);
void komeda_kms_detach(struct komeda_kms_dev *kms);

#endif /*_KOMEDA_KMS_H_*/
