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

#include "drmP.h"

#include "nouveau_drv.h"

void
nvc0_fifo_disable(struct drm_device *dev)
{
}

void
nvc0_fifo_enable(struct drm_device *dev)
{
}

bool
nvc0_fifo_reassign(struct drm_device *dev, bool enable)
{
	return false;
}

bool
nvc0_fifo_cache_flush(struct drm_device *dev)
{
	return true;
}

bool
nvc0_fifo_cache_pull(struct drm_device *dev, bool enable)
{
	return false;
}

int
nvc0_fifo_channel_id(struct drm_device *dev)
{
	return 127;
}

int
nvc0_fifo_create_context(struct nouveau_channel *chan)
{
	return 0;
}

void
nvc0_fifo_destroy_context(struct nouveau_channel *chan)
{
}

int
nvc0_fifo_load_context(struct nouveau_channel *chan)
{
	return 0;
}

int
nvc0_fifo_unload_context(struct drm_device *dev)
{
	return 0;
}

void
nvc0_fifo_takedown(struct drm_device *dev)
{
}

int
nvc0_fifo_init(struct drm_device *dev)
{
	return 0;
}

