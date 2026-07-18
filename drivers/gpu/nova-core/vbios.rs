// SPDX-License-Identifier: GPL-2.0

//! VBIOS extraction and parsing.

use kernel::{
    device,
    io::Io,
    prelude::*,
    ptr::{
        Alignable,
        Alignment, //
    },
    register,
    sizes::SZ_4K,
    sync::aref::ARef,
    transmute::FromBytes,
};

use zerocopy::FromBytes as _;

use crate::{
    driver::Bar0,
    firmware::{
        fwsec::Bcrt30Rsa3kSignature,
        FalconUCodeDesc,
        FalconUCodeDescV2,
        FalconUCodeDescV3, //
    },
    num::FromSafeCast,
};

/// BIOS Image Type from PCI Data Structure code_type field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum BiosImageType {
    /// PC-AT compatible BIOS image (x86 legacy)
    PciAt = 0x00,
    /// EFI (Extensible Firmware Interface) BIOS image
    Efi = 0x03,
    /// NBSI (Notebook System Information) BIOS image
    Nbsi = 0x70,
    /// FwSec (Firmware Security) BIOS image
    FwSec = 0xE0,
}

impl TryFrom<u8> for BiosImageType {
    type Error = Error;

    fn try_from(code: u8) -> Result<Self> {
        match code {
            0x00 => Ok(Self::PciAt),
            0x03 => Ok(Self::Efi),
            0x70 => Ok(Self::Nbsi),
            0xE0 => Ok(Self::FwSec),
            _ => Err(EINVAL),
        }
    }
}

/// Vbios Reader for constructing the VBIOS data.
struct VbiosIterator<'a> {
    dev: &'a device::Device,
    bar0: Bar0<'a>,
    /// VBIOS data vector: As BIOS images are scanned, they are added to this vector for reference
    /// or copying into other data structures. It is the entire scanned contents of the VBIOS which
    /// progressively extends. It is used so that we do not re-read any contents that are already
    /// read as we use the cumulative length read so far, and re-read any gaps as we extend the
    /// length.
    data: KVVec<u8>,
    /// Current offset of the [`Iterator`].
    current_offset: usize,
    /// Indicate whether the last image has been found.
    last_found: bool,
}

impl<'a> VbiosIterator<'a> {
    /// The offset of the VBIOS ROM in the BAR0 space.
    const ROM_OFFSET: usize = 0x300000;
    /// The maximum length of the VBIOS ROM to scan into.
    const BIOS_MAX_SCAN_LEN: usize = 0x100000;
    /// The size to read ahead when parsing initial BIOS image headers.
    const BIOS_READ_AHEAD_SIZE: usize = 1024;

    /// Return the byte offset where the PCI Expansion ROM images begin in the GPU's ROM.
    ///
    /// The GPU's ROM may begin with an Init-from-ROM (IFR) header that precedes the PCI Expansion
    /// ROM images (VBIOS). When present, the PROM shadow method must parse this header to determine
    /// the offset where the PCI ROM images actually begin, and adjust all subsequent reads
    /// accordingly.
    ///
    /// On most GPUs this is not needed because the IFR microcode has already applied the ROM offset
    /// so that PROM reads transparently skip the header. On GA100, for some reason, the IFR offset
    /// is not applied to PROM reads. Therefore, the search for the PCI expansion must skip the IFR
    /// header, if found.
    fn rom_offset(dev: &device::Device, bar0: Bar0<'_>) -> Result<usize> {
        // IFR Header in VBIOS.
        register! {
            NV_PBUS_IFR_FMT_FIXED0(u32) @ 0x300000 {
                31:0    signature;
            }
        }

        register! {
            NV_PBUS_IFR_FMT_FIXED1(u32) @ 0x300004 {
                30:16   fixed_data_size;
                15:8    version => u8;
            }
        }

        register! {
            NV_PBUS_IFR_FMT_FIXED2(u32) @ 0x300008 {
                19:0 total_data_size;
            }
        }

        /// IFR signature.
        const NV_PBUS_IFR_FMT_FIXED0_SIGNATURE_VALUE: u32 = u32::from_le_bytes(*b"NVGI");
        /// ROM directory signature.
        const NV_ROM_DIRECTORY_IDENTIFIER: u32 = u32::from_le_bytes(*b"RFRD");
        /// Offset of the NV_PMGR_ROM_ADDR_OFFSET register in IFR Extended section.
        const IFR_SW_EXT_ROM_ADDR_OFFSET: usize = 4;
        /// Size of Redundant Firmware Flash Status section.
        const RFW_FLASH_STATUS_SIZE: usize = SZ_4K;
        /// Offset in the ROM Directory of the PCI Option ROM offset.
        const PCI_OPTION_ROM_OFFSET: usize = 8;

        let signature = bar0.read(NV_PBUS_IFR_FMT_FIXED0).signature();

        if signature == NV_PBUS_IFR_FMT_FIXED0_SIGNATURE_VALUE {
            let fixed1 = bar0.read(NV_PBUS_IFR_FMT_FIXED1);

            match fixed1.version() {
                1 | 2 => {
                    let fixed_data_size = usize::from(fixed1.fixed_data_size());
                    let pmgr_rom_addr_offset = fixed_data_size + IFR_SW_EXT_ROM_ADDR_OFFSET;
                    bar0.try_read32(Self::ROM_OFFSET + pmgr_rom_addr_offset)
                        .map(usize::from_safe_cast)
                }
                3 => {
                    let fixed2 = bar0.read(NV_PBUS_IFR_FMT_FIXED2);
                    let total_data_size = usize::from(fixed2.total_data_size());
                    let flash_status_offset =
                        usize::from_safe_cast(bar0.try_read32(Self::ROM_OFFSET + total_data_size)?);
                    let dir_offset = flash_status_offset + RFW_FLASH_STATUS_SIZE;
                    let dir_sig = bar0.try_read32(Self::ROM_OFFSET + dir_offset)?;
                    if dir_sig != NV_ROM_DIRECTORY_IDENTIFIER {
                        dev_err!(dev, "could not find IFR ROM directory\n");
                        return Err(EINVAL);
                    }
                    bar0.try_read32(Self::ROM_OFFSET + dir_offset + PCI_OPTION_ROM_OFFSET)
                        .map(usize::from_safe_cast)
                }
                _ => {
                    dev_err!(dev, "unsupported IFR header version {}\n", fixed1.version());
                    Err(EINVAL)
                }
            }
        } else {
            Ok(0)
        }
    }

    fn new(dev: &'a device::Device, bar0: Bar0<'a>) -> Result<Self> {
        Ok(Self {
            dev,
            bar0,
            data: KVVec::new(),
            current_offset: Self::rom_offset(dev, bar0)?,
            last_found: false,
        })
    }

    /// Read bytes from the ROM at the current end of the data vector.
    fn read_more(&mut self, len: usize) -> Result {
        let start = self.data.len();
        let end = start + len;

        if end > Self::BIOS_MAX_SCAN_LEN {
            dev_err!(self.dev, "Error: exceeded BIOS scan limit.\n");
            return Err(EINVAL);
        }

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
        for addr in (start..end).step_by(core::mem::size_of::<u32>()) {
            // Read 32-bit word from the VBIOS ROM
            let word = self.bar0.try_read32(Self::ROM_OFFSET + addr)?;

            // Convert the `u32` to a 4 byte array and push each byte.
            word.to_ne_bytes()
                .iter()
                .try_for_each(|&b| self.data.push(b, GFP_KERNEL))?;
        }

        Ok(())
    }

    /// Read bytes at a specific offset, filling any gap.
    fn read_more_at_offset(&mut self, offset: usize, len: usize) -> Result {
        let end = offset.checked_add(len).ok_or(EINVAL)?;

        self.read_more(end.saturating_sub(self.data.len()))
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
        let end = offset.checked_add(len).ok_or(EINVAL)?;
        if end > self.data.len() {
            self.read_more_at_offset(offset, len).inspect_err(|e| {
                dev_err!(
                    self.dev,
                    "Failed to read more at offset {:#x}: {:?}\n",
                    offset,
                    e
                )
            })?;
        }

        BiosImage::new(self.dev, &self.data[offset..end]).inspect_err(|err| {
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

        if self.current_offset >= Self::BIOS_MAX_SCAN_LEN {
            dev_err!(self.dev, "Error: exceeded BIOS scan limit, stopping scan\n");
            return None;
        }

        // Parse image headers first to get image size.
        let image_size = match self.read_bios_image_at_offset(
            self.current_offset,
            Self::BIOS_READ_AHEAD_SIZE,
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
    pub(crate) fn new(dev: &device::Device, bar0: Bar0<'_>) -> Result<Vbios> {
        // Images to extract from iteration
        let mut pci_at_image: Option<PciAtBiosImage> = None;
        let mut fwsec_section: Option<KVVec<u8>> = None;

        // Parse all VBIOS images in the ROM
        for image_result in VbiosIterator::new(dev, bar0)? {
            let image = image_result?;

            dev_dbg!(
                dev,
                "Found BIOS image: size: {:#x}, type: {:?}, last: {}\n",
                image.image_size_bytes(),
                image.image_type(),
                image.is_last()
            );

            // Once we have found the first FWSEC image, grab all data after that as the FWSEC
            // section. This is indexed as one logical block to build the final FWSEC image.
            if let Some(data) = fwsec_section.as_mut() {
                data.extend_from_slice(&image.data, GFP_KERNEL)?;
                continue;
            }

            // Convert to a specific image type
            match BiosImageType::try_from(image.pcir.code_type) {
                Ok(BiosImageType::PciAt) => {
                    // Silently ignore any extra PCI-AT images.
                    if pci_at_image.is_none() {
                        pci_at_image = Some(PciAtBiosImage::try_from(image)?);
                    }
                }
                Ok(BiosImageType::FwSec) => fwsec_section = Some(image.data),
                _ => {
                    // Ignore other image types or unknown types
                }
            }
        }

        // Using all the images, setup the falcon data pointer in Fwsec.
        let (Some(pci_at), Some(fwsec_section)) = (pci_at_image, fwsec_section) else {
            dev_err!(
                dev,
                "Missing required images for falcon data setup, skipping\n"
            );
            return Err(EINVAL);
        };

        let fwsec_image = FwSecBiosImage::new(dev, pci_at, fwsec_section)
            .inspect_err(|e| dev_err!(dev, "Falcon data setup failed: {:?}\n", e))?;

        Ok(Vbios { fwsec_image })
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

// SAFETY: all bit patterns are valid for `PcirStruct`.
unsafe impl FromBytes for PcirStruct {}

impl PcirStruct {
    /// The bit in `last_image` that indicates the last image.
    const LAST_IMAGE_BIT_MASK: u8 = 0x80;

    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        let (pcir, _) = PcirStruct::from_bytes_copy_prefix(data).ok_or(EINVAL)?;

        // Signature should be "PCIR" (0x52494350) or "NPDS" (0x5344504e).
        if &pcir.signature != b"PCIR" && &pcir.signature != b"NPDS" {
            dev_err!(
                dev,
                "Invalid signature for PcirStruct: {:?}\n",
                pcir.signature
            );
            return Err(EINVAL);
        }

        if pcir.image_len == 0 {
            dev_err!(dev, "Invalid image length: 0\n");
            return Err(EINVAL);
        }

        Ok(pcir)
    }

    /// Check if this is the last image in the ROM.
    fn is_last(&self) -> bool {
        self.last_image & Self::LAST_IMAGE_BIT_MASK != 0
    }

    /// Calculate image size in bytes from 512-byte blocks.
    fn image_size_bytes(&self) -> usize {
        usize::from(self.image_len) * 512
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

// SAFETY: all bit patterns are valid for `BitHeader`.
unsafe impl FromBytes for BitHeader {}

impl BitHeader {
    fn new(data: &[u8]) -> Result<Self> {
        let (header, _) = BitHeader::from_bytes_copy_prefix(data).ok_or(EINVAL)?;

        // Check header ID and signature
        if header.id != 0xB8FF || &header.signature != b"BIT\0" {
            return Err(EINVAL);
        }

        Ok(header)
    }
}

/// BIT Token Entry: Records in the BIT table followed by the BIT header.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
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

// SAFETY: all bit patterns are valid for `BitToken`.
unsafe impl FromBytes for BitToken {}

impl BitToken {
    /// BIT token ID for Falcon data.
    const ID_FALCON_DATA: u8 = 0x70;

    /// Find a BIT token entry by BIT ID in a PciAtBiosImage
    fn from_id(image: &PciAtBiosImage, token_id: u8) -> Result<Self> {
        let header = &image.bit_header;
        let entry_size = usize::from(header.token_size);

        // Offset to the first token entry
        let tokens_start = image.bit_offset + usize::from(header.header_size);

        for i in 0..usize::from(header.token_entries) {
            let entry_offset = i
                .checked_mul(entry_size)
                .and_then(|offset| tokens_start.checked_add(offset))
                .ok_or(EINVAL)?;
            let entry = image
                .base
                .data
                .get(entry_offset..)
                .and_then(|data| data.get(..entry_size))
                .ok_or(EINVAL)?;

            let (token, _) = BitToken::from_bytes_copy_prefix(entry).ok_or(EINVAL)?;

            // Check if this token has the requested ID
            if token.id == token_id {
                return Ok(token);
            }
        }

        // Token not found
        Err(ENOENT)
    }
}

/// PCI ROM Expansion Header as defined in PCI Firmware Specification.
///
/// This header is at the beginning of every image in the set of images in the ROM. It contains a
/// pointer to the PCI Data Structure which describes the image.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
struct PciRomHeader {
    /// 00h: Signature (0xAA55)
    signature: u16,
    /// 02h: Reserved bytes for processor architecture unique data (22 bytes)
    reserved: [u8; 22],
    /// 18h: Pointer to PCI Data Structure (offset from start of ROM image)
    pci_data_struct_offset: u16,
}

// SAFETY: all bit patterns are valid for `PciRomHeader`.
unsafe impl FromBytes for PciRomHeader {}

impl PciRomHeader {
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        let (rom_header, _) = PciRomHeader::from_bytes_copy_prefix(data)
            .ok_or(EINVAL)
            .inspect_err(|_| dev_err!(dev, "Not enough data for ROM header\n"))?;

        // Check for valid ROM signatures.
        match rom_header.signature {
            0xAA55 | 0x4E56 => {}
            _ => {
                dev_err!(dev, "ROM signature unknown {:#x}\n", rom_header.signature);
                return Err(EINVAL);
            }
        }

        Ok(rom_header)
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

// SAFETY: all bit patterns are valid for `NpdeStruct`.
unsafe impl FromBytes for NpdeStruct {}

impl NpdeStruct {
    /// The bit in `last_image` that indicates the last image.
    const LAST_IMAGE_BIT_MASK: u8 = 0x80;

    fn new(dev: &device::Device, data: &[u8]) -> Option<Self> {
        let (npde, _) = NpdeStruct::from_bytes_copy_prefix(data)?;

        // Signature should be "NPDE" (0x4544504E).
        if &npde.signature != b"NPDE" {
            dev_dbg!(
                dev,
                "Invalid signature for NpdeStruct: {:?}\n",
                npde.signature
            );
            return None;
        }

        if npde.subimage_len == 0 {
            dev_dbg!(dev, "Invalid subimage length: 0\n");
            return None;
        }

        Some(npde)
    }

    /// Check if this is the last image in the ROM.
    fn is_last(&self) -> bool {
        self.last_image & Self::LAST_IMAGE_BIT_MASK != 0
    }

    /// Calculate image size in bytes from 512-byte blocks.
    fn image_size_bytes(&self) -> usize {
        usize::from(self.subimage_len) * 512
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
        let pcir_offset = usize::from(rom_header.pci_data_struct_offset);
        let npde_start = (pcir_offset + usize::from(pcir.pci_data_struct_len) + 0x0F) & !0x0F;

        // Check if we have enough data
        if npde_start + core::mem::size_of::<Self>() > data.len() {
            dev_dbg!(dev, "Not enough data for NPDE\n");
            return None;
        }

        // Try to create NPDE from the data
        NpdeStruct::new(dev, &data[npde_start..])
    }
}

/// The PciAt BIOS image is typically the first BIOS image type found in the BIOS image chain.
///
/// It contains the BIT header and the BIT tokens.
struct PciAtBiosImage {
    base: BiosImage,
    bit_header: BitHeader,
    bit_offset: usize,
}

/// The [`FwSecBiosImage`] structure contains the PMU table and the Falcon Ucode.
///
/// The PMU table contains voltage/frequency tables as well as a pointer to the Falcon Ucode.
pub(crate) struct FwSecBiosImage {
    /// Used for logging.
    dev: ARef<device::Device>,
    /// FWSEC data.
    data: KVVec<u8>,
    /// The offset of the Falcon ucode.
    falcon_ucode_offset: usize,
}

/// BIOS Image structure containing various headers and reference fields to all BIOS images.
///
/// A BiosImage struct is embedded into all image types and implements common operations.
struct BiosImage {
    /// PCI Data Structure
    pcir: PcirStruct,
    /// NVIDIA PCI Data Extension (optional)
    npde: Option<NpdeStruct>,
    /// Image data (includes ROM header and PCIR)
    data: KVVec<u8>,
}

impl BiosImage {
    /// Get the image size in bytes.
    fn image_size_bytes(&self) -> usize {
        // Prefer NPDE image size if available
        if let Some(ref npde) = self.npde {
            npde.image_size_bytes()
        } else {
            // Otherwise, fall back to the PCIR image size
            self.pcir.image_size_bytes()
        }
    }

    /// Get the BIOS image type.
    fn image_type(&self) -> Result<BiosImageType> {
        BiosImageType::try_from(self.pcir.code_type)
    }

    /// Check if this is the last image.
    fn is_last(&self) -> bool {
        // For NBSI images, return true as they're considered the last image.
        if self.image_type() == Ok(BiosImageType::Nbsi) {
            return true;
        }

        // For other image types, check the NPDE first if available
        if let Some(ref npde) = self.npde {
            return npde.is_last();
        }

        // Otherwise, fall back to checking the PCIR last_image flag
        self.pcir.is_last()
    }

    /// Creates a new BiosImage from raw byte data.
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        // Parse the ROM header.
        let rom_header = PciRomHeader::new(dev, data)?;

        // Get the PCI Data Structure using the pointer from the ROM header.
        let pcir_offset = usize::from(rom_header.pci_data_struct_offset);
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
        let mut data_copy = KVVec::new();
        data_copy.extend_from_slice(data, GFP_KERNEL)?;

        Ok(BiosImage {
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

    /// Find the Falcon data offset from the start of the FWSEC region.
    ///
    /// The BIT table contains a 4-byte pointer to the Falcon data. Testing shows this pointer
    /// treats the PCI-AT and FWSEC images as logically contiguous even when an EFI image sits in
    /// between them, so subtract the PCI-AT image size here to convert it to a FWSEC-relative
    /// offset.
    fn falcon_data_offset(&self, dev: &device::Device) -> Result<usize> {
        let token = self.get_bit_token(BitToken::ID_FALCON_DATA)?;
        let offset = usize::from(token.data_offset);

        // Read the 4-byte falcon data pointer at the offset specified in the token.
        let data = &self.base.data;
        let (ptr, _) = data
            .get(offset..)
            .and_then(u32::from_bytes_copy_prefix)
            .ok_or(EINVAL)?;

        usize::from_safe_cast(ptr)
            .checked_sub(data.len())
            .ok_or(EINVAL)
            .inspect_err(|_| {
                dev_err!(dev, "Falcon data pointer out of bounds\n");
            })
    }
}

impl TryFrom<BiosImage> for PciAtBiosImage {
    type Error = Error;

    fn try_from(base: BiosImage) -> Result<Self> {
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

// SAFETY: all bit patterns are valid for `PmuLookupTableEntry`.
unsafe impl FromBytes for PmuLookupTableEntry {}

impl PmuLookupTableEntry {
    /// PMU lookup table application ID for firmware security license ucode.
    #[expect(dead_code)]
    const APPID_FIRMWARE_SEC_LIC: u8 = 0x05;
    /// PMU lookup table application ID for debug FWSEC ucode.
    #[expect(dead_code)]
    const APPID_FWSEC_DBG: u8 = 0x45;
    /// PMU lookup table application ID for production FWSEC ucode.
    const APPID_FWSEC_PROD: u8 = 0x85;
}

#[repr(C)]
struct PmuLookupTableHeader {
    version: u8,
    header_len: u8,
    entry_len: u8,
    entry_count: u8,
}

// SAFETY: all bit patterns are valid for `PmuLookupTableHeader`.
unsafe impl FromBytes for PmuLookupTableHeader {}

/// The [`PmuLookupTableEntry`] structure is used to find the [`PmuLookupTableEntry`] for a given
/// application ID.
///
/// The table of entries is pointed to by the falcon data pointer in the BIT table, and is used to
/// locate the Falcon Ucode.
struct PmuLookupTable {
    entries: KVVec<PmuLookupTableEntry>,
}

impl PmuLookupTable {
    fn new(dev: &device::Device, data: &[u8]) -> Result<Self> {
        let (header, _) = PmuLookupTableHeader::from_bytes_copy_prefix(data).ok_or(EINVAL)?;

        let header_len = usize::from(header.header_len);
        let entry_len = usize::from(header.entry_len);
        let entry_count = usize::from(header.entry_count);

        let data = data
            .get(header_len..header_len + entry_count * entry_len)
            .ok_or(EINVAL)
            .inspect_err(|_| {
                dev_err!(dev, "PmuLookupTable data length less than required\n");
            })?;

        let mut entries = KVVec::with_capacity(entry_count, GFP_KERNEL)?;
        for i in 0..entry_count {
            let (entry, _) = PmuLookupTableEntry::from_bytes_copy_prefix(&data[i * entry_len..])
                .ok_or(EINVAL)?;
            entries.push(entry, GFP_KERNEL)?;
        }

        Ok(PmuLookupTable { entries })
    }

    // find entry by type value
    fn find_entry_by_type(&self, entry_type: u8) -> Result<&PmuLookupTableEntry> {
        self.entries
            .iter()
            .find(|entry| entry.application_id == entry_type)
            .ok_or(EINVAL)
    }
}

impl FwSecBiosImage {
    /// Build the final `FwSecBiosImage` from the PCI-AT and FWSEC BIOS images.
    fn new(
        dev: &device::Device,
        pci_at_image: PciAtBiosImage,
        data: KVVec<u8>,
    ) -> Result<FwSecBiosImage> {
        let offset = pci_at_image.falcon_data_offset(dev)?;

        let pmu_lookup_data = data.get(offset..).ok_or(EINVAL)?;
        let pmu_lookup_table = PmuLookupTable::new(dev, pmu_lookup_data)?;

        let entry = pmu_lookup_table
            .find_entry_by_type(PmuLookupTableEntry::APPID_FWSEC_PROD)
            .inspect_err(|e| {
                dev_err!(dev, "PmuLookupTableEntry not found, error: {:?}\n", e);
            })?;

        let falcon_ucode_offset = usize::from_safe_cast(entry.data)
            .checked_sub(pci_at_image.base.data.len())
            .ok_or(EINVAL)
            .inspect_err(|_| {
                dev_err!(dev, "Falcon Ucode offset not in Fwsec.\n");
            })?;

        Ok(FwSecBiosImage {
            dev: dev.into(),
            data,
            falcon_ucode_offset,
        })
    }

    /// Get the FwSec header ([`FalconUCodeDesc`]).
    pub(crate) fn header(&self) -> Result<FalconUCodeDesc> {
        let data = self.data.get(self.falcon_ucode_offset..).ok_or(EINVAL)?;

        // Read the version byte from the header.
        let ver = data.get(1).copied().ok_or(EINVAL)?;
        match ver {
            2 => {
                let v2 = FalconUCodeDescV2::read_from_prefix(data)
                    .map_err(|_| EINVAL)?
                    .0;
                Ok(FalconUCodeDesc::V2(v2))
            }
            3 => {
                let v3 = FalconUCodeDescV3::from_bytes_copy_prefix(data)
                    .ok_or(EINVAL)?
                    .0;
                Ok(FalconUCodeDesc::V3(v3))
            }
            _ => {
                dev_err!(self.dev, "invalid fwsec firmware version: {:?}\n", ver);
                Err(EINVAL)
            }
        }
    }

    /// Get the ucode data as a byte slice
    pub(crate) fn ucode(&self, desc: &FalconUCodeDesc) -> Result<&[u8]> {
        let size = usize::from_safe_cast(
            desc.imem_load_size()
                .checked_add(desc.dmem_load_size())
                .ok_or(ERANGE)?,
        );

        // The ucode data follows the descriptor.
        self.data
            .get(self.falcon_ucode_offset..)
            .and_then(|data| data.get(desc.size()..))
            .and_then(|data| data.get(..size))
            .ok_or(ERANGE)
            .inspect_err(|_| {
                dev_err!(
                    self.dev,
                    "fwsec ucode data not contained within BIOS bounds\n"
                )
            })
    }

    /// Get the signatures as a byte slice
    pub(crate) fn sigs(&self, desc: &FalconUCodeDesc) -> Result<&[Bcrt30Rsa3kSignature]> {
        let hdr_size = match desc {
            FalconUCodeDesc::V2(_v2) => core::mem::size_of::<FalconUCodeDescV2>(),
            FalconUCodeDesc::V3(_v3) => core::mem::size_of::<FalconUCodeDescV3>(),
        };
        // The signatures data follows the descriptor.
        let sigs_data_offset = self.falcon_ucode_offset + hdr_size;
        let sigs_count = usize::from(desc.signature_count());
        let sigs_size = sigs_count * core::mem::size_of::<Bcrt30Rsa3kSignature>();

        // Make sure the data is within bounds.
        if sigs_data_offset + sigs_size > self.data.len() {
            dev_err!(
                self.dev,
                "fwsec signatures data not contained within BIOS bounds\n"
            );
            return Err(ERANGE);
        }

        // SAFETY: we checked that `data + sigs_data_offset + (signature_count *
        // sizeof::<Bcrt30Rsa3kSignature>()` is within the bounds of `data`.
        Ok(unsafe {
            core::slice::from_raw_parts(
                self.data
                    .as_ptr()
                    .add(sigs_data_offset)
                    .cast::<Bcrt30Rsa3kSignature>(),
                sigs_count,
            )
        })
    }
}
