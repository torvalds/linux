// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use crate::{
    driver::Bar0,
    gpu::{
        Architecture,
        Chipset, //
    },
};

mod gb100;
mod gb202;
mod gh100;

pub(super) trait FspHal {
    /// Returns the secure boot status from the architecture-specific `NV_THERM_I2CS_SCRATCH` register.
    fn fsp_boot_status(&self, bar: Bar0<'_>) -> u32;

    /// Returns the FSP Chain of Trust protocol version this chipset advertises.
    fn cot_version(&self) -> u16;
}

/// Returns the FSP HAL, or `None` if the architecture doesn't support FSP.
pub(super) fn fsp_hal(chipset: Chipset) -> Option<&'static dyn FspHal> {
    match chipset.arch() {
        Architecture::Turing | Architecture::Ampere | Architecture::Ada => None,
        Architecture::Hopper => Some(gh100::GH100_HAL),
        Architecture::BlackwellGB10x => Some(gb100::GB100_HAL),
        Architecture::BlackwellGB20x => Some(gb202::GB202_HAL),
    }
}
