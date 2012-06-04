/*
 * Copyright © 2011-2012 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

/*
 * This file implements HW context support. On gen5+ a HW context consists of an
 * opaque GPU object which is referenced at times of context saves and restores.
 * With RC6 enabled, the context is also referenced as the GPU enters and exists
 * from RC6 (GPU has it's own internal power context, except on gen5). Though
 * something like a context does exist for the media ring, the code only
 * supports contexts for the render ring.
 *
 * In software, there is a distinction between contexts created by the user,
 * and the default HW context. The default HW context is used by GPU clients
 * that do not request setup of their own hardware context. The default
 * context's state is never restored to help prevent programming errors. This
 * would happen if a client ran and piggy-backed off another clients GPU state.
 * The default context only exists to give the GPU some offset to load as the
 * current to invoke a save of the context we actually care about. In fact, the
 * code could likely be constructed, albeit in a more complicated fashion, to
 * never use the default context, though that limits the driver's ability to
 * swap out, and/or destroy other contexts.
 *
 * All other contexts are created as a request by the GPU client. These contexts
 * store GPU state, and thus allow GPU clients to not re-emit state (and
 * potentially query certain state) at any time. The kernel driver makes
 * certain that the appropriate commands are inserted.
 *
 * The context life cycle is semi-complicated in that context BOs may live
 * longer than the context itself because of the way the hardware, and object
 * tracking works. Below is a very crude representation of the state machine
 * describing the context life.
 *                                         refcount     pincount     active
 * S0: initial state                          0            0           0
 * S1: context created                        1            0           0
 * S2: context is currently running           2            1           X
 * S3: GPU referenced, but not current        2            0           1
 * S4: context is current, but destroyed      1            1           0
 * S5: like S3, but destroyed                 1            0           1
 *
 * The most common (but not all) transitions:
 * S0->S1: client creates a context
 * S1->S2: client submits execbuf with context
 * S2->S3: other clients submits execbuf with context
 * S3->S1: context object was retired
 * S3->S2: clients submits another execbuf
 * S2->S4: context destroy called with current context
 * S3->S5->S0: destroy path
 * S4->S5->S0: destroy path on current context
 *
 * There are two confusing terms used above:
 *  The "current context" means the context which is currently running on the
 *  GPU. The GPU has loaded it's state already and has stored away the gtt
 *  offset of the BO. The GPU is not actively referencing the data at this
 *  offset, but it will on the next context switch. The only way to avoid this
 *  is to do a GPU reset.
 *
 *  An "active context' is one which was previously the "current context" and is
 *  on the active list waiting for the next context switch to occur. Until this
 *  happens, the object must remain at the same gtt offset. It is therefore
 *  possible to destroy a context, but it is still active.
 *
 */

#include "drmP.h"
#include "i915_drm.h"
#include "i915_drv.h"

static int get_context_size(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;
	u32 reg;

	switch (INTEL_INFO(dev)->gen) {
	case 6:
		reg = I915_READ(CXT_SIZE);
		ret = GEN6_CXT_TOTAL_SIZE(reg) * 64;
		break;
	case 7:
		reg = I915_READ(GEN7_CTX_SIZE);
		ret = GEN7_CTX_TOTAL_SIZE(reg) * 64;
		break;
	default:
		BUG();
	}

	return ret;
}

/**
 * The default context needs to exist per ring that uses contexts. It stores the
 * context state of the GPU for applications that don't utilize HW contexts, as
 * well as an idle case.
 */
static int create_default_context(struct drm_i915_private *dev_priv)
{
	return 0;
}

void i915_gem_context_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t ctx_size;

	if (!HAS_HW_CONTEXTS(dev))
		return;

	/* If called from reset, or thaw... we've been here already */
	if (dev_priv->hw_contexts_disabled)
		return;

	ctx_size = get_context_size(dev);
	dev_priv->hw_context_size = get_context_size(dev);
	dev_priv->hw_context_size = round_up(dev_priv->hw_context_size, 4096);

	if (ctx_size <= 0 || ctx_size > (1<<20)) {
		dev_priv->hw_contexts_disabled = true;
		return;
	}

	if (create_default_context(dev_priv)) {
		dev_priv->hw_contexts_disabled = true;
		return;
	}

	DRM_DEBUG_DRIVER("HW context support initialized\n");
}

void i915_gem_context_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->hw_contexts_disabled)
		return;
}

void i915_gem_context_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->hw_contexts_disabled)
		return;
}

void i915_gem_context_close(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->hw_contexts_disabled)
		return;
}
