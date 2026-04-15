// SPDX-License-Identifier: GPL-2.0

//! Bootloader support for the FWSEC firmware.
//!
//! On Turing, the FWSEC firmware is not loaded directly, but is instead loaded through a small
//! bootloader program that performs the required DMA operations. This bootloader itself needs to
//! be loaded using PIO.

use kernel::{
    alloc::KVec,
    device::{
        self,
        Device, //
    },
    dma::Coherent,
    io::{
        register::WithBase, //
        Io,
    },
    prelude::*,
    ptr::{
        Alignable,
        Alignment, //
    },
    sizes,
    transmute::{
        AsBytes,
        FromBytes, //
    },
};

use crate::{
    driver::Bar0,
    falcon::{
        self,
        gsp::Gsp,
        Falcon,
        FalconBromParams,
        FalconDmaLoadable,
        FalconFbifMemType,
        FalconFbifTarget,
        FalconFirmware,
        FalconPioDmemLoadTarget,
        FalconPioImemLoadTarget,
        FalconPioLoadable, //
    },
    firmware::{
        fwsec::FwsecFirmware,
        request_firmware,
        BinHdr,
        FIRMWARE_VERSION, //
    },
    gpu::Chipset,
    num::FromSafeCast,
    regs,
};

/// Descriptor used by RM to figure out the requirements of the boot loader.
///
/// Most of its fields appear to be legacy and carry incorrect values, so they are left unused.
#[repr(C)]
#[derive(Debug, Clone)]
struct BootloaderDesc {
    /// Starting tag of bootloader.
    start_tag: u32,
    /// DMEM load offset - unused here as we always load at offset `0`.
    _dmem_load_off: u32,
    /// Offset of code section in the image. Unused as there is only one section in the bootloader
    /// binary.
    _code_off: u32,
    /// Size of code section in the image.
    code_size: u32,
    /// Offset of data section in the image. Unused as we build the data section ourselves.
    _data_off: u32,
    /// Size of data section in the image. Unused as we build the data section ourselves.
    _data_size: u32,
}
// SAFETY: any byte sequence is valid for this struct.
unsafe impl FromBytes for BootloaderDesc {}

/// Structure used by the boot-loader to load the rest of the code.
///
/// This has to be filled by the GPU driver and copied into DMEM at offset
/// [`BootloaderDesc.dmem_load_off`].
#[repr(C, packed)]
#[derive(Debug, Clone)]
struct BootloaderDmemDescV2 {
    /// Reserved, should always be first element.
    reserved: [u32; 4],
    /// 16B signature for secure code, 0s if no secure code.
    signature: [u32; 4],
    /// DMA context used by the bootloader while loading code/data.
    ctx_dma: u32,
    /// 256B-aligned physical FB address where code is located.
    code_dma_base: u64,
    /// Offset from `code_dma_base` where the non-secure code is located.
    ///
    /// Also used as destination IMEM offset of non-secure code as the DMA firmware object is
    /// expected to be a mirror image of its loaded state.
    ///
    /// Must be multiple of 256.
    non_sec_code_off: u32,
    /// Size of the non-secure code part.
    non_sec_code_size: u32,
    /// Offset from `code_dma_base` where the secure code is located (must be multiple of 256).
    ///
    /// Also used as destination IMEM offset of secure code as the DMA firmware object is expected
    /// to be a mirror image of its loaded state.
    ///
    /// Must be multiple of 256.
    sec_code_off: u32,
    /// Size of the secure code part.
    sec_code_size: u32,
    /// Code entry point invoked by the bootloader after code is loaded.
    code_entry_point: u32,
    /// 256B-aligned physical FB address where data is located.
    data_dma_base: u64,
    /// Size of data block (should be multiple of 256B).
    data_size: u32,
    /// Number of arguments to be passed to the target firmware being loaded.
    argc: u32,
    /// Arguments to be passed to the target firmware being loaded.
    argv: u32,
}
// SAFETY: This struct doesn't contain uninitialized bytes and doesn't have interior mutability.
unsafe impl AsBytes for BootloaderDmemDescV2 {}

/// Wrapper for [`FwsecFirmware`] that includes the bootloader performing the actual load
/// operation.
pub(crate) struct FwsecFirmwareWithBl {
    /// DMA object the bootloader will copy the firmware from.
    _firmware_dma: Coherent<[u8]>,
    /// Code of the bootloader to be loaded into non-secure IMEM.
    ucode: KVec<u8>,
    /// Descriptor to be loaded into DMEM for the bootloader to read.
    dmem_desc: BootloaderDmemDescV2,
    /// Range-validated start offset of the firmware code in IMEM.
    imem_dst_start: u16,
    /// BROM parameters of the loaded firmware.
    brom_params: FalconBromParams,
    /// Range-validated `desc.start_tag`.
    start_tag: u16,
}

impl FwsecFirmwareWithBl {
    /// Loads the bootloader firmware for `dev` and `chipset`, and wrap `firmware` so it can be
    /// loaded using it.
    pub(crate) fn new(
        firmware: FwsecFirmware,
        dev: &Device<device::Bound>,
        chipset: Chipset,
    ) -> Result<Self> {
        let fw = request_firmware(dev, chipset, "gen_bootloader", FIRMWARE_VERSION)?;
        let hdr = fw
            .data()
            .get(0..size_of::<BinHdr>())
            .and_then(BinHdr::from_bytes_copy)
            .ok_or(EINVAL)?;

        let desc = {
            let desc_offset = usize::from_safe_cast(hdr.header_offset);

            fw.data()
                .get(desc_offset..)
                .and_then(BootloaderDesc::from_bytes_copy_prefix)
                .ok_or(EINVAL)?
                .0
        };

        let ucode = {
            let ucode_start = usize::from_safe_cast(hdr.data_offset);
            let code_size = usize::from_safe_cast(desc.code_size);
            // Align to falcon block size (256 bytes).
            let aligned_code_size = code_size
                .align_up(Alignment::new::<{ falcon::MEM_BLOCK_ALIGNMENT }>())
                .ok_or(EINVAL)?;

            let mut ucode = KVec::with_capacity(aligned_code_size, GFP_KERNEL)?;
            ucode.extend_from_slice(
                fw.data()
                    .get(ucode_start..ucode_start + code_size)
                    .ok_or(EINVAL)?,
                GFP_KERNEL,
            )?;
            ucode.resize(aligned_code_size, 0, GFP_KERNEL)?;

            ucode
        };

        // `BootloaderDmemDescV2` expects the source to be a mirror image of the destination and
        // uses the same offset parameter for both.
        //
        // Thus, the start of the source object needs to be padded with the difference between the
        // destination and source offsets.
        //
        // In practice, this is expected to always be zero but is required for code correctness.
        let (align_padding, firmware_dma) = {
            let align_padding = {
                let imem_sec = firmware.imem_sec_load_params();

                imem_sec
                    .dst_start
                    .checked_sub(imem_sec.src_start)
                    .map(usize::from_safe_cast)
                    .ok_or(EOVERFLOW)?
            };

            let mut firmware_obj = KVVec::new();
            firmware_obj.extend_with(align_padding, 0u8, GFP_KERNEL)?;
            firmware_obj.extend_from_slice(firmware.ucode.0.as_slice(), GFP_KERNEL)?;

            (
                align_padding,
                Coherent::from_slice(dev, firmware_obj.as_slice(), GFP_KERNEL)?,
            )
        };

        let dmem_desc = {
            // Bootloader payload is in non-coherent system memory.
            const FALCON_DMAIDX_PHYS_SYS_NCOH: u32 = 4;

            let imem_sec = firmware.imem_sec_load_params();
            let imem_ns = firmware.imem_ns_load_params().ok_or(EINVAL)?;
            let dmem = firmware.dmem_load_params();

            // The bootloader does not have a data destination offset field and copies the data at
            // the start of DMEM, so it can only be used if the destination offset of the firmware
            // is 0.
            if dmem.dst_start != 0 {
                return Err(EINVAL);
            }

            BootloaderDmemDescV2 {
                reserved: [0; 4],
                signature: [0; 4],
                ctx_dma: FALCON_DMAIDX_PHYS_SYS_NCOH,
                code_dma_base: firmware_dma.dma_handle(),
                // `dst_start` is also valid as the source offset since the firmware DMA object is
                // a mirror image of the target IMEM layout.
                non_sec_code_off: imem_ns.dst_start,
                non_sec_code_size: imem_ns.len,
                // `dst_start` is also valid as the source offset since the firmware DMA object is
                // a mirror image of the target IMEM layout.
                sec_code_off: imem_sec.dst_start,
                sec_code_size: imem_sec.len,
                code_entry_point: 0,
                // Start of data section is the added padding + the DMEM `src_start` field.
                data_dma_base: firmware_dma
                    .dma_handle()
                    .checked_add(u64::from_safe_cast(align_padding))
                    .and_then(|offset| offset.checked_add(dmem.src_start.into()))
                    .ok_or(EOVERFLOW)?,
                data_size: dmem.len,
                argc: 0,
                argv: 0,
            }
        };

        // The bootloader's code must be loaded in the area right below the first 64K of IMEM.
        const BOOTLOADER_LOAD_CEILING: usize = sizes::SZ_64K;
        let imem_dst_start = BOOTLOADER_LOAD_CEILING
            .checked_sub(ucode.len())
            .ok_or(EOVERFLOW)?;

        Ok(Self {
            _firmware_dma: firmware_dma,
            ucode,
            dmem_desc,
            brom_params: firmware.brom_params(),
            imem_dst_start: u16::try_from(imem_dst_start)?,
            start_tag: u16::try_from(desc.start_tag)?,
        })
    }

    /// Loads the bootloader into `falcon` and execute it.
    ///
    /// The bootloader will load the FWSEC firmware and then execute it. This function returns
    /// after FWSEC has reached completion.
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
            .pio_load(bar, self)
            .inspect_err(|e| dev_err!(dev, "Failed to load FWSEC firmware: {:?}\n", e))?;

        // Configure DMA index for the bootloader to fetch the FWSEC firmware from system memory.
        bar.update(
            regs::NV_PFALCON_FBIF_TRANSCFG::of::<Gsp>()
                .try_at(usize::from_safe_cast(self.dmem_desc.ctx_dma))
                .ok_or(EINVAL)?,
            |v| {
                v.with_target(FalconFbifTarget::CoherentSysmem)
                    .with_mem_type(FalconFbifMemType::Physical)
            },
        );

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

impl FalconFirmware for FwsecFirmwareWithBl {
    type Target = Gsp;

    fn brom_params(&self) -> FalconBromParams {
        self.brom_params.clone()
    }

    fn boot_addr(&self) -> u32 {
        // On V2 platforms, the boot address is extracted from the generic bootloader, because the
        // gbl is what actually copies FWSEC into memory, so that is what needs to be booted.
        u32::from(self.start_tag) << 8
    }
}

impl FalconPioLoadable for FwsecFirmwareWithBl {
    fn imem_sec_load_params(&self) -> Option<FalconPioImemLoadTarget<'_>> {
        None
    }

    fn imem_ns_load_params(&self) -> Option<FalconPioImemLoadTarget<'_>> {
        Some(FalconPioImemLoadTarget {
            data: self.ucode.as_ref(),
            dst_start: self.imem_dst_start,
            secure: false,
            start_tag: self.start_tag,
        })
    }

    fn dmem_load_params(&self) -> FalconPioDmemLoadTarget<'_> {
        FalconPioDmemLoadTarget {
            data: self.dmem_desc.as_bytes(),
            dst_start: 0,
        }
    }
}
