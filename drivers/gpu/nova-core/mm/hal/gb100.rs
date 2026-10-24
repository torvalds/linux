// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Blackwell GB10x/GB20x memory management HAL.

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

struct Gb100;

impl MmHal for Gb100 {
    fn write_pramin_window_base(&self, bar: Bar0<'_>, base: VramAddress) -> Result {
        bar.write_reg(
            regs::gb100::NV_XAL_EP_BAR0_WINDOW::zeroed().with_base(window_base(base)?.cast()),
        );
        Ok(())
    }
}

const GB100: Gb100 = Gb100;
pub(super) const GB100_HAL: &dyn MmHal = &GB100;
