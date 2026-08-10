// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! MCTP/NVDM protocol types for NVIDIA GPU firmware communication.
//!
//! MCTP (Management Component Transport Protocol) carries NVDM (NVIDIA
//! Data Model) messages between the kernel driver and GPU firmware processors
//! such as FSP and GSP.

use kernel::{
    bitfield,
    pci::Vendor,
    prelude::*, //
};

use crate::{
    bounded_enum,
    num, //
};

bounded_enum! {
    /// NVDM message type identifiers carried over MCTP.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub(crate) enum NvdmType with TryFrom<Bounded<u32, 8>> {
        /// PRC (Product Reconfiguration Control) message.
        Prc = 0x13,
        /// Chain of Trust boot message.
        Cot = 0x14,
        /// FSP command response.
        FspResponse = 0x15,
    }
}

bitfield! {
    /// MCTP transport header for NVIDIA firmware messages.
    pub(crate) struct MctpHeader(u32) {
        /// Start-of-message bit.
        31:31 som;
        /// End-of-message bit.
        30:30 eom;
        /// Packet sequence number.
        29:28 seq;
        /// Source endpoint ID.
        23:16 seid;
    }
}

impl MctpHeader {
    /// Builds a single-packet MCTP header (`SOM=1`, `EOM=1`, `SEQ=0`, `SEID=0`).
    pub(crate) fn single_packet() -> Self {
        Self::zeroed().with_som(true).with_eom(true)
    }

    /// Returns whether this is a complete single-packet message (`SOM=1` and `EOM=1`).
    pub(crate) fn is_single_packet(self) -> bool {
        self.som().into_bool() && self.eom().into_bool()
    }
}

/// MCTP message type for PCI vendor-defined messages.
const MSG_TYPE_VENDOR_PCI: u8 = 0x7e;

bitfield! {
    /// NVIDIA Vendor-Defined Message header over MCTP.
    pub(crate) struct NvdmHeader(u32) {
        /// NVDM message type.
        31:24 nvdm_type ?=> NvdmType;
        /// PCI vendor ID.
        23:8 vendor_id;
        /// MCTP vendor-defined message type.
        6:0 msg_type;
    }
}

impl NvdmHeader {
    /// Builds an NVDM header for the given message type.
    pub(crate) fn new(nvdm_type: NvdmType) -> Self {
        Self::zeroed()
            .with_const_msg_type::<{ num::u8_as_u32(MSG_TYPE_VENDOR_PCI) }>()
            .with_vendor_id(Vendor::NVIDIA.as_raw())
            .with_nvdm_type(nvdm_type)
    }

    /// Validates this header against the expected NVIDIA NVDM format and type.
    pub(crate) fn validate(self, expected_type: NvdmType) -> bool {
        u8::from(self.msg_type()) == MSG_TYPE_VENDOR_PCI
            && u16::from(self.vendor_id()) == Vendor::NVIDIA.as_raw()
            && matches!(self.nvdm_type(), Ok(nvdm_type) if nvdm_type == expected_type)
    }
}
