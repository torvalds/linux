// SPDX-License-Identifier: GPL-2.0

pub const GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS2: u32 = 0;
pub const GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS3_BAREMETAL: u32 = 23068672;
pub const GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X: u32 = 8388608;
pub const GSP_FW_HEAP_PARAM_SIZE_PER_GB_FB: u32 = 98304;
pub const GSP_FW_HEAP_PARAM_CLIENT_ALLOC_SIZE: u32 = 100663296;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MIN_MB: u32 = 64;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MAX_MB: u32 = 256;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MIN_MB: u32 = 88;
pub const GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MAX_MB: u32 = 280;
pub type __u8 = ffi::c_uchar;
pub type __u16 = ffi::c_ushort;
pub type __u32 = ffi::c_uint;
pub type __u64 = ffi::c_ulonglong;
pub type u8_ = __u8;
pub type u16_ = __u16;
pub type u32_ = __u32;
pub type u64_ = __u64;
#[repr(C)]
#[derive(Copy, Clone)]
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
#[derive(Copy, Clone)]
pub union GspFwWprMeta__bindgen_ty_1 {
    pub __bindgen_anon_1: GspFwWprMeta__bindgen_ty_1__bindgen_ty_1,
    pub __bindgen_anon_2: GspFwWprMeta__bindgen_ty_1__bindgen_ty_2,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone)]
pub struct GspFwWprMeta__bindgen_ty_1__bindgen_ty_1 {
    pub sysmemAddrOfSignature: u64_,
    pub sizeOfSignature: u64_,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone)]
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
#[derive(Copy, Clone)]
pub union GspFwWprMeta__bindgen_ty_2 {
    pub __bindgen_anon_1: GspFwWprMeta__bindgen_ty_2__bindgen_ty_1,
    pub __bindgen_anon_2: GspFwWprMeta__bindgen_ty_2__bindgen_ty_2,
}
#[repr(C)]
#[derive(Debug, Default, Copy, Clone)]
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
#[derive(Debug, Default, Copy, Clone)]
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
#[derive(Debug, Default, Copy, Clone)]
pub struct LibosMemoryRegionInitArgument {
    pub id8: LibosAddress,
    pub pa: LibosAddress,
    pub size: LibosAddress,
    pub kind: u8_,
    pub loc: u8_,
    pub __bindgen_padding_0: [u8; 6usize],
}
