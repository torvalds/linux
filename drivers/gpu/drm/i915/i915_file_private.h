/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_FILE_PRIVATE_H__
#define __I915_FILE_PRIVATE_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/xarray.h>

struct drm_i915_private;
struct drm_file;
struct i915_drm_client;

struct drm_i915_file_private {
	struct drm_i915_private *i915;

	union {
		struct drm_file *file;
		struct rcu_head rcu;
	};

	/** @proto_context_lock: Guards all struct i915_gem_proto_context
	 * operations
	 *
	 * This not only guards @proto_context_xa, but is always held
	 * whenever we manipulate any struct i915_gem_proto_context,
	 * including finalizing it on first actual use of the GEM context.
	 *
	 * See i915_gem_proto_context.
	 */
	struct mutex proto_context_lock;

	/** @proto_context_xa: xarray of struct i915_gem_proto_context
	 *
	 * Historically, the context uAPI allowed for two methods of
	 * setting context parameters: SET_CONTEXT_PARAM and
	 * CONTEXT_CREATE_EXT_SETPARAM.  The former is allowed to be called
	 * at any time while the later happens as part of
	 * GEM_CONTEXT_CREATE.  Everything settable via one was settable
	 * via the other.  While some params are fairly simple and setting
	 * them on a live context is harmless such as the context priority,
	 * others are far trickier such as the VM or the set of engines.
	 * In order to swap out the VM, for instance, we have to delay
	 * until all current in-flight work is complete, swap in the new
	 * VM, and then continue.  This leads to a plethora of potential
	 * race conditions we'd really rather avoid.
	 *
	 * We have since disallowed setting these more complex parameters
	 * on active contexts.  This works by delaying the creation of the
	 * actual context until after the client is done configuring it
	 * with SET_CONTEXT_PARAM.  From the perspective of the client, it
	 * has the same u32 context ID the whole time.  From the
	 * perspective of i915, however, it's a struct i915_gem_proto_context
	 * right up until the point where we attempt to do something which
	 * the proto-context can't handle.  Then the struct i915_gem_context
	 * gets created.
	 *
	 * This is accomplished via a little xarray dance.  When
	 * GEM_CONTEXT_CREATE is called, we create a struct
	 * i915_gem_proto_context, reserve a slot in @context_xa but leave
	 * it NULL, and place the proto-context in the corresponding slot
	 * in @proto_context_xa.  Then, in i915_gem_context_lookup(), we
	 * first check @context_xa.  If it's there, we return the struct
	 * i915_gem_context and we're done.  If it's not, we look in
	 * @proto_context_xa and, if we find it there, we create the actual
	 * context and kill the proto-context.
	 *
	 * In order for this dance to work properly, everything which ever
	 * touches a struct i915_gem_proto_context is guarded by
	 * @proto_context_lock, including context creation.  Yes, this
	 * means context creation now takes a giant global lock but it
	 * can't really be helped and that should never be on any driver's
	 * fast-path anyway.
	 */
	struct xarray proto_context_xa;

	/** @context_xa: xarray of fully created i915_gem_context
	 *
	 * Write access to this xarray is guarded by @proto_context_lock.
	 * Otherwise, writers may race with finalize_create_context_locked().
	 *
	 * See @proto_context_xa.
	 */
	struct xarray context_xa;
	struct xarray vm_xa;

	unsigned int bsd_engine;

/*
 * Every context ban increments per client ban score. Also
 * hangs in short succession increments ban score. If ban threshold
 * is reached, client is considered banned and submitting more work
 * will fail. This is a stop gap measure to limit the badly behaving
 * clients access to gpu. Note that unbannable contexts never increment
 * the client ban score.
 */
#define I915_CLIENT_SCORE_HANG_FAST	1
#define   I915_CLIENT_FAST_HANG_JIFFIES (60 * HZ)
#define I915_CLIENT_SCORE_CONTEXT_BAN   3
#define I915_CLIENT_SCORE_BANNED	9
	/** ban_score: Accumulated score of all ctx bans and fast hangs. */
	atomic_t ban_score;
	unsigned long hang_timestamp;

	struct i915_drm_client *client;
};

#endif /* __I915_FILE_PRIVATE_H__ */
