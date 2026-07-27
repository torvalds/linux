// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::{
    io::Io,
    num::Bounded,
    prelude::*,
    sizes::SizeConstants, //
};

use crate::{
    driver::Bar0,
    fb::{
        hal::FbHal,
        regs, //
    },
};

struct Gh100;

fn read_sysmem_flush_page_gh100(bar: Bar0<'_>) -> u64 {
    let lo = u64::from(bar.read(regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO).adr());
    let hi = u64::from(bar.read(regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI).adr());

    (hi << 32) | lo
}

/// Write the sysmem flush page address through the Hopper FBHUB registers.
fn write_sysmem_flush_page_gh100(bar: Bar0<'_>, addr: Bounded<u64, 52>) {
    // Write HI first. The hardware will trigger the flush on the LO write.
    bar.write_reg(
        regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI::zeroed()
            .with_adr(addr.shr::<32, 20>().cast::<u32>()),
    );
    bar.write_reg(
        // CAST: lower 32 bits. Hardware ignores bits 7:0.
        regs::NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO::zeroed().with_adr(*addr as u32),
    );
}

impl FbHal for Gh100 {
    fn read_sysmem_flush_page(&self, bar: Bar0<'_>) -> u64 {
        read_sysmem_flush_page_gh100(bar)
    }

    fn write_sysmem_flush_page(&self, bar: Bar0<'_>, addr: u64) -> Result {
        let addr = Bounded::<u64, 52>::try_new(addr).ok_or(EINVAL)?;

        write_sysmem_flush_page_gh100(bar, addr);

        Ok(())
    }

    fn supports_display(&self, bar: Bar0<'_>) -> bool {
        super::ga100::display_enabled_ga100(bar)
    }

    fn vidmem_size(&self, bar: Bar0<'_>) -> u64 {
        super::ga102::vidmem_size_ga102(bar)
    }

    fn pmu_reserved_size(&self) -> u32 {
        super::tu102::pmu_reserved_size_tu102()
    }

    fn non_wpr_heap_size(&self) -> u32 {
        // Non-WPR heap for Hopper (see Open RM: kgspCalculateFbLayout_GH100).
        u32::SZ_2M
    }

    fn frts_size(&self) -> u64 {
        super::tu102::frts_size_tu102()
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn FbHal = &GH100;
