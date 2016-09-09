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
#ifndef _FIJI_SMUMANAGER_H_
#define _FIJI_SMUMANAGER_H_

#include "smu73_discrete.h"
#include <pp_endian.h>

#define SMC_RAM_END		0x40000

struct fiji_smu_avfs {
	enum AVFS_BTC_STATUS AvfsBtcStatus;
	uint32_t           AvfsBtcParam;
};

struct fiji_buffer_entry {
	uint32_t data_size;
	uint32_t mc_addr_low;
	uint32_t mc_addr_high;
	void *kaddr;
	unsigned long  handle;
};

struct fiji_smumgr {
	uint8_t        *header;
	uint8_t        *mec_image;

	uint32_t                             soft_regs_start;
	uint32_t                             dpm_table_start;
	uint32_t                             mc_reg_table_start;
	uint32_t                             fan_table_start;
	uint32_t                             arb_table_start;
	struct fiji_smu_avfs avfs;
	uint32_t        acpi_optimization;
	struct fiji_buffer_entry header_buffer;

	struct SMU73_Discrete_DpmTable       smc_state_table;
	struct SMU73_Discrete_Ulv            ulv_setting;
	struct SMU73_Discrete_PmFuses  power_tune_table;
	const struct fiji_pt_defaults  *power_tune_defaults;
	uint32_t        activity_target[SMU73_MAX_LEVELS_GRAPHICS];

};

int fiji_smum_init(struct pp_smumgr *smumgr);
int fiji_read_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smcAddress,
		uint32_t *value, uint32_t limit);
int fiji_write_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr,
		uint32_t value, uint32_t limit);
int fiji_copy_bytes_to_smc(struct pp_smumgr *smumgr, uint32_t smcStartAddress,
		const uint8_t *src,	uint32_t byteCount, uint32_t limit);

#endif

