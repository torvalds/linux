.. SPDX-License-Identifier: GPL-2.0

==============================
Allocating dma-buf using heaps
==============================

Dma-buf Heaps are a way for userspace to allocate dma-buf objects. They are
typically used to allocate buffers from a specific allocation pool, or to share
buffers across frameworks.

Heaps
=====

A heap represents a specific allocator. The Linux kernel currently supports the
following heaps:

 - The ``system`` heap allocates virtually contiguous, cacheable, buffers.

 - The ``cma`` heap allocates physically contiguous, cacheable,
   buffers. Only present if a CMA region is present. Such a region is
   usually created either through the kernel commandline through the
   ``cma`` parameter, a memory region Device-Tree node with the
   ``linux,cma-default`` property set, or through the ``CMA_SIZE_MBYTES`` or
   ``CMA_SIZE_PERCENTAGE`` Kconfig options. The heap's name in devtmpfs is
   ``default_cma_region``. For backwards compatibility, when the
   ``DMABUF_HEAPS_CMA_LEGACY`` Kconfig option is set, a duplicate node is
   created following legacy naming conventions; the legacy name might be
   ``reserved``, ``linux,cma``, or ``default-pool``.
Naming Convention
=================

``dma-buf`` heaps name should meet a number of constraints:

- The name must be stable, and must not change from one version to the other.
  Userspace identifies heaps by their name, so if the names ever change, we
  would be likely to introduce regressions.

- The name must describe the memory region the heap will allocate from, and
  must uniquely identify it in a given platform. Since userspace applications
  use the heap name as the discriminant, it must be able to tell which heap it
  wants to use reliably if there's multiple heaps.

- The name must not mention implementation details, such as the allocator. The
  heap driver will change over time, and implementation details when it was
  introduced might not be relevant in the future.

- The name should describe properties of the buffers that would be allocated.
  Doing so will make heap identification easier for userspace. Such properties
  are:

  - ``contiguous`` for physically contiguous buffers;

  - ``protected`` for encrypted buffers not accessible the OS;

- The name may describe intended usage. Doing so will make heap identification
  easier for userspace applications and users.

For example, assuming a platform with a reserved memory region located
at the RAM address 0x42000000, intended to allocate video framebuffers,
physically contiguous, and backed by the CMA kernel allocator, good
names would be ``memory@42000000-contiguous`` or ``video@42000000``, but
``cma-video`` wouldn't.
