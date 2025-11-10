// SPDX-License-Identifier: GPL-2.0

mod r570_144;

// Alias to avoid repeating the version number with every use.
use r570_144 as bindings;

use core::ops::Range;

use kernel::{
    dma::CoherentAllocation,
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

type GspFwWprMetaBootResumeInfo = r570_144::GspFwWprMeta__bindgen_ty_1;
type GspFwWprMetaBootInfo = r570_144::GspFwWprMeta__bindgen_ty_1__bindgen_ty_1;

impl GspFwWprMeta {
    /// Fill in and return a `GspFwWprMeta` suitable for booting `gsp_firmware` using the
    /// `fb_layout` layout.
    pub(crate) fn new(gsp_firmware: &GspFirmware, fb_layout: &FbLayout) -> Self {
        Self(bindings::GspFwWprMeta {
            // CAST: we want to store the bits of `GSP_FW_WPR_META_MAGIC` unmodified.
            magic: r570_144::GSP_FW_WPR_META_MAGIC as u64,
            revision: u64::from(r570_144::GSP_FW_WPR_META_REVISION),
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
