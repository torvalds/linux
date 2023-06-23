==========================
I915 Small BAR RFC Section
==========================
Starting from DG2 we will have resizable BAR support for device local-memory(i.e
I915_MEMORY_CLASS_DEVICE), but in some cases the final BAR size might still be
smaller than the total probed_size. In such cases, only some subset of
I915_MEMORY_CLASS_DEVICE will be CPU accessible(for example the first 256M),
while the remainder is only accessible via the GPU.

I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS flag
----------------------------------------------
New gem_create_ext flag to tell the kernel that a BO will require CPU access.
This becomes important when placing an object in I915_MEMORY_CLASS_DEVICE, where
underneath the device has a small BAR, meaning only some portion of it is CPU
accessible. Without this flag the kernel will assume that CPU access is not
required, and prioritize using the non-CPU visible portion of
I915_MEMORY_CLASS_DEVICE.

.. kernel-doc:: Documentation/gpu/rfc/i915_small_bar.h
   :functions: __drm_i915_gem_create_ext

probed_cpu_visible_size attribute
---------------------------------
New struct__drm_i915_memory_region attribute which returns the total size of the
CPU accessible portion, for the particular region. This should only be
applicable for I915_MEMORY_CLASS_DEVICE. We also report the
unallocated_cpu_visible_size, alongside the unallocated_size.

Vulkan will need this as part of creating a separate VkMemoryHeap with the
VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT set, to represent the CPU visible portion,
where the total size of the heap needs to be known. It also wants to be able to
give a rough estimate of how memory can potentially be allocated.

.. kernel-doc:: Documentation/gpu/rfc/i915_small_bar.h
   :functions: __drm_i915_memory_region_info

Error Capture restrictions
--------------------------
With error capture we have two new restrictions:

    1) Error capture is best effort on small BAR systems; if the pages are not
    CPU accessible, at the time of capture, then the kernel is free to skip
    trying to capture them.

    2) On discrete and newer integrated platforms we now reject error capture
    on recoverable contexts. In the future the kernel may want to blit during
    error capture, when for example something is not currently CPU accessible.
