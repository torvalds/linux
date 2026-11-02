// SPDX-License-Identifier: GPL-2.0

use kernel::io::{
    io_project,
    register,
    Mmio, //
};

use crate::{
    driver::{
        Bar0,
        NovaRegisters, //
    },
    falcon::FalconEngine, //
};

/// Type specifying the `Sec2` falcon engine. Cannot be instantiated.
pub(crate) struct Sec2(());

register! {
    base: NovaRegisters;

    PFALCON: super::PFalconRegisters @ 0x00840000;
    PFALCON2: super::PFalcon2Registers @ 0x00841000;
}

impl FalconEngine for Sec2 {
    #[inline]
    fn pfalcon(io: Bar0<'_>) -> Mmio<'_, super::PFalconRegisters> {
        io_project!(io, build: PFALCON)
    }

    #[inline]
    fn pfalcon2(io: Bar0<'_>) -> Mmio<'_, super::PFalcon2Registers> {
        io_project!(io, build: PFALCON2)
    }
}
