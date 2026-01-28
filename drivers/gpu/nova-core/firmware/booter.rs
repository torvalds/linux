// SPDX-License-Identifier: GPL-2.0

//! Support for loading and patching the `Booter` firmware. `Booter` is a Heavy Secured firmware
//! running on [`Sec2`], that is used on Turing/Ampere to load the GSP firmware into the GSP falcon
//! (and optionally unload it through a separate firmware image).

use core::{
    marker::PhantomData,
    ops::Deref, //
};

use kernel::{
    device,
    prelude::*,
    transmute::FromBytes, //
};

use crate::{
    dma::DmaObject,
    driver::Bar0,
    falcon::{
        sec2::Sec2,
        Falcon,
        FalconBromParams,
        FalconFirmware,
        FalconLoadParams,
        FalconLoadTarget, //
    },
    firmware::{
        BinFirmware,
        FirmwareDmaObject,
        FirmwareSignature,
        Signed,
        Unsigned, //
    },
    gpu::Chipset,
    num::{
        FromSafeCast,
        IntoSafeCast, //
    },
};

/// Local convenience function to return a copy of `S` by reinterpreting the bytes starting at
/// `offset` in `slice`.
fn frombytes_at<S: FromBytes + Sized>(slice: &[u8], offset: usize) -> Result<S> {
    slice
        .get(offset..offset + size_of::<S>())
        .and_then(S::from_bytes_copy)
        .ok_or(EINVAL)
}

/// Heavy-Secured firmware header.
///
/// Such firmwares have an application-specific payload that needs to be patched with a given
/// signature.
#[repr(C)]
#[derive(Debug, Clone)]
struct HsHeaderV2 {
    /// Offset to the start of the signatures.
    sig_prod_offset: u32,
    /// Size in bytes of the signatures.
    sig_prod_size: u32,
    /// Offset to a `u32` containing the location at which to patch the signature in the microcode
    /// image.
    patch_loc_offset: u32,
    /// Offset to a `u32` containing the index of the signature to patch.
    patch_sig_offset: u32,
    /// Start offset to the signature metadata.
    meta_data_offset: u32,
    /// Size in bytes of the signature metadata.
    meta_data_size: u32,
    /// Offset to a `u32` containing the number of signatures in the signatures section.
    num_sig_offset: u32,
    /// Offset of the application-specific header.
    header_offset: u32,
    /// Size in bytes of the application-specific header.
    header_size: u32,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
unsafe impl FromBytes for HsHeaderV2 {}

/// Heavy-Secured Firmware image container.
///
/// This provides convenient access to the fields of [`HsHeaderV2`] that are actually indices to
/// read from in the firmware data.
struct HsFirmwareV2<'a> {
    hdr: HsHeaderV2,
    fw: &'a [u8],
}

impl<'a> HsFirmwareV2<'a> {
    /// Interprets the header of `bin_fw` as a [`HsHeaderV2`] and returns an instance of
    /// `HsFirmwareV2` for further parsing.
    ///
    /// Fails if the header pointed at by `bin_fw` is not within the bounds of the firmware image.
    fn new(bin_fw: &BinFirmware<'a>) -> Result<Self> {
        frombytes_at::<HsHeaderV2>(bin_fw.fw, bin_fw.hdr.header_offset.into_safe_cast())
            .map(|hdr| Self { hdr, fw: bin_fw.fw })
    }

    /// Returns the location at which the signatures should be patched in the microcode image.
    ///
    /// Fails if the offset of the patch location is outside the bounds of the firmware
    /// image.
    fn patch_location(&self) -> Result<u32> {
        frombytes_at::<u32>(self.fw, self.hdr.patch_loc_offset.into_safe_cast())
    }

    /// Returns an iterator to the signatures of the firmware. The iterator can be empty if the
    /// firmware is unsigned.
    ///
    /// Fails if the pointed signatures are outside the bounds of the firmware image.
    fn signatures_iter(&'a self) -> Result<impl Iterator<Item = BooterSignature<'a>>> {
        let num_sig = frombytes_at::<u32>(self.fw, self.hdr.num_sig_offset.into_safe_cast())?;
        let iter = match self.hdr.sig_prod_size.checked_div(num_sig) {
            // If there are no signatures, return an iterator that will yield zero elements.
            None => (&[] as &[u8]).chunks_exact(1),
            Some(sig_size) => {
                let patch_sig =
                    frombytes_at::<u32>(self.fw, self.hdr.patch_sig_offset.into_safe_cast())?;
                let signatures_start = usize::from_safe_cast(self.hdr.sig_prod_offset + patch_sig);

                self.fw
                    // Get signatures range.
                    .get(
                        signatures_start
                            ..signatures_start + usize::from_safe_cast(self.hdr.sig_prod_size),
                    )
                    .ok_or(EINVAL)?
                    .chunks_exact(sig_size.into_safe_cast())
            }
        };

        // Map the byte slices into signatures.
        Ok(iter.map(BooterSignature))
    }
}

/// Signature parameters, as defined in the firmware.
#[repr(C)]
struct HsSignatureParams {
    /// Fuse version to use.
    fuse_ver: u32,
    /// Mask of engine IDs this firmware applies to.
    engine_id_mask: u32,
    /// ID of the microcode.
    ucode_id: u32,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
unsafe impl FromBytes for HsSignatureParams {}

impl HsSignatureParams {
    /// Returns the signature parameters contained in `hs_fw`.
    ///
    /// Fails if the meta data parameter of `hs_fw` is outside the bounds of the firmware image, or
    /// if its size doesn't match that of [`HsSignatureParams`].
    fn new(hs_fw: &HsFirmwareV2<'_>) -> Result<Self> {
        let start = usize::from_safe_cast(hs_fw.hdr.meta_data_offset);
        let end = start
            .checked_add(hs_fw.hdr.meta_data_size.into_safe_cast())
            .ok_or(EINVAL)?;

        hs_fw
            .fw
            .get(start..end)
            .and_then(Self::from_bytes_copy)
            .ok_or(EINVAL)
    }
}

/// Header for code and data load offsets.
#[repr(C)]
#[derive(Debug, Clone)]
struct HsLoadHeaderV2 {
    // Offset at which the code starts.
    os_code_offset: u32,
    // Total size of the code, for all apps.
    os_code_size: u32,
    // Offset at which the data starts.
    os_data_offset: u32,
    // Size of the data.
    os_data_size: u32,
    // Number of apps following this header. Each app is described by a [`HsLoadHeaderV2App`].
    num_apps: u32,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
unsafe impl FromBytes for HsLoadHeaderV2 {}

impl HsLoadHeaderV2 {
    /// Returns the load header contained in `hs_fw`.
    ///
    /// Fails if the header pointed at by `hs_fw` is not within the bounds of the firmware image.
    fn new(hs_fw: &HsFirmwareV2<'_>) -> Result<Self> {
        frombytes_at::<Self>(hs_fw.fw, hs_fw.hdr.header_offset.into_safe_cast())
    }
}

/// Header for app code loader.
#[repr(C)]
#[derive(Debug, Clone)]
struct HsLoadHeaderV2App {
    /// Offset at which to load the app code.
    offset: u32,
    /// Length in bytes of the app code.
    len: u32,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
unsafe impl FromBytes for HsLoadHeaderV2App {}

impl HsLoadHeaderV2App {
    /// Returns the [`HsLoadHeaderV2App`] for app `idx` of `hs_fw`.
    ///
    /// Fails if `idx` is larger than the number of apps declared in `hs_fw`, or if the header is
    /// not within the bounds of the firmware image.
    fn new(hs_fw: &HsFirmwareV2<'_>, idx: u32) -> Result<Self> {
        let load_hdr = HsLoadHeaderV2::new(hs_fw)?;
        if idx >= load_hdr.num_apps {
            Err(EINVAL)
        } else {
            frombytes_at::<Self>(
                hs_fw.fw,
                usize::from_safe_cast(hs_fw.hdr.header_offset)
                    // Skip the load header...
                    .checked_add(size_of::<HsLoadHeaderV2>())
                    // ... and jump to app header `idx`.
                    .and_then(|offset| {
                        offset
                            .checked_add(usize::from_safe_cast(idx).checked_mul(size_of::<Self>())?)
                    })
                    .ok_or(EINVAL)?,
            )
        }
    }
}

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
    imem_sec_load_target: FalconLoadTarget,
    // Load parameters for Non-Secure `IMEM` falcon memory,
    // used only on Turing and GA100
    imem_ns_load_target: Option<FalconLoadTarget>,
    // Load parameters for `DMEM` falcon memory.
    dmem_load_target: FalconLoadTarget,
    // BROM falcon parameters.
    brom_params: FalconBromParams,
    // Device-mapped firmware image.
    ucode: FirmwareDmaObject<Self, Signed>,
}

impl FirmwareDmaObject<BooterFirmware, Unsigned> {
    fn new_booter(dev: &device::Device<device::Bound>, data: &[u8]) -> Result<Self> {
        DmaObject::from_data(dev, data).map(|ucode| Self(ucode, PhantomData))
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub(crate) enum BooterKind {
    Loader,
    #[expect(unused)]
    Unloader,
}

impl BooterFirmware {
    /// Parses the Booter firmware contained in `fw`, and patches the correct signature so it is
    /// ready to be loaded and run on `falcon`.
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        kind: BooterKind,
        chipset: Chipset,
        ver: &str,
        falcon: &Falcon<<Self as FalconFirmware>::Target>,
        bar: &Bar0,
    ) -> Result<Self> {
        let fw_name = match kind {
            BooterKind::Loader => "booter_load",
            BooterKind::Unloader => "booter_unload",
        };
        let fw = super::request_firmware(dev, chipset, fw_name, ver)?;
        let bin_fw = BinFirmware::new(&fw)?;

        // The binary firmware embeds a Heavy-Secured firmware.
        let hs_fw = HsFirmwareV2::new(&bin_fw)?;

        // The Heavy-Secured firmware embeds a firmware load descriptor.
        let load_hdr = HsLoadHeaderV2::new(&hs_fw)?;

        // Offset in `ucode` where to patch the signature.
        let patch_loc = hs_fw.patch_location()?;

        let sig_params = HsSignatureParams::new(&hs_fw)?;
        let brom_params = FalconBromParams {
            // `load_hdr.os_data_offset` is an absolute index, but `pkc_data_offset` is from the
            // signature patch location.
            pkc_data_offset: patch_loc
                .checked_sub(load_hdr.os_data_offset)
                .ok_or(EINVAL)?,
            engine_id_mask: u16::try_from(sig_params.engine_id_mask).map_err(|_| EINVAL)?,
            ucode_id: u8::try_from(sig_params.ucode_id).map_err(|_| EINVAL)?,
        };
        let app0 = HsLoadHeaderV2App::new(&hs_fw, 0)?;

        // Object containing the firmware microcode to be signature-patched.
        let ucode = bin_fw
            .data()
            .ok_or(EINVAL)
            .and_then(|data| FirmwareDmaObject::<Self, _>::new_booter(dev, data))?;

        let ucode_signed = {
            let mut signatures = hs_fw.signatures_iter()?.peekable();

            if signatures.peek().is_none() {
                // If there are no signatures, then the firmware is unsigned.
                ucode.no_patch_signature()
            } else {
                // Obtain the version from the fuse register, and extract the corresponding
                // signature.
                let reg_fuse_version = falcon.signature_reg_fuse_version(
                    bar,
                    brom_params.engine_id_mask,
                    brom_params.ucode_id,
                )?;

                // `0` means the last signature should be used.
                const FUSE_VERSION_USE_LAST_SIG: u32 = 0;
                let signature = match reg_fuse_version {
                    FUSE_VERSION_USE_LAST_SIG => signatures.last(),
                    // Otherwise hardware fuse version needs to be subtracted to obtain the index.
                    reg_fuse_version => {
                        let Some(idx) = sig_params.fuse_ver.checked_sub(reg_fuse_version) else {
                            dev_err!(dev, "invalid fuse version for Booter firmware\n");
                            return Err(EINVAL);
                        };
                        signatures.nth(idx.into_safe_cast())
                    }
                }
                .ok_or(EINVAL)?;

                ucode.patch_signature(&signature, patch_loc.into_safe_cast())?
            }
        };

        // There are two versions of Booter, one for Turing/GA100, and another for
        // GA102+.  The extraction of the IMEM sections differs between the two
        // versions.  Unfortunately, the file names are the same, and the headers
        // don't indicate the versions.  The only way to differentiate is by the Chipset.
        let (imem_sec_dst_start, imem_ns_load_target) = if chipset <= Chipset::GA100 {
            (
                app0.offset,
                Some(FalconLoadTarget {
                    src_start: 0,
                    dst_start: load_hdr.os_code_offset,
                    len: load_hdr.os_code_size,
                }),
            )
        } else {
            (0, None)
        };

        Ok(Self {
            imem_sec_load_target: FalconLoadTarget {
                src_start: app0.offset,
                dst_start: imem_sec_dst_start,
                len: app0.len,
            },
            imem_ns_load_target,
            dmem_load_target: FalconLoadTarget {
                src_start: load_hdr.os_data_offset,
                dst_start: 0,
                len: load_hdr.os_data_size,
            },
            brom_params,
            ucode: ucode_signed,
        })
    }
}

impl FalconLoadParams for BooterFirmware {
    fn imem_sec_load_params(&self) -> FalconLoadTarget {
        self.imem_sec_load_target.clone()
    }

    fn imem_ns_load_params(&self) -> Option<FalconLoadTarget> {
        self.imem_ns_load_target.clone()
    }

    fn dmem_load_params(&self) -> FalconLoadTarget {
        self.dmem_load_target.clone()
    }

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

impl Deref for BooterFirmware {
    type Target = DmaObject;

    fn deref(&self) -> &Self::Target {
        &self.ucode.0
    }
}

impl FalconFirmware for BooterFirmware {
    type Target = Sec2;
}
