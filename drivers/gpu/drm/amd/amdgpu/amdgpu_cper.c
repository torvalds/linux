// SPDX-License-Identifier: GPL-2.0
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
#include <linux/list.h>
#include "amdgpu.h"

static const guid_t MCE			= CPER_NOTIFY_MCE;
static const guid_t CMC			= CPER_NOTIFY_CMC;
static const guid_t BOOT		= BOOT_TYPE;

static const guid_t CRASHDUMP		= AMD_CRASHDUMP;
static const guid_t RUNTIME		= AMD_GPU_NONSTANDARD_ERROR;

static void __inc_entry_length(struct cper_hdr *hdr, uint32_t size)
{
	hdr->record_length += size;
}

static void amdgpu_cper_get_timestamp(struct cper_timestamp *timestamp)
{
	struct tm tm;
	time64_t now = ktime_get_real_seconds();

	time64_to_tm(now, 0, &tm);
	timestamp->seconds = tm.tm_sec;
	timestamp->minutes = tm.tm_min;
	timestamp->hours = tm.tm_hour;
	timestamp->flag = 0;
	timestamp->day = tm.tm_mday;
	timestamp->month = 1 + tm.tm_mon;
	timestamp->year = (1900 + tm.tm_year) % 100;
	timestamp->century = (1900 + tm.tm_year) / 100;
}

void amdgpu_cper_entry_fill_hdr(struct amdgpu_device *adev,
				struct cper_hdr *hdr,
				enum amdgpu_cper_type type,
				enum cper_error_severity sev)
{
	hdr->signature[0]		= 'C';
	hdr->signature[1]		= 'P';
	hdr->signature[2]		= 'E';
	hdr->signature[3]		= 'R';
	hdr->revision			= CPER_HDR_REV_1;
	hdr->signature_end		= 0xFFFFFFFF;
	hdr->error_severity		= sev;

	hdr->valid_bits.platform_id	= 1;
	hdr->valid_bits.partition_id	= 1;
	hdr->valid_bits.timestamp	= 1;

	amdgpu_cper_get_timestamp(&hdr->timestamp);

	snprintf(hdr->record_id, 8, "%d", atomic_inc_return(&adev->cper.unique_id));
	snprintf(hdr->platform_id, 16, "0x%04X:0x%04X",
		 adev->pdev->vendor, adev->pdev->device);
	/* pmfw version should be part of creator_id according to CPER spec */
	snprintf(hdr->creator_id, 16, "%s", CPER_CREATOR_ID_AMDGPU);

	switch (type) {
	case AMDGPU_CPER_TYPE_BOOT:
		hdr->notify_type = BOOT;
		break;
	case AMDGPU_CPER_TYPE_FATAL:
	case AMDGPU_CPER_TYPE_BP_THRESHOLD:
		hdr->notify_type = MCE;
		break;
	case AMDGPU_CPER_TYPE_RUNTIME:
		if (sev == CPER_SEV_NON_FATAL_CORRECTED)
			hdr->notify_type = CMC;
		else
			hdr->notify_type = MCE;
		break;
	default:
		dev_err(adev->dev, "Unknown CPER Type\n");
		break;
	}

	__inc_entry_length(hdr, HDR_LEN);
}

static int amdgpu_cper_entry_fill_section_desc(struct amdgpu_device *adev,
					       struct cper_sec_desc *section_desc,
					       bool bp_threshold,
					       bool poison,
					       enum cper_error_severity sev,
					       guid_t sec_type,
					       uint32_t section_length,
					       uint32_t section_offset)
{
	section_desc->revision_minor		= CPER_SEC_MINOR_REV_1;
	section_desc->revision_major		= CPER_SEC_MAJOR_REV_22;
	section_desc->sec_offset		= section_offset;
	section_desc->sec_length		= section_length;
	section_desc->valid_bits.fru_id		= 1;
	section_desc->valid_bits.fru_text	= 1;
	section_desc->flag_bits.primary		= 1;
	section_desc->severity			= sev;
	section_desc->sec_type			= sec_type;

	if (adev->smuio.funcs &&
	    adev->smuio.funcs->get_socket_id)
		snprintf(section_desc->fru_text, 20, "OAM%d",
			 adev->smuio.funcs->get_socket_id(adev));
	/* TODO: fru_id is 16 bytes in CPER spec, but driver defines it as 20 bytes */
	snprintf(section_desc->fru_id, 16, "%llx", adev->unique_id);

	if (bp_threshold)
		section_desc->flag_bits.exceed_err_threshold = 1;
	if (poison)
		section_desc->flag_bits.latent_err = 1;

	return 0;
}

int amdgpu_cper_entry_fill_fatal_section(struct amdgpu_device *adev,
					 struct cper_hdr *hdr,
					 uint32_t idx,
					 struct cper_sec_crashdump_reg_data reg_data)
{
	struct cper_sec_desc *section_desc;
	struct cper_sec_crashdump_fatal *section;

	section_desc = (struct cper_sec_desc *)((uint8_t *)hdr + SEC_DESC_OFFSET(idx));
	section = (struct cper_sec_crashdump_fatal *)((uint8_t *)hdr +
		   FATAL_SEC_OFFSET(hdr->sec_cnt, idx));

	amdgpu_cper_entry_fill_section_desc(adev, section_desc, false, false,
					    CPER_SEV_FATAL, CRASHDUMP, FATAL_SEC_LEN,
					    FATAL_SEC_OFFSET(hdr->sec_cnt, idx));

	section->body.reg_ctx_type = CPER_CTX_TYPE_CRASH;
	section->body.reg_arr_size = sizeof(reg_data);
	section->body.data = reg_data;

	__inc_entry_length(hdr, SEC_DESC_LEN + FATAL_SEC_LEN);

	return 0;
}

int amdgpu_cper_entry_fill_runtime_section(struct amdgpu_device *adev,
					   struct cper_hdr *hdr,
					   uint32_t idx,
					   enum cper_error_severity sev,
					   uint32_t *reg_dump,
					   uint32_t reg_count)
{
	struct cper_sec_desc *section_desc;
	struct cper_sec_nonstd_err *section;
	bool poison;

	poison = (sev == CPER_SEV_NON_FATAL_CORRECTED) ? false : true;
	section_desc = (struct cper_sec_desc *)((uint8_t *)hdr + SEC_DESC_OFFSET(idx));
	section = (struct cper_sec_nonstd_err *)((uint8_t *)hdr +
		   NONSTD_SEC_OFFSET(hdr->sec_cnt, idx));

	amdgpu_cper_entry_fill_section_desc(adev, section_desc, false, poison,
					    sev, RUNTIME, NONSTD_SEC_LEN,
					    NONSTD_SEC_OFFSET(hdr->sec_cnt, idx));

	reg_count = min(reg_count, CPER_ACA_REG_COUNT);

	section->hdr.valid_bits.err_info_cnt = 1;
	section->hdr.valid_bits.err_context_cnt = 1;

	section->info.error_type = RUNTIME;
	section->info.ms_chk_bits.err_type_valid = 1;
	section->ctx.reg_ctx_type = CPER_CTX_TYPE_CRASH;
	section->ctx.reg_arr_size = sizeof(section->ctx.reg_dump);

	memcpy(section->ctx.reg_dump, reg_dump, reg_count * sizeof(uint32_t));

	__inc_entry_length(hdr, SEC_DESC_LEN + NONSTD_SEC_LEN);

	return 0;
}

int amdgpu_cper_entry_fill_bad_page_threshold_section(struct amdgpu_device *adev,
						      struct cper_hdr *hdr,
						      uint32_t idx)
{
	struct cper_sec_desc *section_desc;
	struct cper_sec_nonstd_err *section;

	section_desc = (struct cper_sec_desc *)((uint8_t *)hdr + SEC_DESC_OFFSET(idx));
	section = (struct cper_sec_nonstd_err *)((uint8_t *)hdr +
		   NONSTD_SEC_OFFSET(hdr->sec_cnt, idx));

	amdgpu_cper_entry_fill_section_desc(adev, section_desc, true, false,
					    CPER_SEV_FATAL, RUNTIME, NONSTD_SEC_LEN,
					    NONSTD_SEC_OFFSET(hdr->sec_cnt, idx));

	section->hdr.valid_bits.err_info_cnt = 1;
	section->hdr.valid_bits.err_context_cnt = 1;

	section->info.error_type = RUNTIME;
	section->info.ms_chk_bits.err_type_valid = 1;
	section->ctx.reg_ctx_type = CPER_CTX_TYPE_CRASH;
	section->ctx.reg_arr_size = sizeof(section->ctx.reg_dump);

	/* Hardcoded Reg dump for bad page threshold CPER */
	section->ctx.reg_dump[CPER_ACA_REG_CTL_LO]    = 0x1;
	section->ctx.reg_dump[CPER_ACA_REG_CTL_HI]    = 0x0;
	section->ctx.reg_dump[CPER_ACA_REG_STATUS_LO] = 0x137;
	section->ctx.reg_dump[CPER_ACA_REG_STATUS_HI] = 0xB0000000;
	section->ctx.reg_dump[CPER_ACA_REG_ADDR_LO]   = 0x0;
	section->ctx.reg_dump[CPER_ACA_REG_ADDR_HI]   = 0x0;
	section->ctx.reg_dump[CPER_ACA_REG_MISC0_LO]  = 0x0;
	section->ctx.reg_dump[CPER_ACA_REG_MISC0_HI]  = 0x0;
	section->ctx.reg_dump[CPER_ACA_REG_CONFIG_LO] = 0x2;
	section->ctx.reg_dump[CPER_ACA_REG_CONFIG_HI] = 0x1ff;
	section->ctx.reg_dump[CPER_ACA_REG_IPID_LO]   = 0x0;
	section->ctx.reg_dump[CPER_ACA_REG_IPID_HI]   = 0x96;
	section->ctx.reg_dump[CPER_ACA_REG_SYND_LO]   = 0x0;
	section->ctx.reg_dump[CPER_ACA_REG_SYND_HI]   = 0x0;

	__inc_entry_length(hdr, SEC_DESC_LEN + NONSTD_SEC_LEN);

	return 0;
}

struct cper_hdr *amdgpu_cper_alloc_entry(struct amdgpu_device *adev,
					 enum amdgpu_cper_type type,
					 uint16_t section_count)
{
	struct cper_hdr *hdr;
	uint32_t size = 0;

	size += HDR_LEN;
	size += (SEC_DESC_LEN * section_count);

	switch (type) {
	case AMDGPU_CPER_TYPE_RUNTIME:
	case AMDGPU_CPER_TYPE_BP_THRESHOLD:
		size += (NONSTD_SEC_LEN * section_count);
		break;
	case AMDGPU_CPER_TYPE_FATAL:
		size += (FATAL_SEC_LEN * section_count);
		break;
	case AMDGPU_CPER_TYPE_BOOT:
		size += (BOOT_SEC_LEN * section_count);
		break;
	default:
		dev_err(adev->dev, "Unknown CPER Type!\n");
		return NULL;
	}

	hdr = kzalloc(size, GFP_KERNEL);
	if (!hdr)
		return NULL;

	/* Save this early */
	hdr->sec_cnt = section_count;

	return hdr;
}

int amdgpu_cper_generate_ue_record(struct amdgpu_device *adev,
				   struct aca_bank *bank)
{
	struct cper_hdr *fatal = NULL;
	struct cper_sec_crashdump_reg_data reg_data = { 0 };
	int ret;

	fatal = amdgpu_cper_alloc_entry(adev, AMDGPU_CPER_TYPE_FATAL, 1);
	if (!fatal) {
		dev_err(adev->dev, "fail to alloc cper entry for ue record\n");
		return -ENOMEM;
	}

	reg_data.status_lo = lower_32_bits(bank->regs[ACA_REG_IDX_STATUS]);
	reg_data.status_hi = upper_32_bits(bank->regs[ACA_REG_IDX_STATUS]);
	reg_data.addr_lo   = lower_32_bits(bank->regs[ACA_REG_IDX_ADDR]);
	reg_data.addr_hi   = upper_32_bits(bank->regs[ACA_REG_IDX_ADDR]);
	reg_data.ipid_lo   = lower_32_bits(bank->regs[ACA_REG_IDX_IPID]);
	reg_data.ipid_hi   = upper_32_bits(bank->regs[ACA_REG_IDX_IPID]);
	reg_data.synd_lo   = lower_32_bits(bank->regs[ACA_REG_IDX_SYND]);
	reg_data.synd_hi   = upper_32_bits(bank->regs[ACA_REG_IDX_SYND]);

	amdgpu_cper_entry_fill_hdr(adev, fatal, AMDGPU_CPER_TYPE_FATAL, CPER_SEV_FATAL);
	ret = amdgpu_cper_entry_fill_fatal_section(adev, fatal, 0, reg_data);
	if (ret)
		return ret;

	/*TODO: commit the cper entry to cper ring */

	return 0;
}

static enum cper_error_severity amdgpu_aca_err_type_to_cper_sev(struct amdgpu_device *adev,
								enum aca_error_type aca_err_type)
{
	switch (aca_err_type) {
	case ACA_ERROR_TYPE_UE:
		return CPER_SEV_FATAL;
	case ACA_ERROR_TYPE_CE:
		return CPER_SEV_NON_FATAL_CORRECTED;
	case ACA_ERROR_TYPE_DEFERRED:
		return CPER_SEV_NON_FATAL_UNCORRECTED;
	default:
		dev_err(adev->dev, "Unknown ACA error type!\n");
		return CPER_SEV_FATAL;
	}
}

int amdgpu_cper_generate_ce_records(struct amdgpu_device *adev,
				    struct aca_banks *banks,
				    uint16_t bank_count)
{
	struct cper_hdr *corrected = NULL;
	enum cper_error_severity sev = CPER_SEV_NON_FATAL_CORRECTED;
	uint32_t reg_data[CPER_ACA_REG_COUNT] = { 0 };
	struct aca_bank_node *node;
	struct aca_bank *bank;
	uint32_t i = 0;
	int ret;

	corrected = amdgpu_cper_alloc_entry(adev, AMDGPU_CPER_TYPE_RUNTIME, bank_count);
	if (!corrected) {
		dev_err(adev->dev, "fail to allocate cper entry for ce records\n");
		return -ENOMEM;
	}

	/* Raise severity if any DE is detected in the ACA bank list */
	list_for_each_entry(node, &banks->list, node) {
		bank = &node->bank;
		if (bank->aca_err_type == ACA_ERROR_TYPE_DEFERRED) {
			sev = CPER_SEV_NON_FATAL_UNCORRECTED;
			break;
		}
	}

	amdgpu_cper_entry_fill_hdr(adev, corrected, AMDGPU_CPER_TYPE_RUNTIME, sev);

	/* Combine CE and UE in cper record */
	list_for_each_entry(node, &banks->list, node) {
		bank = &node->bank;
		reg_data[CPER_ACA_REG_CTL_LO]    = lower_32_bits(bank->regs[ACA_REG_IDX_CTL]);
		reg_data[CPER_ACA_REG_CTL_HI]    = upper_32_bits(bank->regs[ACA_REG_IDX_CTL]);
		reg_data[CPER_ACA_REG_STATUS_LO] = lower_32_bits(bank->regs[ACA_REG_IDX_STATUS]);
		reg_data[CPER_ACA_REG_STATUS_HI] = upper_32_bits(bank->regs[ACA_REG_IDX_STATUS]);
		reg_data[CPER_ACA_REG_ADDR_LO]   = lower_32_bits(bank->regs[ACA_REG_IDX_ADDR]);
		reg_data[CPER_ACA_REG_ADDR_HI]   = upper_32_bits(bank->regs[ACA_REG_IDX_ADDR]);
		reg_data[CPER_ACA_REG_MISC0_LO]  = lower_32_bits(bank->regs[ACA_REG_IDX_MISC0]);
		reg_data[CPER_ACA_REG_MISC0_HI]  = upper_32_bits(bank->regs[ACA_REG_IDX_MISC0]);
		reg_data[CPER_ACA_REG_CONFIG_LO] = lower_32_bits(bank->regs[ACA_REG_IDX_CONFIG]);
		reg_data[CPER_ACA_REG_CONFIG_HI] = upper_32_bits(bank->regs[ACA_REG_IDX_CONFIG]);
		reg_data[CPER_ACA_REG_IPID_LO]   = lower_32_bits(bank->regs[ACA_REG_IDX_IPID]);
		reg_data[CPER_ACA_REG_IPID_HI]   = upper_32_bits(bank->regs[ACA_REG_IDX_IPID]);
		reg_data[CPER_ACA_REG_SYND_LO]   = lower_32_bits(bank->regs[ACA_REG_IDX_SYND]);
		reg_data[CPER_ACA_REG_SYND_HI]   = upper_32_bits(bank->regs[ACA_REG_IDX_SYND]);

		ret = amdgpu_cper_entry_fill_runtime_section(adev, corrected, i++,
				amdgpu_aca_err_type_to_cper_sev(adev, bank->aca_err_type),
				reg_data, CPER_ACA_REG_COUNT);
		if (ret)
			return ret;
	}

	/*TODO: commit the cper entry to cper ring */

	return 0;
}

int amdgpu_cper_init(struct amdgpu_device *adev)
{
	mutex_init(&adev->cper.cper_lock);

	adev->cper.enabled = true;
	adev->cper.max_count = CPER_MAX_ALLOWED_COUNT;

	/*TODO: initialize cper ring*/

	return 0;
}

int amdgpu_cper_fini(struct amdgpu_device *adev)
{
	adev->cper.enabled = false;

	/*TODO: free cper ring */
	adev->cper.count = 0;
	adev->cper.wptr = 0;

	return 0;
}
