// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::prelude::*;

use kernel::{
    device,
    dma::Coherent,
    io::poll::read_poll_timeout,
    time::Delta, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp as GspEngine,
        sec2::Sec2,
        Falcon, //
    },
    fb::FbLayout,
    firmware::{
        fsp::FspFirmware,
        FIRMWARE_VERSION, //
    },
    fsp::{
        FmcBootArgs,
        Fsp, //
    },
    gpu::Chipset,
    gsp::{
        boot::BootUnloadGuard,
        hal::{
            GspHal,
            UnloadBundle, //
        },
        Gsp,
        GspFwWprMeta, //
    },
};

/// GSP falcon mailbox state, used to track lockdown release status.
struct GspMbox {
    mbox0: u32,
    mbox1: u32,
}

impl GspMbox {
    /// Reads both mailboxes from the GSP falcon.
    fn read(gsp_falcon: &Falcon<GspEngine>, bar: Bar0<'_>) -> Self {
        Self {
            mbox0: gsp_falcon.read_mailbox0(bar),
            mbox1: gsp_falcon.read_mailbox1(bar),
        }
    }

    /// Combines mailbox0 and mailbox1 into a 64-bit address.
    fn combined_addr(&self) -> u64 {
        (u64::from(self.mbox1) << 32) | u64::from(self.mbox0)
    }

    /// Returns `true` if GSP lockdown has been released or a GSP-FMC error happened.
    ///
    /// Returns `true` both on successful lockdown release and on GSP-FMC-reported errors, since
    /// either condition should stop the poll loop.
    fn lockdown_released_or_error(
        &self,
        gsp_falcon: &Falcon<GspEngine>,
        bar: Bar0<'_>,
        fmc_boot_params_addr: u64,
    ) -> bool {
        // GSP-FMC normally clears the boot parameters address from the mailboxes early during
        // boot. If the address is still there, keep polling rather than treating it as an error.
        // Any other non-zero mailbox0 value is a GSP-FMC error code.
        if self.mbox0 != 0 {
            return self.combined_addr() != fmc_boot_params_addr;
        }

        !gsp_falcon.riscv_branch_privilege_lockdown(bar)
    }
}

/// Waits for GSP lockdown to be released after FSP Chain of Trust.
fn wait_for_gsp_lockdown_release(
    dev: &device::Device<device::Bound>,
    bar: Bar0<'_>,
    gsp_falcon: &Falcon<GspEngine>,
    fmc_boot_params_addr: u64,
) -> Result {
    dev_dbg!(dev, "Waiting for GSP lockdown release\n");

    let mbox = read_poll_timeout(
        || {
            // While the PRIV target mask is still locked to FSP, GSP register and mailbox reads
            // are not meaningful. Wait until HWCFG2 says the CPU can read them.
            Ok(match gsp_falcon.priv_target_mask_released(bar) {
                false => None,
                true => Some(GspMbox::read(gsp_falcon, bar)),
            })
        },
        |mbox| match mbox {
            None => false,
            Some(mbox) => mbox.lockdown_released_or_error(gsp_falcon, bar, fmc_boot_params_addr),
        },
        Delta::from_millis(10),
        Delta::from_secs(30),
    )
    .inspect_err(|_| {
        dev_err!(dev, "GSP lockdown release timeout\n");
    })?
    .ok_or(EIO)?;

    // If polling stopped with a non-zero mailbox0, it was not the boot parameters address
    // anymore and therefore represents a GSP-FMC error code.
    if mbox.mbox0 != 0 {
        dev_err!(dev, "GSP-FMC boot failed (mbox: {:#x})\n", mbox.mbox0);
        return Err(EIO);
    }

    dev_dbg!(dev, "GSP lockdown released\n");
    Ok(())
}

struct FspUnloadBundle;

impl UnloadBundle for FspUnloadBundle {
    fn run(
        &self,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<GspEngine>,
        _sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        // GSP falcon does most of the work of resetting, so just wait for it to finish.
        read_poll_timeout(
            || Ok(gsp_falcon.is_riscv_active(bar)),
            |&active| !active,
            Delta::from_millis(10),
            Delta::from_secs(5),
        )
        .map(|_| ())
        .inspect_err(|_| dev_err!(dev, "GSP falcon failed to halt\n"))
    }
}

struct Gh100;

impl GspHal for Gh100 {
    /// Boot GSP via FSP Chain of Trust (Hopper/Blackwell+ path).
    ///
    /// This path uses FSP to establish a chain of trust and boot GSP-FMC. FSP handles
    /// the GSP boot internally - no manual GSP reset/boot is needed.
    fn boot<'a>(
        &self,
        gsp: &'a Gsp,
        dev: &'a device::Device<device::Bound>,
        bar: Bar0<'a>,
        chipset: Chipset,
        fb_layout: &FbLayout,
        wpr_meta: &Coherent<GspFwWprMeta>,
        gsp_falcon: &'a Falcon<GspEngine>,
        sec2_falcon: &'a Falcon<Sec2>,
    ) -> Result<BootUnloadGuard<'a>> {
        let fsp_fw = FspFirmware::new(dev, chipset, FIRMWARE_VERSION)?;

        let unload_bundle = crate::gsp::UnloadBundle(
            KBox::new(FspUnloadBundle, GFP_KERNEL)? as KBox<dyn UnloadBundle>
        );

        // Wrap the unload bundle into a drop guard so it is automatically run upon failure.
        let unload_guard =
            BootUnloadGuard::new(gsp, dev, bar, gsp_falcon, sec2_falcon, Some(unload_bundle));

        let mut fsp = Fsp::wait_secure_boot(dev, bar, chipset, fsp_fw)?;

        let args = FmcBootArgs::new(
            dev,
            chipset,
            wpr_meta.dma_handle(),
            gsp.libos.dma_handle(),
            false,
        )?;

        fsp.boot_fmc(dev, bar, fb_layout, &args)?;

        wait_for_gsp_lockdown_release(dev, bar, gsp_falcon, args.boot_params_dma_handle())?;

        Ok(unload_guard)
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn GspHal = &GH100;
