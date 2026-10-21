// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

mod ga102;
mod gh100;
mod tu102;

use kernel::prelude::*;

use crate::{
    firmware::gsp::GspFirmware,
    gpu::{
        Architecture,
        Chipset, //
    },
    gsp::{
        Gsp,
        GspBootContext, //
    },
};

/// Trait for types containing the resources and code required to fully reset the GSP.
///
/// The GSP unload code might run in a situation where we cannot load firmware dynamically (e.g.
/// because we are in shutdown and the file system is not accessible anymore). Thus, the firmware
/// required for unloading is prepared at load time, and stored here until it needs to be run.
pub(super) trait UnloadBundle: Send {
    /// Performs the steps required to properly reset the GSP after it has been stopped.
    fn run(&self, ctx: &mut GspBootContext<'_, '_>) -> Result;
}

/// Trait implemented by GSP HALs.
pub(super) trait GspHal: Send {
    /// Performs the GSP boot process, loading and running the required firmwares as needed.
    ///
    /// Upon success, returns the [`crate::gsp::UnloadBundle`] to use with [`Gsp::unload`], if one
    /// could be created.
    fn boot(
        &self,
        gsp: &Gsp,
        ctx: &mut GspBootContext<'_, '_>,
        gsp_fw: &GspFirmware,
    ) -> Result<Option<crate::gsp::UnloadBundle>>;

    /// Performs HAL-specific post-GSP boot tasks.
    ///
    /// This method is called by the GSP boot code after the GSP is confirmed to be running, and
    /// after the initialization commands have been pushed onto its queue.
    fn post_boot(
        &self,
        _gsp: &Gsp,
        _ctx: &mut GspBootContext<'_, '_>,
        _gsp_fw: &GspFirmware,
    ) -> Result {
        Ok(())
    }
}

/// Returns the names of the firmware files required to boot the GSP of `chipset`, in addition to
/// the "bootloader" and "gsp" images required by all chipsets.
pub(crate) const fn boot_firmware_files(chipset: Chipset) -> &'static [&'static str] {
    match chipset.arch() {
        // Turing chipsets boot the GSP via the SEC2 Booter, and require the FWSEC bootloader.
        Architecture::Turing => &["booter_load.tlv", "booter_unload.tlv", "gen_bootloader.tlv"],
        // GA100 also requires the FWSEC bootloader.
        Architecture::Ampere if matches!(chipset, Chipset::GA100) => {
            &["booter_load.tlv", "booter_unload.tlv", "gen_bootloader.tlv"]
        }
        // Other Ampere chipsets, as well as Ada chipsets, run FWSEC directly.
        Architecture::Ampere | Architecture::Ada => &["booter_load.tlv", "booter_unload.tlv"],
        // Hopper and later chipsets boot the GSP via the FMC image loaded by FSP.
        Architecture::Hopper | Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => {
            &["fmc.tlv"]
        }
    }
}

/// Returns the GSP HAL to be used for `chipset`.
pub(super) fn gsp_hal(chipset: Chipset) -> &'static dyn GspHal {
    match chipset.arch() {
        Architecture::Turing => tu102::TU102_HAL,
        Architecture::Ampere if matches!(chipset, Chipset::GA100) => tu102::TU102_HAL,
        Architecture::Ampere | Architecture::Ada => ga102::GA102_HAL,
        Architecture::Hopper | Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => {
            gh100::GH100_HAL
        }
    }
}
