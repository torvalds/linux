// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::io::Io;

use crate::{
    driver::Bar0,
    fsp::hal::FspHal,
    regs, //
};

struct Gh100;

/// Reads the FSP secure boot status from the Hopper/GB10x thermal scratch register.
pub(super) fn fsp_boot_status_gh100(bar: Bar0<'_>) -> u32 {
    bar.read(regs::gh100::NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE)
        .fsp_boot_complete()
        .into()
}

impl FspHal for Gh100 {
    fn fsp_boot_status(&self, bar: Bar0<'_>) -> u32 {
        fsp_boot_status_gh100(bar)
    }

    fn cot_version(&self) -> u16 {
        1
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn FspHal = &GH100;
