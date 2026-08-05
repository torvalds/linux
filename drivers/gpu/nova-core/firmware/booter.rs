// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Support for loading and patching the `Booter` firmware. `Booter` is a Heavy Secured firmware
//! running on [`Sec2`], that is used on Turing/Ampere to load the GSP firmware into the GSP falcon
//! (and optionally unload it through a separate firmware image).

use core::marker::PhantomData;

use kernel::{
    device,
    dma::Coherent,
    prelude::*, //
};

use crate::{
    falcon::{
        sec2::Sec2,
        Falcon,
        FalconBromParams,
        FalconDmaLoadTarget,
        FalconDmaLoadable,
        FalconFirmware, //
    },
    firmware::{
        tlv::{
            request_tlv, //
            Tlv,
        },
        FirmwareObject,
        FirmwareSignature,
        Signed,
        Unsigned, //
    },
    gpu::Chipset,
    num::IntoSafeCast,
};

/// Signature for Booter firmware. Their size is encoded into the header and not known a compile
/// time, so we just wrap a byte slices on which we can implement [`FirmwareSignature`].
struct BooterSignature<'a>(&'a [u8]);

impl<'a> AsRef<[u8]> for BooterSignature<'a> {
    fn as_ref(&self) -> &[u8] {
        self.0
    }
}

impl<'a> FirmwareSignature<BooterFirmware> for BooterSignature<'a> {}

/// The `Booter` loader firmware, responsible for loading the GSP.
pub(crate) struct BooterFirmware {
    // Load parameters for Secure `IMEM` falcon memory.
    imem_sec_load_target: FalconDmaLoadTarget,
    // Load parameters for Non-Secure `IMEM` falcon memory,
    // used only on Turing and GA100
    imem_ns_load_target: Option<FalconDmaLoadTarget>,
    // Load parameters for `DMEM` falcon memory.
    dmem_load_target: FalconDmaLoadTarget,
    // BROM falcon parameters.
    brom_params: FalconBromParams,
    // Device-mapped firmware image.
    ucode: FirmwareObject<Self, Signed>,
}

impl FirmwareObject<BooterFirmware, Unsigned> {
    fn new_booter(data: &[u8]) -> Result<Self> {
        let mut ucode = KVVec::new();
        ucode.extend_from_slice(data, GFP_KERNEL)?;

        Ok(Self(ucode, PhantomData))
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub(crate) enum BooterKind {
    Loader,
    Unloader,
}

impl BooterFirmware {
    /// Parses the Booter firmware contained in `fw`, and patches the correct signature so it is
    /// ready to be loaded and run on `falcon`.
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        kind: BooterKind,
        chipset: Chipset,
        falcon: &Falcon<'_, <Self as FalconFirmware>::Target>,
    ) -> Result<Self> {
        let fw_name = match kind {
            BooterKind::Loader => "booter_load",
            BooterKind::Unloader => "booter_unload",
        };
        let fw = request_tlv(dev, chipset, fw_name)?;
        let tlv = Tlv::new(fw.data())?;
        dev_dbg!(
            dev,
            "loaded {} firmware v{}\n",
            fw_name,
            tlv.get_string(b"VERS")?
        );

        let os_data_offset = tlv.get_u32(b"DAOF")?;
        let os_data_size = tlv.get_u32(b"DASZ")?;
        let os_code_offset = tlv.get_u32(b"CDOF")?;
        let os_code_size = tlv.get_u32(b"CDSZ")?;
        let patch_loc = tlv.get_u32(b"PLOC")?;
        let fuse_version: usize = tlv.get_u32(b"FUSE")?.into_safe_cast();
        let engine_id = tlv.get_u32(b"ENID")?;
        let ucode_id = tlv.get_u32(b"UCID")?;
        let app0_code_offset = tlv.get_u32(b"A0CO")?;
        let app0_code_size = tlv.get_u32(b"A0CS")?;

        let brom_params = FalconBromParams {
            // `os_data_offset` is an absolute index, but `pkc_data_offset` is from the
            // signature patch location.
            pkc_data_offset: patch_loc.checked_sub(os_data_offset).ok_or(EINVAL)?,
            engine_id_mask: u16::try_from(engine_id).map_err(|_| EINVAL)?,
            ucode_id: u8::try_from(ucode_id).map_err(|_| EINVAL)?,
        };

        let ucode = tlv
            .get_bytes(b"BLOB")
            .and_then(FirmwareObject::<Self, _>::new_booter)?;

        // Obtain the version from the fuse register, and extract the corresponding
        // signature.
        let reg_fuse_version: usize = falcon
            .signature_reg_fuse_version(brom_params.engine_id_mask, brom_params.ucode_id)?
            .into_safe_cast();

        const FUSE_VERSION_USE_LAST_SIG: usize = 0;

        let index = match reg_fuse_version {
            // `0` means the last signature should be used.
            FUSE_VERSION_USE_LAST_SIG => None,
            // Otherwise, hardware fuse version needs to be subtracted to obtain the index.
            _ => Some(fuse_version.checked_sub(reg_fuse_version).ok_or(EINVAL)?),
        };

        // Extract the nth signature.  Booter is always signed.
        let sig_chunk = tlv.get_signature(index)?;

        let signature = BooterSignature(sig_chunk);
        let ucode_signed = ucode.patch_signature(&signature, patch_loc.into_safe_cast())?;

        // There are two versions of Booter, one for Turing/GA100, and another for
        // GA102+.  The extraction of the IMEM sections differs between the two
        // versions.  Unfortunately, the file names are the same, and the headers
        // don't indicate the versions.  The only way to differentiate is by the Chipset.
        let (imem_sec_dst_start, imem_ns_load_target) = if chipset <= Chipset::GA100 {
            (
                app0_code_offset,
                Some(FalconDmaLoadTarget {
                    src_start: 0,
                    dst_start: os_code_offset,
                    len: os_code_size,
                }),
            )
        } else {
            (0, None)
        };

        Ok(Self {
            imem_sec_load_target: FalconDmaLoadTarget {
                src_start: app0_code_offset,
                dst_start: imem_sec_dst_start,
                len: app0_code_size,
            },
            imem_ns_load_target,
            dmem_load_target: FalconDmaLoadTarget {
                src_start: os_data_offset,
                dst_start: 0,
                len: os_data_size,
            },
            brom_params,
            ucode: ucode_signed,
        })
    }

    /// Load and run the booter firmware on SEC2.
    ///
    /// Resets SEC2, loads this firmware image, then boots with the WPR metadata
    /// address passed via the SEC2 mailboxes.
    pub(crate) fn run<T>(
        &self,
        dev: &device::Device<device::Bound>,
        sec2_falcon: &Falcon<'_, Sec2>,
        wpr_meta: &Coherent<T>,
    ) -> Result {
        sec2_falcon.reset()?;
        sec2_falcon.load(self)?;
        let wpr_dma_address = wpr_meta.dma_address();
        let (mbox0, mbox1) = sec2_falcon.boot(
            Some(wpr_dma_address as u32),
            Some((wpr_dma_address >> 32) as u32),
        )?;
        dev_dbg!(dev, "SEC2 MBOX0: {:#x}, MBOX1: {:#x}\n", mbox0, mbox1);

        if mbox0 != 0 {
            dev_err!(dev, "Booter-load failed with error {:#x}\n", mbox0);
            return Err(ENODEV);
        }

        Ok(())
    }
}

impl FalconDmaLoadable for BooterFirmware {
    fn as_slice(&self) -> &[u8] {
        self.ucode.0.as_slice()
    }

    fn imem_sec_load_params(&self) -> FalconDmaLoadTarget {
        self.imem_sec_load_target.clone()
    }

    fn imem_ns_load_params(&self) -> Option<FalconDmaLoadTarget> {
        self.imem_ns_load_target.clone()
    }

    fn dmem_load_params(&self) -> FalconDmaLoadTarget {
        self.dmem_load_target.clone()
    }
}

impl FalconFirmware for BooterFirmware {
    type Target = Sec2;

    fn brom_params(&self) -> FalconBromParams {
        self.brom_params.clone()
    }

    fn boot_addr(&self) -> u32 {
        if let Some(ns_target) = &self.imem_ns_load_target {
            ns_target.dst_start
        } else {
            self.imem_sec_load_target.src_start
        }
    }
}
