// SPDX-License-Identifier: GPL-2.0 or MIT

//! GPU virtual memory management using the DRM GPUVM framework.
//!
//! This module manages GPU virtual address spaces, providing memory isolation and
//! the illusion of owning the entire virtual address (VA) range, similar to CPU virtual memory.
//! Each virtual memory (VM) area is backed by ARM64 LPAE Stage 1 page tables and can be
//! mapped into hardware address space (AS) slots for GPU execution.

use core::marker::PhantomData;
use core::ops::Range;

use kernel::{
    device::{
        Bound,
        Device, //
    },
    drm::{
        gem::BaseObject,
        gpuvm::{
            DriverGpuVm,
            GpuVaAlloc,
            GpuVm,
            GpuVmBo,
            OpMap,
            OpMapRequest,
            OpMapped,
            OpRemap,
            OpRemapped,
            OpUnmap,
            OpUnmapped,
            UniqueRefGpuVm, //
        }, //
    },
    fmt,
    impl_flags,
    io::PhysAddr,
    iommu::pgtable::{
        prot,
        IoPageTable,
        ARM64LPAES1, //
    },
    new_mutex,
    prelude::*,
    sizes::{
        SZ_1G,
        SZ_2M,
        SZ_4K, //
    },
    sync::{
        aref::ARef,
        Arc,
        ArcBorrow,
        Mutex, //
    },
    uapi, //
};

use crate::{
    driver::{
        TyrDrmDevice,
        TyrDrmDriver, //
    },
    gem,
    gem::Bo,
    gpu::GpuInfo,
    mmu::{
        address_space::VmAsData,
        Mmu, //
    },
    regs::gpu_control::MMU_FEATURES,
};

impl_flags!(
    /// Flags controlling virtual memory mapping behavior.
    ///
    /// These flags control access permissions and caching behavior for GPU virtual
    /// memory mappings.
    #[derive(Debug, Clone, Default, Copy, PartialEq, Eq)]
    pub(crate) struct VmMapFlags(u32);

    /// Individual flags that can be combined in [`VmMapFlags`].
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub(crate) enum VmFlag {
        /// Map as read-only.
        Readonly = uapi::drm_panthor_vm_bind_op_flags_DRM_PANTHOR_VM_BIND_OP_MAP_READONLY as u32,
        /// Map as non-executable.
        Noexec = uapi::drm_panthor_vm_bind_op_flags_DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC as u32,
        /// Map as uncached.
        Uncached = uapi::drm_panthor_vm_bind_op_flags_DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED as u32,
    }
);

impl VmMapFlags {
    /// Convert the flags to `pgtable::prot`.
    fn to_prot(self) -> u32 {
        let mut prot = 0;

        if self.contains(VmFlag::Readonly) {
            prot |= prot::READ;
        } else {
            prot |= prot::READ | prot::WRITE;
        }

        if self.contains(VmFlag::Noexec) {
            prot |= prot::NOEXEC;
        }

        if !self.contains(VmFlag::Uncached) {
            prot |= prot::CACHE;
        }

        prot
    }
}

impl fmt::Display for VmMapFlags {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut first = true;

        if self.contains(VmFlag::Readonly) {
            write!(f, "READONLY")?;
            first = false;
        }
        if self.contains(VmFlag::Noexec) {
            if !first {
                write!(f, " | ")?;
            }
            write!(f, "NOEXEC")?;
            first = false;
        }

        if self.contains(VmFlag::Uncached) {
            if !first {
                write!(f, " | ")?;
            }
            write!(f, "UNCACHED")?;
        }

        Ok(())
    }
}

impl TryFrom<u32> for VmMapFlags {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        let valid = VmFlag::Readonly as u32 | VmFlag::Noexec as u32 | VmFlag::Uncached as u32;

        if value & !valid != 0 {
            return Err(EINVAL);
        }
        Ok(Self(value))
    }
}

/// Arguments for a virtual memory map operation.
struct VmMapArgs<'drm> {
    /// Access permissions and caching behavior for the mapping.
    flags: VmMapFlags,
    /// GEM buffer object registered with the GPUVM framework.
    vm_bo: ARef<GpuVmBo<GpuVmData<'drm>>>,
    /// Offset in bytes from the start of the buffer object.
    bo_offset: u64,
}

/// Type of virtual memory operation.
enum VmOpType<'drm> {
    /// Map a GEM buffer object into the virtual address space.
    Map(VmMapArgs<'drm>),
    /// Unmap a region from the virtual address space.
    Unmap,
}

/// Preallocated resources needed to execute a VM operation.
///
/// VM operations may require allocating new GPUVA objects to track mappings.
/// To avoid allocation failures during the operation, preallocate the
/// maximum number of GPUVAs that might be needed.
struct VmOpResources<'drm> {
    /// Preallocated GPUVA objects for remap operations.
    ///
    /// Partial unmap requests or map requests overlapping existing mappings
    /// will trigger a remap call, which needs to register up to three VA
    /// objects (one for the new mapping, and two for the previous and next
    /// mappings).
    preallocated_gpuvas: [Option<GpuVaAlloc<GpuVmData<'drm>>>; 3],
}

/// Request to execute a virtual memory operation.
struct VmOpRequest<'drm> {
    /// Request type.
    op_type: VmOpType<'drm>,

    /// Region of the virtual address space covered by this request.
    region: Range<u64>,
}

/// Arguments for a page table map operation.
struct PtMapArgs {
    /// Memory protection flags describing allowed accesses for this mapping.
    ///
    /// This is directly derived from [`VmMapFlags`] via [`VmMapFlags::to_prot`].
    prot: u32,
}

/// Type of page table operation.
enum PtOpType {
    /// Map pages into the page table.
    Map(PtMapArgs),
    /// Unmap pages from the page table.
    Unmap,
}

/// Context for updating the GPU page table.
///
/// This context is created when beginning a page table update operation and
/// automatically flushes changes when dropped. It ensures that the
/// Memory Management Unit (MMU) state is properly managed and Translation
/// Lookaside Buffer (TLB) entries are flushed.
pub(crate) struct PtUpdateContext<'ctx, 'drm> {
    /// Device used for DMA-mapping GEM shmem SG tables.
    dev: &'ctx Device<Bound>,

    /// Page table.
    pt: &'ctx IoPageTable<'drm, ARM64LPAES1>,

    /// MMU manager.
    mmu: &'ctx Mmu<'drm>,

    /// Reference to the address space data to pass to the MMU functions.
    as_data: &'ctx VmAsData<'drm>,

    /// Region of the virtual address space covered by this request.
    region: Range<u64>,

    /// Operation type.
    op_type: PtOpType,

    /// Preallocated resources that can be used when executing the request.
    resources: &'ctx mut VmOpResources<'drm>,
}

impl<'ctx, 'drm> PtUpdateContext<'ctx, 'drm> {
    /// Creates a new page table update context.
    ///
    /// This prepares the MMU for a page table update.
    /// The context will automatically flush the TLB and
    /// complete the update when dropped.
    fn new(
        dev: &'ctx Device<Bound>,
        pt: &'ctx IoPageTable<'drm, ARM64LPAES1>,
        mmu: &'ctx Mmu<'drm>,
        as_data: &'ctx VmAsData<'drm>,
        region: Range<u64>,
        op_type: PtOpType,
        resources: &'ctx mut VmOpResources<'drm>,
    ) -> Result<PtUpdateContext<'ctx, 'drm>> {
        mmu.start_vm_update(as_data, &region)?;

        Ok(Self {
            dev,
            pt,
            mmu,
            as_data,
            region,
            op_type,
            resources,
        })
    }

    /// Finds one of our pre-allocated VAs.
    fn preallocated_gpuva(&mut self) -> Result<GpuVaAlloc<GpuVmData<'drm>>> {
        self.resources
            .preallocated_gpuvas
            .iter_mut()
            .find_map(|f| f.take())
            .ok_or(EINVAL)
    }

    /// Returns an unused GPUVA object to the preallocated pool.
    /// If the pool is already full, the unused allocation is simply dropped.
    fn return_preallocated_gpuva(&mut self, gpuva: GpuVaAlloc<GpuVmData<'drm>>) {
        if let Some(slot) = self
            .resources
            .preallocated_gpuvas
            .iter_mut()
            .find(|slot| slot.is_none())
        {
            *slot = Some(gpuva);
        }
    }
}

impl Drop for PtUpdateContext<'_, '_> {
    fn drop(&mut self) {
        if let Err(e) = self.mmu.end_vm_update(self.as_data) {
            dev_err!(self.dev, "Failed to end VM update {:?}", e);
        }

        if let Err(e) = self.mmu.flush_vm(self.as_data) {
            dev_err!(self.dev, "Failed to flush VM {:?}", e);
        }
    }
}

/// Driver implementation for the GPUVM framework.
///
/// Implements [`DriverGpuVm`] to provide VM operation callbacks (map, unmap, remap)
/// and associated types for buffer objects, virtual addresses, and contexts.
pub(crate) struct GpuVmData<'drm> {
    _phantom: PhantomData<&'drm ()>,
}

/// GPU virtual address space.
///
/// Each VM can be mapped into a hardware address space slot.
#[pin_data]
pub(crate) struct Vm<'drm> {
    /// Data referenced by an AS when the VM is active
    as_data: Arc<VmAsData<'drm>>,
    /// MMU manager.
    mmu: Arc<Mmu<'drm>>,
    /// Parent device used for DMA mapping and page-table operations.
    dev: &'drm Device<Bound>,
    /// DRM GPUVM core for managing virtual address space.
    #[pin]
    gpuvm_unique: Mutex<UniqueRefGpuVm<GpuVmData<'drm>>>,
    /// Non-core part of the GPUVM. Can be used for stuff that doesn't modify the
    /// internal mapping tree, like GpuVm::obtain()
    gpuvm: ARef<GpuVm<GpuVmData<'drm>>>,
    /// VA range for this VM.
    va_range: Range<u64>,
}

impl<'drm> Vm<'drm> {
    /// Creates a new GPU virtual address space.
    ///
    /// The VM is initialized with a page table configured according to the GPU's
    /// address translation capabilities and registered with the GPUVM framework.
    pub(crate) fn new(
        dev: &'drm Device<Bound>,
        ddev: &TyrDrmDevice,
        mmu: ArcBorrow<'_, Mmu<'drm>>,
        gpu_info: &GpuInfo,
    ) -> Result<Arc<Vm<'drm>>> {
        let mmu_features = MMU_FEATURES::from_raw(gpu_info.mmu_features);
        let va_bits = mmu_features.va_bits().get();
        let pa_bits = mmu_features.pa_bits().get();

        let range = 0..(1u64 << va_bits);
        let reserve_range = 0..0u64;

        // dummy_obj is used to initialize the GPUVM tree.
        let dummy_obj = gem::new_dummy_object(ddev).inspect_err(|e| {
            dev_err!(dev, "Failed to create dummy GEM object: {:?}", e);
        })?;

        let gpuvm_unique = GpuVm::new::<Error, _>(
            c"Tyr::GpuVm",
            ddev,
            &*dummy_obj,
            range.clone(),
            reserve_range,
            GpuVmData::<'drm> {
                _phantom: PhantomData::<&()>,
            },
        )
        .inspect_err(|e| {
            dev_err!(dev, "Failed to create GpuVm: {:?}", e);
        })?;
        let gpuvm = ARef::from(&*gpuvm_unique);

        let as_data = Arc::pin_init(VmAsData::new(&mmu, dev, va_bits, pa_bits), GFP_KERNEL)?;

        let vm = Arc::pin_init(
            pin_init!(Self{
                as_data,
                dev,
                mmu: mmu.into(),
                gpuvm,
                gpuvm_unique <- new_mutex!(gpuvm_unique),
                va_range: range,
            }),
            GFP_KERNEL,
        )?;

        Ok(vm)
    }

    /// Returns the parent device used by this VM for DMA mapping and page-table operations.
    pub(crate) fn dev(&self) -> &'drm Device<Bound> {
        self.dev
    }

    /// Activate the VM in a hardware address space slot.
    pub(crate) fn activate(&self) -> Result {
        self.mmu
            .activate_vm(self.as_data.as_arc_borrow())
            .inspect_err(|e| {
                dev_err!(self.dev, "Failed to activate VM: {:?}", e);
            })
    }

    /// Deactivate the VM by evicting it from its address space slot.
    fn deactivate(&self) -> Result {
        self.mmu.deactivate_vm(&self.as_data).inspect_err(|e| {
            dev_err!(self.dev, "Failed to deactivate VM: {:?}", e);
        })
    }

    /// Kills the VM by deactivating it and unmapping all regions.
    pub(crate) fn kill(&self) {
        // TODO: Turn the VM into a state where it can't be used.
        let _ = self.deactivate();
        let _ = self
            .unmap_range(self.va_range.start, self.va_range.end - self.va_range.start)
            .inspect_err(|e| {
                dev_err!(self.dev, "Failed to unmap range during deactivate: {:?}", e);
            });
    }

    /// Executes a virtual memory operation.
    ///
    /// This handles both map and unmap operations by coordinating between the
    /// GPUVM framework and the hardware page table.
    fn exec_op<'a>(
        &self,
        gpuvm_unique: &mut UniqueRefGpuVm<GpuVmData<'drm>>,
        req: VmOpRequest<'drm>,
        resources: &'a mut VmOpResources<'drm>,
    ) -> Result {
        let pt = &self.as_data.page_table;

        match req.op_type {
            VmOpType::Map(args) => {
                let mut pt_upd = PtUpdateContext::new(
                    self.dev,
                    pt,
                    &self.mmu,
                    &self.as_data,
                    req.region,
                    PtOpType::Map(PtMapArgs {
                        prot: args.flags.to_prot(),
                    }),
                    resources,
                )?;

                gpuvm_unique.sm_map(OpMapRequest {
                    addr: pt_upd.region.start,
                    range: pt_upd.region.end - pt_upd.region.start,
                    gem_offset: args.bo_offset,
                    vm_bo: &args.vm_bo,
                    context: &mut pt_upd,
                })
                //PtUpdateContext drops here flushing the page table
            }
            VmOpType::Unmap => {
                let mut pt_upd = PtUpdateContext::new(
                    self.dev,
                    pt,
                    &self.mmu,
                    &self.as_data,
                    req.region,
                    PtOpType::Unmap,
                    resources,
                )?;

                gpuvm_unique.sm_unmap(
                    pt_upd.region.start,
                    pt_upd.region.end - pt_upd.region.start,
                    &mut pt_upd,
                )
                //PtUpdateContext drops here flushing the page table
            }
        }
    }

    /// Maps a GEM buffer object range into the VM at the specified virtual address.
    ///
    /// This creates a mapping from GPU virtual address `va` to the physical pages
    /// backing the GEM object, starting at `bo_offset` bytes into the object and
    /// spanning `map_size` bytes. The mapping respects the access permissions and
    /// caching behavior specified in `flags`.
    pub(crate) fn map_bo_range(
        &self,
        bo: &Bo,
        bo_offset: u64,
        map_size: u64,
        va: u64,
        flags: VmMapFlags,
    ) -> Result {
        if map_size == 0
            || va % SZ_4K as u64 != 0
            || bo_offset % SZ_4K as u64 != 0
            || map_size % SZ_4K as u64 != 0
        {
            return Err(EINVAL);
        }

        let bo_size = u64::try_from(bo.size()).map_err(|_| EOVERFLOW)?;
        let bo_end = bo_offset.checked_add(map_size).ok_or(EINVAL)?;

        if bo_end > bo_size {
            dev_err!(
                self.dev,
                "BO mapping range {:#x}..{:#x} exceeds BO size {:#x}",
                bo_offset,
                bo_end,
                bo_size
            );
            return Err(EINVAL);
        }

        let va_end: u64 = va.checked_add(map_size).ok_or(EINVAL)?;

        let req = VmOpRequest {
            op_type: VmOpType::Map(VmMapArgs {
                vm_bo: self.gpuvm.obtain(bo, ())?,
                flags,
                bo_offset,
            }),
            region: va..va_end,
        };
        let mut resources = VmOpResources {
            preallocated_gpuvas: [
                Some(GpuVaAlloc::<GpuVmData<'drm>>::new(GFP_KERNEL)?),
                Some(GpuVaAlloc::<GpuVmData<'drm>>::new(GFP_KERNEL)?),
                Some(GpuVaAlloc::<GpuVmData<'drm>>::new(GFP_KERNEL)?),
            ],
        };
        let result = {
            let mut gpuvm_unique = self.gpuvm_unique.lock();
            self.exec_op(gpuvm_unique.as_mut().get_mut(), req, &mut resources)
        };
        // We flush the defer cleanup list now. Things will be different in
        // the asynchronous VM_BIND path, where we want the cleanup to
        // happen outside the DMA signalling path.
        self.gpuvm.deferred_cleanup();
        result
    }

    /// Unmaps a virtual address range from the VM.
    ///
    /// This removes any existing mappings in the specified range, freeing the
    /// virtual address space for reuse.
    pub(crate) fn unmap_range(&self, va: u64, size: u64) -> Result {
        if size == 0 || va % SZ_4K as u64 != 0 || size % SZ_4K as u64 != 0 {
            return Err(EINVAL);
        }

        let end = va.checked_add(size).ok_or(EINVAL)?;

        if va < self.va_range.start || end > self.va_range.end {
            dev_err!(
                self.dev,
                "Unmap range {:#x}..{:#x} exceeds VM range {:#x}..{:#x}",
                va,
                end,
                self.va_range.start,
                self.va_range.end
            );
            return Err(EINVAL);
        }

        let req = VmOpRequest {
            op_type: VmOpType::Unmap,
            region: va..end,
        };

        let full_vm = va == self.va_range.start && end == self.va_range.end;

        let mut resources = VmOpResources {
            preallocated_gpuvas: if full_vm {
                // Unmapping the entire VM cannot split an existing mapping,
                // so no GPUVA objects are needed for remap operations.
                [None, None, None]
            } else {
                [
                    Some(GpuVaAlloc::<GpuVmData<'drm>>::new(GFP_KERNEL)?),
                    Some(GpuVaAlloc::<GpuVmData<'drm>>::new(GFP_KERNEL)?),
                    Some(GpuVaAlloc::<GpuVmData<'drm>>::new(GFP_KERNEL)?),
                ]
            },
        };
        let result = {
            let mut gpuvm_unique = self.gpuvm_unique.lock();
            self.exec_op(gpuvm_unique.as_mut().get_mut(), req, &mut resources)
        };
        // We flush the defer cleanup list now. Things will be different in
        // the asynchronous VM_BIND path, where we want the cleanup to
        // happen outside the DMA signalling path.
        self.gpuvm.deferred_cleanup();
        result
    }
}

impl<'drm> DriverGpuVm for GpuVmData<'drm> {
    type Driver = TyrDrmDriver;
    type Object = Bo;
    type VmBoData = ();
    type VaData = ();
    type SmContext<'ctx>
        = PtUpdateContext<'ctx, 'drm>
    where
        Self: 'ctx;

    /// Create a new mapping.
    fn sm_step_map<'op>(
        &mut self,
        op: OpMap<'op, Self>,
        context: &mut Self::SmContext<'_>,
    ) -> Result<OpMapped<'op, Self>, Error> {
        let start_iova = op.addr();
        let mut iova = start_iova;
        let mut bytes_left_to_map = op.length();
        let mut gem_offset = op.gem_offset();

        // Make sure that the end of the requested GEM range doesn't run past the
        // end of the GEM buffer itself.
        let gem_range_end = op.gem_offset().checked_add(op.length()).ok_or(EINVAL)?;

        if gem_range_end > op.obj().size() as u64 {
            dev_err!(
                context.dev,
                "Requested GEM range ends at {} which is beyond the GEM buffer size {}",
                gem_range_end,
                op.obj().size()
            );
            return Err(EINVAL);
        }

        let sgt = op.obj().sg_table(context.dev).inspect_err(|e| {
            dev_err!(context.dev, "Failed to get sg_table: {:?}", e);
        })?;
        let prot = match &context.op_type {
            PtOpType::Map(args) => args.prot,
            _ => {
                return Err(EINVAL);
            }
        };

        for sgt_entry in sgt.iter() {
            // Expressly convert to u64 to work with arm 32-bit builds.
            #[allow(clippy::useless_conversion)]
            let mut paddr = u64::from(sgt_entry.dma_address());
            #[allow(clippy::useless_conversion)]
            let mut sgt_entry_length = u64::from(sgt_entry.dma_len());

            if bytes_left_to_map == 0 {
                break;
            }

            if gem_offset > 0 {
                // Skip the entire SGT entry if the gem_offset exceeds its length.
                let skip = u64::min(sgt_entry_length, gem_offset);
                paddr += skip;
                sgt_entry_length -= skip;
                gem_offset -= skip;
            }

            if sgt_entry_length == 0 {
                continue;
            }

            let len = u64::min(sgt_entry_length, bytes_left_to_map);

            let segment_mapped = match pt_map(context.dev, context.pt, iova, paddr, len, prot) {
                Ok(segment_mapped) => segment_mapped,
                Err(e) => {
                    // clean up any successful mappings from previous SGT entries.
                    let total_mapped = iova - start_iova;
                    if total_mapped > 0 {
                        let _ = pt_unmap(
                            context.dev,
                            context.pt,
                            start_iova..(start_iova + total_mapped),
                        );
                    }
                    return Err(e);
                }
            };

            bytes_left_to_map -= segment_mapped;
            iova += segment_mapped;
        }

        if bytes_left_to_map != 0 {
            let total_mapped = iova - start_iova;

            if total_mapped > 0 {
                let _ = pt_unmap(context.dev, context.pt, start_iova..iova);
            }

            dev_err!(
                context.dev,
                "SG table is too small for requested mapping: {} bytes remain",
                bytes_left_to_map
            );

            return Err(EINVAL);
        }

        let gpuva = context.preallocated_gpuva()?;
        let op = op.insert(gpuva, pin_init::init_zeroed());

        Ok(op)
    }

    /// Indicates that an existing mapping should be removed.
    fn sm_step_unmap<'op>(
        &mut self,
        op: OpUnmap<'op, Self>,
        context: &mut Self::SmContext<'_>,
    ) -> Result<OpUnmapped<'op, Self>, Error> {
        let start_iova = op.va().addr();
        let length = op.va().length();

        let region = start_iova..(start_iova + length);
        pt_unmap(context.dev, context.pt, region.clone()).inspect_err(|e| {
            dev_err!(
                context.dev,
                "Failed to unmap region {:#x}..{:#x}: {:?}",
                region.start,
                region.end,
                e
            );
        })?;

        let (op_unmapped, _va_removed) = op.remove();

        Ok(op_unmapped)
    }

    /// Split up an existing mapping.
    fn sm_step_remap<'op>(
        &mut self,
        op: OpRemap<'op, Self>,
        context: &mut Self::SmContext<'_>,
    ) -> Result<OpRemapped<'op, Self>, Error> {
        let unmap_start = if let Some(prev) = op.prev() {
            prev.addr() + prev.length()
        } else {
            op.va_to_unmap().addr()
        };

        let unmap_end = if let Some(next) = op.next() {
            next.addr()
        } else {
            op.va_to_unmap().addr() + op.va_to_unmap().length()
        };

        let unmap_length = unmap_end - unmap_start;

        if unmap_length > 0 {
            let region = unmap_start..(unmap_start + unmap_length);
            pt_unmap(context.dev, context.pt, region.clone()).inspect_err(|e| {
                dev_err!(
                    context.dev,
                    "Failed to unmap remap region {:#x}..{:#x}: {:?}",
                    region.start,
                    region.end,
                    e
                );
            })?;
        }

        let prev_va = context.preallocated_gpuva()?;
        let next_va = context.preallocated_gpuva()?;

        let (op_remapped, remap_ret) = op.remap(
            [prev_va, next_va],
            pin_init::init_zeroed(),
            pin_init::init_zeroed(),
        );

        if let Some(unused_va) = remap_ret.unused_va {
            context.return_preallocated_gpuva(unused_va);
        }

        Ok(op_remapped)
    }
}

/// This function selects the largest supported block size (currently 4KB or 2MB)
/// that can be used for a mapping at the given address and size, respecting alignment constraints.
///
/// We can map multiple pages at once but we can't exceed the size of the
/// table entry itself. So, if mapping 4KB pages, figure out how many pages
/// can be mapped before we hit the 2MB boundary. Or, if mapping 2MB pages,
/// figure out how many pages can be mapped before hitting the 1GB boundary
/// Returns the page size (4KB or 2MB) and the number of pages that can be mapped at that size.
fn get_pgsize(addr: u64, size: u64) -> (u64, u64) {
    // Get the distance to the next boundary of 2MB block
    let blk_offset_2m = addr.wrapping_neg() % (SZ_2M as u64);

    // Use 4K blocks if the address is not 2MB aligned, or we have less than 2MB to map
    if blk_offset_2m != 0 || size < SZ_2M as u64 {
        let pgcount = if blk_offset_2m == 0 {
            size / SZ_4K as u64
        } else {
            u64::min(blk_offset_2m, size) / SZ_4K as u64
        };
        return (SZ_4K as u64, pgcount);
    }

    let blk_offset_1g = addr.wrapping_neg() % (SZ_1G as u64);
    let blk_offset = if blk_offset_1g == 0 {
        SZ_1G as u64
    } else {
        blk_offset_1g
    };
    let pgcount = u64::min(blk_offset, size) / SZ_2M as u64;

    (SZ_2M as u64, pgcount)
}

/// Maps a physical address range into the page table at the specified virtual address.
///
/// This function maps `len` bytes of physical memory starting at `paddr` to the
/// virtual address `iova`, using the protection flags specified in `prot`. It
/// automatically selects optimal page sizes to minimize page table overhead.
///
/// If the mapping fails partway through, all successfully mapped pages are
/// unmapped before returning an error.
///
/// Returns the number of bytes successfully mapped.
fn pt_map(
    dev: &Device,
    pt: &IoPageTable<'_, ARM64LPAES1>,
    iova: u64,
    paddr: u64,
    len: u64,
    prot: u32,
) -> Result<u64> {
    let mut segment_mapped = 0u64;
    while segment_mapped < len {
        let remaining = len - segment_mapped;
        let curr_iova = iova + segment_mapped;
        let curr_paddr = paddr + segment_mapped;

        let (pgsize, pgcount) = get_pgsize(curr_iova | curr_paddr, remaining);

        // On 32-bit systems, usize is only 32 bits, so check that
        // the iova can be converted without truncation.
        let curr_iova = match usize::try_from(curr_iova) {
            Ok(curr_iova) => curr_iova,
            Err(_) => {
                dev_err!(
                    dev,
                    "curr_iova {:#x} cannot be represented as usize (max {:#x})",
                    curr_iova,
                    usize::MAX
                );

                if segment_mapped > 0 {
                    let _ = pt_unmap(dev, pt, iova..(iova + segment_mapped));
                }

                return Err(EOVERFLOW);
            }
        };

        // SAFETY:
        // No other io-pgtable operation can currently access this range because Tyr holds
        // the gpuvm_unique mutex for the entire sm_map() operation.
        // The addresses being mapped won't overlap any existing mappings in this
        // page table because drm_gpuvm_sm_map() checks each requested mapping and either unmaps
        // or remaps any overlap before creating the new mapping.
        let (mapped, result) = unsafe {
            pt.map_pages(
                curr_iova,
                curr_paddr as PhysAddr,
                pgsize as usize,
                pgcount as usize,
                prot,
                GFP_KERNEL,
            )
        };

        if let Err(e) = result {
            // If map_pages fails, mapped will be zero because the ARM LPAE backend
            // only updates the mapped value after the entire request succeeds.
            dev_err!(dev, "pt.map_pages failed at iova {:#x}: {:?}", curr_iova, e);
            if segment_mapped > 0 {
                let _ = pt_unmap(dev, pt, iova..(iova + segment_mapped));
            }
            return Err(e);
        }

        if mapped == 0 {
            dev_err!(dev, "Failed to map any pages at iova {:#x}", curr_iova);
            if segment_mapped > 0 {
                let _ = pt_unmap(dev, pt, iova..(iova + segment_mapped));
            }
            return Err(ENOMEM);
        }

        segment_mapped += mapped as u64;
    }

    Ok(segment_mapped)
}

/// Unmaps a virtual address range from the page table.
///
/// This function removes all page table entries in the specified range,
/// automatically handling different page sizes that may be present.
fn pt_unmap(dev: &Device, pt: &IoPageTable<'_, ARM64LPAES1>, range: Range<u64>) -> Result {
    let mut iova = range.start;
    let mut bytes_left_to_unmap = range.end - range.start;

    while bytes_left_to_unmap > 0 {
        // It is fine to use just the iova to determine the page size
        // because if the actual mapping was represented with smaller page sizes,
        // (e.g. because the physical address was not 2MiB aligned)
        // the ARM LPAE backend will notice and handle the lower-level table correctly.
        let (pgsize, pgcount) = get_pgsize(iova, bytes_left_to_unmap);

        // On 32-bit systems, usize is only 32 bits, so check that
        // the iova can be converted without truncation.
        let iova_usize = usize::try_from(iova).map_err(|_| {
            dev_err!(
                dev,
                "IOVA {:#x} cannot be represented as usize (max {:#x})",
                iova,
                usize::MAX
            );
            EOVERFLOW
        })?;

        // SAFETY:
        // No other io-pgtable operation can currently access this range because Tyr holds
        // the gpuvm_unique mutex for the entire sm_unmap() operation.
        // We know that this page table has one or more consecutive mappings
        // starting at `iova` with the total size of `pgcount * pgsize` because
        // gpuvm callbacks provide exactly the range that was previously mapped.
        let unmapped = unsafe { pt.unmap_pages(iova_usize, pgsize as usize, pgcount as usize) };

        if unmapped == 0 {
            dev_err!(dev, "Failed to unmap any bytes at iova {:#x}", iova_usize);
            return Err(EINVAL);
        }

        bytes_left_to_unmap -= unmapped as u64;
        iova += unmapped as u64;
    }

    Ok(())
}
