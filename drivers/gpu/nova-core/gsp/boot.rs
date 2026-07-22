// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::{
    bits,
    dma::Coherent,
    io::poll::read_poll_timeout,
    prelude::*,
    time::Delta,
    types::ScopeGuard, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp,
        Falcon, //
    },
    fb::FbLayout,
    firmware::{
        gsp::GspFirmware,
        FIRMWARE_VERSION, //
    },
    gsp::{
        cmdq::Cmdq,
        commands,
        GspFwWprMeta, //
    },
};

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
        mut ctx: super::GspBootContext<'_, '_>,
    ) -> Result<Option<super::UnloadBundle>> {
        let pdev = ctx.pdev;
        let bar = ctx.bar;
        let chipset = ctx.chipset;
        let gsp_falcon = ctx.gsp_falcon;
        let dev = pdev.as_ref();
        let hal = super::hal::gsp_hal(chipset);

        let gsp_fw = KBox::pin_init(GspFirmware::new(dev, chipset, FIRMWARE_VERSION), GFP_KERNEL)?;

        let fb_layout = FbLayout::new(chipset, bar, &gsp_fw, ctx.vgpu.state())?;
        dev_dbg!(dev, "{:#x?}\n", fb_layout);

        let wpr_meta = Coherent::init(dev, GFP_KERNEL, GspFwWprMeta::new(&gsp_fw, &fb_layout))?;

        // Perform the chipset-specific boot sequence, and retrieve the unload bundle.
        let unload_bundle = hal
            .boot(&self, &mut ctx, &fb_layout, &wpr_meta)?
            .or_else(|| {
                dev_warn!(dev, "The GSP won't be able to unload properly on unbind.\n");
                dev_warn!(
                    dev,
                    "The GPU will need to be reset before the driver can bind again.\n"
                );

                None
            });

        let mut unload_guard =
            ScopeGuard::new_with_data((ctx, unload_bundle), |(ctx, unload_bundle)| {
                let _ = self.unload(ctx, unload_bundle);
            });
        let ctx = &mut unload_guard.0;

        gsp_falcon.write_os_version(gsp_fw.bootloader.app_version);

        // Poll for RISC-V to become active before continuing.
        read_poll_timeout(
            || Ok(gsp_falcon.is_riscv_active()),
            |val: &bool| *val,
            Delta::from_millis(10),
            Delta::from_secs(5),
        )?;

        dev_dbg!(pdev, "RISC-V active? {}\n", gsp_falcon.is_riscv_active(),);

        self.cmdq
            .send_command_no_wait(bar, commands::SetSystemInfo::new(pdev, chipset))?;
        self.cmdq
            .send_command_no_wait(bar, commands::SetRegistry::new(ctx.vgpu.state())?)?;

        hal.post_boot(&self, ctx, &gsp_fw)?;

        // Wait until GSP is fully initialized.
        commands::wait_gsp_init_done(&self.cmdq)?;

        Ok(unload_guard.dismiss().1)
    }

    /// Shut down the GSP and wait until it is offline.
    fn shutdown_gsp(
        cmdq: &Cmdq,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<'_, Gsp>,
        mode: commands::PowerStateLevel,
    ) -> Result {
        // Command to shut the GSP down.
        cmdq.send_command(bar, commands::UnloadingGuestDriver::new(mode))?;

        // Wait until GSP signals it is suspended.
        const LIBOS_INTERRUPT_PROCESSOR_SUSPENDED: u32 = bits::bit_u32(31);
        read_poll_timeout(
            || Ok(gsp_falcon.read_mailbox0()),
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
        mut ctx: super::GspBootContext<'_, '_>,
        unload_bundle: Option<super::UnloadBundle>,
    ) -> Result {
        let dev = ctx.dev();

        // Shut down the GSP. Keep going even in case of error.
        let mut res = Self::shutdown_gsp(
            &self.cmdq,
            ctx.bar,
            ctx.gsp_falcon,
            commands::PowerStateLevel::Level0,
        )
        .inspect_err(|e| dev_err!(dev, "GSP shutdown failed: {:?}\n", e));

        // Run the unload bundle to reset the GSP so it can be booted again.
        if let Some(unload_bundle) = unload_bundle {
            res = res.and(
                unload_bundle
                    .0
                    .run(&mut ctx)
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
