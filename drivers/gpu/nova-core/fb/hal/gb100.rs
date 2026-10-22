// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Blackwell GB10x framebuffer HAL.

use kernel::{
    io::{
        io_project,
        register,
        Io,
        Mmio, //
    },
    num::Bounded,
    prelude::*,
    ptr::{
        const_align_up,
        Alignment, //
    },
    sizes::*, //
};

use crate::{
    driver::{
        Bar0,
        NovaRegisters, //
    },
    fb::{
        hal::FbHal,
        regs, //
    },
    num::usize_into_u32,
};

struct Gb100;

register! {
    base: NovaRegisters;

    HSHUB0: regs::Hshub0Registers @ 0x0087_0000;
}

#[inline]
fn hshub0(bar: Bar0<'_>) -> Mmio<'_, regs::Hshub0Registers> {
    io_project!(bar, build: HSHUB0)
}

fn read_sysmem_flush_page_gb100(hshub0: Mmio<'_, regs::Hshub0Registers>) -> u64 {
    let lo = u64::from(
        hshub0
            .read(regs::NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_LO)
            .adr(),
    );
    let hi = u64::from(
        hshub0
            .read(regs::NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_HI)
            .adr(),
    );

    lo | (hi << 32)
}

/// Write the sysmem flush page address through the GB10x HSHUB0 registers.
///
/// Both the primary and EG (egress) register pairs must be programmed to the same address,
/// as required by hardware.
fn write_sysmem_flush_page_gb100(hshub0: Mmio<'_, regs::Hshub0Registers>, addr: Bounded<u64, 52>) {
    // CAST: lower 32 bits. Hardware ignores bits 7:0.
    let addr_lo = *addr as u32;
    let addr_hi = addr.shr::<32, 20>().cast::<u32>();

    // Write HI first. The hardware will trigger the flush on the LO write.

    // Primary HSHUB pair.
    hshub0.write_reg(regs::NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_HI::zeroed().with_adr(addr_hi));
    hshub0.write_reg(regs::NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_LO::zeroed().with_adr(addr_lo));

    // EG (egress) pair -- must match the primary pair.
    hshub0.write_reg(regs::NV_PFB_HSHUB_EG_PCIE_FLUSH_SYSMEM_ADDR_HI::zeroed().with_adr(addr_hi));
    hshub0.write_reg(regs::NV_PFB_HSHUB_EG_PCIE_FLUSH_SYSMEM_ADDR_LO::zeroed().with_adr(addr_lo));
}

// This PMU reservation size is r570-specific.
pub(super) const fn pmu_reserved_size_gb100() -> u32 {
    usize_into_u32::<{ const_align_up(SZ_8M + SZ_16M + SZ_4K, Alignment::new::<SZ_128K>()).unwrap() }>(
    )
}

impl FbHal for Gb100 {
    fn read_sysmem_flush_page(&self, bar: Bar0<'_>) -> u64 {
        read_sysmem_flush_page_gb100(hshub0(bar))
    }

    fn write_sysmem_flush_page(&self, bar: Bar0<'_>, addr: u64) -> Result {
        let addr = Bounded::<u64, 52>::try_new(addr).ok_or(EINVAL)?;

        write_sysmem_flush_page_gb100(hshub0(bar), addr);

        Ok(())
    }

    fn supports_display(&self, bar: Bar0<'_>) -> bool {
        super::ga100::display_enabled_ga100(bar)
    }

    fn vidmem_size(&self, bar: Bar0<'_>) -> u64 {
        super::ga102::vidmem_size_ga102(bar)
    }

    fn pmu_reserved_size(&self) -> u32 {
        pmu_reserved_size_gb100()
    }

    fn non_wpr_heap_size(&self) -> u64 {
        // Non-WPR heap for GB10x (see Open RM: kgspGetNonWprHeapSize, GB100/GB102).
        u64::SZ_2M
    }

    fn frts_size(&self) -> u64 {
        super::tu102::frts_size_tu102()
    }
}

const GB100: Gb100 = Gb100;
pub(super) const GB100_HAL: &dyn FbHal = &GB100;
