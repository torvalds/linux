// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Hopper memory management HAL.

use kernel::{
    io::Io,
    prelude::*, //
};

use crate::{
    driver::Bar0,
    mm::{
        hal::{
            window_base,
            MmHal, //
        },
        regs,
        VramAddress, //
    },
};

struct Gh100;

impl MmHal for Gh100 {
    fn write_pramin_window_base(&self, bar: Bar0<'_>, base: VramAddress) -> Result {
        bar.write_reg(
            regs::gh100::NV_XAL_EP_BAR0_WINDOW::zeroed().with_base(window_base(base)?.cast()),
        );
        Ok(())
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn MmHal = &GH100;
