// SPDX-License-Identifier: GPL-2.0

//! Contains structures and functions dedicated to the parsing, building and patching of firmwares
//! to be loaded into a given execution unit.

use core::marker::PhantomData;
use core::ops::Deref;

use kernel::{
    device,
    firmware,
    prelude::*,
    str::CString,
    transmute::FromBytes, //
};

use crate::{
    dma::DmaObject,
    falcon::{
        FalconFirmware,
        FalconLoadTarget, //
    },
    gpu,
    num::{
        FromSafeCast,
        IntoSafeCast, //
    },
};

pub(crate) mod booter;
pub(crate) mod fwsec;
pub(crate) mod gsp;
pub(crate) mod riscv;

pub(crate) const FIRMWARE_VERSION: &str = "570.144";

/// Requests the GPU firmware `name` suitable for `chipset`, with version `ver`.
fn request_firmware(
    dev: &device::Device,
    chipset: gpu::Chipset,
    name: &str,
    ver: &str,
) -> Result<firmware::Firmware> {
    let chip_name = chipset.name();

    CString::try_from_fmt(fmt!("nvidia/{chip_name}/gsp/{name}-{ver}.bin"))
        .and_then(|path| firmware::Firmware::request(&path, dev))
}

/// Structure used to describe some firmwares, notably FWSEC-FRTS.
#[repr(C)]
#[derive(Debug, Clone)]
pub(crate) struct FalconUCodeDescV2 {
    /// Header defined by 'NV_BIT_FALCON_UCODE_DESC_HEADER_VDESC*' in OpenRM.
    hdr: u32,
    /// Stored size of the ucode after the header, compressed or uncompressed
    stored_size: u32,
    /// Uncompressed size of the ucode.  If store_size == uncompressed_size, then the ucode
    /// is not compressed.
    pub(crate) uncompressed_size: u32,
    /// Code entry point
    pub(crate) virtual_entry: u32,
    /// Offset after the code segment at which the Application Interface Table headers are located.
    pub(crate) interface_offset: u32,
    /// Base address at which to load the code segment into 'IMEM'.
    pub(crate) imem_phys_base: u32,
    /// Size in bytes of the code to copy into 'IMEM'.
    pub(crate) imem_load_size: u32,
    /// Virtual 'IMEM' address (i.e. 'tag') at which the code should start.
    pub(crate) imem_virt_base: u32,
    /// Virtual address of secure IMEM segment.
    pub(crate) imem_sec_base: u32,
    /// Size of secure IMEM segment.
    pub(crate) imem_sec_size: u32,
    /// Offset into stored (uncompressed) image at which DMEM begins.
    pub(crate) dmem_offset: u32,
    /// Base address at which to load the data segment into 'DMEM'.
    pub(crate) dmem_phys_base: u32,
    /// Size in bytes of the data to copy into 'DMEM'.
    pub(crate) dmem_load_size: u32,
    /// "Alternate" Size of data to load into IMEM.
    pub(crate) alt_imem_load_size: u32,
    /// "Alternate" Size of data to load into DMEM.
    pub(crate) alt_dmem_load_size: u32,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
unsafe impl FromBytes for FalconUCodeDescV2 {}

/// Structure used to describe some firmwares, notably FWSEC-FRTS.
#[repr(C)]
#[derive(Debug, Clone)]
pub(crate) struct FalconUCodeDescV3 {
    /// Header defined by `NV_BIT_FALCON_UCODE_DESC_HEADER_VDESC*` in OpenRM.
    hdr: u32,
    /// Stored size of the ucode after the header.
    stored_size: u32,
    /// Offset in `DMEM` at which the signature is expected to be found.
    pub(crate) pkc_data_offset: u32,
    /// Offset after the code segment at which the app headers are located.
    pub(crate) interface_offset: u32,
    /// Base address at which to load the code segment into `IMEM`.
    pub(crate) imem_phys_base: u32,
    /// Size in bytes of the code to copy into `IMEM`.
    pub(crate) imem_load_size: u32,
    /// Virtual `IMEM` address (i.e. `tag`) at which the code should start.
    pub(crate) imem_virt_base: u32,
    /// Base address at which to load the data segment into `DMEM`.
    pub(crate) dmem_phys_base: u32,
    /// Size in bytes of the data to copy into `DMEM`.
    pub(crate) dmem_load_size: u32,
    /// Mask of the falcon engines on which this firmware can run.
    pub(crate) engine_id_mask: u16,
    /// ID of the ucode used to infer a fuse register to validate the signature.
    pub(crate) ucode_id: u8,
    /// Number of signatures in this firmware.
    pub(crate) signature_count: u8,
    /// Versions of the signatures, used to infer a valid signature to use.
    pub(crate) signature_versions: u16,
    _reserved: u16,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use
// interior mutability.
unsafe impl FromBytes for FalconUCodeDescV3 {}

/// Enum wrapping the different versions of Falcon microcode descriptors.
///
/// This allows handling both V2 and V3 descriptor formats through a
/// unified type, providing version-agnostic access to firmware metadata
/// via the [`FalconUCodeDescriptor`] trait.
#[derive(Debug, Clone)]
pub(crate) enum FalconUCodeDesc {
    V2(FalconUCodeDescV2),
    V3(FalconUCodeDescV3),
}

impl Deref for FalconUCodeDesc {
    type Target = dyn FalconUCodeDescriptor;

    fn deref(&self) -> &Self::Target {
        match self {
            FalconUCodeDesc::V2(v2) => v2,
            FalconUCodeDesc::V3(v3) => v3,
        }
    }
}

/// Trait providing a common interface for accessing Falcon microcode descriptor fields.
///
/// This trait abstracts over the different descriptor versions ([`FalconUCodeDescV2`] and
/// [`FalconUCodeDescV3`]), allowing code to work with firmware metadata without needing to
/// know the specific descriptor version. Fields not present return zero.
pub(crate) trait FalconUCodeDescriptor {
    fn hdr(&self) -> u32;
    fn imem_load_size(&self) -> u32;
    fn interface_offset(&self) -> u32;
    fn dmem_load_size(&self) -> u32;
    fn pkc_data_offset(&self) -> u32;
    fn engine_id_mask(&self) -> u16;
    fn ucode_id(&self) -> u8;
    fn signature_count(&self) -> u8;
    fn signature_versions(&self) -> u16;

    /// Returns the size in bytes of the header.
    fn size(&self) -> usize {
        let hdr = self.hdr();

        const HDR_SIZE_SHIFT: u32 = 16;
        const HDR_SIZE_MASK: u32 = 0xffff0000;
        ((hdr & HDR_SIZE_MASK) >> HDR_SIZE_SHIFT).into_safe_cast()
    }

    fn imem_sec_load_params(&self) -> FalconLoadTarget;
    fn imem_ns_load_params(&self) -> Option<FalconLoadTarget>;
    fn dmem_load_params(&self) -> FalconLoadTarget;
}

impl FalconUCodeDescriptor for FalconUCodeDescV2 {
    fn hdr(&self) -> u32 {
        self.hdr
    }
    fn imem_load_size(&self) -> u32 {
        self.imem_load_size
    }
    fn interface_offset(&self) -> u32 {
        self.interface_offset
    }
    fn dmem_load_size(&self) -> u32 {
        self.dmem_load_size
    }
    fn pkc_data_offset(&self) -> u32 {
        0
    }
    fn engine_id_mask(&self) -> u16 {
        0
    }
    fn ucode_id(&self) -> u8 {
        0
    }
    fn signature_count(&self) -> u8 {
        0
    }
    fn signature_versions(&self) -> u16 {
        0
    }

    fn imem_sec_load_params(&self) -> FalconLoadTarget {
        FalconLoadTarget {
            src_start: 0,
            dst_start: self.imem_sec_base,
            len: self.imem_sec_size,
        }
    }

    fn imem_ns_load_params(&self) -> Option<FalconLoadTarget> {
        Some(FalconLoadTarget {
            src_start: 0,
            dst_start: self.imem_phys_base,
            len: self.imem_load_size.checked_sub(self.imem_sec_size)?,
        })
    }

    fn dmem_load_params(&self) -> FalconLoadTarget {
        FalconLoadTarget {
            src_start: self.dmem_offset,
            dst_start: self.dmem_phys_base,
            len: self.dmem_load_size,
        }
    }
}

impl FalconUCodeDescriptor for FalconUCodeDescV3 {
    fn hdr(&self) -> u32 {
        self.hdr
    }
    fn imem_load_size(&self) -> u32 {
        self.imem_load_size
    }
    fn interface_offset(&self) -> u32 {
        self.interface_offset
    }
    fn dmem_load_size(&self) -> u32 {
        self.dmem_load_size
    }
    fn pkc_data_offset(&self) -> u32 {
        self.pkc_data_offset
    }
    fn engine_id_mask(&self) -> u16 {
        self.engine_id_mask
    }
    fn ucode_id(&self) -> u8 {
        self.ucode_id
    }
    fn signature_count(&self) -> u8 {
        self.signature_count
    }
    fn signature_versions(&self) -> u16 {
        self.signature_versions
    }

    fn imem_sec_load_params(&self) -> FalconLoadTarget {
        FalconLoadTarget {
            src_start: 0,
            dst_start: self.imem_phys_base,
            len: self.imem_load_size,
        }
    }

    fn imem_ns_load_params(&self) -> Option<FalconLoadTarget> {
        // Not used on V3 platforms
        None
    }

    fn dmem_load_params(&self) -> FalconLoadTarget {
        FalconLoadTarget {
            src_start: self.imem_load_size,
            dst_start: self.dmem_phys_base,
            len: self.dmem_load_size,
        }
    }
}

/// Trait implemented by types defining the signed state of a firmware.
trait SignedState {}

/// Type indicating that the firmware must be signed before it can be used.
struct Unsigned;
impl SignedState for Unsigned {}

/// Type indicating that the firmware is signed and ready to be loaded.
struct Signed;
impl SignedState for Signed {}

/// A [`DmaObject`] containing a specific microcode ready to be loaded into a falcon.
///
/// This is module-local and meant for sub-modules to use internally.
///
/// After construction, a firmware is [`Unsigned`], and must generally be patched with a signature
/// before it can be loaded (with an exception for development hardware). The
/// [`Self::patch_signature`] and [`Self::no_patch_signature`] methods are used to transition the
/// firmware to its [`Signed`] state.
struct FirmwareDmaObject<F: FalconFirmware, S: SignedState>(DmaObject, PhantomData<(F, S)>);

/// Trait for signatures to be patched directly into a given firmware.
///
/// This is module-local and meant for sub-modules to use internally.
trait FirmwareSignature<F: FalconFirmware>: AsRef<[u8]> {}

impl<F: FalconFirmware> FirmwareDmaObject<F, Unsigned> {
    /// Patches the firmware at offset `sig_base_img` with `signature`.
    fn patch_signature<S: FirmwareSignature<F>>(
        mut self,
        signature: &S,
        sig_base_img: usize,
    ) -> Result<FirmwareDmaObject<F, Signed>> {
        let signature_bytes = signature.as_ref();
        if sig_base_img + signature_bytes.len() > self.0.size() {
            return Err(EINVAL);
        }

        // SAFETY: We are the only user of this object, so there cannot be any race.
        let dst = unsafe { self.0.start_ptr_mut().add(sig_base_img) };

        // SAFETY: `signature` and `dst` are valid, properly aligned, and do not overlap.
        unsafe {
            core::ptr::copy_nonoverlapping(signature_bytes.as_ptr(), dst, signature_bytes.len())
        };

        Ok(FirmwareDmaObject(self.0, PhantomData))
    }

    /// Mark the firmware as signed without patching it.
    ///
    /// This method is used to explicitly confirm that we do not need to sign the firmware, while
    /// allowing us to continue as if it was. This is typically only needed for development
    /// hardware.
    fn no_patch_signature(self) -> FirmwareDmaObject<F, Signed> {
        FirmwareDmaObject(self.0, PhantomData)
    }
}

/// Header common to most firmware files.
#[repr(C)]
#[derive(Debug, Clone)]
struct BinHdr {
    /// Magic number, must be `0x10de`.
    bin_magic: u32,
    /// Version of the header.
    bin_ver: u32,
    /// Size in bytes of the binary (to be ignored).
    bin_size: u32,
    /// Offset of the start of the application-specific header.
    header_offset: u32,
    /// Offset of the start of the data payload.
    data_offset: u32,
    /// Size in bytes of the data payload.
    data_size: u32,
}

// SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
unsafe impl FromBytes for BinHdr {}

// A firmware blob starting with a `BinHdr`.
struct BinFirmware<'a> {
    hdr: BinHdr,
    fw: &'a [u8],
}

impl<'a> BinFirmware<'a> {
    /// Interpret `fw` as a firmware image starting with a [`BinHdr`], and returns the
    /// corresponding [`BinFirmware`] that can be used to extract its payload.
    fn new(fw: &'a firmware::Firmware) -> Result<Self> {
        const BIN_MAGIC: u32 = 0x10de;
        let fw = fw.data();

        fw.get(0..size_of::<BinHdr>())
            // Extract header.
            .and_then(BinHdr::from_bytes_copy)
            // Validate header.
            .and_then(|hdr| {
                if hdr.bin_magic == BIN_MAGIC {
                    Some(hdr)
                } else {
                    None
                }
            })
            .map(|hdr| Self { hdr, fw })
            .ok_or(EINVAL)
    }

    /// Returns the data payload of the firmware, or `None` if the data range is out of bounds of
    /// the firmware image.
    fn data(&self) -> Option<&[u8]> {
        let fw_start = usize::from_safe_cast(self.hdr.data_offset);
        let fw_size = usize::from_safe_cast(self.hdr.data_size);

        self.fw.get(fw_start..fw_start + fw_size)
    }
}

pub(crate) struct ModInfoBuilder<const N: usize>(firmware::ModInfoBuilder<N>);

impl<const N: usize> ModInfoBuilder<N> {
    const fn make_entry_file(self, chipset: &str, fw: &str) -> Self {
        ModInfoBuilder(
            self.0
                .new_entry()
                .push("nvidia/")
                .push(chipset)
                .push("/gsp/")
                .push(fw)
                .push("-")
                .push(FIRMWARE_VERSION)
                .push(".bin"),
        )
    }

    const fn make_entry_chipset(self, chipset: &str) -> Self {
        self.make_entry_file(chipset, "booter_load")
            .make_entry_file(chipset, "booter_unload")
            .make_entry_file(chipset, "bootloader")
            .make_entry_file(chipset, "gsp")
    }

    pub(crate) const fn create(
        module_name: &'static kernel::str::CStr,
    ) -> firmware::ModInfoBuilder<N> {
        let mut this = Self(firmware::ModInfoBuilder::new(module_name));
        let mut i = 0;

        while i < gpu::Chipset::ALL.len() {
            this = this.make_entry_chipset(gpu::Chipset::ALL[i].name());
            i += 1;
        }

        this.0
    }
}
