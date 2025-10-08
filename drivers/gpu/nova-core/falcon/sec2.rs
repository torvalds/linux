// SPDX-License-Identifier: GPL-2.0

use crate::falcon::FalconEngine;

/// Type specifying the `Sec2` falcon engine. Cannot be instantiated.
pub(crate) struct Sec2(());

impl FalconEngine for Sec2 {
    const BASE: usize = 0x00840000;
}
