/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_CONTEXT_TYPES_H__
#define __I915_GEM_CONTEXT_TYPES_H__

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
#include <linux/types.h>

#include "gt/intel_context_types.h"

#include "i915_scheduler.h"
#include "i915_sw_fence.h"

struct pid;

struct drm_i915_private;
struct drm_i915_file_private;
struct i915_address_space;
struct intel_timeline;
struct intel_ring;

/**
 * struct i915_gem_engines - A set of engines
 */
struct i915_gem_engines {
	union {
		/** @link: Link in i915_gem_context::stale::engines */
		struct list_head link;

		/** @rcu: RCU to use when freeing */
		struct rcu_head rcu;
	};

	/** @fence: Fence used for delayed destruction of engines */
	struct i915_sw_fence fence;

	/** @ctx: i915_gem_context backpointer */
	struct i915_gem_context *ctx;

	/** @num_engines: Number of engines in this set */
	unsigned int num_engines;

	/** @engines: Array of engines */
	struct intel_context *engines[];
};

/**
 * struct i915_gem_engines_iter - Iterator for an i915_gem_engines set
 */
struct i915_gem_engines_iter {
	/** @idx: Index into i915_gem_engines::engines */
	unsigned int idx;

	/** @engines: Engine set being iterated */
	const struct i915_gem_engines *engines;
};

/**
 * enum i915_gem_engine_type - Describes the type of an i915_gem_proto_engine
 */
enum i915_gem_engine_type {
	/** @I915_GEM_ENGINE_TYPE_INVALID: An invalid engine */
	I915_GEM_ENGINE_TYPE_INVALID = 0,

	/** @I915_GEM_ENGINE_TYPE_PHYSICAL: A single physical engine */
	I915_GEM_ENGINE_TYPE_PHYSICAL,

	/** @I915_GEM_ENGINE_TYPE_BALANCED: A load-balanced engine set */
	I915_GEM_ENGINE_TYPE_BALANCED,

	/** @I915_GEM_ENGINE_TYPE_PARALLEL: A parallel engine set */
	I915_GEM_ENGINE_TYPE_PARALLEL,
};

/**
 * struct i915_gem_proto_engine - prototype engine
 *
 * This struct describes an engine that a context may contain.  Engines
 * have four types:
 *
 *  - I915_GEM_ENGINE_TYPE_INVALID: Invalid engines can be created but they
 *    show up as a NULL in i915_gem_engines::engines[i] and any attempt to
 *    use them by the user results in -EINVAL.  They are also useful during
 *    proto-context construction because the client may create invalid
 *    engines and then set them up later as virtual engines.
 *
 *  - I915_GEM_ENGINE_TYPE_PHYSICAL: A single physical engine, described by
 *    i915_gem_proto_engine::engine.
 *
 *  - I915_GEM_ENGINE_TYPE_BALANCED: A load-balanced engine set, described
 *    i915_gem_proto_engine::num_siblings and i915_gem_proto_engine::siblings.
 *
 *  - I915_GEM_ENGINE_TYPE_PARALLEL: A parallel submission engine set, described
 *    i915_gem_proto_engine::width, i915_gem_proto_engine::num_siblings, and
 *    i915_gem_proto_engine::siblings.
 */
struct i915_gem_proto_engine {
	/** @type: Type of this engine */
	enum i915_gem_engine_type type;

	/** @engine: Engine, for physical */
	struct intel_engine_cs *engine;

	/** @num_siblings: Number of balanced or parallel siblings */
	unsigned int num_siblings;

	/** @width: Width of each sibling */
	unsigned int width;

	/** @siblings: Balanced siblings or num_siblings * width for parallel */
	struct intel_engine_cs **siblings;

	/** @sseu: Client-set SSEU parameters */
	struct intel_sseu sseu;
};

/**
 * struct i915_gem_proto_context - prototype context
 *
 * The struct i915_gem_proto_context represents the creation parameters for
 * a struct i915_gem_context.  This is used to gather parameters provided
 * either through creation flags or via SET_CONTEXT_PARAM so that, when we
 * create the final i915_gem_context, those parameters can be immutable.
 *
 * The context uAPI allows for two methods of setting context parameters:
 * SET_CONTEXT_PARAM and CONTEXT_CREATE_EXT_SETPARAM.  The former is
 * allowed to be called at any time while the later happens as part of
 * GEM_CONTEXT_CREATE.  When these were initially added, Currently,
 * everything settable via one is settable via the other.  While some
 * params are fairly simple and setting them on a live context is harmless
 * such the context priority, others are far trickier such as the VM or the
 * set of engines.  To avoid some truly nasty race conditions, we don't
 * allow setting the VM or the set of engines on live contexts.
 *
 * The way we dealt with this without breaking older userspace that sets
 * the VM or engine set via SET_CONTEXT_PARAM is to delay the creation of
 * the actual context until after the client is done configuring it with
 * SET_CONTEXT_PARAM.  From the perspective of the client, it has the same
 * u32 context ID the whole time.  From the perspective of i915, however,
 * it's an i915_gem_proto_context right up until the point where we attempt
 * to do something which the proto-context can't handle at which point the
 * real context gets created.
 *
 * This is accomplished via a little xarray dance.  When GEM_CONTEXT_CREATE
 * is called, we create a proto-context, reserve a slot in context_xa but
 * leave it NULL, the proto-context in the corresponding slot in
 * proto_context_xa.  Then, whenever we go to look up a context, we first
 * check context_xa.  If it's there, we return the i915_gem_context and
 * we're done.  If it's not, we look in proto_context_xa and, if we find it
 * there, we create the actual context and kill the proto-context.
 *
 * At the time we made this change (April, 2021), we did a fairly complete
 * audit of existing userspace to ensure this wouldn't break anything:
 *
 *  - Mesa/i965 didn't use the engines or VM APIs at all
 *
 *  - Mesa/ANV used the engines API but via CONTEXT_CREATE_EXT_SETPARAM and
 *    didn't use the VM API.
 *
 *  - Mesa/iris didn't use the engines or VM APIs at all
 *
 *  - The open-source compute-runtime didn't yet use the engines API but
 *    did use the VM API via SET_CONTEXT_PARAM.  However, CONTEXT_SETPARAM
 *    was always the second ioctl on that context, immediately following
 *    GEM_CONTEXT_CREATE.
 *
 *  - The media driver sets engines and bonding/balancing via
 *    SET_CONTEXT_PARAM.  However, CONTEXT_SETPARAM to set the VM was
 *    always the second ioctl on that context, immediately following
 *    GEM_CONTEXT_CREATE and setting engines immediately followed that.
 *
 * In order for this dance to work properly, any modification to an
 * i915_gem_proto_context that is exposed to the client via
 * drm_i915_file_private::proto_context_xa must be guarded by
 * drm_i915_file_private::proto_context_lock.  The exception is when a
 * proto-context has not yet been exposed such as when handling
 * CONTEXT_CREATE_SET_PARAM during GEM_CONTEXT_CREATE.
 */
struct i915_gem_proto_context {
	/** @fpriv: Client which creates the context */
	struct drm_i915_file_private *fpriv;

	/** @vm: See &i915_gem_context.vm */
	struct i915_address_space *vm;

	/** @user_flags: See &i915_gem_context.user_flags */
	unsigned long user_flags;

	/** @sched: See &i915_gem_context.sched */
	struct i915_sched_attr sched;

	/** @num_user_engines: Number of user-specified engines or -1 */
	int num_user_engines;

	/** @user_engines: User-specified engines */
	struct i915_gem_proto_engine *user_engines;

	/** @legacy_rcs_sseu: Client-set SSEU parameters for the legacy RCS */
	struct intel_sseu legacy_rcs_sseu;

	/** @single_timeline: See See &i915_gem_context.syncobj */
	bool single_timeline;

	/** @uses_protected_content: See &i915_gem_context.uses_protected_content */
	bool uses_protected_content;

	/** @pxp_wakeref: See &i915_gem_context.pxp_wakeref */
	intel_wakeref_t pxp_wakeref;
};

/**
 * struct i915_gem_context - client state
 *
 * The struct i915_gem_context represents the combined view of the driver and
 * logical hardware state for a particular client.
 */
struct i915_gem_context {
	/** @i915: i915 device backpointer */
	struct drm_i915_private *i915;

	/** @file_priv: owning file descriptor */
	struct drm_i915_file_private *file_priv;

	/**
	 * @engines: User defined engines for this context
	 *
	 * Various uAPI offer the ability to lookup up an
	 * index from this array to select an engine operate on.
	 *
	 * Multiple logically distinct instances of the same engine
	 * may be defined in the array, as well as composite virtual
	 * engines.
	 *
	 * Execbuf uses the I915_EXEC_RING_MASK as an index into this
	 * array to select which HW context + engine to execute on. For
	 * the default array, the user_ring_map[] is used to translate
	 * the legacy uABI onto the appropriate index (e.g. both
	 * I915_EXEC_DEFAULT and I915_EXEC_RENDER select the same
	 * context, and I915_EXEC_BSD is weird). For a user defined
	 * array, execbuf uses I915_EXEC_RING_MASK as a plain index.
	 *
	 * User defined by I915_CONTEXT_PARAM_ENGINE (when the
	 * CONTEXT_USER_ENGINES flag is set).
	 */
	struct i915_gem_engines __rcu *engines;

	/** @engines_mutex: guards writes to engines */
	struct mutex engines_mutex;

	/**
	 * @syncobj: Shared timeline syncobj
	 *
	 * When the SHARED_TIMELINE flag is set on context creation, we
	 * emulate a single timeline across all engines using this syncobj.
	 * For every execbuffer2 call, this syncobj is used as both an in-
	 * and out-fence.  Unlike the real intel_timeline, this doesn't
	 * provide perfect atomic in-order guarantees if the client races
	 * with itself by calling execbuffer2 twice concurrently.  However,
	 * if userspace races with itself, that's not likely to yield well-
	 * defined results anyway so we choose to not care.
	 */
	struct drm_syncobj *syncobj;

	/**
	 * @vm: unique address space (GTT)
	 *
	 * In full-ppgtt mode, each context has its own address space ensuring
	 * complete separation of one client from all others.
	 *
	 * In other modes, this is a NULL pointer with the expectation that
	 * the caller uses the shared global GTT.
	 */
	struct i915_address_space *vm;

	/**
	 * @pid: process id of creator
	 *
	 * Note that who created the context may not be the principle user,
	 * as the context may be shared across a local socket. However,
	 * that should only affect the default context, all contexts created
	 * explicitly by the client are expected to be isolated.
	 */
	struct pid *pid;

	/** @link: place with &drm_i915_private.context_list */
	struct list_head link;

	/** @client: struct i915_drm_client */
	struct i915_drm_client *client;

	/** @client_link: for linking onto &i915_drm_client.ctx_list */
	struct list_head client_link;

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
	 * @release_work:
	 *
	 * Work item for deferred cleanup, since i915_gem_context_put() tends to
	 * be called from hardirq context.
	 *
	 * FIXME: The only real reason for this is &i915_gem_engines.fence, all
	 * other callers are from process context and need at most some mild
	 * shuffling to pull the i915_gem_context_put() call out of a spinlock.
	 */
	struct work_struct release_work;

	/**
	 * @rcu: rcu_head for deferred freeing.
	 */
	struct rcu_head rcu;

	/**
	 * @user_flags: small set of booleans controlled by the user
	 */
	unsigned long user_flags;
#define UCONTEXT_NO_ERROR_CAPTURE	1
#define UCONTEXT_BANNABLE		2
#define UCONTEXT_RECOVERABLE		3
#define UCONTEXT_PERSISTENCE		4
#define UCONTEXT_LOW_LATENCY		5

	/**
	 * @flags: small set of booleans
	 */
	unsigned long flags;
#define CONTEXT_CLOSED			0
#define CONTEXT_USER_ENGINES		1

	/**
	 * @uses_protected_content: context uses PXP-encrypted objects.
	 *
	 * This flag can only be set at ctx creation time and it's immutable for
	 * the lifetime of the context. See I915_CONTEXT_PARAM_PROTECTED_CONTENT
	 * in uapi/drm/i915_drm.h for more info on setting restrictions and
	 * expected behaviour of marked contexts.
	 */
	bool uses_protected_content;

	/**
	 * @pxp_wakeref: wakeref to keep the device awake when PXP is in use
	 *
	 * PXP sessions are invalidated when the device is suspended, which in
	 * turns invalidates all contexts and objects using it. To keep the
	 * flow simple, we keep the device awake when contexts using PXP objects
	 * are in use. It is expected that the userspace application only uses
	 * PXP when the display is on, so taking a wakeref here shouldn't worsen
	 * our power metrics.
	 */
	intel_wakeref_t pxp_wakeref;

	/** @mutex: guards everything that isn't engines or handles_vma */
	struct mutex mutex;

	/** @sched: scheduler parameters */
	struct i915_sched_attr sched;

	/** @guilty_count: How many times this context has caused a GPU hang. */
	atomic_t guilty_count;
	/**
	 * @active_count: How many times this context was active during a GPU
	 * hang, but did not cause it.
	 */
	atomic_t active_count;

	/**
	 * @hang_timestamp: The last time(s) this context caused a GPU hang
	 */
	unsigned long hang_timestamp[2];
#define CONTEXT_FAST_HANG_JIFFIES (120 * HZ) /* 3 hangs within 120s? Banned! */

	/** @remap_slice: Bitmask of cache lines that need remapping */
	u8 remap_slice;

	/**
	 * @handles_vma: rbtree to look up our context specific obj/vma for
	 * the user handle. (user handles are per fd, but the binding is
	 * per vm, which may be one per context or shared with the global GTT)
	 */
	struct radix_tree_root handles_vma;

	/** @lut_mutex: Locks handles_vma */
	struct mutex lut_mutex;

	/**
	 * @name: arbitrary name, used for user debug
	 *
	 * A name is constructed for the context from the creator's process
	 * name, pid and user handle in order to uniquely identify the
	 * context in messages.
	 */
	char name[TASK_COMM_LEN + 8];

	/** @stale: tracks stale engines to be destroyed */
	struct {
		/** @stale.lock: guards engines */
		spinlock_t lock;
		/** @stale.engines: list of stale engines */
		struct list_head engines;
	} stale;
};

#endif /* __I915_GEM_CONTEXT_TYPES_H__ */
