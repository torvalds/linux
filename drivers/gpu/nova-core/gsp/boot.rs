// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::{
    bits,
    device,
    dma::Coherent,
    io::poll::read_poll_timeout,
    pci,
    prelude::*,
    time::Delta,
    types::ScopeGuard, //
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
        gsp::GspFirmware,
        FIRMWARE_VERSION, //
    },
    gpu::Chipset,
    gsp::{
        cmdq::Cmdq,
        commands,
        GspFwWprMeta, //
    },
};

/// Arguments required to call [`Gsp::unload`](super::Gsp::unload).
///
/// Stored as their own type to avoid repeating a long and tedious list in [`BootUnloadGuard`].
pub(super) struct BootUnloadArgs<'a> {
    gsp: &'a super::Gsp,
    dev: &'a device::Device<device::Bound>,
    bar: Bar0<'a>,
    gsp_falcon: &'a Falcon<Gsp>,
    sec2_falcon: &'a Falcon<Sec2>,
    unload_bundle: Option<super::UnloadBundle>,
}

/// Guard that calls [`Gsp::unload`](super::Gsp::unload) with a
/// [`UnloadBundle`](super::UnloadBundle) when dropped.
///
/// Used to ensure the `UnloadBundle` is run during failure paths.
pub(super) struct BootUnloadGuard<'a> {
    guard: ScopeGuard<BootUnloadArgs<'a>, fn(BootUnloadArgs<'a>)>,
}

impl<'a> BootUnloadGuard<'a> {
    /// Wraps `unload_bundle` into a guard that executes it when dropped.
    pub(super) fn new(
        gsp: &'a super::Gsp,
        dev: &'a device::Device<device::Bound>,
        bar: Bar0<'a>,
        gsp_falcon: &'a Falcon<Gsp>,
        sec2_falcon: &'a Falcon<Sec2>,
        unload_bundle: Option<super::UnloadBundle>,
    ) -> Self {
        Self {
            guard: ScopeGuard::new_with_data(
                BootUnloadArgs {
                    gsp,
                    dev,
                    bar,
                    gsp_falcon,
                    sec2_falcon,
                    unload_bundle,
                },
                |args| {
                    let _ = super::Gsp::unload(
                        args.gsp,
                        args.dev,
                        args.bar,
                        args.gsp_falcon,
                        args.sec2_falcon,
                        args.unload_bundle,
                    );
                },
            ),
        }
    }

    /// Disarms the guard and returns the [`UnloadBundle`](super::UnloadBundle) it contains.
    pub(super) fn dismiss(self) -> Option<super::UnloadBundle> {
        self.guard.dismiss().unload_bundle
    }
}

impl super::Gsp {
    /// Attempt to boot the GSP.
    ///
    /// This is a GPU-dependent and complex procedure that involves loading firmware files from
    /// user-space, patching them with signatures, and building firmware-specific intricate data
    /// structures that the GSP will use at runtime.
    ///
    /// Upon return, the GSP is up and running, and its unload bundle (to be given as argument to
    /// [`Self::unload`]) returned.
    pub(crate) fn boot(
        self: Pin<&mut Self>,
        pdev: &pci::Device<device::Bound>,
        bar: Bar0<'_>,
        chipset: Chipset,
        gsp_falcon: &Falcon<Gsp>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result<Option<super::UnloadBundle>> {
        let dev = pdev.as_ref();
        let hal = super::hal::gsp_hal(chipset);

        let gsp_fw = KBox::pin_init(GspFirmware::new(dev, chipset, FIRMWARE_VERSION), GFP_KERNEL)?;

        let fb_layout = FbLayout::new(chipset, bar, &gsp_fw)?;
        dev_dbg!(dev, "{:#x?}\n", fb_layout);

        let wpr_meta = Coherent::init(dev, GFP_KERNEL, GspFwWprMeta::new(&gsp_fw, &fb_layout))?;

        // Perform the chipset-specific boot sequence, and retrieve the unload bundle.
        let unload_guard = hal.boot(
            &self,
            dev,
            bar,
            chipset,
            &fb_layout,
            &wpr_meta,
            gsp_falcon,
            sec2_falcon,
        )?;

        gsp_falcon.write_os_version(bar, gsp_fw.bootloader.app_version);

        // Poll for RISC-V to become active before continuing.
        read_poll_timeout(
            || Ok(gsp_falcon.is_riscv_active(bar)),
            |val: &bool| *val,
            Delta::from_millis(10),
            Delta::from_secs(5),
        )?;

        dev_dbg!(pdev, "RISC-V active? {}\n", gsp_falcon.is_riscv_active(bar),);

        self.cmdq
            .send_command_no_wait(bar, commands::SetSystemInfo::new(pdev, chipset))?;
        self.cmdq
            .send_command_no_wait(bar, commands::SetRegistry::new())?;

        hal.post_boot(&self, dev, bar, &gsp_fw, gsp_falcon, sec2_falcon)?;

        // Wait until GSP is fully initialized.
        commands::wait_gsp_init_done(&self.cmdq)?;

        // Obtain and display basic GPU information.
        let info = self.cmdq.send_command(bar, commands::GetGspStaticInfo)?;
        match info.gpu_name() {
            Ok(name) => dev_info!(pdev, "GPU name: {}\n", name),
            Err(e) => dev_warn!(pdev, "GPU name unavailable: {:?}\n", e),
        }

        Ok(unload_guard.dismiss())
    }

    /// Shut down the GSP and wait until it is offline.
    fn shutdown_gsp(
        cmdq: &Cmdq,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<Gsp>,
        mode: commands::PowerStateLevel,
    ) -> Result {
        // Command to shut the GSP down.
        cmdq.send_command(bar, commands::UnloadingGuestDriver::new(mode))?;

        // Wait until GSP signals it is suspended.
        const LIBOS_INTERRUPT_PROCESSOR_SUSPENDED: u32 = bits::bit_u32(31);
        read_poll_timeout(
            || Ok(gsp_falcon.read_mailbox0(bar)),
            |&mb0| mb0 & LIBOS_INTERRUPT_PROCESSOR_SUSPENDED != 0,
            Delta::from_millis(10),
            Delta::from_secs(5),
        )
        .map(|_| ())
    }

    /// Attempts to unload the GSP firmware.
    ///
    /// This stops all activity on the GSP.
    pub(crate) fn unload(
        &self,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<Gsp>,
        sec2_falcon: &Falcon<Sec2>,
        unload_bundle: Option<super::UnloadBundle>,
    ) -> Result {
        // Shut down the GSP. Keep going even in case of error.
        let mut res = Self::shutdown_gsp(
            &self.cmdq,
            bar,
            gsp_falcon,
            commands::PowerStateLevel::Level0,
        )
        .inspect_err(|e| dev_err!(dev, "GSP shutdown failed: {:?}\n", e));

        // Run the unload bundle to reset the GSP so it can be booted again.
        if let Some(unload_bundle) = unload_bundle {
            res = res.and(
                unload_bundle
                    .0
                    .run(dev, bar, gsp_falcon, sec2_falcon)
                    .inspect_err(|e| dev_err!(dev, "Unload bundle failed: {:?}\n", e)),
            );
        } else {
            dev_warn!(
                dev,
                "Unload bundle is missing, GSP won't be properly reset.\n"
            );

            res = Err(EAGAIN);
        }

        res.inspect(|()| dev_info!(dev, "GSP successfully unloaded\n"))
    }
}
