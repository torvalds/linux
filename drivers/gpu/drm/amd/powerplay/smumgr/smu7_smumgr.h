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

#ifndef _SMU7_SMUMANAGER_H
#define _SMU7_SMUMANAGER_H


#include <pp_endian.h>

#define SMC_RAM_END 0x40000

struct smu7_buffer_entry {
	uint32_t data_size;
	uint64_t mc_addr;
	void *kaddr;
	struct amdgpu_bo *handle;
};

struct smu7_smumgr {
	uint8_t *header;
	uint8_t *mec_image;
	struct smu7_buffer_entry smu_buffer;
	struct smu7_buffer_entry header_buffer;

	uint32_t                             soft_regs_start;
	uint32_t                             dpm_table_start;
	uint32_t                             mc_reg_table_start;
	uint32_t                             fan_table_start;
	uint32_t                             arb_table_start;
	uint32_t                             ulv_setting_starts;
	uint8_t                              security_hard_key;
	uint32_t                             acpi_optimization;
	uint32_t                             avfs_btc_param;
};


int smu7_copy_bytes_from_smc(struct pp_hwmgr *hwmgr, uint32_t smc_start_address,
				uint32_t *dest, uint32_t byte_count, uint32_t limit);
int smu7_copy_bytes_to_smc(struct pp_hwmgr *hwmgr, uint32_t smc_start_address,
			const uint8_t *src, uint32_t byte_count, uint32_t limit);
int smu7_program_jump_on_start(struct pp_hwmgr *hwmgr);
bool smu7_is_smc_ram_running(struct pp_hwmgr *hwmgr);
int smu7_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg);
int smu7_send_msg_to_smc_without_waiting(struct pp_hwmgr *hwmgr, uint16_t msg);
int smu7_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr, uint16_t msg,
						uint32_t parameter);
int smu7_send_msg_to_smc_with_parameter_without_waiting(struct pp_hwmgr *hwmgr,
						uint16_t msg, uint32_t parameter);
int smu7_send_msg_to_smc_offset(struct pp_hwmgr *hwmgr);
int smu7_wait_for_smc_inactive(struct pp_hwmgr *hwmgr);

enum cgs_ucode_id smu7_convert_fw_type_to_cgs(uint32_t fw_type);
int smu7_read_smc_sram_dword(struct pp_hwmgr *hwmgr, uint32_t smc_addr,
						uint32_t *value, uint32_t limit);
int smu7_write_smc_sram_dword(struct pp_hwmgr *hwmgr, uint32_t smc_addr,
						uint32_t value, uint32_t limit);

int smu7_request_smu_load_fw(struct pp_hwmgr *hwmgr);
int smu7_check_fw_load_finish(struct pp_hwmgr *hwmgr, uint32_t fw_type);
int smu7_reload_firmware(struct pp_hwmgr *hwmgr);
int smu7_upload_smu_firmware_image(struct pp_hwmgr *hwmgr);
int smu7_init(struct pp_hwmgr *hwmgr);
int smu7_smu_fini(struct pp_hwmgr *hwmgr);

int smu7_setup_pwr_virus(struct pp_hwmgr *hwmgr);

#endif
