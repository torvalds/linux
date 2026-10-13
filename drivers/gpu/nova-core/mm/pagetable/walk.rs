// SPDX-License-Identifier: GPL-2.0

//! Page table walker implementation for NVIDIA GPUs.
//!
//! This module provides page table walking functionality for MMU v2 and v3.
//! The walker traverses the page table hierarchy to resolve virtual addresses
//! to physical addresses or to find PTE locations.
//!
//! # Page Table Hierarchy
//!
//! ## MMU v2 (Turing/Ampere/Ada) - 5 levels
//!
//! ```text
//!     +-------+     +-------+     +-------+     +---------+     +-------+
//!     | PDB   |---->|  L1   |---->|  L2   |---->| L3 Dual |---->|  L4   |
//!     | (L0)  |     |       |     |       |     | PDE     |     | (PTE) |
//!     +-------+     +-------+     +-------+     +---------+     +-------+
//!       64-bit        64-bit        64-bit        128-bit         64-bit
//!        PDE           PDE           PDE        (big+small)        PTE
//! ```
//!
//! ## MMU v3 (Hopper+) - 6 levels
//!
//! ```text
//!     +-------+     +-------+     +-------+     +-------+     +---------+     +-------+
//!     | PDB   |---->|  L1   |---->|  L2   |---->|  L3   |---->| L4 Dual |---->|  L5   |
//!     | (L0)  |     |       |     |       |     |       |     | PDE     |     | (PTE) |
//!     +-------+     +-------+     +-------+     +-------+     +---------+     +-------+
//!       64-bit        64-bit        64-bit        64-bit        128-bit         64-bit
//!        PDE           PDE           PDE           PDE        (big+small)        PTE
//! ```
//!
//! # Result of a page table walk
//!
//! The walker returns a [`WalkResult`] indicating the outcome.

use core::marker::PhantomData;

use kernel::prelude::*;

use super::{
    DualPdeOps,
    MmuConfig,
    MmuV2,
    MmuV3,
    MmuVersion,
    PageTableLevel,
    PdeOps,
    PteOps, //
};
use crate::{
    mm::{
        pramin,
        GpuMm,
        Pfn,
        Vfn,
        VirtualAddress,
        VramAddress, //
    },
    num::{
        IntoSafeCast, //
    },
};

/// Result of walking to a PTE.
#[derive(Debug, Clone, Copy)]
pub(in crate::mm) enum WalkResult {
    /// Intermediate page tables are missing (only returned in lookup mode).
    PageTableMissing,
    /// PTE exists but is invalid (page not mapped).
    Unmapped { pte_addr: VramAddress },
    /// PTE exists and is valid (page is mapped).
    Mapped { pte_addr: VramAddress, pfn: Pfn },
}

/// Result of walking PDE levels only.
///
/// Returned by [`PtWalkInner::walk_pde_levels()`] to indicate whether all PDE
/// levels resolved or a PDE is missing.
#[derive(Debug, Clone, Copy)]
pub(in crate::mm) enum WalkPdeResult {
    /// All PDE levels resolved -- returns PTE page table address.
    Complete {
        /// VRAM address of the PTE-level page table.
        pte_table: VramAddress,
    },
    /// A PDE is missing and no prepared page was provided by the closure.
    Missing {
        /// PDE slot address in the parent page table (where to install).
        install_addr: VramAddress,
        /// The page table level that is missing.
        level: PageTableLevel,
    },
}

/// Page table walker.
pub(in crate::mm) struct PtWalkInner<M: MmuConfig> {
    pdb_addr: VramAddress,
    _phantom: PhantomData<M>,
}

impl<M: MmuConfig> PtWalkInner<M> {
    /// Calculate the VRAM address of an entry within a page table.
    fn entry_addr(table: VramAddress, level: PageTableLevel, index: u64) -> VramAddress {
        let entry_size: u64 = M::entry_size(level).into_safe_cast();
        table + index * entry_size
    }

    /// Create a new page table walker.
    pub(super) fn new(pdb_addr: VramAddress) -> Self {
        Self {
            pdb_addr,
            _phantom: PhantomData,
        }
    }

    /// Walk PDE levels with closure-based resolution for missing PDEs.
    ///
    /// Traverses all PDE levels for the MMU version. At each level, reads the PDE.
    /// If valid, extracts the child table address and continues. If missing, calls
    /// `resolve_prepared(install_addr)` to resolve the missing PDE.
    pub(super) fn walk_pde_levels(
        &self,
        pramin: &mut pramin::Pramin<'_>,
        vfn: Vfn,
        resolve_prepared: impl Fn(VramAddress) -> Option<VramAddress>,
    ) -> Result<WalkPdeResult> {
        let va = VirtualAddress::from(vfn);
        let mut cur_table = self.pdb_addr;

        for &level in M::PDE_LEVELS {
            let idx = M::level_index(va, level.as_index());
            let install_addr = Self::entry_addr(cur_table, level, idx);

            if level == M::DUAL_PDE_LEVEL {
                // 128-bit dual PDE with big+small page table pointers.
                let dpde = M::DualPde::read(pramin, install_addr)?;
                if dpde.has_small() {
                    cur_table = dpde.small_vram_address();
                    continue;
                }
            } else {
                // Regular 64-bit PDE. Use `is_valid_vram()` because
                // `table_vram_address()` only reads the VRAM frame-number
                // bitfield; system-memory PDEs store the address in a
                // different (wider) field and would be silently truncated.
                let pde = M::Pde::read(pramin, install_addr)?;
                if pde.is_valid_vram() {
                    cur_table = pde.table_vram_address();
                    continue;
                }
            }

            // PDE missing in HW. Ask caller for resolution.
            if let Some(prepared_addr) = resolve_prepared(install_addr) {
                cur_table = prepared_addr;
                continue;
            }

            return Ok(WalkPdeResult::Missing {
                install_addr,
                level,
            });
        }

        Ok(WalkPdeResult::Complete {
            pte_table: cur_table,
        })
    }

    /// Walk to PTE for lookup only (no allocation).
    ///
    /// Returns [`WalkResult::PageTableMissing`] if intermediate tables don't exist.
    pub(super) fn walk_to_pte_lookup(&self, mm: &mut GpuMm<'_>, vfn: Vfn) -> Result<WalkResult> {
        self.walk_to_pte_lookup_with_window(mm.pramin_mut(), vfn)
    }

    /// Walk to PTE using a caller-provided PRAMIN manager (lookup only).
    pub(super) fn walk_to_pte_lookup_with_window(
        &self,
        pramin: &mut pramin::Pramin<'_>,
        vfn: Vfn,
    ) -> Result<WalkResult> {
        match self.walk_pde_levels(pramin, vfn, |_| None)? {
            WalkPdeResult::Complete { pte_table } => {
                Self::read_pte_at_level(pramin, vfn, pte_table)
            }
            WalkPdeResult::Missing { .. } => Ok(WalkResult::PageTableMissing),
        }
    }

    /// Read the PTE at the PTE level given the PTE table address.
    fn read_pte_at_level(
        pramin: &mut pramin::Pramin<'_>,
        vfn: Vfn,
        pte_table: VramAddress,
    ) -> Result<WalkResult> {
        let va = VirtualAddress::from(vfn);
        let pte_level = M::PTE_LEVEL;
        let pte_idx = M::level_index(va, pte_level.as_index());
        let pte_addr = Self::entry_addr(pte_table, pte_level, pte_idx);
        let pte = M::Pte::read(pramin, pte_addr)?;

        if pte.is_valid() {
            return Ok(WalkResult::Mapped {
                pte_addr,
                pfn: pte.frame_number(),
            });
        }
        Ok(WalkResult::Unmapped { pte_addr })
    }
}

macro_rules! pt_walk_dispatch {
    ($self:expr, $method:ident ( $($arg:expr),* $(,)? )) => {
        match $self {
            PtWalk::V2(inner) => inner.$method($($arg),*),
            PtWalk::V3(inner) => inner.$method($($arg),*),
        }
    };
}

/// Page table walker dispatch.
pub(in crate::mm) enum PtWalk {
    /// MMU v2 (Turing/Ampere/Ada).
    V2(PtWalkInner<MmuV2>),
    /// MMU v3 (Hopper+).
    V3(PtWalkInner<MmuV3>),
}

impl PtWalk {
    /// Create a new page table walker for the given MMU version.
    pub(in crate::mm) fn new(pdb_addr: VramAddress, version: MmuVersion) -> Self {
        match version {
            MmuVersion::V2 => Self::V2(PtWalkInner::<MmuV2>::new(pdb_addr)),
            MmuVersion::V3 => Self::V3(PtWalkInner::<MmuV3>::new(pdb_addr)),
        }
    }

    /// Walk to PTE for lookup.
    pub(in crate::mm) fn walk_to_pte(&self, mm: &mut GpuMm<'_>, vfn: Vfn) -> Result<WalkResult> {
        pt_walk_dispatch!(self, walk_to_pte_lookup(mm, vfn))
    }
}
