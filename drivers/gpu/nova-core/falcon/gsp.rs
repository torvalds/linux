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

impl<'a> Falcon<'a, Gsp> {
    /// Clears the SWGEN0 bit in the Falcon's IRQ status clear register to
    /// allow GSP to signal CPU for processing new messages in message queue.
    pub(crate) fn clear_swgen0_intr(&self) {
        self.bar.write(
            WithBase::of::<Gsp>(),
            regs::NV_PFALCON_FALCON_IRQSCLR::zeroed().with_swgen0(true),
        );
    }

    /// Checks if GSP reload/resume has completed during the boot process.
    pub(crate) fn check_reload_completed(&self, timeout: Delta) -> Result<bool> {
        read_poll_timeout(
            || Ok(self.bar.read(regs::NV_PGC6_BSI_SECURE_SCRATCH_14)),
            |val| val.boot_stage_3_handoff(),
            Delta::ZERO,
            timeout,
        )
        .map(|_| true)
    }

    /// Returns whether the RISC-V branch privilege lockdown bit is set.
    pub(crate) fn riscv_branch_privilege_lockdown(&self) -> bool {
        self.bar
            .read(regs::NV_PFALCON_FALCON_HWCFG2::of::<Gsp>())
            .riscv_br_priv_lockdown()
    }

    /// Returns whether GSP registers can be read by the CPU.
    pub(crate) fn priv_target_mask_released(&self) -> bool {
        /// Pattern returned by GSP register reads while the PRIV target mask still blocks CPU
        /// access. The low byte varies; the upper 24 bits are fixed.
        const LOCKED_PATTERN: u32 = 0xbadf_4100;
        const LOCKED_MASK: u32 = 0xffff_ff00;

        let hwcfg2 = self
            .bar
            .read(regs::NV_PFALCON_FALCON_HWCFG2::of::<Gsp>())
            .into_raw();

        hwcfg2 != 0 && (hwcfg2 & LOCKED_MASK) != LOCKED_PATTERN
    }
}
