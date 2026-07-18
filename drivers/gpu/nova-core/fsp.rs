// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! FSP (Foundation Security Processor) interface for Hopper/Blackwell GPUs.
//!
//! Hopper/Blackwell use a simplified firmware boot sequence: FMC, then FSP, then GSP.
//! Unlike Turing/Ampere/Ada, there is no SEC2 (Security Engine 2) usage.
//! FSP handles secure boot directly using FMC firmware and Chain of Trust.

use kernel::{
    device,
    dma::Coherent,
    io::poll::read_poll_timeout,
    prelude::*,
    ptr::{
        Alignable,
        Alignment, //
    },
    sizes::SZ_2M,
    time::Delta,
    transmute::{
        AsBytes,
        FromBytes, //
    },
};

use crate::{
    driver::Bar0,
    falcon::{
        fsp::Fsp as FspEngine,
        Falcon, //
    },
    fb::FbLayout,
    firmware::fsp::{
        FmcSignatures,
        FspFirmware, //
    },
    gpu::Chipset,
    gsp::GspFmcBootParams,
    mctp::{
        MctpHeader,
        NvdmHeader,
        NvdmType, //
    },
    num,
    regs, //
};

mod hal;

/// FSP command response payload (`NVDM_PAYLOAD_COMMAND_RESPONSE`).
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct NvdmPayloadCommandResponse {
    task_id: u32,
    command_nvdm_type: u32,
    error_code: u32,
}

/// Complete FSP response structure with MCTP and NVDM headers.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct FspResponse {
    mctp_header: MctpHeader,
    nvdm_header: NvdmHeader,
    response: NvdmPayloadCommandResponse,
}

// SAFETY: FspResponse is a packed C struct with only integral fields.
unsafe impl FromBytes for FspResponse {}

/// Trait implemented by types representing a message to send to FSP.
///
/// This provides [`Fsp::send_sync_fsp`] with the information it needs to send
/// a given message, following the same pattern as GSP's `CommandToGsp`.
trait MessageToFsp: AsBytes {
    /// NVDM type identifying this message to FSP.
    const NVDM_TYPE: NvdmType;
}

/// NVDM (NVIDIA Data Model) CoT (Chain of Trust) payload, the main
/// message body sent to FSP for Chain of Trust boot.
#[repr(C, packed)]
#[derive(Clone, Copy, Zeroable)]
struct NvdmPayloadCot {
    version: u16,
    size: u16,
    gsp_fmc_sysmem_offset: u64,
    frts_sysmem_offset: u64,
    frts_sysmem_size: u32,
    frts_vidmem_offset: u64,
    frts_vidmem_size: u32,
    sigs: FmcSignatures,
    gsp_boot_args_sysmem_offset: u64,
}

/// Complete FSP message structure with MCTP and NVDM headers.
#[repr(C)]
#[derive(Clone, Copy)]
struct FspMessage {
    mctp_header: MctpHeader,
    nvdm_header: NvdmHeader,
    cot: NvdmPayloadCot,
}

impl FspMessage {
    /// Returns an in-place initializer for [`FspMessage`].
    fn new<'a>(
        fb_layout: &FbLayout,
        fsp_fw: &'a FspFirmware,
        args: &'a FmcBootArgs,
    ) -> Result<impl Init<Self> + 'a> {
        // frts_offset is relative to FB end: FRTS_location = FB_END - frts_offset
        let frts_vidmem_offset = if !args.resume {
            let frts_reserved_size = fb_layout.heap.len() + u64::from(fb_layout.pmu_reserved_size);

            frts_reserved_size
                .align_up(Alignment::new::<SZ_2M>())
                .ok_or(EINVAL)?
        } else {
            0
        };

        let frts_size: u32 = if !args.resume {
            fb_layout.frts.len().try_into()?
        } else {
            0
        };

        let version = hal::fsp_hal(args.chipset).ok_or(ENOTSUPP)?.cot_version();
        let size = num::usize_into_u16::<{ core::mem::size_of::<NvdmPayloadCot>() }>();

        Ok(init!(Self {
            mctp_header: MctpHeader::single_packet(),
            nvdm_header: NvdmHeader::new(NvdmType::Cot),
            // The payload is packed, so we cannot use `init!`. Initialize it member-by-member using
            // `chain`.
            cot <- pin_init::init_zeroed(),
        })
        .chain(move |msg| {
            msg.cot.version = version;
            msg.cot.size = size;
            msg.cot.gsp_fmc_sysmem_offset = fsp_fw.fmc_image.dma_handle();
            msg.cot.frts_vidmem_offset = frts_vidmem_offset;
            msg.cot.frts_vidmem_size = frts_size;
            // frts_sysmem_* intentionally left at zero for now, but will be needed for e.g.
            // systems without VRAM.
            msg.cot.gsp_boot_args_sysmem_offset = args.fmc_boot_params.dma_handle();
            msg.cot.sigs = *fsp_fw.fmc_sigs;

            Ok(())
        }))
    }
}

// SAFETY: `FspMessage` is `#[repr(C)]` with no padding, so all of its
// bytes are initialized.
unsafe impl AsBytes for FspMessage {}

impl MessageToFsp for FspMessage {
    const NVDM_TYPE: NvdmType = NvdmType::Cot;
}

/// Bundled arguments for FMC boot via FSP Chain of Trust.
pub(crate) struct FmcBootArgs {
    chipset: Chipset,
    fmc_boot_params: Coherent<GspFmcBootParams>,
    resume: bool,
}

impl FmcBootArgs {
    /// Builds FMC boot arguments, allocating the DMA-coherent boot parameter
    /// structure that FSP will read.
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        chipset: Chipset,
        wpr_meta_addr: u64,
        libos_addr: u64,
        resume: bool,
    ) -> Result<Self> {
        let init = GspFmcBootParams::new(wpr_meta_addr, libos_addr);

        Ok(Self {
            chipset,
            fmc_boot_params: Coherent::<GspFmcBootParams>::init(dev, GFP_KERNEL, init)?,
            resume,
        })
    }

    /// DMA address of the FMC boot parameters, needed after boot for lockdown
    /// release polling.
    pub(crate) fn boot_params_dma_handle(&self) -> u64 {
        self.fmc_boot_params.dma_handle()
    }
}

/// FSP interface for Hopper/Blackwell GPUs.
///
/// An `Fsp` is produced by [`Fsp::wait_secure_boot`], which only returns once FSP secure boot
/// has completed. It owns the FSP falcon and the FMC firmware, which are used for the subsequent
/// Chain of Trust boot.
pub(crate) struct Fsp {
    falcon: Falcon<FspEngine>,
    fsp_fw: FspFirmware,
}

impl Fsp {
    /// Waits for FSP secure boot completion, then returns the [`Fsp`] interface.
    ///
    /// Polls the thermal scratch register until FSP signals boot completion or the timeout
    /// elapses. Returning an [`Fsp`] only on success guarantees, at the API level, that the
    /// interface is not used before secure boot has completed.
    pub(crate) fn wait_secure_boot(
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        chipset: Chipset,
        fsp_fw: FspFirmware,
    ) -> Result<Fsp> {
        /// FSP secure boot completion timeout in milliseconds.
        const FSP_SECURE_BOOT_TIMEOUT_MS: i64 = 5000;

        let hal = hal::fsp_hal(chipset).ok_or(ENOTSUPP)?;
        let falcon = Falcon::<FspEngine>::new(dev, chipset)?;

        read_poll_timeout(
            || Ok(hal.fsp_boot_status(bar)),
            |&status| status == regs::NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_SUCCESS,
            Delta::from_millis(10),
            Delta::from_millis(FSP_SECURE_BOOT_TIMEOUT_MS),
        )
        .inspect_err(|e| {
            dev_err!(dev, "FSP secure boot completion error: {:?}\n", e);
        })?;

        Ok(Fsp { falcon, fsp_fw })
    }

    /// Sends a message to FSP and waits for the response.
    fn send_sync_fsp<M>(&mut self, dev: &device::Device, bar: Bar0<'_>, msg: &M) -> Result
    where
        M: MessageToFsp,
    {
        self.falcon.send_msg(bar, msg.as_bytes())?;

        let response_buf = self.falcon.recv_msg(bar).inspect_err(|e| {
            dev_err!(dev, "FSP response error: {:?}\n", e);
        })?;

        let (response, _) = FspResponse::from_bytes_prefix(&response_buf[..]).ok_or_else(|| {
            dev_err!(dev, "FSP response too small: {}\n", response_buf.len());
            EIO
        })?;

        let mctp_header = response.mctp_header;
        let nvdm_header = response.nvdm_header;
        let command_nvdm_type = response.response.command_nvdm_type;
        let error_code = response.response.error_code;

        if !mctp_header.is_single_packet() {
            dev_err!(
                dev,
                "Unexpected MCTP header in FSP reply: {:x?}\n",
                mctp_header,
            );
            return Err(EIO);
        }

        if !nvdm_header.validate(NvdmType::FspResponse) {
            dev_err!(
                dev,
                "Unexpected NVDM header in FSP reply: {:x?}\n",
                nvdm_header,
            );
            return Err(EIO);
        }

        if command_nvdm_type != u8::from(M::NVDM_TYPE).into() {
            dev_err!(
                dev,
                "Expected NVDM type {:?} in reply, got {:#x}\n",
                M::NVDM_TYPE,
                command_nvdm_type
            );
            return Err(EIO);
        }

        if error_code != 0 {
            dev_err!(
                dev,
                "NVDM command {:?} failed with error {:#x}\n",
                M::NVDM_TYPE,
                error_code
            );
            return Err(EIO);
        }

        Ok(())
    }

    /// Boots GSP FMC via FSP Chain of Trust.
    ///
    /// Builds the CoT message from the pre-configured [`FmcBootArgs`], sends it
    /// to FSP, and waits for the response.
    pub(crate) fn boot_fmc(
        &mut self,
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        fb_layout: &FbLayout,
        args: &FmcBootArgs,
    ) -> Result {
        dev_dbg!(dev, "Starting FSP boot sequence for {}\n", args.chipset);

        let msg = KBox::init(FspMessage::new(fb_layout, &self.fsp_fw, args)?, GFP_KERNEL)?;

        self.send_sync_fsp(dev, bar, &*msg)?;

        dev_dbg!(dev, "FSP Chain of Trust completed successfully\n");
        Ok(())
    }
}
