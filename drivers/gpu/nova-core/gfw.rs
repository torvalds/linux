// SPDX-License-Identifier: GPL-2.0

//! GPU Firmware (GFW) support.
//!
//! Upon reset, the GPU runs some firmware code from the BIOS to setup its core parameters. Most of
//! the GPU is considered unusable until this step is completed, so we must wait on it before
//! performing driver initialization.

use kernel::bindings;
use kernel::prelude::*;
use kernel::time::Delta;

use crate::driver::Bar0;
use crate::regs;
use crate::util;

/// Wait until `GFW` (GPU Firmware) completes, or a 4 seconds timeout elapses.
pub(crate) fn wait_gfw_boot_completion(bar: &Bar0) -> Result {
    // TIMEOUT: arbitrarily large value. GFW starts running immediately after the GPU is put out of
    // reset, and should complete in less time than that.
    util::wait_on(Delta::from_secs(4), || {
        // Check that FWSEC has lowered its protection level before reading the GFW_BOOT
        // status.
        let gfw_booted = regs::NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_PRIV_LEVEL_MASK::read(bar)
            .read_protection_level0()
            && regs::NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_0_GFW_BOOT::read(bar).completed();

        if gfw_booted {
            Some(())
        } else {
            // TODO[DLAY]: replace with [1] once it merges.
            // [1] https://lore.kernel.org/rust-for-linux/20250423192857.199712-6-fujita.tomonori@gmail.com/
            //
            // SAFETY: `msleep()` is safe to call with any parameter.
            unsafe { bindings::msleep(1) };

            None
        }
    })
}
