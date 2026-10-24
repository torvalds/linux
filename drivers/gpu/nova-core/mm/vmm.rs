// SPDX-License-Identifier: GPL-2.0

//! Virtual Memory Manager for NVIDIA GPU page table management.
//!
//! The [`Vmm`] provides high-level page mapping and unmapping operations for GPU
//! virtual address spaces (Channels, BAR1, BAR2).

use kernel::{
    gpu::buddy::AllocatedBlocks,
    maple_tree::MapleTreeAlloc,
    prelude::*,
    rbtree::RBTree, //
};

use core::{
    cell::Cell,
    ops::Range, //
};

use crate::{
    mm::{
        pagetable::{
            map::{
                PtMap, //
            },
            walk::{
                PtWalk,
                WalkResult, //
            },
            MmuVersion, //
        },
        GpuMm,
        Pfn,
        Vfn,
        VramAddress,
        PAGE_SIZE, //
    },
    num::{
        IntoSafeCast, //
    },
};

/// Multi-page prepared mapping -- VA range allocated, ready for execute.
///
/// Produced by [`Vmm::prepare_map()`], consumed by [`Vmm::execute_map()`].
/// The VA space allocation is tracked in the [`Vmm`]'s maple tree and freed
/// on error or via [`Vmm::unmap_pages()`].
///
/// Dropping without calling [`Vmm::execute_map()`] logs a warning and leaks
/// the VA range in the maple tree.
pub(crate) struct PreparedMapping {
    vfn_start: Vfn,
    num_pages: usize,
    /// Logs a warning if dropped without executing.
    _drop_guard: MustExecuteGuard,
}

/// Result of a mapping operation -- tracks the active mapped range.
///
/// Returned by [`Vmm::execute_map()`] and [`Vmm::map_pages()`].
/// Callers must call [`Vmm::unmap_pages()`] before dropping to invalidate
/// PTEs and free the VA range. Dropping without unmapping logs a warning
/// and leaks the VA range in the maple tree.
pub(crate) struct MappedRange {
    pub(super) vfn_start: Vfn,
    pub(super) num_pages: usize,
    /// Logs a warning if dropped without unmapping.
    _drop_guard: MustUnmapGuard,
}

/// Guard that logs a warning if a [`PreparedMapping`] is dropped without
/// being consumed by [`Vmm::execute_map()`].
struct MustExecuteGuard {
    armed: Cell<bool>,
}

impl MustExecuteGuard {
    const fn new() -> Self {
        Self {
            armed: Cell::new(true),
        }
    }

    fn disarm(&self) {
        self.armed.set(false);
    }
}

impl Drop for MustExecuteGuard {
    fn drop(&mut self) {
        if self.armed.get() {
            kernel::pr_warn!("PreparedMapping dropped without calling execute_map()\n");
        }
    }
}

/// Guard that logs a warning if a [`MappedRange`] is dropped without
/// calling [`Vmm::unmap_pages()`].
struct MustUnmapGuard {
    armed: Cell<bool>,
}

impl MustUnmapGuard {
    const fn new() -> Self {
        Self {
            armed: Cell::new(true),
        }
    }

    fn disarm(&self) {
        self.armed.set(false);
    }
}

impl Drop for MustUnmapGuard {
    fn drop(&mut self) {
        if self.armed.get() {
            kernel::pr_warn!("MappedRange dropped without calling unmap_pages()\n");
        }
    }
}

/// Virtual Memory Manager for a GPU address space.
///
/// Each [`Vmm`] instance manages a single address space identified by its Page
/// Directory Base (`PDB`) address. Used for Channel, BAR1 and BAR2 mappings.
pub(crate) struct Vmm {
    /// Page Directory Base address for this address space.
    #[expect(dead_code)]
    pdb_addr: VramAddress,
    /// Page table walker for reading existing mappings.
    pt_walk: PtWalk,
    /// Page table mapper for prepare/execute operations.
    pt_map: PtMap,
    /// Page table allocations required for mappings.
    page_table_allocs: KVec<Pin<KBox<AllocatedBlocks>>>,
    /// Maple tree allocator for virtual address range tracking.
    virt_alloc: Pin<KBox<MapleTreeAlloc<()>>>,
    /// Total number of pages in the virtual address space.
    va_pages: usize,
    /// Prepared PT pages pending PDE installation, keyed by `install_addr`.
    ///
    /// Populated during prepare phase and drained in execute phase. Shared by all
    /// pending maps, preventing races on the same PDE slot.
    pt_pages: RBTree<VramAddress, super::pagetable::map::PreparedPtPage>,
}

impl Vmm {
    /// Create a new [`Vmm`] for the given Page Directory Base address.
    ///
    /// The [`Vmm`] will manage a virtual address space of `va_size` bytes.
    pub(crate) fn new(
        pdb_addr: VramAddress,
        mmu_version: MmuVersion,
        va_size: u64,
    ) -> Result<Self> {
        let page_size: u64 = PAGE_SIZE.into_safe_cast();
        let va_pages: usize = (va_size / page_size).into_safe_cast();
        let virt_alloc = KBox::pin_init(MapleTreeAlloc::<()>::new(), GFP_KERNEL)?;

        Ok(Self {
            pdb_addr,
            pt_walk: PtWalk::new(pdb_addr, mmu_version),
            pt_map: PtMap::new(pdb_addr, mmu_version),
            page_table_allocs: KVec::new(),
            virt_alloc,
            va_pages,
            pt_pages: RBTree::new(),
        })
    }

    /// Allocate a contiguous virtual frame number range.
    fn alloc_vfn_range(&self, num_pages: usize, va_range: Option<Range<u64>>) -> Result<Vfn> {
        let page_size: u64 = PAGE_SIZE.into_safe_cast();

        let start_vfn = match va_range {
            Some(r) => {
                let num_pages_u64: u64 = num_pages.into_safe_cast();
                let size = num_pages_u64.checked_mul(page_size).ok_or(EOVERFLOW)?;
                let range_size = r.end.checked_sub(r.start).ok_or(EOVERFLOW)?;
                if range_size != size {
                    return Err(EINVAL);
                }
                let start_vfn: usize = (r.start / page_size).into_safe_cast();
                let end_vfn: usize = (r.end / page_size).into_safe_cast();
                self.virt_alloc
                    .insert_range(start_vfn..end_vfn, (), GFP_KERNEL)?;
                start_vfn
            }
            None => self
                .virt_alloc
                .alloc_range(num_pages, (), ..self.va_pages, GFP_KERNEL)?,
        };

        Ok(Vfn::new(start_vfn.into_safe_cast()))
    }

    /// Free a virtual frame number range back to the maple tree.
    fn free_vfn(&self, vfn: Vfn) {
        let vfn_index: usize = vfn.raw().into_safe_cast();
        if self.virt_alloc.erase(vfn_index).is_none() {
            kernel::pr_warn!("free_vfn: VFN {} not found in maple tree\n", vfn_index);
        }
    }

    /// Read the [`Pfn`] for a mapped [`Vfn`] if one is mapped.
    pub(super) fn read_mapping(&self, mm: &mut GpuMm<'_>, vfn: Vfn) -> Result<Option<Pfn>> {
        match self.pt_walk.walk_to_pte(mm, vfn)? {
            WalkResult::Mapped { pfn, .. } => Ok(Some(pfn)),
            WalkResult::Unmapped { .. } | WalkResult::PageTableMissing => Ok(None),
        }
    }

    /// Prepare resources for mapping `num_pages` pages.
    ///
    /// Allocates a contiguous VA range, then walks the hierarchy per-VFN to prepare pages
    /// for all missing PDEs. Returns a [`PreparedMapping`] with the VA allocation.
    ///
    /// If `va_range` is not `None`, the VA range is constrained to the given range. Safe
    /// to call outside the fence signalling critical path.
    pub(crate) fn prepare_map(
        &mut self,
        mm: &mut GpuMm<'_>,
        num_pages: usize,
        va_range: Option<Range<u64>>,
    ) -> Result<PreparedMapping> {
        if num_pages == 0 {
            return Err(EINVAL);
        }

        // Allocate contiguous VA range.
        let vfn_start = self.alloc_vfn_range(num_pages, va_range)?;

        if let Err(e) = self.pt_map.prepare_map(
            mm,
            vfn_start,
            num_pages,
            &mut self.page_table_allocs,
            &mut self.pt_pages,
        ) {
            self.free_vfn(vfn_start);
            return Err(e);
        }

        Ok(PreparedMapping {
            vfn_start,
            num_pages,
            _drop_guard: MustExecuteGuard::new(),
        })
    }

    /// Execute a prepared multi-page mapping.
    ///
    /// Installs all prepared PDEs and writes PTEs into the page table, then flushes TLB.
    pub(crate) fn execute_map(
        &mut self,
        mm: &mut GpuMm<'_>,
        prepared: PreparedMapping,
        pfns: &[Pfn],
        writable: bool,
    ) -> Result<MappedRange> {
        if pfns.len() != prepared.num_pages {
            self.free_vfn(prepared.vfn_start);
            return Err(EINVAL);
        }

        let PreparedMapping {
            vfn_start,
            num_pages,
            _drop_guard,
        } = prepared;
        _drop_guard.disarm();

        if let Err(e) = self.pt_map.install_mappings(
            mm,
            &mut self.pt_pages,
            &mut self.page_table_allocs,
            vfn_start,
            pfns,
            writable,
        ) {
            self.free_vfn(vfn_start);
            return Err(e);
        }

        Ok(MappedRange {
            vfn_start,
            num_pages,
            _drop_guard: MustUnmapGuard::new(),
        })
    }

    /// Map pages doing prepare and execute in the same call.
    ///
    /// This is a convenience wrapper for callers outside the fence signalling critical
    /// path (e.g., BAR mappings). For DRM usecases, [`Vmm::prepare_map()`] and
    /// [`Vmm::execute_map()`] will be called separately.
    pub(crate) fn map_pages(
        &mut self,
        mm: &mut GpuMm<'_>,
        pfns: &[Pfn],
        va_range: Option<Range<u64>>,
        writable: bool,
    ) -> Result<MappedRange> {
        if pfns.is_empty() {
            return Err(EINVAL);
        }

        // Check if provided VA range is sufficient (if provided).
        if let Some(ref range) = va_range {
            let required: u64 = pfns
                .len()
                .checked_mul(PAGE_SIZE)
                .ok_or(EOVERFLOW)?
                .into_safe_cast();
            let available = range.end.checked_sub(range.start).ok_or(EINVAL)?;
            if available < required {
                return Err(EINVAL);
            }
        }

        let prepared = self.prepare_map(mm, pfns.len(), va_range)?;
        self.execute_map(mm, prepared, pfns, writable)
    }

    /// Unmap all pages in a [`MappedRange`] with a single TLB flush.
    pub(crate) fn unmap_pages(&mut self, mm: &mut GpuMm<'_>, range: MappedRange) -> Result {
        let result = self
            .pt_map
            .invalidate_ptes(mm, range.vfn_start, range.num_pages);

        // TODO: Internal page table pages (PDE, PTE pages) are still kept around.
        // This is by design as repeated maps/unmaps will be fast. As a future TODO,
        // we can add a reclaimer here to reclaim if VRAM is short. For now, the PT
        // pages are dropped once the `Vmm` is dropped.

        // Free the VA range regardless of PTE invalidation success, so that the VA
        // range is recovered even on failure (PTEs may be stale, but that is better
        // than leaking both PTEs and VA range).
        self.free_vfn(range.vfn_start);

        // Unmap complete, safe to drop `MappedRange`.
        range._drop_guard.disarm();
        result
    }
}
