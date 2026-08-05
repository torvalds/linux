// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::prelude::*;

use kernel::{
    device,
    dma::Coherent,
    io::poll::read_poll_timeout,
    time::Delta,
    types::ScopeGuard, //
};

use crate::{
    falcon::{
        gsp::Gsp as GspEngine,
        Falcon, //
    },
    fb::FbSizes,
    firmware::gsp::GspFirmware,
    fsp::FmcBootArgs,
    gsp::{
        hal::{
            GspHal,
            UnloadBundle, //
        },
        Gsp,
        GspBootContext,
        GspFmcBootParams,
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
    fn read(gsp_falcon: &Falcon<'_, GspEngine>) -> Self {
        Self {
            mbox0: gsp_falcon.read_mailbox0(),
            mbox1: gsp_falcon.read_mailbox1(),
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
        gsp_falcon: &Falcon<'_, GspEngine>,
        fmc_boot_params: &Coherent<GspFmcBootParams>,
    ) -> bool {
        // GSP-FMC normally clears the boot parameters address from the mailboxes early during
        // boot. If the address is still there, keep polling rather than treating it as an error.
        // Any other non-zero mailbox0 value is a GSP-FMC error code.
        if self.mbox0 != 0 {
            return self.combined_addr() != fmc_boot_params.dma_address();
        }

        !gsp_falcon.riscv_branch_privilege_lockdown()
    }
}

/// Waits for GSP lockdown to be released after FSP Chain of Trust.
fn wait_for_gsp_lockdown_release(
    dev: &device::Device<device::Bound>,
    gsp_falcon: &Falcon<'_, GspEngine>,
    fmc_boot_params: &Coherent<GspFmcBootParams>,
) -> Result {
    dev_dbg!(dev, "Waiting for GSP lockdown release\n");

    let mbox = read_poll_timeout(
        || {
            // While the PRIV target mask is still locked to FSP, GSP register and mailbox reads
            // are not meaningful. Wait until HWCFG2 says the CPU can read them.
            Ok(match gsp_falcon.priv_target_mask_released() {
                false => None,
                true => Some(GspMbox::read(gsp_falcon)),
            })
        },
        |mbox| match mbox {
            None => false,
            Some(mbox) => mbox.lockdown_released_or_error(gsp_falcon, fmc_boot_params),
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
    fn run(&self, ctx: &mut GspBootContext<'_, '_>) -> Result {
        // GSP falcon does most of the work of resetting, so just wait for it to finish.
        read_poll_timeout(
            || {
                // GSP register reads are not meaningful until the PRIV target mask is released.
                if !ctx.gsp_falcon.priv_target_mask_released() {
                    return Ok(false);
                }

                ctx.gsp_falcon.is_riscv_halted()
            },
            |&halted| halted,
            Delta::from_millis(10),
            Delta::from_secs(5),
        )
        .map(|_| ())
        .inspect_err(|_| dev_err!(ctx.dev(), "GSP falcon failed to halt\n"))
    }
}

struct Gh100;

impl GspHal for Gh100 {
    /// Boot GSP via FSP Chain of Trust (Hopper/Blackwell+ path).
    ///
    /// This path uses FSP to establish a chain of trust and boot GSP-FMC. FSP handles
    /// the GSP boot internally - no manual GSP reset/boot is needed.
    fn boot(
        &self,
        gsp: &Gsp,
        ctx: &mut GspBootContext<'_, '_>,
        gsp_fw: &GspFirmware,
    ) -> Result<Option<crate::gsp::UnloadBundle>> {
        let dev = ctx.dev();
        let chipset = ctx.chipset;
        let gsp_falcon = ctx.gsp_falcon;

        let fb_sizes = FbSizes::new(chipset, ctx.bar, ctx.vgpu.state())?;
        dev_dbg!(dev, "{:#x?}\n", fb_sizes);

        let wpr_meta =
            Coherent::init(dev, GFP_KERNEL, GspFwWprMeta::from_sizes(gsp_fw, &fb_sizes))?;
        let args = FmcBootArgs::new(dev, chipset, wpr_meta, &gsp.libos, false)?;

        let unload_bundle = crate::gsp::UnloadBundle(
            KBox::new(FspUnloadBundle, GFP_KERNEL)? as KBox<dyn UnloadBundle>
        );

        // Wait for the GSP RISC-V core to halt in case of error. We create this guard after `args`
        // to make sure that the boot args and the WPR metadata they own are kept alive until halt,
        // in case they are still being accessed.
        let mut unload_guard =
            ScopeGuard::new_with_data((unload_bundle, ctx), |(unload_bundle, ctx)| {
                let _ = unload_bundle.0.run(ctx);
            });

        let fsp = unload_guard.1.fsp.as_mut().ok_or(ENODEV)?;

        fsp.boot_fmc(dev, &fb_sizes, &args)?;

        // Wait for GSP-FMC to release the GSP lockdown, indicating that `args` is not accessed
        // anymore.
        wait_for_gsp_lockdown_release(dev, gsp_falcon, args.boot_params())?;

        Ok(Some(unload_guard.dismiss().0))
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn GspHal = &GH100;
