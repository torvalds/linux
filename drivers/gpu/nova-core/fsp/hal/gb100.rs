// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::sizes::SizeConstants;

use crate::{
    driver::Bar0,
    fsp::hal::FspHal, //
};

struct Gb100;

impl FspHal for Gb100 {
    fn fsp_boot_status(&self, bar: Bar0<'_>) -> u32 {
        // GB10x shares Hopper's FSP secure boot status register.
        super::gh100::fsp_boot_status_gh100(bar)
    }

    fn cot_version(&self) -> u16 {
        2
    }

    fn fb_end_reserved_size(&self) -> u64 {
        u64::SZ_2M + u64::SZ_128K
    }
}

const GB100: Gb100 = Gb100;
pub(super) const GB100_HAL: &dyn FspHal = &GB100;
