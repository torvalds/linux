/*
 * Copyright © 2016 Intel Corporation
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
 */

#ifndef __I915_GEM_CONTEXT_H__
#define __I915_GEM_CONTEXT_H__

#include <linux/bitops.h>
#include <linux/list.h>

struct pid;

struct drm_device;
struct drm_file;

struct drm_i915_private;
struct drm_i915_file_private;
struct i915_hw_ppgtt;
struct i915_vma;
struct intel_ring;

#define DEFAULT_CONTEXT_HANDLE 0

/**
 * struct i915_gem_context - client state
 *
 * The struct i915_gem_context represents the combined view of the driver and
 * logical hardware state for a particular client.
 */
struct i915_gem_context {
	/** i915: i915 device backpointer */
	struct drm_i915_private *i915;

	/** file_priv: owning file descriptor */
	struct drm_i915_file_private *file_priv;

	/**
	 * @ppgtt: unique address space (GTT)
	 *
	 * In full-ppgtt mode, each context has its own address space ensuring
	 * complete seperation of one client from all others.
	 *
	 * In other modes, this is a NULL pointer with the expectation that
	 * the caller uses the shared global GTT.
	 */
	struct i915_hw_ppgtt *ppgtt;

	/**
	 * @pid: process id of creator
	 *
	 * Note that who created the context may not be the principle user,
	 * as the context may be shared across a local socket. However,
	 * that should only affect the default context, all contexts created
	 * explicitly by the client are expected to be isolated.
	 */
	struct pid *pid;

	/**
	 * @name: arbitrary name
	 *
	 * A name is constructed for the context from the creator's process
	 * name, pid and user handle in order to uniquely identify the
	 * context in messages.
	 */
	const char *name;

	/** link: place with &drm_i915_private.context_list */
	struct list_head link;

	/**
	 * @ref: reference count
	 *
	 * A reference to a context is held by both the client who created it
	 * and on each request submitted to the hardware using the request
	 * (to ensure the hardware has access to the state until it has
	 * finished all pending writes). See i915_gem_context_get() and
	 * i915_gem_context_put() for access.
	 */
	struct kref ref;

	/**
	 * @flags: small set of booleans
	 */
	unsigned long flags;
#define CONTEXT_NO_ZEROMAP		BIT(0)
#define CONTEXT_NO_ERROR_CAPTURE	1
#define CONTEXT_CLOSED			2
#define CONTEXT_BANNABLE		3
#define CONTEXT_BANNED			4
#define CONTEXT_FORCE_SINGLE_SUBMISSION	5

	/**
	 * @hw_id: - unique identifier for the context
	 *
	 * The hardware needs to uniquely identify the context for a few
	 * functions like fault reporting, PASID, scheduling. The
	 * &drm_i915_private.context_hw_ida is used to assign a unqiue
	 * id for the lifetime of the context.
	 */
	unsigned int hw_id;

	/**
	 * @user_handle: userspace identifier
	 *
	 * A unique per-file identifier is generated from
	 * &drm_i915_file_private.contexts.
	 */
	u32 user_handle;

	/**
	 * @priority: execution and service priority
	 *
	 * All clients are equal, but some are more equal than others!
	 *
	 * Requests from a context with a greater (more positive) value of
	 * @priority will be executed before those with a lower @priority
	 * value, forming a simple QoS.
	 *
	 * The &drm_i915_private.kernel_context is assigned the lowest priority.
	 */
	int priority;

	/** ggtt_alignment: alignment restriction for context objects */
	u32 ggtt_alignment;
	/** ggtt_offset_bias: placement restriction for context objects */
	u32 ggtt_offset_bias;

	/** engine: per-engine logical HW state */
	struct intel_context {
		struct i915_vma *state;
		struct intel_ring *ring;
		u32 *lrc_reg_state;
		u64 lrc_desc;
		int pin_count;
		bool initialised;
	} engine[I915_NUM_ENGINES];

	/** ring_size: size for allocating the per-engine ring buffer */
	u32 ring_size;
	/** desc_template: invariant fields for the HW context descriptor */
	u32 desc_template;

	/** status_notifier: list of callbacks for context-switch changes */
	struct atomic_notifier_head status_notifier;

	/** guilty_count: How many times this context has caused a GPU hang. */
	unsigned int guilty_count;
	/**
	 * @active_count: How many times this context was active during a GPU
	 * hang, but did not cause it.
	 */
	unsigned int active_count;

#define CONTEXT_SCORE_GUILTY		10
#define CONTEXT_SCORE_BAN_THRESHOLD	40
	/** ban_score: Accumulated score of all hangs caused by this context. */
	int ban_score;

	/** remap_slice: Bitmask of cache lines that need remapping */
	u8 remap_slice;
};

static inline bool i915_gem_context_is_closed(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_CLOSED, &ctx->flags);
}

static inline void i915_gem_context_set_closed(struct i915_gem_context *ctx)
{
	GEM_BUG_ON(i915_gem_context_is_closed(ctx));
	__set_bit(CONTEXT_CLOSED, &ctx->flags);
}

static inline bool i915_gem_context_no_error_capture(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_NO_ERROR_CAPTURE, &ctx->flags);
}

static inline void i915_gem_context_set_no_error_capture(struct i915_gem_context *ctx)
{
	__set_bit(CONTEXT_NO_ERROR_CAPTURE, &ctx->flags);
}

static inline void i915_gem_context_clear_no_error_capture(struct i915_gem_context *ctx)
{
	__clear_bit(CONTEXT_NO_ERROR_CAPTURE, &ctx->flags);
}

static inline bool i915_gem_context_is_bannable(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_BANNABLE, &ctx->flags);
}

static inline void i915_gem_context_set_bannable(struct i915_gem_context *ctx)
{
	__set_bit(CONTEXT_BANNABLE, &ctx->flags);
}

static inline void i915_gem_context_clear_bannable(struct i915_gem_context *ctx)
{
	__clear_bit(CONTEXT_BANNABLE, &ctx->flags);
}

static inline bool i915_gem_context_is_banned(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_BANNED, &ctx->flags);
}

static inline void i915_gem_context_set_banned(struct i915_gem_context *ctx)
{
	__set_bit(CONTEXT_BANNED, &ctx->flags);
}

static inline bool i915_gem_context_force_single_submission(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_FORCE_SINGLE_SUBMISSION, &ctx->flags);
}

static inline void i915_gem_context_set_force_single_submission(struct i915_gem_context *ctx)
{
	__set_bit(CONTEXT_FORCE_SINGLE_SUBMISSION, &ctx->flags);
}

static inline bool i915_gem_context_is_default(const struct i915_gem_context *c)
{
	return c->user_handle == DEFAULT_CONTEXT_HANDLE;
}

static inline bool i915_gem_context_is_kernel(struct i915_gem_context *ctx)
{
	return !ctx->file_priv;
}

/* i915_gem_context.c */
int __must_check i915_gem_context_init(struct drm_i915_private *dev_priv);
void i915_gem_context_lost(struct drm_i915_private *dev_priv);
void i915_gem_context_fini(struct drm_i915_private *dev_priv);
int i915_gem_context_open(struct drm_device *dev, struct drm_file *file);
void i915_gem_context_close(struct drm_device *dev, struct drm_file *file);
int i915_switch_context(struct drm_i915_gem_request *req);
int i915_gem_switch_to_kernel_context(struct drm_i915_private *dev_priv);
void i915_gem_context_free(struct kref *ctx_ref);
struct i915_gem_context *
i915_gem_context_create_gvt(struct drm_device *dev);

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file);
int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file);
int i915_gem_context_getparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int i915_gem_context_setparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int i915_gem_context_reset_stats_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file);

#endif /* !__I915_GEM_CONTEXT_H__ */
