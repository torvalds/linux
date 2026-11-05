// SPDX-License-Identifier: GPL-2.0

//! Common page table types shared between MMU v2 and v3.
//!
//! This module provides foundational types used by both MMU versions:
//! - Page table level hierarchy
//! - Memory aperture types for PDEs and PTEs

#![expect(dead_code)]

pub(super) mod map;
pub(super) mod ver2;
pub(super) mod ver3;
pub(super) mod walk;

use kernel::{
    io::Io,
    num::Bounded,
    prelude::*, //
};

use crate::{
    gpu::Architecture,
    mm::{
        pramin,
        Pfn,
        VirtualAddress,
        VramAddress, //
    },
};

/// Extracts the page table index at a given level from a virtual address.
pub(super) trait VaLevelIndex {
    /// Return the page table index at `level` for this virtual address.
    fn level_index(&self, level: u64) -> u64;
}

/// MMU version enumeration.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum MmuVersion {
    /// MMU v2 for Turing/Ampere/Ada.
    V2,
    /// MMU v3 for Hopper and later.
    V3,
}

impl From<Architecture> for MmuVersion {
    fn from(arch: Architecture) -> Self {
        match arch {
            Architecture::Turing | Architecture::Ampere | Architecture::Ada => Self::V2,
            Architecture::Hopper | Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => {
                Self::V3
            }
        }
    }
}

/// Page Table Level hierarchy for MMU v2/v3.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum PageTableLevel {
    /// Level 0 - Page Directory Base (root).
    Pdb,
    /// Level 1 - Intermediate page directory.
    L1,
    /// Level 2 - Intermediate page directory.
    L2,
    /// Level 3 - Intermediate page directory or dual PDE (version-dependent).
    L3,
    /// Level 4 - PTE level for v2, intermediate page directory for v3.
    L4,
    /// Level 5 - PTE level used for MMU v3 only.
    L5,
}

impl PageTableLevel {
    /// Number of entries per page table (512 for 4KB pages).
    pub(super) const ENTRIES_PER_TABLE: usize = 512;

    /// Get the next level in the hierarchy.
    pub(super) const fn next(&self) -> Option<PageTableLevel> {
        match self {
            Self::Pdb => Some(Self::L1),
            Self::L1 => Some(Self::L2),
            Self::L2 => Some(Self::L3),
            Self::L3 => Some(Self::L4),
            Self::L4 => Some(Self::L5),
            Self::L5 => None,
        }
    }

    /// Convert level to index.
    pub(super) const fn as_index(&self) -> u64 {
        match self {
            Self::Pdb => 0,
            Self::L1 => 1,
            Self::L2 => 2,
            Self::L3 => 3,
            Self::L4 => 4,
            Self::L5 => 5,
        }
    }
}

// Trait abstractions for page table operations.

/// Operations on Page Table Entries (`PTE`s).
pub(super) trait PteOps: Copy + core::fmt::Debug + Into<u64> {
    /// Create a `PTE` from a raw `u64` value.
    fn from_raw(val: u64) -> Self;

    /// Create an invalid `PTE`.
    fn invalid() -> Self;

    /// Create a valid `PTE` for the given memory aperture.
    fn new(aperture: AperturePte, pfn: Pfn, writable: bool) -> Self;

    /// Check if this `PTE` is valid.
    fn is_valid(&self) -> bool;

    /// Get the physical frame number.
    fn frame_number(&self) -> Pfn;

    /// Read a `PTE` from VRAM.
    fn read(pramin: &mut pramin::Pramin<'_>, addr: VramAddress) -> Result<Self> {
        let val = pramin.window_at::<u64>(addr)?.view().read_val();
        Ok(Self::from_raw(val))
    }

    /// Write this `PTE` to VRAM.
    fn write(&self, pramin: &mut pramin::Pramin<'_>, addr: VramAddress) -> Result {
        pramin
            .window_at::<u64>(addr)?
            .view()
            .write_val((*self).into());
        Ok(())
    }
}

/// Operations on Page Directory Entries (`PDE`s).
pub(super) trait PdeOps: Copy + core::fmt::Debug + Into<u64> {
    /// Create a `PDE` from a raw `u64` value.
    fn from_raw(val: u64) -> Self;

    /// Create a valid `PDE` pointing to a page table in the given aperture.
    fn new(aperture: AperturePde, table_pfn: Pfn) -> Self;

    /// Create an invalid `PDE`.
    fn invalid() -> Self;

    /// Check if this `PDE` is valid.
    fn is_valid(&self) -> bool;

    /// Get the memory aperture of this `PDE`.
    fn aperture(&self) -> AperturePde;

    /// Get the VRAM address of the page table.
    fn table_vram_address(&self) -> VramAddress;

    /// Read a `PDE` from VRAM.
    fn read(pramin: &mut pramin::Pramin<'_>, addr: VramAddress) -> Result<Self> {
        let val = pramin.window_at::<u64>(addr)?.view().read_val();
        Ok(Self::from_raw(val))
    }

    /// Write this `PDE` to VRAM.
    fn write(&self, pramin: &mut pramin::Pramin<'_>, addr: VramAddress) -> Result {
        pramin
            .window_at::<u64>(addr)?
            .view()
            .write_val((*self).into());
        Ok(())
    }

    /// Check if this `PDE` is valid and points to video memory.
    fn is_valid_vram(&self) -> bool {
        self.is_valid() && self.aperture() == AperturePde::VideoMemory
    }
}

/// Operations on Dual Page Directory Entries (128-bit `DualPde`s).
pub(super) trait DualPdeOps: Copy + core::fmt::Debug {
    /// Create a `DualPde` from raw 128-bit value (two `u64`s).
    fn from_raw(big: u64, small: u64) -> Self;

    /// Create a `DualPde` with only the small page table pointer set.
    fn new_small(table_pfn: Pfn) -> Self;

    /// Check if the small page table pointer is valid.
    fn has_small(&self) -> bool;

    /// Get the small page table VRAM address.
    fn small_vram_address(&self) -> VramAddress;

    /// Get the raw `u64` value of the big PDE.
    fn big_raw_u64(&self) -> u64;

    /// Get the raw `u64` value of the small PDE.
    fn small_raw_u64(&self) -> u64;

    /// Read a dual PDE (128-bit) from VRAM.
    fn read(pramin: &mut pramin::Pramin<'_>, addr: VramAddress) -> Result<Self> {
        let lo = pramin.window_at::<u64>(addr)?.view().read_val();
        let hi = pramin.window_at::<u64>(addr + 8)?.view().read_val();
        Ok(Self::from_raw(lo, hi))
    }

    /// Write this dual PDE (128-bit) to VRAM.
    fn write(&self, pramin: &mut pramin::Pramin<'_>, addr: VramAddress) -> Result {
        pramin
            .window_at::<u64>(addr)?
            .view()
            .write_val(self.big_raw_u64());
        pramin
            .window_at::<u64>(addr + 8)?
            .view()
            .write_val(self.small_raw_u64());
        Ok(())
    }
}

/// MMU configuration trait -- encodes version-specific constants and types.
pub(super) trait MmuConfig: 'static {
    /// Page Table Entry type.
    type Pte: PteOps;
    /// Page Directory Entry type.
    type Pde: PdeOps;
    /// Dual Page Directory Entry type (128-bit).
    type DualPde: DualPdeOps;

    /// PDE levels (excluding PTE level) for page table walking.
    const PDE_LEVELS: &'static [PageTableLevel];
    /// PTE level for this MMU version.
    const PTE_LEVEL: PageTableLevel;
    /// Dual PDE level (128-bit entries) for this MMU version.
    const DUAL_PDE_LEVEL: PageTableLevel;

    /// Get the number of entries per page table page for a given level.
    fn entries_per_page(level: PageTableLevel) -> usize;

    /// Extract the page table index at `level` from `va`.
    fn level_index(va: VirtualAddress, level: u64) -> u64;

    /// Get the entry size in bytes for a given level.
    fn entry_size(level: PageTableLevel) -> usize {
        if level == Self::DUAL_PDE_LEVEL {
            16 // 128-bit dual PDE
        } else {
            8 // 64-bit PDE/PTE
        }
    }

    /// Compute upper bound on page table pages needed for `num_virt_pages`.
    ///
    /// Walks from PTE level up through PDE levels, accumulating the tree.
    fn pt_pages_upper_bound(num_virt_pages: usize) -> usize {
        let mut total = 0;

        // PTE pages at the leaf level.
        let pte_epp = Self::entries_per_page(Self::PTE_LEVEL);
        let mut pages_at_level = num_virt_pages.div_ceil(pte_epp);
        total += pages_at_level;

        // Walk PDE levels bottom-up (reverse of PDE_LEVELS).
        for &level in Self::PDE_LEVELS.iter().rev() {
            let epp = Self::entries_per_page(level);

            // How many pages at this level do we need to point to
            // the previous pages_at_level?
            pages_at_level = pages_at_level.div_ceil(epp);
            total += pages_at_level;
        }

        total
    }
}

/// Marker struct for MMU v2 (Turing/Ampere/Ada).
pub(super) struct MmuV2;

impl MmuConfig for MmuV2 {
    type Pte = ver2::Pte;
    type Pde = ver2::Pde;
    type DualPde = ver2::DualPde;

    const PDE_LEVELS: &'static [PageTableLevel] = ver2::PDE_LEVELS;
    const PTE_LEVEL: PageTableLevel = ver2::PTE_LEVEL;
    const DUAL_PDE_LEVEL: PageTableLevel = ver2::DUAL_PDE_LEVEL;

    fn entries_per_page(level: PageTableLevel) -> usize {
        // TODO: Calculate these values from the bitfield dynamically
        // instead of hardcoding them.
        match level {
            PageTableLevel::Pdb => 4,  // PD3 root: bits [48:47] = 2 bits
            PageTableLevel::L3 => 256, // PD0 dual: bits [28:21] = 8 bits
            _ => 512,                  // PD2, PD1, PT: 9 bits each
        }
    }

    fn level_index(va: VirtualAddress, level: u64) -> u64 {
        ver2::VirtualAddressV2::new(va).level_index(level)
    }
}

/// Marker struct for MMU v3 (Hopper and later).
pub(super) struct MmuV3;

impl MmuConfig for MmuV3 {
    type Pte = ver3::Pte;
    type Pde = ver3::Pde;
    type DualPde = ver3::DualPde;

    const PDE_LEVELS: &'static [PageTableLevel] = ver3::PDE_LEVELS;
    const PTE_LEVEL: PageTableLevel = ver3::PTE_LEVEL;
    const DUAL_PDE_LEVEL: PageTableLevel = ver3::DUAL_PDE_LEVEL;

    fn entries_per_page(level: PageTableLevel) -> usize {
        match level {
            PageTableLevel::Pdb => 2,  // PDE4 root: bit [56] = 1 bit, 2 entries
            PageTableLevel::L4 => 256, // PDE0 dual: bits [28:21] = 8 bits
            _ => 512,                  // PDE3, PDE2, PDE1, PT: 9 bits each
        }
    }

    fn level_index(va: VirtualAddress, level: u64) -> u64 {
        ver3::VirtualAddressV3::new(va).level_index(level)
    }
}

/// Memory aperture for Page Table Entries (`PTE`s).
///
/// Determines which memory region the `PTE` points to.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(super) enum AperturePte {
    /// Local video memory (VRAM).
    #[default]
    VideoMemory = 0,
    /// Peer GPU's video memory.
    PeerMemory = 1,
    /// System memory with cache coherence.
    SystemCoherent = 2,
    /// System memory without cache coherence.
    SystemNonCoherent = 3,
}

// TODO[FPRI]: Replace with `#[derive(FromPrimitive)]` when available.
impl From<Bounded<u64, 2>> for AperturePte {
    fn from(val: Bounded<u64, 2>) -> Self {
        match *val {
            0 => Self::VideoMemory,
            1 => Self::PeerMemory,
            2 => Self::SystemCoherent,
            3 => Self::SystemNonCoherent,
            _ => Self::VideoMemory,
        }
    }
}

// TODO[FPRI]: Replace with `#[derive(ToPrimitive)]` when available.
impl From<AperturePte> for Bounded<u64, 2> {
    fn from(val: AperturePte) -> Self {
        Bounded::from_expr(val as u64 & 0x3)
    }
}

/// Memory aperture for Page Directory Entries (`PDE`s).
///
/// Note: For `PDE`s, `Invalid` (0) means the entry is not valid.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(super) enum AperturePde {
    /// Invalid/unused entry.
    #[default]
    Invalid = 0,
    /// Page table is in video memory.
    VideoMemory = 1,
    /// Page table is in system memory with coherence.
    SystemCoherent = 2,
    /// Page table is in system memory without coherence.
    SystemNonCoherent = 3,
}

// TODO[FPRI]: Replace with `#[derive(FromPrimitive)]` when available.
impl From<Bounded<u64, 2>> for AperturePde {
    fn from(val: Bounded<u64, 2>) -> Self {
        match *val {
            1 => Self::VideoMemory,
            2 => Self::SystemCoherent,
            3 => Self::SystemNonCoherent,
            _ => Self::Invalid,
        }
    }
}

// TODO[FPRI]: Replace with `#[derive(ToPrimitive)]` when available.
impl From<AperturePde> for Bounded<u64, 2> {
    fn from(val: AperturePde) -> Self {
        Bounded::from_expr(val as u64 & 0x3)
    }
}

/// Check if the PDB has valid, VRAM-backed page tables.
#[cfg(CONFIG_NOVA_CORE_SELFTESTS)]
fn check_pdb_inner<M: MmuConfig>(pramin: &mut pramin::Pramin<'_>, pdb_addr: VramAddress) -> Result {
    let raw = pramin.window_at::<u64>(pdb_addr)?.view().read_val();

    if !M::Pde::from_raw(raw).is_valid_vram() {
        return Err(ENOENT);
    }
    Ok(())
}

/// Check if the PDB has valid, VRAM-backed page tables, dispatching by MMU version.
#[cfg(CONFIG_NOVA_CORE_SELFTESTS)]
pub(super) fn check_pdb_valid(
    pramin: &mut pramin::Pramin<'_>,
    pdb_addr: VramAddress,
    chipset: crate::gpu::Chipset,
) -> Result {
    match MmuVersion::from(chipset.arch()) {
        MmuVersion::V2 => check_pdb_inner::<MmuV2>(pramin, pdb_addr),
        MmuVersion::V3 => check_pdb_inner::<MmuV3>(pramin, pdb_addr),
    }
}
