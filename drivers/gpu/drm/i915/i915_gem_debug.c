/*
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Packard <keithp@keithp.com>
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#if WATCH_LISTS
int
i915_verify_lists(struct drm_device *dev)
{
	static int warned;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	int err = 0;

	if (warned)
		return 0;

	list_for_each_entry(obj, &dev_priv->render_ring.active_list, list) {
		if (obj->base.dev != dev ||
		    !atomic_read(&obj->base.refcount.refcount)) {
			DRM_ERROR("freed render active %p\n", obj);
			err++;
			break;
		} else if (!obj->active ||
			   (obj->base.read_domains & I915_GEM_GPU_DOMAINS) == 0) {
			DRM_ERROR("invalid render active %p (a %d r %x)\n",
				  obj,
				  obj->active,
				  obj->base.read_domains);
			err++;
		} else if (obj->base.write_domain && list_empty(&obj->gpu_write_list)) {
			DRM_ERROR("invalid render active %p (w %x, gwl %d)\n",
				  obj,
				  obj->base.write_domain,
				  !list_empty(&obj->gpu_write_list));
			err++;
		}
	}

	list_for_each_entry(obj, &dev_priv->mm.flushing_list, list) {
		if (obj->base.dev != dev ||
		    !atomic_read(&obj->base.refcount.refcount)) {
			DRM_ERROR("freed flushing %p\n", obj);
			err++;
			break;
		} else if (!obj->active ||
			   (obj->base.write_domain & I915_GEM_GPU_DOMAINS) == 0 ||
			   list_empty(&obj->gpu_write_list)){
			DRM_ERROR("invalid flushing %p (a %d w %x gwl %d)\n",
				  obj,
				  obj->active,
				  obj->base.write_domain,
				  !list_empty(&obj->gpu_write_list));
			err++;
		}
	}

	list_for_each_entry(obj, &dev_priv->mm.gpu_write_list, gpu_write_list) {
		if (obj->base.dev != dev ||
		    !atomic_read(&obj->base.refcount.refcount)) {
			DRM_ERROR("freed gpu write %p\n", obj);
			err++;
			break;
		} else if (!obj->active ||
			   (obj->base.write_domain & I915_GEM_GPU_DOMAINS) == 0) {
			DRM_ERROR("invalid gpu write %p (a %d w %x)\n",
				  obj,
				  obj->active,
				  obj->base.write_domain);
			err++;
		}
	}

	list_for_each_entry(obj, &dev_priv->mm.inactive_list, list) {
		if (obj->base.dev != dev ||
		    !atomic_read(&obj->base.refcount.refcount)) {
			DRM_ERROR("freed inactive %p\n", obj);
			err++;
			break;
		} else if (obj->pin_count || obj->active ||
			   (obj->base.write_domain & I915_GEM_GPU_DOMAINS)) {
			DRM_ERROR("invalid inactive %p (p %d a %d w %x)\n",
				  obj,
				  obj->pin_count, obj->active,
				  obj->base.write_domain);
			err++;
		}
	}

	list_for_each_entry(obj, &dev_priv->mm.pinned_list, list) {
		if (obj->base.dev != dev ||
		    !atomic_read(&obj->base.refcount.refcount)) {
			DRM_ERROR("freed pinned %p\n", obj);
			err++;
			break;
		} else if (!obj->pin_count || obj->active ||
			   (obj->base.write_domain & I915_GEM_GPU_DOMAINS)) {
			DRM_ERROR("invalid pinned %p (p %d a %d w %x)\n",
				  obj,
				  obj->pin_count, obj->active,
				  obj->base.write_domain);
			err++;
		}
	}

	return warned = err;
}
#endif /* WATCH_INACTIVE */


#if WATCH_EXEC | WATCH_PWRITE
static void
i915_gem_dump_page(struct page *page, uint32_t start, uint32_t end,
		   uint32_t bias, uint32_t mark)
{
	uint32_t *mem = kmap_atomic(page, KM_USER0);
	int i;
	for (i = start; i < end; i += 4)
		DRM_INFO("%08x: %08x%s\n",
			  (int) (bias + i), mem[i / 4],
			  (bias + i == mark) ? " ********" : "");
	kunmap_atomic(mem, KM_USER0);
	/* give syslog time to catch up */
	msleep(1);
}

void
i915_gem_dump_object(struct drm_i915_gem_object *obj, int len,
		     const char *where, uint32_t mark)
{
	int page;

	DRM_INFO("%s: object at offset %08x\n", where, obj->gtt_offset);
	for (page = 0; page < (len + PAGE_SIZE-1) / PAGE_SIZE; page++) {
		int page_len, chunk, chunk_len;

		page_len = len - page * PAGE_SIZE;
		if (page_len > PAGE_SIZE)
			page_len = PAGE_SIZE;

		for (chunk = 0; chunk < page_len; chunk += 128) {
			chunk_len = page_len - chunk;
			if (chunk_len > 128)
				chunk_len = 128;
			i915_gem_dump_page(obj->pages[page],
					   chunk, chunk + chunk_len,
					   obj->gtt_offset +
					   page * PAGE_SIZE,
					   mark);
		}
	}
}
#endif

#if WATCH_COHERENCY
void
i915_gem_object_check_coherency(struct drm_i915_gem_object *obj, int handle)
{
	struct drm_device *dev = obj->base.dev;
	int page;
	uint32_t *gtt_mapping;
	uint32_t *backing_map = NULL;
	int bad_count = 0;

	DRM_INFO("%s: checking coherency of object %p@0x%08x (%d, %zdkb):\n",
		 __func__, obj, obj->gtt_offset, handle,
		 obj->size / 1024);

	gtt_mapping = ioremap(dev->agp->base + obj->gtt_offset, obj->base.size);
	if (gtt_mapping == NULL) {
		DRM_ERROR("failed to map GTT space\n");
		return;
	}

	for (page = 0; page < obj->size / PAGE_SIZE; page++) {
		int i;

		backing_map = kmap_atomic(obj->pages[page], KM_USER0);

		if (backing_map == NULL) {
			DRM_ERROR("failed to map backing page\n");
			goto out;
		}

		for (i = 0; i < PAGE_SIZE / 4; i++) {
			uint32_t cpuval = backing_map[i];
			uint32_t gttval = readl(gtt_mapping +
						page * 1024 + i);

			if (cpuval != gttval) {
				DRM_INFO("incoherent CPU vs GPU at 0x%08x: "
					 "0x%08x vs 0x%08x\n",
					 (int)(obj->gtt_offset +
					       page * PAGE_SIZE + i * 4),
					 cpuval, gttval);
				if (bad_count++ >= 8) {
					DRM_INFO("...\n");
					goto out;
				}
			}
		}
		kunmap_atomic(backing_map, KM_USER0);
		backing_map = NULL;
	}

 out:
	if (backing_map != NULL)
		kunmap_atomic(backing_map, KM_USER0);
	iounmap(gtt_mapping);

	/* give syslog time to catch up */
	msleep(1);

	/* Directly flush the object, since we just loaded values with the CPU
	 * from the backing pages and we don't want to disturb the cache
	 * management that we're trying to observe.
	 */

	i915_gem_clflush_object(obj);
}
#endif
