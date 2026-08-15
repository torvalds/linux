// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::prelude::*;

use crate::{
    driver::Bar0,
    gpu::{
        Architecture,
        Chipset, //
    },
};

mod ga100;
mod ga102;
mod gb100;
mod gb202;
mod gh100;
mod tu102;

pub(crate) trait FbHal {
    /// Returns the address of the currently-registered sysmem flush page.
    fn read_sysmem_flush_page(&self, bar: Bar0<'_>) -> u64;

    /// Register `addr` as the address of the sysmem flush page.
    ///
    /// This might fail if the address is too large for the receiving register.
    fn write_sysmem_flush_page(&self, bar: Bar0<'_>, addr: u64) -> Result;

    /// Returns `true` is display is supported.
    fn supports_display(&self, bar: Bar0<'_>) -> bool;

    /// Returns the VRAM size, in bytes.
    fn vidmem_size(&self, bar: Bar0<'_>) -> u64;

    /// Returns the amount of VRAM to reserve for the PMU.
    fn pmu_reserved_size(&self) -> u32;

    /// Returns the non-WPR heap size for this chipset, in bytes.
    fn non_wpr_heap_size(&self) -> u32;

    /// Returns the FRTS size, in bytes.
    fn frts_size(&self) -> u64;
}

/// Returns the HAL corresponding to `chipset`.
pub(super) fn fb_hal(chipset: Chipset) -> &'static dyn FbHal {
    match chipset.arch() {
        Architecture::Turing => tu102::TU102_HAL,
        Architecture::Ampere if chipset == Chipset::GA100 => ga100::GA100_HAL,
        Architecture::Ampere | Architecture::Ada => ga102::GA102_HAL,
        Architecture::Hopper => gh100::GH100_HAL,
        Architecture::BlackwellGB10x => gb100::GB100_HAL,
        Architecture::BlackwellGB20x => gb202::GB202_HAL,
    }
}
