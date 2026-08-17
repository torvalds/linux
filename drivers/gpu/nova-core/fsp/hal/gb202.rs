// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::io::Io;

use crate::{
    driver::Bar0,
    fsp::hal::FspHal,
    regs, //
};

struct Gb202;

impl FspHal for Gb202 {
    fn fsp_boot_status(&self, bar: Bar0<'_>) -> u32 {
        bar.read(regs::gb202::NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE)
            .fsp_boot_complete()
            .into()
    }

    fn cot_version(&self) -> u16 {
        2
    }
}

const GB202: Gb202 = Gb202;
pub(super) const GB202_HAL: &dyn FspHal = &GB202;
