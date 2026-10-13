// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use core::marker::PhantomData;

use kernel::{
    io::{
        poll::read_poll_timeout,
        register::Array,
        Io, //
    },
    prelude::*,
    time::Delta, //
};

use crate::{
    falcon::{
        hal::LoadMethod,
        Falcon,
        FalconBromParams,
        FalconEngine, //
    },
    regs, //
};

use super::FalconHal;

/// Writes the trigger register available on GA100 and more recent.
pub(super) fn retrigger_ga100<E: FalconEngine>(falcon: &Falcon<'_, E>) {
    falcon.pfalcon.write(
        Array::at(0),
        regs::NV_PFALCON_FALCON_INTR_RETRIGGER::zeroed().with_trigger(true),
    );
}

/// The falcon HAL for Turing and GA100. GA100 boots like a Turing, but unlike Turing, it has a
/// retrigger register.
pub(super) struct Tu102<E: FalconEngine> {
    /// If `true`, the falcons have `NV_PFALCON_FALCON_INTR_RETRIGGER`.
    has_intr_retrigger: bool,
    _engine: PhantomData<E>,
}

impl<E: FalconEngine> Tu102<E> {
    /// Creates a new HAL.
    ///
    /// `has_intr_retrigger` indicates whether the retrigger register can be used.
    pub(super) fn new(has_intr_retrigger: bool) -> Self {
        Self {
            has_intr_retrigger,
            _engine: PhantomData,
        }
    }
}

impl<E: FalconEngine> FalconHal<E> for Tu102<E> {
    fn select_core(&self, _falcon: &Falcon<'_, E>) -> Result {
        Ok(())
    }

    fn signature_reg_fuse_version(
        &self,
        _falcon: &Falcon<'_, E>,
        _engine_id_mask: u16,
        _ucode_id: u8,
    ) -> Result<u32> {
        Ok(0)
    }

    fn program_brom(&self, _falcon: &Falcon<'_, E>, _params: &FalconBromParams) {}

    fn is_riscv_active(&self, falcon: &Falcon<'_, E>) -> bool {
        falcon
            .pfalcon2
            .read(regs::NV_PRISCV_RISCV_CORE_SWITCH_RISCV_STATUS)
            .active_stat()
    }

    fn is_riscv_halted(&self, _falcon: &Falcon<'_, E>) -> Result<bool> {
        Err(ENOTSUPP)
    }

    fn reset_wait_mem_scrubbing(&self, falcon: &Falcon<'_, E>) -> Result {
        // TIMEOUT: memory scrubbing should complete in less than 10ms.
        read_poll_timeout(
            || Ok(falcon.pfalcon.read(regs::NV_PFALCON_FALCON_DMACTL)),
            |r| r.mem_scrubbing_done(),
            Delta::ZERO,
            Delta::from_millis(10),
        )
        .map(|_| ())
    }

    fn reset_eng(&self, falcon: &Falcon<'_, E>) -> Result {
        regs::NV_PFALCON_FALCON_ENGINE::reset_engine(falcon.pfalcon);
        self.reset_wait_mem_scrubbing(falcon)?;

        Ok(())
    }

    fn load_method(&self) -> LoadMethod {
        LoadMethod::Pio
    }

    fn host_routed_causes(
        &self,
        falcon: &Falcon<'_, E>,
        latched: regs::NV_PFALCON_FALCON_IRQSTAT,
    ) -> regs::NV_PFALCON_FALCON_IRQSTAT {
        let pfalcon2 = falcon.pfalcon2;
        let mask = pfalcon2.read(regs::tu102::NV_PRISCV_RISCV_IRQMASK).value();
        let dest = pfalcon2.read(regs::tu102::NV_PRISCV_RISCV_IRQDEST).value();

        regs::NV_PFALCON_FALCON_IRQSTAT::from(latched.into_raw() & mask & dest)
    }

    fn retrigger(&self, falcon: &Falcon<'_, E>) {
        if self.has_intr_retrigger {
            retrigger_ga100(falcon);
        }
    }
}
