// SPDX-License-Identifier: GPL-2.0 or MIT

// We don't expect that all the registers and fields will be used, even in the
// future.
//
// Nevertheless, it is useful to have most of them defined, like the C driver
// does.
#![allow(dead_code)]

use kernel::bits::bit_u32;
use kernel::device::Bound;
use kernel::device::Device;
use kernel::devres::Devres;
use kernel::prelude::*;

use crate::driver::IoMem;

/// Represents a register in the Register Set
///
/// TODO: Replace this with the Nova `register!()` macro when it is available.
/// In particular, this will automatically give us 64bit register reads and
/// writes.
pub(crate) struct Register<const OFFSET: usize>;

impl<const OFFSET: usize> Register<OFFSET> {
    #[inline]
    pub(crate) fn read(&self, dev: &Device<Bound>, iomem: &Devres<IoMem>) -> Result<u32> {
        let value = (*iomem).access(dev)?.read32(OFFSET);
        Ok(value)
    }

    #[inline]
    pub(crate) fn write(&self, dev: &Device<Bound>, iomem: &Devres<IoMem>, value: u32) -> Result {
        (*iomem).access(dev)?.write32(value, OFFSET);
        Ok(())
    }
}

pub(crate) const GPU_ID: Register<0x0> = Register;
pub(crate) const GPU_L2_FEATURES: Register<0x4> = Register;
pub(crate) const GPU_CORE_FEATURES: Register<0x8> = Register;
pub(crate) const GPU_CSF_ID: Register<0x1c> = Register;
pub(crate) const GPU_REVID: Register<0x280> = Register;
pub(crate) const GPU_TILER_FEATURES: Register<0xc> = Register;
pub(crate) const GPU_MEM_FEATURES: Register<0x10> = Register;
pub(crate) const GPU_MMU_FEATURES: Register<0x14> = Register;
pub(crate) const GPU_AS_PRESENT: Register<0x18> = Register;
pub(crate) const GPU_IRQ_RAWSTAT: Register<0x20> = Register;

pub(crate) const GPU_IRQ_RAWSTAT_FAULT: u32 = bit_u32(0);
pub(crate) const GPU_IRQ_RAWSTAT_PROTECTED_FAULT: u32 = bit_u32(1);
pub(crate) const GPU_IRQ_RAWSTAT_RESET_COMPLETED: u32 = bit_u32(8);
pub(crate) const GPU_IRQ_RAWSTAT_POWER_CHANGED_SINGLE: u32 = bit_u32(9);
pub(crate) const GPU_IRQ_RAWSTAT_POWER_CHANGED_ALL: u32 = bit_u32(10);
pub(crate) const GPU_IRQ_RAWSTAT_CLEAN_CACHES_COMPLETED: u32 = bit_u32(17);
pub(crate) const GPU_IRQ_RAWSTAT_DOORBELL_STATUS: u32 = bit_u32(18);
pub(crate) const GPU_IRQ_RAWSTAT_MCU_STATUS: u32 = bit_u32(19);

pub(crate) const GPU_IRQ_CLEAR: Register<0x24> = Register;
pub(crate) const GPU_IRQ_MASK: Register<0x28> = Register;
pub(crate) const GPU_IRQ_STAT: Register<0x2c> = Register;
pub(crate) const GPU_CMD: Register<0x30> = Register;
pub(crate) const GPU_CMD_SOFT_RESET: u32 = 1 | (1 << 8);
pub(crate) const GPU_CMD_HARD_RESET: u32 = 1 | (2 << 8);
pub(crate) const GPU_THREAD_FEATURES: Register<0xac> = Register;
pub(crate) const GPU_THREAD_MAX_THREADS: Register<0xa0> = Register;
pub(crate) const GPU_THREAD_MAX_WORKGROUP_SIZE: Register<0xa4> = Register;
pub(crate) const GPU_THREAD_MAX_BARRIER_SIZE: Register<0xa8> = Register;
pub(crate) const GPU_TEXTURE_FEATURES0: Register<0xb0> = Register;
pub(crate) const GPU_SHADER_PRESENT_LO: Register<0x100> = Register;
pub(crate) const GPU_SHADER_PRESENT_HI: Register<0x104> = Register;
pub(crate) const GPU_TILER_PRESENT_LO: Register<0x110> = Register;
pub(crate) const GPU_TILER_PRESENT_HI: Register<0x114> = Register;
pub(crate) const GPU_L2_PRESENT_LO: Register<0x120> = Register;
pub(crate) const GPU_L2_PRESENT_HI: Register<0x124> = Register;
pub(crate) const L2_READY_LO: Register<0x160> = Register;
pub(crate) const L2_READY_HI: Register<0x164> = Register;
pub(crate) const L2_PWRON_LO: Register<0x1a0> = Register;
pub(crate) const L2_PWRON_HI: Register<0x1a4> = Register;
pub(crate) const L2_PWRTRANS_LO: Register<0x220> = Register;
pub(crate) const L2_PWRTRANS_HI: Register<0x204> = Register;
pub(crate) const L2_PWRACTIVE_LO: Register<0x260> = Register;
pub(crate) const L2_PWRACTIVE_HI: Register<0x264> = Register;

pub(crate) const MCU_CONTROL: Register<0x700> = Register;
pub(crate) const MCU_CONTROL_ENABLE: u32 = 1;
pub(crate) const MCU_CONTROL_AUTO: u32 = 2;
pub(crate) const MCU_CONTROL_DISABLE: u32 = 0;

pub(crate) const MCU_STATUS: Register<0x704> = Register;
pub(crate) const MCU_STATUS_DISABLED: u32 = 0;
pub(crate) const MCU_STATUS_ENABLED: u32 = 1;
pub(crate) const MCU_STATUS_HALT: u32 = 2;
pub(crate) const MCU_STATUS_FATAL: u32 = 3;

pub(crate) const GPU_COHERENCY_FEATURES: Register<0x300> = Register;

pub(crate) const JOB_IRQ_RAWSTAT: Register<0x1000> = Register;
pub(crate) const JOB_IRQ_CLEAR: Register<0x1004> = Register;
pub(crate) const JOB_IRQ_MASK: Register<0x1008> = Register;
pub(crate) const JOB_IRQ_STAT: Register<0x100c> = Register;

pub(crate) const JOB_IRQ_GLOBAL_IF: u32 = bit_u32(31);

pub(crate) const MMU_IRQ_RAWSTAT: Register<0x2000> = Register;
pub(crate) const MMU_IRQ_CLEAR: Register<0x2004> = Register;
pub(crate) const MMU_IRQ_MASK: Register<0x2008> = Register;
pub(crate) const MMU_IRQ_STAT: Register<0x200c> = Register;
