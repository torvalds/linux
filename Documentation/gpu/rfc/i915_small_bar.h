/**
 * struct __drm_i915_memory_region_info - Describes one region as known to the
 * driver.
 *
 * Note this is using both struct drm_i915_query_item and struct drm_i915_query.
 * For this new query we are adding the new query id DRM_I915_QUERY_MEMORY_REGIONS
 * at &drm_i915_query_item.query_id.
 */
struct __drm_i915_memory_region_info {
	/** @region: The class:instance pair encoding */
	struct drm_i915_gem_memory_class_instance region;

	/** @rsvd0: MBZ */
	__u32 rsvd0;

	/**
	 * @probed_size: Memory probed by the driver
	 *
	 * Note that it should not be possible to ever encounter a zero value
	 * here, also note that no current region type will ever return -1 here.
	 * Although for future region types, this might be a possibility. The
	 * same applies to the other size fields.
	 */
	__u64 probed_size;

	/**
	 * @unallocated_size: Estimate of memory remaining
	 *
	 * Requires CAP_PERFMON or CAP_SYS_ADMIN to get reliable accounting.
	 * Without this (or if this is an older kernel) the value here will
	 * always equal the @probed_size. Note this is only currently tracked
	 * for I915_MEMORY_CLASS_DEVICE regions (for other types the value here
	 * will always equal the @probed_size).
	 */
	__u64 unallocated_size;

	union {
		/** @rsvd1: MBZ */
		__u64 rsvd1[8];
		struct {
			/**
			 * @probed_cpu_visible_size: Memory probed by the driver
			 * that is CPU accessible.
			 *
			 * This will be always be <= @probed_size, and the
			 * remainder (if there is any) will not be CPU
			 * accessible.
			 *
			 * On systems without small BAR, the @probed_size will
			 * always equal the @probed_cpu_visible_size, since all
			 * of it will be CPU accessible.
			 *
			 * Note this is only tracked for
			 * I915_MEMORY_CLASS_DEVICE regions (for other types the
			 * value here will always equal the @probed_size).
			 *
			 * Note that if the value returned here is zero, then
			 * this must be an old kernel which lacks the relevant
			 * small-bar uAPI support (including
			 * I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS), but on
			 * such systems we should never actually end up with a
			 * small BAR configuration, assuming we are able to load
			 * the kernel module. Hence it should be safe to treat
			 * this the same as when @probed_cpu_visible_size ==
			 * @probed_size.
			 */
			__u64 probed_cpu_visible_size;

			/**
			 * @unallocated_cpu_visible_size: Estimate of CPU
			 * visible memory remaining
			 *
			 * Note this is only tracked for
			 * I915_MEMORY_CLASS_DEVICE regions (for other types the
			 * value here will always equal the
			 * @probed_cpu_visible_size).
			 *
			 * Requires CAP_PERFMON or CAP_SYS_ADMIN to get reliable
			 * accounting.  Without this the value here will always
			 * equal the @probed_cpu_visible_size. Note this is only
			 * currently tracked for I915_MEMORY_CLASS_DEVICE
			 * regions (for other types the value here will also
			 * always equal the @probed_cpu_visible_size).
			 *
			 * If this is an older kernel the value here will be
			 * zero, see also @probed_cpu_visible_size.
			 */
			__u64 unallocated_cpu_visible_size;
		};
	};
};

/**
 * struct __drm_i915_gem_create_ext - Existing gem_create behaviour, with added
 * extension support using struct i915_user_extension.
 *
 * Note that new buffer flags should be added here, at least for the stuff that
 * is immutable. Previously we would have two ioctls, one to create the object
 * with gem_create, and another to apply various parameters, however this
 * creates some ambiguity for the params which are considered immutable. Also in
 * general we're phasing out the various SET/GET ioctls.
 */
struct __drm_i915_gem_create_ext {
	/**
	 * @size: Requested size for the object.
	 *
	 * The (page-aligned) allocated size for the object will be returned.
	 *
	 * Note that for some devices we have might have further minimum
	 * page-size restrictions (larger than 4K), like for device local-memory.
	 * However in general the final size here should always reflect any
	 * rounding up, if for example using the I915_GEM_CREATE_EXT_MEMORY_REGIONS
	 * extension to place the object in device local-memory. The kernel will
	 * always select the largest minimum page-size for the set of possible
	 * placements as the value to use when rounding up the @size.
	 */
	__u64 size;

	/**
	 * @handle: Returned handle for the object.
	 *
	 * Object handles are nonzero.
	 */
	__u32 handle;

	/**
	 * @flags: Optional flags.
	 *
	 * Supported values:
	 *
	 * I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS - Signal to the kernel that
	 * the object will need to be accessed via the CPU.
	 *
	 * Only valid when placing objects in I915_MEMORY_CLASS_DEVICE, and only
	 * strictly required on configurations where some subset of the device
	 * memory is directly visible/mappable through the CPU (which we also
	 * call small BAR), like on some DG2+ systems. Note that this is quite
	 * undesirable, but due to various factors like the client CPU, BIOS etc
	 * it's something we can expect to see in the wild. See
	 * &__drm_i915_memory_region_info.probed_cpu_visible_size for how to
	 * determine if this system applies.
	 *
	 * Note that one of the placements MUST be I915_MEMORY_CLASS_SYSTEM, to
	 * ensure the kernel can always spill the allocation to system memory,
	 * if the object can't be allocated in the mappable part of
	 * I915_MEMORY_CLASS_DEVICE.
	 *
	 * Also note that since the kernel only supports flat-CCS on objects
	 * that can *only* be placed in I915_MEMORY_CLASS_DEVICE, we therefore
	 * don't support I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS together with
	 * flat-CCS.
	 *
	 * Without this hint, the kernel will assume that non-mappable
	 * I915_MEMORY_CLASS_DEVICE is preferred for this object. Note that the
	 * kernel can still migrate the object to the mappable part, as a last
	 * resort, if userspace ever CPU faults this object, but this might be
	 * expensive, and so ideally should be avoided.
	 *
	 * On older kernels which lack the relevant small-bar uAPI support (see
	 * also &__drm_i915_memory_region_info.probed_cpu_visible_size),
	 * usage of the flag will result in an error, but it should NEVER be
	 * possible to end up with a small BAR configuration, assuming we can
	 * also successfully load the i915 kernel module. In such cases the
	 * entire I915_MEMORY_CLASS_DEVICE region will be CPU accessible, and as
	 * such there are zero restrictions on where the object can be placed.
	 */
#define I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS (1 << 0)
	__u32 flags;

	/**
	 * @extensions: The chain of extensions to apply to this object.
	 *
	 * This will be useful in the future when we need to support several
	 * different extensions, and we need to apply more than one when
	 * creating the object. See struct i915_user_extension.
	 *
	 * If we don't supply any extensions then we get the same old gem_create
	 * behaviour.
	 *
	 * For I915_GEM_CREATE_EXT_MEMORY_REGIONS usage see
	 * struct drm_i915_gem_create_ext_memory_regions.
	 *
	 * For I915_GEM_CREATE_EXT_PROTECTED_CONTENT usage see
	 * struct drm_i915_gem_create_ext_protected_content.
	 */
#define I915_GEM_CREATE_EXT_MEMORY_REGIONS 0
#define I915_GEM_CREATE_EXT_PROTECTED_CONTENT 1
	__u64 extensions;
};
