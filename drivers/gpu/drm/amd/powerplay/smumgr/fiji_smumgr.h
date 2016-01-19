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

enum AVFS_BTC_STATUS {
	AVFS_BTC_BOOT = 0,
	AVFS_BTC_BOOT_STARTEDSMU,
	AVFS_LOAD_VIRUS,
	AVFS_BTC_VIRUS_LOADED,
	AVFS_BTC_VIRUS_FAIL,
	AVFS_BTC_STARTED,
	AVFS_BTC_FAILED,
	AVFS_BTC_RESTOREVFT_FAILED,
	AVFS_BTC_SAVEVFT_FAILED,
	AVFS_BTC_DPMTABLESETUP_FAILED,
	AVFS_BTC_COMPLETED_UNSAVED,
	AVFS_BTC_COMPLETED_SAVED,
	AVFS_BTC_COMPLETED_RESTORED,
	AVFS_BTC_DISABLED,
	AVFS_BTC_NOTSUPPORTED,
	AVFS_BTC_SMUMSG_ERROR
};

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
	uint32_t        soft_regs_start;
	struct fiji_smu_avfs avfs;
	uint32_t        acpi_optimization;

	struct fiji_buffer_entry header_buffer;
};

int fiji_smum_init(struct pp_smumgr *smumgr);
int fiji_read_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smcAddress,
		uint32_t *value, uint32_t limit);
int fiji_write_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr,
		uint32_t value, uint32_t limit);
int fiji_copy_bytes_to_smc(struct pp_smumgr *smumgr, uint32_t smcStartAddress,
		const uint8_t *src,	uint32_t byteCount, uint32_t limit);

#endif

