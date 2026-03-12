// SPDX-License-Identifier: GPL-2.0

use core::marker::PhantomData;

use kernel::{
    io::poll::read_poll_timeout,
    prelude::*,
    time::Delta, //
};

use crate::{
    driver::Bar0,
    falcon::{
        hal::LoadMethod,
        Falcon,
        FalconBromParams,
        FalconEngine, //
    },
    regs, //
};

use super::FalconHal;

pub(super) struct Tu102<E: FalconEngine>(PhantomData<E>);

impl<E: FalconEngine> Tu102<E> {
    pub(super) fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: FalconEngine> FalconHal<E> for Tu102<E> {
    fn select_core(&self, _falcon: &Falcon<E>, _bar: &Bar0) -> Result {
        Ok(())
    }

    fn signature_reg_fuse_version(
        &self,
        _falcon: &Falcon<E>,
        _bar: &Bar0,
        _engine_id_mask: u16,
        _ucode_id: u8,
    ) -> Result<u32> {
        Ok(0)
    }

    fn program_brom(&self, _falcon: &Falcon<E>, _bar: &Bar0, _params: &FalconBromParams) -> Result {
        Ok(())
    }

    fn is_riscv_active(&self, bar: &Bar0) -> bool {
        let cpuctl = regs::NV_PRISCV_RISCV_CORE_SWITCH_RISCV_STATUS::read(bar, &E::ID);
        cpuctl.active_stat()
    }

    fn reset_wait_mem_scrubbing(&self, bar: &Bar0) -> Result {
        // TIMEOUT: memory scrubbing should complete in less than 10ms.
        read_poll_timeout(
            || Ok(regs::NV_PFALCON_FALCON_DMACTL::read(bar, &E::ID)),
            |r| r.mem_scrubbing_done(),
            Delta::ZERO,
            Delta::from_millis(10),
        )
        .map(|_| ())
    }

    fn reset_eng(&self, bar: &Bar0) -> Result {
        regs::NV_PFALCON_FALCON_ENGINE::reset_engine::<E>(bar);
        self.reset_wait_mem_scrubbing(bar)?;

        Ok(())
    }

    fn load_method(&self) -> LoadMethod {
        LoadMethod::Pio
    }
}
