// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::{
    io::Io,
    prelude::*,
    sizes::*, //
};

use crate::{
    driver::Bar0,
    fb::hal::FbHal,
    regs, //
};

/// Shift applied to the sysmem address before it is written into `NV_PFB_NISO_FLUSH_SYSMEM_ADDR`,
/// to be used by HALs.
pub(super) const FLUSH_SYSMEM_ADDR_SHIFT: u32 = 8;

pub(super) fn read_sysmem_flush_page_gm107(bar: Bar0<'_>) -> u64 {
    u64::from(bar.read(regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR).adr_39_08()) << FLUSH_SYSMEM_ADDR_SHIFT
}

pub(super) fn write_sysmem_flush_page_gm107(bar: Bar0<'_>, addr: u64) -> Result {
    // Check that the address doesn't overflow the receiving 32-bit register.
    u32::try_from(addr >> FLUSH_SYSMEM_ADDR_SHIFT)
        .map_err(|_| EINVAL)
        .map(|addr| {
            bar.write_reg(regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR::zeroed().with_adr_39_08(addr))
        })
}

pub(super) fn display_enabled_gm107(bar: Bar0<'_>) -> bool {
    !bar.read(regs::gm107::NV_FUSE_STATUS_OPT_DISPLAY)
        .display_disabled()
}

pub(super) fn vidmem_size_gp102(bar: Bar0<'_>) -> u64 {
    bar.read(regs::NV_PFB_PRI_MMU_LOCAL_MEMORY_RANGE)
        .usable_fb_size()
}

pub(super) const fn pmu_reserved_size_tu102() -> u32 {
    0
}

pub(super) const fn non_wpr_heap_size_tu102() -> u32 {
    u32::SZ_1M
}

pub(super) const fn frts_size_tu102() -> u64 {
    u64::SZ_1M
}

struct Tu102;

impl FbHal for Tu102 {
    fn read_sysmem_flush_page(&self, bar: Bar0<'_>) -> u64 {
        read_sysmem_flush_page_gm107(bar)
    }

    fn write_sysmem_flush_page(&self, bar: Bar0<'_>, addr: u64) -> Result {
        write_sysmem_flush_page_gm107(bar, addr)
    }

    fn supports_display(&self, bar: Bar0<'_>) -> bool {
        display_enabled_gm107(bar)
    }

    fn vidmem_size(&self, bar: Bar0<'_>) -> u64 {
        vidmem_size_gp102(bar)
    }

    fn pmu_reserved_size(&self) -> u32 {
        pmu_reserved_size_tu102()
    }

    fn non_wpr_heap_size(&self) -> u32 {
        non_wpr_heap_size_tu102()
    }

    fn frts_size(&self) -> u64 {
        frts_size_tu102()
    }
}

const TU102: Tu102 = Tu102;
pub(super) const TU102_HAL: &dyn FbHal = &TU102;
