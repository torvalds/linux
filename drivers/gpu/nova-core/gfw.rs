// SPDX-License-Identifier: GPL-2.0

//! GPU Firmware (`GFW`) support, a.k.a `devinit`.
//!
//! Upon reset, the GPU runs some firmware code from the BIOS to setup its core parameters. Most of
//! the GPU is considered unusable until this step is completed, so we must wait on it before
//! performing driver initialization.
//!
//! A clarification about devinit terminology: devinit is a sequence of register read/writes after
//! reset that performs tasks such as:
//! 1. Programming VRAM memory controller timings.
//! 2. Power sequencing.
//! 3. Clock and PLL configuration.
//! 4. Thermal management.
//!
//! devinit itself is a 'script' which is interpreted by an interpreter program typically running
//! on the PMU microcontroller.
//!
//! Note that the devinit sequence also needs to run during suspend/resume.

use kernel::bindings;
use kernel::prelude::*;
use kernel::time::Delta;

use crate::driver::Bar0;
use crate::regs;
use crate::util;

/// Wait for the `GFW` (GPU firmware) boot completion signal (`GFW_BOOT`), or a 4 seconds timeout.
///
/// Upon GPU reset, several microcontrollers (such as PMU, SEC2, GSP etc) run some firmware code to
/// setup its core parameters. Most of the GPU is considered unusable until this step is completed,
/// so it must be waited on very early during driver initialization.
///
/// The `GFW` code includes several components that need to execute before the driver loads. These
/// components are located in the VBIOS ROM and executed in a sequence on these different
/// microcontrollers. The devinit sequence typically runs on the PMU, and the FWSEC runs on the
/// GSP.
///
/// This function waits for a signal indicating that core initialization is complete. Before this
/// signal is received, little can be done with the GPU. This signal is set by the FWSEC running on
/// the GSP in Heavy-secured mode.
pub(crate) fn wait_gfw_boot_completion(bar: &Bar0) -> Result {
    // Before accessing the completion status in `NV_PGC6_AON_SECURE_SCRATCH_GROUP_05`, we must
    // first check `NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_PRIV_LEVEL_MASK`. This is because
    // `NV_PGC6_AON_SECURE_SCRATCH_GROUP_05` becomes accessible only after the secure firmware
    // (FWSEC) lowers the privilege level to allow CPU (LS/Light-secured) access. We can only
    // safely read the status register from CPU (LS/Light-secured) once the mask indicates
    // that the privilege level has been lowered.
    //
    // TIMEOUT: arbitrarily large value. GFW starts running immediately after the GPU is put out of
    // reset, and should complete in less time than that.
    util::wait_on(Delta::from_secs(4), || {
        // Check that FWSEC has lowered its protection level before reading the GFW_BOOT status.
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
