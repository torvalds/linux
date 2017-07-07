/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#ifndef _CZ_SMUMGR_H_
#define _CZ_SMUMGR_H_


#define MAX_NUM_FIRMWARE                        8
#define MAX_NUM_SCRATCH                         11
#define CZ_SCRATCH_SIZE_NONGFX_CLOCKGATING      1024
#define CZ_SCRATCH_SIZE_NONGFX_GOLDENSETTING    2048
#define CZ_SCRATCH_SIZE_SDMA_METADATA           1024
#define CZ_SCRATCH_SIZE_IH                      ((2*256+1)*4)

enum cz_scratch_entry {
	CZ_SCRATCH_ENTRY_UCODE_ID_SDMA0 = 0,
	CZ_SCRATCH_ENTRY_UCODE_ID_SDMA1,
	CZ_SCRATCH_ENTRY_UCODE_ID_CP_CE,
	CZ_SCRATCH_ENTRY_UCODE_ID_CP_PFP,
	CZ_SCRATCH_ENTRY_UCODE_ID_CP_ME,
	CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1,
	CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2,
	CZ_SCRATCH_ENTRY_UCODE_ID_GMCON_RENG,
	CZ_SCRATCH_ENTRY_UCODE_ID_RLC_G,
	CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH,
	CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM,
	CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM,
	CZ_SCRATCH_ENTRY_UCODE_ID_DMCU_ERAM,
	CZ_SCRATCH_ENTRY_UCODE_ID_DMCU_IRAM,
	CZ_SCRATCH_ENTRY_UCODE_ID_POWER_PROFILING,
	CZ_SCRATCH_ENTRY_DATA_ID_SDMA_HALT,
	CZ_SCRATCH_ENTRY_DATA_ID_SYS_CLOCKGATING,
	CZ_SCRATCH_ENTRY_DATA_ID_SDMA_RING_REGS,
	CZ_SCRATCH_ENTRY_DATA_ID_NONGFX_REINIT,
	CZ_SCRATCH_ENTRY_DATA_ID_SDMA_START,
	CZ_SCRATCH_ENTRY_DATA_ID_IH_REGISTERS,
	CZ_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE
};

struct cz_buffer_entry {
	uint32_t data_size;
	uint32_t mc_addr_low;
	uint32_t mc_addr_high;
	void *kaddr;
	enum cz_scratch_entry firmware_ID;
	unsigned long handle; /* as bo handle used when release bo */
};

struct cz_register_index_data_pair {
	uint32_t offset;
	uint32_t value;
};

struct cz_ih_meta_data {
	uint32_t command;
	struct cz_register_index_data_pair register_index_value_pair[1];
};

struct cz_smumgr {
	uint8_t driver_buffer_length;
	uint8_t scratch_buffer_length;
	uint16_t toc_entry_used_count;
	uint16_t toc_entry_initialize_index;
	uint16_t toc_entry_power_profiling_index;
	uint16_t toc_entry_aram;
	uint16_t toc_entry_ih_register_restore_task_index;
	uint16_t toc_entry_clock_table;
	uint16_t ih_register_restore_task_size;
	uint16_t smu_buffer_used_bytes;

	struct cz_buffer_entry toc_buffer;
	struct cz_buffer_entry smu_buffer;
	struct cz_buffer_entry firmware_buffer;
	struct cz_buffer_entry driver_buffer[MAX_NUM_FIRMWARE];
	struct cz_buffer_entry meta_data_buffer[MAX_NUM_FIRMWARE];
	struct cz_buffer_entry scratch_buffer[MAX_NUM_SCRATCH];
};

#endif
