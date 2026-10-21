// SPDX-License-Identifier: GPL-2.0

use crate::gpu::{
    Architecture,
    Chipset, //
};

mod gb202;
mod tu102;

pub(super) trait VgpuHal {
    /// Returns whether this chipset can support vGPU.
    fn supports_vgpu(&self) -> bool;
}

pub(super) fn vgpu_hal(chipset: Chipset) -> &'static dyn VgpuHal {
    match chipset.arch() {
        Architecture::BlackwellGB20x => gb202::GB202_HAL,
        Architecture::Turing
        | Architecture::Ampere
        | Architecture::Hopper
        | Architecture::Ada
        | Architecture::BlackwellGB10x => tu102::TU102_HAL,
    }
}
