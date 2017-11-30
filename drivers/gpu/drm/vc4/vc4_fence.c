/*
 * Copyright © 2017 Broadcom
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
 */

#include "vc4_drv.h"

static const char *vc4_fence_get_driver_name(struct dma_fence *fence)
{
	return "vc4";
}

static const char *vc4_fence_get_timeline_name(struct dma_fence *fence)
{
	return "vc4-v3d";
}

static bool vc4_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static bool vc4_fence_signaled(struct dma_fence *fence)
{
	struct vc4_fence *f = to_vc4_fence(fence);
	struct vc4_dev *vc4 = to_vc4_dev(f->dev);

	return vc4->finished_seqno >= f->seqno;
}

const struct dma_fence_ops vc4_fence_ops = {
	.get_driver_name = vc4_fence_get_driver_name,
	.get_timeline_name = vc4_fence_get_timeline_name,
	.enable_signaling = vc4_fence_enable_signaling,
	.signaled = vc4_fence_signaled,
	.wait = dma_fence_default_wait,
	.release = dma_fence_free,
};
