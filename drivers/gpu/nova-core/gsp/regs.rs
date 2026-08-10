// SPDX-License-Identifier: GPL-2.0

use kernel::io::register;

use crate::regs::NV_PBUS_SW_SCRATCH;

// PGSP

register! {
    pub(super) NV_PGSP_QUEUE_HEAD(u32) @ 0x00110c00 {
        31:0    address;
    }
}

// PBUS

register! {
    /// Scratch register 0xe used as FRTS firmware error code.
    pub(super) NV_PBUS_SW_SCRATCH_0E_FRTS_ERR(u32) => NV_PBUS_SW_SCRATCH[0xe] {
        31:16   frts_err_code;
    }
}
