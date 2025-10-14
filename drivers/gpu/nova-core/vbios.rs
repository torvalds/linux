// SPDX-License-Identifier: GPL-2.0

//! VBIOS extraction and parsing.

use crate::driver::Bar0;
use crate::firmware::fwsec::Bcrt30Rsa3kSignature;
use crate::firmware::FalconUCodeDescV3;
use core::convert::TryFrom;
use kernel::device;
use kernel::error::Result;
use kernel::prelude::*;
use kernel::ptr::{Alignable, Alignment};
use kernel::types::ARef;

/// The offset of the VBIOS ROM in the BAR0 space.
const ROM_OFFSET: usize = 0x300000;
/// The maximum length of the VBIOS ROM to scan into.
const BIOS_MAX_SCAN_LEN: usize = 0x100000;
/// The size to read ahead when parsing initial BIOS image headers.
const BIOS_READ_AHEAD_SIZE: usize = 1024;
/// The bit in the last image indicator byte for the PCI Data Structure that
/// indicates the last image. Bit 0-6 are reserved, bit 7 is last image bit.
const LAST_IMAGE_BIT_MASK: u8 = 0x80;

// PMU lookup table entry types. Used to locate PMU table entries
// in the Fwsec image, corresponding to falcon ucodes.
#[expect(dead_code)]
const FALCON_UCODE_ENTRY_APPID_FIRMWARE_SEC_LIC: u8 = 0x05;
#[expect(dead_code)]
const FALCON_UCODE_ENTRY_APPID_FWSEC_DBG: u8 = 0x45;
const FALCON_UCODE_ENTRY_APPID_FWSEC_PROD: u8 = 0x85;

/// Vbios Reader for constructing the VBIOS data.
struct VbiosIterator<'a> {
    dev: &'a device::Device,
    bar0: &'a Bar0,
    /// VBIOS data vector: As BIOS images are scanned, they are added to this vector for reference
    /// or copying into other data structures. It is the entire scanned contents of the VBIOS which
    /// progressively extends. It is used so that we do not re-read any contents that are already
    /// read as we use the cumulative length read so far, and re-read any gaps as we extend the
    /// length.
    data: KVec<u8>,
    /// Current offset of the [`Iterator`].
    current_offset: usize,
    /// Indicate whether the last image has been found.
    last_found: bool,
}

impl<'a> VbiosIterator<'a> {
    fn new(dev: &'a device::Device, bar0: &'a Bar0) -> Result<Self> {
        Ok(Self {
            dev,
            bar0,
            data: KVec::new(),
            current_offset: 0,
            last_found: false,
        })
    }

    /// Read bytes from the ROM at the current end of the data vector.
    fn read_more(&mut self, len: usize) -> Result {
        let current_len = self.data.len();
        let start = ROM_OFFSET + current_len;

        // Ensure length is a multiple of 4 for 32-bit reads
        if len % core::mem::size_of::<u32>() != 0 {
            dev_err!(
                self.dev,
                "VBIOS read length {} is not a multiple of 4\n",
                len
            );
            return Err(EINVAL);
        }

        self.data.reserve(len, GFP_KERNEL)?;
        // Read ROM data bytes and push directly to `data`.
        for addr in (start..start + len).step_by(core::mem::size_of::<u32>()) {
            // Read 32-bit word from the VBIOS ROM
            let word = self.bar0.try_read32(addr)?;

            // Convert the `u32` to a 4 byte array and push each byte.
            word.to_ne_bytes()
                .iter()
                .try_for_each(|&b| self.data.push(b, GFP_KERNEL))?;
        }

        Ok(())
    }

    /// Read bytes at a specific offset, filling any gap.
    fn read_more_at_offset(&mut self, offset: usize, len: usize) -> Result {
        if offset > BIOS_MAX_SCAN_LEN {
            dev_err!(self.dev, "Error: exceeded BIOS scan limit.\n");
            return Err(EINVAL);
        }

        // If `offset` is beyond current data size, fill the gap first.
        let current_len = self.data.len();
        let gap_bytes = offset.saturating_sub(current_len);

        // Now read the requested bytes at the offset.
        self.read_more(gap_bytes + len)
    }

    /// Read a BIOS image at a specific offset and create a [`BiosImage`] from it.
    ///
    /// `self.data` is extended as needed and a new [`BiosImage`] is returned.
    /// `context` is a string describing the operation for error reporting.
    fn read_bios_image_at_offset(
        &mut self,
        offset: usize,
        len: usize,
        context: &str,
    ) -> Result<BiosImage> {
        let data_len = self.data.len();
        if offset + len > data_len {
            self.read_more_at_offset(offset, len).inspect_err(|e| {
                dev_err!(
                    self.dev,
                    "Failed to read more at offset {:#x}: {:?}\n",
                    offset,
                    e
                )
            })?;
        }

        BiosImage::new(self.dev, &self.data[offset..offset + len]).inspect_err(|err| {
            dev_err!(
                self.dev,
                "Failed to {} at offset {:#x}: {:?}\n",
                context,
                offset,
                err
            )
        })
    }
}

impl<'a> Iterator for VbiosIterator<'a> {
    type Item = Result<BiosImage>;

    /// Iterate over all VBIOS images until the last image is detected or offset
    /// exceeds scan limit.
    fn next(&mut self) -> Option<Self::Item> {
        if self.last_found {
            return None;
        }

        if self.current_offset > BIOS_MAX_SCAN_LEN {
            dev_err!(self.dev, "Error: exceeded BIOS scan limit, stopping scan\n");
            return None;
        }

        // Parse image headers first to get image size.
        let image_size = match self.read_bios_image_at_offset(
            self.current_offset,
            BIOS_READ_AHEAD_SIZE,
            "parse initial BIOS image headers",
        ) {
            Ok(image) => image.image_size_bytes(),
            Err(e) => return Some(Err(e)),
        };

        // Now create a new `BiosImage` with the full image data.
        let full_image = match self.read_bios_image_at_offset(
            self.current_offset,
            image_size,
            "parse full BIOS image",
        ) {
            Ok(image) => image,
            Err(e) => return Some(Err(e)),
        };

        self.last_found = full_image.is_last();

        // Advance to next image (aligned to 512 bytes).
        self.current_offset += image_size;
        self.current_offset = self.current_offset.align_up(Alignment::new::<512>())?;

        Some(Ok(full_image))
    }
}

pub(crate) struct Vbios {
    fwsec_image: FwSecBiosImage,
}

impl Vbios {
    /// Probe for VBIOS extraction.
    ///
    /// Once the VBIOS object is built, `bar0` is not read for [`Vbios`] purposes anymore.
    pub(crate) fn new(dev: &device::Device, bar0: &Bar0) -> Result<Vbios> {
        // Images to extract from iteration
        let mut pci_at_image: Option<PciAtBiosImage> = None;
        let mut first_fwsec_image: Option<FwSecBiosBuilder> = None;
        let mut second_fwsec_image: Option<FwSecBiosBuilder> = None;

        // Parse all VBIOS images in the ROM
        for image_result in VbiosIterator::new(dev, bar0)? {
            let full_image = image_result?;

            dev_dbg!(
                dev,
                "Found BIOS image: size: {:#x}, type: {}, last: {}\n",
                full_image.image_size_bytes(),
                full_image.image_type_str(),
                full_image.is_last()
            );

            // Get references to images we will need after the loop, in order to
            // setup the falcon data offset.
            match full_image {
                BiosImage::PciAt(image) => {
                    pci_at_image = Some(image);
                }
                BiosImage::FwSec(image) => {
                    if first_fwsec_image.is_none() {
                        first_fwsec_image = Some(image);
                    } else {
                        second_fwsec_image = Some(image);
                    }
                }
                // For now we don't need to handle these
                BiosImage::Efi(_image) => {}
                BiosImage::Nbsi(_image) => {}
            }
        }

        // Using all the images, setup the falcon data pointer in Fwsec.
        if let (Some(mut second), Some(first), Some(pci_at)) =
            (second_fwsec_image, first_fwsec_image, pci_at_image)
        {
            second
                .setup_falcon_data(&pci_at, &first)
                .inspect_err(|e| dev_err!(dev, "Falcon data setup failed: {:?}\n", e))?;
            Ok(Vbios {
                fwsec_image: second.build()?,
            })
        } else {
            dev_err!(
                dev,
                "Missing required images for falcon data setup, skipping\n"
            );
            Err(EINVAL)
        }
    }

    pub(crate) fn fwsec_image(&self) -> &FwSecBiosImage {
        &self.fwsec_image
    }
}

/// PCI Data Structure as defined in PCI Firmware Specification
#[derive(Debug, Clone)]
#[repr(C)]
struct PcirStruct {
    /// PCI Data Structure signature ("PCIR" or "NPDS")
    signature: [u8; 4],
    /// PCI Vendor ID (e.g., 0x10DE for NVIDIA)
    vendor_id: u16,
    /// PCI Device ID
    device_id: u16,
    /// Device List Pointer
    device_list_ptr: u16,
    /// PCI Data Structure Length
    pci_data_struct_len: u16,
    /// PCI Data Structure Revision
    pci_data_struct_rev: u8,
    /// Class code (3 bytes, 0x03 for display controller)
    class_code: [u8; 3],
    /// Size of this image in 512-byte blocks
    image_len: u16,
    /// Revision Level of the Vendor's ROM
    vendor_rom_rev: u16,
    /// ROM image type (0x00 = PC-AT compatible, 0x03 = EFI, 0x70 = NBSI)
    code_type: u8,
    /// Last image indicator (0x00 = Not last image, 0x80 = Last image)
    last_image: u8,
    /// Maximum Run-time Image Length (units of 512 bytes)
    max_runtime_image_len: u16,
}

impl PcirStruct {
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        if data.len() < core::mem::size_of::<PcirStruct>() {
            dev_err!(dev, "Not enough data for PcirStruct\n");
            return Err(EINVAL);
        }

        let mut signature = [0u8; 4];
        signature.copy_from_slice(&data[0..4]);

        // Signature should be "PCIR" (0x52494350) or "NPDS" (0x5344504e).
        if &signature != b"PCIR" && &signature != b"NPDS" {
            dev_err!(dev, "Invalid signature for PcirStruct: {:?}\n", signature);
            return Err(EINVAL);
        }

        let mut class_code = [0u8; 3];
        class_code.copy_from_slice(&data[13..16]);

        let image_len = u16::from_le_bytes([data[16], data[17]]);
        if image_len == 0 {
            dev_err!(dev, "Invalid image length: 0\n");
            return Err(EINVAL);
        }

        Ok(PcirStruct {
            signature,
            vendor_id: u16::from_le_bytes([data[4], data[5]]),
            device_id: u16::from_le_bytes([data[6], data[7]]),
            device_list_ptr: u16::from_le_bytes([data[8], data[9]]),
            pci_data_struct_len: u16::from_le_bytes([data[10], data[11]]),
            pci_data_struct_rev: data[12],
            class_code,
            image_len,
            vendor_rom_rev: u16::from_le_bytes([data[18], data[19]]),
            code_type: data[20],
            last_image: data[21],
            max_runtime_image_len: u16::from_le_bytes([data[22], data[23]]),
        })
    }

    /// Check if this is the last image in the ROM.
    fn is_last(&self) -> bool {
        self.last_image & LAST_IMAGE_BIT_MASK != 0
    }

    /// Calculate image size in bytes from 512-byte blocks.
    fn image_size_bytes(&self) -> usize {
        self.image_len as usize * 512
    }
}

/// BIOS Information Table (BIT) Header.
///
/// This is the head of the BIT table, that is used to locate the Falcon data. The BIT table (with
/// its header) is in the [`PciAtBiosImage`] and the falcon data it is pointing to is in the
/// [`FwSecBiosImage`].
#[derive(Debug, Clone, Copy)]
#[repr(C)]
struct BitHeader {
    /// 0h: BIT Header Identifier (BMP=0x7FFF/BIT=0xB8FF)
    id: u16,
    /// 2h: BIT Header Signature ("BIT\0")
    signature: [u8; 4],
    /// 6h: Binary Coded Decimal Version, ex: 0x0100 is 1.00.
    bcd_version: u16,
    /// 8h: Size of BIT Header (in bytes)
    header_size: u8,
    /// 9h: Size of BIT Tokens (in bytes)
    token_size: u8,
    /// 10h: Number of token entries that follow
    token_entries: u8,
    /// 11h: BIT Header Checksum
    checksum: u8,
}

impl BitHeader {
    fn new(data: &[u8]) -> Result<Self> {
        if data.len() < core::mem::size_of::<Self>() {
            return Err(EINVAL);
        }

        let mut signature = [0u8; 4];
        signature.copy_from_slice(&data[2..6]);

        // Check header ID and signature
        let id = u16::from_le_bytes([data[0], data[1]]);
        if id != 0xB8FF || &signature != b"BIT\0" {
            return Err(EINVAL);
        }

        Ok(BitHeader {
            id,
            signature,
            bcd_version: u16::from_le_bytes([data[6], data[7]]),
            header_size: data[8],
            token_size: data[9],
            token_entries: data[10],
            checksum: data[11],
        })
    }
}

/// BIT Token Entry: Records in the BIT table followed by the BIT header.
#[derive(Debug, Clone, Copy)]
#[expect(dead_code)]
struct BitToken {
    /// 00h: Token identifier
    id: u8,
    /// 01h: Version of the token data
    data_version: u8,
    /// 02h: Size of token data in bytes
    data_size: u16,
    /// 04h: Offset to the token data
    data_offset: u16,
}

// Define the token ID for the Falcon data
const BIT_TOKEN_ID_FALCON_DATA: u8 = 0x70;

impl BitToken {
    /// Find a BIT token entry by BIT ID in a PciAtBiosImage
    fn from_id(image: &PciAtBiosImage, token_id: u8) -> Result<Self> {
        let header = &image.bit_header;

        // Offset to the first token entry
        let tokens_start = image.bit_offset + header.header_size as usize;

        for i in 0..header.token_entries as usize {
            let entry_offset = tokens_start + (i * header.token_size as usize);

            // Make sure we don't go out of bounds
            if entry_offset + header.token_size as usize > image.base.data.len() {
                return Err(EINVAL);
            }

            // Check if this token has the requested ID
            if image.base.data[entry_offset] == token_id {
                return Ok(BitToken {
                    id: image.base.data[entry_offset],
                    data_version: image.base.data[entry_offset + 1],
                    data_size: u16::from_le_bytes([
                        image.base.data[entry_offset + 2],
                        image.base.data[entry_offset + 3],
                    ]),
                    data_offset: u16::from_le_bytes([
                        image.base.data[entry_offset + 4],
                        image.base.data[entry_offset + 5],
                    ]),
                });
            }
        }

        // Token not found
        Err(ENOENT)
    }
}

/// PCI ROM Expansion Header as defined in PCI Firmware Specification.
///
/// This is header is at the beginning of every image in the set of images in the ROM. It contains
/// a pointer to the PCI Data Structure which describes the image. For "NBSI" images (NoteBook
/// System Information), the ROM header deviates from the standard and contains an offset to the
/// NBSI image however we do not yet parse that in this module and keep it for future reference.
#[derive(Debug, Clone, Copy)]
#[expect(dead_code)]
struct PciRomHeader {
    /// 00h: Signature (0xAA55)
    signature: u16,
    /// 02h: Reserved bytes for processor architecture unique data (20 bytes)
    reserved: [u8; 20],
    /// 16h: NBSI Data Offset (NBSI-specific, offset from header to NBSI image)
    nbsi_data_offset: Option<u16>,
    /// 18h: Pointer to PCI Data Structure (offset from start of ROM image)
    pci_data_struct_offset: u16,
    /// 1Ah: Size of block (this is NBSI-specific)
    size_of_block: Option<u32>,
}

impl PciRomHeader {
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        if data.len() < 26 {
            // Need at least 26 bytes to read pciDataStrucPtr and sizeOfBlock.
            return Err(EINVAL);
        }

        let signature = u16::from_le_bytes([data[0], data[1]]);

        // Check for valid ROM signatures.
        match signature {
            0xAA55 | 0xBB77 | 0x4E56 => {}
            _ => {
                dev_err!(dev, "ROM signature unknown {:#x}\n", signature);
                return Err(EINVAL);
            }
        }

        // Read the pointer to the PCI Data Structure at offset 0x18.
        let pci_data_struct_ptr = u16::from_le_bytes([data[24], data[25]]);

        // Try to read optional fields if enough data.
        let mut size_of_block = None;
        let mut nbsi_data_offset = None;

        if data.len() >= 30 {
            // Read size_of_block at offset 0x1A.
            size_of_block = Some(
                u32::from(data[29]) << 24
                    | u32::from(data[28]) << 16
                    | u32::from(data[27]) << 8
                    | u32::from(data[26]),
            );
        }

        // For NBSI images, try to read the nbsiDataOffset at offset 0x16.
        if data.len() >= 24 {
            nbsi_data_offset = Some(u16::from_le_bytes([data[22], data[23]]));
        }

        Ok(PciRomHeader {
            signature,
            reserved: [0u8; 20],
            pci_data_struct_offset: pci_data_struct_ptr,
            size_of_block,
            nbsi_data_offset,
        })
    }
}

/// NVIDIA PCI Data Extension Structure.
///
/// This is similar to the PCI Data Structure, but is Nvidia-specific and is placed right after the
/// PCI Data Structure. It contains some fields that are redundant with the PCI Data Structure, but
/// are needed for traversing the BIOS images. It is expected to be present in all BIOS images
/// except for NBSI images.
#[derive(Debug, Clone)]
#[repr(C)]
struct NpdeStruct {
    /// 00h: Signature ("NPDE")
    signature: [u8; 4],
    /// 04h: NVIDIA PCI Data Extension Revision
    npci_data_ext_rev: u16,
    /// 06h: NVIDIA PCI Data Extension Length
    npci_data_ext_len: u16,
    /// 08h: Sub-image Length (in 512-byte units)
    subimage_len: u16,
    /// 0Ah: Last image indicator flag
    last_image: u8,
}

impl NpdeStruct {
    fn new(dev: &device::Device, data: &[u8]) -> Option<Self> {
        if data.len() < core::mem::size_of::<Self>() {
            dev_dbg!(dev, "Not enough data for NpdeStruct\n");
            return None;
        }

        let mut signature = [0u8; 4];
        signature.copy_from_slice(&data[0..4]);

        // Signature should be "NPDE" (0x4544504E).
        if &signature != b"NPDE" {
            dev_dbg!(dev, "Invalid signature for NpdeStruct: {:?}\n", signature);
            return None;
        }

        let subimage_len = u16::from_le_bytes([data[8], data[9]]);
        if subimage_len == 0 {
            dev_dbg!(dev, "Invalid subimage length: 0\n");
            return None;
        }

        Some(NpdeStruct {
            signature,
            npci_data_ext_rev: u16::from_le_bytes([data[4], data[5]]),
            npci_data_ext_len: u16::from_le_bytes([data[6], data[7]]),
            subimage_len,
            last_image: data[10],
        })
    }

    /// Check if this is the last image in the ROM.
    fn is_last(&self) -> bool {
        self.last_image & LAST_IMAGE_BIT_MASK != 0
    }

    /// Calculate image size in bytes from 512-byte blocks.
    fn image_size_bytes(&self) -> usize {
        self.subimage_len as usize * 512
    }

    /// Try to find NPDE in the data, the NPDE is right after the PCIR.
    fn find_in_data(
        dev: &device::Device,
        data: &[u8],
        rom_header: &PciRomHeader,
        pcir: &PcirStruct,
    ) -> Option<Self> {
        // Calculate the offset where NPDE might be located
        // NPDE should be right after the PCIR structure, aligned to 16 bytes
        let pcir_offset = rom_header.pci_data_struct_offset as usize;
        let npde_start = (pcir_offset + pcir.pci_data_struct_len as usize + 0x0F) & !0x0F;

        // Check if we have enough data
        if npde_start + core::mem::size_of::<Self>() > data.len() {
            dev_dbg!(dev, "Not enough data for NPDE\n");
            return None;
        }

        // Try to create NPDE from the data
        NpdeStruct::new(dev, &data[npde_start..])
    }
}

// Use a macro to implement BiosImage enum and methods. This avoids having to
// repeat each enum type when implementing functions like base() in BiosImage.
macro_rules! bios_image {
    (
        $($variant:ident: $class:ident),* $(,)?
    ) => {
        // BiosImage enum with variants for each image type
        enum BiosImage {
            $($variant($class)),*
        }

        impl BiosImage {
            /// Get a reference to the common BIOS image data regardless of type
            fn base(&self) -> &BiosImageBase {
                match self {
                    $(Self::$variant(img) => &img.base),*
                }
            }

            /// Returns a string representing the type of BIOS image
            fn image_type_str(&self) -> &'static str {
                match self {
                    $(Self::$variant(_) => stringify!($variant)),*
                }
            }
        }
    }
}

impl BiosImage {
    /// Check if this is the last image.
    fn is_last(&self) -> bool {
        let base = self.base();

        // For NBSI images (type == 0x70), return true as they're
        // considered the last image
        if matches!(self, Self::Nbsi(_)) {
            return true;
        }

        // For other image types, check the NPDE first if available
        if let Some(ref npde) = base.npde {
            return npde.is_last();
        }

        // Otherwise, fall back to checking the PCIR last_image flag
        base.pcir.is_last()
    }

    /// Get the image size in bytes.
    fn image_size_bytes(&self) -> usize {
        let base = self.base();

        // Prefer NPDE image size if available
        if let Some(ref npde) = base.npde {
            return npde.image_size_bytes();
        }

        // Otherwise, fall back to the PCIR image size
        base.pcir.image_size_bytes()
    }

    /// Create a [`BiosImageBase`] from a byte slice and convert it to a [`BiosImage`] which
    /// triggers the constructor of the specific BiosImage enum variant.
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        let base = BiosImageBase::new(dev, data)?;
        let image = base.into_image().inspect_err(|e| {
            dev_err!(dev, "Failed to create BiosImage: {:?}\n", e);
        })?;

        Ok(image)
    }
}

bios_image! {
    PciAt: PciAtBiosImage,   // PCI-AT compatible BIOS image
    Efi: EfiBiosImage,       // EFI (Extensible Firmware Interface)
    Nbsi: NbsiBiosImage,     // NBSI (Nvidia Bios System Interface)
    FwSec: FwSecBiosBuilder, // FWSEC (Firmware Security)
}

/// The PciAt BIOS image is typically the first BIOS image type found in the BIOS image chain.
///
/// It contains the BIT header and the BIT tokens.
struct PciAtBiosImage {
    base: BiosImageBase,
    bit_header: BitHeader,
    bit_offset: usize,
}

struct EfiBiosImage {
    base: BiosImageBase,
    // EFI-specific fields can be added here in the future.
}

struct NbsiBiosImage {
    base: BiosImageBase,
    // NBSI-specific fields can be added here in the future.
}

struct FwSecBiosBuilder {
    base: BiosImageBase,
    /// These are temporary fields that are used during the construction of the
    /// [`FwSecBiosBuilder`].
    ///
    /// Once FwSecBiosBuilder is constructed, the `falcon_ucode_offset` will be copied into a new
    /// [`FwSecBiosImage`].
    ///
    /// The offset of the Falcon data from the start of Fwsec image.
    falcon_data_offset: Option<usize>,
    /// The [`PmuLookupTable`] starts at the offset of the falcon data pointer.
    pmu_lookup_table: Option<PmuLookupTable>,
    /// The offset of the Falcon ucode.
    falcon_ucode_offset: Option<usize>,
}

/// The [`FwSecBiosImage`] structure contains the PMU table and the Falcon Ucode.
///
/// The PMU table contains voltage/frequency tables as well as a pointer to the Falcon Ucode.
pub(crate) struct FwSecBiosImage {
    base: BiosImageBase,
    /// The offset of the Falcon ucode.
    falcon_ucode_offset: usize,
}

// Convert from BiosImageBase to BiosImage
impl TryFrom<BiosImageBase> for BiosImage {
    type Error = Error;

    fn try_from(base: BiosImageBase) -> Result<Self> {
        match base.pcir.code_type {
            0x00 => Ok(BiosImage::PciAt(base.try_into()?)),
            0x03 => Ok(BiosImage::Efi(EfiBiosImage { base })),
            0x70 => Ok(BiosImage::Nbsi(NbsiBiosImage { base })),
            0xE0 => Ok(BiosImage::FwSec(FwSecBiosBuilder {
                base,
                falcon_data_offset: None,
                pmu_lookup_table: None,
                falcon_ucode_offset: None,
            })),
            _ => Err(EINVAL),
        }
    }
}

/// BIOS Image structure containing various headers and reference fields to all BIOS images.
///
/// Each BiosImage type has a BiosImageBase type along with other image-specific fields. Note that
/// Rust favors composition of types over inheritance.
#[expect(dead_code)]
struct BiosImageBase {
    /// Used for logging.
    dev: ARef<device::Device>,
    /// PCI ROM Expansion Header
    rom_header: PciRomHeader,
    /// PCI Data Structure
    pcir: PcirStruct,
    /// NVIDIA PCI Data Extension (optional)
    npde: Option<NpdeStruct>,
    /// Image data (includes ROM header and PCIR)
    data: KVec<u8>,
}

impl BiosImageBase {
    fn into_image(self) -> Result<BiosImage> {
        BiosImage::try_from(self)
    }

    /// Creates a new BiosImageBase from raw byte data.
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        // Ensure we have enough data for the ROM header.
        if data.len() < 26 {
            dev_err!(dev, "Not enough data for ROM header\n");
            return Err(EINVAL);
        }

        // Parse the ROM header.
        let rom_header = PciRomHeader::new(dev, &data[0..26])
            .inspect_err(|e| dev_err!(dev, "Failed to create PciRomHeader: {:?}\n", e))?;

        // Get the PCI Data Structure using the pointer from the ROM header.
        let pcir_offset = rom_header.pci_data_struct_offset as usize;
        let pcir_data = data
            .get(pcir_offset..pcir_offset + core::mem::size_of::<PcirStruct>())
            .ok_or(EINVAL)
            .inspect_err(|_| {
                dev_err!(
                    dev,
                    "PCIR offset {:#x} out of bounds (data length: {})\n",
                    pcir_offset,
                    data.len()
                );
                dev_err!(
                    dev,
                    "Consider reading more data for construction of BiosImage\n"
                );
            })?;

        let pcir = PcirStruct::new(dev, pcir_data)
            .inspect_err(|e| dev_err!(dev, "Failed to create PcirStruct: {:?}\n", e))?;

        // Look for NPDE structure if this is not an NBSI image (type != 0x70).
        let npde = NpdeStruct::find_in_data(dev, data, &rom_header, &pcir);

        // Create a copy of the data.
        let mut data_copy = KVec::new();
        data_copy.extend_from_slice(data, GFP_KERNEL)?;

        Ok(BiosImageBase {
            dev: dev.into(),
            rom_header,
            pcir,
            npde,
            data: data_copy,
        })
    }
}

impl PciAtBiosImage {
    /// Find a byte pattern in a slice.
    fn find_byte_pattern(haystack: &[u8], needle: &[u8]) -> Result<usize> {
        haystack
            .windows(needle.len())
            .position(|window| window == needle)
            .ok_or(EINVAL)
    }

    /// Find the BIT header in the [`PciAtBiosImage`].
    fn find_bit_header(data: &[u8]) -> Result<(BitHeader, usize)> {
        let bit_pattern = [0xff, 0xb8, b'B', b'I', b'T', 0x00];
        let bit_offset = Self::find_byte_pattern(data, &bit_pattern)?;
        let bit_header = BitHeader::new(&data[bit_offset..])?;

        Ok((bit_header, bit_offset))
    }

    /// Get a BIT token entry from the BIT table in the [`PciAtBiosImage`]
    fn get_bit_token(&self, token_id: u8) -> Result<BitToken> {
        BitToken::from_id(self, token_id)
    }

    /// Find the Falcon data pointer structure in the [`PciAtBiosImage`].
    ///
    /// This is just a 4 byte structure that contains a pointer to the Falcon data in the FWSEC
    /// image.
    fn falcon_data_ptr(&self) -> Result<u32> {
        let token = self.get_bit_token(BIT_TOKEN_ID_FALCON_DATA)?;

        // Make sure we don't go out of bounds
        if token.data_offset as usize + 4 > self.base.data.len() {
            return Err(EINVAL);
        }

        // read the 4 bytes at the offset specified in the token
        let offset = token.data_offset as usize;
        let bytes: [u8; 4] = self.base.data[offset..offset + 4].try_into().map_err(|_| {
            dev_err!(self.base.dev, "Failed to convert data slice to array");
            EINVAL
        })?;

        let data_ptr = u32::from_le_bytes(bytes);

        if (data_ptr as usize) < self.base.data.len() {
            dev_err!(self.base.dev, "Falcon data pointer out of bounds\n");
            return Err(EINVAL);
        }

        Ok(data_ptr)
    }
}

impl TryFrom<BiosImageBase> for PciAtBiosImage {
    type Error = Error;

    fn try_from(base: BiosImageBase) -> Result<Self> {
        let data_slice = &base.data;
        let (bit_header, bit_offset) = PciAtBiosImage::find_bit_header(data_slice)?;

        Ok(PciAtBiosImage {
            base,
            bit_header,
            bit_offset,
        })
    }
}

/// The [`PmuLookupTableEntry`] structure is a single entry in the [`PmuLookupTable`].
///
/// See the [`PmuLookupTable`] description for more information.
#[repr(C, packed)]
struct PmuLookupTableEntry {
    application_id: u8,
    target_id: u8,
    data: u32,
}

impl PmuLookupTableEntry {
    fn new(data: &[u8]) -> Result<Self> {
        if data.len() < core::mem::size_of::<Self>() {
            return Err(EINVAL);
        }

        Ok(PmuLookupTableEntry {
            application_id: data[0],
            target_id: data[1],
            data: u32::from_le_bytes(data[2..6].try_into().map_err(|_| EINVAL)?),
        })
    }
}

/// The [`PmuLookupTableEntry`] structure is used to find the [`PmuLookupTableEntry`] for a given
/// application ID.
///
/// The table of entries is pointed to by the falcon data pointer in the BIT table, and is used to
/// locate the Falcon Ucode.
#[expect(dead_code)]
struct PmuLookupTable {
    version: u8,
    header_len: u8,
    entry_len: u8,
    entry_count: u8,
    table_data: KVec<u8>,
}

impl PmuLookupTable {
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        if data.len() < 4 {
            return Err(EINVAL);
        }

        let header_len = data[1] as usize;
        let entry_len = data[2] as usize;
        let entry_count = data[3] as usize;

        let required_bytes = header_len + (entry_count * entry_len);

        if data.len() < required_bytes {
            dev_err!(dev, "PmuLookupTable data length less than required\n");
            return Err(EINVAL);
        }

        // Create a copy of only the table data
        let table_data = {
            let mut ret = KVec::new();
            ret.extend_from_slice(&data[header_len..required_bytes], GFP_KERNEL)?;
            ret
        };

        // Debug logging of entries (dumps the table data to dmesg)
        for i in (header_len..required_bytes).step_by(entry_len) {
            dev_dbg!(dev, "PMU entry: {:02x?}\n", &data[i..][..entry_len]);
        }

        Ok(PmuLookupTable {
            version: data[0],
            header_len: header_len as u8,
            entry_len: entry_len as u8,
            entry_count: entry_count as u8,
            table_data,
        })
    }

    fn lookup_index(&self, idx: u8) -> Result<PmuLookupTableEntry> {
        if idx >= self.entry_count {
            return Err(EINVAL);
        }

        let index = (idx as usize) * self.entry_len as usize;
        PmuLookupTableEntry::new(&self.table_data[index..])
    }

    // find entry by type value
    fn find_entry_by_type(&self, entry_type: u8) -> Result<PmuLookupTableEntry> {
        for i in 0..self.entry_count {
            let entry = self.lookup_index(i)?;
            if entry.application_id == entry_type {
                return Ok(entry);
            }
        }

        Err(EINVAL)
    }
}

impl FwSecBiosBuilder {
    fn setup_falcon_data(
        &mut self,
        pci_at_image: &PciAtBiosImage,
        first_fwsec: &FwSecBiosBuilder,
    ) -> Result {
        let mut offset = pci_at_image.falcon_data_ptr()? as usize;
        let mut pmu_in_first_fwsec = false;

        // The falcon data pointer assumes that the PciAt and FWSEC images
        // are contiguous in memory. However, testing shows the EFI image sits in
        // between them. So calculate the offset from the end of the PciAt image
        // rather than the start of it. Compensate.
        offset -= pci_at_image.base.data.len();

        // The offset is now from the start of the first Fwsec image, however
        // the offset points to a location in the second Fwsec image. Since
        // the fwsec images are contiguous, subtract the length of the first Fwsec
        // image from the offset to get the offset to the start of the second
        // Fwsec image.
        if offset < first_fwsec.base.data.len() {
            pmu_in_first_fwsec = true;
        } else {
            offset -= first_fwsec.base.data.len();
        }

        self.falcon_data_offset = Some(offset);

        if pmu_in_first_fwsec {
            self.pmu_lookup_table = Some(PmuLookupTable::new(
                &self.base.dev,
                &first_fwsec.base.data[offset..],
            )?);
        } else {
            self.pmu_lookup_table = Some(PmuLookupTable::new(
                &self.base.dev,
                &self.base.data[offset..],
            )?);
        }

        match self
            .pmu_lookup_table
            .as_ref()
            .ok_or(EINVAL)?
            .find_entry_by_type(FALCON_UCODE_ENTRY_APPID_FWSEC_PROD)
        {
            Ok(entry) => {
                let mut ucode_offset = entry.data as usize;
                ucode_offset -= pci_at_image.base.data.len();
                if ucode_offset < first_fwsec.base.data.len() {
                    dev_err!(self.base.dev, "Falcon Ucode offset not in second Fwsec.\n");
                    return Err(EINVAL);
                }
                ucode_offset -= first_fwsec.base.data.len();
                self.falcon_ucode_offset = Some(ucode_offset);
            }
            Err(e) => {
                dev_err!(
                    self.base.dev,
                    "PmuLookupTableEntry not found, error: {:?}\n",
                    e
                );
                return Err(EINVAL);
            }
        }
        Ok(())
    }

    /// Build the final FwSecBiosImage from this builder
    fn build(self) -> Result<FwSecBiosImage> {
        let ret = FwSecBiosImage {
            base: self.base,
            falcon_ucode_offset: self.falcon_ucode_offset.ok_or(EINVAL)?,
        };

        if cfg!(debug_assertions) {
            // Print the desc header for debugging
            let desc = ret.header()?;
            dev_dbg!(ret.base.dev, "PmuLookupTableEntry desc: {:#?}\n", desc);
        }

        Ok(ret)
    }
}

impl FwSecBiosImage {
    /// Get the FwSec header ([`FalconUCodeDescV3`]).
    pub(crate) fn header(&self) -> Result<&FalconUCodeDescV3> {
        // Get the falcon ucode offset that was found in setup_falcon_data.
        let falcon_ucode_offset = self.falcon_ucode_offset;

        // Make sure the offset is within the data bounds.
        if falcon_ucode_offset + core::mem::size_of::<FalconUCodeDescV3>() > self.base.data.len() {
            dev_err!(
                self.base.dev,
                "fwsec-frts header not contained within BIOS bounds\n"
            );
            return Err(ERANGE);
        }

        // Read the first 4 bytes to get the version.
        let hdr_bytes: [u8; 4] = self.base.data[falcon_ucode_offset..falcon_ucode_offset + 4]
            .try_into()
            .map_err(|_| EINVAL)?;
        let hdr = u32::from_le_bytes(hdr_bytes);
        let ver = (hdr & 0xff00) >> 8;

        if ver != 3 {
            dev_err!(self.base.dev, "invalid fwsec firmware version: {:?}\n", ver);
            return Err(EINVAL);
        }

        // Return a reference to the FalconUCodeDescV3 structure.
        //
        // SAFETY: We have checked that `falcon_ucode_offset + size_of::<FalconUCodeDescV3>` is
        // within the bounds of `data`. Also, this data vector is from ROM, and the `data` field
        // in `BiosImageBase` is immutable after construction.
        Ok(unsafe {
            &*(self
                .base
                .data
                .as_ptr()
                .add(falcon_ucode_offset)
                .cast::<FalconUCodeDescV3>())
        })
    }

    /// Get the ucode data as a byte slice
    pub(crate) fn ucode(&self, desc: &FalconUCodeDescV3) -> Result<&[u8]> {
        let falcon_ucode_offset = self.falcon_ucode_offset;

        // The ucode data follows the descriptor.
        let ucode_data_offset = falcon_ucode_offset + desc.size();
        let size = (desc.imem_load_size + desc.dmem_load_size) as usize;

        // Get the data slice, checking bounds in a single operation.
        self.base
            .data
            .get(ucode_data_offset..ucode_data_offset + size)
            .ok_or(ERANGE)
            .inspect_err(|_| {
                dev_err!(
                    self.base.dev,
                    "fwsec ucode data not contained within BIOS bounds\n"
                )
            })
    }

    /// Get the signatures as a byte slice
    pub(crate) fn sigs(&self, desc: &FalconUCodeDescV3) -> Result<&[Bcrt30Rsa3kSignature]> {
        // The signatures data follows the descriptor.
        let sigs_data_offset = self.falcon_ucode_offset + core::mem::size_of::<FalconUCodeDescV3>();
        let sigs_size =
            desc.signature_count as usize * core::mem::size_of::<Bcrt30Rsa3kSignature>();

        // Make sure the data is within bounds.
        if sigs_data_offset + sigs_size > self.base.data.len() {
            dev_err!(
                self.base.dev,
                "fwsec signatures data not contained within BIOS bounds\n"
            );
            return Err(ERANGE);
        }

        // SAFETY: we checked that `data + sigs_data_offset + (signature_count *
        // sizeof::<Bcrt30Rsa3kSignature>()` is within the bounds of `data`.
        Ok(unsafe {
            core::slice::from_raw_parts(
                self.base
                    .data
                    .as_ptr()
                    .add(sigs_data_offset)
                    .cast::<Bcrt30Rsa3kSignature>(),
                desc.signature_count as usize,
            )
        })
    }
}
