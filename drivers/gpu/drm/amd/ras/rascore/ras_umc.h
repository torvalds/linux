/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef __RAS_UMC_H__
#define __RAS_UMC_H__
#include "ras.h"
#include "ras_eeprom.h"
#include "ras_cmd.h"

#define UMC_VRAM_TYPE_UNKNOWN 0
#define UMC_VRAM_TYPE_GDDR1   1
#define UMC_VRAM_TYPE_DDR2    2
#define UMC_VRAM_TYPE_GDDR3   3
#define UMC_VRAM_TYPE_GDDR4   4
#define UMC_VRAM_TYPE_GDDR5   5
#define UMC_VRAM_TYPE_HBM     6
#define UMC_VRAM_TYPE_DDR3    7
#define UMC_VRAM_TYPE_DDR4    8
#define UMC_VRAM_TYPE_GDDR6   9
#define UMC_VRAM_TYPE_DDR5    10
#define UMC_VRAM_TYPE_LPDDR4  11
#define UMC_VRAM_TYPE_LPDDR5  12
#define UMC_VRAM_TYPE_HBM3E   13

#define UMC_ECC_NEW_DETECTED_TAG       0x1
#define UMC_INV_MEM_PFN  (0xFFFFFFFFFFFFFFFF)

/* three column bits and one row bit in MCA address flip
 * in bad page retirement
 */
#define UMC_PA_FLIP_BITS_NUM 4

enum umc_memory_partition_mode {
	UMC_MEMORY_PARTITION_MODE_NONE = 0,
	UMC_MEMORY_PARTITION_MODE_NPS1 = 1,
	UMC_MEMORY_PARTITION_MODE_NPS2 = 2,
	UMC_MEMORY_PARTITION_MODE_NPS3 = 3,
	UMC_MEMORY_PARTITION_MODE_NPS4 = 4,
	UMC_MEMORY_PARTITION_MODE_NPS6 = 6,
	UMC_MEMORY_PARTITION_MODE_NPS8 = 8,
	UMC_MEMORY_PARTITION_MODE_UNKNOWN
};

struct ras_core_context;
struct ras_bank_ecc;

struct umc_flip_bits {
	uint32_t flip_bits_in_pa[UMC_PA_FLIP_BITS_NUM];
	uint32_t flip_row_bit;
	uint32_t r13_in_pa;
	uint32_t bit_num;
};

struct umc_mca_addr {
	uint64_t err_addr;
	uint32_t ch_inst;
	uint32_t umc_inst;
	uint32_t node_inst;
	uint32_t socket_id;
};

struct umc_phy_addr {
	uint64_t pa;
	uint32_t bank;
	uint32_t channel_idx;
};

struct umc_bank_addr {
	uint32_t stack_id; /* SID */
	uint32_t bank_group;
	uint32_t bank;
	uint32_t row;
	uint32_t column;
	uint32_t channel;
	uint32_t subchannel; /* Also called Pseudochannel (PC) */
};

struct ras_umc_ip_func {
	int (*bank_to_eeprom_record)(struct ras_core_context *ras_core,
			struct ras_bank_ecc *bank, struct eeprom_umc_record *record);
	int (*eeprom_record_to_nps_record)(struct ras_core_context *ras_core,
			struct eeprom_umc_record *record, uint32_t nps);
	int (*eeprom_record_to_nps_pages)(struct ras_core_context *ras_core,
			struct eeprom_umc_record *record, uint32_t nps,
			uint64_t *pfns, uint32_t num);
	int (*bank_to_soc_pa)(struct ras_core_context *ras_core,
			struct umc_bank_addr bank_addr, uint64_t *soc_pa);
	int (*soc_pa_to_bank)(struct ras_core_context *ras_core,
			uint64_t soc_pa, struct umc_bank_addr *bank_addr);
};

struct eeprom_store_record {
	/* point to data records array */
	struct eeprom_umc_record *bps;
	/* the count of entries */
	int count;
	/* the space can place new entries */
	int space_left;
};

struct ras_umc_err_data {
	struct eeprom_store_record rom_data;
	struct eeprom_store_record ram_data;
	enum umc_memory_partition_mode umc_nps_mode;
	uint64_t last_retired_pfn;
};

struct ras_umc {
	u32 umc_ip_version;
	u32 umc_vram_type;
	const struct ras_umc_ip_func *ip_func;
	struct radix_tree_root root;
	struct mutex  tree_lock;
	struct mutex  umc_lock;
	struct mutex  bank_log_lock;
	struct mutex  pending_ecc_lock;
	struct ras_umc_err_data umc_err_data;
	struct list_head pending_ecc_list;
};

int ras_umc_sw_init(struct ras_core_context *ras);
int ras_umc_sw_fini(struct ras_core_context *ras);
int ras_umc_hw_init(struct ras_core_context *ras);
int ras_umc_hw_fini(struct ras_core_context *ras);
int ras_umc_psp_convert_ma_to_pa(struct ras_core_context *ras_core,
		struct umc_mca_addr *in, struct umc_phy_addr *out,
		uint32_t nps);
int ras_umc_handle_bad_pages(struct ras_core_context *ras_core, void *data);
int ras_umc_log_bad_bank(struct ras_core_context *ras, struct ras_bank_ecc *bank);
int ras_umc_log_bad_bank_pending(struct ras_core_context *ras_core, struct ras_bank_ecc *bank);
int ras_umc_log_pending_bad_bank(struct ras_core_context *ras_core);
int ras_umc_clear_logged_ecc(struct ras_core_context *ras_core);
int ras_umc_load_bad_pages(struct ras_core_context *ras_core);
int ras_umc_get_saved_eeprom_count(struct ras_core_context *ras_core);
int ras_umc_clean_badpage_data(struct ras_core_context *ras_core);
int ras_umc_fill_eeprom_record(struct ras_core_context *ras_core,
		uint64_t err_addr, uint32_t umc_inst, struct umc_phy_addr *cur_nps_addr,
		enum umc_memory_partition_mode cur_nps, struct eeprom_umc_record *record);

int ras_umc_get_badpage_count(struct ras_core_context *ras_core);
int ras_umc_get_badpage_record(struct ras_core_context *ras_core, uint32_t index, void *record);
bool ras_umc_check_retired_addr(struct ras_core_context *ras_core, uint64_t addr);
int ras_umc_translate_soc_pa_and_bank(struct ras_core_context *ras_core,
			uint64_t *soc_pa, struct umc_bank_addr *bank_addr, bool bank_to_pa);
#endif
