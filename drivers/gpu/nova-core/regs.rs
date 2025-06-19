// SPDX-License-Identifier: GPL-2.0

// Required to retain the original register names used by OpenRM, which are all capital snake case
// but are mapped to types.
#![allow(non_camel_case_types)]

#[macro_use]
mod macros;

use crate::gpu::{Architecture, Chipset};
use kernel::prelude::*;

/* PMC */

register!(NV_PMC_BOOT_0 @ 0x00000000, "Basic revision information about the GPU" {
    3:0     minor_revision as u8, "Minor revision of the chip";
    7:4     major_revision as u8, "Major revision of the chip";
    8:8     architecture_1 as u8, "MSB of the architecture";
    23:20   implementation as u8, "Implementation version of the architecture";
    28:24   architecture_0 as u8, "Lower bits of the architecture";
});

impl NV_PMC_BOOT_0 {
    /// Combines `architecture_0` and `architecture_1` to obtain the architecture of the chip.
    pub(crate) fn architecture(self) -> Result<Architecture> {
        Architecture::try_from(
            self.architecture_0() | (self.architecture_1() << Self::ARCHITECTURE_0.len()),
        )
    }

    /// Combines `architecture` and `implementation` to obtain a code unique to the chipset.
    pub(crate) fn chipset(self) -> Result<Chipset> {
        self.architecture()
            .map(|arch| {
                ((arch as u32) << Self::IMPLEMENTATION.len()) | self.implementation() as u32
            })
            .and_then(Chipset::try_from)
    }
}

/* PFB */

register!(NV_PFB_NISO_FLUSH_SYSMEM_ADDR @ 0x00100c10 {
    31:0    adr_39_08 as u32;
});

register!(NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI @ 0x00100c40 {
    23:0    adr_63_40 as u32;
});

/* PGC6 */

register!(NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_PRIV_LEVEL_MASK @ 0x00118128 {
    0:0     read_protection_level0 as bool, "Set after FWSEC lowers its protection level";
});

// TODO: This is an array of registers.
register!(NV_PGC6_AON_SECURE_SCRATCH_GROUP_05 @ 0x00118234 {
    31:0    value as u32;
});

register!(
    NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_0_GFW_BOOT => NV_PGC6_AON_SECURE_SCRATCH_GROUP_05,
    "Scratch group 05 register 0 used as GFW boot progress indicator" {
        7:0    progress as u8, "Progress of GFW boot (0xff means completed)";
    }
);

impl NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_0_GFW_BOOT {
    /// Returns `true` if GFW boot is completed.
    pub(crate) fn completed(self) -> bool {
        self.progress() == 0xff
    }
}
