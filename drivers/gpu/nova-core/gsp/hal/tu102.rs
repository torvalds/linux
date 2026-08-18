// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::prelude::*;

use kernel::{
    device,
    dma::Coherent,
    io::Io, //
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
        boot::BootUnloadGuard,
        hal::{
            GspHal,
            UnloadBundle, //
        },
        sequencer::{
            GspSequencer,
            GspSequencerParams, //
        },
        Gsp,
        GspFwWprMeta, //
    },
    regs,
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
    /// Loads the FWSEC SB firmware, as well as its bootloader if `chipset` requires it.
    fn new(
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        chipset: Chipset,
        bios: &Vbios,
        gsp_falcon: &Falcon<GspEngine>,
    ) -> Result<Self> {
        let fwsec_sb = FwsecFirmware::new(dev, gsp_falcon, bar, bios, FwsecCommand::Sb)?;

        Ok(if chipset.needs_fwsec_bootloader() {
            Self::WithBl(FwsecFirmwareWithBl::new(fwsec_sb, dev, chipset)?)
        } else {
            Self::WithoutBl(fwsec_sb)
        })
    }

    /// Runs the FWSEC SB firmware.
    fn run(
        &self,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<GspEngine>,
    ) -> Result {
        match self {
            Self::WithoutBl(fw) => fw.run(dev, gsp_falcon, bar),
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

impl Sec2UnloadBundle {
    /// Load and prepare the resources required to properly reset the GSP after it has been stopped.
    fn build(
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        chipset: Chipset,
        bios: &Vbios,
        gsp_falcon: &Falcon<GspEngine>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result<KBox<dyn UnloadBundle>> {
        KBox::new(
            Self {
                fwsec_sb: FwsecUnloadFirmware::new(dev, bar, chipset, bios, gsp_falcon)?,
                booter_unloader: BooterFirmware::new(
                    dev,
                    BooterKind::Unloader,
                    chipset,
                    FIRMWARE_VERSION,
                    sec2_falcon,
                    bar,
                )?,
            },
            GFP_KERNEL,
        )
        .map(|b| b as KBox<dyn UnloadBundle>)
        .map_err(Into::into)
    }
}

impl UnloadBundle for Sec2UnloadBundle {
    fn run(
        &self,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<GspEngine>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        // Run FWSEC-SB to reset the GSP falcon to its pre-libos state.
        self.fwsec_sb.run(dev, bar, gsp_falcon)?;

        // Remove WPR2 region if set.
        let wpr2_hi = bar.read(regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI);
        if wpr2_hi.is_wpr2_set() {
            sec2_falcon.reset(bar)?;
            sec2_falcon.load(dev, bar, &self.booter_unloader)?;

            // Sentinel value to confirm that Booter Unloader has run.
            const MAILBOX_SENTINEL: u32 = 0xff;
            let (mbox0, _) =
                sec2_falcon.boot(bar, Some(MAILBOX_SENTINEL), Some(MAILBOX_SENTINEL))?;
            if mbox0 != 0 {
                dev_err!(dev, "Booter Unloader returned error 0x{:x}\n", mbox0);
                return Err(EINVAL);
            }

            // Confirm that the WPR2 region has been removed.
            let wpr2_hi = bar.read(regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI);
            if wpr2_hi.is_wpr2_set() {
                dev_err!(
                    dev,
                    "WPR2 region still set after Booter Unloader returned\n"
                );
                return Err(EBUSY);
            }
        }

        Ok(())
    }
}

/// Helper function to load and run the FWSEC-FRTS firmware and confirm that it has properly
/// created the WPR2 region.
fn run_fwsec_frts(
    dev: &device::Device<device::Bound>,
    chipset: Chipset,
    falcon: &Falcon<GspEngine>,
    bar: Bar0<'_>,
    bios: &Vbios,
    fb_layout: &FbLayout,
) -> Result {
    // Check that the WPR2 region does not already exist - if it does, we cannot run
    // FWSEC-FRTS until the GPU is reset.
    if bar.read(regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI).higher_bound() != 0 {
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
        bar,
        bios,
        FwsecCommand::Frts {
            frts_addr: fb_layout.frts.start,
            frts_size: fb_layout.frts.len(),
        },
    )?;

    if chipset.needs_fwsec_bootloader() {
        let fwsec_frts_bl = FwsecFirmwareWithBl::new(fwsec_frts, dev, chipset)?;
        // Load and run the bootloader, which will load FWSEC-FRTS and run it.
        fwsec_frts_bl.run(dev, falcon, bar)?;
    } else {
        // Load and run FWSEC-FRTS directly.
        fwsec_frts.run(dev, falcon, bar)?;
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
    let (wpr2_lo, wpr2_hi) = (
        bar.read(regs::NV_PFB_PRI_MMU_WPR2_ADDR_LO).lower_bound(),
        bar.read(regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI).higher_bound(),
    );

    match (wpr2_lo, wpr2_hi) {
        (_, 0) => {
            dev_err!(dev, "WPR2 region not created after running FWSEC-FRTS\n");

            Err(EIO)
        }
        (wpr2_lo, _) if wpr2_lo != fb_layout.frts.start => {
            dev_err!(
                dev,
                "WPR2 region created at unexpected address {:#x}; expected {:#x}\n",
                wpr2_lo,
                fb_layout.frts.start,
            );

            Err(EIO)
        }
        (wpr2_lo, wpr2_hi) => {
            dev_dbg!(dev, "WPR2: {:#x}-{:#x}\n", wpr2_lo, wpr2_hi);
            dev_dbg!(dev, "GPU instance built\n");

            Ok(())
        }
    }
}

struct Tu102;

impl GspHal for Tu102 {
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
        let bios = Vbios::new(dev, bar)?;

        // Try and prepare the unload bundle.
        //
        // If the unload bundle creation fails, the GPU will need to be reset before the driver can
        // be probed again.
        let unload_bundle =
            Sec2UnloadBundle::build(dev, bar, chipset, &bios, gsp_falcon, sec2_falcon)
                .inspect_err(|e| {
                    dev_warn!(dev, "Failed to prepare unload firmware: {:?}\n", e);
                    dev_warn!(dev, "The GSP won't be able to unload properly on unbind.\n");
                    dev_warn!(
                        dev,
                        "The GPU will need to be reset before the driver can bind again.\n"
                    );
                })
                .ok()
                .map(crate::gsp::UnloadBundle);

        // Wrap the unload bundle into a drop guard so it is automatically run upon failure.
        let unload_guard =
            BootUnloadGuard::new(gsp, dev, bar, gsp_falcon, sec2_falcon, unload_bundle);

        // FWSEC-FRTS is not executed on chips where the FRTS region size is 0 (e.g. GA100).
        if !fb_layout.frts.is_empty() {
            run_fwsec_frts(dev, chipset, gsp_falcon, bar, &bios, fb_layout)?;
        }

        gsp_falcon.reset(bar)?;
        let libos_handle = gsp.libos.dma_handle();
        let (mbox0, mbox1) = gsp_falcon.boot(
            bar,
            Some(libos_handle as u32),
            Some((libos_handle >> 32) as u32),
        )?;
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
            bar,
        )?
        .run(dev, bar, sec2_falcon, wpr_meta)?;

        Ok(unload_guard)
    }

    fn post_boot(
        &self,
        gsp: &Gsp,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        gsp_fw: &GspFirmware,
        gsp_falcon: &Falcon<GspEngine>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        // Create and run the GSP sequencer.
        let seq_params = GspSequencerParams {
            bootloader_app_version: gsp_fw.bootloader.app_version,
            libos_dma_handle: gsp.libos.dma_handle(),
            gsp_falcon,
            sec2_falcon,
            dev,
            bar,
        };
        GspSequencer::run(&gsp.cmdq, seq_params)?;

        Ok(())
    }
}

const TU102: Tu102 = Tu102;
pub(super) const TU102_HAL: &dyn GspHal = &TU102;
