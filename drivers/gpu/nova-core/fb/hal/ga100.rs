// SPDX-License-Identifier: GPL-2.0

use kernel::{
    io::Io,
    num::Bounded,
    prelude::*, //
};

use crate::{
    driver::Bar0,
    fb::hal::FbHal,
    regs, //
};

use super::tu102::FLUSH_SYSMEM_ADDR_SHIFT;

struct Ga100;

pub(super) fn read_sysmem_flush_page_ga100(bar: &Bar0) -> u64 {
    u64::from(bar.read(regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR).adr_39_08()) << FLUSH_SYSMEM_ADDR_SHIFT
        | u64::from(bar.read(regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI).adr_63_40())
            << FLUSH_SYSMEM_ADDR_SHIFT_HI
}

pub(super) fn write_sysmem_flush_page_ga100(bar: &Bar0, addr: u64) {
    bar.write_reg(
        regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI::zeroed().with_adr_63_40(
            Bounded::<u64, _>::from(addr)
                .shr::<FLUSH_SYSMEM_ADDR_SHIFT_HI, _>()
                .cast(),
        ),
    );

    bar.write_reg(
        regs::NV_PFB_NISO_FLUSH_SYSMEM_ADDR::zeroed()
            // CAST: `as u32` is used on purpose since we want to strip the upper bits that have
            // been written to `NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI`.
            .with_adr_39_08((addr >> FLUSH_SYSMEM_ADDR_SHIFT) as u32),
    );
}

pub(super) fn display_enabled_ga100(bar: &Bar0) -> bool {
    !bar.read(regs::ga100::NV_FUSE_STATUS_OPT_DISPLAY)
        .display_disabled()
}

/// Shift applied to the sysmem address before it is written into
/// `NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI`,
const FLUSH_SYSMEM_ADDR_SHIFT_HI: u32 = 40;

impl FbHal for Ga100 {
    fn read_sysmem_flush_page(&self, bar: &Bar0) -> u64 {
        read_sysmem_flush_page_ga100(bar)
    }

    fn write_sysmem_flush_page(&self, bar: &Bar0, addr: u64) -> Result {
        write_sysmem_flush_page_ga100(bar, addr);

        Ok(())
    }

    fn supports_display(&self, bar: &Bar0) -> bool {
        display_enabled_ga100(bar)
    }

    fn vidmem_size(&self, bar: &Bar0) -> u64 {
        super::tu102::vidmem_size_gp102(bar)
    }
}

const GA100: Ga100 = Ga100;
pub(super) const GA100_HAL: &dyn FbHal = &GA100;
