// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Blackwell GB20x framebuffer HAL.

use kernel::{
    io::{
        register::{
            RegisterBase,
            WithBase, //
        },
        Io, //
    },
    num::Bounded,
    prelude::*,
    sizes::SizeConstants, //
};

use crate::{
    driver::Bar0,
    fb::hal::FbHal,
    regs, //
};

struct Gb202;

impl RegisterBase<regs::Fbhub0Base> for Gb202 {
    const BASE: usize = 0x008a_0000;
}

fn read_sysmem_flush_page_gb202(bar: Bar0<'_>) -> u64 {
    let lo = u64::from(
        bar.read(regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO::of::<Gb202>())
            .adr(),
    );
    let hi = u64::from(
        bar.read(regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI::of::<Gb202>())
            .adr(),
    );

    lo | (hi << 32)
}

/// Write the sysmem flush page address through the GB20x FBHUB0 registers.
fn write_sysmem_flush_page_gb202(bar: Bar0<'_>, addr: Bounded<u64, 52>) {
    // Write HI first. The hardware will trigger the flush on the LO write.
    bar.write(
        regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI::of::<Gb202>(),
        regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI::zeroed()
            .with_adr(addr.shr::<32, 20>().cast::<u32>()),
    );
    bar.write(
        regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO::of::<Gb202>(),
        // CAST: lower 32 bits. Hardware ignores bits 7:0.
        regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO::zeroed().with_adr(*addr as u32),
    );
}

impl FbHal for Gb202 {
    fn read_sysmem_flush_page(&self, bar: Bar0<'_>) -> u64 {
        read_sysmem_flush_page_gb202(bar)
    }

    fn write_sysmem_flush_page(&self, bar: Bar0<'_>, addr: u64) -> Result {
        let addr = Bounded::<u64, 52>::try_new(addr).ok_or(EINVAL)?;

        write_sysmem_flush_page_gb202(bar, addr);

        Ok(())
    }

    fn supports_display(&self, bar: Bar0<'_>) -> bool {
        super::ga100::display_enabled_ga100(bar)
    }

    fn vidmem_size(&self, bar: Bar0<'_>) -> u64 {
        super::ga102::vidmem_size_ga102(bar)
    }

    fn pmu_reserved_size(&self) -> u32 {
        super::gb100::pmu_reserved_size_gb100()
    }

    fn non_wpr_heap_size(&self) -> u32 {
        // Non-WPR heap for GB20x (see Open RM: kgspGetNonWprHeapSize, GB202+).
        u32::SZ_2M + u32::SZ_128K
    }

    fn frts_size(&self) -> u64 {
        super::tu102::frts_size_tu102()
    }
}

const GB202: Gb202 = Gb202;
pub(super) const GB202_HAL: &dyn FbHal = &GB202;
