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
    num::TryIntoBounded,
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
    firmware::{
        fsp::{
            FmcSignatures,
            FspFirmware, //
        },
        FIRMWARE_VERSION, //
    },
    gpu::Chipset,
    gsp::{
        GspFmcBootParams,
        GspFwWprMeta,
        LibosMemoryRegionInitArgument, //
    },
    mctp::{
        MctpHeader,
        NvdmHeader,
        NvdmType, //
    },
    num,
    regs, //
};

mod hal;

/// PRC message sub-command.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum PrcMessageSubcmd {
    /// Read a PRC knob value.
    Read = 0x0c,
}

impl From<PrcMessageSubcmd> for u8 {
    fn from(value: PrcMessageSubcmd) -> Self {
        value as u8
    }
}

/// PRC object identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum PrcObjectId {
    /// vGPU mode configuration knob.
    VgpuMode = 0x29,
}

impl From<PrcObjectId> for u8 {
    fn from(value: PrcObjectId) -> Self {
        value as u8
    }
}

kernel::impl_flags!(
    /// PRC request flags.
    #[derive(Clone, Copy, Default, PartialEq, Eq)]
    struct PrcFlags(u8);

    /// Individual PRC request flag.
    #[derive(Clone, Copy, PartialEq, Eq)]
    enum PrcFlag {
        /// Request the active knob value for the current boot.
        Active = 1 << 1,
    }
);

/// vGPU operating mode as reported by FSP via the PRC protocol.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum VgpuMode {
    /// vGPU support is disabled on this GPU.
    Disabled,
    /// vGPU support is enabled on this GPU.
    Enabled,
}

/// FSP command response payload (`NVDM_PAYLOAD_COMMAND_RESPONSE`).
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct NvdmPayloadCommandResponse {
    task_id: u32,
    command_nvdm_type: u32,
    error_code: u32,
}

/// PRC message payload.
///
/// Sent to FSP to query or modify a device configuration knob.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct NvdmPayloadPrc {
    sub_message_id: u8,
    flags: u8,
    object_id: u8,
    reserved: u8,
}

impl NvdmPayloadPrc {
    /// Constructs a PRC payload from typed protocol fields.
    fn new(subcmd: PrcMessageSubcmd, object_id: PrcObjectId, flags: PrcFlags) -> Self {
        Self {
            sub_message_id: subcmd.into(),
            flags: flags.into(),
            object_id: object_id.into(),
            reserved: 0,
        }
    }
}

// SAFETY: NvdmPayloadPrc is a packed C struct with only integral fields.
unsafe impl AsBytes for NvdmPayloadPrc {}

/// PRC response payload containing the knob state value.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct NvdmPayloadPrcResponse {
    value_low: u8,
    value_high: u8,
    reserved1: u8,
    reserved2: u8,
}

impl NvdmPayloadPrcResponse {
    /// Returns the PRC knob value as a little-endian 16-bit integer.
    fn value(self) -> u16 {
        u16::from(self.value_low) | (u16::from(self.value_high) << 8)
    }
}

impl TryFrom<NvdmPayloadPrcResponse> for VgpuMode {
    type Error = kernel::error::Error;

    fn try_from(value: NvdmPayloadPrcResponse) -> Result<Self> {
        match value.value() {
            0 => Ok(VgpuMode::Disabled),
            1 => Ok(VgpuMode::Enabled),
            _ => Err(EINVAL),
        }
    }
}

/// Common MCTP and NVDM headers shared by all FSP messages.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct FspMessageHeader {
    mctp_header: MctpHeader,
    nvdm_header: NvdmHeader,
}

// SAFETY: FspMessageHeader is a packed C struct with only integral fields.
unsafe impl AsBytes for FspMessageHeader {}

// SAFETY: FspMessageHeader is a packed C struct with only integral fields.
unsafe impl FromBytes for FspMessageHeader {}

impl FspMessageHeader {
    /// Construct a standard FSP message header for the given NVDM type.
    fn new(nvdm_type: NvdmType) -> Self {
        Self {
            mctp_header: MctpHeader::single_packet(),
            nvdm_header: NvdmHeader::new(nvdm_type),
        }
    }
}

/// Common FSP response header with MCTP, NVDM and command response payloads.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct FspResponseHeader {
    header: FspMessageHeader,
    response: NvdmPayloadCommandResponse,
}

// SAFETY: FspResponseHeader is a packed C struct with only integral fields.
unsafe impl FromBytes for FspResponseHeader {}

/// Complete FSP PRC response including the knob state payload.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct FspPrcResponse {
    header: FspResponseHeader,
    prc_data: NvdmPayloadPrcResponse,
}

// SAFETY: FspPrcResponse is a packed C struct with only integral fields.
unsafe impl FromBytes for FspPrcResponse {}

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

/// Complete FSP COT (Chain of Trust) message structure.
#[repr(C)]
#[derive(Clone, Copy)]
struct FspCotMessage {
    header: FspMessageHeader,
    cot: NvdmPayloadCot,
}

impl FspCotMessage {
    /// Returns an in-place initializer for [`FspCotMessage`].
    fn new<'a>(
        fb_layout: &FbLayout,
        fsp_fw: &'a FspFirmware,
        args: &'a FmcBootArgs<'_>,
    ) -> Result<impl Init<Self> + 'a> {
        // frts_vidmem_offset is measured from the end of FB, so FRTS sits at
        // (end of FB) - frts_vidmem_offset.
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
            header: FspMessageHeader::new(NvdmType::Cot),
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
            // frts_sysmem_* are left at zero because this path places FRTS in vidmem. The sysmem
            // fields point to an FRTS buffer in sysmem instead, for systems without VRAM.
            msg.cot.gsp_boot_args_sysmem_offset = args.fmc_boot_params.dma_handle();
            msg.cot.sigs = *fsp_fw.fmc_sigs;

            Ok(())
        }))
    }
}

// SAFETY: `FspCotMessage` is `#[repr(C)]` with no padding, so all of its
// bytes are initialized.
unsafe impl AsBytes for FspCotMessage {}

/// Complete FSP PRC message.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct FspPrcMessage {
    header: FspMessageHeader,
    prc: NvdmPayloadPrc,
}

impl FspPrcMessage {
    /// Constructs a PRC message.
    fn new(subcmd: PrcMessageSubcmd, object_id: PrcObjectId, flags: PrcFlags) -> Self {
        Self {
            header: FspMessageHeader::new(NvdmType::Prc),
            prc: NvdmPayloadPrc::new(subcmd, object_id, flags),
        }
    }
}

// SAFETY: FspPrcMessage is a packed C struct with only integral fields.
unsafe impl AsBytes for FspPrcMessage {}

impl MessageToFsp for FspCotMessage {
    const NVDM_TYPE: NvdmType = NvdmType::Cot;
}

impl MessageToFsp for FspPrcMessage {
    const NVDM_TYPE: NvdmType = NvdmType::Prc;
}

/// Bundled arguments for FMC boot via FSP Chain of Trust.
pub(crate) struct FmcBootArgs<'a> {
    chipset: Chipset,
    fmc_boot_params: Coherent<GspFmcBootParams>,
    resume: bool,
    // Additional dependencies required to be kept alive for FMC boot.
    _wpr_meta: &'a Coherent<GspFwWprMeta>,
    _libos: &'a Coherent<[LibosMemoryRegionInitArgument]>,
}

impl<'a> FmcBootArgs<'a> {
    /// Builds FMC boot arguments, allocating the DMA-coherent boot parameter
    /// structure that FSP will read.
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        chipset: Chipset,
        wpr_meta: &'a Coherent<GspFwWprMeta>,
        libos: &'a Coherent<[LibosMemoryRegionInitArgument]>,
        resume: bool,
    ) -> Result<Self> {
        let init = GspFmcBootParams::new(wpr_meta.dma_handle(), libos.dma_handle());

        Ok(Self {
            chipset,
            fmc_boot_params: Coherent::<GspFmcBootParams>::init(dev, GFP_KERNEL, init)?,
            resume,
            _wpr_meta: wpr_meta,
            _libos: libos,
        })
    }

    /// Returns the FMC boot parameters allocation.
    pub(crate) fn boot_params(&self) -> &Coherent<GspFmcBootParams> {
        &self.fmc_boot_params
    }
}

/// FSP interface for Hopper/Blackwell GPUs.
///
/// An `Fsp` is produced by [`Fsp::wait_secure_boot`], which only returns once FSP secure boot
/// has completed. It owns the FSP falcon and the FMC firmware, which are used for the subsequent
/// Chain of Trust boot.
pub(crate) struct Fsp<'a> {
    falcon: Falcon<'a, FspEngine>,
    fsp_fw: FspFirmware,
}

impl<'a> Fsp<'a> {
    /// Attempts to create a `Fsp` instance.
    ///
    /// This can involve waiting for FSP secure boot completion, but should be instantaneous in
    /// practice.
    ///
    /// If `chipset` doesn't support FSP, `Ok(None)` is returned.
    pub(crate) fn try_new(
        dev: &'a device::Device<device::Bound>,
        bar: Bar0<'a>,
        chipset: Chipset,
    ) -> Result<Option<Self>> {
        match hal::fsp_hal(chipset) {
            None => Ok(None),
            Some(hal) => Self::wait_secure_boot(dev, bar, chipset, hal).map(Option::Some),
        }
    }

    /// Waits for FSP secure boot completion, then returns the [`Fsp`] interface.
    ///
    /// Polls the thermal scratch register until FSP signals boot completion or the timeout
    /// elapses. Returning an [`Fsp`] only on success guarantees, at the API level, that the
    /// interface is not used before secure boot has completed.
    fn wait_secure_boot(
        dev: &'a device::Device<device::Bound>,
        bar: Bar0<'a>,
        chipset: Chipset,
        hal: &'static dyn hal::FspHal,
    ) -> Result<Fsp<'a>> {
        /// FSP secure boot completion timeout in milliseconds.
        const FSP_SECURE_BOOT_TIMEOUT_MS: i64 = 5000;

        let falcon = Falcon::<FspEngine>::new(dev, chipset, bar)?;
        let fsp_fw = FspFirmware::new(dev, chipset, FIRMWARE_VERSION)?;

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
    /// Returns the full response buffer on success.
    fn send_sync_fsp<M>(&mut self, dev: &device::Device, msg: &M) -> Result<KVec<u8>>
    where
        M: MessageToFsp,
    {
        self.falcon.send_msg(msg.as_bytes())?;

        let response_buf = self.falcon.recv_msg().inspect_err(|e| {
            dev_err!(dev, "FSP response error: {:?}\n", e);
        })?;

        let (response, _) =
            FspResponseHeader::from_bytes_prefix(&response_buf[..]).ok_or_else(|| {
                dev_err!(dev, "FSP response too small: {}\n", response_buf.len());
                EIO
            })?;

        let mctp_header = response.header.mctp_header;
        let nvdm_header = response.header.nvdm_header;
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

        if command_nvdm_type.try_into_bounded() != Some(M::NVDM_TYPE.into()) {
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

        Ok(response_buf)
    }

    /// Reads the active vGPU mode from FSP using the PRC protocol.
    ///
    /// Queries FSP's Management Partition for the active vGPU mode knob value.
    pub(crate) fn read_vgpu_mode(
        &mut self,
        dev: &device::Device<device::Bound>,
    ) -> Result<VgpuMode> {
        let msg = FspPrcMessage::new(
            PrcMessageSubcmd::Read,
            PrcObjectId::VgpuMode,
            PrcFlags::from(PrcFlag::Active),
        );

        let response_buf = self.send_sync_fsp(dev, &msg)?;
        let (prc_response, _) =
            FspPrcResponse::from_bytes_prefix(&response_buf[..]).ok_or_else(|| {
                dev_err!(dev, "PRC response too small: {}\n", response_buf.len());
                EIO
            })?;

        let prc_data = prc_response.prc_data;

        VgpuMode::try_from(prc_data).inspect_err(|_| {
            dev_err!(dev, "Unexpected vGPU mode value: {:#x}\n", prc_data.value());
        })
    }

    /// Boots GSP FMC via FSP Chain of Trust.
    ///
    /// Builds the CoT message from the pre-configured [`FmcBootArgs`], sends it
    /// to FSP, and waits for the response.
    pub(crate) fn boot_fmc(
        &mut self,
        dev: &device::Device<device::Bound>,
        fb_layout: &FbLayout,
        args: &FmcBootArgs<'_>,
    ) -> Result {
        dev_dbg!(dev, "Starting FSP boot sequence for {}\n", args.chipset);

        let msg = KBox::init(
            FspCotMessage::new(fb_layout, &self.fsp_fw, args)?,
            GFP_KERNEL,
        )?;

        let _response_buf = self.send_sync_fsp(dev, &*msg)?;

        dev_dbg!(dev, "FSP Chain of Trust completed successfully\n");
        Ok(())
    }
}
