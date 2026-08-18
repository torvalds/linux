// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! FSP is a hardware unit that runs FMC firmware.

use kernel::{
    device,
    dma::Coherent,
    firmware::Firmware,
    prelude::*, //
};

use crate::{
    firmware::elf,
    gpu::Chipset, //
};

/// Size of the FSP SHA-384 hash, in bytes.
const FSP_HASH_SIZE: usize = 48;
/// Maximum size of the FSP public key (RSA-3072), in bytes.
///
/// The FMC ELF `publickey` section may be shorter, so the remaining bytes are zero-padded.
const FSP_PKEY_SIZE: usize = 384;
/// Maximum size of the FSP signature (RSA-3072), in bytes.
///
/// The FMC ELF `signature` section may be shorter, so the remaining bytes are zero-padded.
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
    /// FMC firmware image data (only the "image" ELF section).
    pub(crate) fmc_image: Coherent<[u8]>,
    /// FMC firmware signatures.
    pub(crate) fmc_sigs: KBox<FmcSignatures>,
}

impl FspFirmware {
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        chipset: Chipset,
        ver: &str,
    ) -> Result<Self> {
        let fw = super::request_firmware(dev, chipset, "fmc", ver)?;

        // FSP expects only the "image" section, not the entire ELF file.
        let fmc_image_data = elf::elf_section(fw.data(), "image").ok_or_else(|| {
            dev_err!(dev, "FMC ELF file missing 'image' section\n");
            EINVAL
        })?;
        let fmc_image = Coherent::from_slice(dev, fmc_image_data, GFP_KERNEL)?;

        Ok(Self {
            fmc_image,
            fmc_sigs: Self::extract_fmc_signatures(&fw, dev)?,
        })
    }

    /// Extract FMC firmware signatures for Chain of Trust verification.
    ///
    /// Extracts real cryptographic signatures from FMC ELF32 firmware sections.
    /// Returns signatures in a heap-allocated structure to prevent stack overflow.
    fn extract_fmc_signatures(
        fmc_fw: &Firmware,
        dev: &device::Device,
    ) -> Result<KBox<FmcSignatures>> {
        let get_section = |name: &str, max_len: usize| {
            elf::elf_section(fmc_fw.data(), name)
                .ok_or(EINVAL)
                .inspect_err(|_| dev_err!(dev, "FMC firmware missing '{}' section\n", name))
                .and_then(|section| {
                    if section.len() > max_len {
                        dev_err!(
                            dev,
                            "FMC {} section size {} > maximum {}\n",
                            name,
                            section.len(),
                            max_len
                        );
                        Err(EINVAL)
                    } else {
                        Ok(section)
                    }
                })
        };

        let hash_section = get_section("hash", FSP_HASH_SIZE)?;
        let pkey_section = get_section("publickey", FSP_PKEY_SIZE)?;
        let sig_section = get_section("signature", FSP_SIG_SIZE)?;

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

        // Initialize the signatures in place to avoid building the large `FmcSignatures` on the
        // stack, then fill each section from the firmware.
        let signatures = KBox::init(
            pin_init::init_zeroed::<FmcSignatures>().chain(|sigs| {
                // PANIC: src and dst lengths are both FSP_HASH_SIZE (verified above).
                sigs.hash384.copy_from_slice(hash_section);
                // PANIC: dst is sliced to src.len(); src.len() <= FSP_PKEY_SIZE per `get_section`.
                sigs.public_key[..pkey_section.len()].copy_from_slice(pkey_section);
                // PANIC: dst is sliced to src.len(); src.len() <= FSP_SIG_SIZE per `get_section`.
                sigs.signature[..sig_section.len()].copy_from_slice(sig_section);
                Ok(())
            }),
            GFP_KERNEL,
        )?;

        Ok(signatures)
    }
}
