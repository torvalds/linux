// SPDX-License-Identifier: GPL-2.0

use crate::driver::Bar0;

// TODO
//
// Create register definitions via generic macros. See task "Generic register
// abstraction" in Documentation/gpu/nova/core/todo.rst.

const BOOT0_OFFSET: usize = 0x00000000;

// 3:0 - chipset minor revision
const BOOT0_MINOR_REV_SHIFT: u8 = 0;
const BOOT0_MINOR_REV_MASK: u32 = 0x0000000f;

// 7:4 - chipset major revision
const BOOT0_MAJOR_REV_SHIFT: u8 = 4;
const BOOT0_MAJOR_REV_MASK: u32 = 0x000000f0;

// 23:20 - chipset implementation Identifier (depends on architecture)
const BOOT0_IMPL_SHIFT: u8 = 20;
const BOOT0_IMPL_MASK: u32 = 0x00f00000;

// 28:24 - chipset architecture identifier
const BOOT0_ARCH_MASK: u32 = 0x1f000000;

// 28:20 - chipset identifier (virtual register field combining BOOT0_IMPL and
//         BOOT0_ARCH)
const BOOT0_CHIPSET_SHIFT: u8 = BOOT0_IMPL_SHIFT;
const BOOT0_CHIPSET_MASK: u32 = BOOT0_IMPL_MASK | BOOT0_ARCH_MASK;

#[derive(Copy, Clone)]
pub(crate) struct Boot0(u32);

impl Boot0 {
    #[inline]
    pub(crate) fn read(bar: &Bar0) -> Self {
        Self(bar.readl(BOOT0_OFFSET))
    }

    #[inline]
    pub(crate) fn chipset(&self) -> u32 {
        (self.0 & BOOT0_CHIPSET_MASK) >> BOOT0_CHIPSET_SHIFT
    }

    #[inline]
    pub(crate) fn minor_rev(&self) -> u8 {
        ((self.0 & BOOT0_MINOR_REV_MASK) >> BOOT0_MINOR_REV_SHIFT) as u8
    }

    #[inline]
    pub(crate) fn major_rev(&self) -> u8 {
        ((self.0 & BOOT0_MAJOR_REV_MASK) >> BOOT0_MAJOR_REV_SHIFT) as u8
    }
}
