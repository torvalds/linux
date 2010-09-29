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

#if WATCH_INACTIVE
void
i915_verify_inactive(struct drm_device *dev, char *file, int line)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;

	list_for_each_entry(obj_priv, &dev_priv->mm.inactive_list, list) {
		obj = &obj_priv->base;
		if (obj_priv->pin_count || obj_priv->active ||
		    (obj->write_domain & ~(I915_GEM_DOMAIN_CPU |
					   I915_GEM_DOMAIN_GTT)))
			DRM_ERROR("inactive %p (p %d a %d w %x)  %s:%d\n",
				  obj,
				  obj_priv->pin_count, obj_priv->active,
				  obj->write_domain, file, line);
	}
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
i915_gem_dump_object(struct drm_gem_object *obj, int len,
		     const char *where, uint32_t mark)
{
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int page;

	DRM_INFO("%s: object at offset %08x\n", where, obj_priv->gtt_offset);
	for (page = 0; page < (len + PAGE_SIZE-1) / PAGE_SIZE; page++) {
		int page_len, chunk, chunk_len;

		page_len = len - page * PAGE_SIZE;
		if (page_len > PAGE_SIZE)
			page_len = PAGE_SIZE;

		for (chunk = 0; chunk < page_len; chunk += 128) {
			chunk_len = page_len - chunk;
			if (chunk_len > 128)
				chunk_len = 128;
			i915_gem_dump_page(obj_priv->pages[page],
					   chunk, chunk + chunk_len,
					   obj_priv->gtt_offset +
					   page * PAGE_SIZE,
					   mark);
		}
	}
}
#endif

#if WATCH_COHERENCY
void
i915_gem_object_check_coherency(struct drm_gem_object *obj, int handle)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);
	int page;
	uint32_t *gtt_mapping;
	uint32_t *backing_map = NULL;
	int bad_count = 0;

	DRM_INFO("%s: checking coherency of object %p@0x%08x (%d, %zdkb):\n",
		 __func__, obj, obj_priv->gtt_offset, handle,
		 obj->size / 1024);

	gtt_mapping = ioremap(dev->agp->base + obj_priv->gtt_offset,
			      obj->size);
	if (gtt_mapping == NULL) {
		DRM_ERROR("failed to map GTT space\n");
		return;
	}

	for (page = 0; page < obj->size / PAGE_SIZE; page++) {
		int i;

		backing_map = kmap_atomic(obj_priv->pages[page], KM_USER0);

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
					 (int)(obj_priv->gtt_offset +
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
