// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::{
    io::Io,
    prelude::*, //
};

use crate::{
    driver::Bar0,
    fb::hal::FbHal,
    regs, //
};

pub(super) fn vidmem_size_ga102(bar: Bar0<'_>) -> u64 {
    bar.read(regs::NV_USABLE_FB_SIZE_IN_MB).usable_fb_size()
}

struct Ga102;

impl FbHal for Ga102 {
    fn read_sysmem_flush_page(&self, bar: Bar0<'_>) -> u64 {
        super::ga100::read_sysmem_flush_page_ga100(bar)
    }

    fn write_sysmem_flush_page(&self, bar: Bar0<'_>, addr: u64) -> Result {
        super::ga100::write_sysmem_flush_page_ga100(bar, addr);

        Ok(())
    }

    fn supports_display(&self, bar: Bar0<'_>) -> bool {
        super::ga100::display_enabled_ga100(bar)
    }

    fn vidmem_size(&self, bar: Bar0<'_>) -> u64 {
        vidmem_size_ga102(bar)
    }

    fn pmu_reserved_size(&self) -> u32 {
        super::tu102::pmu_reserved_size_tu102()
    }

    fn non_wpr_heap_size(&self) -> u32 {
        super::tu102::non_wpr_heap_size_tu102()
    }

    fn frts_size(&self) -> u64 {
        super::tu102::frts_size_tu102()
    }
}

const GA102: Ga102 = Ga102;
pub(super) const GA102_HAL: &dyn FbHal = &GA102;
