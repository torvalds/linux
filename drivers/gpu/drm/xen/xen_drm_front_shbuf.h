/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_DRM_FRONT_SHBUF_H_
#define __XEN_DRM_FRONT_SHBUF_H_

#include <linux/kernel.h>
#include <linux/scatterlist.h>

#include <xen/grant_table.h>

struct xen_drm_front_shbuf {
	/*
	 * number of references granted for the backend use:
	 *  - for allocated/imported dma-buf's this holds number of grant
	 *    references for the page directory and pages of the buffer
	 *  - for the buffer provided by the backend this holds number of
	 *    grant references for the page directory as grant references for
	 *    the buffer will be provided by the backend
	 */
	int num_grefs;
	grant_ref_t *grefs;
	unsigned char *directory;

	/*
	 * there are 2 ways to provide backing storage for this shared buffer:
	 * either pages or sgt. if buffer created from sgt then we own
	 * the pages and must free those ourselves on closure
	 */
	int num_pages;
	struct page **pages;

	struct sg_table *sgt;

	struct xenbus_device *xb_dev;

	/* these are the ops used internally depending on be_alloc mode */
	const struct xen_drm_front_shbuf_ops *ops;

	/* Xen map handles for the buffer allocated by the backend */
	grant_handle_t *backend_map_handles;
};

struct xen_drm_front_shbuf_cfg {
	struct xenbus_device *xb_dev;
	size_t size;
	struct page **pages;
	struct sg_table *sgt;
	bool be_alloc;
};

struct xen_drm_front_shbuf *
xen_drm_front_shbuf_alloc(struct xen_drm_front_shbuf_cfg *cfg);

grant_ref_t xen_drm_front_shbuf_get_dir_start(struct xen_drm_front_shbuf *buf);

int xen_drm_front_shbuf_map(struct xen_drm_front_shbuf *buf);

int xen_drm_front_shbuf_unmap(struct xen_drm_front_shbuf *buf);

void xen_drm_front_shbuf_flush(struct xen_drm_front_shbuf *buf);

void xen_drm_front_shbuf_free(struct xen_drm_front_shbuf *buf);

#endif /* __XEN_DRM_FRONT_SHBUF_H_ */
