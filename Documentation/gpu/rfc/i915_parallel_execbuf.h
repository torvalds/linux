/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#define I915_CONTEXT_ENGINES_EXT_PARALLEL_SUBMIT 2 /* see i915_context_engines_parallel_submit */

/**
 * struct drm_i915_context_engines_parallel_submit - Configure engine for
 * parallel submission.
 *
 * Setup a slot in the context engine map to allow multiple BBs to be submitted
 * in a single execbuf IOCTL. Those BBs will then be scheduled to run on the GPU
 * in parallel. Multiple hardware contexts are created internally in the i915
 * run these BBs. Once a slot is configured for N BBs only N BBs can be
 * submitted in each execbuf IOCTL and this is implicit behavior e.g. The user
 * doesn't tell the execbuf IOCTL there are N BBs, the execbuf IOCTL knows how
 * many BBs there are based on the slot's configuration. The N BBs are the last
 * N buffer objects or first N if I915_EXEC_BATCH_FIRST is set.
 *
 * The default placement behavior is to create implicit bonds between each
 * context if each context maps to more than 1 physical engine (e.g. context is
 * a virtual engine). Also we only allow contexts of same engine class and these
 * contexts must be in logically contiguous order. Examples of the placement
 * behavior described below. Lastly, the default is to not allow BBs to
 * preempted mid BB rather insert coordinated preemption on all hardware
 * contexts between each set of BBs. Flags may be added in the future to change
 * both of these default behaviors.
 *
 * Returns -EINVAL if hardware context placement configuration is invalid or if
 * the placement configuration isn't supported on the platform / submission
 * interface.
 * Returns -ENODEV if extension isn't supported on the platform / submission
 * interface.
 *
 * .. code-block:: none
 *
 *	Example 1 pseudo code:
 *	CS[X] = generic engine of same class, logical instance X
 *	INVALID = I915_ENGINE_CLASS_INVALID, I915_ENGINE_CLASS_INVALID_NONE
 *	set_engines(INVALID)
 *	set_parallel(engine_index=0, width=2, num_siblings=1,
 *		     engines=CS[0],CS[1])
 *
 *	Results in the following valid placement:
 *	CS[0], CS[1]
 *
 *	Example 2 pseudo code:
 *	CS[X] = generic engine of same class, logical instance X
 *	INVALID = I915_ENGINE_CLASS_INVALID, I915_ENGINE_CLASS_INVALID_NONE
 *	set_engines(INVALID)
 *	set_parallel(engine_index=0, width=2, num_siblings=2,
 *		     engines=CS[0],CS[2],CS[1],CS[3])
 *
 *	Results in the following valid placements:
 *	CS[0], CS[1]
 *	CS[2], CS[3]
 *
 *	This can also be thought of as 2 virtual engines described by 2-D array
 *	in the engines the field with bonds placed between each index of the
 *	virtual engines. e.g. CS[0] is bonded to CS[1], CS[2] is bonded to
 *	CS[3].
 *	VE[0] = CS[0], CS[2]
 *	VE[1] = CS[1], CS[3]
 *
 *	Example 3 pseudo code:
 *	CS[X] = generic engine of same class, logical instance X
 *	INVALID = I915_ENGINE_CLASS_INVALID, I915_ENGINE_CLASS_INVALID_NONE
 *	set_engines(INVALID)
 *	set_parallel(engine_index=0, width=2, num_siblings=2,
 *		     engines=CS[0],CS[1],CS[1],CS[3])
 *
 *	Results in the following valid and invalid placements:
 *	CS[0], CS[1]
 *	CS[1], CS[3] - Not logical contiguous, return -EINVAL
 */
struct drm_i915_context_engines_parallel_submit {
	/**
	 * @base: base user extension.
	 */
	struct i915_user_extension base;

	/**
	 * @engine_index: slot for parallel engine
	 */
	__u16 engine_index;

	/**
	 * @width: number of contexts per parallel engine
	 */
	__u16 width;

	/**
	 * @num_siblings: number of siblings per context
	 */
	__u16 num_siblings;

	/**
	 * @mbz16: reserved for future use; must be zero
	 */
	__u16 mbz16;

	/**
	 * @flags: all undefined flags must be zero, currently not defined flags
	 */
	__u64 flags;

	/**
	 * @mbz64: reserved for future use; must be zero
	 */
	__u64 mbz64[3];

	/**
	 * @engines: 2-d array of engine instances to configure parallel engine
	 *
	 * length = width (i) * num_siblings (j)
	 * index = j + i * num_siblings
	 */
	struct i915_engine_class_instance engines[0];

} __packed;

