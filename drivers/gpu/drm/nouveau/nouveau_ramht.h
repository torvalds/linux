/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#ifndef __NOUVEAU_RAMHT_H__
#define __NOUVEAU_RAMHT_H__

struct nouveau_ramht_entry {
	struct list_head head;
	struct nouveau_channel *channel;
	struct nouveau_gpuobj *gpuobj;
	u32 handle;
};

struct nouveau_ramht {
	struct drm_device *dev;
	struct kref refcount;
	spinlock_t lock;
	struct nouveau_gpuobj *gpuobj;
	struct list_head entries;
	int bits;
};

extern int  nouveau_ramht_new(struct drm_device *, struct nouveau_gpuobj *,
			      struct nouveau_ramht **);
extern void nouveau_ramht_ref(struct nouveau_ramht *, struct nouveau_ramht **,
			      struct nouveau_channel *unref_channel);

extern int  nouveau_ramht_insert(struct nouveau_channel *, u32 handle,
				 struct nouveau_gpuobj *);
extern void nouveau_ramht_remove(struct nouveau_channel *, u32 handle);
extern struct nouveau_gpuobj *
nouveau_ramht_find(struct nouveau_channel *chan, u32 handle);

#endif
