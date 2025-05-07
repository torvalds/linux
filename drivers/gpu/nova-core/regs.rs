// SPDX-License-Identifier: GPL-2.0

// Required to retain the original register names used by OpenRM, which are all capital snake case
// but are mapped to types.
#![allow(non_camel_case_types)]

#[macro_use]
mod macros;

use crate::gpu::Chipset;

/* PMC */

register!(NV_PMC_BOOT_0 @ 0x00000000, "Basic revision information about the GPU" {
    3:0     minor_revision as u8, "Minor revision of the chip";
    7:4     major_revision as u8, "Major revision of the chip";
    28:20   chipset as u32 ?=> Chipset, "Chipset model";
});
