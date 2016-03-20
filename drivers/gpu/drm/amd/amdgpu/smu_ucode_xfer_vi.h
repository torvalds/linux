/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef SMU_UCODE_XFER_VI_H
#define SMU_UCODE_XFER_VI_H

#define SMU_DRAMData_TOC_VERSION  1
#define MAX_IH_REGISTER_COUNT     65535
#define SMU_DIGEST_SIZE_BYTES     20
#define SMU_FB_SIZE_BYTES         1048576
#define SMU_MAX_ENTRIES           12

#define UCODE_ID_SMU              0
#define UCODE_ID_SDMA0            1
#define UCODE_ID_SDMA1            2
#define UCODE_ID_CP_CE            3
#define UCODE_ID_CP_PFP           4
#define UCODE_ID_CP_ME            5
#define UCODE_ID_CP_MEC           6
#define UCODE_ID_CP_MEC_JT1       7
#define UCODE_ID_CP_MEC_JT2       8
#define UCODE_ID_GMCON_RENG       9
#define UCODE_ID_RLC_G            10
#define UCODE_ID_IH_REG_RESTORE   11
#define UCODE_ID_VBIOS            12
#define UCODE_ID_MISC_METADATA    13
#define UCODE_ID_RLC_SCRATCH      32
#define UCODE_ID_RLC_SRM_ARAM     33
#define UCODE_ID_RLC_SRM_DRAM     34
#define UCODE_ID_MEC_STORAGE      35
#define UCODE_ID_VBIOS_PARAMETERS 36
#define UCODE_META_DATA           0xFF

#define UCODE_ID_SMU_MASK             0x00000001
#define UCODE_ID_SDMA0_MASK           0x00000002
#define UCODE_ID_SDMA1_MASK           0x00000004
#define UCODE_ID_CP_CE_MASK           0x00000008
#define UCODE_ID_CP_PFP_MASK          0x00000010
#define UCODE_ID_CP_ME_MASK           0x00000020
#define UCODE_ID_CP_MEC_MASK          0x00000040
#define UCODE_ID_CP_MEC_JT1_MASK      0x00000080
#define UCODE_ID_CP_MEC_JT2_MASK      0x00000100
#define UCODE_ID_GMCON_RENG_MASK      0x00000200
#define UCODE_ID_RLC_G_MASK           0x00000400
#define UCODE_ID_IH_REG_RESTORE_MASK  0x00000800
#define UCODE_ID_VBIOS_MASK           0x00001000

#define UCODE_FLAG_UNHALT_MASK   0x1

struct SMU_Entry {
#ifndef __BIG_ENDIAN
	uint16_t id;
	uint16_t version;
	uint32_t image_addr_high;
	uint32_t image_addr_low;
	uint32_t meta_data_addr_high;
	uint32_t meta_data_addr_low;
	uint32_t data_size_byte;
	uint16_t flags;
	uint16_t num_register_entries;
#else
	uint16_t version;
	uint16_t id;
	uint32_t image_addr_high;
	uint32_t image_addr_low;
	uint32_t meta_data_addr_high;
	uint32_t meta_data_addr_low;
	uint32_t data_size_byte;
	uint16_t num_register_entries;
	uint16_t flags;
#endif
};

struct SMU_DRAMData_TOC {
	uint32_t structure_version;
	uint32_t num_entries;
	struct SMU_Entry entry[SMU_MAX_ENTRIES];
};

#endif
