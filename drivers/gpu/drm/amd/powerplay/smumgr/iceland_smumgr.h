/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Huang Rui <ray.huang@amd.com>
 *
 */

#ifndef _ICELAND_SMUMGR_H_
#define _ICELAND_SMUMGR_H_

struct iceland_buffer_entry {
	uint32_t data_size;
	uint32_t mc_addr_low;
	uint32_t mc_addr_high;
	void *kaddr;
	unsigned long  handle;
};

/* Iceland only has header_buffer, don't have smu buffer. */
struct iceland_smumgr {
	uint8_t *pHeader;
	uint8_t *pMecImage;
	uint32_t ulSoftRegsStart;

	struct iceland_buffer_entry header_buffer;
};

extern int iceland_smum_init(struct pp_smumgr *smumgr);
extern int iceland_copy_bytes_to_smc(struct pp_smumgr *smumgr,
				     uint32_t smcStartAddress,
				     const uint8_t *src,
				     uint32_t byteCount, uint32_t limit);

extern int iceland_smu_start_smc(struct pp_smumgr *smumgr);

extern int iceland_read_smc_sram_dword(struct pp_smumgr *smumgr,
				       uint32_t smcAddress,
				       uint32_t *value, uint32_t limit);
extern int iceland_write_smc_sram_dword(struct pp_smumgr *smumgr,
					uint32_t smcAddress,
					uint32_t value, uint32_t limit);

extern bool iceland_is_smc_ram_running(struct pp_smumgr *smumgr);
extern int iceland_smu_upload_firmware_image(struct pp_smumgr *smumgr);

#endif
