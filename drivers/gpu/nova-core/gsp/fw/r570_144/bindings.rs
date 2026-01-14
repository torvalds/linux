// SPDX-License-Identifier: GPL-2.0

#[repr(C)]
#[derive(Default)]
pub struct __IncompleteArrayField<T>(::core::marker::PhantomData<T>, [T; 0]);
impl<T> __IncompleteArrayField<T> {
    #[inline]
    pub const fn new() -> Self {
        __IncompleteArrayField(::core::marker::PhantomData, [])
    }
    #[inline]
    pub fn as_ptr(&self) -> *const T {
        self as *const _ as *const T
    }
    #[inline]
    pub fn as_mut_ptr(&mut self) -> *mut T {
        self as *mut _ as *mut T
    }
    #[inline]
    pub unsafe fn as_slice(&self, len: usize) -> &[T] {
        ::core::slice::from_raw_parts(self.as_ptr(), len)
    }
    #[inline]
    pub unsafe fn as_mut_slice(&mut self, len: usize) -> &mut [T] {
        ::core::slice::from_raw_parts_mut(self.as_mut_ptr(), len)
    }
}
impl<T> ::core::fmt::Debug for __IncompleteArrayField<T> {
    fn fmt(&self, fmt: &mut ::core::fmt::Formatter<'_>) -> ::core::fmt::Result {
        fmt.write_str("__IncompleteArrayField")
    }
}
pub const NV_VGPU_MSG_SIGNATURE_VALID: u32 = 1129337430;
pub const GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS2: u32 = 0;
pub const GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS3_BAREMETAL: u32 = 23068672;
pub const GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X: u32 = 8388608;
pub const GSP_FW_HEAP_PARAM_SIZE_PER_GB_FB: u32 = 98304;
pub const GSP_FW_HEAP_PARAM_CLIENT_ALLOC_SIZE: u32 = 100663296;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MIN_MB: u32 = 64;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MAX_MB: u32 = 256;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MIN_MB: u32 = 88;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MAX_MB: u32 = 280;
pub const GSP_FW_WPR_META_REVISION: u32 = 1;
pub const GSP_FW_WPR_META_MAGIC: i64 = -2577556379034558285;
pub const REGISTRY_TABLE_ENTRY_TYPE_DWORD: u32 = 1;
pub type __u8 = ffi::c_uchar;
pub type __u16 = ffi::c_ushort;
pub type __u32 = ffi::c_uint;
pub type __u64 = ffi::c_ulonglong;
pub type u8_ = __u8;
pub type u16_ = __u16;
pub type u32_ = __u32;
pub type u64_ = __u64;
pub const NV_VGPU_MSG_FUNCTION_NOP: _bindgen_ty_2 = 0;
pub const NV_VGPU_MSG_FUNCTION_SET_GUEST_SYSTEM_INFO: _bindgen_ty_2 = 1;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_ROOT: _bindgen_ty_2 = 2;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_DEVICE: _bindgen_ty_2 = 3;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_MEMORY: _bindgen_ty_2 = 4;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_CTX_DMA: _bindgen_ty_2 = 5;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_CHANNEL_DMA: _bindgen_ty_2 = 6;
pub const NV_VGPU_MSG_FUNCTION_MAP_MEMORY: _bindgen_ty_2 = 7;
pub const NV_VGPU_MSG_FUNCTION_BIND_CTX_DMA: _bindgen_ty_2 = 8;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_OBJECT: _bindgen_ty_2 = 9;
pub const NV_VGPU_MSG_FUNCTION_FREE: _bindgen_ty_2 = 10;
pub const NV_VGPU_MSG_FUNCTION_LOG: _bindgen_ty_2 = 11;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_VIDMEM: _bindgen_ty_2 = 12;
pub const NV_VGPU_MSG_FUNCTION_UNMAP_MEMORY: _bindgen_ty_2 = 13;
pub const NV_VGPU_MSG_FUNCTION_MAP_MEMORY_DMA: _bindgen_ty_2 = 14;
pub const NV_VGPU_MSG_FUNCTION_UNMAP_MEMORY_DMA: _bindgen_ty_2 = 15;
pub const NV_VGPU_MSG_FUNCTION_GET_EDID: _bindgen_ty_2 = 16;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_DISP_CHANNEL: _bindgen_ty_2 = 17;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_DISP_OBJECT: _bindgen_ty_2 = 18;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_SUBDEVICE: _bindgen_ty_2 = 19;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_DYNAMIC_MEMORY: _bindgen_ty_2 = 20;
pub const NV_VGPU_MSG_FUNCTION_DUP_OBJECT: _bindgen_ty_2 = 21;
pub const NV_VGPU_MSG_FUNCTION_IDLE_CHANNELS: _bindgen_ty_2 = 22;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_EVENT: _bindgen_ty_2 = 23;
pub const NV_VGPU_MSG_FUNCTION_SEND_EVENT: _bindgen_ty_2 = 24;
pub const NV_VGPU_MSG_FUNCTION_REMAPPER_CONTROL: _bindgen_ty_2 = 25;
pub const NV_VGPU_MSG_FUNCTION_DMA_CONTROL: _bindgen_ty_2 = 26;
pub const NV_VGPU_MSG_FUNCTION_DMA_FILL_PTE_MEM: _bindgen_ty_2 = 27;
pub const NV_VGPU_MSG_FUNCTION_MANAGE_HW_RESOURCE: _bindgen_ty_2 = 28;
pub const NV_VGPU_MSG_FUNCTION_BIND_ARBITRARY_CTX_DMA: _bindgen_ty_2 = 29;
pub const NV_VGPU_MSG_FUNCTION_CREATE_FB_SEGMENT: _bindgen_ty_2 = 30;
pub const NV_VGPU_MSG_FUNCTION_DESTROY_FB_SEGMENT: _bindgen_ty_2 = 31;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_SHARE_DEVICE: _bindgen_ty_2 = 32;
pub const NV_VGPU_MSG_FUNCTION_DEFERRED_API_CONTROL: _bindgen_ty_2 = 33;
pub const NV_VGPU_MSG_FUNCTION_REMOVE_DEFERRED_API: _bindgen_ty_2 = 34;
pub const NV_VGPU_MSG_FUNCTION_SIM_ESCAPE_READ: _bindgen_ty_2 = 35;
pub const NV_VGPU_MSG_FUNCTION_SIM_ESCAPE_WRITE: _bindgen_ty_2 = 36;
pub const NV_VGPU_MSG_FUNCTION_SIM_MANAGE_DISPLAY_CONTEXT_DMA: _bindgen_ty_2 = 37;
pub const NV_VGPU_MSG_FUNCTION_FREE_VIDMEM_VIRT: _bindgen_ty_2 = 38;
pub const NV_VGPU_MSG_FUNCTION_PERF_GET_PSTATE_INFO: _bindgen_ty_2 = 39;
pub const NV_VGPU_MSG_FUNCTION_PERF_GET_PERFMON_SAMPLE: _bindgen_ty_2 = 40;
pub const NV_VGPU_MSG_FUNCTION_PERF_GET_VIRTUAL_PSTATE_INFO: _bindgen_ty_2 = 41;
pub const NV_VGPU_MSG_FUNCTION_PERF_GET_LEVEL_INFO: _bindgen_ty_2 = 42;
pub const NV_VGPU_MSG_FUNCTION_MAP_SEMA_MEMORY: _bindgen_ty_2 = 43;
pub const NV_VGPU_MSG_FUNCTION_UNMAP_SEMA_MEMORY: _bindgen_ty_2 = 44;
pub const NV_VGPU_MSG_FUNCTION_SET_SURFACE_PROPERTIES: _bindgen_ty_2 = 45;
pub const NV_VGPU_MSG_FUNCTION_CLEANUP_SURFACE: _bindgen_ty_2 = 46;
pub const NV_VGPU_MSG_FUNCTION_UNLOADING_GUEST_DRIVER: _bindgen_ty_2 = 47;
pub const NV_VGPU_MSG_FUNCTION_TDR_SET_TIMEOUT_STATE: _bindgen_ty_2 = 48;
pub const NV_VGPU_MSG_FUNCTION_SWITCH_TO_VGA: _bindgen_ty_2 = 49;
pub const NV_VGPU_MSG_FUNCTION_GPU_EXEC_REG_OPS: _bindgen_ty_2 = 50;
pub const NV_VGPU_MSG_FUNCTION_GET_STATIC_INFO: _bindgen_ty_2 = 51;
pub const NV_VGPU_MSG_FUNCTION_ALLOC_VIRTMEM: _bindgen_ty_2 = 52;
pub const NV_VGPU_MSG_FUNCTION_UPDATE_PDE_2: _bindgen_ty_2 = 53;
pub const NV_VGPU_MSG_FUNCTION_SET_PAGE_DIRECTORY: _bindgen_ty_2 = 54;
pub const NV_VGPU_MSG_FUNCTION_GET_STATIC_PSTATE_INFO: _bindgen_ty_2 = 55;
pub const NV_VGPU_MSG_FUNCTION_TRANSLATE_GUEST_GPU_PTES: _bindgen_ty_2 = 56;
pub const NV_VGPU_MSG_FUNCTION_RESERVED_57: _bindgen_ty_2 = 57;
pub const NV_VGPU_MSG_FUNCTION_RESET_CURRENT_GR_CONTEXT: _bindgen_ty_2 = 58;
pub const NV_VGPU_MSG_FUNCTION_SET_SEMA_MEM_VALIDATION_STATE: _bindgen_ty_2 = 59;
pub const NV_VGPU_MSG_FUNCTION_GET_ENGINE_UTILIZATION: _bindgen_ty_2 = 60;
pub const NV_VGPU_MSG_FUNCTION_UPDATE_GPU_PDES: _bindgen_ty_2 = 61;
pub const NV_VGPU_MSG_FUNCTION_GET_ENCODER_CAPACITY: _bindgen_ty_2 = 62;
pub const NV_VGPU_MSG_FUNCTION_VGPU_PF_REG_READ32: _bindgen_ty_2 = 63;
pub const NV_VGPU_MSG_FUNCTION_SET_GUEST_SYSTEM_INFO_EXT: _bindgen_ty_2 = 64;
pub const NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO: _bindgen_ty_2 = 65;
pub const NV_VGPU_MSG_FUNCTION_RMFS_INIT: _bindgen_ty_2 = 66;
pub const NV_VGPU_MSG_FUNCTION_RMFS_CLOSE_QUEUE: _bindgen_ty_2 = 67;
pub const NV_VGPU_MSG_FUNCTION_RMFS_CLEANUP: _bindgen_ty_2 = 68;
pub const NV_VGPU_MSG_FUNCTION_RMFS_TEST: _bindgen_ty_2 = 69;
pub const NV_VGPU_MSG_FUNCTION_UPDATE_BAR_PDE: _bindgen_ty_2 = 70;
pub const NV_VGPU_MSG_FUNCTION_CONTINUATION_RECORD: _bindgen_ty_2 = 71;
pub const NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO: _bindgen_ty_2 = 72;
pub const NV_VGPU_MSG_FUNCTION_SET_REGISTRY: _bindgen_ty_2 = 73;
pub const NV_VGPU_MSG_FUNCTION_GSP_INIT_POST_OBJGPU: _bindgen_ty_2 = 74;
pub const NV_VGPU_MSG_FUNCTION_SUBDEV_EVENT_SET_NOTIFICATION: _bindgen_ty_2 = 75;
pub const NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL: _bindgen_ty_2 = 76;
pub const NV_VGPU_MSG_FUNCTION_GET_STATIC_INFO2: _bindgen_ty_2 = 77;
pub const NV_VGPU_MSG_FUNCTION_DUMP_PROTOBUF_COMPONENT: _bindgen_ty_2 = 78;
pub const NV_VGPU_MSG_FUNCTION_UNSET_PAGE_DIRECTORY: _bindgen_ty_2 = 79;
pub const NV_VGPU_MSG_FUNCTION_GET_CONSOLIDATED_STATIC_INFO: _bindgen_ty_2 = 80;
pub const NV_VGPU_MSG_FUNCTION_GMMU_REGISTER_FAULT_BUFFER: _bindgen_ty_2 = 81;
pub const NV_VGPU_MSG_FUNCTION_GMMU_UNREGISTER_FAULT_BUFFER: _bindgen_ty_2 = 82;
pub const NV_VGPU_MSG_FUNCTION_GMMU_REGISTER_CLIENT_SHADOW_FAULT_BUFFER: _bindgen_ty_2 = 83;
pub const NV_VGPU_MSG_FUNCTION_GMMU_UNREGISTER_CLIENT_SHADOW_FAULT_BUFFER: _bindgen_ty_2 = 84;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_VGPU_FB_USAGE: _bindgen_ty_2 = 85;
pub const NV_VGPU_MSG_FUNCTION_CTRL_NVFBC_SW_SESSION_UPDATE_INFO: _bindgen_ty_2 = 86;
pub const NV_VGPU_MSG_FUNCTION_CTRL_NVENC_SW_SESSION_UPDATE_INFO: _bindgen_ty_2 = 87;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RESET_CHANNEL: _bindgen_ty_2 = 88;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RESET_ISOLATED_CHANNEL: _bindgen_ty_2 = 89;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPU_HANDLE_VF_PRI_FAULT: _bindgen_ty_2 = 90;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CLK_GET_EXTENDED_INFO: _bindgen_ty_2 = 91;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PERF_BOOST: _bindgen_ty_2 = 92;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PERF_VPSTATES_GET_CONTROL: _bindgen_ty_2 = 93;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_ZBC_CLEAR_TABLE: _bindgen_ty_2 = 94;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_ZBC_COLOR_CLEAR: _bindgen_ty_2 = 95;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_ZBC_DEPTH_CLEAR: _bindgen_ty_2 = 96;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPFIFO_SCHEDULE: _bindgen_ty_2 = 97;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_TIMESLICE: _bindgen_ty_2 = 98;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PREEMPT: _bindgen_ty_2 = 99;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FIFO_DISABLE_CHANNELS: _bindgen_ty_2 = 100;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_TSG_INTERLEAVE_LEVEL: _bindgen_ty_2 = 101;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_CHANNEL_INTERLEAVE_LEVEL: _bindgen_ty_2 = 102;
pub const NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC: _bindgen_ty_2 = 103;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_P2P_CAPS_V2: _bindgen_ty_2 = 104;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CIPHER_AES_ENCRYPT: _bindgen_ty_2 = 105;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CIPHER_SESSION_KEY: _bindgen_ty_2 = 106;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CIPHER_SESSION_KEY_STATUS: _bindgen_ty_2 = 107;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_CLEAR_ALL_SM_ERROR_STATES: _bindgen_ty_2 = 108;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_READ_ALL_SM_ERROR_STATES: _bindgen_ty_2 = 109;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_SET_EXCEPTION_MASK: _bindgen_ty_2 = 110;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPU_PROMOTE_CTX: _bindgen_ty_2 = 111;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GR_CTXSW_PREEMPTION_BIND: _bindgen_ty_2 = 112;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GR_SET_CTXSW_PREEMPTION_MODE: _bindgen_ty_2 = 113;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GR_CTXSW_ZCULL_BIND: _bindgen_ty_2 = 114;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPU_INITIALIZE_CTX: _bindgen_ty_2 = 115;
pub const NV_VGPU_MSG_FUNCTION_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES: _bindgen_ty_2 = 116;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FIFO_CLEAR_FAULTED_BIT: _bindgen_ty_2 = 117;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_LATEST_ECC_ADDRESSES: _bindgen_ty_2 = 118;
pub const NV_VGPU_MSG_FUNCTION_CTRL_MC_SERVICE_INTERRUPTS: _bindgen_ty_2 = 119;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DMA_SET_DEFAULT_VASPACE: _bindgen_ty_2 = 120;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_CE_PCE_MASK: _bindgen_ty_2 = 121;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_ZBC_CLEAR_TABLE_ENTRY: _bindgen_ty_2 = 122;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_NVLINK_PEER_ID_MASK: _bindgen_ty_2 = 123;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_NVLINK_STATUS: _bindgen_ty_2 = 124;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_P2P_CAPS: _bindgen_ty_2 = 125;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_P2P_CAPS_MATRIX: _bindgen_ty_2 = 126;
pub const NV_VGPU_MSG_FUNCTION_RESERVED_0: _bindgen_ty_2 = 127;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RESERVE_PM_AREA_SMPC: _bindgen_ty_2 = 128;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RESERVE_HWPM_LEGACY: _bindgen_ty_2 = 129;
pub const NV_VGPU_MSG_FUNCTION_CTRL_B0CC_EXEC_REG_OPS: _bindgen_ty_2 = 130;
pub const NV_VGPU_MSG_FUNCTION_CTRL_BIND_PM_RESOURCES: _bindgen_ty_2 = 131;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_SUSPEND_CONTEXT: _bindgen_ty_2 = 132;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_RESUME_CONTEXT: _bindgen_ty_2 = 133;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_EXEC_REG_OPS: _bindgen_ty_2 = 134;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_SET_MODE_MMU_DEBUG: _bindgen_ty_2 = 135;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_READ_SINGLE_SM_ERROR_STATE: _bindgen_ty_2 = 136;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_CLEAR_SINGLE_SM_ERROR_STATE: _bindgen_ty_2 = 137;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_SET_MODE_ERRBAR_DEBUG: _bindgen_ty_2 = 138;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_SET_NEXT_STOP_TRIGGER_TYPE: _bindgen_ty_2 = 139;
pub const NV_VGPU_MSG_FUNCTION_CTRL_ALLOC_PMA_STREAM: _bindgen_ty_2 = 140;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PMA_STREAM_UPDATE_GET_PUT: _bindgen_ty_2 = 141;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FB_GET_INFO_V2: _bindgen_ty_2 = 142;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FIFO_SET_CHANNEL_PROPERTIES: _bindgen_ty_2 = 143;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GR_GET_CTX_BUFFER_INFO: _bindgen_ty_2 = 144;
pub const NV_VGPU_MSG_FUNCTION_CTRL_KGR_GET_CTX_BUFFER_PTES: _bindgen_ty_2 = 145;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPU_EVICT_CTX: _bindgen_ty_2 = 146;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FB_GET_FS_INFO: _bindgen_ty_2 = 147;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GRMGR_GET_GR_FS_INFO: _bindgen_ty_2 = 148;
pub const NV_VGPU_MSG_FUNCTION_CTRL_STOP_CHANNEL: _bindgen_ty_2 = 149;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GR_PC_SAMPLING_MODE: _bindgen_ty_2 = 150;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PERF_RATED_TDP_GET_STATUS: _bindgen_ty_2 = 151;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PERF_RATED_TDP_SET_CONTROL: _bindgen_ty_2 = 152;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FREE_PMA_STREAM: _bindgen_ty_2 = 153;
pub const NV_VGPU_MSG_FUNCTION_CTRL_TIMER_SET_GR_TICK_FREQ: _bindgen_ty_2 = 154;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FIFO_SETUP_VF_ZOMBIE_SUBCTX_PDB: _bindgen_ty_2 = 155;
pub const NV_VGPU_MSG_FUNCTION_GET_CONSOLIDATED_GR_STATIC_INFO: _bindgen_ty_2 = 156;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_SET_SINGLE_SM_SINGLE_STEP: _bindgen_ty_2 = 157;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GR_GET_TPC_PARTITION_MODE: _bindgen_ty_2 = 158;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GR_SET_TPC_PARTITION_MODE: _bindgen_ty_2 = 159;
pub const NV_VGPU_MSG_FUNCTION_UVM_PAGING_CHANNEL_ALLOCATE: _bindgen_ty_2 = 160;
pub const NV_VGPU_MSG_FUNCTION_UVM_PAGING_CHANNEL_DESTROY: _bindgen_ty_2 = 161;
pub const NV_VGPU_MSG_FUNCTION_UVM_PAGING_CHANNEL_MAP: _bindgen_ty_2 = 162;
pub const NV_VGPU_MSG_FUNCTION_UVM_PAGING_CHANNEL_UNMAP: _bindgen_ty_2 = 163;
pub const NV_VGPU_MSG_FUNCTION_UVM_PAGING_CHANNEL_PUSH_STREAM: _bindgen_ty_2 = 164;
pub const NV_VGPU_MSG_FUNCTION_UVM_PAGING_CHANNEL_SET_HANDLES: _bindgen_ty_2 = 165;
pub const NV_VGPU_MSG_FUNCTION_UVM_METHOD_STREAM_GUEST_PAGES_OPERATION: _bindgen_ty_2 = 166;
pub const NV_VGPU_MSG_FUNCTION_CTRL_INTERNAL_QUIESCE_PMA_CHANNEL: _bindgen_ty_2 = 167;
pub const NV_VGPU_MSG_FUNCTION_DCE_RM_INIT: _bindgen_ty_2 = 168;
pub const NV_VGPU_MSG_FUNCTION_REGISTER_VIRTUAL_EVENT_BUFFER: _bindgen_ty_2 = 169;
pub const NV_VGPU_MSG_FUNCTION_CTRL_EVENT_BUFFER_UPDATE_GET: _bindgen_ty_2 = 170;
pub const NV_VGPU_MSG_FUNCTION_GET_PLCABLE_ADDRESS_KIND: _bindgen_ty_2 = 171;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PERF_LIMITS_SET_STATUS_V2: _bindgen_ty_2 = 172;
pub const NV_VGPU_MSG_FUNCTION_CTRL_INTERNAL_SRIOV_PROMOTE_PMA_STREAM: _bindgen_ty_2 = 173;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_MMU_DEBUG_MODE: _bindgen_ty_2 = 174;
pub const NV_VGPU_MSG_FUNCTION_CTRL_INTERNAL_PROMOTE_FAULT_METHOD_BUFFERS: _bindgen_ty_2 = 175;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FLCN_GET_CTX_BUFFER_SIZE: _bindgen_ty_2 = 176;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FLCN_GET_CTX_BUFFER_INFO: _bindgen_ty_2 = 177;
pub const NV_VGPU_MSG_FUNCTION_DISABLE_CHANNELS: _bindgen_ty_2 = 178;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FABRIC_MEMORY_DESCRIBE: _bindgen_ty_2 = 179;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FABRIC_MEM_STATS: _bindgen_ty_2 = 180;
pub const NV_VGPU_MSG_FUNCTION_SAVE_HIBERNATION_DATA: _bindgen_ty_2 = 181;
pub const NV_VGPU_MSG_FUNCTION_RESTORE_HIBERNATION_DATA: _bindgen_ty_2 = 182;
pub const NV_VGPU_MSG_FUNCTION_CTRL_INTERNAL_MEMSYS_SET_ZBC_REFERENCED: _bindgen_ty_2 = 183;
pub const NV_VGPU_MSG_FUNCTION_CTRL_EXEC_PARTITIONS_CREATE: _bindgen_ty_2 = 184;
pub const NV_VGPU_MSG_FUNCTION_CTRL_EXEC_PARTITIONS_DELETE: _bindgen_ty_2 = 185;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPFIFO_GET_WORK_SUBMIT_TOKEN: _bindgen_ty_2 = 186;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPFIFO_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX: _bindgen_ty_2 = 187;
pub const NV_VGPU_MSG_FUNCTION_PMA_SCRUBBER_SHARED_BUFFER_GUEST_PAGES_OPERATION: _bindgen_ty_2 =
    188;
pub const NV_VGPU_MSG_FUNCTION_CTRL_MASTER_GET_VIRTUAL_FUNCTION_ERROR_CONT_INTR_MASK:
    _bindgen_ty_2 = 189;
pub const NV_VGPU_MSG_FUNCTION_SET_SYSMEM_DIRTY_PAGE_TRACKING_BUFFER: _bindgen_ty_2 = 190;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SUBDEVICE_GET_P2P_CAPS: _bindgen_ty_2 = 191;
pub const NV_VGPU_MSG_FUNCTION_CTRL_BUS_SET_P2P_MAPPING: _bindgen_ty_2 = 192;
pub const NV_VGPU_MSG_FUNCTION_CTRL_BUS_UNSET_P2P_MAPPING: _bindgen_ty_2 = 193;
pub const NV_VGPU_MSG_FUNCTION_CTRL_FLA_SETUP_INSTANCE_MEM_BLOCK: _bindgen_ty_2 = 194;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPU_MIGRATABLE_OPS: _bindgen_ty_2 = 195;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_TOTAL_HS_CREDITS: _bindgen_ty_2 = 196;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GET_HS_CREDITS: _bindgen_ty_2 = 197;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_HS_CREDITS: _bindgen_ty_2 = 198;
pub const NV_VGPU_MSG_FUNCTION_CTRL_PM_AREA_PC_SAMPLER: _bindgen_ty_2 = 199;
pub const NV_VGPU_MSG_FUNCTION_INVALIDATE_TLB: _bindgen_ty_2 = 200;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPU_QUERY_ECC_STATUS: _bindgen_ty_2 = 201;
pub const NV_VGPU_MSG_FUNCTION_ECC_NOTIFIER_WRITE_ACK: _bindgen_ty_2 = 202;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_GET_MODE_MMU_DEBUG: _bindgen_ty_2 = 203;
pub const NV_VGPU_MSG_FUNCTION_RM_API_CONTROL: _bindgen_ty_2 = 204;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CMD_INTERNAL_GPU_START_FABRIC_PROBE: _bindgen_ty_2 = 205;
pub const NV_VGPU_MSG_FUNCTION_CTRL_NVLINK_GET_INBAND_RECEIVED_DATA: _bindgen_ty_2 = 206;
pub const NV_VGPU_MSG_FUNCTION_GET_STATIC_DATA: _bindgen_ty_2 = 207;
pub const NV_VGPU_MSG_FUNCTION_RESERVED_208: _bindgen_ty_2 = 208;
pub const NV_VGPU_MSG_FUNCTION_CTRL_GPU_GET_INFO_V2: _bindgen_ty_2 = 209;
pub const NV_VGPU_MSG_FUNCTION_GET_BRAND_CAPS: _bindgen_ty_2 = 210;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CMD_NVLINK_INBAND_SEND_DATA: _bindgen_ty_2 = 211;
pub const NV_VGPU_MSG_FUNCTION_UPDATE_GPM_GUEST_BUFFER_INFO: _bindgen_ty_2 = 212;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CMD_INTERNAL_CONTROL_GSP_TRACE: _bindgen_ty_2 = 213;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SET_ZBC_STENCIL_CLEAR: _bindgen_ty_2 = 214;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SUBDEVICE_GET_VGPU_HEAP_STATS: _bindgen_ty_2 = 215;
pub const NV_VGPU_MSG_FUNCTION_CTRL_SUBDEVICE_GET_LIBOS_HEAP_STATS: _bindgen_ty_2 = 216;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_SET_MODE_MMU_GCC_DEBUG: _bindgen_ty_2 = 217;
pub const NV_VGPU_MSG_FUNCTION_CTRL_DBG_GET_MODE_MMU_GCC_DEBUG: _bindgen_ty_2 = 218;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RESERVE_HES: _bindgen_ty_2 = 219;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RELEASE_HES: _bindgen_ty_2 = 220;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RESERVE_CCU_PROF: _bindgen_ty_2 = 221;
pub const NV_VGPU_MSG_FUNCTION_CTRL_RELEASE_CCU_PROF: _bindgen_ty_2 = 222;
pub const NV_VGPU_MSG_FUNCTION_RESERVED: _bindgen_ty_2 = 223;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CMD_GET_CHIPLET_HS_CREDIT_POOL: _bindgen_ty_2 = 224;
pub const NV_VGPU_MSG_FUNCTION_CTRL_CMD_GET_HS_CREDITS_MAPPING: _bindgen_ty_2 = 225;
pub const NV_VGPU_MSG_FUNCTION_CTRL_EXEC_PARTITIONS_EXPORT: _bindgen_ty_2 = 226;
pub const NV_VGPU_MSG_FUNCTION_NUM_FUNCTIONS: _bindgen_ty_2 = 227;
pub type _bindgen_ty_2 = ffi::c_uint;
pub const NV_VGPU_MSG_EVENT_FIRST_EVENT: _bindgen_ty_3 = 4096;
pub const NV_VGPU_MSG_EVENT_GSP_INIT_DONE: _bindgen_ty_3 = 4097;
pub const NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER: _bindgen_ty_3 = 4098;
pub const NV_VGPU_MSG_EVENT_POST_EVENT: _bindgen_ty_3 = 4099;
pub const NV_VGPU_MSG_EVENT_RC_TRIGGERED: _bindgen_ty_3 = 4100;
pub const NV_VGPU_MSG_EVENT_MMU_FAULT_QUEUED: _bindgen_ty_3 = 4101;
pub const NV_VGPU_MSG_EVENT_OS_ERROR_LOG: _bindgen_ty_3 = 4102;
pub const NV_VGPU_MSG_EVENT_RG_LINE_INTR: _bindgen_ty_3 = 4103;
pub const NV_VGPU_MSG_EVENT_GPUACCT_PERFMON_UTIL_SAMPLES: _bindgen_ty_3 = 4104;
pub const NV_VGPU_MSG_EVENT_SIM_READ: _bindgen_ty_3 = 4105;
pub const NV_VGPU_MSG_EVENT_SIM_WRITE: _bindgen_ty_3 = 4106;
pub const NV_VGPU_MSG_EVENT_SEMAPHORE_SCHEDULE_CALLBACK: _bindgen_ty_3 = 4107;
pub const NV_VGPU_MSG_EVENT_UCODE_LIBOS_PRINT: _bindgen_ty_3 = 4108;
pub const NV_VGPU_MSG_EVENT_VGPU_GSP_PLUGIN_TRIGGERED: _bindgen_ty_3 = 4109;
pub const NV_VGPU_MSG_EVENT_PERF_GPU_BOOST_SYNC_LIMITS_CALLBACK: _bindgen_ty_3 = 4110;
pub const NV_VGPU_MSG_EVENT_PERF_BRIDGELESS_INFO_UPDATE: _bindgen_ty_3 = 4111;
pub const NV_VGPU_MSG_EVENT_VGPU_CONFIG: _bindgen_ty_3 = 4112;
pub const NV_VGPU_MSG_EVENT_DISPLAY_MODESET: _bindgen_ty_3 = 4113;
pub const NV_VGPU_MSG_EVENT_EXTDEV_INTR_SERVICE: _bindgen_ty_3 = 4114;
pub const NV_VGPU_MSG_EVENT_NVLINK_INBAND_RECEIVED_DATA_256: _bindgen_ty_3 = 4115;
pub const NV_VGPU_MSG_EVENT_NVLINK_INBAND_RECEIVED_DATA_512: _bindgen_ty_3 = 4116;
pub const NV_VGPU_MSG_EVENT_NVLINK_INBAND_RECEIVED_DATA_1024: _bindgen_ty_3 = 4117;
pub const NV_VGPU_MSG_EVENT_NVLINK_INBAND_RECEIVED_DATA_2048: _bindgen_ty_3 = 4118;
pub const NV_VGPU_MSG_EVENT_NVLINK_INBAND_RECEIVED_DATA_4096: _bindgen_ty_3 = 4119;
pub const NV_VGPU_MSG_EVENT_TIMED_SEMAPHORE_RELEASE: _bindgen_ty_3 = 4120;
pub const NV_VGPU_MSG_EVENT_NVLINK_IS_GPU_DEGRADED: _bindgen_ty_3 = 4121;
pub const NV_VGPU_MSG_EVENT_PFM_REQ_HNDLR_STATE_SYNC_CALLBACK: _bindgen_ty_3 = 4122;
pub const NV_VGPU_MSG_EVENT_NVLINK_FAULT_UP: _bindgen_ty_3 = 4123;
pub const NV_VGPU_MSG_EVENT_GSP_LOCKDOWN_NOTICE: _bindgen_ty_3 = 4124;
pub const NV_VGPU_MSG_EVENT_MIG_CI_CONFIG_UPDATE: _bindgen_ty_3 = 4125;
pub const NV_VGPU_MSG_EVENT_UPDATE_GSP_TRACE: _bindgen_ty_3 = 4126;
pub const NV_VGPU_MSG_EVENT_NVLINK_FATAL_ERROR_RECOVERY: _bindgen_ty_3 = 4127;
pub const NV_VGPU_MSG_EVENT_GSP_POST_NOCAT_RECORD: _bindgen_ty_3 = 4128;
pub const NV_VGPU_MSG_EVENT_FECS_ERROR: _bindgen_ty_3 = 4129;
pub const NV_VGPU_MSG_EVENT_RECOVERY_ACTION: _bindgen_ty_3 = 4130;
pub const NV_VGPU_MSG_EVENT_NUM_EVENTS: _bindgen_ty_3 = 4131;
pub type _bindgen_ty_3 = ffi::c_uint;
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS {
    pub totalVFs: u32_,
    pub firstVfOffset: u32_,
    pub vfFeatureMask: u32_,
    pub __bindgen_padding_0: [u8; 4usize],
    pub FirstVFBar0Address: u64_,
    pub FirstVFBar1Address: u64_,
    pub FirstVFBar2Address: u64_,
    pub bar0Size: u64_,
    pub bar1Size: u64_,
    pub bar2Size: u64_,
    pub b64bitBar0: u8_,
    pub b64bitBar1: u8_,
    pub b64bitBar2: u8_,
    pub bSriovEnabled: u8_,
    pub bSriovHeavyEnabled: u8_,
    pub bEmulateVFBar0TlbInvalidationRegister: u8_,
    pub bClientRmAllocatedCtxBuffer: u8_,
    pub bNonPowerOf2ChannelCountSupported: u8_,
    pub bVfResizableBAR1Supported: u8_,
    pub __bindgen_padding_1: [u8; 7usize],
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS {
    pub BoardID: u32_,
    pub chipSKU: [ffi::c_char; 9usize],
    pub chipSKUMod: [ffi::c_char; 5usize],
    pub __bindgen_padding_0: [u8; 2usize],
    pub skuConfigVersion: u32_,
    pub project: [ffi::c_char; 5usize],
    pub projectSKU: [ffi::c_char; 5usize],
    pub CDP: [ffi::c_char; 6usize],
    pub projectSKUMod: [ffi::c_char; 2usize],
    pub __bindgen_padding_1: [u8; 2usize],
    pub businessCycle: u32_,
}
pub type NV2080_CTRL_CMD_FB_GET_FB_REGION_SURFACE_MEM_TYPE_FLAG = [u8_; 17usize];
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO {
    pub base: u64_,
    pub limit: u64_,
    pub reserved: u64_,
    pub performance: u32_,
    pub supportCompressed: u8_,
    pub supportISO: u8_,
    pub bProtected: u8_,
    pub blackList: NV2080_CTRL_CMD_FB_GET_FB_REGION_SURFACE_MEM_TYPE_FLAG,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS {
    pub numFBRegions: u32_,
    pub __bindgen_padding_0: [u8; 4usize],
    pub fbRegion: [NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO; 16usize],
}
#[repr(C)]
#[derive(Debug, Copy, Clone, MaybeZeroable)]
pub struct NV2080_CTRL_GPU_GET_GID_INFO_PARAMS {
    pub index: u32_,
    pub flags: u32_,
    pub length: u32_,
    pub data: [u8_; 256usize],
}
impl Default for NV2080_CTRL_GPU_GET_GID_INFO_PARAMS {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct DOD_METHOD_DATA {
    pub status: u32_,
    pub acpiIdListLen: u32_,
    pub acpiIdList: [u32_; 16usize],
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct JT_METHOD_DATA {
    pub status: u32_,
    pub jtCaps: u32_,
    pub jtRevId: u16_,
    pub bSBIOSCaps: u8_,
    pub __bindgen_padding_0: u8,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct MUX_METHOD_DATA_ELEMENT {
    pub acpiId: u32_,
    pub mode: u32_,
    pub status: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct MUX_METHOD_DATA {
    pub tableLen: u32_,
    pub acpiIdMuxModeTable: [MUX_METHOD_DATA_ELEMENT; 16usize],
    pub acpiIdMuxPartTable: [MUX_METHOD_DATA_ELEMENT; 16usize],
    pub acpiIdMuxStateTable: [MUX_METHOD_DATA_ELEMENT; 16usize],
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct CAPS_METHOD_DATA {
    pub status: u32_,
    pub optimusCaps: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct ACPI_METHOD_DATA {
    pub bValid: u8_,
    pub __bindgen_padding_0: [u8; 3usize],
    pub dodMethodData: DOD_METHOD_DATA,
    pub jtMethodData: JT_METHOD_DATA,
    pub muxMethodData: MUX_METHOD_DATA,
    pub capsMethodData: CAPS_METHOD_DATA,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS {
    pub headIndex: u32_,
    pub maxHResolution: u32_,
    pub maxVResolution: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS {
    pub numHeads: u32_,
    pub maxNumHeads: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct BUSINFO {
    pub deviceID: u16_,
    pub vendorID: u16_,
    pub subdeviceID: u16_,
    pub subvendorID: u16_,
    pub revisionID: u8_,
    pub __bindgen_padding_0: u8,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_VF_INFO {
    pub totalVFs: u32_,
    pub firstVFOffset: u32_,
    pub FirstVFBar0Address: u64_,
    pub FirstVFBar1Address: u64_,
    pub FirstVFBar2Address: u64_,
    pub b64bitBar0: u8_,
    pub b64bitBar1: u8_,
    pub b64bitBar2: u8_,
    pub __bindgen_padding_0: [u8; 5usize],
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_PCIE_CONFIG_REG {
    pub linkCap: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct EcidManufacturingInfo {
    pub ecidLow: u32_,
    pub ecidHigh: u32_,
    pub ecidExtended: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct FW_WPR_LAYOUT_OFFSET {
    pub nonWprHeapOffset: u64_,
    pub frtsOffset: u64_,
}
#[repr(C)]
#[derive(Debug, Copy, Clone, MaybeZeroable)]
pub struct GspStaticConfigInfo_t {
    pub grCapsBits: [u8_; 23usize],
    pub __bindgen_padding_0: u8,
    pub gidInfo: NV2080_CTRL_GPU_GET_GID_INFO_PARAMS,
    pub SKUInfo: NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS,
    pub __bindgen_padding_1: [u8; 4usize],
    pub fbRegionInfoParams: NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS,
    pub sriovCaps: NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS,
    pub sriovMaxGfid: u32_,
    pub engineCaps: [u32_; 3usize],
    pub poisonFuseEnabled: u8_,
    pub __bindgen_padding_2: [u8; 7usize],
    pub fb_length: u64_,
    pub fbio_mask: u64_,
    pub fb_bus_width: u32_,
    pub fb_ram_type: u32_,
    pub fbp_mask: u64_,
    pub l2_cache_size: u32_,
    pub gpuNameString: [u8_; 64usize],
    pub gpuShortNameString: [u8_; 64usize],
    pub gpuNameString_Unicode: [u16_; 64usize],
    pub bGpuInternalSku: u8_,
    pub bIsQuadroGeneric: u8_,
    pub bIsQuadroAd: u8_,
    pub bIsNvidiaNvs: u8_,
    pub bIsVgx: u8_,
    pub bGeforceSmb: u8_,
    pub bIsTitan: u8_,
    pub bIsTesla: u8_,
    pub bIsMobile: u8_,
    pub bIsGc6Rtd3Allowed: u8_,
    pub bIsGc8Rtd3Allowed: u8_,
    pub bIsGcOffRtd3Allowed: u8_,
    pub bIsGcoffLegacyAllowed: u8_,
    pub bIsMigSupported: u8_,
    pub RTD3GC6TotalBoardPower: u16_,
    pub RTD3GC6PerstDelay: u16_,
    pub __bindgen_padding_3: [u8; 2usize],
    pub bar1PdeBase: u64_,
    pub bar2PdeBase: u64_,
    pub bVbiosValid: u8_,
    pub __bindgen_padding_4: [u8; 3usize],
    pub vbiosSubVendor: u32_,
    pub vbiosSubDevice: u32_,
    pub bPageRetirementSupported: u8_,
    pub bSplitVasBetweenServerClientRm: u8_,
    pub bClRootportNeedsNosnoopWAR: u8_,
    pub __bindgen_padding_5: u8,
    pub displaylessMaxHeads: VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS,
    pub displaylessMaxResolution: VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS,
    pub __bindgen_padding_6: [u8; 4usize],
    pub displaylessMaxPixels: u64_,
    pub hInternalClient: u32_,
    pub hInternalDevice: u32_,
    pub hInternalSubdevice: u32_,
    pub bSelfHostedMode: u8_,
    pub bAtsSupported: u8_,
    pub bIsGpuUefi: u8_,
    pub bIsEfiInit: u8_,
    pub ecidInfo: [EcidManufacturingInfo; 2usize],
    pub fwWprLayoutOffset: FW_WPR_LAYOUT_OFFSET,
}
impl Default for GspStaticConfigInfo_t {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GspSystemInfo {
    pub gpuPhysAddr: u64_,
    pub gpuPhysFbAddr: u64_,
    pub gpuPhysInstAddr: u64_,
    pub gpuPhysIoAddr: u64_,
    pub nvDomainBusDeviceFunc: u64_,
    pub simAccessBufPhysAddr: u64_,
    pub notifyOpSharedSurfacePhysAddr: u64_,
    pub pcieAtomicsOpMask: u64_,
    pub consoleMemSize: u64_,
    pub maxUserVa: u64_,
    pub pciConfigMirrorBase: u32_,
    pub pciConfigMirrorSize: u32_,
    pub PCIDeviceID: u32_,
    pub PCISubDeviceID: u32_,
    pub PCIRevisionID: u32_,
    pub pcieAtomicsCplDeviceCapMask: u32_,
    pub oorArch: u8_,
    pub __bindgen_padding_0: [u8; 7usize],
    pub clPdbProperties: u64_,
    pub Chipset: u32_,
    pub bGpuBehindBridge: u8_,
    pub bFlrSupported: u8_,
    pub b64bBar0Supported: u8_,
    pub bMnocAvailable: u8_,
    pub chipsetL1ssEnable: u32_,
    pub bUpstreamL0sUnsupported: u8_,
    pub bUpstreamL1Unsupported: u8_,
    pub bUpstreamL1PorSupported: u8_,
    pub bUpstreamL1PorMobileOnly: u8_,
    pub bSystemHasMux: u8_,
    pub upstreamAddressValid: u8_,
    pub FHBBusInfo: BUSINFO,
    pub chipsetIDInfo: BUSINFO,
    pub __bindgen_padding_1: [u8; 2usize],
    pub acpiMethodData: ACPI_METHOD_DATA,
    pub hypervisorType: u32_,
    pub bIsPassthru: u8_,
    pub __bindgen_padding_2: [u8; 7usize],
    pub sysTimerOffsetNs: u64_,
    pub gspVFInfo: GSP_VF_INFO,
    pub bIsPrimary: u8_,
    pub isGridBuild: u8_,
    pub __bindgen_padding_3: [u8; 2usize],
    pub pcieConfigReg: GSP_PCIE_CONFIG_REG,
    pub gridBuildCsp: u32_,
    pub bPreserveVideoMemoryAllocations: u8_,
    pub bTdrEventSupported: u8_,
    pub bFeatureStretchVblankCapable: u8_,
    pub bEnableDynamicGranularityPageArrays: u8_,
    pub bClockBoostSupported: u8_,
    pub bRouteDispIntrsToCPU: u8_,
    pub __bindgen_padding_4: [u8; 6usize],
    pub hostPageSize: u64_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct MESSAGE_QUEUE_INIT_ARGUMENTS {
    pub sharedMemPhysAddr: u64_,
    pub pageTableEntryCount: u32_,
    pub __bindgen_padding_0: [u8; 4usize],
    pub cmdQueueOffset: u64_,
    pub statQueueOffset: u64_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_SR_INIT_ARGUMENTS {
    pub oldLevel: u32_,
    pub flags: u32_,
    pub bInPMTransition: u8_,
    pub __bindgen_padding_0: [u8; 3usize],
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_ARGUMENTS_CACHED {
    pub messageQueueInitArguments: MESSAGE_QUEUE_INIT_ARGUMENTS,
    pub srInitArguments: GSP_SR_INIT_ARGUMENTS,
    pub gpuInstance: u32_,
    pub bDmemStack: u8_,
    pub __bindgen_padding_0: [u8; 7usize],
    pub profilerArgs: GSP_ARGUMENTS_CACHED__bindgen_ty_1,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_ARGUMENTS_CACHED__bindgen_ty_1 {
    pub pa: u64_,
    pub size: u64_,
}
#[repr(C)]
#[derive(Copy, Clone, MaybeZeroable)]
pub union rpc_message_rpc_union_field_v03_00 {
    pub spare: u32_,
    pub cpuRmGfid: u32_,
}
impl Default for rpc_message_rpc_union_field_v03_00 {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
pub type rpc_message_rpc_union_field_v = rpc_message_rpc_union_field_v03_00;
#[repr(C)]
#[derive(MaybeZeroable)]
pub struct rpc_message_header_v03_00 {
    pub header_version: u32_,
    pub signature: u32_,
    pub length: u32_,
    pub function: u32_,
    pub rpc_result: u32_,
    pub rpc_result_private: u32_,
    pub sequence: u32_,
    pub u: rpc_message_rpc_union_field_v,
    pub rpc_message_data: __IncompleteArrayField<u8_>,
}
impl Default for rpc_message_header_v03_00 {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
pub type rpc_message_header_v = rpc_message_header_v03_00;
#[repr(C)]
#[derive(Copy, Clone, MaybeZeroable)]
pub struct GspFwWprMeta {
    pub magic: u64_,
    pub revision: u64_,
    pub sysmemAddrOfRadix3Elf: u64_,
    pub sizeOfRadix3Elf: u64_,
    pub sysmemAddrOfBootloader: u64_,
    pub sizeOfBootloader: u64_,
    pub bootloaderCodeOffset: u64_,
    pub bootloaderDataOffset: u64_,
    pub bootloaderManifestOffset: u64_,
    pub __bindgen_anon_1: GspFwWprMeta__bindgen_ty_1,
    pub gspFwRsvdStart: u64_,
    pub nonWprHeapOffset: u64_,
    pub nonWprHeapSize: u64_,
    pub gspFwWprStart: u64_,
    pub gspFwHeapOffset: u64_,
    pub gspFwHeapSize: u64_,
    pub gspFwOffset: u64_,
    pub bootBinOffset: u64_,
    pub frtsOffset: u64_,
    pub frtsSize: u64_,
    pub gspFwWprEnd: u64_,
    pub fbSize: u64_,
    pub vgaWorkspaceOffset: u64_,
    pub vgaWorkspaceSize: u64_,
    pub bootCount: u64_,
    pub __bindgen_anon_2: GspFwWprMeta__bindgen_ty_2,
    pub gspFwHeapVfPartitionCount: u8_,
    pub flags: u8_,
    pub padding: [u8_; 2usize],
    pub pmuReservedSize: u32_,
    pub verified: u64_,
}
#[repr(C)]
#[derive(Copy, Clone, MaybeZeroable)]
pub union GspFwWprMeta__bindgen_ty_1 {
    pub __bindgen_anon_1: GspFwWprMeta__bindgen_ty_1__bindgen_ty_1,
    pub __bindgen_anon_2: GspFwWprMeta__bindgen_ty_1__bindgen_ty_2,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GspFwWprMeta__bindgen_ty_1__bindgen_ty_1 {
    pub sysmemAddrOfSignature: u64_,
    pub sizeOfSignature: u64_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GspFwWprMeta__bindgen_ty_1__bindgen_ty_2 {
    pub gspFwHeapFreeListWprOffset: u32_,
    pub unused0: u32_,
    pub unused1: u64_,
}
impl Default for GspFwWprMeta__bindgen_ty_1 {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
#[repr(C)]
#[derive(Copy, Clone, MaybeZeroable)]
pub union GspFwWprMeta__bindgen_ty_2 {
    pub __bindgen_anon_1: GspFwWprMeta__bindgen_ty_2__bindgen_ty_1,
    pub __bindgen_anon_2: GspFwWprMeta__bindgen_ty_2__bindgen_ty_2,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GspFwWprMeta__bindgen_ty_2__bindgen_ty_1 {
    pub partitionRpcAddr: u64_,
    pub partitionRpcRequestOffset: u16_,
    pub partitionRpcReplyOffset: u16_,
    pub elfCodeOffset: u32_,
    pub elfDataOffset: u32_,
    pub elfCodeSize: u32_,
    pub elfDataSize: u32_,
    pub lsUcodeVersion: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GspFwWprMeta__bindgen_ty_2__bindgen_ty_2 {
    pub partitionRpcPadding: [u32_; 4usize],
    pub sysmemAddrOfCrashReportQueue: u64_,
    pub sizeOfCrashReportQueue: u32_,
    pub lsUcodeVersionPadding: [u32_; 1usize],
}
impl Default for GspFwWprMeta__bindgen_ty_2 {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
impl Default for GspFwWprMeta {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
pub type LibosAddress = u64_;
pub const LibosMemoryRegionKind_LIBOS_MEMORY_REGION_NONE: LibosMemoryRegionKind = 0;
pub const LibosMemoryRegionKind_LIBOS_MEMORY_REGION_CONTIGUOUS: LibosMemoryRegionKind = 1;
pub const LibosMemoryRegionKind_LIBOS_MEMORY_REGION_RADIX3: LibosMemoryRegionKind = 2;
pub type LibosMemoryRegionKind = ffi::c_uint;
pub const LibosMemoryRegionLoc_LIBOS_MEMORY_REGION_LOC_NONE: LibosMemoryRegionLoc = 0;
pub const LibosMemoryRegionLoc_LIBOS_MEMORY_REGION_LOC_SYSMEM: LibosMemoryRegionLoc = 1;
pub const LibosMemoryRegionLoc_LIBOS_MEMORY_REGION_LOC_FB: LibosMemoryRegionLoc = 2;
pub type LibosMemoryRegionLoc = ffi::c_uint;
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct LibosMemoryRegionInitArgument {
    pub id8: LibosAddress,
    pub pa: LibosAddress,
    pub size: LibosAddress,
    pub kind: u8_,
    pub loc: u8_,
    pub __bindgen_padding_0: [u8; 6usize],
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct PACKED_REGISTRY_ENTRY {
    pub nameOffset: u32_,
    pub type_: u8_,
    pub __bindgen_padding_0: [u8; 3usize],
    pub data: u32_,
    pub length: u32_,
}
#[repr(C)]
#[derive(Debug, Default, MaybeZeroable)]
pub struct PACKED_REGISTRY_TABLE {
    pub size: u32_,
    pub numEntries: u32_,
    pub entries: __IncompleteArrayField<PACKED_REGISTRY_ENTRY>,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct msgqTxHeader {
    pub version: u32_,
    pub size: u32_,
    pub msgSize: u32_,
    pub msgCount: u32_,
    pub writePtr: u32_,
    pub flags: u32_,
    pub rxHdrOff: u32_,
    pub entryOff: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct msgqRxHeader {
    pub readPtr: u32_,
}
#[repr(C)]
#[repr(align(8))]
#[derive(MaybeZeroable)]
pub struct GSP_MSG_QUEUE_ELEMENT {
    pub authTagBuffer: [u8_; 16usize],
    pub aadBuffer: [u8_; 16usize],
    pub checkSum: u32_,
    pub seqNum: u32_,
    pub elemCount: u32_,
    pub __bindgen_padding_0: [u8; 4usize],
    pub rpc: rpc_message_header_v,
}
impl Default for GSP_MSG_QUEUE_ELEMENT {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
#[repr(C)]
#[derive(Debug, Default, MaybeZeroable)]
pub struct rpc_run_cpu_sequencer_v17_00 {
    pub bufferSizeDWord: u32_,
    pub cmdIndex: u32_,
    pub regSaveArea: [u32_; 8usize],
    pub commandBuffer: __IncompleteArrayField<u32_>,
}
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_WRITE: GSP_SEQ_BUF_OPCODE = 0;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_MODIFY: GSP_SEQ_BUF_OPCODE = 1;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_POLL: GSP_SEQ_BUF_OPCODE = 2;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_DELAY_US: GSP_SEQ_BUF_OPCODE = 3;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_STORE: GSP_SEQ_BUF_OPCODE = 4;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESET: GSP_SEQ_BUF_OPCODE = 5;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_START: GSP_SEQ_BUF_OPCODE = 6;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT: GSP_SEQ_BUF_OPCODE = 7;
pub const GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESUME: GSP_SEQ_BUF_OPCODE = 8;
pub type GSP_SEQ_BUF_OPCODE = ffi::c_uint;
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_SEQ_BUF_PAYLOAD_REG_WRITE {
    pub addr: u32_,
    pub val: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_SEQ_BUF_PAYLOAD_REG_MODIFY {
    pub addr: u32_,
    pub mask: u32_,
    pub val: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_SEQ_BUF_PAYLOAD_REG_POLL {
    pub addr: u32_,
    pub mask: u32_,
    pub val: u32_,
    pub timeout: u32_,
    pub error: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_SEQ_BUF_PAYLOAD_DELAY_US {
    pub val: u32_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, MaybeZeroable)]
pub struct GSP_SEQ_BUF_PAYLOAD_REG_STORE {
    pub addr: u32_,
    pub index: u32_,
}
#[repr(C)]
#[derive(Copy, Clone, MaybeZeroable)]
pub struct GSP_SEQUENCER_BUFFER_CMD {
    pub opCode: GSP_SEQ_BUF_OPCODE,
    pub payload: GSP_SEQUENCER_BUFFER_CMD__bindgen_ty_1,
}
#[repr(C)]
#[derive(Copy, Clone, MaybeZeroable)]
pub union GSP_SEQUENCER_BUFFER_CMD__bindgen_ty_1 {
    pub regWrite: GSP_SEQ_BUF_PAYLOAD_REG_WRITE,
    pub regModify: GSP_SEQ_BUF_PAYLOAD_REG_MODIFY,
    pub regPoll: GSP_SEQ_BUF_PAYLOAD_REG_POLL,
    pub delayUs: GSP_SEQ_BUF_PAYLOAD_DELAY_US,
    pub regStore: GSP_SEQ_BUF_PAYLOAD_REG_STORE,
}
impl Default for GSP_SEQUENCER_BUFFER_CMD__bindgen_ty_1 {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
impl Default for GSP_SEQUENCER_BUFFER_CMD {
    fn default() -> Self {
        let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}
