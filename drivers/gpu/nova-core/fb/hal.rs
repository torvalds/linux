// SPDX-License-Identifier: GPL-2.0

use kernel::prelude::*;

use crate::driver::Bar0;
use crate::gpu::Chipset;

mod ga100;
mod ga102;
mod tu102;

pub(crate) trait FbHal {
    /// Returns the address of the currently-registered sysmem flush page.
    fn read_sysmem_flush_page(&self, bar: &Bar0) -> u64;

    /// Register `addr` as the address of the sysmem flush page.
    ///
    /// This might fail if the address is too large for the receiving register.
    fn write_sysmem_flush_page(&self, bar: &Bar0, addr: u64) -> Result;

    /// Returns `true` is display is supported.
    fn supports_display(&self, bar: &Bar0) -> bool;

    /// Returns the VRAM size, in bytes.
    fn vidmem_size(&self, bar: &Bar0) -> u64;
}

/// Returns the HAL corresponding to `chipset`.
pub(super) fn fb_hal(chipset: Chipset) -> &'static dyn FbHal {
    use Chipset::*;

    match chipset {
        TU102 | TU104 | TU106 | TU117 | TU116 => tu102::TU102_HAL,
        GA100 => ga100::GA100_HAL,
        GA102 | GA103 | GA104 | GA106 | GA107 | AD102 | AD103 | AD104 | AD106 | AD107 => {
            ga102::GA102_HAL
        }
    }
}
