=========================
I915 DG1/LMEM RFC Section
=========================

Upstream plan
=============
For upstream the overall plan for landing all the DG1 stuff and turning it for
real, with all the uAPI bits is:

* Merge basic HW enabling of DG1(still without pciid)
* Merge the uAPI bits behind special CONFIG_BROKEN(or so) flag
        * At this point we can still make changes, but importantly this lets us
          start running IGTs which can utilize local-memory in CI
* Convert over to TTM, make sure it all keeps working. Some of the work items:
        * TTM shrinker for discrete
        * dma_resv_lockitem for full dma_resv_lock, i.e not just trylock
        * Use TTM CPU pagefault handler
        * Route shmem backend over to TTM SYSTEM for discrete
        * TTM purgeable object support
        * Move i915 buddy allocator over to TTM
        * MMAP ioctl mode(see `I915 MMAP`_)
        * SET/GET ioctl caching(see `I915 SET/GET CACHING`_)
* Send RFC(with mesa-dev on cc) for final sign off on the uAPI
* Add pciid for DG1 and turn on uAPI for real

New object placement and region query uAPI
==========================================
Starting from DG1 we need to give userspace the ability to allocate buffers from
device local-memory. Currently the driver supports gem_create, which can place
buffers in system memory via shmem, and the usual assortment of other
interfaces, like dumb buffers and userptr.

To support this new capability, while also providing a uAPI which will work
beyond just DG1, we propose to offer three new bits of uAPI:

DRM_I915_QUERY_MEMORY_REGIONS
-----------------------------
New query ID which allows userspace to discover the list of supported memory
regions(like system-memory and local-memory) for a given device. We identify
each region with a class and instance pair, which should be unique. The class
here would be DEVICE or SYSTEM, and the instance would be zero, on platforms
like DG1.

Side note: The class/instance design is borrowed from our existing engine uAPI,
where we describe every physical engine in terms of its class, and the
particular instance, since we can have more than one per class.

In the future we also want to expose more information which can further
describe the capabilities of a region.

.. kernel-doc:: include/uapi/drm/i915_drm.h
        :functions: drm_i915_gem_memory_class drm_i915_gem_memory_class_instance drm_i915_memory_region_info drm_i915_query_memory_regions

GEM_CREATE_EXT
--------------
New ioctl which is basically just gem_create but now allows userspace to provide
a chain of possible extensions. Note that if we don't provide any extensions and
set flags=0 then we get the exact same behaviour as gem_create.

Side note: We also need to support PXP[1] in the near future, which is also
applicable to integrated platforms, and adds its own gem_create_ext extension,
which basically lets userspace mark a buffer as "protected".

.. kernel-doc:: include/uapi/drm/i915_drm.h
        :functions: drm_i915_gem_create_ext

I915_GEM_CREATE_EXT_MEMORY_REGIONS
----------------------------------
Implemented as an extension for gem_create_ext, we would now allow userspace to
optionally provide an immutable list of preferred placements at creation time,
in priority order, for a given buffer object.  For the placements we expect
them each to use the class/instance encoding, as per the output of the regions
query. Having the list in priority order will be useful in the future when
placing an object, say during eviction.

.. kernel-doc:: include/uapi/drm/i915_drm.h
        :functions: drm_i915_gem_create_ext_memory_regions

One fair criticism here is that this seems a little over-engineered[2]. If we
just consider DG1 then yes, a simple gem_create.flags or something is totally
all that's needed to tell the kernel to allocate the buffer in local-memory or
whatever. However looking to the future we need uAPI which can also support
upcoming Xe HP multi-tile architecture in a sane way, where there can be
multiple local-memory instances for a given device, and so using both class and
instance in our uAPI to describe regions is desirable, although specifically
for DG1 it's uninteresting, since we only have a single local-memory instance.

Existing uAPI issues
====================
Some potential issues we still need to resolve.

I915 MMAP
---------
In i915 there are multiple ways to MMAP GEM object, including mapping the same
object using different mapping types(WC vs WB), i.e multiple active mmaps per
object. TTM expects one MMAP at most for the lifetime of the object. If it
turns out that we have to backpedal here, there might be some potential
userspace fallout.

I915 SET/GET CACHING
--------------------
In i915 we have set/get_caching ioctl. TTM doesn't let us to change this, but
DG1 doesn't support non-snooped pcie transactions, so we can just always
allocate as WB for smem-only buffers.  If/when our hw gains support for
non-snooped pcie transactions then we must fix this mode at allocation time as
a new GEM extension.

This is related to the mmap problem, because in general (meaning, when we're
not running on intel cpus) the cpu mmap must not, ever, be inconsistent with
allocation mode.

Possible idea is to let the kernel picks the mmap mode for userspace from the
following table:

smem-only: WB. Userspace does not need to call clflush.

smem+lmem: We only ever allow a single mode, so simply allocate this as uncached
memory, and always give userspace a WC mapping. GPU still does snooped access
here(assuming we can't turn it off like on DG1), which is a bit inefficient.

lmem only: always WC

This means on discrete you only get a single mmap mode, all others must be
rejected. That's probably going to be a new default mode or something like
that.

Links
=====
[1] https://patchwork.freedesktop.org/series/86798/

[2] https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/5599#note_553791
