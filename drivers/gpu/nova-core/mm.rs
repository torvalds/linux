// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Memory management subsystems.

#![cfg_attr(not(CONFIG_NOVA_CORE_SELFTESTS), expect(dead_code))]

/// Implements `From` conversions between a frame-number type and `Bounded<u64, N>`.
///
/// Each MMU version module should invoke this for the specific bit widths used by that version's
/// PTE/PDE bitfield definitions.
macro_rules! impl_frame_number_bounded {
    ($type:ty, $bits:literal) => {
        impl From<Bounded<u64, $bits>> for $type {
            fn from(val: Bounded<u64, $bits>) -> Self {
                Self::new(val.get())
            }
        }

        impl From<$type> for Bounded<u64, $bits> {
            fn from(v: $type) -> Self {
                Bounded::from_expr(v.raw() & ::kernel::bits::genmask_u64(0..=($bits - 1)))
            }
        }
    };
}

/// Implements `From` conversions between [`Pfn`] and `Bounded<u64, N>` for bitfield interop.
macro_rules! impl_pfn_bounded {
    ($bits:literal) => {
        impl_frame_number_bounded!(Pfn, $bits);
    };
}

use core::{
    fmt::LowerHex,
    ops, //
};

use kernel::{
    bitfield,
    fmt,
    gpu::buddy::{
        GpuBuddy,
        GpuBuddyParams, //
    },
    num::Bounded,
    prelude::*,
    ptr::{
        Alignable,
        Alignment, //
    },
    sizes::SZ_4K, //
};

use crate::{
    driver::Bar0,
    gpu::Chipset, //
};

pub(crate) use tlb::Tlb;

pub(crate) mod bar_user;
mod hal;
pub(super) mod pagetable;
mod pramin;
mod regs;
pub(super) mod tlb;
pub(super) mod vmm;

/// GPU Memory Manager - owns all core MM components.
///
/// Provides centralized ownership of memory management resources:
/// - [`GpuBuddy`] allocator for VRAM page table allocation.
/// - [`pramin::Pramin`] for direct VRAM access.
/// - [`Tlb`] manager for translation buffer flush operations.
pub(crate) struct GpuMm<'gpu> {
    buddy: GpuBuddy,
    pramin: pramin::Pramin<'gpu>,
    tlb: Pin<KBox<Tlb<'gpu>>>,
}

impl<'gpu> GpuMm<'gpu> {
    /// Creates the GPU memory manager.
    pub(crate) fn new(
        bar: Bar0<'gpu>,
        chipset: Chipset,
        buddy_params: GpuBuddyParams,
        total_fb_end: VramAddress,
    ) -> Result<Self> {
        // PRAMIN covers all physical VRAM (including GSP-reserved areas
        // above the usable region, e.g. the BAR1 page directory).
        let vram_region = VramAddress::ZERO..total_fb_end;

        Ok(Self {
            buddy: GpuBuddy::new(buddy_params)?,
            pramin: pramin::Pramin::new(bar, chipset, vram_region)?,
            tlb: KBox::pin_init(Tlb::new(bar), GFP_KERNEL)?,
        })
    }

    /// Access the [`GpuBuddy`] allocator.
    pub(crate) fn buddy(&self) -> &GpuBuddy {
        &self.buddy
    }

    /// Access the [`pramin::Pramin`].
    fn pramin_mut(&mut self) -> &mut pramin::Pramin<'gpu> {
        &mut self.pramin
    }

    /// Access the [`Tlb`] manager.
    pub(crate) fn tlb(&self) -> &Tlb<'gpu> {
        self.tlb.as_ref().get_ref()
    }
}

/// Page size in bytes (4 KiB).
pub(crate) const PAGE_SIZE: usize = SZ_4K;

/// Physical VRAM address in GPU video memory.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
#[repr(transparent)]
pub(crate) struct VramAddress(u64);

impl VramAddress {
    /// The zero address.
    pub(crate) const ZERO: Self = Self::from_raw(0);

    /// Creates an address from a raw value.
    pub(crate) const fn from_raw(addr: u64) -> Self {
        Self(addr)
    }

    /// Returns the address as a raw value.
    pub(crate) const fn into_raw(self) -> u64 {
        self.0
    }

    /// Adds `rhs` to this address, returning [`None`] on overflow.
    pub(crate) const fn checked_add(self, rhs: u64) -> Option<Self> {
        match self.into_raw().checked_add(rhs) {
            Some(addr) => Some(Self::from_raw(addr)),
            None => None,
        }
    }
}

impl Alignable for VramAddress {
    fn align_down(self, alignment: Alignment) -> Self {
        Self::from_raw(self.into_raw().align_down(alignment))
    }

    fn align_up(self, alignment: Alignment) -> Option<Self> {
        self.into_raw().align_up(alignment).map(Self::from_raw)
    }
}

impl LowerHex for VramAddress {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        LowerHex::fmt(&self.into_raw(), f)
    }
}

impl fmt::Debug for VramAddress {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_fmt(fmt!("{:#x}", self))
    }
}

impl ops::Add<u64> for VramAddress {
    type Output = Self;

    fn add(self, rhs: u64) -> Self::Output {
        Self::from_raw(self.into_raw() + rhs)
    }
}

impl ops::Sub for VramAddress {
    type Output = u64;

    fn sub(self, rhs: Self) -> Self::Output {
        self.into_raw() - rhs.into_raw()
    }
}

impl From<Pfn> for VramAddress {
    fn from(pfn: Pfn) -> Self {
        Self::from_raw(pfn.raw() << 12)
    }
}

bitfield! {
    /// Virtual address in GPU address space.
    pub(crate) struct VirtualAddress(u64) {
        /// Offset within 4KB page.
        11:0    offset;
        /// Virtual frame number.
        63:12   frame_number => Vfn;
    }
}

impl VirtualAddress {
    /// Create a new virtual address from a raw value.
    #[expect(dead_code)]
    pub(crate) const fn new(addr: u64) -> Self {
        Self::from_raw(addr)
    }
}

impl From<Vfn> for VirtualAddress {
    fn from(vfn: Vfn) -> Self {
        Self::zeroed().with_frame_number(vfn)
    }
}

/// Physical Frame Number.
///
/// Represents a physical page in VRAM.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(crate) struct Pfn(u64);

impl Pfn {
    /// Create a new PFN from a frame number.
    pub(crate) const fn new(frame_number: u64) -> Self {
        Self(frame_number)
    }

    /// Get the raw frame number.
    pub(crate) const fn raw(self) -> u64 {
        self.0
    }
}

impl From<VramAddress> for Pfn {
    fn from(addr: VramAddress) -> Self {
        Self::new(addr.into_raw() >> 12)
    }
}

impl From<u64> for Pfn {
    fn from(val: u64) -> Self {
        Self(val)
    }
}

impl From<Pfn> for u64 {
    fn from(pfn: Pfn) -> Self {
        pfn.0
    }
}

impl_pfn_bounded!(52);

/// Virtual Frame Number.
///
/// Represents a virtual page in GPU address space.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(crate) struct Vfn(u64);

impl Vfn {
    /// Create a new VFN from a frame number.
    pub(crate) const fn new(frame_number: u64) -> Self {
        Self(frame_number)
    }

    /// Get the raw frame number.
    pub(crate) const fn raw(self) -> u64 {
        self.0
    }
}

impl From<VirtualAddress> for Vfn {
    fn from(addr: VirtualAddress) -> Self {
        addr.frame_number()
    }
}

impl From<u64> for Vfn {
    fn from(val: u64) -> Self {
        Self(val)
    }
}

impl From<Vfn> for u64 {
    fn from(vfn: Vfn) -> Self {
        vfn.0
    }
}

impl_frame_number_bounded!(Vfn, 52);

#[cfg(CONFIG_NOVA_CORE_SELFTESTS)]
pub(crate) mod selftest {
    use core::ops::Range;

    use kernel::{
        device,
        sizes::SizeConstants, //
    };

    use super::*;

    /// Run MM subsystem self-tests during probe.
    pub(crate) fn run(
        dev: &device::Device<device::Bound>,
        mm: &mut GpuMm<'_>,
        usable_fb_regions: &[Range<u64>],
        bar_user: &bar_user::BarUser<'_>,
        bar1_pdb: u64,
        chipset: Chipset,
    ) -> Result {
        // VRAM span the self-tests are free to overwrite, from the chosen test base.
        const SELFTEST_SPAN: u64 = u64::SZ_64M;

        let base = usable_fb_regions.iter().find_map(|region| {
            // Tests rely on this being 8 byte aligned for checking misalignment handling.
            let base = region.start.align_up(Alignment::new::<8>())?;
            (base.checked_add(SELFTEST_SPAN)? <= region.end).then_some(base)
        });
        let Some(base) = base else {
            dev_warn!(
                dev,
                "PRAMIN: skipping self-tests, no usable VRAM region of {:#x} bytes\n",
                SELFTEST_SPAN
            );
            return Ok(());
        };

        pramin::selftest::run(dev, mm.pramin_mut(), VramAddress::from_raw(base))?;
        bar_user::run_self_test(dev, mm, bar_user, bar1_pdb, chipset)
    }
}
