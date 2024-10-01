/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/**
 * DOC: I915_PARAM_VM_BIND_VERSION
 *
 * VM_BIND feature version supported.
 * See typedef drm_i915_getparam_t param.
 *
 * Specifies the VM_BIND feature version supported.
 * The following versions of VM_BIND have been defined:
 *
 * 0: No VM_BIND support.
 *
 * 1: In VM_UNBIND calls, the UMD must specify the exact mappings created
 *    previously with VM_BIND, the ioctl will not support unbinding multiple
 *    mappings or splitting them. Similarly, VM_BIND calls will not replace
 *    any existing mappings.
 *
 * 2: The restrictions on unbinding partial or multiple mappings is
 *    lifted, Similarly, binding will replace any mappings in the given range.
 *
 * See struct drm_i915_gem_vm_bind and struct drm_i915_gem_vm_unbind.
 */
#define I915_PARAM_VM_BIND_VERSION	57

/**
 * DOC: I915_VM_CREATE_FLAGS_USE_VM_BIND
 *
 * Flag to opt-in for VM_BIND mode of binding during VM creation.
 * See struct drm_i915_gem_vm_control flags.
 *
 * The older execbuf2 ioctl will not support VM_BIND mode of operation.
 * For VM_BIND mode, we have new execbuf3 ioctl which will not accept any
 * execlist (See struct drm_i915_gem_execbuffer3 for more details).
 */
#define I915_VM_CREATE_FLAGS_USE_VM_BIND	(1 << 0)

/* VM_BIND related ioctls */
#define DRM_I915_GEM_VM_BIND		0x3d
#define DRM_I915_GEM_VM_UNBIND		0x3e
#define DRM_I915_GEM_EXECBUFFER3	0x3f

#define DRM_IOCTL_I915_GEM_VM_BIND		DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_VM_BIND, struct drm_i915_gem_vm_bind)
#define DRM_IOCTL_I915_GEM_VM_UNBIND		DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_VM_UNBIND, struct drm_i915_gem_vm_bind)
#define DRM_IOCTL_I915_GEM_EXECBUFFER3		DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_EXECBUFFER3, struct drm_i915_gem_execbuffer3)

/**
 * struct drm_i915_gem_timeline_fence - An input or output timeline fence.
 *
 * The operation will wait for input fence to signal.
 *
 * The returned output fence will be signaled after the completion of the
 * operation.
 */
struct drm_i915_gem_timeline_fence {
	/** @handle: User's handle for a drm_syncobj to wait on or signal. */
	__u32 handle;

	/**
	 * @flags: Supported flags are:
	 *
	 * I915_TIMELINE_FENCE_WAIT:
	 * Wait for the input fence before the operation.
	 *
	 * I915_TIMELINE_FENCE_SIGNAL:
	 * Return operation completion fence as output.
	 */
	__u32 flags;
#define I915_TIMELINE_FENCE_WAIT            (1 << 0)
#define I915_TIMELINE_FENCE_SIGNAL          (1 << 1)
#define __I915_TIMELINE_FENCE_UNKNOWN_FLAGS (-(I915_TIMELINE_FENCE_SIGNAL << 1))

	/**
	 * @value: A point in the timeline.
	 * Value must be 0 for a binary drm_syncobj. A Value of 0 for a
	 * timeline drm_syncobj is invalid as it turns a drm_syncobj into a
	 * binary one.
	 */
	__u64 value;
};

/**
 * struct drm_i915_gem_vm_bind - VA to object mapping to bind.
 *
 * This structure is passed to VM_BIND ioctl and specifies the mapping of GPU
 * virtual address (VA) range to the section of an object that should be bound
 * in the device page table of the specified address space (VM).
 * The VA range specified must be unique (ie., not currently bound) and can
 * be mapped to whole object or a section of the object (partial binding).
 * Multiple VA mappings can be created to the same section of the object
 * (aliasing).
 *
 * The @start, @offset and @length must be 4K page aligned. However the DG2 has
 * 64K page size for device local memory and has compact page table. On that
 * platform, for binding device local-memory objects, the @start, @offset and
 * @length must be 64K aligned. Also, UMDs should not mix the local memory 64K
 * page and the system memory 4K page bindings in the same 2M range.
 *
 * Error code -EINVAL will be returned if @start, @offset and @length are not
 * properly aligned. In version 1 (See I915_PARAM_VM_BIND_VERSION), error code
 * -ENOSPC will be returned if the VA range specified can't be reserved.
 *
 * VM_BIND/UNBIND ioctl calls executed on different CPU threads concurrently
 * are not ordered. Furthermore, parts of the VM_BIND operation can be done
 * asynchronously, if valid @fence is specified.
 */
struct drm_i915_gem_vm_bind {
	/** @vm_id: VM (address space) id to bind */
	__u32 vm_id;

	/** @handle: Object handle */
	__u32 handle;

	/** @start: Virtual Address start to bind */
	__u64 start;

	/** @offset: Offset in object to bind */
	__u64 offset;

	/** @length: Length of mapping to bind */
	__u64 length;

	/**
	 * @flags: Supported flags are:
	 *
	 * I915_GEM_VM_BIND_CAPTURE:
	 * Capture this mapping in the dump upon GPU error.
	 *
	 * Note that @fence carries its own flags.
	 */
	__u64 flags;
#define I915_GEM_VM_BIND_CAPTURE	(1 << 0)

	/**
	 * @fence: Timeline fence for bind completion signaling.
	 *
	 * Timeline fence is of format struct drm_i915_gem_timeline_fence.
	 *
	 * It is an out fence, hence using I915_TIMELINE_FENCE_WAIT flag
	 * is invalid, and an error will be returned.
	 *
	 * If I915_TIMELINE_FENCE_SIGNAL flag is not set, then out fence
	 * is not requested and binding is completed synchronously.
	 */
	struct drm_i915_gem_timeline_fence fence;

	/**
	 * @extensions: Zero-terminated chain of extensions.
	 *
	 * For future extensions. See struct i915_user_extension.
	 */
	__u64 extensions;
};

/**
 * struct drm_i915_gem_vm_unbind - VA to object mapping to unbind.
 *
 * This structure is passed to VM_UNBIND ioctl and specifies the GPU virtual
 * address (VA) range that should be unbound from the device page table of the
 * specified address space (VM). VM_UNBIND will force unbind the specified
 * range from device page table without waiting for any GPU job to complete.
 * It is UMDs responsibility to ensure the mapping is no longer in use before
 * calling VM_UNBIND.
 *
 * If the specified mapping is not found, the ioctl will simply return without
 * any error.
 *
 * VM_BIND/UNBIND ioctl calls executed on different CPU threads concurrently
 * are not ordered. Furthermore, parts of the VM_UNBIND operation can be done
 * asynchronously, if valid @fence is specified.
 */
struct drm_i915_gem_vm_unbind {
	/** @vm_id: VM (address space) id to bind */
	__u32 vm_id;

	/** @rsvd: Reserved, MBZ */
	__u32 rsvd;

	/** @start: Virtual Address start to unbind */
	__u64 start;

	/** @length: Length of mapping to unbind */
	__u64 length;

	/**
	 * @flags: Currently reserved, MBZ.
	 *
	 * Note that @fence carries its own flags.
	 */
	__u64 flags;

	/**
	 * @fence: Timeline fence for unbind completion signaling.
	 *
	 * Timeline fence is of format struct drm_i915_gem_timeline_fence.
	 *
	 * It is an out fence, hence using I915_TIMELINE_FENCE_WAIT flag
	 * is invalid, and an error will be returned.
	 *
	 * If I915_TIMELINE_FENCE_SIGNAL flag is not set, then out fence
	 * is not requested and unbinding is completed synchronously.
	 */
	struct drm_i915_gem_timeline_fence fence;

	/**
	 * @extensions: Zero-terminated chain of extensions.
	 *
	 * For future extensions. See struct i915_user_extension.
	 */
	__u64 extensions;
};

/**
 * struct drm_i915_gem_execbuffer3 - Structure for DRM_I915_GEM_EXECBUFFER3
 * ioctl.
 *
 * DRM_I915_GEM_EXECBUFFER3 ioctl only works in VM_BIND mode and VM_BIND mode
 * only works with this ioctl for submission.
 * See I915_VM_CREATE_FLAGS_USE_VM_BIND.
 */
struct drm_i915_gem_execbuffer3 {
	/**
	 * @ctx_id: Context id
	 *
	 * Only contexts with user engine map are allowed.
	 */
	__u32 ctx_id;

	/**
	 * @engine_idx: Engine index
	 *
	 * An index in the user engine map of the context specified by @ctx_id.
	 */
	__u32 engine_idx;

	/**
	 * @batch_address: Batch gpu virtual address/es.
	 *
	 * For normal submission, it is the gpu virtual address of the batch
	 * buffer. For parallel submission, it is a pointer to an array of
	 * batch buffer gpu virtual addresses with array size equal to the
	 * number of (parallel) engines involved in that submission (See
	 * struct i915_context_engines_parallel_submit).
	 */
	__u64 batch_address;

	/** @flags: Currently reserved, MBZ */
	__u64 flags;

	/** @rsvd1: Reserved, MBZ */
	__u32 rsvd1;

	/** @fence_count: Number of fences in @timeline_fences array. */
	__u32 fence_count;

	/**
	 * @timeline_fences: Pointer to an array of timeline fences.
	 *
	 * Timeline fences are of format struct drm_i915_gem_timeline_fence.
	 */
	__u64 timeline_fences;

	/** @rsvd2: Reserved, MBZ */
	__u64 rsvd2;

	/**
	 * @extensions: Zero-terminated chain of extensions.
	 *
	 * For future extensions. See struct i915_user_extension.
	 */
	__u64 extensions;
};

/**
 * struct drm_i915_gem_create_ext_vm_private - Extension to make the object
 * private to the specified VM.
 *
 * See struct drm_i915_gem_create_ext.
 */
struct drm_i915_gem_create_ext_vm_private {
#define I915_GEM_CREATE_EXT_VM_PRIVATE		2
	/** @base: Extension link. See struct i915_user_extension. */
	struct i915_user_extension base;

	/** @vm_id: Id of the VM to which the object is private */
	__u32 vm_id;
};
