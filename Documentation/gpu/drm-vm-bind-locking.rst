.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

===============
VM_BIND locking
===============

This document attempts to describe what's needed to get VM_BIND locking right,
including the userptr mmu_notifier locking. It also discusses some
optimizations to get rid of the looping through of all userptr mappings and
external / shared object mappings that is needed in the simplest
implementation. In addition, there is a section describing the VM_BIND locking
required for implementing recoverable pagefaults.

The DRM GPUVM set of helpers
============================

There is a set of helpers for drivers implementing VM_BIND, and this
set of helpers implements much, but not all of the locking described
in this document. In particular, it is currently lacking a userptr
implementation. This document does not intend to describe the DRM GPUVM
implementation in detail, but it is covered in :ref:`its own
documentation <drm_gpuvm>`. It is highly recommended for any driver
implementing VM_BIND to use the DRM GPUVM helpers and to extend it if
common functionality is missing.

Nomenclature
============

* ``gpu_vm``: Abstraction of a virtual GPU address space with
  meta-data. Typically one per client (DRM file-private), or one per
  execution context.
* ``gpu_vma``: Abstraction of a GPU address range within a gpu_vm with
  associated meta-data. The backing storage of a gpu_vma can either be
  a GEM object or anonymous or page-cache pages mapped also into the CPU
  address space for the process.
* ``gpu_vm_bo``: Abstracts the association of a GEM object and
  a VM. The GEM object maintains a list of gpu_vm_bos, where each gpu_vm_bo
  maintains a list of gpu_vmas.
* ``userptr gpu_vma or just userptr``: A gpu_vma, whose backing store
  is anonymous or page-cache pages as described above.
* ``revalidating``: Revalidating a gpu_vma means making the latest version
  of the backing store resident and making sure the gpu_vma's
  page-table entries point to that backing store.
* ``dma_fence``: A struct dma_fence that is similar to a struct completion
  and which tracks GPU activity. When the GPU activity is finished,
  the dma_fence signals. Please refer to the ``DMA Fences`` section of
  the :doc:`dma-buf doc </driver-api/dma-buf>`.
* ``dma_resv``: A struct dma_resv (a.k.a reservation object) that is used
  to track GPU activity in the form of multiple dma_fences on a
  gpu_vm or a GEM object. The dma_resv contains an array / list
  of dma_fences and a lock that needs to be held when adding
  additional dma_fences to the dma_resv. The lock is of a type that
  allows deadlock-safe locking of multiple dma_resvs in arbitrary
  order. Please refer to the ``Reservation Objects`` section of the
  :doc:`dma-buf doc </driver-api/dma-buf>`.
* ``exec function``: An exec function is a function that revalidates all
  affected gpu_vmas, submits a GPU command batch and registers the
  dma_fence representing the GPU command's activity with all affected
  dma_resvs. For completeness, although not covered by this document,
  it's worth mentioning that an exec function may also be the
  revalidation worker that is used by some drivers in compute /
  long-running mode.
* ``local object``: A GEM object which is only mapped within a
  single VM. Local GEM objects share the gpu_vm's dma_resv.
* ``external object``: a.k.a shared object: A GEM object which may be shared
  by multiple gpu_vms and whose backing storage may be shared with
  other drivers.

Locks and locking order
=======================

One of the benefits of VM_BIND is that local GEM objects share the gpu_vm's
dma_resv object and hence the dma_resv lock. So, even with a huge
number of local GEM objects, only one lock is needed to make the exec
sequence atomic.

The following locks and locking orders are used:

* The ``gpu_vm->lock`` (optionally an rwsem). Protects the gpu_vm's
  data structure keeping track of gpu_vmas. It can also protect the
  gpu_vm's list of userptr gpu_vmas. With a CPU mm analogy this would
  correspond to the mmap_lock. An rwsem allows several readers to walk
  the VM tree concurrently, but the benefit of that concurrency most
  likely varies from driver to driver.
* The ``userptr_seqlock``. This lock is taken in read mode for each
  userptr gpu_vma on the gpu_vm's userptr list, and in write mode during mmu
  notifier invalidation. This is not a real seqlock but described in
  ``mm/mmu_notifier.c`` as a "Collision-retry read-side/write-side
  'lock' a lot like a seqcount. However this allows multiple
  write-sides to hold it at once...". The read side critical section
  is enclosed by ``mmu_interval_read_begin() /
  mmu_interval_read_retry()`` with ``mmu_interval_read_begin()``
  sleeping if the write side is held.
  The write side is held by the core mm while calling mmu interval
  invalidation notifiers.
* The ``gpu_vm->resv`` lock. Protects the gpu_vm's list of gpu_vmas needing
  rebinding, as well as the residency state of all the gpu_vm's local
  GEM objects.
  Furthermore, it typically protects the gpu_vm's list of evicted and
  external GEM objects.
* The ``gpu_vm->userptr_notifier_lock``. This is an rwsem that is
  taken in read mode during exec and write mode during a mmu notifier
  invalidation. The userptr notifier lock is per gpu_vm.
* The ``gem_object->gpuva_lock`` This lock protects the GEM object's
  list of gpu_vm_bos. This is usually the same lock as the GEM
  object's dma_resv, but some drivers protects this list differently,
  see below.
* The ``gpu_vm list spinlocks``. With some implementations they are needed
  to be able to update the gpu_vm evicted- and external object
  list. For those implementations, the spinlocks are grabbed when the
  lists are manipulated. However, to avoid locking order violations
  with the dma_resv locks, a special scheme is needed when iterating
  over the lists.

.. _gpu_vma lifetime:

Protection and lifetime of gpu_vm_bos and gpu_vmas
==================================================

The GEM object's list of gpu_vm_bos, and the gpu_vm_bo's list of gpu_vmas
is protected by the ``gem_object->gpuva_lock``, which is typically the
same as the GEM object's dma_resv, but if the driver
needs to access these lists from within a dma_fence signalling
critical section, it can instead choose to protect it with a
separate lock, which can be locked from within the dma_fence signalling
critical section. Such drivers then need to pay additional attention
to what locks need to be taken from within the loop when iterating
over the gpu_vm_bo and gpu_vma lists to avoid locking-order violations.

The DRM GPUVM set of helpers provide lockdep asserts that this lock is
held in relevant situations and also provides a means of making itself
aware of which lock is actually used: :c:func:`drm_gem_gpuva_set_lock`.

Each gpu_vm_bo holds a reference counted pointer to the underlying GEM
object, and each gpu_vma holds a reference counted pointer to the
gpu_vm_bo. When iterating over the GEM object's list of gpu_vm_bos and
over the gpu_vm_bo's list of gpu_vmas, the ``gem_object->gpuva_lock`` must
not be dropped, otherwise, gpu_vmas attached to a gpu_vm_bo may
disappear without notice since those are not reference-counted. A
driver may implement its own scheme to allow this at the expense of
additional complexity, but this is outside the scope of this document.

In the DRM GPUVM implementation, each gpu_vm_bo and each gpu_vma
holds a reference count on the gpu_vm itself. Due to this, and to avoid circular
reference counting, cleanup of the gpu_vm's gpu_vmas must not be done from the
gpu_vm's destructor. Drivers typically implements a gpu_vm close
function for this cleanup. The gpu_vm close function will abort gpu
execution using this VM, unmap all gpu_vmas and release page-table memory.

Revalidation and eviction of local objects
==========================================

Note that in all the code examples given below we use simplified
pseudo-code. In particular, the dma_resv deadlock avoidance algorithm
as well as reserving memory for dma_resv fences is left out.

Revalidation
____________
With VM_BIND, all local objects need to be resident when the gpu is
executing using the gpu_vm, and the objects need to have valid
gpu_vmas set up pointing to them. Typically, each gpu command buffer
submission is therefore preceded with a re-validation section:

.. code-block:: C

   dma_resv_lock(gpu_vm->resv);

   // Validation section starts here.
   for_each_gpu_vm_bo_on_evict_list(&gpu_vm->evict_list, &gpu_vm_bo) {
           validate_gem_bo(&gpu_vm_bo->gem_bo);

           // The following list iteration needs the Gem object's
           // dma_resv to be held (it protects the gpu_vm_bo's list of
           // gpu_vmas, but since local gem objects share the gpu_vm's
           // dma_resv, it is already held at this point.
           for_each_gpu_vma_of_gpu_vm_bo(&gpu_vm_bo, &gpu_vma)
                  move_gpu_vma_to_rebind_list(&gpu_vma, &gpu_vm->rebind_list);
   }

   for_each_gpu_vma_on_rebind_list(&gpu vm->rebind_list, &gpu_vma) {
           rebind_gpu_vma(&gpu_vma);
           remove_gpu_vma_from_rebind_list(&gpu_vma);
   }
   // Validation section ends here, and job submission starts.

   add_dependencies(&gpu_job, &gpu_vm->resv);
   job_dma_fence = gpu_submit(&gpu_job));

   add_dma_fence(job_dma_fence, &gpu_vm->resv);
   dma_resv_unlock(gpu_vm->resv);

The reason for having a separate gpu_vm rebind list is that there
might be userptr gpu_vmas that are not mapping a buffer object that
also need rebinding.

Eviction
________

Eviction of one of these local objects will then look similar to the
following:

.. code-block:: C

   obj = get_object_from_lru();

   dma_resv_lock(obj->resv);
   for_each_gpu_vm_bo_of_obj(obj, &gpu_vm_bo);
           add_gpu_vm_bo_to_evict_list(&gpu_vm_bo, &gpu_vm->evict_list);

   add_dependencies(&eviction_job, &obj->resv);
   job_dma_fence = gpu_submit(&eviction_job);
   add_dma_fence(&obj->resv, job_dma_fence);

   dma_resv_unlock(&obj->resv);
   put_object(obj);

Note that since the object is local to the gpu_vm, it will share the gpu_vm's
dma_resv lock such that ``obj->resv == gpu_vm->resv``.
The gpu_vm_bos marked for eviction are put on the gpu_vm's evict list,
which is protected by ``gpu_vm->resv``. During eviction all local
objects have their dma_resv locked and, due to the above equality, also
the gpu_vm's dma_resv protecting the gpu_vm's evict list is locked.

With VM_BIND, gpu_vmas don't need to be unbound before eviction,
since the driver must ensure that the eviction blit or copy will wait
for GPU idle or depend on all previous GPU activity. Furthermore, any
subsequent attempt by the GPU to access freed memory through the
gpu_vma will be preceded by a new exec function, with a revalidation
section which will make sure all gpu_vmas are rebound. The eviction
code holding the object's dma_resv while revalidating will ensure a
new exec function may not race with the eviction.

A driver can be implemented in such a way that, on each exec function,
only a subset of vmas are selected for rebind.  In this case, all vmas that are
*not* selected for rebind must be unbound before the exec
function workload is submitted.

Locking with external buffer objects
====================================

Since external buffer objects may be shared by multiple gpu_vm's they
can't share their reservation object with a single gpu_vm. Instead
they need to have a reservation object of their own. The external
objects bound to a gpu_vm using one or many gpu_vmas are therefore put on a
per-gpu_vm list which is protected by the gpu_vm's dma_resv lock or
one of the :ref:`gpu_vm list spinlocks <Spinlock iteration>`. Once
the gpu_vm's reservation object is locked, it is safe to traverse the
external object list and lock the dma_resvs of all external
objects. However, if instead a list spinlock is used, a more elaborate
iteration scheme needs to be used.

At eviction time, the gpu_vm_bos of *all* the gpu_vms an external
object is bound to need to be put on their gpu_vm's evict list.
However, when evicting an external object, the dma_resvs of the
gpu_vms the object is bound to are typically not held. Only
the object's private dma_resv can be guaranteed to be held. If there
is a ww_acquire context at hand at eviction time we could grab those
dma_resvs but that could cause expensive ww_mutex rollbacks. A simple
option is to just mark the gpu_vm_bos of the evicted gem object with
an ``evicted`` bool that is inspected before the next time the
corresponding gpu_vm evicted list needs to be traversed. For example, when
traversing the list of external objects and locking them. At that time,
both the gpu_vm's dma_resv and the object's dma_resv is held, and the
gpu_vm_bo marked evicted, can then be added to the gpu_vm's list of
evicted gpu_vm_bos. The ``evicted`` bool is formally protected by the
object's dma_resv.

The exec function becomes

.. code-block:: C

   dma_resv_lock(gpu_vm->resv);

   // External object list is protected by the gpu_vm->resv lock.
   for_each_gpu_vm_bo_on_extobj_list(gpu_vm, &gpu_vm_bo) {
           dma_resv_lock(gpu_vm_bo.gem_obj->resv);
           if (gpu_vm_bo_marked_evicted(&gpu_vm_bo))
                   add_gpu_vm_bo_to_evict_list(&gpu_vm_bo, &gpu_vm->evict_list);
   }

   for_each_gpu_vm_bo_on_evict_list(&gpu_vm->evict_list, &gpu_vm_bo) {
           validate_gem_bo(&gpu_vm_bo->gem_bo);

           for_each_gpu_vma_of_gpu_vm_bo(&gpu_vm_bo, &gpu_vma)
                  move_gpu_vma_to_rebind_list(&gpu_vma, &gpu_vm->rebind_list);
   }

   for_each_gpu_vma_on_rebind_list(&gpu vm->rebind_list, &gpu_vma) {
           rebind_gpu_vma(&gpu_vma);
           remove_gpu_vma_from_rebind_list(&gpu_vma);
   }

   add_dependencies(&gpu_job, &gpu_vm->resv);
   job_dma_fence = gpu_submit(&gpu_job));

   add_dma_fence(job_dma_fence, &gpu_vm->resv);
   for_each_external_obj(gpu_vm, &obj)
          add_dma_fence(job_dma_fence, &obj->resv);
   dma_resv_unlock_all_resv_locks();

And the corresponding shared-object aware eviction would look like:

.. code-block:: C

   obj = get_object_from_lru();

   dma_resv_lock(obj->resv);
   for_each_gpu_vm_bo_of_obj(obj, &gpu_vm_bo)
           if (object_is_vm_local(obj))
                add_gpu_vm_bo_to_evict_list(&gpu_vm_bo, &gpu_vm->evict_list);
           else
                mark_gpu_vm_bo_evicted(&gpu_vm_bo);

   add_dependencies(&eviction_job, &obj->resv);
   job_dma_fence = gpu_submit(&eviction_job);
   add_dma_fence(&obj->resv, job_dma_fence);

   dma_resv_unlock(&obj->resv);
   put_object(obj);

.. _Spinlock iteration:

Accessing the gpu_vm's lists without the dma_resv lock held
===========================================================

Some drivers will hold the gpu_vm's dma_resv lock when accessing the
gpu_vm's evict list and external objects lists. However, there are
drivers that need to access these lists without the dma_resv lock
held, for example due to asynchronous state updates from within the
dma_fence signalling critical path. In such cases, a spinlock can be
used to protect manipulation of the lists. However, since higher level
sleeping locks need to be taken for each list item while iterating
over the lists, the items already iterated over need to be
temporarily moved to a private list and the spinlock released
while processing each item:

.. code block:: C

    struct list_head still_in_list;

    INIT_LIST_HEAD(&still_in_list);

    spin_lock(&gpu_vm->list_lock);
    do {
            struct list_head *entry = list_first_entry_or_null(&gpu_vm->list, head);

            if (!entry)
                    break;

            list_move_tail(&entry->head, &still_in_list);
            list_entry_get_unless_zero(entry);
            spin_unlock(&gpu_vm->list_lock);

            process(entry);

            spin_lock(&gpu_vm->list_lock);
            list_entry_put(entry);
    } while (true);

    list_splice_tail(&still_in_list, &gpu_vm->list);
    spin_unlock(&gpu_vm->list_lock);

Due to the additional locking and atomic operations, drivers that *can*
avoid accessing the gpu_vm's list outside of the dma_resv lock
might want to avoid also this iteration scheme. Particularly, if the
driver anticipates a large number of list items. For lists where the
anticipated number of list items is small, where list iteration doesn't
happen very often or if there is a significant additional cost
associated with each iteration, the atomic operation overhead
associated with this type of iteration is, most likely, negligible. Note that
if this scheme is used, it is necessary to make sure this list
iteration is protected by an outer level lock or semaphore, since list
items are temporarily pulled off the list while iterating, and it is
also worth mentioning that the local list ``still_in_list`` should
also be considered protected by the ``gpu_vm->list_lock``, and it is
thus possible that items can be removed also from the local list
concurrently with list iteration.

Please refer to the :ref:`DRM GPUVM locking section
<drm_gpuvm_locking>` and its internal
:c:func:`get_next_vm_bo_from_list` function.


userptr gpu_vmas
================

A userptr gpu_vma is a gpu_vma that, instead of mapping a buffer object to a
GPU virtual address range, directly maps a CPU mm range of anonymous-
or file page-cache pages.
A very simple approach would be to just pin the pages using
pin_user_pages() at bind time and unpin them at unbind time, but this
creates a Denial-Of-Service vector since a single user-space process
would be able to pin down all of system memory, which is not
desirable. (For special use-cases and assuming proper accounting pinning might
still be a desirable feature, though). What we need to do in the
general case is to obtain a reference to the desired pages, make sure
we are notified using a MMU notifier just before the CPU mm unmaps the
pages, dirty them if they are not mapped read-only to the GPU, and
then drop the reference.
When we are notified by the MMU notifier that CPU mm is about to drop the
pages, we need to stop GPU access to the pages by waiting for VM idle
in the MMU notifier and make sure that before the next time the GPU
tries to access whatever is now present in the CPU mm range, we unmap
the old pages from the GPU page tables and repeat the process of
obtaining new page references. (See the :ref:`notifier example
<Invalidation example>` below). Note that when the core mm decides to
laundry pages, we get such an unmap MMU notification and can mark the
pages dirty again before the next GPU access. We also get similar MMU
notifications for NUMA accounting which the GPU driver doesn't really
need to care about, but so far it has proven difficult to exclude
certain notifications.

Using a MMU notifier for device DMA (and other methods) is described in
:ref:`the pin_user_pages() documentation <mmu-notifier-registration-case>`.

Now, the method of obtaining struct page references using
get_user_pages() unfortunately can't be used under a dma_resv lock
since that would violate the locking order of the dma_resv lock vs the
mmap_lock that is grabbed when resolving a CPU pagefault. This means
the gpu_vm's list of userptr gpu_vmas needs to be protected by an
outer lock, which in our example below is the ``gpu_vm->lock``.

The MMU interval seqlock for a userptr gpu_vma is used in the following
way:

.. code-block:: C

   // Exclusive locking mode here is strictly needed only if there are
   // invalidated userptr gpu_vmas present, to avoid concurrent userptr
   // revalidations of the same userptr gpu_vma.
   down_write(&gpu_vm->lock);
   retry:

   // Note: mmu_interval_read_begin() blocks until there is no
   // invalidation notifier running anymore.
   seq = mmu_interval_read_begin(&gpu_vma->userptr_interval);
   if (seq != gpu_vma->saved_seq) {
           obtain_new_page_pointers(&gpu_vma);
           dma_resv_lock(&gpu_vm->resv);
           add_gpu_vma_to_revalidate_list(&gpu_vma, &gpu_vm);
           dma_resv_unlock(&gpu_vm->resv);
           gpu_vma->saved_seq = seq;
   }

   // The usual revalidation goes here.

   // Final userptr sequence validation may not happen before the
   // submission dma_fence is added to the gpu_vm's resv, from the POW
   // of the MMU invalidation notifier. Hence the
   // userptr_notifier_lock that will make them appear atomic.

   add_dependencies(&gpu_job, &gpu_vm->resv);
   down_read(&gpu_vm->userptr_notifier_lock);
   if (mmu_interval_read_retry(&gpu_vma->userptr_interval, gpu_vma->saved_seq)) {
          up_read(&gpu_vm->userptr_notifier_lock);
          goto retry;
   }

   job_dma_fence = gpu_submit(&gpu_job));

   add_dma_fence(job_dma_fence, &gpu_vm->resv);

   for_each_external_obj(gpu_vm, &obj)
          add_dma_fence(job_dma_fence, &obj->resv);

   dma_resv_unlock_all_resv_locks();
   up_read(&gpu_vm->userptr_notifier_lock);
   up_write(&gpu_vm->lock);

The code between ``mmu_interval_read_begin()`` and the
``mmu_interval_read_retry()`` marks the read side critical section of
what we call the ``userptr_seqlock``. In reality, the gpu_vm's userptr
gpu_vma list is looped through, and the check is done for *all* of its
userptr gpu_vmas, although we only show a single one here.

The userptr gpu_vma MMU invalidation notifier might be called from
reclaim context and, again, to avoid locking order violations, we can't
take any dma_resv lock nor the gpu_vm->lock from within it.

.. _Invalidation example:
.. code-block:: C

  bool gpu_vma_userptr_invalidate(userptr_interval, cur_seq)
  {
          // Make sure the exec function either sees the new sequence
          // and backs off or we wait for the dma-fence:

          down_write(&gpu_vm->userptr_notifier_lock);
          mmu_interval_set_seq(userptr_interval, cur_seq);
          up_write(&gpu_vm->userptr_notifier_lock);

          // At this point, the exec function can't succeed in
          // submitting a new job, because cur_seq is an invalid
          // sequence number and will always cause a retry. When all
          // invalidation callbacks, the mmu notifier core will flip
          // the sequence number to a valid one. However we need to
          // stop gpu access to the old pages here.

          dma_resv_wait_timeout(&gpu_vm->resv, DMA_RESV_USAGE_BOOKKEEP,
                                false, MAX_SCHEDULE_TIMEOUT);
          return true;
  }

When this invalidation notifier returns, the GPU can no longer be
accessing the old pages of the userptr gpu_vma and needs to redo the
page-binding before a new GPU submission can succeed.

Efficient userptr gpu_vma exec_function iteration
_________________________________________________

If the gpu_vm's list of userptr gpu_vmas becomes large, it's
inefficient to iterate through the complete lists of userptrs on each
exec function to check whether each userptr gpu_vma's saved
sequence number is stale. A solution to this is to put all
*invalidated* userptr gpu_vmas on a separate gpu_vm list and
only check the gpu_vmas present on this list on each exec
function. This list will then lend itself very-well to the spinlock
locking scheme that is
:ref:`described in the spinlock iteration section <Spinlock iteration>`, since
in the mmu notifier, where we add the invalidated gpu_vmas to the
list, it's not possible to take any outer locks like the
``gpu_vm->lock`` or the ``gpu_vm->resv`` lock. Note that the
``gpu_vm->lock`` still needs to be taken while iterating to ensure the list is
complete, as also mentioned in that section.

If using an invalidated userptr list like this, the retry check in the
exec function trivially becomes a check for invalidated list empty.

Locking at bind and unbind time
===============================

At bind time, assuming a GEM object backed gpu_vma, each
gpu_vma needs to be associated with a gpu_vm_bo and that
gpu_vm_bo in turn needs to be added to the GEM object's
gpu_vm_bo list, and possibly to the gpu_vm's external object
list. This is referred to as *linking* the gpu_vma, and typically
requires that the ``gpu_vm->lock`` and the ``gem_object->gpuva_lock``
are held. When unlinking a gpu_vma the same locks should be held,
and that ensures that when iterating over ``gpu_vmas`, either under
the ``gpu_vm->resv`` or the GEM object's dma_resv, that the gpu_vmas
stay alive as long as the lock under which we iterate is not released. For
userptr gpu_vmas it's similarly required that during vma destroy, the
outer ``gpu_vm->lock`` is held, since otherwise when iterating over
the invalidated userptr list as described in the previous section,
there is nothing keeping those userptr gpu_vmas alive.

Locking for recoverable page-fault page-table updates
=====================================================

There are two important things we need to ensure with locking for
recoverable page-faults:

* At the time we return pages back to the system / allocator for
  reuse, there should be no remaining GPU mappings and any GPU TLB
  must have been flushed.
* The unmapping and mapping of a gpu_vma must not race.

Since the unmapping (or zapping) of GPU ptes is typically taking place
where it is hard or even impossible to take any outer level locks we
must either introduce a new lock that is held at both mapping and
unmapping time, or look at the locks we do hold at unmapping time and
make sure that they are held also at mapping time. For userptr
gpu_vmas, the ``userptr_seqlock`` is held in write mode in the mmu
invalidation notifier where zapping happens. Hence, if the
``userptr_seqlock`` as well as the ``gpu_vm->userptr_notifier_lock``
is held in read mode during mapping, it will not race with the
zapping. For GEM object backed gpu_vmas, zapping will take place under
the GEM object's dma_resv and ensuring that the dma_resv is held also
when populating the page-tables for any gpu_vma pointing to the GEM
object, will similarly ensure we are race-free.

If any part of the mapping is performed asynchronously
under a dma-fence with these locks released, the zapping will need to
wait for that dma-fence to signal under the relevant lock before
starting to modify the page-table.

Since modifying the
page-table structure in a way that frees up page-table memory
might also require outer level locks, the zapping of GPU ptes
typically focuses only on zeroing page-table or page-directory entries
and flushing TLB, whereas freeing of page-table memory is deferred to
unbind or rebind time.
