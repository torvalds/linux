// SPDX-License-Identifier: GPL-2.0

use kernel::{
    io::{
        io_project,
        poll::read_poll_timeout,
        register,
        Io,
        Mmio, //
    },
    prelude::*,
    time::Delta, //
};

use crate::{
    driver::{
        Bar0,
        NovaRegisters, //
    },
    falcon::{
        Falcon,
        FalconEngine, //
    },
    regs,
};

/// Type specifying the `Gsp` falcon engine. Cannot be instantiated.
pub(crate) struct Gsp(());

register! {
    base: NovaRegisters;

    PFALCON: super::PFalconRegisters @ 0x00110000;
    PFALCON2: super::PFalcon2Registers @ 0x00111000;
}

impl FalconEngine for Gsp {
    #[inline]
    fn pfalcon(io: Bar0<'_>) -> Mmio<'_, super::PFalconRegisters> {
        io_project!(io, build: PFALCON)
    }

    #[inline]
    fn pfalcon2(io: Bar0<'_>) -> Mmio<'_, super::PFalcon2Registers> {
        io_project!(io, build: PFALCON2)
    }
}

impl<'a> Falcon<'a, Gsp> {
    /// Clears the SWGEN0 latch in the GSP falcon.
    ///
    /// While the latch is set, no later message signals the tree, so a caller that consumed a
    /// notification by polling must clear it.
    pub(crate) fn clear_swgen0_intr(&self) {
        self.pfalcon
            .write_reg(regs::NV_PFALCON_FALCON_IRQSCLR::zeroed().with_swgen0(true));
    }

    /// Reads the GSP falcon causes that are routed to the host, without clearing any latch.
    ///
    /// Every one of them other than SWGEN0 reports a GSP fault.
    pub(crate) fn read_host_intr(&self) -> regs::NV_PFALCON_FALCON_IRQSTAT {
        let latched = self.pfalcon.read(regs::NV_PFALCON_FALCON_IRQSTAT);

        self.hal.host_routed_causes(self, latched)
    }

    /// Reads the host-routed causes and clears the SWGEN0 latch if it was set.
    ///
    /// Returns the causes as read, before the clear. No other latch changes.
    pub(crate) fn take_host_intr(&self) -> regs::NV_PFALCON_FALCON_IRQSTAT {
        let status = self.read_host_intr();

        if status.swgen0() {
            self.clear_swgen0_intr();
        }

        status
    }

    /// Clears the latch of every interrupt cause set in `status`.
    ///
    /// A cause driven from outside the falcon is still set on return, and
    /// [`Self::read_host_intr`] reports the causes that remain.
    pub(crate) fn clear_intr(&self, status: regs::NV_PFALCON_FALCON_IRQSTAT) {
        self.pfalcon
            .write_reg(regs::NV_PFALCON_FALCON_IRQSCLR::from(status.into_raw()));
    }

    /// Retriggers the GSP falcon, which then re-emits its host-routed causes into the tree.
    ///
    /// Call this only once every host cause is clear. A cause still set is re-emitted at once, and
    /// its vector arrives again as soon as delivery is rearmed.
    ///
    /// Does nothing on Turing, whose falcons have no retrigger register.
    pub(crate) fn retrigger_intr(&self) {
        self.hal.retrigger(self);
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
        self.pfalcon
            .read(regs::NV_PFALCON_FALCON_HWCFG2)
            .riscv_br_priv_lockdown()
    }

    /// Returns whether GSP registers can be read by the CPU.
    pub(crate) fn priv_target_mask_released(&self) -> bool {
        /// Pattern returned by GSP register reads while the PRIV target mask still blocks CPU
        /// access. The low byte varies; the upper 24 bits are fixed.
        const LOCKED_PATTERN: u32 = 0xbadf_4100;
        const LOCKED_MASK: u32 = 0xffff_ff00;

        let hwcfg2 = self.pfalcon.read(regs::NV_PFALCON_FALCON_HWCFG2).into_raw();

        hwcfg2 != 0 && (hwcfg2 & LOCKED_MASK) != LOCKED_PATTERN
    }
}
