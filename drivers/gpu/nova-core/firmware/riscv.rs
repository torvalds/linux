// SPDX-License-Identifier: GPL-2.0

//! Support for firmware binaries designed to run on a RISC-V core. Such firmwares files have a
//! dedicated header.

use kernel::{
    device,
    dma::Coherent,
    firmware::Firmware,
    prelude::*, //
};

use crate::firmware::tlv::Tlv;

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
    pub(crate) ucode: Coherent<[u8]>,
}

impl RiscvFirmware {
    /// Parses the RISC-V firmware image contained in `fw`.
    pub(crate) fn new(dev: &device::Device<device::Bound>, fw: &Firmware) -> Result<Self> {
        let tlv = Tlv::new(fw.data())?;
        dev_dbg!(
            dev,
            "loaded gsp bootloader firmware v{}\n",
            tlv.get_string(b"VERS")?
        );

        let code_offset = tlv.get_u32(b"CDOF")?;
        let data_offset = tlv.get_u32(b"DAOF")?;
        let manifest_offset = tlv.get_u32(b"MFOF")?;
        let app_version = tlv.get_u32(b"APPV")?;

        let ucode = Coherent::from_slice(dev, tlv.get_bytes(b"BLOB")?, GFP_KERNEL)?;

        Ok(Self {
            ucode,
            code_offset,
            data_offset,
            manifest_offset,
            app_version,
        })
    }
}
