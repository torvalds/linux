.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

===============
GPU SVM Section
===============

Agreed upon design principles
=============================

* migrate_to_ram path
	* Rely only on core MM concepts (migration PTEs, page references, and
	  page locking).
	* No driver specific locks other than locks for hardware interaction in
	  this path. These are not required and generally a bad idea to
	  invent driver defined locks to seal core MM races.
	* An example of a driver-specific lock causing issues occurred before
	  fixing do_swap_page to lock the faulting page. A driver-exclusive lock
	  in migrate_to_ram produced a stable livelock if enough threads read
	  the faulting page.
	* Partial migration is supported (i.e., a subset of pages attempting to
	  migrate can actually migrate, with only the faulting page guaranteed
	  to migrate).
	* Driver handles mixed migrations via retry loops rather than locking.
* Eviction
	* Eviction is defined as migrating data from the GPU back to the
	  CPU without a virtual address to free up GPU memory.
	* Only looking at physical memory data structures and locks as opposed to
	  looking at virtual memory data structures and locks.
	* No looking at mm/vma structs or relying on those being locked.
	* The rationale for the above two points is that CPU virtual addresses
	  can change at any moment, while the physical pages remain stable.
	* GPU page table invalidation, which requires a GPU virtual address, is
	  handled via the notifier that has access to the GPU virtual address.
* GPU fault side
	* mmap_read only used around core MM functions which require this lock
	  and should strive to take mmap_read lock only in GPU SVM layer.
	* Big retry loop to handle all races with the mmu notifier under the gpu
	  pagetable locks/mmu notifier range lock/whatever we end up calling
          those.
	* Races (especially against concurrent eviction or migrate_to_ram)
	  should not be handled on the fault side by trying to hold locks;
	  rather, they should be handled using retry loops. One possible
	  exception is holding a BO's dma-resv lock during the initial migration
	  to VRAM, as this is a well-defined lock that can be taken underneath
	  the mmap_read lock.
	* One possible issue with the above approach is if a driver has a strict
	  migration policy requiring GPU access to occur in GPU memory.
	  Concurrent CPU access could cause a livelock due to endless retries.
	  While no current user (Xe) of GPU SVM has such a policy, it is likely
	  to be added in the future. Ideally, this should be resolved on the
	  core-MM side rather than through a driver-side lock.
* Physical memory to virtual backpointer
	* This does not work, as no pointers from physical memory to virtual
	  memory should exist. mremap() is an example of the core MM updating
	  the virtual address without notifying the driver of address
	  change rather the driver only receiving the invalidation notifier.
	* The physical memory backpointer (page->zone_device_data) should remain
	  stable from allocation to page free. Safely updating this against a
	  concurrent user would be very difficult unless the page is free.
* GPU pagetable locking
	* Notifier lock only protects range tree, pages valid state for a range
	  (rather than seqno due to wider notifiers), pagetable entries, and
	  mmu notifier seqno tracking, it is not a global lock to protect
          against races.
	* All races handled with big retry as mentioned above.

Overview of baseline design
===========================

.. kernel-doc:: drivers/gpu/drm/drm_gpusvm.c
   :doc: Overview

.. kernel-doc:: drivers/gpu/drm/drm_gpusvm.c
   :doc: Locking

.. kernel-doc:: drivers/gpu/drm/drm_gpusvm.c
   :doc: Partial Unmapping of Ranges

.. kernel-doc:: drivers/gpu/drm/drm_gpusvm.c
   :doc: Examples

Overview of drm_pagemap design
==============================

.. kernel-doc:: drivers/gpu/drm/drm_pagemap.c
   :doc: Overview

.. kernel-doc:: drivers/gpu/drm/drm_pagemap.c
   :doc: Migration

Possible future design features
===============================

* Concurrent GPU faults
	* CPU faults are concurrent so makes sense to have concurrent GPU
	  faults.
	* Should be possible with fined grained locking in the driver GPU
	  fault handler.
	* No expected GPU SVM changes required.
* Ranges with mixed system and device pages
	* Can be added if required to drm_gpusvm_get_pages fairly easily.
* Multi-GPU support
	* Work in progress and patches expected after initially landing on GPU
	  SVM.
	* Ideally can be done with little to no changes to GPU SVM.
* Drop ranges in favor of radix tree
	* May be desirable for faster notifiers.
* Compound device pages
	* Nvidia, AMD, and Intel all have agreed expensive core MM functions in
	  migrate device layer are a performance bottleneck, having compound
	  device pages should help increase performance by reducing the number
	  of these expensive calls.
* Higher order dma mapping for migration
	* 4k dma mapping adversely affects migration performance on Intel
	  hardware, higher order (2M) dma mapping should help here.
* Build common userptr implementation on top of GPU SVM
* Driver side madvise implementation and migration policies
* Pull in pending dma-mapping API changes from Leon / Nvidia when these land
