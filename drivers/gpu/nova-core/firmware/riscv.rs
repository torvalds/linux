// SPDX-License-Identifier: GPL-2.0

//! Support for firmware binaries designed to run on a RISC-V core. Such firmwares files have a
//! dedicated header.

use kernel::{
    device,
    firmware::Firmware,
    prelude::*,
    transmute::FromBytes, //
};

use crate::{
    dma::DmaObject,
    firmware::BinFirmware,
    num::FromSafeCast, //
};

/// Descriptor for microcode running on a RISC-V core.
#[repr(C)]
#[derive(Debug)]
struct RmRiscvUCodeDesc {
    version: u32,
    bootloader_offset: u32,
    bootloader_size: u32,
    bootloader_param_offset: u32,
    bootloader_param_size: u32,
    riscv_elf_offset: u32,
    riscv_elf_size: u32,
    app_version: u32,
    manifest_offset: u32,
    manifest_size: u32,
    monitor_data_offset: u32,
    monitor_data_size: u32,
    monitor_code_offset: u32,
    monitor_code_size: u32,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
unsafe impl FromBytes for RmRiscvUCodeDesc {}

impl RmRiscvUCodeDesc {
    /// Interprets the header of `bin_fw` as a [`RmRiscvUCodeDesc`] and returns it.
    ///
    /// Fails if the header pointed at by `bin_fw` is not within the bounds of the firmware image.
    fn new(bin_fw: &BinFirmware<'_>) -> Result<Self> {
        let offset = usize::from_safe_cast(bin_fw.hdr.header_offset);

        bin_fw
            .fw
            .get(offset..offset + size_of::<Self>())
            .and_then(Self::from_bytes_copy)
            .ok_or(EINVAL)
    }
}

/// A parsed firmware for a RISC-V core, ready to be loaded and run.
pub(crate) struct RiscvFirmware {
    /// Offset at which the code starts in the firmware image.
    pub(crate) code_offset: u32,
    /// Offset at which the data starts in the firmware image.
    pub(crate) data_offset: u32,
    /// Offset at which the manifest starts in the firmware image.
    pub(crate) manifest_offset: u32,
    /// Application version.
    pub(crate) app_version: u32,
    /// Device-mapped firmware image.
    pub(crate) ucode: DmaObject,
}

impl RiscvFirmware {
    /// Parses the RISC-V firmware image contained in `fw`.
    pub(crate) fn new(dev: &device::Device<device::Bound>, fw: &Firmware) -> Result<Self> {
        let bin_fw = BinFirmware::new(fw)?;

        let riscv_desc = RmRiscvUCodeDesc::new(&bin_fw)?;

        let ucode = {
            let start = usize::from_safe_cast(bin_fw.hdr.data_offset);
            let len = usize::from_safe_cast(bin_fw.hdr.data_size);

            DmaObject::from_data(dev, fw.data().get(start..start + len).ok_or(EINVAL)?)?
        };

        Ok(Self {
            ucode,
            code_offset: riscv_desc.monitor_code_offset,
            data_offset: riscv_desc.monitor_data_offset,
            manifest_offset: riscv_desc.manifest_offset,
            app_version: riscv_desc.app_version,
        })
    }
}
