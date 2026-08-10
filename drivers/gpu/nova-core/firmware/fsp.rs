// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! FSP is a hardware unit that runs FMC firmware.

use kernel::{
    device,
    dma::Coherent,
    prelude::*, //
};

use crate::{
    firmware::tlv::{
        request_tlv, //
        Tlv,
    },
    gpu::Chipset, //
};

/// Size of the FSP SHA-384 hash, in bytes.
const FSP_HASH_SIZE: usize = 48;
/// Maximum size of the FSP public key (RSA-3072), in bytes.
///
/// The FMC `PKEY` tag may be shorter, so the remaining bytes are zero-padded.
const FSP_PKEY_SIZE: usize = 384;
/// Maximum size of the FSP signature (RSA-3072), in bytes.
///
/// The FMC `SIGN` tag may be shorter, so the remaining bytes are zero-padded.
const FSP_SIG_SIZE: usize = 384;

/// Structure to hold FMC signatures.
///
/// C representation is used because this type is used for communication with the FSP.
#[derive(Debug, Clone, Copy, Zeroable)]
#[repr(C)]
pub(crate) struct FmcSignatures {
    pub(crate) hash384: [u8; FSP_HASH_SIZE],
    pub(crate) public_key: [u8; FSP_PKEY_SIZE],
    pub(crate) signature: [u8; FSP_SIG_SIZE],
}

pub(crate) struct FspFirmware {
    /// FMC firmware image data
    pub(crate) fmc_image: Coherent<[u8]>,
    /// FMC firmware signatures.
    pub(crate) fmc_sigs: KBox<FmcSignatures>,
}

impl FspFirmware {
    pub(crate) fn new(dev: &device::Device<device::Bound>, chipset: Chipset) -> Result<Self> {
        let fw = request_tlv(dev, chipset, "fmc")?;
        let tlv = Tlv::new(fw.data())?;
        dev_dbg!(dev, "loaded fsp firmware v{}\n", tlv.get_string(b"VERS")?);

        let fmc_image_data = tlv.get_bytes(b"BLOB")?;
        let fmc_image = Coherent::from_slice(dev, fmc_image_data, GFP_KERNEL)?;

        Ok(Self {
            fmc_image,
            fmc_sigs: Self::extract_fmc_signatures(&tlv, dev)?,
        })
    }

    /// Extract FMC firmware signatures for Chain of Trust verification.
    ///
    /// Extracts real cryptographic signatures from FMC TLV firmware tags.
    /// Returns signatures in a heap-allocated structure to prevent stack overflow.
    fn extract_fmc_signatures(tlv: &Tlv<'_>, dev: &device::Device) -> Result<KBox<FmcSignatures>> {
        let hash_section = tlv.get_bytes(b"HASH")?;
        let pkey_section = tlv.get_bytes(b"PKEY")?;
        let sig_section = tlv.get_bytes(b"SIGN")?;

        // The hash section is a SHA-384 output: it must be exactly FSP_HASH_SIZE bytes.
        if hash_section.len() != FSP_HASH_SIZE {
            dev_err!(
                dev,
                "FMC hash section size {} != expected {}\n",
                hash_section.len(),
                FSP_HASH_SIZE
            );
            return Err(EINVAL);
        }

        // The key and signature sections are zero-padded to a fixed maximum, so they may be
        // shorter, but must not exceed the destination buffers.
        if pkey_section.len() > FSP_PKEY_SIZE {
            dev_err!(
                dev,
                "FMC public key section size {} > maximum {}\n",
                pkey_section.len(),
                FSP_PKEY_SIZE
            );
            return Err(EINVAL);
        }
        if sig_section.len() > FSP_SIG_SIZE {
            dev_err!(
                dev,
                "FMC signature section size {} > maximum {}\n",
                sig_section.len(),
                FSP_SIG_SIZE
            );
            return Err(EINVAL);
        }

        // Initialize the signatures in place to avoid building the large `FmcSignatures` on the
        // stack, then fill each section from the firmware.
        let signatures = KBox::init(
            pin_init::init_zeroed::<FmcSignatures>().chain(|sigs| {
                // PANIC: src and dst lengths are both FSP_HASH_SIZE (verified above).
                sigs.hash384.copy_from_slice(hash_section);
                // PANIC: dst is sliced to src.len(); src.len() <= FSP_PKEY_SIZE (verified above).
                sigs.public_key[..pkey_section.len()].copy_from_slice(pkey_section);
                // PANIC: dst is sliced to src.len(); src.len() <= FSP_SIG_SIZE (verified above).
                sigs.signature[..sig_section.len()].copy_from_slice(sig_section);
                Ok(())
            }),
            GFP_KERNEL,
        )?;

        Ok(signatures)
    }
}
