// SPDX-License-Identifier: GPL-2.0

use crate::driver::Bar0;
use crate::fb::hal::FbHal;
use crate::regs;
use kernel::prelude::*;

/// Shift applied to the sysmem address before it is written into `NV_PFB_NISO_FLUSH_SYSMEM_ADDR`,
/// to be used by HALs.
pub(super) const FLUSH_SYSMEM_ADDR_SHIFT: u32 = 8;

pub(super) fn read_sysmem_flush_page_gm107(bar: &Bar0) -> u64 {
    (regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR::read(bar).adr_39_08() as u64) << FLUSH_SYSMEM_ADDR_SHIFT
}

pub(super) fn write_sysmem_flush_page_gm107(bar: &Bar0, addr: u64) -> Result {
    // Check that the address doesn't overflow the receiving 32-bit register.
    if addr >> (u32::BITS + FLUSH_SYSMEM_ADDR_SHIFT) == 0 {
        regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR::default()
            .set_adr_39_08((addr >> FLUSH_SYSMEM_ADDR_SHIFT) as u32)
            .write(bar);

        Ok(())
    } else {
        Err(EINVAL)
    }
}

struct Tu102;

impl FbHal for Tu102 {
    fn read_sysmem_flush_page(&self, bar: &Bar0) -> u64 {
        read_sysmem_flush_page_gm107(bar)
    }

    fn write_sysmem_flush_page(&self, bar: &Bar0, addr: u64) -> Result {
        write_sysmem_flush_page_gm107(bar, addr)
    }
}

const TU102: Tu102 = Tu102;
pub(super) const TU102_HAL: &dyn FbHal = &TU102;
