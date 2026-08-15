// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! MCTP/NVDM protocol types for NVIDIA GPU firmware communication.
//!
//! MCTP (Management Component Transport Protocol) carries NVDM (NVIDIA
//! Data Model) messages between the kernel driver and GPU firmware processors
//! such as FSP and GSP.

use kernel::pci::Vendor;

/// NVDM message type identifiers carried over MCTP.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum NvdmType {
    #[default]
    /// Chain of Trust boot message.
    Cot = 0x14,
    /// FSP command response.
    FspResponse = 0x15,
}

impl TryFrom<u8> for NvdmType {
    type Error = u8;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            x if x == u8::from(Self::Cot) => Ok(Self::Cot),
            x if x == u8::from(Self::FspResponse) => Ok(Self::FspResponse),
            _ => Err(value),
        }
    }
}

impl From<NvdmType> for u8 {
    fn from(value: NvdmType) -> Self {
        value as u8
    }
}

bitfield! {
    pub(crate) struct MctpHeader(u32), "MCTP transport header for NVIDIA firmware messages." {
        31:31 som as bool, "Start-of-message bit.";
        30:30 eom as bool, "End-of-message bit.";
        29:28 seq as u8, "Packet sequence number.";
        23:16 seid as u8, "Source endpoint ID.";
    }
}

impl MctpHeader {
    /// Builds a single-packet MCTP header (`SOM=1`, `EOM=1`, `SEQ=0`, `SEID=0`).
    pub(crate) fn single_packet() -> Self {
        Self::default().set_som(true).set_eom(true)
    }

    /// Returns whether this is a complete single-packet message (`SOM=1` and `EOM=1`).
    pub(crate) fn is_single_packet(self) -> bool {
        self.som() && self.eom()
    }
}

/// MCTP message type for PCI vendor-defined messages.
const MSG_TYPE_VENDOR_PCI: u8 = 0x7e;

bitfield! {
    pub(crate) struct NvdmHeader(u32), "NVIDIA Vendor-Defined Message header over MCTP." {
        31:24 nvdm_type as u8 ?=> NvdmType, "NVDM message type.";
        23:8 vendor_id as u16, "PCI vendor ID.";
        6:0 msg_type as u8, "MCTP vendor-defined message type.";
    }
}

impl NvdmHeader {
    /// Builds an NVDM header for the given message type.
    pub(crate) fn new(nvdm_type: NvdmType) -> Self {
        Self::default()
            .set_msg_type(MSG_TYPE_VENDOR_PCI)
            .set_vendor_id(Vendor::NVIDIA.as_raw())
            .set_nvdm_type(nvdm_type)
    }

    /// Validates this header against the expected NVIDIA NVDM format and type.
    pub(crate) fn validate(self, expected_type: NvdmType) -> bool {
        self.msg_type() == MSG_TYPE_VENDOR_PCI
            && self.vendor_id() == Vendor::NVIDIA.as_raw()
            && matches!(self.nvdm_type(), Ok(nvdm_type) if nvdm_type == expected_type)
    }
}
