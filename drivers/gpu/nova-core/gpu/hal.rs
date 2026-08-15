// SPDX-License-Identifier: GPL-2.0

use core::ops::Range;

use kernel::{
    dma::DmaMask,
    prelude::*, //
};

use crate::{
    driver::Bar0,
    gpu::{
        Architecture,
        Chipset, //
    },
};

mod gh100;
mod tu102;

pub(crate) trait GpuHal {
    /// Waits for GFW_BOOT completion if required by this hardware family.
    fn wait_gfw_boot_completion(&self, bar: Bar0<'_>) -> Result;

    /// Returns the DMA mask for the current architecture.
    fn dma_mask(&self) -> DmaMask;

    /// Returns the address range of the PCI config mirror space.
    fn pci_config_mirror_range(&self) -> Range<u32>;
}

pub(super) fn gpu_hal(chipset: Chipset) -> &'static dyn GpuHal {
    match chipset.arch() {
        Architecture::Turing | Architecture::Ampere | Architecture::Ada => tu102::TU102_HAL,
        Architecture::Hopper | Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => {
            gh100::GH100_HAL
        }
    }
}
