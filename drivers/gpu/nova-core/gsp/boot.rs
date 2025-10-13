// SPDX-License-Identifier: GPL-2.0

use kernel::device;
use kernel::pci;
use kernel::prelude::*;

use crate::driver::Bar0;
use crate::falcon::{gsp::Gsp, sec2::Sec2, Falcon};
use crate::fb::FbLayout;
use crate::firmware::{
    booter::{BooterFirmware, BooterKind},
    fwsec::{FwsecCommand, FwsecFirmware},
    gsp::GspFirmware,
    FIRMWARE_VERSION,
};
use crate::gpu::Chipset;
use crate::regs;
use crate::vbios::Vbios;

impl super::Gsp {
    /// Helper function to load and run the FWSEC-FRTS firmware and confirm that it has properly
    /// created the WPR2 region.
    fn run_fwsec_frts(
        dev: &device::Device<device::Bound>,
        falcon: &Falcon<Gsp>,
        bar: &Bar0,
        bios: &Vbios,
        fb_layout: &FbLayout,
    ) -> Result<()> {
        // Check that the WPR2 region does not already exists - if it does, we cannot run
        // FWSEC-FRTS until the GPU is reset.
        if regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI::read(bar).higher_bound() != 0 {
            dev_err!(
                dev,
                "WPR2 region already exists - GPU needs to be reset to proceed\n"
            );
            return Err(EBUSY);
        }

        let fwsec_frts = FwsecFirmware::new(
            dev,
            falcon,
            bar,
            bios,
            FwsecCommand::Frts {
                frts_addr: fb_layout.frts.start,
                frts_size: fb_layout.frts.end - fb_layout.frts.start,
            },
        )?;

        // Run FWSEC-FRTS to create the WPR2 region.
        fwsec_frts.run(dev, falcon, bar)?;

        // SCRATCH_E contains the error code for FWSEC-FRTS.
        let frts_status = regs::NV_PBUS_SW_SCRATCH_0E_FRTS_ERR::read(bar).frts_err_code();
        if frts_status != 0 {
            dev_err!(
                dev,
                "FWSEC-FRTS returned with error code {:#x}",
                frts_status
            );

            return Err(EIO);
        }

        // Check that the WPR2 region has been created as we requested.
        let (wpr2_lo, wpr2_hi) = (
            regs::NV_PFB_PRI_MMU_WPR2_ADDR_LO::read(bar).lower_bound(),
            regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI::read(bar).higher_bound(),
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

    /// Attempt to boot the GSP.
    ///
    /// This is a GPU-dependent and complex procedure that involves loading firmware files from
    /// user-space, patching them with signatures, and building firmware-specific intricate data
    /// structures that the GSP will use at runtime.
    ///
    /// Upon return, the GSP is up and running, and its runtime object given as return value.
    pub(crate) fn boot(
        self: Pin<&mut Self>,
        pdev: &pci::Device<device::Bound>,
        bar: &Bar0,
        chipset: Chipset,
        gsp_falcon: &Falcon<Gsp>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        let dev = pdev.as_ref();

        let bios = Vbios::new(dev, bar)?;

        let _gsp_fw = KBox::pin_init(
            GspFirmware::new(dev, chipset, FIRMWARE_VERSION)?,
            GFP_KERNEL,
        )?;

        let fb_layout = FbLayout::new(chipset, bar)?;
        dev_dbg!(dev, "{:#x?}\n", fb_layout);

        Self::run_fwsec_frts(dev, gsp_falcon, bar, &bios, &fb_layout)?;

        let _booter_loader = BooterFirmware::new(
            dev,
            BooterKind::Loader,
            chipset,
            FIRMWARE_VERSION,
            sec2_falcon,
            bar,
        )?;

        Ok(())
    }
}
