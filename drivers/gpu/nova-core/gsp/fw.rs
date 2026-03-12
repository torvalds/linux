// SPDX-License-Identifier: GPL-2.0

pub(crate) mod commands;
mod r570_144;

// Alias to avoid repeating the version number with every use.
use r570_144 as bindings;

use core::ops::Range;

use kernel::{
    dma::CoherentAllocation,
    fmt,
    prelude::*,
    ptr::{
        Alignable,
        Alignment, //
    },
    sizes::{
        SZ_128K,
        SZ_1M, //
    },
    transmute::{
        AsBytes,
        FromBytes, //
    },
};

use crate::{
    fb::FbLayout,
    firmware::gsp::GspFirmware,
    gpu::Chipset,
    gsp::{
        cmdq::Cmdq, //
        GSP_PAGE_SIZE,
    },
    num::{
        self,
        FromSafeCast, //
    },
};

/// Empty type to group methods related to heap parameters for running the GSP firmware.
enum GspFwHeapParams {}

/// Minimum required alignment for the GSP heap.
const GSP_HEAP_ALIGNMENT: Alignment = Alignment::new::<{ 1 << 20 }>();

impl GspFwHeapParams {
    /// Returns the amount of GSP-RM heap memory used during GSP-RM boot and initialization (up to
    /// and including the first client subdevice allocation).
    fn base_rm_size(_chipset: Chipset) -> u64 {
        // TODO: this needs to be updated to return the correct value for Hopper+ once support for
        // them is added:
        // u64::from(bindings::GSP_FW_HEAP_PARAM_BASE_RM_SIZE_GH100)
        u64::from(bindings::GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X)
    }

    /// Returns the amount of heap memory required to support a single channel allocation.
    fn client_alloc_size() -> u64 {
        u64::from(bindings::GSP_FW_HEAP_PARAM_CLIENT_ALLOC_SIZE)
            .align_up(GSP_HEAP_ALIGNMENT)
            .unwrap_or(u64::MAX)
    }

    /// Returns the amount of memory to reserve for management purposes for a framebuffer of size
    /// `fb_size`.
    fn management_overhead(fb_size: u64) -> u64 {
        let fb_size_gb = fb_size.div_ceil(u64::from_safe_cast(kernel::sizes::SZ_1G));

        u64::from(bindings::GSP_FW_HEAP_PARAM_SIZE_PER_GB_FB)
            .saturating_mul(fb_size_gb)
            .align_up(GSP_HEAP_ALIGNMENT)
            .unwrap_or(u64::MAX)
    }
}

/// Heap memory requirements and constraints for a given version of the GSP LIBOS.
pub(crate) struct LibosParams {
    /// The base amount of heap required by the GSP operating system, in bytes.
    carveout_size: u64,
    /// The minimum and maximum sizes allowed for the GSP FW heap, in bytes.
    allowed_heap_size: Range<u64>,
}

impl LibosParams {
    /// Version 2 of the GSP LIBOS (Turing and GA100)
    const LIBOS2: LibosParams = LibosParams {
        carveout_size: num::u32_as_u64(bindings::GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS2),
        allowed_heap_size: num::u32_as_u64(bindings::GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MIN_MB)
            * num::usize_as_u64(SZ_1M)
            ..num::u32_as_u64(bindings::GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MAX_MB)
                * num::usize_as_u64(SZ_1M),
    };

    /// Version 3 of the GSP LIBOS (GA102+)
    const LIBOS3: LibosParams = LibosParams {
        carveout_size: num::u32_as_u64(bindings::GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS3_BAREMETAL),
        allowed_heap_size: num::u32_as_u64(
            bindings::GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MIN_MB,
        ) * num::usize_as_u64(SZ_1M)
            ..num::u32_as_u64(bindings::GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MAX_MB)
                * num::usize_as_u64(SZ_1M),
    };

    /// Returns the libos parameters corresponding to `chipset`.
    pub(crate) fn from_chipset(chipset: Chipset) -> &'static LibosParams {
        if chipset < Chipset::GA102 {
            &Self::LIBOS2
        } else {
            &Self::LIBOS3
        }
    }

    /// Returns the amount of memory (in bytes) to allocate for the WPR heap for a framebuffer size
    /// of `fb_size` (in bytes) for `chipset`.
    pub(crate) fn wpr_heap_size(&self, chipset: Chipset, fb_size: u64) -> u64 {
        // The WPR heap will contain the following:
        // LIBOS carveout,
        self.carveout_size
            // RM boot working memory,
            .saturating_add(GspFwHeapParams::base_rm_size(chipset))
            // One RM client,
            .saturating_add(GspFwHeapParams::client_alloc_size())
            // Overhead for memory management.
            .saturating_add(GspFwHeapParams::management_overhead(fb_size))
            // Clamp to the supported heap sizes.
            .clamp(self.allowed_heap_size.start, self.allowed_heap_size.end - 1)
    }
}

/// Structure passed to the GSP bootloader, containing the framebuffer layout as well as the DMA
/// addresses of the GSP bootloader and firmware.
#[repr(transparent)]
pub(crate) struct GspFwWprMeta(bindings::GspFwWprMeta);

// SAFETY: Padding is explicit and does not contain uninitialized data.
unsafe impl AsBytes for GspFwWprMeta {}

// SAFETY: This struct only contains integer types for which all bit patterns
// are valid.
unsafe impl FromBytes for GspFwWprMeta {}

type GspFwWprMetaBootResumeInfo = bindings::GspFwWprMeta__bindgen_ty_1;
type GspFwWprMetaBootInfo = bindings::GspFwWprMeta__bindgen_ty_1__bindgen_ty_1;

impl GspFwWprMeta {
    /// Fill in and return a `GspFwWprMeta` suitable for booting `gsp_firmware` using the
    /// `fb_layout` layout.
    pub(crate) fn new(gsp_firmware: &GspFirmware, fb_layout: &FbLayout) -> Self {
        Self(bindings::GspFwWprMeta {
            // CAST: we want to store the bits of `GSP_FW_WPR_META_MAGIC` unmodified.
            magic: bindings::GSP_FW_WPR_META_MAGIC as u64,
            revision: u64::from(bindings::GSP_FW_WPR_META_REVISION),
            sysmemAddrOfRadix3Elf: gsp_firmware.radix3_dma_handle(),
            sizeOfRadix3Elf: u64::from_safe_cast(gsp_firmware.size),
            sysmemAddrOfBootloader: gsp_firmware.bootloader.ucode.dma_handle(),
            sizeOfBootloader: u64::from_safe_cast(gsp_firmware.bootloader.ucode.size()),
            bootloaderCodeOffset: u64::from(gsp_firmware.bootloader.code_offset),
            bootloaderDataOffset: u64::from(gsp_firmware.bootloader.data_offset),
            bootloaderManifestOffset: u64::from(gsp_firmware.bootloader.manifest_offset),
            __bindgen_anon_1: GspFwWprMetaBootResumeInfo {
                __bindgen_anon_1: GspFwWprMetaBootInfo {
                    sysmemAddrOfSignature: gsp_firmware.signatures.dma_handle(),
                    sizeOfSignature: u64::from_safe_cast(gsp_firmware.signatures.size()),
                },
            },
            gspFwRsvdStart: fb_layout.heap.start,
            nonWprHeapOffset: fb_layout.heap.start,
            nonWprHeapSize: fb_layout.heap.end - fb_layout.heap.start,
            gspFwWprStart: fb_layout.wpr2.start,
            gspFwHeapOffset: fb_layout.wpr2_heap.start,
            gspFwHeapSize: fb_layout.wpr2_heap.end - fb_layout.wpr2_heap.start,
            gspFwOffset: fb_layout.elf.start,
            bootBinOffset: fb_layout.boot.start,
            frtsOffset: fb_layout.frts.start,
            frtsSize: fb_layout.frts.end - fb_layout.frts.start,
            gspFwWprEnd: fb_layout
                .vga_workspace
                .start
                .align_down(Alignment::new::<SZ_128K>()),
            gspFwHeapVfPartitionCount: fb_layout.vf_partition_count,
            fbSize: fb_layout.fb.end - fb_layout.fb.start,
            vgaWorkspaceOffset: fb_layout.vga_workspace.start,
            vgaWorkspaceSize: fb_layout.vga_workspace.end - fb_layout.vga_workspace.start,
            ..Default::default()
        })
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
#[repr(u32)]
pub(crate) enum MsgFunction {
    // Common function codes
    Nop = bindings::NV_VGPU_MSG_FUNCTION_NOP,
    SetGuestSystemInfo = bindings::NV_VGPU_MSG_FUNCTION_SET_GUEST_SYSTEM_INFO,
    AllocRoot = bindings::NV_VGPU_MSG_FUNCTION_ALLOC_ROOT,
    AllocDevice = bindings::NV_VGPU_MSG_FUNCTION_ALLOC_DEVICE,
    AllocMemory = bindings::NV_VGPU_MSG_FUNCTION_ALLOC_MEMORY,
    AllocCtxDma = bindings::NV_VGPU_MSG_FUNCTION_ALLOC_CTX_DMA,
    AllocChannelDma = bindings::NV_VGPU_MSG_FUNCTION_ALLOC_CHANNEL_DMA,
    MapMemory = bindings::NV_VGPU_MSG_FUNCTION_MAP_MEMORY,
    BindCtxDma = bindings::NV_VGPU_MSG_FUNCTION_BIND_CTX_DMA,
    AllocObject = bindings::NV_VGPU_MSG_FUNCTION_ALLOC_OBJECT,
    Free = bindings::NV_VGPU_MSG_FUNCTION_FREE,
    Log = bindings::NV_VGPU_MSG_FUNCTION_LOG,
    GetGspStaticInfo = bindings::NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO,
    SetRegistry = bindings::NV_VGPU_MSG_FUNCTION_SET_REGISTRY,
    GspSetSystemInfo = bindings::NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO,
    GspInitPostObjGpu = bindings::NV_VGPU_MSG_FUNCTION_GSP_INIT_POST_OBJGPU,
    GspRmControl = bindings::NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL,
    GetStaticInfo = bindings::NV_VGPU_MSG_FUNCTION_GET_STATIC_INFO,

    // Event codes
    GspInitDone = bindings::NV_VGPU_MSG_EVENT_GSP_INIT_DONE,
    GspRunCpuSequencer = bindings::NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER,
    PostEvent = bindings::NV_VGPU_MSG_EVENT_POST_EVENT,
    RcTriggered = bindings::NV_VGPU_MSG_EVENT_RC_TRIGGERED,
    MmuFaultQueued = bindings::NV_VGPU_MSG_EVENT_MMU_FAULT_QUEUED,
    OsErrorLog = bindings::NV_VGPU_MSG_EVENT_OS_ERROR_LOG,
    GspPostNoCat = bindings::NV_VGPU_MSG_EVENT_GSP_POST_NOCAT_RECORD,
    GspLockdownNotice = bindings::NV_VGPU_MSG_EVENT_GSP_LOCKDOWN_NOTICE,
    UcodeLibOsPrint = bindings::NV_VGPU_MSG_EVENT_UCODE_LIBOS_PRINT,
}

impl fmt::Display for MsgFunction {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            // Common function codes
            MsgFunction::Nop => write!(f, "NOP"),
            MsgFunction::SetGuestSystemInfo => write!(f, "SET_GUEST_SYSTEM_INFO"),
            MsgFunction::AllocRoot => write!(f, "ALLOC_ROOT"),
            MsgFunction::AllocDevice => write!(f, "ALLOC_DEVICE"),
            MsgFunction::AllocMemory => write!(f, "ALLOC_MEMORY"),
            MsgFunction::AllocCtxDma => write!(f, "ALLOC_CTX_DMA"),
            MsgFunction::AllocChannelDma => write!(f, "ALLOC_CHANNEL_DMA"),
            MsgFunction::MapMemory => write!(f, "MAP_MEMORY"),
            MsgFunction::BindCtxDma => write!(f, "BIND_CTX_DMA"),
            MsgFunction::AllocObject => write!(f, "ALLOC_OBJECT"),
            MsgFunction::Free => write!(f, "FREE"),
            MsgFunction::Log => write!(f, "LOG"),
            MsgFunction::GetGspStaticInfo => write!(f, "GET_GSP_STATIC_INFO"),
            MsgFunction::SetRegistry => write!(f, "SET_REGISTRY"),
            MsgFunction::GspSetSystemInfo => write!(f, "GSP_SET_SYSTEM_INFO"),
            MsgFunction::GspInitPostObjGpu => write!(f, "GSP_INIT_POST_OBJGPU"),
            MsgFunction::GspRmControl => write!(f, "GSP_RM_CONTROL"),
            MsgFunction::GetStaticInfo => write!(f, "GET_STATIC_INFO"),

            // Event codes
            MsgFunction::GspInitDone => write!(f, "INIT_DONE"),
            MsgFunction::GspRunCpuSequencer => write!(f, "RUN_CPU_SEQUENCER"),
            MsgFunction::PostEvent => write!(f, "POST_EVENT"),
            MsgFunction::RcTriggered => write!(f, "RC_TRIGGERED"),
            MsgFunction::MmuFaultQueued => write!(f, "MMU_FAULT_QUEUED"),
            MsgFunction::OsErrorLog => write!(f, "OS_ERROR_LOG"),
            MsgFunction::GspPostNoCat => write!(f, "NOCAT"),
            MsgFunction::GspLockdownNotice => write!(f, "LOCKDOWN_NOTICE"),
            MsgFunction::UcodeLibOsPrint => write!(f, "LIBOS_PRINT"),
        }
    }
}

impl TryFrom<u32> for MsgFunction {
    type Error = kernel::error::Error;

    fn try_from(value: u32) -> Result<MsgFunction> {
        match value {
            bindings::NV_VGPU_MSG_FUNCTION_NOP => Ok(MsgFunction::Nop),
            bindings::NV_VGPU_MSG_FUNCTION_SET_GUEST_SYSTEM_INFO => {
                Ok(MsgFunction::SetGuestSystemInfo)
            }
            bindings::NV_VGPU_MSG_FUNCTION_ALLOC_ROOT => Ok(MsgFunction::AllocRoot),
            bindings::NV_VGPU_MSG_FUNCTION_ALLOC_DEVICE => Ok(MsgFunction::AllocDevice),
            bindings::NV_VGPU_MSG_FUNCTION_ALLOC_MEMORY => Ok(MsgFunction::AllocMemory),
            bindings::NV_VGPU_MSG_FUNCTION_ALLOC_CTX_DMA => Ok(MsgFunction::AllocCtxDma),
            bindings::NV_VGPU_MSG_FUNCTION_ALLOC_CHANNEL_DMA => Ok(MsgFunction::AllocChannelDma),
            bindings::NV_VGPU_MSG_FUNCTION_MAP_MEMORY => Ok(MsgFunction::MapMemory),
            bindings::NV_VGPU_MSG_FUNCTION_BIND_CTX_DMA => Ok(MsgFunction::BindCtxDma),
            bindings::NV_VGPU_MSG_FUNCTION_ALLOC_OBJECT => Ok(MsgFunction::AllocObject),
            bindings::NV_VGPU_MSG_FUNCTION_FREE => Ok(MsgFunction::Free),
            bindings::NV_VGPU_MSG_FUNCTION_LOG => Ok(MsgFunction::Log),
            bindings::NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO => Ok(MsgFunction::GetGspStaticInfo),
            bindings::NV_VGPU_MSG_FUNCTION_SET_REGISTRY => Ok(MsgFunction::SetRegistry),
            bindings::NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO => Ok(MsgFunction::GspSetSystemInfo),
            bindings::NV_VGPU_MSG_FUNCTION_GSP_INIT_POST_OBJGPU => {
                Ok(MsgFunction::GspInitPostObjGpu)
            }
            bindings::NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL => Ok(MsgFunction::GspRmControl),
            bindings::NV_VGPU_MSG_FUNCTION_GET_STATIC_INFO => Ok(MsgFunction::GetStaticInfo),
            bindings::NV_VGPU_MSG_EVENT_GSP_INIT_DONE => Ok(MsgFunction::GspInitDone),
            bindings::NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER => {
                Ok(MsgFunction::GspRunCpuSequencer)
            }
            bindings::NV_VGPU_MSG_EVENT_POST_EVENT => Ok(MsgFunction::PostEvent),
            bindings::NV_VGPU_MSG_EVENT_RC_TRIGGERED => Ok(MsgFunction::RcTriggered),
            bindings::NV_VGPU_MSG_EVENT_MMU_FAULT_QUEUED => Ok(MsgFunction::MmuFaultQueued),
            bindings::NV_VGPU_MSG_EVENT_OS_ERROR_LOG => Ok(MsgFunction::OsErrorLog),
            bindings::NV_VGPU_MSG_EVENT_GSP_POST_NOCAT_RECORD => Ok(MsgFunction::GspPostNoCat),
            bindings::NV_VGPU_MSG_EVENT_GSP_LOCKDOWN_NOTICE => Ok(MsgFunction::GspLockdownNotice),
            bindings::NV_VGPU_MSG_EVENT_UCODE_LIBOS_PRINT => Ok(MsgFunction::UcodeLibOsPrint),
            _ => Err(EINVAL),
        }
    }
}

impl From<MsgFunction> for u32 {
    fn from(value: MsgFunction) -> Self {
        // CAST: `MsgFunction` is `repr(u32)` and can thus be cast losslessly.
        value as u32
    }
}

/// Sequencer buffer opcode for GSP sequencer commands.
#[derive(Copy, Clone, Debug, PartialEq)]
#[repr(u32)]
pub(crate) enum SeqBufOpcode {
    // Core operation opcodes
    CoreReset = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESET,
    CoreResume = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESUME,
    CoreStart = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_START,
    CoreWaitForHalt = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT,

    // Delay opcode
    DelayUs = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_DELAY_US,

    // Register operation opcodes
    RegModify = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_MODIFY,
    RegPoll = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_POLL,
    RegStore = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_STORE,
    RegWrite = bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_WRITE,
}

impl fmt::Display for SeqBufOpcode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SeqBufOpcode::CoreReset => write!(f, "CORE_RESET"),
            SeqBufOpcode::CoreResume => write!(f, "CORE_RESUME"),
            SeqBufOpcode::CoreStart => write!(f, "CORE_START"),
            SeqBufOpcode::CoreWaitForHalt => write!(f, "CORE_WAIT_FOR_HALT"),
            SeqBufOpcode::DelayUs => write!(f, "DELAY_US"),
            SeqBufOpcode::RegModify => write!(f, "REG_MODIFY"),
            SeqBufOpcode::RegPoll => write!(f, "REG_POLL"),
            SeqBufOpcode::RegStore => write!(f, "REG_STORE"),
            SeqBufOpcode::RegWrite => write!(f, "REG_WRITE"),
        }
    }
}

impl TryFrom<u32> for SeqBufOpcode {
    type Error = kernel::error::Error;

    fn try_from(value: u32) -> Result<SeqBufOpcode> {
        match value {
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESET => {
                Ok(SeqBufOpcode::CoreReset)
            }
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESUME => {
                Ok(SeqBufOpcode::CoreResume)
            }
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_START => {
                Ok(SeqBufOpcode::CoreStart)
            }
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT => {
                Ok(SeqBufOpcode::CoreWaitForHalt)
            }
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_DELAY_US => Ok(SeqBufOpcode::DelayUs),
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_MODIFY => {
                Ok(SeqBufOpcode::RegModify)
            }
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_POLL => Ok(SeqBufOpcode::RegPoll),
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_STORE => Ok(SeqBufOpcode::RegStore),
            bindings::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_WRITE => Ok(SeqBufOpcode::RegWrite),
            _ => Err(EINVAL),
        }
    }
}

impl From<SeqBufOpcode> for u32 {
    fn from(value: SeqBufOpcode) -> Self {
        // CAST: `SeqBufOpcode` is `repr(u32)` and can thus be cast losslessly.
        value as u32
    }
}

/// Wrapper for GSP sequencer register write payload.
#[repr(transparent)]
#[derive(Copy, Clone)]
pub(crate) struct RegWritePayload(bindings::GSP_SEQ_BUF_PAYLOAD_REG_WRITE);

impl RegWritePayload {
    /// Returns the register address.
    pub(crate) fn addr(&self) -> u32 {
        self.0.addr
    }

    /// Returns the value to write.
    pub(crate) fn val(&self) -> u32 {
        self.0.val
    }
}

// SAFETY: This struct only contains integer types for which all bit patterns are valid.
unsafe impl FromBytes for RegWritePayload {}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for RegWritePayload {}

/// Wrapper for GSP sequencer register modify payload.
#[repr(transparent)]
#[derive(Copy, Clone)]
pub(crate) struct RegModifyPayload(bindings::GSP_SEQ_BUF_PAYLOAD_REG_MODIFY);

impl RegModifyPayload {
    /// Returns the register address.
    pub(crate) fn addr(&self) -> u32 {
        self.0.addr
    }

    /// Returns the mask to apply.
    pub(crate) fn mask(&self) -> u32 {
        self.0.mask
    }

    /// Returns the value to write.
    pub(crate) fn val(&self) -> u32 {
        self.0.val
    }
}

// SAFETY: This struct only contains integer types for which all bit patterns are valid.
unsafe impl FromBytes for RegModifyPayload {}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for RegModifyPayload {}

/// Wrapper for GSP sequencer register poll payload.
#[repr(transparent)]
#[derive(Copy, Clone)]
pub(crate) struct RegPollPayload(bindings::GSP_SEQ_BUF_PAYLOAD_REG_POLL);

impl RegPollPayload {
    /// Returns the register address.
    pub(crate) fn addr(&self) -> u32 {
        self.0.addr
    }

    /// Returns the mask to apply.
    pub(crate) fn mask(&self) -> u32 {
        self.0.mask
    }

    /// Returns the expected value.
    pub(crate) fn val(&self) -> u32 {
        self.0.val
    }

    /// Returns the timeout in microseconds.
    pub(crate) fn timeout(&self) -> u32 {
        self.0.timeout
    }
}

// SAFETY: This struct only contains integer types for which all bit patterns are valid.
unsafe impl FromBytes for RegPollPayload {}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for RegPollPayload {}

/// Wrapper for GSP sequencer delay payload.
#[repr(transparent)]
#[derive(Copy, Clone)]
pub(crate) struct DelayUsPayload(bindings::GSP_SEQ_BUF_PAYLOAD_DELAY_US);

impl DelayUsPayload {
    /// Returns the delay value in microseconds.
    pub(crate) fn val(&self) -> u32 {
        self.0.val
    }
}

// SAFETY: This struct only contains integer types for which all bit patterns are valid.
unsafe impl FromBytes for DelayUsPayload {}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for DelayUsPayload {}

/// Wrapper for GSP sequencer register store payload.
#[repr(transparent)]
#[derive(Copy, Clone)]
pub(crate) struct RegStorePayload(bindings::GSP_SEQ_BUF_PAYLOAD_REG_STORE);

impl RegStorePayload {
    /// Returns the register address.
    pub(crate) fn addr(&self) -> u32 {
        self.0.addr
    }

    /// Returns the storage index.
    #[allow(unused)]
    pub(crate) fn index(&self) -> u32 {
        self.0.index
    }
}

// SAFETY: This struct only contains integer types for which all bit patterns are valid.
unsafe impl FromBytes for RegStorePayload {}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for RegStorePayload {}

/// Wrapper for GSP sequencer buffer command.
#[repr(transparent)]
pub(crate) struct SequencerBufferCmd(bindings::GSP_SEQUENCER_BUFFER_CMD);

impl SequencerBufferCmd {
    /// Returns the opcode as a `SeqBufOpcode` enum, or error if invalid.
    pub(crate) fn opcode(&self) -> Result<SeqBufOpcode> {
        self.0.opCode.try_into()
    }

    /// Returns the register write payload by value.
    ///
    /// Returns an error if the opcode is not `SeqBufOpcode::RegWrite`.
    pub(crate) fn reg_write_payload(&self) -> Result<RegWritePayload> {
        if self.opcode()? != SeqBufOpcode::RegWrite {
            return Err(EINVAL);
        }
        // SAFETY: Opcode is verified to be `RegWrite`, so union contains valid `RegWritePayload`.
        let payload_bytes = unsafe {
            core::slice::from_raw_parts(
                core::ptr::addr_of!(self.0.payload.regWrite).cast::<u8>(),
                core::mem::size_of::<RegWritePayload>(),
            )
        };
        Ok(*RegWritePayload::from_bytes(payload_bytes).ok_or(EINVAL)?)
    }

    /// Returns the register modify payload by value.
    ///
    /// Returns an error if the opcode is not `SeqBufOpcode::RegModify`.
    pub(crate) fn reg_modify_payload(&self) -> Result<RegModifyPayload> {
        if self.opcode()? != SeqBufOpcode::RegModify {
            return Err(EINVAL);
        }
        // SAFETY: Opcode is verified to be `RegModify`, so union contains valid `RegModifyPayload`.
        let payload_bytes = unsafe {
            core::slice::from_raw_parts(
                core::ptr::addr_of!(self.0.payload.regModify).cast::<u8>(),
                core::mem::size_of::<RegModifyPayload>(),
            )
        };
        Ok(*RegModifyPayload::from_bytes(payload_bytes).ok_or(EINVAL)?)
    }

    /// Returns the register poll payload by value.
    ///
    /// Returns an error if the opcode is not `SeqBufOpcode::RegPoll`.
    pub(crate) fn reg_poll_payload(&self) -> Result<RegPollPayload> {
        if self.opcode()? != SeqBufOpcode::RegPoll {
            return Err(EINVAL);
        }
        // SAFETY: Opcode is verified to be `RegPoll`, so union contains valid `RegPollPayload`.
        let payload_bytes = unsafe {
            core::slice::from_raw_parts(
                core::ptr::addr_of!(self.0.payload.regPoll).cast::<u8>(),
                core::mem::size_of::<RegPollPayload>(),
            )
        };
        Ok(*RegPollPayload::from_bytes(payload_bytes).ok_or(EINVAL)?)
    }

    /// Returns the delay payload by value.
    ///
    /// Returns an error if the opcode is not `SeqBufOpcode::DelayUs`.
    pub(crate) fn delay_us_payload(&self) -> Result<DelayUsPayload> {
        if self.opcode()? != SeqBufOpcode::DelayUs {
            return Err(EINVAL);
        }
        // SAFETY: Opcode is verified to be `DelayUs`, so union contains valid `DelayUsPayload`.
        let payload_bytes = unsafe {
            core::slice::from_raw_parts(
                core::ptr::addr_of!(self.0.payload.delayUs).cast::<u8>(),
                core::mem::size_of::<DelayUsPayload>(),
            )
        };
        Ok(*DelayUsPayload::from_bytes(payload_bytes).ok_or(EINVAL)?)
    }

    /// Returns the register store payload by value.
    ///
    /// Returns an error if the opcode is not `SeqBufOpcode::RegStore`.
    pub(crate) fn reg_store_payload(&self) -> Result<RegStorePayload> {
        if self.opcode()? != SeqBufOpcode::RegStore {
            return Err(EINVAL);
        }
        // SAFETY: Opcode is verified to be `RegStore`, so union contains valid `RegStorePayload`.
        let payload_bytes = unsafe {
            core::slice::from_raw_parts(
                core::ptr::addr_of!(self.0.payload.regStore).cast::<u8>(),
                core::mem::size_of::<RegStorePayload>(),
            )
        };
        Ok(*RegStorePayload::from_bytes(payload_bytes).ok_or(EINVAL)?)
    }
}

// SAFETY: This struct only contains integer types for which all bit patterns are valid.
unsafe impl FromBytes for SequencerBufferCmd {}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for SequencerBufferCmd {}

/// Wrapper for GSP run CPU sequencer RPC.
#[repr(transparent)]
pub(crate) struct RunCpuSequencer(bindings::rpc_run_cpu_sequencer_v17_00);

impl RunCpuSequencer {
    /// Returns the command index.
    pub(crate) fn cmd_index(&self) -> u32 {
        self.0.cmdIndex
    }
}

// SAFETY: This struct only contains integer types for which all bit patterns are valid.
unsafe impl FromBytes for RunCpuSequencer {}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for RunCpuSequencer {}

/// Struct containing the arguments required to pass a memory buffer to the GSP
/// for use during initialisation.
///
/// The GSP only understands 4K pages (GSP_PAGE_SIZE), so even if the kernel is
/// configured for a larger page size (e.g. 64K pages), we need to give
/// the GSP an array of 4K pages. Since we only create physically contiguous
/// buffers the math to calculate the addresses is simple.
///
/// The buffers must be a multiple of GSP_PAGE_SIZE.  GSP-RM also currently
/// ignores the @kind field for LOGINIT, LOGINTR, and LOGRM, but expects the
/// buffers to be physically contiguous anyway.
///
/// The memory allocated for the arguments must remain until the GSP sends the
/// init_done RPC.
#[repr(transparent)]
pub(crate) struct LibosMemoryRegionInitArgument(bindings::LibosMemoryRegionInitArgument);

// SAFETY: Padding is explicit and does not contain uninitialized data.
unsafe impl AsBytes for LibosMemoryRegionInitArgument {}

// SAFETY: This struct only contains integer types for which all bit patterns
// are valid.
unsafe impl FromBytes for LibosMemoryRegionInitArgument {}

impl LibosMemoryRegionInitArgument {
    pub(crate) fn new<A: AsBytes + FromBytes>(
        name: &'static str,
        obj: &CoherentAllocation<A>,
    ) -> Self {
        /// Generates the `ID8` identifier required for some GSP objects.
        fn id8(name: &str) -> u64 {
            let mut bytes = [0u8; core::mem::size_of::<u64>()];

            for (c, b) in name.bytes().rev().zip(&mut bytes) {
                *b = c;
            }

            u64::from_ne_bytes(bytes)
        }

        Self(bindings::LibosMemoryRegionInitArgument {
            id8: id8(name),
            pa: obj.dma_handle(),
            size: num::usize_as_u64(obj.size()),
            kind: num::u32_into_u8::<
                { bindings::LibosMemoryRegionKind_LIBOS_MEMORY_REGION_CONTIGUOUS },
            >(),
            loc: num::u32_into_u8::<
                { bindings::LibosMemoryRegionLoc_LIBOS_MEMORY_REGION_LOC_SYSMEM },
            >(),
            ..Default::default()
        })
    }
}

/// TX header for setting up a message queue with the GSP.
#[repr(transparent)]
pub(crate) struct MsgqTxHeader(bindings::msgqTxHeader);

impl MsgqTxHeader {
    /// Create a new TX queue header.
    ///
    /// # Arguments
    ///
    /// * `msgq_size` - Total size of the message queue structure, in bytes.
    /// * `rx_hdr_offset` - Offset, in bytes, of the start of the RX header in the message queue
    ///   structure.
    /// * `msg_count` - Number of messages that can be sent, i.e. the number of memory pages
    ///   allocated for the message queue in the message queue structure.
    pub(crate) fn new(msgq_size: u32, rx_hdr_offset: u32, msg_count: u32) -> Self {
        Self(bindings::msgqTxHeader {
            version: 0,
            size: msgq_size,
            msgSize: num::usize_into_u32::<GSP_PAGE_SIZE>(),
            msgCount: msg_count,
            writePtr: 0,
            flags: 1,
            rxHdrOff: rx_hdr_offset,
            entryOff: num::usize_into_u32::<GSP_PAGE_SIZE>(),
        })
    }

    /// Returns the value of the write pointer for this queue.
    pub(crate) fn write_ptr(&self) -> u32 {
        let ptr = core::ptr::from_ref(&self.0.writePtr);

        // SAFETY: `ptr` is a valid pointer to a `u32`.
        unsafe { ptr.read_volatile() }
    }

    /// Sets the value of the write pointer for this queue.
    pub(crate) fn set_write_ptr(&mut self, val: u32) {
        let ptr = core::ptr::from_mut(&mut self.0.writePtr);

        // SAFETY: `ptr` is a valid pointer to a `u32`.
        unsafe { ptr.write_volatile(val) }
    }
}

// SAFETY: Padding is explicit and does not contain uninitialized data.
unsafe impl AsBytes for MsgqTxHeader {}

/// RX header for setting up a message queue with the GSP.
#[repr(transparent)]
pub(crate) struct MsgqRxHeader(bindings::msgqRxHeader);

/// Header for the message RX queue.
impl MsgqRxHeader {
    /// Creates a new RX queue header.
    pub(crate) fn new() -> Self {
        Self(Default::default())
    }

    /// Returns the value of the read pointer for this queue.
    pub(crate) fn read_ptr(&self) -> u32 {
        let ptr = core::ptr::from_ref(&self.0.readPtr);

        // SAFETY: `ptr` is a valid pointer to a `u32`.
        unsafe { ptr.read_volatile() }
    }

    /// Sets the value of the read pointer for this queue.
    pub(crate) fn set_read_ptr(&mut self, val: u32) {
        let ptr = core::ptr::from_mut(&mut self.0.readPtr);

        // SAFETY: `ptr` is a valid pointer to a `u32`.
        unsafe { ptr.write_volatile(val) }
    }
}

// SAFETY: Padding is explicit and does not contain uninitialized data.
unsafe impl AsBytes for MsgqRxHeader {}

bitfield! {
    struct MsgHeaderVersion(u32) {
        31:24 major as u8;
        23:16 minor as u8;
    }
}

impl MsgHeaderVersion {
    const MAJOR_TOT: u8 = 3;
    const MINOR_TOT: u8 = 0;

    fn new() -> Self {
        Self::default()
            .set_major(Self::MAJOR_TOT)
            .set_minor(Self::MINOR_TOT)
    }
}

impl bindings::rpc_message_header_v {
    fn init(cmd_size: usize, function: MsgFunction) -> impl Init<Self, Error> {
        type RpcMessageHeader = bindings::rpc_message_header_v;

        try_init!(RpcMessageHeader {
            header_version: MsgHeaderVersion::new().into(),
            signature: bindings::NV_VGPU_MSG_SIGNATURE_VALID,
            function: function.into(),
            length: size_of::<Self>()
                .checked_add(cmd_size)
                .ok_or(EOVERFLOW)
                .and_then(|v| v.try_into().map_err(|_| EINVAL))?,
            rpc_result: 0xffffffff,
            rpc_result_private: 0xffffffff,
            ..Zeroable::init_zeroed()
        })
    }
}

/// GSP Message Element.
///
/// This is essentially a message header expected to be followed by the message data.
#[repr(transparent)]
pub(crate) struct GspMsgElement {
    inner: bindings::GSP_MSG_QUEUE_ELEMENT,
}

impl GspMsgElement {
    /// Creates a new message element.
    ///
    /// # Arguments
    ///
    /// * `sequence` - Sequence number of the message.
    /// * `cmd_size` - Size of the command (not including the message element), in bytes.
    /// * `function` - Function of the message.
    #[allow(non_snake_case)]
    pub(crate) fn init(
        sequence: u32,
        cmd_size: usize,
        function: MsgFunction,
    ) -> impl Init<Self, Error> {
        type RpcMessageHeader = bindings::rpc_message_header_v;
        type InnerGspMsgElement = bindings::GSP_MSG_QUEUE_ELEMENT;
        let init_inner = try_init!(InnerGspMsgElement {
            seqNum: sequence,
            elemCount: size_of::<Self>()
                .checked_add(cmd_size)
                .ok_or(EOVERFLOW)?
                .div_ceil(GSP_PAGE_SIZE)
                .try_into()
                .map_err(|_| EOVERFLOW)?,
            rpc <- RpcMessageHeader::init(cmd_size, function),
            ..Zeroable::init_zeroed()
        });

        try_init!(GspMsgElement {
            inner <- init_inner,
        })
    }

    /// Sets the checksum of this message.
    ///
    /// Since the header is also part of the checksum, this is usually called after the whole
    /// message has been written to the shared memory area.
    pub(crate) fn set_checksum(&mut self, checksum: u32) {
        self.inner.checkSum = checksum;
    }

    /// Returns the length of the message's payload.
    pub(crate) fn payload_length(&self) -> usize {
        // `rpc.length` includes the length of the RPC message header.
        num::u32_as_usize(self.inner.rpc.length)
            .saturating_sub(size_of::<bindings::rpc_message_header_v>())
    }

    /// Returns the total length of the message, message and RPC headers included.
    pub(crate) fn length(&self) -> usize {
        size_of::<Self>() + self.payload_length()
    }

    // Returns the sequence number of the message.
    pub(crate) fn sequence(&self) -> u32 {
        self.inner.rpc.sequence
    }

    // Returns the function of the message, if it is valid, or the invalid function number as an
    // error.
    pub(crate) fn function(&self) -> Result<MsgFunction, u32> {
        self.inner
            .rpc
            .function
            .try_into()
            .map_err(|_| self.inner.rpc.function)
    }

    // Returns the number of elements (i.e. memory pages) used by this message.
    pub(crate) fn element_count(&self) -> u32 {
        self.inner.elemCount
    }
}

// SAFETY: Padding is explicit and does not contain uninitialized data.
unsafe impl AsBytes for GspMsgElement {}

// SAFETY: This struct only contains integer types for which all bit patterns
// are valid.
unsafe impl FromBytes for GspMsgElement {}

/// Arguments for GSP startup.
#[repr(transparent)]
pub(crate) struct GspArgumentsCached(bindings::GSP_ARGUMENTS_CACHED);

impl GspArgumentsCached {
    /// Creates the arguments for starting the GSP up using `cmdq` as its command queue.
    pub(crate) fn new(cmdq: &Cmdq) -> Self {
        Self(bindings::GSP_ARGUMENTS_CACHED {
            messageQueueInitArguments: MessageQueueInitArguments::new(cmdq).0,
            bDmemStack: 1,
            ..Default::default()
        })
    }
}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for GspArgumentsCached {}

/// On Turing and GA100, the entries in the `LibosMemoryRegionInitArgument`
/// must all be a multiple of GSP_PAGE_SIZE in size, so add padding to force it
/// to that size.
#[repr(C)]
pub(crate) struct GspArgumentsPadded {
    pub(crate) inner: GspArgumentsCached,
    _padding: [u8; GSP_PAGE_SIZE - core::mem::size_of::<bindings::GSP_ARGUMENTS_CACHED>()],
}

// SAFETY: Padding is explicit and will not contain uninitialized data.
unsafe impl AsBytes for GspArgumentsPadded {}

// SAFETY: This struct only contains integer types for which all bit patterns
// are valid.
unsafe impl FromBytes for GspArgumentsPadded {}

/// Init arguments for the message queue.
#[repr(transparent)]
struct MessageQueueInitArguments(bindings::MESSAGE_QUEUE_INIT_ARGUMENTS);

impl MessageQueueInitArguments {
    /// Creates a new init arguments structure for `cmdq`.
    fn new(cmdq: &Cmdq) -> Self {
        Self(bindings::MESSAGE_QUEUE_INIT_ARGUMENTS {
            sharedMemPhysAddr: cmdq.dma_handle(),
            pageTableEntryCount: num::usize_into_u32::<{ Cmdq::NUM_PTES }>(),
            cmdQueueOffset: num::usize_as_u64(Cmdq::CMDQ_OFFSET),
            statQueueOffset: num::usize_as_u64(Cmdq::STATQ_OFFSET),
            ..Default::default()
        })
    }
}
