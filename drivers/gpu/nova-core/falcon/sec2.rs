// SPDX-License-Identifier: GPL-2.0

use crate::falcon::{FalconEngine, PFalcon2Base, PFalconBase};
use crate::regs::macros::RegisterBase;

/// Type specifying the `Sec2` falcon engine. Cannot be instantiated.
pub(crate) struct Sec2(());

impl RegisterBase<PFalconBase> for Sec2 {
    const BASE: usize = 0x00840000;
}

impl RegisterBase<PFalcon2Base> for Sec2 {
    const BASE: usize = 0x00841000;
}

impl FalconEngine for Sec2 {
    const ID: Self = Sec2(());
}
