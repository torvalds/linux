#ifndef __src_nvidia_arch_nvalloc_common_inc_gsp_gsp_fw_wpr_meta_h__
#define __src_nvidia_arch_nvalloc_common_inc_gsp_gsp_fw_wpr_meta_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

typedef struct
{
    // Magic
    // BL to use for verification (i.e. Booter locked it in WPR2)
    NvU64 magic; // = 0xdc3aae21371a60b3;

    // Revision number of Booter-BL-Sequencer handoff interface
    // Bumped up when we change this interface so it is not backward compatible.
    // Bumped up when we revoke GSP-RM ucode
    NvU64 revision; // = 1;

    // ---- Members regarding data in SYSMEM ----------------------------
    // Consumed by Booter for DMA

    NvU64 sysmemAddrOfRadix3Elf;
    NvU64 sizeOfRadix3Elf;

    NvU64 sysmemAddrOfBootloader;
    NvU64 sizeOfBootloader;

    // Offsets inside bootloader image needed by Booter
    NvU64 bootloaderCodeOffset;
    NvU64 bootloaderDataOffset;
    NvU64 bootloaderManifestOffset;

    union
    {
        // Used only at initial boot
        struct
        {
            NvU64 sysmemAddrOfSignature;
            NvU64 sizeOfSignature;
        };

        //
        // Used at suspend/resume to read GspFwHeapFreeList
        // Offset relative to GspFwWprMeta FBMEM PA (gspFwWprStart)
        //
        struct
        {
            NvU32 gspFwHeapFreeListWprOffset;
            NvU32 unused0;
            NvU64 unused1;
        };
    };

    // ---- Members describing FB layout --------------------------------
    NvU64 gspFwRsvdStart;

    NvU64 nonWprHeapOffset;
    NvU64 nonWprHeapSize;

    NvU64 gspFwWprStart;

    // GSP-RM to use to setup heap.
    NvU64 gspFwHeapOffset;
    NvU64 gspFwHeapSize;

    // BL to use to find ELF for jump
    NvU64 gspFwOffset;
    // Size is sizeOfRadix3Elf above.

    NvU64 bootBinOffset;
    // Size is sizeOfBootloader above.

    NvU64 frtsOffset;
    NvU64 frtsSize;

    NvU64 gspFwWprEnd;

    // GSP-RM to use for fbRegionInfo?
    NvU64 fbSize;

    // ---- Other members -----------------------------------------------

    // GSP-RM to use for fbRegionInfo?
    NvU64 vgaWorkspaceOffset;
    NvU64 vgaWorkspaceSize;

    // Boot count.  Used to determine whether to load the firmware image.
    NvU64 bootCount;

    // TODO: the partitionRpc* fields below do not really belong in this
    //       structure. The values are patched in by the partition bootstrapper
    //       when GSP-RM is booted in a partition, and this structure was a
    //       convenient place for the bootstrapper to access them. These should
    //       be moved to a different comm. mechanism between the bootstrapper
    //       and the GSP-RM tasks.

    union
    {
	struct
	{
	    // Shared partition RPC memory (physical address)
	    NvU64 partitionRpcAddr;

	    // Offsets relative to partitionRpcAddr
	    NvU16 partitionRpcRequestOffset;
	    NvU16 partitionRpcReplyOffset;

	    // Code section and dataSection offset and size.
	    NvU32 elfCodeOffset;
	    NvU32 elfDataOffset;
	    NvU32 elfCodeSize;
	    NvU32 elfDataSize;

	    // Used during GSP-RM resume to check for revocation
	    NvU32 lsUcodeVersion;
	};

        struct
	{
	    // Pad for the partitionRpc* fields, plus 4 bytes
	    NvU32 partitionRpcPadding[4];

            // CrashCat (contiguous) buffer size/location - occupies same bytes as the
            // elf(Code|Data)(Offset|Size) fields above.
            // TODO: move to GSP_FMC_INIT_PARAMS
            NvU64 sysmemAddrOfCrashReportQueue;
            NvU32 sizeOfCrashReportQueue;

            // Pad for the lsUcodeVersion field
            NvU32 lsUcodeVersionPadding[1];
        };
    };

    // Number of VF partitions allocating sub-heaps from the WPR heap
    // Used during boot to ensure the heap is adequately sized
    NvU8 gspFwHeapVfPartitionCount;

    // Pad structure to exactly 256 bytes.  Can replace padding with additional
    // fields without incrementing revision.  Padding initialized to 0.
    NvU8 padding[7];

    // BL to use for verification (i.e. Booter says OK to boot)
    NvU64 verified;  // 0x0 -> unverified, 0xa0a0a0a0a0a0a0a0 -> verified
} GspFwWprMeta;

#define GSP_FW_WPR_META_REVISION  1
#define GSP_FW_WPR_META_MAGIC     0xdc3aae21371a60b3ULL

#endif
