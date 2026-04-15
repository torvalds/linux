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
    falcon::{
        FalconDmaLoadTarget,
        FalconFirmware, //
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
    /// Size in bytes of the code to copy into 'IMEM' (includes both secure and non-secure
    /// segments).
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

    fn imem_sec_load_params(&self) -> FalconDmaLoadTarget;
    fn imem_ns_load_params(&self) -> Option<FalconDmaLoadTarget>;
    fn dmem_load_params(&self) -> FalconDmaLoadTarget;
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

    fn imem_sec_load_params(&self) -> FalconDmaLoadTarget {
        // `imem_sec_base` is the *virtual* start address of the secure IMEM segment, so subtract
        // `imem_virt_base` to get its physical offset.
        let imem_sec_start = self.imem_sec_base.saturating_sub(self.imem_virt_base);

        FalconDmaLoadTarget {
            src_start: imem_sec_start,
            dst_start: self.imem_phys_base.saturating_add(imem_sec_start),
            len: self.imem_sec_size,
        }
    }

    fn imem_ns_load_params(&self) -> Option<FalconDmaLoadTarget> {
        Some(FalconDmaLoadTarget {
            // Non-secure code always starts at offset 0.
            src_start: 0,
            dst_start: self.imem_phys_base,
            // `imem_load_size` includes the size of the secure segment, so subtract it to
            // get the correct amount of data to copy.
            len: self.imem_load_size.saturating_sub(self.imem_sec_size),
        })
    }

    fn dmem_load_params(&self) -> FalconDmaLoadTarget {
        FalconDmaLoadTarget {
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

    fn imem_sec_load_params(&self) -> FalconDmaLoadTarget {
        FalconDmaLoadTarget {
            // IMEM segment always starts at offset 0.
            src_start: 0,
            dst_start: self.imem_phys_base,
            len: self.imem_load_size,
        }
    }

    fn imem_ns_load_params(&self) -> Option<FalconDmaLoadTarget> {
        // Not used on V3 platforms
        None
    }

    fn dmem_load_params(&self) -> FalconDmaLoadTarget {
        FalconDmaLoadTarget {
            // DMEM segment starts right after the IMEM one.
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

/// Microcode to be loaded into a specific falcon.
///
/// This is module-local and meant for sub-modules to use internally.
///
/// After construction, a firmware is [`Unsigned`], and must generally be patched with a signature
/// before it can be loaded (with an exception for development hardware). The
/// [`Self::patch_signature`] and [`Self::no_patch_signature`] methods are used to transition the
/// firmware to its [`Signed`] state.
// TODO: Consider replacing this with a coherent memory object once `CoherentAllocation` supports
// temporary CPU-exclusive access to the object without unsafe methods.
struct FirmwareObject<F: FalconFirmware, S: SignedState>(KVVec<u8>, PhantomData<(F, S)>);

/// Trait for signatures to be patched directly into a given firmware.
///
/// This is module-local and meant for sub-modules to use internally.
trait FirmwareSignature<F: FalconFirmware>: AsRef<[u8]> {}

impl<F: FalconFirmware> FirmwareObject<F, Unsigned> {
    /// Patches the firmware at offset `signature_start` with `signature`.
    fn patch_signature<S: FirmwareSignature<F>>(
        mut self,
        signature: &S,
        signature_start: usize,
    ) -> Result<FirmwareObject<F, Signed>> {
        let signature_bytes = signature.as_ref();
        let signature_end = signature_start
            .checked_add(signature_bytes.len())
            .ok_or(EOVERFLOW)?;
        let dst = self
            .0
            .get_mut(signature_start..signature_end)
            .ok_or(EINVAL)?;

        // PANIC: `dst` and `signature_bytes` have the same length.
        dst.copy_from_slice(signature_bytes);

        Ok(FirmwareObject(self.0, PhantomData))
    }

    /// Mark the firmware as signed without patching it.
    ///
    /// This method is used to explicitly confirm that we do not need to sign the firmware, while
    /// allowing us to continue as if it was. This is typically only needed for development
    /// hardware.
    fn no_patch_signature(self) -> FirmwareObject<F, Signed> {
        FirmwareObject(self.0, PhantomData)
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
        let fw_end = fw_start.checked_add(fw_size)?;

        self.fw.get(fw_start..fw_end)
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

    const fn make_entry_chipset(self, chipset: gpu::Chipset) -> Self {
        let name = chipset.name();

        let this = self
            .make_entry_file(name, "booter_load")
            .make_entry_file(name, "booter_unload")
            .make_entry_file(name, "bootloader")
            .make_entry_file(name, "gsp");

        if chipset.needs_fwsec_bootloader() {
            this.make_entry_file(name, "gen_bootloader")
        } else {
            this
        }
    }

    pub(crate) const fn create(
        module_name: &'static core::ffi::CStr,
    ) -> firmware::ModInfoBuilder<N> {
        let mut this = Self(firmware::ModInfoBuilder::new(module_name));
        let mut i = 0;

        while i < gpu::Chipset::ALL.len() {
            this = this.make_entry_chipset(gpu::Chipset::ALL[i]);
            i += 1;
        }

        this.0
    }
}

/// Ad-hoc and temporary module to extract sections from ELF images.
///
/// Some firmware images are currently packaged as ELF files, where sections names are used as keys
/// to specific and related bits of data. Future firmware versions are scheduled to move away from
/// that scheme before nova-core becomes stable, which means this module will eventually be
/// removed.
mod elf {
    use core::mem::size_of;

    use kernel::{
        bindings,
        str::CStr,
        transmute::FromBytes, //
    };

    /// Newtype to provide a [`FromBytes`] implementation.
    #[repr(transparent)]
    struct Elf64Hdr(bindings::elf64_hdr);
    // SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
    unsafe impl FromBytes for Elf64Hdr {}

    #[repr(transparent)]
    struct Elf64SHdr(bindings::elf64_shdr);
    // SAFETY: all bit patterns are valid for this type, and it doesn't use interior mutability.
    unsafe impl FromBytes for Elf64SHdr {}

    /// Returns a NULL-terminated string from the ELF image at `offset`.
    fn elf_str(elf: &[u8], offset: u64) -> Option<&str> {
        let idx = usize::try_from(offset).ok()?;
        let bytes = elf.get(idx..)?;
        CStr::from_bytes_until_nul(bytes).ok()?.to_str().ok()
    }

    /// Tries to extract section with name `name` from the ELF64 image `elf`, and returns it.
    pub(super) fn elf64_section<'a, 'b>(elf: &'a [u8], name: &'b str) -> Option<&'a [u8]> {
        let hdr = &elf
            .get(0..size_of::<bindings::elf64_hdr>())
            .and_then(Elf64Hdr::from_bytes)?
            .0;

        // Get all the section headers.
        let mut shdr = {
            let shdr_num = usize::from(hdr.e_shnum);
            let shdr_start = usize::try_from(hdr.e_shoff).ok()?;
            let shdr_end = shdr_num
                .checked_mul(size_of::<Elf64SHdr>())
                .and_then(|v| v.checked_add(shdr_start))?;

            elf.get(shdr_start..shdr_end)
                .map(|slice| slice.chunks_exact(size_of::<Elf64SHdr>()))?
        };

        // Get the strings table.
        let strhdr = shdr
            .clone()
            .nth(usize::from(hdr.e_shstrndx))
            .and_then(Elf64SHdr::from_bytes)?;

        // Find the section which name matches `name` and return it.
        shdr.find_map(|sh| {
            let hdr = Elf64SHdr::from_bytes(sh)?;
            let name_offset = strhdr.0.sh_offset.checked_add(u64::from(hdr.0.sh_name))?;
            let section_name = elf_str(elf, name_offset)?;

            if section_name != name {
                return None;
            }

            let start = usize::try_from(hdr.0.sh_offset).ok()?;
            let end = usize::try_from(hdr.0.sh_size)
                .ok()
                .and_then(|sh_size| start.checked_add(sh_size))?;

            elf.get(start..end)
        })
    }
}
