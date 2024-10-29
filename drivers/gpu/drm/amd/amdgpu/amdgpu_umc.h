/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_UMC_H__
#define __AMDGPU_UMC_H__
#include "amdgpu_ras.h"
#include "amdgpu_mca.h"
/*
 * (addr / 256) * 4096, the higher 26 bits in ErrorAddr
 * is the index of 4KB block
 */
#define ADDR_OF_4KB_BLOCK(addr)			(((addr) & ~0xffULL) << 4)
/*
 * (addr / 256) * 8192, the higher 26 bits in ErrorAddr
 * is the index of 8KB block
 */
#define ADDR_OF_8KB_BLOCK(addr)			(((addr) & ~0xffULL) << 5)
/*
 * (addr / 256) * 32768, the higher 26 bits in ErrorAddr
 * is the index of 8KB block
 */
#define ADDR_OF_32KB_BLOCK(addr)			(((addr) & ~0xffULL) << 7)
/* channel index is the index of 256B block */
#define ADDR_OF_256B_BLOCK(channel_index)	((channel_index) << 8)
/* offset in 256B block */
#define OFFSET_IN_256B_BLOCK(addr)		((addr) & 0xffULL)

#define LOOP_UMC_INST(umc_inst) for ((umc_inst) = 0; (umc_inst) < adev->umc.umc_inst_num; (umc_inst)++)
#define LOOP_UMC_CH_INST(ch_inst) for ((ch_inst) = 0; (ch_inst) < adev->umc.channel_inst_num; (ch_inst)++)
#define LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) LOOP_UMC_INST((umc_inst)) LOOP_UMC_CH_INST((ch_inst))

#define LOOP_UMC_NODE_INST(node_inst) \
		for_each_set_bit((node_inst), &(adev->umc.active_mask), adev->umc.node_inst_num)

#define LOOP_UMC_EACH_NODE_INST_AND_CH(node_inst, umc_inst, ch_inst) \
		LOOP_UMC_NODE_INST((node_inst)) LOOP_UMC_INST_AND_CH((umc_inst), (ch_inst))

/* Page retirement tag */
#define UMC_ECC_NEW_DETECTED_TAG       0x1

typedef int (*umc_func)(struct amdgpu_device *adev, uint32_t node_inst,
			uint32_t umc_inst, uint32_t ch_inst, void *data);

struct amdgpu_umc_ras {
	struct amdgpu_ras_block_object ras_block;
	void (*err_cnt_init)(struct amdgpu_device *adev);
	bool (*query_ras_poison_mode)(struct amdgpu_device *adev);
	void (*ecc_info_query_ras_error_count)(struct amdgpu_device *adev,
				      void *ras_error_status);
	void (*ecc_info_query_ras_error_address)(struct amdgpu_device *adev,
					void *ras_error_status);
	bool (*check_ecc_err_status)(struct amdgpu_device *adev,
			enum amdgpu_mca_error_type type, void *ras_error_status);
	int (*update_ecc_status)(struct amdgpu_device *adev,
			uint64_t status, uint64_t ipid, uint64_t addr);
	int (*convert_ras_err_addr)(struct amdgpu_device *adev,
			struct ras_err_data *err_data,
			struct ta_ras_query_address_input *addr_in,
			struct ta_ras_query_address_output *addr_out,
			bool dump_addr);
};

struct amdgpu_umc_funcs {
	void (*init_registers)(struct amdgpu_device *adev);
};

struct amdgpu_umc {
	/* max error count in one ras query call */
	uint32_t max_ras_err_cnt_per_query;
	/* number of umc channel instance with memory map register access */
	uint32_t channel_inst_num;
	/* number of umc instance with memory map register access */
	uint32_t umc_inst_num;

	/* Total number of umc node instance including harvest one */
	uint32_t node_inst_num;

	/* UMC regiser per channel offset */
	uint32_t channel_offs;
	/* how many pages are retired in one UE */
	uint32_t retire_unit;
	/* channel index table of interleaved memory */
	const uint32_t *channel_idx_tbl;
	struct ras_common_if *ras_if;

	const struct amdgpu_umc_funcs *funcs;
	struct amdgpu_umc_ras *ras;

	/* active mask for umc node instance */
	unsigned long active_mask;
};

int amdgpu_umc_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_umc_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block);
int amdgpu_umc_poison_handler(struct amdgpu_device *adev,
			enum amdgpu_ras_block block, uint32_t reset);
int amdgpu_umc_pasid_poison_handler(struct amdgpu_device *adev,
			enum amdgpu_ras_block block, uint16_t pasid,
			pasid_notify pasid_fn, void *data, uint32_t reset);
int amdgpu_umc_process_ecc_irq(struct amdgpu_device *adev,
		struct amdgpu_irq_src *source,
		struct amdgpu_iv_entry *entry);
int amdgpu_umc_fill_error_record(struct ras_err_data *err_data,
		uint64_t err_addr,
		uint64_t retired_page,
		uint32_t channel_index,
		uint32_t umc_inst);

int amdgpu_umc_process_ras_data_cb(struct amdgpu_device *adev,
		void *ras_error_status,
		struct amdgpu_iv_entry *entry);
int amdgpu_umc_page_retirement_mca(struct amdgpu_device *adev,
			uint64_t err_addr, uint32_t ch_inst, uint32_t umc_inst);

int amdgpu_umc_loop_channels(struct amdgpu_device *adev,
			umc_func func, void *data);

int amdgpu_umc_update_ecc_status(struct amdgpu_device *adev,
				uint64_t status, uint64_t ipid, uint64_t addr);
int amdgpu_umc_logs_ecc_err(struct amdgpu_device *adev,
		struct radix_tree_root *ecc_tree, struct ras_ecc_err *ecc_err);

void amdgpu_umc_handle_bad_pages(struct amdgpu_device *adev,
			void *ras_error_status);
int amdgpu_umc_pages_in_a_row(struct amdgpu_device *adev,
			struct ras_err_data *err_data, uint64_t pa_addr);
int amdgpu_umc_lookup_bad_pages_in_a_row(struct amdgpu_device *adev,
			uint64_t pa_addr, uint64_t *pfns, int len);
int amdgpu_umc_mca_to_addr(struct amdgpu_device *adev,
			uint64_t err_addr, uint32_t ch, uint32_t umc,
			uint32_t node, uint32_t socket,
			struct ta_ras_query_address_output *addr_out, bool dump_addr);
#endif
