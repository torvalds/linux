// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

mod gh100;
mod tu102;

use kernel::prelude::*;

use kernel::{
    device,
    dma::Coherent, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp as GspEngine,
        sec2::Sec2,
        Falcon, //
    },
    fb::FbLayout,
    firmware::gsp::GspFirmware,
    gpu::{
        Architecture,
        Chipset, //
    },
    gsp::{
        boot::BootUnloadGuard,
        Gsp,
        GspFwWprMeta, //
    },
};

/// Trait for types containing the resources and code required to fully reset the GSP.
///
/// The GSP unload code might run in a situation where we cannot load firmware dynamically (e.g.
/// because we are in shutdown and the file system is not accessible anymore). Thus, the firmware
/// required for unloading is prepared at load time, and stored here until it needs to be run.
pub(super) trait UnloadBundle: Send {
    /// Performs the steps required to properly reset the GSP after it has been stopped.
    fn run(
        &self,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        gsp_falcon: &Falcon<GspEngine>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result;
}

/// Trait implemented by GSP HALs.
pub(super) trait GspHal: Send {
    /// Performs the GSP boot process, loading and running the required firmwares as needed.
    ///
    /// Upon success, returns a guard that runs the GSP unload sequence if GSP boot does not
    /// complete.
    #[allow(clippy::too_many_arguments)]
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
    ) -> Result<BootUnloadGuard<'a>>;

    /// Performs HAL-specific post-GSP boot tasks.
    ///
    /// This method is called by the GSP boot code after the GSP is confirmed to be running, and
    /// after the initialization commands have been pushed onto its queue.
    fn post_boot(
        &self,
        _gsp: &Gsp,
        _dev: &device::Device<device::Bound>,
        _bar: Bar0<'_>,
        _gsp_fw: &GspFirmware,
        _gsp_falcon: &Falcon<GspEngine>,
        _sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        Ok(())
    }
}

/// Returns the GSP HAL to be used for `chipset`.
pub(super) fn gsp_hal(chipset: Chipset) -> &'static dyn GspHal {
    match chipset.arch() {
        Architecture::Turing | Architecture::Ampere | Architecture::Ada => tu102::TU102_HAL,
        Architecture::Hopper | Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => {
            gh100::GH100_HAL
        }
    }
}
