// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Memory management HAL.

use kernel::{
    num::Bounded,
    prelude::*, //
};

use crate::{
    driver::Bar0,
    gpu::{
        Architecture,
        Chipset, //
    },
    mm::VramAddress, //
};

mod gb100;
mod gh100;
mod tu102;

/// Trait implemented by per-architecture MM HALs.
///
/// `Sync` is required so that the `&'static dyn MmHal` references can be stored in `Send`
/// structures.
pub(super) trait MmHal: Sync {
    /// Positions the PRAMIN window at `base`.
    ///
    /// This fails if `base` is not aligned to the 64 KiB window alignment or is too large for
    /// the receiving register.
    fn write_pramin_window_base(&self, bar: Bar0<'_>, base: VramAddress) -> Result;
}

/// Returns the HAL corresponding to `chipset`.
pub(super) fn mm_hal(chipset: Chipset) -> &'static dyn MmHal {
    match chipset.arch() {
        Architecture::Turing | Architecture::Ampere | Architecture::Ada => tu102::TU102_HAL,
        Architecture::Hopper => gh100::GH100_HAL,
        Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => gb100::GB100_HAL,
    }
}

/// Converts `base` into the value of the window-base register field.
///
/// Fails with [`EINVAL`] if `base` is not aligned to the window alignment required by the register
/// field's shift, or if the shifted value does not fit within `RES` bits.
fn window_base<const RES: u32>(base: VramAddress) -> Result<Bounded<u64, RES>> {
    const WINDOW_BASE_SHIFT: u32 = 16;

    Bounded::<u64, 64>::from(base.into_raw())
        .shr_exact::<WINDOW_BASE_SHIFT, { 64 - WINDOW_BASE_SHIFT }>()
        .and_then(Bounded::try_shrink)
        .ok_or(EINVAL)
}
