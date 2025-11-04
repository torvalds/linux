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

#ifndef __AMDGPU_CPER_H__
#define __AMDGPU_CPER_H__

#include "amd_cper.h"
#include "amdgpu_aca.h"

#define CPER_MAX_ALLOWED_COUNT		0x1000
#define CPER_MAX_RING_SIZE		0X100000
#define HDR_LEN				(sizeof(struct cper_hdr))
#define SEC_DESC_LEN			(sizeof(struct cper_sec_desc))

#define BOOT_SEC_LEN			(sizeof(struct cper_sec_crashdump_boot))
#define FATAL_SEC_LEN			(sizeof(struct cper_sec_crashdump_fatal))
#define NONSTD_SEC_LEN			(sizeof(struct cper_sec_nonstd_err))

#define SEC_DESC_OFFSET(idx)		(HDR_LEN + (SEC_DESC_LEN * idx))

#define BOOT_SEC_OFFSET(count, idx)	(HDR_LEN + (SEC_DESC_LEN * count) + (BOOT_SEC_LEN * idx))
#define FATAL_SEC_OFFSET(count, idx)	(HDR_LEN + (SEC_DESC_LEN * count) + (FATAL_SEC_LEN * idx))
#define NONSTD_SEC_OFFSET(count, idx)	(HDR_LEN + (SEC_DESC_LEN * count) + (NONSTD_SEC_LEN * idx))

enum amdgpu_cper_type {
	AMDGPU_CPER_TYPE_RUNTIME,
	AMDGPU_CPER_TYPE_FATAL,
	AMDGPU_CPER_TYPE_BOOT,
	AMDGPU_CPER_TYPE_BP_THRESHOLD,
};

struct amdgpu_cper {
	bool enabled;

	atomic_t unique_id;
	struct mutex cper_lock;

	/* Lifetime CPERs generated */
	uint32_t count;
	uint32_t max_count;

	uint32_t wptr;

	void *ring[CPER_MAX_ALLOWED_COUNT];
	struct amdgpu_ring ring_buf;
	struct mutex ring_lock;
};

void amdgpu_cper_entry_fill_hdr(struct amdgpu_device *adev,
				struct cper_hdr *hdr,
				enum amdgpu_cper_type type,
				enum cper_error_severity sev);
int amdgpu_cper_entry_fill_fatal_section(struct amdgpu_device *adev,
					 struct cper_hdr *hdr,
					 uint32_t idx,
					 struct cper_sec_crashdump_reg_data reg_data);
int amdgpu_cper_entry_fill_runtime_section(struct amdgpu_device *adev,
					   struct cper_hdr *hdr,
					   uint32_t idx,
					   enum cper_error_severity sev,
					   uint32_t *reg_dump,
					   uint32_t reg_count);
int amdgpu_cper_entry_fill_bad_page_threshold_section(struct amdgpu_device *adev,
						      struct cper_hdr *hdr,
						      uint32_t section_idx);

struct cper_hdr *amdgpu_cper_alloc_entry(struct amdgpu_device *adev,
					 enum amdgpu_cper_type type,
					 uint16_t section_count);
/* UE must be encoded into separated cper entries, 1 UE 1 cper */
int amdgpu_cper_generate_ue_record(struct amdgpu_device *adev,
				   struct aca_bank *bank);
/* CEs and DEs are combined into 1 cper entry */
int amdgpu_cper_generate_ce_records(struct amdgpu_device *adev,
				    struct aca_banks *banks,
				    uint16_t bank_count);
/* Bad page threshold is encoded into separated cper entry */
int amdgpu_cper_generate_bp_threshold_record(struct amdgpu_device *adev);
void amdgpu_cper_ring_write(struct amdgpu_ring *ring,
			void *src, int count);
int amdgpu_cper_init(struct amdgpu_device *adev);
int amdgpu_cper_fini(struct amdgpu_device *adev);

#endif
