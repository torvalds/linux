/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_VM_DOC_H_
#define _XE_VM_DOC_H_

/**
 * DOC: XE VM (user address space)
 *
 * VM creation
 * ===========
 *
 * Allocate a physical page for root of the page table structure, create default
 * bind engine, and return a handle to the user.
 *
 * Scratch page
 * ------------
 *
 * If the VM is created with the flag, DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE, set the
 * entire page table structure defaults pointing to blank page allocated by the
 * VM. Invalid memory access rather than fault just read / write to this page.
 *
 * VM bind (create GPU mapping for a BO or userptr)
 * ================================================
 *
 * Creates GPU mappings for a BO or userptr within a VM. VM binds uses the same
 * in / out fence interface (struct drm_xe_sync) as execs which allows users to
 * think of binds and execs as more or less the same operation.
 *
 * Operations
 * ----------
 *
 * DRM_XE_VM_BIND_OP_MAP		- Create mapping for a BO
 * DRM_XE_VM_BIND_OP_UNMAP		- Destroy mapping for a BO / userptr
 * DRM_XE_VM_BIND_OP_MAP_USERPTR	- Create mapping for userptr
 *
 * Implementation details
 * ~~~~~~~~~~~~~~~~~~~~~~
 *
 * All bind operations are implemented via a hybrid approach of using the CPU
 * and GPU to modify page tables. If a new physical page is allocated in the
 * page table structure we populate that page via the CPU and insert that new
 * page into the existing page table structure via a GPU job. Also any existing
 * pages in the page table structure that need to be modified also are updated
 * via the GPU job. As the root physical page is prealloced on VM creation our
 * GPU job will always have at least 1 update. The in / out fences are passed to
 * this job so again this is conceptually the same as an exec.
 *
 * Very simple example of few binds on an empty VM with 48 bits of address space
 * and the resulting operations:
 *
 * .. code-block::
 *
 *	bind BO0 0x0-0x1000
 *	alloc page level 3a, program PTE[0] to BO0 phys address (CPU)
 *	alloc page level 2, program PDE[0] page level 3a phys address (CPU)
 *	alloc page level 1, program PDE[0] page level 2 phys address (CPU)
 *	update root PDE[0] to page level 1 phys address (GPU)
 *
 *	bind BO1 0x201000-0x202000
 *	alloc page level 3b, program PTE[1] to BO1 phys address (CPU)
 *	update page level 2 PDE[1] to page level 3b phys address (GPU)
 *
 *	bind BO2 0x1ff000-0x201000
 *	update page level 3a PTE[511] to BO2 phys address (GPU)
 *	update page level 3b PTE[0] to BO2 phys address + 0x1000 (GPU)
 *
 * GPU bypass
 * ~~~~~~~~~~
 *
 * In the above example the steps using the GPU can be converted to CPU if the
 * bind can be done immediately (all in-fences satisfied, VM dma-resv kernel
 * slot is idle).
 *
 * Address space
 * -------------
 *
 * Depending on platform either 48 or 57 bits of address space is supported.
 *
 * Page sizes
 * ----------
 *
 * The minimum page size is either 4k or 64k depending on platform and memory
 * placement (sysmem vs. VRAM). We enforce that binds must be aligned to the
 * minimum page size.
 *
 * Larger pages (2M or 1GB) can be used for BOs in VRAM, the BO physical address
 * is aligned to the larger pages size, and VA is aligned to the larger page
 * size. Larger pages for userptrs / BOs in sysmem should be possible but is not
 * yet implemented.
 *
 * Sync error handling mode
 * ------------------------
 *
 * In both modes during the bind IOCTL the user input is validated. In sync
 * error handling mode the newly bound BO is validated (potentially moved back
 * to a region of memory where is can be used), page tables are updated by the
 * CPU and the job to do the GPU binds is created in the IOCTL itself. This step
 * can fail due to memory pressure. The user can recover by freeing memory and
 * trying this operation again.
 *
 * Async error handling mode
 * -------------------------
 *
 * In async error handling the step of validating the BO, updating page tables,
 * and generating a job are deferred to an async worker. As this step can now
 * fail after the IOCTL has reported success we need an error handling flow for
 * which the user can recover from.
 *
 * The solution is for a user to register a user address with the VM which the
 * VM uses to report errors to. The ufence wait interface can be used to wait on
 * a VM going into an error state. Once an error is reported the VM's async
 * worker is paused. While the VM's async worker is paused sync,
 * DRM_XE_VM_BIND_OP_UNMAP operations are allowed (this can free memory). Once the
 * uses believe the error state is fixed, the async worker can be resumed via
 * XE_VM_BIND_OP_RESTART operation. When VM async bind work is restarted, the
 * first operation processed is the operation that caused the original error.
 *
 * Bind queues / engines
 * ---------------------
 *
 * Think of the case where we have two bind operations A + B and are submitted
 * in that order. A has in fences while B has none. If using a single bind
 * queue, B is now blocked on A's in fences even though it is ready to run. This
 * example is a real use case for VK sparse binding. We work around this
 * limitation by implementing bind engines.
 *
 * In the bind IOCTL the user can optionally pass in an engine ID which must map
 * to an engine which is of the special class DRM_XE_ENGINE_CLASS_VM_BIND.
 * Underneath this is a really virtual engine that can run on any of the copy
 * hardware engines. The job(s) created each IOCTL are inserted into this
 * engine's ring. In the example above if A and B have different bind engines B
 * is free to pass A. If the engine ID field is omitted, the default bind queue
 * for the VM is used.
 *
 * TODO: Explain race in issue 41 and how we solve it
 *
 * Array of bind operations
 * ------------------------
 *
 * The uAPI allows multiple binds operations to be passed in via a user array,
 * of struct drm_xe_vm_bind_op, in a single VM bind IOCTL. This interface
 * matches the VK sparse binding API. The implementation is rather simple, parse
 * the array into a list of operations, pass the in fences to the first operation,
 * and pass the out fences to the last operation. The ordered nature of a bind
 * engine makes this possible.
 *
 * Munmap semantics for unbinds
 * ----------------------------
 *
 * Munmap allows things like:
 *
 * .. code-block::
 *
 *	0x0000-0x2000 and 0x3000-0x5000 have mappings
 *	Munmap 0x1000-0x4000, results in mappings 0x0000-0x1000 and 0x4000-0x5000
 *
 * To support this semantic in the above example we decompose the above example
 * into 4 operations:
 *
 * .. code-block::
 *
 *	unbind 0x0000-0x2000
 *	unbind 0x3000-0x5000
 *	rebind 0x0000-0x1000
 *	rebind 0x4000-0x5000
 *
 * Why not just do a partial unbind of 0x1000-0x2000 and 0x3000-0x4000? This
 * falls apart when using large pages at the edges and the unbind forces us to
 * use a smaller page size. For simplity we always issue a set of unbinds
 * unmapping anything in the range and at most 2 rebinds on the edges.
 *
 * Similar to an array of binds, in fences are passed to the first operation and
 * out fences are signaled on the last operation.
 *
 * In this example there is a window of time where 0x0000-0x1000 and
 * 0x4000-0x5000 are invalid but the user didn't ask for these addresses to be
 * removed from the mapping. To work around this we treat any munmap style
 * unbinds which require a rebind as a kernel operations (BO eviction or userptr
 * invalidation). The first operation waits on the VM's
 * DMA_RESV_USAGE_PREEMPT_FENCE slots (waits for all pending jobs on VM to
 * complete / triggers preempt fences) and the last operation is installed in
 * the VM's DMA_RESV_USAGE_KERNEL slot (blocks future jobs / resume compute mode
 * VM). The caveat is all dma-resv slots must be updated atomically with respect
 * to execs and compute mode rebind worker. To accomplish this, hold the
 * vm->lock in write mode from the first operation until the last.
 *
 * Deferred binds in fault mode
 * ----------------------------
 *
 * If a VM is in fault mode (TODO: link to fault mode), new bind operations that
 * create mappings are by default deferred to the page fault handler (first
 * use). This behavior can be overridden by setting the flag
 * DRM_XE_VM_BIND_FLAG_IMMEDIATE which indicates to creating the mapping
 * immediately.
 *
 * User pointer
 * ============
 *
 * User pointers are user allocated memory (malloc'd, mmap'd, etc..) for which the
 * user wants to create a GPU mapping. Typically in other DRM drivers a dummy BO
 * was created and then a binding was created. We bypass creating a dummy BO in
 * XE and simply create a binding directly from the userptr.
 *
 * Invalidation
 * ------------
 *
 * Since this a core kernel managed memory the kernel can move this memory
 * whenever it wants. We register an invalidation MMU notifier to alert XE when
 * a user pointer is about to move. The invalidation notifier needs to block
 * until all pending users (jobs or compute mode engines) of the userptr are
 * idle to ensure no faults. This done by waiting on all of VM's dma-resv slots.
 *
 * Rebinds
 * -------
 *
 * Either the next exec (non-compute) or rebind worker (compute mode) will
 * rebind the userptr. The invalidation MMU notifier kicks the rebind worker
 * after the VM dma-resv wait if the VM is in compute mode.
 *
 * Compute mode
 * ============
 *
 * A VM in compute mode enables long running workloads and ultra low latency
 * submission (ULLS). ULLS is implemented via a continuously running batch +
 * semaphores. This enables the user to insert jump to new batch commands
 * into the continuously running batch. In both cases these batches exceed the
 * time a dma fence is allowed to exist for before signaling, as such dma fences
 * are not used when a VM is in compute mode. User fences (TODO: link user fence
 * doc) are used instead to signal operation's completion.
 *
 * Preempt fences
 * --------------
 *
 * If the kernel decides to move memory around (either userptr invalidate, BO
 * eviction, or mumap style unbind which results in a rebind) and a batch is
 * running on an engine, that batch can fault or cause a memory corruption as
 * page tables for the moved memory are no longer valid. To work around this we
 * introduce the concept of preempt fences. When sw signaling is enabled on a
 * preempt fence it tells the submission backend to kick that engine off the
 * hardware and the preempt fence signals when the engine is off the hardware.
 * Once all preempt fences are signaled for a VM the kernel can safely move the
 * memory and kick the rebind worker which resumes all the engines execution.
 *
 * A preempt fence, for every engine using the VM, is installed into the VM's
 * dma-resv DMA_RESV_USAGE_PREEMPT_FENCE slot. The same preempt fence, for every
 * engine using the VM, is also installed into the same dma-resv slot of every
 * external BO mapped in the VM.
 *
 * Rebind worker
 * -------------
 *
 * The rebind worker is very similar to an exec. It is responsible for rebinding
 * evicted BOs or userptrs, waiting on those operations, installing new preempt
 * fences, and finally resuming executing of engines in the VM.
 *
 * Flow
 * ~~~~
 *
 * .. code-block::
 *
 *	<----------------------------------------------------------------------|
 *	Check if VM is closed, if so bail out                                  |
 *	Lock VM global lock in read mode                                       |
 *	Pin userptrs (also finds userptr invalidated since last rebind worker) |
 *	Lock VM dma-resv and external BOs dma-resv                             |
 *	Validate BOs that have been evicted                                    |
 *	Wait on and allocate new preempt fences for every engine using the VM  |
 *	Rebind invalidated userptrs + evicted BOs                              |
 *	Wait on last rebind fence                                              |
 *	Wait VM's DMA_RESV_USAGE_KERNEL dma-resv slot                          |
 *	Install preeempt fences and issue resume for every engine using the VM |
 *	Check if any userptrs invalidated since pin                            |
 *		Squash resume for all engines                                  |
 *		Unlock all                                                     |
 *		Wait all VM's dma-resv slots                                   |
 *		Retry ----------------------------------------------------------
 *	Release all engines waiting to resume
 *	Unlock all
 *
 * Timeslicing
 * -----------
 *
 * In order to prevent an engine from continuously being kicked off the hardware
 * and making no forward progress an engine has a period of time it allowed to
 * run after resume before it can be kicked off again. This effectively gives
 * each engine a timeslice.
 *
 * Handling multiple GTs
 * =====================
 *
 * If a GT has slower access to some regions and the page table structure are in
 * the slow region, the performance on that GT could adversely be affected. To
 * work around this we allow a VM page tables to be shadowed in multiple GTs.
 * When VM is created, a default bind engine and PT table structure are created
 * on each GT.
 *
 * Binds can optionally pass in a mask of GTs where a mapping should be created,
 * if this mask is zero then default to all the GTs where the VM has page
 * tables.
 *
 * The implementation for this breaks down into a bunch for_each_gt loops in
 * various places plus exporting a composite fence for multi-GT binds to the
 * user.
 *
 * Fault mode (unified shared memory)
 * ==================================
 *
 * A VM in fault mode can be enabled on devices that support page faults. If
 * page faults are enabled, using dma fences can potentially induce a deadlock:
 * A pending page fault can hold up the GPU work which holds up the dma fence
 * signaling, and memory allocation is usually required to resolve a page
 * fault, but memory allocation is not allowed to gate dma fence signaling. As
 * such, dma fences are not allowed when VM is in fault mode. Because dma-fences
 * are not allowed, only long running workloads and ULLS are enabled on a faulting
 * VM.
 *
 * Deferred VM binds
 * ----------------
 *
 * By default, on a faulting VM binds just allocate the VMA and the actual
 * updating of the page tables is deferred to the page fault handler. This
 * behavior can be overridden by setting the flag DRM_XE_VM_BIND_FLAG_IMMEDIATE in
 * the VM bind which will then do the bind immediately.
 *
 * Page fault handler
 * ------------------
 *
 * Page faults are received in the G2H worker under the CT lock which is in the
 * path of dma fences (no memory allocations are allowed, faults require memory
 * allocations) thus we cannot process faults under the CT lock. Another issue
 * is faults issue TLB invalidations which require G2H credits and we cannot
 * allocate G2H credits in the G2H handlers without deadlocking. Lastly, we do
 * not want the CT lock to be an outer lock of the VM global lock (VM global
 * lock required to fault processing).
 *
 * To work around the above issue with processing faults in the G2H worker, we
 * sink faults to a buffer which is large enough to sink all possible faults on
 * the GT (1 per hardware engine) and kick a worker to process the faults. Since
 * the page faults G2H are already received in a worker, kicking another worker
 * adds more latency to a critical performance path. We add a fast path in the
 * G2H irq handler which looks at first G2H and if it is a page fault we sink
 * the fault to the buffer and kick the worker to process the fault. TLB
 * invalidation responses are also in the critical path so these can also be
 * processed in this fast path.
 *
 * Multiple buffers and workers are used and hashed over based on the ASID so
 * faults from different VMs can be processed in parallel.
 *
 * The page fault handler itself is rather simple, flow is below.
 *
 * .. code-block::
 *
 *	Lookup VM from ASID in page fault G2H
 *	Lock VM global lock in read mode
 *	Lookup VMA from address in page fault G2H
 *	Check if VMA is valid, if not bail
 *	Check if VMA's BO has backing store, if not allocate
 *	<----------------------------------------------------------------------|
 *	If userptr, pin pages                                                  |
 *	Lock VM & BO dma-resv locks                                            |
 *	If atomic fault, migrate to VRAM, else validate BO location            |
 *	Issue rebind                                                           |
 *	Wait on rebind to complete                                             |
 *	Check if userptr invalidated since pin                                 |
 *		Drop VM & BO dma-resv locks                                    |
 *		Retry ----------------------------------------------------------
 *	Unlock all
 *	Issue blocking TLB invalidation                                        |
 *	Send page fault response to GuC
 *
 * Access counters
 * ---------------
 *
 * Access counters can be configured to trigger a G2H indicating the device is
 * accessing VMAs in system memory frequently as hint to migrate those VMAs to
 * VRAM.
 *
 * Same as the page fault handler, access counters G2H cannot be processed the
 * G2H worker under the CT lock. Again we use a buffer to sink access counter
 * G2H. Unlike page faults there is no upper bound so if the buffer is full we
 * simply drop the G2H. Access counters are a best case optimization and it is
 * safe to drop these unlike page faults.
 *
 * The access counter handler itself is rather simple flow is below.
 *
 * .. code-block::
 *
 *	Lookup VM from ASID in access counter G2H
 *	Lock VM global lock in read mode
 *	Lookup VMA from address in access counter G2H
 *	If userptr, bail nothing to do
 *	Lock VM & BO dma-resv locks
 *	Issue migration to VRAM
 *	Unlock all
 *
 * Notice no rebind is issued in the access counter handler as the rebind will
 * be issued on next page fault.
 *
 * Caveats with eviction / user pointer invalidation
 * -------------------------------------------------
 *
 * In the case of eviction and user pointer invalidation on a faulting VM, there
 * is no need to issue a rebind rather we just need to blow away the page tables
 * for the VMAs and the page fault handler will rebind the VMAs when they fault.
 * The caveat is to update / read the page table structure the VM global lock is
 * needed. In both the case of eviction and user pointer invalidation locks are
 * held which make acquiring the VM global lock impossible. To work around this
 * every VMA maintains a list of leaf page table entries which should be written
 * to zero to blow away the VMA's page tables. After writing zero to these
 * entries a blocking TLB invalidate is issued. At this point it is safe for the
 * kernel to move the VMA's memory around. This is a necessary lockless
 * algorithm and is safe as leafs cannot be changed while either an eviction or
 * userptr invalidation is occurring.
 *
 * Locking
 * =======
 *
 * VM locking protects all of the core data paths (bind operations, execs,
 * evictions, and compute mode rebind worker) in XE.
 *
 * Locks
 * -----
 *
 * VM global lock (vm->lock) - rw semaphore lock. Outer most lock which protects
 * the list of userptrs mapped in the VM, the list of engines using this VM, and
 * the array of external BOs mapped in the VM. When adding or removing any of the
 * aforementioned state from the VM should acquire this lock in write mode. The VM
 * bind path also acquires this lock in write while the exec / compute mode
 * rebind worker acquires this lock in read mode.
 *
 * VM dma-resv lock (vm->gpuvm.r_obj->resv->lock) - WW lock. Protects VM dma-resv
 * slots which is shared with any private BO in the VM. Expected to be acquired
 * during VM binds, execs, and compute mode rebind worker. This lock is also
 * held when private BOs are being evicted.
 *
 * external BO dma-resv lock (bo->ttm.base.resv->lock) - WW lock. Protects
 * external BO dma-resv slots. Expected to be acquired during VM binds (in
 * addition to the VM dma-resv lock). All external BO dma-locks within a VM are
 * expected to be acquired (in addition to the VM dma-resv lock) during execs
 * and the compute mode rebind worker. This lock is also held when an external
 * BO is being evicted.
 *
 * Putting it all together
 * -----------------------
 *
 * 1. An exec and bind operation with the same VM can't be executing at the same
 * time (vm->lock).
 *
 * 2. A compute mode rebind worker and bind operation with the same VM can't be
 * executing at the same time (vm->lock).
 *
 * 3. We can't add / remove userptrs or external BOs to a VM while an exec with
 * the same VM is executing (vm->lock).
 *
 * 4. We can't add / remove userptrs, external BOs, or engines to a VM while a
 * compute mode rebind worker with the same VM is executing (vm->lock).
 *
 * 5. Evictions within a VM can't be happen while an exec with the same VM is
 * executing (dma-resv locks).
 *
 * 6. Evictions within a VM can't be happen while a compute mode rebind worker
 * with the same VM is executing (dma-resv locks).
 *
 * dma-resv usage
 * ==============
 *
 * As previously stated to enforce the ordering of kernel ops (eviction, userptr
 * invalidation, munmap style unbinds which result in a rebind), rebinds during
 * execs, execs, and resumes in the rebind worker we use both the VMs and
 * external BOs dma-resv slots. Let try to make this as clear as possible.
 *
 * Slot installation
 * -----------------
 *
 * 1. Jobs from kernel ops install themselves into the DMA_RESV_USAGE_KERNEL
 * slot of either an external BO or VM (depends on if kernel op is operating on
 * an external or private BO)
 *
 * 2. In non-compute mode, jobs from execs install themselves into the
 * DMA_RESV_USAGE_BOOKKEEP slot of the VM
 *
 * 3. In non-compute mode, jobs from execs install themselves into the
 * DMA_RESV_USAGE_WRITE slot of all external BOs in the VM
 *
 * 4. Jobs from binds install themselves into the DMA_RESV_USAGE_BOOKKEEP slot
 * of the VM
 *
 * 5. Jobs from binds install themselves into the DMA_RESV_USAGE_BOOKKEEP slot
 * of the external BO (if the bind is to an external BO, this is addition to #4)
 *
 * 6. Every engine using a compute mode VM has a preempt fence in installed into
 * the DMA_RESV_USAGE_PREEMPT_FENCE slot of the VM
 *
 * 7. Every engine using a compute mode VM has a preempt fence in installed into
 * the DMA_RESV_USAGE_PREEMPT_FENCE slot of all the external BOs in the VM
 *
 * Slot waiting
 * ------------
 *
 * 1. The execution of all jobs from kernel ops shall wait on all slots
 * (DMA_RESV_USAGE_PREEMPT_FENCE) of either an external BO or VM (depends on if
 * kernel op is operating on external or private BO)
 *
 * 2. In non-compute mode, the execution of all jobs from rebinds in execs shall
 * wait on the DMA_RESV_USAGE_KERNEL slot of either an external BO or VM
 * (depends on if the rebind is operatiing on an external or private BO)
 *
 * 3. In non-compute mode, the execution of all jobs from execs shall wait on the
 * last rebind job
 *
 * 4. In compute mode, the execution of all jobs from rebinds in the rebind
 * worker shall wait on the DMA_RESV_USAGE_KERNEL slot of either an external BO
 * or VM (depends on if rebind is operating on external or private BO)
 *
 * 5. In compute mode, resumes in rebind worker shall wait on last rebind fence
 *
 * 6. In compute mode, resumes in rebind worker shall wait on the
 * DMA_RESV_USAGE_KERNEL slot of the VM
 *
 * Putting it all together
 * -----------------------
 *
 * 1. New jobs from kernel ops are blocked behind any existing jobs from
 * non-compute mode execs
 *
 * 2. New jobs from non-compute mode execs are blocked behind any existing jobs
 * from kernel ops and rebinds
 *
 * 3. New jobs from kernel ops are blocked behind all preempt fences signaling in
 * compute mode
 *
 * 4. Compute mode engine resumes are blocked behind any existing jobs from
 * kernel ops and rebinds
 *
 * Future work
 * ===========
 *
 * Support large pages for sysmem and userptr.
 *
 * Update page faults to handle BOs are page level grainularity (e.g. part of BO
 * could be in system memory while another part could be in VRAM).
 *
 * Page fault handler likely we be optimized a bit more (e.g. Rebinds always
 * wait on the dma-resv kernel slots of VM or BO, technically we only have to
 * wait the BO moving. If using a job to do the rebind, we could not block in
 * the page fault handler rather attach a callback to fence of the rebind job to
 * signal page fault complete. Our handling of short circuting for atomic faults
 * for bound VMAs could be better. etc...). We can tune all of this once we have
 * benchmarks / performance number from workloads up and running.
 */

#endif
