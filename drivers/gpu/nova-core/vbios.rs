// SPDX-License-Identifier: GPL-2.0

//! VBIOS extraction and parsing.

// To be removed when all code is used.
#![expect(dead_code)]

use crate::driver::Bar0;
use core::convert::TryFrom;
use kernel::error::Result;
use kernel::pci;
use kernel::prelude::*;

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
    pdev: &'a pci::Device,
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
    fn new(pdev: &'a pci::Device, bar0: &'a Bar0) -> Result<Self> {
        Ok(Self {
            pdev,
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
                self.pdev.as_ref(),
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
            dev_err!(self.pdev.as_ref(), "Error: exceeded BIOS scan limit.\n");
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
                    self.pdev.as_ref(),
                    "Failed to read more at offset {:#x}: {:?}\n",
                    offset,
                    e
                )
            })?;
        }

        BiosImage::new(self.pdev, &self.data[offset..offset + len]).inspect_err(|err| {
            dev_err!(
                self.pdev.as_ref(),
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
            dev_err!(
                self.pdev.as_ref(),
                "Error: exceeded BIOS scan limit, stopping scan\n"
            );
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
        // TODO: replace with `align_up` once it lands.
        self.current_offset = self.current_offset.next_multiple_of(512);

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
    pub(crate) fn new(pdev: &pci::Device, bar0: &Bar0) -> Result<Vbios> {
        // Images to extract from iteration
        let mut pci_at_image: Option<PciAtBiosImage> = None;
        let mut first_fwsec_image: Option<FwSecBiosImage> = None;
        let mut second_fwsec_image: Option<FwSecBiosImage> = None;

        // Parse all VBIOS images in the ROM
        for image_result in VbiosIterator::new(pdev, bar0)? {
            let full_image = image_result?;

            dev_dbg!(
                pdev.as_ref(),
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
        // These are temporarily unused images and will be used in later patches.
        if let (Some(second), Some(_first), Some(_pci_at)) =
            (second_fwsec_image, first_fwsec_image, pci_at_image)
        {
            Ok(Vbios {
                fwsec_image: second,
            })
        } else {
            dev_err!(
                pdev.as_ref(),
                "Missing required images for falcon data setup, skipping\n"
            );
            Err(EINVAL)
        }
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
    fn new(pdev: &pci::Device, data: &[u8]) -> Result<Self> {
        if data.len() < core::mem::size_of::<PcirStruct>() {
            dev_err!(pdev.as_ref(), "Not enough data for PcirStruct\n");
            return Err(EINVAL);
        }

        let mut signature = [0u8; 4];
        signature.copy_from_slice(&data[0..4]);

        // Signature should be "PCIR" (0x52494350) or "NPDS" (0x5344504e).
        if &signature != b"PCIR" && &signature != b"NPDS" {
            dev_err!(
                pdev.as_ref(),
                "Invalid signature for PcirStruct: {:?}\n",
                signature
            );
            return Err(EINVAL);
        }

        let mut class_code = [0u8; 3];
        class_code.copy_from_slice(&data[13..16]);

        let image_len = u16::from_le_bytes([data[16], data[17]]);
        if image_len == 0 {
            dev_err!(pdev.as_ref(), "Invalid image length: 0\n");
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
    fn new(pdev: &pci::Device, data: &[u8]) -> Result<Self> {
        if data.len() < 26 {
            // Need at least 26 bytes to read pciDataStrucPtr and sizeOfBlock.
            return Err(EINVAL);
        }

        let signature = u16::from_le_bytes([data[0], data[1]]);

        // Check for valid ROM signatures.
        match signature {
            0xAA55 | 0xBB77 | 0x4E56 => {}
            _ => {
                dev_err!(pdev.as_ref(), "ROM signature unknown {:#x}\n", signature);
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
                (data[29] as u32) << 24
                    | (data[28] as u32) << 16
                    | (data[27] as u32) << 8
                    | (data[26] as u32),
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
    fn new(pdev: &pci::Device, data: &[u8]) -> Option<Self> {
        if data.len() < core::mem::size_of::<Self>() {
            dev_dbg!(pdev.as_ref(), "Not enough data for NpdeStruct\n");
            return None;
        }

        let mut signature = [0u8; 4];
        signature.copy_from_slice(&data[0..4]);

        // Signature should be "NPDE" (0x4544504E).
        if &signature != b"NPDE" {
            dev_dbg!(
                pdev.as_ref(),
                "Invalid signature for NpdeStruct: {:?}\n",
                signature
            );
            return None;
        }

        let subimage_len = u16::from_le_bytes([data[8], data[9]]);
        if subimage_len == 0 {
            dev_dbg!(pdev.as_ref(), "Invalid subimage length: 0\n");
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
        pdev: &pci::Device,
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
            dev_dbg!(pdev.as_ref(), "Not enough data for NPDE\n");
            return None;
        }

        // Try to create NPDE from the data
        NpdeStruct::new(pdev, &data[npde_start..])
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
    fn new(pdev: &pci::Device, data: &[u8]) -> Result<Self> {
        let base = BiosImageBase::new(pdev, data)?;
        let image = base.into_image().inspect_err(|e| {
            dev_err!(pdev.as_ref(), "Failed to create BiosImage: {:?}\n", e);
        })?;

        Ok(image)
    }
}

bios_image! {
    PciAt: PciAtBiosImage,   // PCI-AT compatible BIOS image
    Efi: EfiBiosImage,       // EFI (Extensible Firmware Interface)
    Nbsi: NbsiBiosImage,     // NBSI (Nvidia Bios System Interface)
    FwSec: FwSecBiosImage,   // FWSEC (Firmware Security)
}

struct PciAtBiosImage {
    base: BiosImageBase,
    // PCI-AT-specific fields can be added here in the future.
}

struct EfiBiosImage {
    base: BiosImageBase,
    // EFI-specific fields can be added here in the future.
}

struct NbsiBiosImage {
    base: BiosImageBase,
    // NBSI-specific fields can be added here in the future.
}

struct FwSecBiosImage {
    base: BiosImageBase,
    // FWSEC-specific fields can be added here in the future.
}

// Convert from BiosImageBase to BiosImage
impl TryFrom<BiosImageBase> for BiosImage {
    type Error = Error;

    fn try_from(base: BiosImageBase) -> Result<Self> {
        match base.pcir.code_type {
            0x00 => Ok(BiosImage::PciAt(PciAtBiosImage { base })),
            0x03 => Ok(BiosImage::Efi(EfiBiosImage { base })),
            0x70 => Ok(BiosImage::Nbsi(NbsiBiosImage { base })),
            0xE0 => Ok(BiosImage::FwSec(FwSecBiosImage { base })),
            _ => Err(EINVAL),
        }
    }
}

/// BIOS Image structure containing various headers and reference fields to all BIOS images.
///
/// Each BiosImage type has a BiosImageBase type along with other image-specific fields. Note that
/// Rust favors composition of types over inheritance.
#[derive(Debug)]
#[expect(dead_code)]
struct BiosImageBase {
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
    fn new(pdev: &pci::Device, data: &[u8]) -> Result<Self> {
        // Ensure we have enough data for the ROM header.
        if data.len() < 26 {
            dev_err!(pdev.as_ref(), "Not enough data for ROM header\n");
            return Err(EINVAL);
        }

        // Parse the ROM header.
        let rom_header = PciRomHeader::new(pdev, &data[0..26])
            .inspect_err(|e| dev_err!(pdev.as_ref(), "Failed to create PciRomHeader: {:?}\n", e))?;

        // Get the PCI Data Structure using the pointer from the ROM header.
        let pcir_offset = rom_header.pci_data_struct_offset as usize;
        let pcir_data = data
            .get(pcir_offset..pcir_offset + core::mem::size_of::<PcirStruct>())
            .ok_or(EINVAL)
            .inspect_err(|_| {
                dev_err!(
                    pdev.as_ref(),
                    "PCIR offset {:#x} out of bounds (data length: {})\n",
                    pcir_offset,
                    data.len()
                );
                dev_err!(
                    pdev.as_ref(),
                    "Consider reading more data for construction of BiosImage\n"
                );
            })?;

        let pcir = PcirStruct::new(pdev, pcir_data)
            .inspect_err(|e| dev_err!(pdev.as_ref(), "Failed to create PcirStruct: {:?}\n", e))?;

        // Look for NPDE structure if this is not an NBSI image (type != 0x70).
        let npde = NpdeStruct::find_in_data(pdev, data, &rom_header, &pcir);

        // Create a copy of the data.
        let mut data_copy = KVec::new();
        data_copy.extend_from_slice(data, GFP_KERNEL)?;

        Ok(BiosImageBase {
            rom_header,
            pcir,
            npde,
            data: data_copy,
        })
    }
}
