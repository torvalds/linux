// SPDX-License-Identifier: GPL-2.0

use core::marker::PhantomData;

use kernel::{
    device,
    io::{
        poll::read_poll_timeout,
        register::{
            Array,
            WithBase, //
        },
        Io, //
    },
    prelude::*,
    time::Delta, //
};

use crate::{
    driver::Bar0,
    falcon::{
        hal::LoadMethod,
        Falcon,
        FalconBromParams,
        FalconEngine,
        FalconModSelAlgo,
        PeregrineCoreSelect, //
    },
    regs,
};

use super::FalconHal;

fn select_core_ga102<E: FalconEngine>(bar: &Bar0) -> Result {
    let bcr_ctrl = bar.read(regs::NV_PRISCV_RISCV_BCR_CTRL::of::<E>());
    if bcr_ctrl.core_select() != PeregrineCoreSelect::Falcon {
        bar.write(
            WithBase::of::<E>(),
            regs::NV_PRISCV_RISCV_BCR_CTRL::zeroed().with_core_select(PeregrineCoreSelect::Falcon),
        );

        // TIMEOUT: falcon core should take less than 10ms to report being enabled.
        read_poll_timeout(
            || Ok(bar.read(regs::NV_PRISCV_RISCV_BCR_CTRL::of::<E>())),
            |r| r.valid(),
            Delta::ZERO,
            Delta::from_millis(10),
        )?;
    }

    Ok(())
}

fn signature_reg_fuse_version_ga102(
    dev: &device::Device,
    bar: &Bar0,
    engine_id_mask: u16,
    ucode_id: u8,
) -> Result<u32> {
    // Each engine has 16 ucode version registers numbered from 1 to 16.
    let ucode_idx = match usize::from(ucode_id) {
        ucode_id @ 1..=regs::NV_FUSE_OPT_FPF_SIZE => ucode_id - 1,
        _ => {
            dev_err!(dev, "invalid ucode id {:#x}\n", ucode_id);
            return Err(EINVAL);
        }
    };

    // `ucode_idx` is guaranteed to be in the range [0..15], making the `read` calls provable valid
    // at build-time.
    let reg_fuse_version: u16 = if engine_id_mask & 0x0001 != 0 {
        bar.read(regs::NV_FUSE_OPT_FPF_SEC2_UCODE1_VERSION::at(ucode_idx))
            .data()
    } else if engine_id_mask & 0x0004 != 0 {
        bar.read(regs::NV_FUSE_OPT_FPF_NVDEC_UCODE1_VERSION::at(ucode_idx))
            .data()
    } else if engine_id_mask & 0x0400 != 0 {
        bar.read(regs::NV_FUSE_OPT_FPF_GSP_UCODE1_VERSION::at(ucode_idx))
            .data()
    } else {
        dev_err!(dev, "unexpected engine_id_mask {:#x}\n", engine_id_mask);
        return Err(EINVAL);
    };

    // TODO[NUMM]: replace with `last_set_bit` once it lands.
    Ok(u16::BITS - reg_fuse_version.leading_zeros())
}

fn program_brom_ga102<E: FalconEngine>(bar: &Bar0, params: &FalconBromParams) -> Result {
    bar.write(
        WithBase::of::<E>().at(0),
        regs::NV_PFALCON2_FALCON_BROM_PARAADDR::zeroed().with_value(params.pkc_data_offset),
    );
    bar.write(
        WithBase::of::<E>(),
        regs::NV_PFALCON2_FALCON_BROM_ENGIDMASK::zeroed()
            .with_value(u32::from(params.engine_id_mask)),
    );
    bar.write(
        WithBase::of::<E>(),
        regs::NV_PFALCON2_FALCON_BROM_CURR_UCODE_ID::zeroed().with_ucode_id(params.ucode_id),
    );
    bar.write(
        WithBase::of::<E>(),
        regs::NV_PFALCON2_FALCON_MOD_SEL::zeroed().with_algo(FalconModSelAlgo::Rsa3k),
    );

    Ok(())
}

pub(super) struct Ga102<E: FalconEngine>(PhantomData<E>);

impl<E: FalconEngine> Ga102<E> {
    pub(super) fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: FalconEngine> FalconHal<E> for Ga102<E> {
    fn select_core(&self, _falcon: &Falcon<E>, bar: &Bar0) -> Result {
        select_core_ga102::<E>(bar)
    }

    fn signature_reg_fuse_version(
        &self,
        falcon: &Falcon<E>,
        bar: &Bar0,
        engine_id_mask: u16,
        ucode_id: u8,
    ) -> Result<u32> {
        signature_reg_fuse_version_ga102(&falcon.dev, bar, engine_id_mask, ucode_id)
    }

    fn program_brom(&self, _falcon: &Falcon<E>, bar: &Bar0, params: &FalconBromParams) -> Result {
        program_brom_ga102::<E>(bar, params)
    }

    fn is_riscv_active(&self, bar: &Bar0) -> bool {
        bar.read(regs::NV_PRISCV_RISCV_CPUCTL::of::<E>())
            .active_stat()
    }

    fn reset_wait_mem_scrubbing(&self, bar: &Bar0) -> Result {
        // TIMEOUT: memory scrubbing should complete in less than 20ms.
        read_poll_timeout(
            || Ok(bar.read(regs::NV_PFALCON_FALCON_HWCFG2::of::<E>())),
            |r| r.mem_scrubbing_done(),
            Delta::ZERO,
            Delta::from_millis(20),
        )
        .map(|_| ())
    }

    fn reset_eng(&self, bar: &Bar0) -> Result {
        let _ = bar.read(regs::NV_PFALCON_FALCON_HWCFG2::of::<E>());

        // According to OpenRM's `kflcnPreResetWait_GA102` documentation, HW sometimes does not set
        // RESET_READY so a non-failing timeout is used.
        let _ = read_poll_timeout(
            || Ok(bar.read(regs::NV_PFALCON_FALCON_HWCFG2::of::<E>())),
            |r| r.reset_ready(),
            Delta::ZERO,
            Delta::from_micros(150),
        );

        regs::NV_PFALCON_FALCON_ENGINE::reset_engine::<E>(bar);
        self.reset_wait_mem_scrubbing(bar)?;

        Ok(())
    }

    fn load_method(&self) -> LoadMethod {
        LoadMethod::Dma
    }
}
