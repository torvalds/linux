// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::{
    prelude::*,
    sizes::SizeConstants, //
};

use crate::{
    driver::Bar0,
    fb::hal::FbHal, //
};

struct Gh100;

impl FbHal for Gh100 {
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
