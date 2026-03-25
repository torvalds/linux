// SPDX-License-Identifier: GPL-2.0

use kernel::{
    io::{
        poll::read_poll_timeout,
        register::{
            RegisterBase,
            WithBase, //
        },
        Io,
    },
    prelude::*,
    time::Delta, //
};

use crate::{
    driver::Bar0,
    falcon::{
        Falcon,
        FalconEngine,
        PFalcon2Base,
        PFalconBase, //
    },
    regs,
};

/// Type specifying the `Gsp` falcon engine. Cannot be instantiated.
pub(crate) struct Gsp(());

impl RegisterBase<PFalconBase> for Gsp {
    const BASE: usize = 0x00110000;
}

impl RegisterBase<PFalcon2Base> for Gsp {
    const BASE: usize = 0x00111000;
}

impl FalconEngine for Gsp {}

impl Falcon<Gsp> {
    /// Clears the SWGEN0 bit in the Falcon's IRQ status clear register to
    /// allow GSP to signal CPU for processing new messages in message queue.
    pub(crate) fn clear_swgen0_intr(&self, bar: &Bar0) {
        bar.write(
            WithBase::of::<Gsp>(),
            regs::NV_PFALCON_FALCON_IRQSCLR::zeroed().with_swgen0(true),
        );
    }

    /// Checks if GSP reload/resume has completed during the boot process.
    pub(crate) fn check_reload_completed(&self, bar: &Bar0, timeout: Delta) -> Result<bool> {
        read_poll_timeout(
            || Ok(bar.read(regs::NV_PGC6_BSI_SECURE_SCRATCH_14)),
            |val| val.boot_stage_3_handoff(),
            Delta::ZERO,
            timeout,
        )
        .map(|_| true)
    }
}
