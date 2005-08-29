/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "drmP.h"
#include "via_drm.h"
#include "via_drv.h"
#include "via_ds.h"
#include "via_mm.h"

#define MAX_CONTEXT 100

typedef struct {
	int used;
	int context;
	set_t *sets[2];		/* 0 for frame buffer, 1 for AGP , 2 for System */
} via_context_t;

static via_context_t global_ppriv[MAX_CONTEXT];

static int via_agp_alloc(drm_via_mem_t * mem);
static int via_agp_free(drm_via_mem_t * mem);
static int via_fb_alloc(drm_via_mem_t * mem);
static int via_fb_free(drm_via_mem_t * mem);

static int add_alloc_set(int context, int type, unsigned int val)
{
	int i, retval = 0;

	for (i = 0; i < MAX_CONTEXT; i++) {
		if (global_ppriv[i].used && global_ppriv[i].context == context) {
			retval = via_setAdd(global_ppriv[i].sets[type], val);
			break;
		}
	}

	return retval;
}

static int del_alloc_set(int context, int type, unsigned int val)
{
	int i, retval = 0;

	for (i = 0; i < MAX_CONTEXT; i++)
		if (global_ppriv[i].used && global_ppriv[i].context == context) {
			retval = via_setDel(global_ppriv[i].sets[type], val);
			break;
		}

	return retval;
}

/* agp memory management */
static memHeap_t *AgpHeap = NULL;

int via_agp_init(DRM_IOCTL_ARGS)
{
	drm_via_agp_t agp;

	DRM_COPY_FROM_USER_IOCTL(agp, (drm_via_agp_t __user *) data,
				 sizeof(agp));

	AgpHeap = via_mmInit(agp.offset, agp.size);

	DRM_DEBUG("offset = %lu, size = %lu", (unsigned long)agp.offset, (unsigned long)agp.size);

	return 0;
}

/* fb memory management */
static memHeap_t *FBHeap = NULL;

int via_fb_init(DRM_IOCTL_ARGS)
{
	drm_via_fb_t fb;

	DRM_COPY_FROM_USER_IOCTL(fb, (drm_via_fb_t __user *) data, sizeof(fb));

	FBHeap = via_mmInit(fb.offset, fb.size);

	DRM_DEBUG("offset = %lu, size = %lu", (unsigned long)fb.offset, (unsigned long)fb.size);

	return 0;
}

int via_init_context(struct drm_device *dev, int context)
{
	int i;

	for (i = 0; i < MAX_CONTEXT; i++)
		if (global_ppriv[i].used &&
		    (global_ppriv[i].context == context))
			break;

	if (i >= MAX_CONTEXT) {
		for (i = 0; i < MAX_CONTEXT; i++) {
			if (!global_ppriv[i].used) {
				global_ppriv[i].context = context;
				global_ppriv[i].used = 1;
				global_ppriv[i].sets[0] = via_setInit();
				global_ppriv[i].sets[1] = via_setInit();
				DRM_DEBUG("init allocation set, socket=%d,"
					  " context = %d\n", i, context);
				break;
			}
		}

		if ((i >= MAX_CONTEXT) || (global_ppriv[i].sets[0] == NULL) ||
		    (global_ppriv[i].sets[1] == NULL)) {
			return 0;
		}
	}

	return 1;
}

int via_final_context(struct drm_device *dev, int context)
{	
        int i;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	for (i = 0; i < MAX_CONTEXT; i++)
		if (global_ppriv[i].used &&
		    (global_ppriv[i].context == context))
			break;

	if (i < MAX_CONTEXT) {
		set_t *set;
		ITEM_TYPE item;
		int retval;

		DRM_DEBUG("find socket %d, context = %d\n", i, context);

		/* Video Memory */
		set = global_ppriv[i].sets[0];
		retval = via_setFirst(set, &item);
		while (retval) {
			DRM_DEBUG("free video memory 0x%lx\n", item);
			via_mmFreeMem((PMemBlock) item);
			retval = via_setNext(set, &item);
		}
		via_setDestroy(set);

		/* AGP Memory */
		set = global_ppriv[i].sets[1];
		retval = via_setFirst(set, &item);
		while (retval) {
			DRM_DEBUG("free agp memory 0x%lx\n", item);
			via_mmFreeMem((PMemBlock) item);
			retval = via_setNext(set, &item);
		}
		via_setDestroy(set);
		global_ppriv[i].used = 0;
	}
	via_release_futex(dev_priv, context); 
	
			
#if defined(__linux__)
	/* Linux specific until context tracking code gets ported to BSD */
	/* Last context, perform cleanup */
	if (dev->ctx_count == 1 && dev->dev_private) {
	        DRM_DEBUG("Last Context\n");
		if (dev->irq)
			drm_irq_uninstall(dev);

		via_cleanup_futex(dev_priv);
		via_do_cleanup_map(dev);
	}
#endif

	return 1;
}

int via_mem_alloc(DRM_IOCTL_ARGS)
{
	drm_via_mem_t mem;

	DRM_COPY_FROM_USER_IOCTL(mem, (drm_via_mem_t __user *) data,
				 sizeof(mem));

	switch (mem.type) {
	case VIDEO:
		if (via_fb_alloc(&mem) < 0)
			return -EFAULT;
		DRM_COPY_TO_USER_IOCTL((drm_via_mem_t __user *) data, mem,
				       sizeof(mem));
		return 0;
	case AGP:
		if (via_agp_alloc(&mem) < 0)
			return -EFAULT;
		DRM_COPY_TO_USER_IOCTL((drm_via_mem_t __user *) data, mem,
				       sizeof(mem));
		return 0;
	}

	return -EFAULT;
}

static int via_fb_alloc(drm_via_mem_t * mem)
{
	drm_via_mm_t fb;
	PMemBlock block;
	int retval = 0;

	if (!FBHeap)
		return -1;

	fb.size = mem->size;
	fb.context = mem->context;

	block = via_mmAllocMem(FBHeap, fb.size, 5, 0);
	if (block) {
		fb.offset = block->ofs;
		fb.free = (unsigned long)block;
		if (!add_alloc_set(fb.context, VIDEO, fb.free)) {
			DRM_DEBUG("adding to allocation set fails\n");
			via_mmFreeMem((PMemBlock) fb.free);
			retval = -1;
		}
	} else {
		fb.offset = 0;
		fb.size = 0;
		fb.free = 0;
		retval = -1;
	}

	mem->offset = fb.offset;
	mem->index = fb.free;

	DRM_DEBUG("alloc fb, size = %d, offset = %d\n", fb.size,
		  (int)fb.offset);

	return retval;
}

static int via_agp_alloc(drm_via_mem_t * mem)
{
	drm_via_mm_t agp;
	PMemBlock block;
	int retval = 0;

	if (!AgpHeap)
		return -1;

	agp.size = mem->size;
	agp.context = mem->context;

	block = via_mmAllocMem(AgpHeap, agp.size, 5, 0);
	if (block) {
		agp.offset = block->ofs;
		agp.free = (unsigned long)block;
		if (!add_alloc_set(agp.context, AGP, agp.free)) {
			DRM_DEBUG("adding to allocation set fails\n");
			via_mmFreeMem((PMemBlock) agp.free);
			retval = -1;
		}
	} else {
		agp.offset = 0;
		agp.size = 0;
		agp.free = 0;
	}

	mem->offset = agp.offset;
	mem->index = agp.free;

	DRM_DEBUG("alloc agp, size = %d, offset = %d\n", agp.size,
		  (unsigned int)agp.offset);
	return retval;
}

int via_mem_free(DRM_IOCTL_ARGS)
{
	drm_via_mem_t mem;

	DRM_COPY_FROM_USER_IOCTL(mem, (drm_via_mem_t __user *) data,
				 sizeof(mem));

	switch (mem.type) {

	case VIDEO:
		if (via_fb_free(&mem) == 0)
			return 0;
		break;
	case AGP:
		if (via_agp_free(&mem) == 0)
			return 0;
		break;
	}

	return -EFAULT;
}

static int via_fb_free(drm_via_mem_t * mem)
{
	drm_via_mm_t fb;
	int retval = 0;

	if (!FBHeap) {
		return -1;
	}

	fb.free = mem->index;
	fb.context = mem->context;

	if (!fb.free) {
		return -1;

	}

	via_mmFreeMem((PMemBlock) fb.free);

	if (!del_alloc_set(fb.context, VIDEO, fb.free)) {
		retval = -1;
	}

	DRM_DEBUG("free fb, free = %ld\n", fb.free);

	return retval;
}

static int via_agp_free(drm_via_mem_t * mem)
{
	drm_via_mm_t agp;

	int retval = 0;

	agp.free = mem->index;
	agp.context = mem->context;

	if (!agp.free)
		return -1;

	via_mmFreeMem((PMemBlock) agp.free);

	if (!del_alloc_set(agp.context, AGP, agp.free)) {
		retval = -1;
	}

	DRM_DEBUG("free agp, free = %ld\n", agp.free);

	return retval;
}
