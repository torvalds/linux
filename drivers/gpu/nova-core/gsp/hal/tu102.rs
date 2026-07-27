// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::prelude::*;

use kernel::{
    device,
    dma::Coherent,
    io::Io,
    types::ScopeGuard, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp as GspEngine,
        sec2::Sec2,
        Falcon, //
    },
    fb::{
        wpr2_range,
        FbLayout, //
    },
    firmware::{
        booter::{
            BooterFirmware,
            BooterKind, //
        },
        fwsec::{
            bootloader::FwsecFirmwareWithBl,
            FwsecCommand,
            FwsecFirmware, //
        },
        gsp::GspFirmware,
        FIRMWARE_VERSION, //
    },
    gpu::Chipset,
    gsp::{
        hal::{
            GspHal,
            UnloadBundle, //
        },
        regs,
        sequencer::GspSequencer,
        Gsp,
        GspBootContext,
        GspFwWprMeta, //
    },
    vbios::Vbios, //
};

// A ready-to-run FWSEC unload firmware.
//
// Since there are two variants of the prepared firmware (with and without a bootloader), this type
// abstracts the difference.
enum FwsecUnloadFirmware {
    WithoutBl(FwsecFirmware),
    WithBl(FwsecFirmwareWithBl),
}

impl FwsecUnloadFirmware {
    /// Runs the FWSEC SB firmware.
    fn run(
        &self,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<'_, GspEngine>,
    ) -> Result {
        match self {
            Self::WithoutBl(fw) => fw.run(dev, gsp_falcon),
            Self::WithBl(fw) => fw.run(dev, gsp_falcon, bar),
        }
    }
}

// Contains the firmware required to fully reset GSP on chipsets where the GSP is started using
// FWSEC/Booter.
struct Sec2UnloadBundle {
    fwsec_sb: FwsecUnloadFirmware,
    booter_unloader: BooterFirmware,
}

impl UnloadBundle for Sec2UnloadBundle {
    fn run(&self, ctx: &mut GspBootContext<'_, '_>) -> Result {
        let dev = ctx.dev();
        let bar = ctx.bar;

        // Run FWSEC-SB to reset the GSP falcon to its pre-libos state.
        // Log errors but keep going if it fails.
        let fwsec_sb_res = self
            .fwsec_sb
            .run(dev, bar, ctx.gsp_falcon)
            .inspect_err(|e| dev_err!(dev, "FWSEC-SB failed to run: {:?}\n", e));

        // Remove WPR2 region if set.
        let booter_unloader_res = (|| {
            if wpr2_range(bar).is_none() {
                return Ok(());
            }

            ctx.sec2_falcon.reset()?;
            ctx.sec2_falcon.load(&self.booter_unloader)?;

            // Sentinel value to confirm that Booter Unloader has run.
            const MAILBOX_SENTINEL: u32 = 0xff;
            let (mbox0, _) = ctx
                .sec2_falcon
                .boot(Some(MAILBOX_SENTINEL), Some(MAILBOX_SENTINEL))?;
            if mbox0 != 0 {
                dev_err!(dev, "Booter Unloader returned error 0x{:x}\n", mbox0);
                return Err(EINVAL);
            }

            // Confirm that the WPR2 region has been removed.
            if wpr2_range(bar).is_some() {
                dev_err!(
                    dev,
                    "WPR2 region still set after Booter Unloader returned\n"
                );
                return Err(EBUSY);
            }

            Ok(())
        })()
        .inspect_err(|e| dev_err!(dev, "Booter Unloader failed to run: {:?}\n", e));

        fwsec_sb_res.and(booter_unloader_res)
    }
}

pub(super) struct Tu102 {
    /// If `true`, then the FWSEC-FRTS bootloader will be used to load the actual firmware.
    pub(super) needs_fwsec_bootloader: bool,
}

impl Tu102 {
    /// Helper method to load and run the FWSEC-FRTS firmware and confirm that it has properly
    /// created the WPR2 region.
    fn run_fwsec_frts(
        &self,
        dev: &device::Device<device::Bound>,
        chipset: Chipset,
        falcon: &Falcon<'_, GspEngine>,
        bar: Bar0<'_>,
        bios: &Vbios,
        fb_layout: &FbLayout,
    ) -> Result {
        // Check that the WPR2 region does not already exist - if it does, we cannot run
        // FWSEC-FRTS until the GPU is reset.
        if wpr2_range(bar).is_some() {
            dev_err!(
                dev,
                "WPR2 region already exists - GPU needs to be reset to proceed\n"
            );
            return Err(EBUSY);
        }

        // FWSEC-FRTS will create the WPR2 region.
        let fwsec_frts = FwsecFirmware::new(
            dev,
            falcon,
            bios,
            FwsecCommand::Frts {
                frts_addr: fb_layout.frts.start,
                frts_size: fb_layout.frts.len(),
            },
        )?;

        if self.needs_fwsec_bootloader {
            let fwsec_frts_bl = FwsecFirmwareWithBl::new(fwsec_frts, dev, chipset)?;
            // Load and run the bootloader, which will load FWSEC-FRTS and run it.
            fwsec_frts_bl.run(dev, falcon, bar)?;
        } else {
            // Load and run FWSEC-FRTS directly.
            fwsec_frts.run(dev, falcon)?;
        }

        // SCRATCH_E contains the error code for FWSEC-FRTS.
        let frts_status = bar
            .read(regs::NV_PBUS_SW_SCRATCH_0E_FRTS_ERR)
            .frts_err_code();
        if frts_status != 0 {
            dev_err!(
                dev,
                "FWSEC-FRTS returned with error code {:#x}\n",
                frts_status
            );

            return Err(EIO);
        }

        // Check that the WPR2 region has been created as we requested.
        let Some(wpr2_range) = wpr2_range(bar) else {
            dev_err!(dev, "WPR2 region not created after running FWSEC-FRTS\n");

            return Err(EIO);
        };

        if wpr2_range.start != fb_layout.frts.start {
            dev_err!(
                dev,
                "WPR2 region created at unexpected address {:#x}; expected {:#x}\n",
                wpr2_range.start,
                fb_layout.frts.start,
            );

            return Err(EIO);
        }

        dev_dbg!(dev, "WPR2: {:#x}-{:#x}\n", wpr2_range.start, wpr2_range.end);
        dev_dbg!(dev, "GPU instance built\n");

        Ok(())
    }

    /// Load and prepare the resources required to properly reset the GSP after it has been stopped.
    fn build_unload_bundle(
        &self,
        dev: &device::Device<device::Bound>,
        chipset: Chipset,
        bios: &Vbios,
        gsp_falcon: &Falcon<'_, GspEngine>,
        sec2_falcon: &Falcon<'_, Sec2>,
    ) -> Result<crate::gsp::UnloadBundle> {
        // Load the FWSEC SB firmware, as well as its bootloader if required.
        let fwsec_sb = FwsecFirmware::new(dev, gsp_falcon, bios, FwsecCommand::Sb)?;
        let fwsec_sb = if self.needs_fwsec_bootloader {
            FwsecUnloadFirmware::WithBl(FwsecFirmwareWithBl::new(fwsec_sb, dev, chipset)?)
        } else {
            FwsecUnloadFirmware::WithoutBl(fwsec_sb)
        };

        KBox::new(
            Sec2UnloadBundle {
                fwsec_sb,
                booter_unloader: BooterFirmware::new(
                    dev,
                    BooterKind::Unloader,
                    chipset,
                    FIRMWARE_VERSION,
                    sec2_falcon,
                )?,
            },
            GFP_KERNEL,
        )
        .map(|b| crate::gsp::UnloadBundle(b))
        .map_err(Into::into)
    }
}

impl GspHal for Tu102 {
    fn boot(
        &self,
        gsp: &Gsp,
        ctx: &mut GspBootContext<'_, '_>,
        fb_layout: &FbLayout,
        wpr_meta: &Coherent<GspFwWprMeta>,
    ) -> Result<Option<crate::gsp::UnloadBundle>> {
        let dev = ctx.dev();
        let bar = ctx.bar;
        let chipset = ctx.chipset;
        let gsp_falcon = ctx.gsp_falcon;
        let sec2_falcon = ctx.sec2_falcon;

        let bios = Vbios::new(dev, bar)?;

        // Try and prepare the unload bundle.
        //
        // If the unload bundle creation fails, the GPU will need to be reset before the driver can
        // be probed again.
        let unload_bundle = self
            .build_unload_bundle(dev, chipset, &bios, gsp_falcon, sec2_falcon)
            .inspect_err(|e| dev_warn!(dev, "Failed to prepare unload firmware: {:?}\n", e))
            .ok();

        // Run the unload bundle to try and recover the GSP if an error occurs.
        let unload_guard = ScopeGuard::new_with_data(unload_bundle, |unload_bundle| {
            if let Some(unload_bundle) = unload_bundle {
                let _ = unload_bundle.0.run(ctx);
            }
        });

        // FWSEC-FRTS is not executed on chips where the FRTS region size is 0 (e.g. GA100).
        if !fb_layout.frts.is_empty() {
            self.run_fwsec_frts(dev, chipset, gsp_falcon, bar, &bios, fb_layout)?;
        }

        gsp_falcon.reset()?;
        let libos_handle = gsp.libos.dma_handle();
        let (mbox0, mbox1) =
            gsp_falcon.boot(Some(libos_handle as u32), Some((libos_handle >> 32) as u32))?;
        dev_dbg!(dev, "GSP MBOX0: {:#x}, MBOX1: {:#x}\n", mbox0, mbox1);

        dev_dbg!(
            dev,
            "Using SEC2 to load and run the booter_load firmware...\n"
        );

        BooterFirmware::new(
            dev,
            BooterKind::Loader,
            chipset,
            FIRMWARE_VERSION,
            sec2_falcon,
        )?
        .run(dev, sec2_falcon, wpr_meta)?;

        Ok(unload_guard.dismiss())
    }

    fn post_boot(
        &self,
        gsp: &Gsp,
        ctx: &mut GspBootContext<'_, '_>,
        gsp_fw: &GspFirmware,
    ) -> Result {
        GspSequencer::run(&gsp.cmdq, ctx, &gsp.libos, gsp_fw.bootloader.app_version)?;

        Ok(())
    }
}

/// The TU102 HAL requires the use of the FWSEC bootloader.
const TU102: Tu102 = Tu102 {
    needs_fwsec_bootloader: true,
};

pub(super) const TU102_HAL: &dyn GspHal = &TU102;
