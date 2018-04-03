/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_DRM_FRONT_H_
#define __XEN_DRM_FRONT_H_

#include <drm/drmP.h>
#include <drm/drm_simple_kms_helper.h>

#include <linux/scatterlist.h>

#include "xen_drm_front_cfg.h"

/**
 * DOC: Driver modes of operation in terms of display buffers used
 *
 * Depending on the requirements for the para-virtualized environment, namely
 * requirements dictated by the accompanying DRM/(v)GPU drivers running in both
 * host and guest environments, number of operating modes of para-virtualized
 * display driver are supported:
 *
 * - display buffers can be allocated by either frontend driver or backend
 * - display buffers can be allocated to be contiguous in memory or not
 *
 * Note! Frontend driver itself has no dependency on contiguous memory for
 * its operation.
 */

/**
 * DOC: Buffers allocated by the frontend driver
 *
 * The below modes of operation are configured at compile-time via
 * frontend driver's kernel configuration:
 */

/**
 * DOC: With GEM CMA helpers
 *
 * This use-case is useful when used with accompanying DRM/vGPU driver in
 * guest domain which was designed to only work with contiguous buffers,
 * e.g. DRM driver based on GEM CMA helpers: such drivers can only import
 * contiguous PRIME buffers, thus requiring frontend driver to provide
 * such. In order to implement this mode of operation para-virtualized
 * frontend driver can be configured to use GEM CMA helpers.
 */

/**
 * DOC: Without GEM CMA helpers
 *
 * If accompanying drivers can cope with non-contiguous memory then, to
 * lower pressure on CMA subsystem of the kernel, driver can allocate
 * buffers from system memory.
 *
 * Note! If used with accompanying DRM/(v)GPU drivers this mode of operation
 * may require IOMMU support on the platform, so accompanying DRM/vGPU
 * hardware can still reach display buffer memory while importing PRIME
 * buffers from the frontend driver.
 */

/**
 * DOC: Buffers allocated by the backend
 *
 * This mode of operation is run-time configured via guest domain configuration
 * through XenStore entries.
 *
 * For systems which do not provide IOMMU support, but having specific
 * requirements for display buffers it is possible to allocate such buffers
 * at backend side and share those with the frontend.
 * For example, if host domain is 1:1 mapped and has DRM/GPU hardware expecting
 * physically contiguous memory, this allows implementing zero-copying
 * use-cases.
 *
 * Note, while using this scenario the following should be considered:
 *
 * #. If guest domain dies then pages/grants received from the backend
 *    cannot be claimed back
 *
 * #. Misbehaving guest may send too many requests to the
 *    backend exhausting its grant references and memory
 *    (consider this from security POV)
 */

/**
 * DOC: Driver limitations
 *
 * #. Only primary plane without additional properties is supported.
 *
 * #. Only one video mode per connector supported which is configured
 *    via XenStore.
 *
 * #. All CRTCs operate at fixed frequency of 60Hz.
 */

/* timeout in ms to wait for backend to respond */
#define XEN_DRM_FRONT_WAIT_BACK_MS	3000

#ifndef GRANT_INVALID_REF
/*
 * Note on usage of grant reference 0 as invalid grant reference:
 * grant reference 0 is valid, but never exposed to a PV driver,
 * because of the fact it is already in use/reserved by the PV console.
 */
#define GRANT_INVALID_REF	0
#endif

struct xen_drm_front_info {
	struct xenbus_device *xb_dev;
	struct xen_drm_front_drm_info *drm_info;

	/* to protect data between backend IO code and interrupt handler */
	spinlock_t io_lock;

	int num_evt_pairs;
	struct xen_drm_front_evtchnl_pair *evt_pairs;
	struct xen_drm_front_cfg cfg;

	/* display buffers */
	struct list_head dbuf_list;
};

struct xen_drm_front_drm_pipeline {
	struct xen_drm_front_drm_info *drm_info;

	int index;

	struct drm_simple_display_pipe pipe;

	struct drm_connector conn;
	/* These are only for connector mode checking */
	int width, height;

	struct drm_pending_vblank_event *pending_event;

	struct delayed_work pflip_to_worker;

	bool conn_connected;
};

struct xen_drm_front_drm_info {
	struct xen_drm_front_info *front_info;
	struct drm_device *drm_dev;

	struct xen_drm_front_drm_pipeline pipeline[XEN_DRM_FRONT_MAX_CRTCS];
};

static inline u64 xen_drm_front_fb_to_cookie(struct drm_framebuffer *fb)
{
	return (u64)fb;
}

static inline u64 xen_drm_front_dbuf_to_cookie(struct drm_gem_object *gem_obj)
{
	return (u64)gem_obj;
}

int xen_drm_front_mode_set(struct xen_drm_front_drm_pipeline *pipeline,
			   u32 x, u32 y, u32 width, u32 height,
			   u32 bpp, u64 fb_cookie);

int xen_drm_front_dbuf_create_from_sgt(struct xen_drm_front_info *front_info,
				       u64 dbuf_cookie, u32 width, u32 height,
				       u32 bpp, u64 size, struct sg_table *sgt);

int xen_drm_front_dbuf_create_from_pages(struct xen_drm_front_info *front_info,
					 u64 dbuf_cookie, u32 width, u32 height,
					 u32 bpp, u64 size, struct page **pages);

int xen_drm_front_fb_attach(struct xen_drm_front_info *front_info,
			    u64 dbuf_cookie, u64 fb_cookie, u32 width,
			    u32 height, u32 pixel_format);

int xen_drm_front_fb_detach(struct xen_drm_front_info *front_info,
			    u64 fb_cookie);

int xen_drm_front_page_flip(struct xen_drm_front_info *front_info,
			    int conn_idx, u64 fb_cookie);

void xen_drm_front_on_frame_done(struct xen_drm_front_info *front_info,
				 int conn_idx, u64 fb_cookie);

#endif /* __XEN_DRM_FRONT_H_ */
