// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <drm/drmP.h>

#if defined(CONFIG_X86)
#include <drm/drm_cache.h>
#endif
#include <linux/errno.h>
#include <linux/mm.h>

#include <asm/xen/hypervisor.h>
#include <xen/balloon.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/io/displif.h>

#include "xen_drm_front.h"
#include "xen_drm_front_shbuf.h"

struct xen_drm_front_shbuf_ops {
	/*
	 * Calculate number of grefs required to handle this buffer,
	 * e.g. if grefs are required for page directory only or the buffer
	 * pages as well.
	 */
	void (*calc_num_grefs)(struct xen_drm_front_shbuf *buf);
	/* Fill page directory according to para-virtual display protocol. */
	void (*fill_page_dir)(struct xen_drm_front_shbuf *buf);
	/* Claim grant references for the pages of the buffer. */
	int (*grant_refs_for_buffer)(struct xen_drm_front_shbuf *buf,
				     grant_ref_t *priv_gref_head, int gref_idx);
	/* Map grant references of the buffer. */
	int (*map)(struct xen_drm_front_shbuf *buf);
	/* Unmap grant references of the buffer. */
	int (*unmap)(struct xen_drm_front_shbuf *buf);
};

grant_ref_t xen_drm_front_shbuf_get_dir_start(struct xen_drm_front_shbuf *buf)
{
	if (!buf->grefs)
		return GRANT_INVALID_REF;

	return buf->grefs[0];
}

int xen_drm_front_shbuf_map(struct xen_drm_front_shbuf *buf)
{
	if (buf->ops->map)
		return buf->ops->map(buf);

	/* no need to map own grant references */
	return 0;
}

int xen_drm_front_shbuf_unmap(struct xen_drm_front_shbuf *buf)
{
	if (buf->ops->unmap)
		return buf->ops->unmap(buf);

	/* no need to unmap own grant references */
	return 0;
}

void xen_drm_front_shbuf_flush(struct xen_drm_front_shbuf *buf)
{
#if defined(CONFIG_X86)
	drm_clflush_pages(buf->pages, buf->num_pages);
#endif
}

void xen_drm_front_shbuf_free(struct xen_drm_front_shbuf *buf)
{
	if (buf->grefs) {
		int i;

		for (i = 0; i < buf->num_grefs; i++)
			if (buf->grefs[i] != GRANT_INVALID_REF)
				gnttab_end_foreign_access(buf->grefs[i],
							  0, 0UL);
	}
	kfree(buf->grefs);
	kfree(buf->directory);
	kfree(buf);
}

/*
 * number of grefs a page can hold with respect to the
 * struct xendispl_page_directory header
 */
#define XEN_DRM_NUM_GREFS_PER_PAGE ((PAGE_SIZE - \
		offsetof(struct xendispl_page_directory, gref)) / \
		sizeof(grant_ref_t))

static int get_num_pages_dir(struct xen_drm_front_shbuf *buf)
{
	/* number of pages the page directory consumes itself */
	return DIV_ROUND_UP(buf->num_pages, XEN_DRM_NUM_GREFS_PER_PAGE);
}

static void backend_calc_num_grefs(struct xen_drm_front_shbuf *buf)
{
	/* only for pages the page directory consumes itself */
	buf->num_grefs = get_num_pages_dir(buf);
}

static void guest_calc_num_grefs(struct xen_drm_front_shbuf *buf)
{
	/*
	 * number of pages the page directory consumes itself
	 * plus grefs for the buffer pages
	 */
	buf->num_grefs = get_num_pages_dir(buf) + buf->num_pages;
}

#define xen_page_to_vaddr(page) \
		((uintptr_t)pfn_to_kaddr(page_to_xen_pfn(page)))

static int backend_unmap(struct xen_drm_front_shbuf *buf)
{
	struct gnttab_unmap_grant_ref *unmap_ops;
	int i, ret;

	if (!buf->pages || !buf->backend_map_handles || !buf->grefs)
		return 0;

	unmap_ops = kcalloc(buf->num_pages, sizeof(*unmap_ops),
			    GFP_KERNEL);
	if (!unmap_ops) {
		DRM_ERROR("Failed to get memory while unmapping\n");
		return -ENOMEM;
	}

	for (i = 0; i < buf->num_pages; i++) {
		phys_addr_t addr;

		addr = xen_page_to_vaddr(buf->pages[i]);
		gnttab_set_unmap_op(&unmap_ops[i], addr, GNTMAP_host_map,
				    buf->backend_map_handles[i]);
	}

	ret = gnttab_unmap_refs(unmap_ops, NULL, buf->pages,
				buf->num_pages);

	for (i = 0; i < buf->num_pages; i++) {
		if (unlikely(unmap_ops[i].status != GNTST_okay))
			DRM_ERROR("Failed to unmap page %d: %d\n",
				  i, unmap_ops[i].status);
	}

	if (ret)
		DRM_ERROR("Failed to unmap grant references, ret %d", ret);

	kfree(unmap_ops);
	kfree(buf->backend_map_handles);
	buf->backend_map_handles = NULL;
	return ret;
}

static int backend_map(struct xen_drm_front_shbuf *buf)
{
	struct gnttab_map_grant_ref *map_ops = NULL;
	unsigned char *ptr;
	int ret, cur_gref, cur_dir_page, cur_page, grefs_left;

	map_ops = kcalloc(buf->num_pages, sizeof(*map_ops), GFP_KERNEL);
	if (!map_ops)
		return -ENOMEM;

	buf->backend_map_handles = kcalloc(buf->num_pages,
					   sizeof(*buf->backend_map_handles),
					   GFP_KERNEL);
	if (!buf->backend_map_handles) {
		kfree(map_ops);
		return -ENOMEM;
	}

	/*
	 * read page directory to get grefs from the backend: for external
	 * buffer we only allocate buf->grefs for the page directory,
	 * so buf->num_grefs has number of pages in the page directory itself
	 */
	ptr = buf->directory;
	grefs_left = buf->num_pages;
	cur_page = 0;
	for (cur_dir_page = 0; cur_dir_page < buf->num_grefs; cur_dir_page++) {
		struct xendispl_page_directory *page_dir =
				(struct xendispl_page_directory *)ptr;
		int to_copy = XEN_DRM_NUM_GREFS_PER_PAGE;

		if (to_copy > grefs_left)
			to_copy = grefs_left;

		for (cur_gref = 0; cur_gref < to_copy; cur_gref++) {
			phys_addr_t addr;

			addr = xen_page_to_vaddr(buf->pages[cur_page]);
			gnttab_set_map_op(&map_ops[cur_page], addr,
					  GNTMAP_host_map,
					  page_dir->gref[cur_gref],
					  buf->xb_dev->otherend_id);
			cur_page++;
		}

		grefs_left -= to_copy;
		ptr += PAGE_SIZE;
	}
	ret = gnttab_map_refs(map_ops, NULL, buf->pages, buf->num_pages);

	/* save handles even if error, so we can unmap */
	for (cur_page = 0; cur_page < buf->num_pages; cur_page++) {
		buf->backend_map_handles[cur_page] = map_ops[cur_page].handle;
		if (unlikely(map_ops[cur_page].status != GNTST_okay))
			DRM_ERROR("Failed to map page %d: %d\n",
				  cur_page, map_ops[cur_page].status);
	}

	if (ret) {
		DRM_ERROR("Failed to map grant references, ret %d", ret);
		backend_unmap(buf);
	}

	kfree(map_ops);
	return ret;
}

static void backend_fill_page_dir(struct xen_drm_front_shbuf *buf)
{
	struct xendispl_page_directory *page_dir;
	unsigned char *ptr;
	int i, num_pages_dir;

	ptr = buf->directory;
	num_pages_dir = get_num_pages_dir(buf);

	/* fill only grefs for the page directory itself */
	for (i = 0; i < num_pages_dir - 1; i++) {
		page_dir = (struct xendispl_page_directory *)ptr;

		page_dir->gref_dir_next_page = buf->grefs[i + 1];
		ptr += PAGE_SIZE;
	}
	/* last page must say there is no more pages */
	page_dir = (struct xendispl_page_directory *)ptr;
	page_dir->gref_dir_next_page = GRANT_INVALID_REF;
}

static void guest_fill_page_dir(struct xen_drm_front_shbuf *buf)
{
	unsigned char *ptr;
	int cur_gref, grefs_left, to_copy, i, num_pages_dir;

	ptr = buf->directory;
	num_pages_dir = get_num_pages_dir(buf);

	/*
	 * while copying, skip grefs at start, they are for pages
	 * granted for the page directory itself
	 */
	cur_gref = num_pages_dir;
	grefs_left = buf->num_pages;
	for (i = 0; i < num_pages_dir; i++) {
		struct xendispl_page_directory *page_dir =
				(struct xendispl_page_directory *)ptr;

		if (grefs_left <= XEN_DRM_NUM_GREFS_PER_PAGE) {
			to_copy = grefs_left;
			page_dir->gref_dir_next_page = GRANT_INVALID_REF;
		} else {
			to_copy = XEN_DRM_NUM_GREFS_PER_PAGE;
			page_dir->gref_dir_next_page = buf->grefs[i + 1];
		}
		memcpy(&page_dir->gref, &buf->grefs[cur_gref],
		       to_copy * sizeof(grant_ref_t));
		ptr += PAGE_SIZE;
		grefs_left -= to_copy;
		cur_gref += to_copy;
	}
}

static int guest_grant_refs_for_buffer(struct xen_drm_front_shbuf *buf,
				       grant_ref_t *priv_gref_head,
				       int gref_idx)
{
	int i, cur_ref, otherend_id;

	otherend_id = buf->xb_dev->otherend_id;
	for (i = 0; i < buf->num_pages; i++) {
		cur_ref = gnttab_claim_grant_reference(priv_gref_head);
		if (cur_ref < 0)
			return cur_ref;

		gnttab_grant_foreign_access_ref(cur_ref, otherend_id,
						xen_page_to_gfn(buf->pages[i]),
						0);
		buf->grefs[gref_idx++] = cur_ref;
	}
	return 0;
}

static int grant_references(struct xen_drm_front_shbuf *buf)
{
	grant_ref_t priv_gref_head;
	int ret, i, j, cur_ref;
	int otherend_id, num_pages_dir;

	ret = gnttab_alloc_grant_references(buf->num_grefs, &priv_gref_head);
	if (ret < 0) {
		DRM_ERROR("Cannot allocate grant references\n");
		return ret;
	}

	otherend_id = buf->xb_dev->otherend_id;
	j = 0;
	num_pages_dir = get_num_pages_dir(buf);
	for (i = 0; i < num_pages_dir; i++) {
		unsigned long frame;

		cur_ref = gnttab_claim_grant_reference(&priv_gref_head);
		if (cur_ref < 0)
			return cur_ref;

		frame = xen_page_to_gfn(virt_to_page(buf->directory +
					PAGE_SIZE * i));
		gnttab_grant_foreign_access_ref(cur_ref, otherend_id, frame, 0);
		buf->grefs[j++] = cur_ref;
	}

	if (buf->ops->grant_refs_for_buffer) {
		ret = buf->ops->grant_refs_for_buffer(buf, &priv_gref_head, j);
		if (ret)
			return ret;
	}

	gnttab_free_grant_references(priv_gref_head);
	return 0;
}

static int alloc_storage(struct xen_drm_front_shbuf *buf)
{
	buf->grefs = kcalloc(buf->num_grefs, sizeof(*buf->grefs), GFP_KERNEL);
	if (!buf->grefs)
		return -ENOMEM;

	buf->directory = kcalloc(get_num_pages_dir(buf), PAGE_SIZE, GFP_KERNEL);
	if (!buf->directory)
		return -ENOMEM;

	return 0;
}

/*
 * For be allocated buffers we don't need grant_refs_for_buffer as those
 * grant references are allocated at backend side
 */
static const struct xen_drm_front_shbuf_ops backend_ops = {
	.calc_num_grefs = backend_calc_num_grefs,
	.fill_page_dir = backend_fill_page_dir,
	.map = backend_map,
	.unmap = backend_unmap
};

/* For locally granted references we do not need to map/unmap the references */
static const struct xen_drm_front_shbuf_ops local_ops = {
	.calc_num_grefs = guest_calc_num_grefs,
	.fill_page_dir = guest_fill_page_dir,
	.grant_refs_for_buffer = guest_grant_refs_for_buffer,
};

struct xen_drm_front_shbuf *
xen_drm_front_shbuf_alloc(struct xen_drm_front_shbuf_cfg *cfg)
{
	struct xen_drm_front_shbuf *buf;
	int ret;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	if (cfg->be_alloc)
		buf->ops = &backend_ops;
	else
		buf->ops = &local_ops;

	buf->xb_dev = cfg->xb_dev;
	buf->num_pages = DIV_ROUND_UP(cfg->size, PAGE_SIZE);
	buf->pages = cfg->pages;

	buf->ops->calc_num_grefs(buf);

	ret = alloc_storage(buf);
	if (ret)
		goto fail;

	ret = grant_references(buf);
	if (ret)
		goto fail;

	buf->ops->fill_page_dir(buf);

	return buf;

fail:
	xen_drm_front_shbuf_free(buf);
	return ERR_PTR(ret);
}
