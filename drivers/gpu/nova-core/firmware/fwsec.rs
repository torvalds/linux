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

use core::marker::PhantomData;
use core::mem::{align_of, size_of};
use core::ops::Deref;

use kernel::device::{self, Device};
use kernel::prelude::*;
use kernel::transmute::FromBytes;

use crate::dma::DmaObject;
use crate::driver::Bar0;
use crate::falcon::gsp::Gsp;
use crate::falcon::{Falcon, FalconBromParams, FalconFirmware, FalconLoadParams, FalconLoadTarget};
use crate::firmware::{FalconUCodeDescV3, FirmwareDmaObject, FirmwareSignature, Signed, Unsigned};
use crate::vbios::Vbios;

const NVFW_FALCON_APPIF_ID_DMEMMAPPER: u32 = 0x4;

#[repr(C)]
#[derive(Debug)]
struct FalconAppifHdrV1 {
    version: u8,
    header_size: u8,
    entry_size: u8,
    entry_count: u8,
}
// SAFETY: any byte sequence is valid for this struct.
unsafe impl FromBytes for FalconAppifHdrV1 {}

#[repr(C, packed)]
#[derive(Debug)]
struct FalconAppifV1 {
    id: u32,
    dmem_base: u32,
}
// SAFETY: any byte sequence is valid for this struct.
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
// SAFETY: any byte sequence is valid for this struct.
unsafe impl FromBytes for FalconAppifDmemmapperV3 {}

#[derive(Debug)]
#[repr(C, packed)]
struct ReadVbios {
    ver: u32,
    hdr: u32,
    addr: u64,
    size: u32,
    flags: u32,
}
// SAFETY: any byte sequence is valid for this struct.
unsafe impl FromBytes for ReadVbios {}

#[derive(Debug)]
#[repr(C, packed)]
struct FrtsRegion {
    ver: u32,
    hdr: u32,
    addr: u32,
    size: u32,
    ftype: u32,
}
// SAFETY: any byte sequence is valid for this struct.
unsafe impl FromBytes for FrtsRegion {}

const NVFW_FRTS_CMD_REGION_TYPE_FB: u32 = 2;

#[repr(C, packed)]
struct FrtsCmd {
    read_vbios: ReadVbios,
    frts_region: FrtsRegion,
}
// SAFETY: any byte sequence is valid for this struct.
unsafe impl FromBytes for FrtsCmd {}

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

/// Reinterpret the area starting from `offset` in `fw` as an instance of `T` (which must implement
/// [`FromBytes`]) and return a reference to it.
///
/// # Safety
///
/// Callers must ensure that the region of memory returned is not written for as long as the
/// returned reference is alive.
///
/// TODO[TRSM][COHA]: Remove this and `transmute_mut` once `CoherentAllocation::as_slice` is
/// available and we have a way to transmute objects implementing FromBytes, e.g.:
/// https://lore.kernel.org/lkml/20250330234039.29814-1-christiansantoslima21@gmail.com/
unsafe fn transmute<'a, 'b, T: Sized + FromBytes>(
    fw: &'a DmaObject,
    offset: usize,
) -> Result<&'b T> {
    if offset + size_of::<T>() > fw.size() {
        return Err(EINVAL);
    }
    if (fw.start_ptr() as usize + offset) % align_of::<T>() != 0 {
        return Err(EINVAL);
    }

    // SAFETY: we have checked that the pointer is properly aligned that its pointed memory is
    // large enough the contains an instance of `T`, which implements `FromBytes`.
    Ok(unsafe { &*(fw.start_ptr().add(offset).cast::<T>()) })
}

/// Reinterpret the area starting from `offset` in `fw` as a mutable instance of `T` (which must
/// implement [`FromBytes`]) and return a reference to it.
///
/// # Safety
///
/// Callers must ensure that the region of memory returned is not read or written for as long as
/// the returned reference is alive.
unsafe fn transmute_mut<'a, 'b, T: Sized + FromBytes>(
    fw: &'a mut DmaObject,
    offset: usize,
) -> Result<&'b mut T> {
    if offset + size_of::<T>() > fw.size() {
        return Err(EINVAL);
    }
    if (fw.start_ptr_mut() as usize + offset) % align_of::<T>() != 0 {
        return Err(EINVAL);
    }

    // SAFETY: we have checked that the pointer is properly aligned that its pointed memory is
    // large enough the contains an instance of `T`, which implements `FromBytes`.
    Ok(unsafe { &mut *(fw.start_ptr_mut().add(offset).cast::<T>()) })
}

/// The FWSEC microcode, extracted from the BIOS and to be run on the GSP falcon.
///
/// It is responsible for e.g. carving out the WPR2 region as the first step of the GSP bootflow.
pub(crate) struct FwsecFirmware {
    /// Descriptor of the firmware.
    desc: FalconUCodeDescV3,
    /// GPU-accessible DMA object containing the firmware.
    ucode: FirmwareDmaObject<Self, Signed>,
}

impl FalconLoadParams for FwsecFirmware {
    fn imem_load_params(&self) -> FalconLoadTarget {
        FalconLoadTarget {
            src_start: 0,
            dst_start: self.desc.imem_phys_base,
            len: self.desc.imem_load_size,
        }
    }

    fn dmem_load_params(&self) -> FalconLoadTarget {
        FalconLoadTarget {
            src_start: self.desc.imem_load_size,
            dst_start: self.desc.dmem_phys_base,
            len: self.desc.dmem_load_size,
        }
    }

    fn brom_params(&self) -> FalconBromParams {
        FalconBromParams {
            pkc_data_offset: self.desc.pkc_data_offset,
            engine_id_mask: self.desc.engine_id_mask,
            ucode_id: self.desc.ucode_id,
        }
    }

    fn boot_addr(&self) -> u32 {
        0
    }
}

impl Deref for FwsecFirmware {
    type Target = DmaObject;

    fn deref(&self) -> &Self::Target {
        &self.ucode.0
    }
}

impl FalconFirmware for FwsecFirmware {
    type Target = Gsp;
}

impl FirmwareDmaObject<FwsecFirmware, Unsigned> {
    fn new_fwsec(dev: &Device<device::Bound>, bios: &Vbios, cmd: FwsecCommand) -> Result<Self> {
        let desc = bios.fwsec_image().header()?;
        let ucode = bios.fwsec_image().ucode(desc)?;
        let mut dma_object = DmaObject::from_data(dev, ucode)?;

        let hdr_offset = (desc.imem_load_size + desc.interface_offset) as usize;
        // SAFETY: we have exclusive access to `dma_object`.
        let hdr: &FalconAppifHdrV1 = unsafe { transmute(&dma_object, hdr_offset) }?;

        if hdr.version != 1 {
            return Err(EINVAL);
        }

        // Find the DMEM mapper section in the firmware.
        for i in 0..hdr.entry_count as usize {
            let app: &FalconAppifV1 =
            // SAFETY: we have exclusive access to `dma_object`.
            unsafe {
                transmute(
                    &dma_object,
                    hdr_offset + hdr.header_size as usize + i * hdr.entry_size as usize
                )
            }?;

            if app.id != NVFW_FALCON_APPIF_ID_DMEMMAPPER {
                continue;
            }

            // SAFETY: we have exclusive access to `dma_object`.
            let dmem_mapper: &mut FalconAppifDmemmapperV3 = unsafe {
                transmute_mut(
                    &mut dma_object,
                    (desc.imem_load_size + app.dmem_base) as usize,
                )
            }?;

            // SAFETY: we have exclusive access to `dma_object`.
            let frts_cmd: &mut FrtsCmd = unsafe {
                transmute_mut(
                    &mut dma_object,
                    (desc.imem_load_size + dmem_mapper.cmd_in_buffer_offset) as usize,
                )
            }?;

            frts_cmd.read_vbios = ReadVbios {
                ver: 1,
                hdr: size_of::<ReadVbios>() as u32,
                addr: 0,
                size: 0,
                flags: 2,
            };

            dmem_mapper.init_cmd = match cmd {
                FwsecCommand::Frts {
                    frts_addr,
                    frts_size,
                } => {
                    frts_cmd.frts_region = FrtsRegion {
                        ver: 1,
                        hdr: size_of::<FrtsRegion>() as u32,
                        addr: (frts_addr >> 12) as u32,
                        size: (frts_size >> 12) as u32,
                        ftype: NVFW_FRTS_CMD_REGION_TYPE_FB,
                    };

                    NVFW_FALCON_APPIF_DMEMMAPPER_CMD_FRTS
                }
                FwsecCommand::Sb => NVFW_FALCON_APPIF_DMEMMAPPER_CMD_SB,
            };

            // Return early as we found and patched the DMEMMAPPER region.
            return Ok(Self(dma_object, PhantomData));
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
        let ucode_dma = FirmwareDmaObject::<Self, _>::new_fwsec(dev, bios, cmd)?;

        // Patch signature if needed.
        let desc = bios.fwsec_image().header()?;
        let ucode_signed = if desc.signature_count != 0 {
            let sig_base_img = (desc.imem_load_size + desc.pkc_data_offset) as usize;
            let desc_sig_versions = u32::from(desc.signature_versions);
            let reg_fuse_version =
                falcon.signature_reg_fuse_version(bar, desc.engine_id_mask, desc.ucode_id)?;
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

                (desc_sig_versions & reg_fuse_version_mask).count_ones() as usize
            };

            dev_dbg!(dev, "patching signature with index {}\n", signature_idx);
            let signature = bios
                .fwsec_image()
                .sigs(desc)
                .and_then(|sigs| sigs.get(signature_idx).ok_or(EINVAL))?;

            ucode_dma.patch_signature(signature, sig_base_img)?
        } else {
            ucode_dma.no_patch_signature()
        };

        Ok(FwsecFirmware {
            desc: desc.clone(),
            ucode: ucode_signed,
        })
    }

    /// Loads the FWSEC firmware into `falcon` and execute it.
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
            .dma_load(bar, self)
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
