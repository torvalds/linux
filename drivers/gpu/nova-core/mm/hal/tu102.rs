// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Turing, Ampere and Ada memory management HAL.

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

struct Tu102;

impl MmHal for Tu102 {
    fn write_pramin_window_base(&self, bar: Bar0<'_>, base: VramAddress) -> Result {
        bar.write_reg(
            regs::NV_PBUS_BAR0_WINDOW::zeroed()
                .with_target(regs::Bar0WindowTarget::VidMem)
                .with_base(window_base(base)?.cast()),
        );
        Ok(())
    }
}

const TU102: Tu102 = Tu102;
pub(super) const TU102_HAL: &dyn MmHal = &TU102;
