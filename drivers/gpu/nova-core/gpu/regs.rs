// SPDX-License-Identifier: GPL-2.0

use kernel::{
    io::register,
    prelude::*, //
};

use super::{
    Architecture,
    Chipset, //
};

use crate::driver::NovaRegisters;

// PMC

register! {
    base: NovaRegisters;

    /// Basic revision information about the GPU.
    pub(super) NV_PMC_BOOT_0(u32) @ 0x00000000 {
        /// Lower bits of the architecture.
        28:24   architecture_0;
        /// Implementation version of the architecture.
        23:20   implementation;
        /// MSB of the architecture.
        8:8     architecture_1;
        /// Major revision of the chip.
        7:4     major_revision;
        /// Minor revision of the chip.
        3:0     minor_revision;
    }

    /// Extended architecture information.
    pub(super) NV_PMC_BOOT_42(u32) @ 0x00000a00 {
        /// Architecture value.
        29:24   architecture ?=> Architecture;
        /// Implementation version of the architecture.
        23:20   implementation;
        /// Major revision of the chip.
        19:16   major_revision;
        /// Minor revision of the chip.
        15:12   minor_revision;
    }
}

impl NV_PMC_BOOT_0 {
    pub(super) fn is_older_than_fermi(self) -> bool {
        // From https://github.com/NVIDIA/open-gpu-doc/tree/master/manuals :
        const NV_PMC_BOOT_0_ARCHITECTURE_GF100: u32 = 0xc;

        // Older chips left arch1 zeroed out. That, combined with an arch0 value that is less than
        // GF100, means "older than Fermi".
        self.architecture_1() == 0 && self.architecture_0() < NV_PMC_BOOT_0_ARCHITECTURE_GF100
    }
}

impl NV_PMC_BOOT_42 {
    /// Combines `architecture` and `implementation` to obtain a code unique to the chipset.
    pub(super) fn chipset(self) -> Result<Chipset> {
        self.architecture()
            .map(|arch| {
                ((arch as u32) << Self::IMPLEMENTATION_RANGE.len())
                    | u32::from(self.implementation())
            })
            .and_then(Chipset::try_from)
    }

    /// Returns the raw architecture value from the register.
    fn architecture_raw(self) -> u8 {
        ((self.into_raw() >> Self::ARCHITECTURE_RANGE.start())
            & ((1 << Self::ARCHITECTURE_RANGE.len()) - 1)) as u8
    }
}

impl kernel::fmt::Display for NV_PMC_BOOT_42 {
    fn fmt(&self, f: &mut kernel::fmt::Formatter<'_>) -> kernel::fmt::Result {
        write!(
            f,
            "boot42 = 0x{:08x} (architecture 0x{:x}, implementation 0x{:x})",
            self.inner,
            self.architecture_raw(),
            self.implementation()
        )
    }
}
