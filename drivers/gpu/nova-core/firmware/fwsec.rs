// SPDX-License-Identifier: GPL-2.0

//! FWSEC is a High Secure firmware that is extracted from the BIOS and performs the first step of
//! the GSP startup by creating the WPR2 memory region and copying critical areas of the VBIOS into
//! it after authenticating them, ensuring they haven't been tampered with. It runs on the GSP
//! falcon.
//!
//! Before being run, it needs to be patched in two areas:
//!
//! - The command to be run, as this firmware can perform several tasks ;
//! - The ucode signature, so the GSP falcon can run FWSEC in HS mode.

pub(crate) mod bootloader;

use core::marker::PhantomData;

use kernel::{
    device::{
        self,
        Device, //
    },
    prelude::*,
    transmute::{
        AsBytes,
        FromBytes, //
    },
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp,
        Falcon,
        FalconBromParams,
        FalconDmaLoadTarget,
        FalconDmaLoadable,
        FalconFirmware, //
    },
    firmware::{
        FalconUCodeDesc,
        FirmwareObject,
        FirmwareSignature,
        Signed,
        Unsigned, //
    },
    num::FromSafeCast,
    vbios::Vbios,
};

const NVFW_FALCON_APPIF_ID_DMEMMAPPER: u32 = 0x4;

#[repr(C)]
#[derive(Debug)]
struct FalconAppifHdrV1 {
    version: u8,
    header_size: u8,
    entry_size: u8,
    entry_count: u8,
}
// SAFETY: Any byte sequence is valid for this struct.
unsafe impl FromBytes for FalconAppifHdrV1 {}

#[repr(C, packed)]
#[derive(Debug)]
struct FalconAppifV1 {
    id: u32,
    dmem_base: u32,
}
// SAFETY: Any byte sequence is valid for this struct.
unsafe impl FromBytes for FalconAppifV1 {}

#[derive(Debug)]
#[repr(C, packed)]
struct FalconAppifDmemmapperV3 {
    signature: u32,
    version: u16,
    size: u16,
    cmd_in_buffer_offset: u32,
    cmd_in_buffer_size: u32,
    cmd_out_buffer_offset: u32,
    cmd_out_buffer_size: u32,
    nvf_img_data_buffer_offset: u32,
    nvf_img_data_buffer_size: u32,
    printf_buffer_hdr: u32,
    ucode_build_time_stamp: u32,
    ucode_signature: u32,
    init_cmd: u32,
    ucode_feature: u32,
    ucode_cmd_mask0: u32,
    ucode_cmd_mask1: u32,
    multi_tgt_tbl: u32,
}
// SAFETY: Any byte sequence is valid for this struct.
unsafe impl FromBytes for FalconAppifDmemmapperV3 {}
// SAFETY: This struct doesn't contain uninitialized bytes and doesn't have interior mutability.
unsafe impl AsBytes for FalconAppifDmemmapperV3 {}

#[derive(Debug)]
#[repr(C, packed)]
struct ReadVbios {
    ver: u32,
    hdr: u32,
    addr: u64,
    size: u32,
    flags: u32,
}
// SAFETY: Any byte sequence is valid for this struct.
unsafe impl FromBytes for ReadVbios {}
// SAFETY: This struct doesn't contain uninitialized bytes and doesn't have interior mutability.
unsafe impl AsBytes for ReadVbios {}

#[derive(Debug)]
#[repr(C, packed)]
struct FrtsRegion {
    ver: u32,
    hdr: u32,
    addr: u32,
    size: u32,
    ftype: u32,
}
// SAFETY: Any byte sequence is valid for this struct.
unsafe impl FromBytes for FrtsRegion {}
// SAFETY: This struct doesn't contain uninitialized bytes and doesn't have interior mutability.
unsafe impl AsBytes for FrtsRegion {}

const NVFW_FRTS_CMD_REGION_TYPE_FB: u32 = 2;

#[repr(C, packed)]
struct FrtsCmd {
    read_vbios: ReadVbios,
    frts_region: FrtsRegion,
}
// SAFETY: Any byte sequence is valid for this struct.
unsafe impl FromBytes for FrtsCmd {}
// SAFETY: This struct doesn't contain uninitialized bytes and doesn't have interior mutability.
unsafe impl AsBytes for FrtsCmd {}

const NVFW_FALCON_APPIF_DMEMMAPPER_CMD_FRTS: u32 = 0x15;
const NVFW_FALCON_APPIF_DMEMMAPPER_CMD_SB: u32 = 0x19;

/// Command for the [`FwsecFirmware`] to execute.
pub(crate) enum FwsecCommand {
    /// Asks [`FwsecFirmware`] to carve out the WPR2 area and place a verified copy of the VBIOS
    /// image into it.
    Frts { frts_addr: u64, frts_size: u64 },
    /// Asks [`FwsecFirmware`] to load pre-OS apps on the PMU.
    #[expect(dead_code)]
    Sb,
}

/// Size of the signatures used in FWSEC.
const BCRT30_RSA3K_SIG_SIZE: usize = 384;

/// A single signature that can be patched into a FWSEC image.
#[repr(transparent)]
pub(crate) struct Bcrt30Rsa3kSignature([u8; BCRT30_RSA3K_SIG_SIZE]);

/// SAFETY: A signature is just an array of bytes.
unsafe impl FromBytes for Bcrt30Rsa3kSignature {}

impl From<[u8; BCRT30_RSA3K_SIG_SIZE]> for Bcrt30Rsa3kSignature {
    fn from(sig: [u8; BCRT30_RSA3K_SIG_SIZE]) -> Self {
        Self(sig)
    }
}

impl AsRef<[u8]> for Bcrt30Rsa3kSignature {
    fn as_ref(&self) -> &[u8] {
        &self.0
    }
}

impl FirmwareSignature<FwsecFirmware> for Bcrt30Rsa3kSignature {}

/// The FWSEC microcode, extracted from the BIOS and to be run on the GSP falcon.
///
/// It is responsible for e.g. carving out the WPR2 region as the first step of the GSP bootflow.
pub(crate) struct FwsecFirmware {
    /// Descriptor of the firmware.
    desc: FalconUCodeDesc,
    /// Object containing the firmware binary.
    ucode: FirmwareObject<Self, Signed>,
}

impl FalconDmaLoadable for FwsecFirmware {
    fn as_slice(&self) -> &[u8] {
        self.ucode.0.as_slice()
    }

    fn imem_sec_load_params(&self) -> FalconDmaLoadTarget {
        self.desc.imem_sec_load_params()
    }

    fn imem_ns_load_params(&self) -> Option<FalconDmaLoadTarget> {
        self.desc.imem_ns_load_params()
    }

    fn dmem_load_params(&self) -> FalconDmaLoadTarget {
        self.desc.dmem_load_params()
    }
}

impl FalconFirmware for FwsecFirmware {
    type Target = Gsp;

    fn brom_params(&self) -> FalconBromParams {
        FalconBromParams {
            pkc_data_offset: self.desc.pkc_data_offset(),
            engine_id_mask: self.desc.engine_id_mask(),
            ucode_id: self.desc.ucode_id(),
        }
    }

    fn boot_addr(&self) -> u32 {
        0
    }
}

impl FirmwareObject<FwsecFirmware, Unsigned> {
    fn new_fwsec(bios: &Vbios, cmd: FwsecCommand) -> Result<Self> {
        let desc = bios.fwsec_image().header()?;
        let mut ucode = KVVec::new();
        ucode.extend_from_slice(bios.fwsec_image().ucode(&desc)?, GFP_KERNEL)?;

        let hdr_offset = desc
            .imem_load_size()
            .checked_add(desc.interface_offset())
            .map(usize::from_safe_cast)
            .ok_or(EINVAL)?;

        let hdr = ucode
            .get(hdr_offset..)
            .and_then(FalconAppifHdrV1::from_bytes_prefix)
            .ok_or(EINVAL)?
            .0;

        if hdr.version != 1 {
            return Err(EINVAL);
        }

        // Find the DMEM mapper section in the firmware.
        for i in 0..usize::from(hdr.entry_count) {
            // CALC: hdr_offset + header_size + i * entry_size.
            let entry_offset = hdr_offset
                .checked_add(usize::from(hdr.header_size))
                .and_then(|o| o.checked_add(i.checked_mul(usize::from(hdr.entry_size))?))
                .ok_or(EINVAL)?;

            let app = ucode
                .get(entry_offset..)
                .and_then(FalconAppifV1::from_bytes_prefix)
                .ok_or(EINVAL)?
                .0;

            if app.id != NVFW_FALCON_APPIF_ID_DMEMMAPPER {
                continue;
            }
            let dmem_base = app.dmem_base;

            let dmem_mapper_offset = desc
                .imem_load_size()
                .checked_add(dmem_base)
                .map(usize::from_safe_cast)
                .ok_or(EINVAL)?;

            let dmem_mapper = ucode
                .get_mut(dmem_mapper_offset..)
                .and_then(FalconAppifDmemmapperV3::from_bytes_mut_prefix)
                .ok_or(EINVAL)?
                .0;

            dmem_mapper.init_cmd = match cmd {
                FwsecCommand::Frts { .. } => NVFW_FALCON_APPIF_DMEMMAPPER_CMD_FRTS,
                FwsecCommand::Sb => NVFW_FALCON_APPIF_DMEMMAPPER_CMD_SB,
            };
            let cmd_in_buffer_offset = dmem_mapper.cmd_in_buffer_offset;

            let frts_cmd_offset = desc
                .imem_load_size()
                .checked_add(cmd_in_buffer_offset)
                .map(usize::from_safe_cast)
                .ok_or(EINVAL)?;

            let frts_cmd = ucode
                .get_mut(frts_cmd_offset..)
                .and_then(FrtsCmd::from_bytes_mut_prefix)
                .ok_or(EINVAL)?
                .0;

            frts_cmd.read_vbios = ReadVbios {
                ver: 1,
                hdr: u32::try_from(size_of::<ReadVbios>())?,
                addr: 0,
                size: 0,
                flags: 2,
            };
            if let FwsecCommand::Frts {
                frts_addr,
                frts_size,
            } = cmd
            {
                frts_cmd.frts_region = FrtsRegion {
                    ver: 1,
                    hdr: u32::try_from(size_of::<FrtsRegion>())?,
                    addr: u32::try_from(frts_addr >> 12)?,
                    size: u32::try_from(frts_size >> 12)?,
                    ftype: NVFW_FRTS_CMD_REGION_TYPE_FB,
                };
            }

            // Return early as we found and patched the DMEMMAPPER region.
            return Ok(Self(ucode, PhantomData));
        }

        Err(ENOTSUPP)
    }
}

impl FwsecFirmware {
    /// Extract the Fwsec firmware from `bios` and patch it to run on `falcon` with the `cmd`
    /// command.
    pub(crate) fn new(
        dev: &Device<device::Bound>,
        falcon: &Falcon<Gsp>,
        bar: &Bar0,
        bios: &Vbios,
        cmd: FwsecCommand,
    ) -> Result<Self> {
        let ucode_dma = FirmwareObject::<Self, _>::new_fwsec(bios, cmd)?;

        // Patch signature if needed.
        let desc = bios.fwsec_image().header()?;
        let ucode_signed = if desc.signature_count() != 0 {
            let sig_base_img = desc
                .imem_load_size()
                .checked_add(desc.pkc_data_offset())
                .map(usize::from_safe_cast)
                .ok_or(EINVAL)?;
            let desc_sig_versions = u32::from(desc.signature_versions());
            let reg_fuse_version =
                falcon.signature_reg_fuse_version(bar, desc.engine_id_mask(), desc.ucode_id())?;
            dev_dbg!(
                dev,
                "desc_sig_versions: {:#x}, reg_fuse_version: {}\n",
                desc_sig_versions,
                reg_fuse_version
            );
            let signature_idx = {
                let reg_fuse_version_bit = 1 << reg_fuse_version;

                // Check if the fuse version is supported by the firmware.
                if desc_sig_versions & reg_fuse_version_bit == 0 {
                    dev_err!(
                        dev,
                        "no matching signature: {:#x} {:#x}\n",
                        reg_fuse_version_bit,
                        desc_sig_versions,
                    );
                    return Err(EINVAL);
                }

                // `desc_sig_versions` has one bit set per included signature. Thus, the index of
                // the signature to patch is the number of bits in `desc_sig_versions` set to `1`
                // before `reg_fuse_version_bit`.

                // Mask of the bits of `desc_sig_versions` to preserve.
                let reg_fuse_version_mask = reg_fuse_version_bit.wrapping_sub(1);

                usize::from_safe_cast((desc_sig_versions & reg_fuse_version_mask).count_ones())
            };

            dev_dbg!(dev, "patching signature with index {}\n", signature_idx);
            let signature = bios
                .fwsec_image()
                .sigs(&desc)
                .and_then(|sigs| sigs.get(signature_idx).ok_or(EINVAL))?;

            ucode_dma.patch_signature(signature, sig_base_img)?
        } else {
            ucode_dma.no_patch_signature()
        };

        Ok(FwsecFirmware {
            desc,
            ucode: ucode_signed,
        })
    }

    /// Loads the FWSEC firmware into `falcon` and execute it.
    ///
    /// This must only be called on chipsets that do not need the FWSEC bootloader (i.e., where
    /// [`Chipset::needs_fwsec_bootloader()`](crate::gpu::Chipset::needs_fwsec_bootloader) returns
    /// `false`). On chipsets that do, use [`bootloader::FwsecFirmwareWithBl`] instead.
    pub(crate) fn run(
        &self,
        dev: &Device<device::Bound>,
        falcon: &Falcon<Gsp>,
        bar: &Bar0,
    ) -> Result<()> {
        // Reset falcon, load the firmware, and run it.
        falcon
            .reset(bar)
            .inspect_err(|e| dev_err!(dev, "Failed to reset GSP falcon: {:?}\n", e))?;
        falcon
            .load(dev, bar, self)
            .inspect_err(|e| dev_err!(dev, "Failed to load FWSEC firmware: {:?}\n", e))?;
        let (mbox0, _) = falcon
            .boot(bar, Some(0), None)
            .inspect_err(|e| dev_err!(dev, "Failed to boot FWSEC firmware: {:?}\n", e))?;
        if mbox0 != 0 {
            dev_err!(dev, "FWSEC firmware returned error {}\n", mbox0);
            Err(EIO)
        } else {
            Ok(())
        }
    }
}
