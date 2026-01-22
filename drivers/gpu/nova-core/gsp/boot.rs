// SPDX-License-Identifier: GPL-2.0

use kernel::{
    device,
    dma::CoherentAllocation,
    dma_write,
    io::poll::read_poll_timeout,
    pci,
    prelude::*,
    time::Delta, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp,
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
            FwsecCommand,
            FwsecFirmware, //
        },
        gsp::GspFirmware,
        FIRMWARE_VERSION, //
    },
    gpu::Chipset,
    gsp::{
        commands,
        sequencer::{
            GspSequencer,
            GspSequencerParams, //
        },
        GspFwWprMeta, //
    },
    regs,
    vbios::Vbios,
};

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
                "FWSEC-FRTS returned with error code {:#x}\n",
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
        mut self: Pin<&mut Self>,
        pdev: &pci::Device<device::Bound>,
        bar: &Bar0,
        chipset: Chipset,
        gsp_falcon: &Falcon<Gsp>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        let dev = pdev.as_ref();

        let bios = Vbios::new(dev, bar)?;

        let gsp_fw = KBox::pin_init(GspFirmware::new(dev, chipset, FIRMWARE_VERSION), GFP_KERNEL)?;

        let fb_layout = FbLayout::new(chipset, bar, &gsp_fw)?;
        dev_dbg!(dev, "{:#x?}\n", fb_layout);

        Self::run_fwsec_frts(dev, gsp_falcon, bar, &bios, &fb_layout)?;

        let booter_loader = BooterFirmware::new(
            dev,
            BooterKind::Loader,
            chipset,
            FIRMWARE_VERSION,
            sec2_falcon,
            bar,
        )?;

        let wpr_meta =
            CoherentAllocation::<GspFwWprMeta>::alloc_coherent(dev, 1, GFP_KERNEL | __GFP_ZERO)?;
        dma_write!(wpr_meta[0] = GspFwWprMeta::new(&gsp_fw, &fb_layout))?;

        self.cmdq
            .send_command(bar, commands::SetSystemInfo::new(pdev))?;
        self.cmdq.send_command(bar, commands::SetRegistry::new())?;

        gsp_falcon.reset(bar)?;
        let libos_handle = self.libos.dma_handle();
        let (mbox0, mbox1) = gsp_falcon.boot(
            bar,
            Some(libos_handle as u32),
            Some((libos_handle >> 32) as u32),
        )?;
        dev_dbg!(
            pdev.as_ref(),
            "GSP MBOX0: {:#x}, MBOX1: {:#x}\n",
            mbox0,
            mbox1
        );

        dev_dbg!(
            pdev.as_ref(),
            "Using SEC2 to load and run the booter_load firmware...\n"
        );

        sec2_falcon.reset(bar)?;
        sec2_falcon.load(bar, &booter_loader)?;
        let wpr_handle = wpr_meta.dma_handle();
        let (mbox0, mbox1) = sec2_falcon.boot(
            bar,
            Some(wpr_handle as u32),
            Some((wpr_handle >> 32) as u32),
        )?;
        dev_dbg!(
            pdev.as_ref(),
            "SEC2 MBOX0: {:#x}, MBOX1{:#x}\n",
            mbox0,
            mbox1
        );

        if mbox0 != 0 {
            dev_err!(
                pdev.as_ref(),
                "Booter-load failed with error {:#x}\n",
                mbox0
            );
            return Err(ENODEV);
        }

        gsp_falcon.write_os_version(bar, gsp_fw.bootloader.app_version);

        // Poll for RISC-V to become active before running sequencer
        read_poll_timeout(
            || Ok(gsp_falcon.is_riscv_active(bar)),
            |val: &bool| *val,
            Delta::from_millis(10),
            Delta::from_secs(5),
        )?;

        dev_dbg!(
            pdev.as_ref(),
            "RISC-V active? {}\n",
            gsp_falcon.is_riscv_active(bar),
        );

        // Create and run the GSP sequencer.
        let seq_params = GspSequencerParams {
            bootloader_app_version: gsp_fw.bootloader.app_version,
            libos_dma_handle: libos_handle,
            gsp_falcon,
            sec2_falcon,
            dev: pdev.as_ref().into(),
            bar,
        };
        GspSequencer::run(&mut self.cmdq, seq_params)?;

        // Wait until GSP is fully initialized.
        commands::wait_gsp_init_done(&mut self.cmdq)?;

        // Obtain and display basic GPU information.
        let info = commands::get_gsp_info(&mut self.cmdq, bar)?;
        match info.gpu_name() {
            Ok(name) => dev_info!(pdev.as_ref(), "GPU name: {}\n", name),
            Err(e) => dev_warn!(pdev.as_ref(), "GPU name unavailable: {:?}\n", e),
        }

        Ok(())
    }
}
