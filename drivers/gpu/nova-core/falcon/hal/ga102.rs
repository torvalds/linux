// SPDX-License-Identifier: GPL-2.0

use core::marker::PhantomData;

use kernel::device;
use kernel::prelude::*;
use kernel::time::Delta;

use crate::driver::Bar0;
use crate::falcon::{
    Falcon, FalconBromParams, FalconEngine, FalconModSelAlgo, PeregrineCoreSelect,
};
use crate::regs;
use crate::util;

use super::FalconHal;

fn select_core_ga102<E: FalconEngine>(bar: &Bar0) -> Result {
    let bcr_ctrl = regs::NV_PRISCV_RISCV_BCR_CTRL::read(bar, &E::ID);
    if bcr_ctrl.core_select() != PeregrineCoreSelect::Falcon {
        regs::NV_PRISCV_RISCV_BCR_CTRL::default()
            .set_core_select(PeregrineCoreSelect::Falcon)
            .write(bar, &E::ID);

        // TIMEOUT: falcon core should take less than 10ms to report being enabled.
        util::wait_on(Delta::from_millis(10), || {
            let r = regs::NV_PRISCV_RISCV_BCR_CTRL::read(bar, &E::ID);
            if r.valid() {
                Some(())
            } else {
                None
            }
        })?;
    }

    Ok(())
}

fn signature_reg_fuse_version_ga102(
    dev: &device::Device,
    bar: &Bar0,
    engine_id_mask: u16,
    ucode_id: u8,
) -> Result<u32> {
    const NV_FUSE_OPT_FPF_SIZE: u8 = regs::NV_FUSE_OPT_FPF_SIZE as u8;

    // Each engine has 16 ucode version registers numbered from 1 to 16.
    let ucode_idx = match ucode_id {
        1..=NV_FUSE_OPT_FPF_SIZE => (ucode_id - 1) as usize,
        _ => {
            dev_err!(dev, "invalid ucode id {:#x}", ucode_id);
            return Err(EINVAL);
        }
    };

    // `ucode_idx` is guaranteed to be in the range [0..15], making the `read` calls provable valid
    // at build-time.
    let reg_fuse_version = if engine_id_mask & 0x0001 != 0 {
        regs::NV_FUSE_OPT_FPF_SEC2_UCODE1_VERSION::read(bar, ucode_idx).data()
    } else if engine_id_mask & 0x0004 != 0 {
        regs::NV_FUSE_OPT_FPF_NVDEC_UCODE1_VERSION::read(bar, ucode_idx).data()
    } else if engine_id_mask & 0x0400 != 0 {
        regs::NV_FUSE_OPT_FPF_GSP_UCODE1_VERSION::read(bar, ucode_idx).data()
    } else {
        dev_err!(dev, "unexpected engine_id_mask {:#x}", engine_id_mask);
        return Err(EINVAL);
    };

    // TODO[NUMM]: replace with `last_set_bit` once it lands.
    Ok(u16::BITS - reg_fuse_version.leading_zeros())
}

fn program_brom_ga102<E: FalconEngine>(bar: &Bar0, params: &FalconBromParams) -> Result {
    regs::NV_PFALCON2_FALCON_BROM_PARAADDR::default()
        .set_value(params.pkc_data_offset)
        .write(bar, &E::ID, 0);
    regs::NV_PFALCON2_FALCON_BROM_ENGIDMASK::default()
        .set_value(u32::from(params.engine_id_mask))
        .write(bar, &E::ID);
    regs::NV_PFALCON2_FALCON_BROM_CURR_UCODE_ID::default()
        .set_ucode_id(params.ucode_id)
        .write(bar, &E::ID);
    regs::NV_PFALCON2_FALCON_MOD_SEL::default()
        .set_algo(FalconModSelAlgo::Rsa3k)
        .write(bar, &E::ID);

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
}
