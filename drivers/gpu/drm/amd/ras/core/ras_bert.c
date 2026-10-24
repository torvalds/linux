// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, sublicense,
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

#include "ras.h"

#if defined(CONFIG_X86_MCE_AMD) && defined(CONFIG_ACPI_APEI)
#define RAS_BERT_ERR_INFO_COUNT(bits)  (((bits) & GENMASK_ULL(7, 2)) >> 2)
#define RAS_BERT_CONTEXT_COUNT(bits)   (((bits) & GENMASK_ULL(13, 8)) >> 8)
#define CPER_MEM_VALID_NODE            0x0008
#define CPER_MEM_VALID_ERROR_STATUS    0x0001
#define CPER_MEM_VALID_PA              0x0002
#define CPER_MEM_VALID_PA_MASK         0x0004
#define CPER_MEM_VALID_REQUESTOR_ID    0x0800
#define CPER_MEM_VALID_RESPONDER_ID    0x1000
#define CPER_MEM_VALID_TARGET_ID       0x2000
#define CPER_MEM_VALID_ERROR_TYPE      0x4000
#define AMD_CPER_BODY_VALID_MASK       GENMASK_ULL(15, 1)
#define RAS_BERT_VALID_FRU_STRING       BIT(1)
#define RAS_BERT_PROC_CTX_TYPE_MSR      1

struct ras_bert_hest_generic_status {
	u32 block_status;
	u32 raw_data_offset;
	u32 raw_data_length;
	u32 data_length;
	u32 error_severity;
};

struct ras_bert_hest_generic_data {
	u8 section_type[16];
	u32 error_severity;
	u16 revision;
	u8 validation_bits;
	u8 flags;
	u32 error_data_length;
	u8 fru_id[16];
	u8 fru_text[20];
};

struct ras_bert_hest_generic_data_v300 {
	u8 section_type[16];
	u32 error_severity;
	u16 revision;
	u8 validation_bits;
	u8 flags;
	u32 error_data_length;
	u8 fru_id[16];
	u8 fru_text[20];
	u64 time_stamp;
};

struct ras_bert_sec_proc_ia {
	u64 validation_bits;
	u64 lapic_id;
	u8 cpuid[48];
};

struct ras_bert_ia_err_info {
	u8 err_type[16];
	u64 validation_bits;
	u64 check_info;
	u64 target_id;
	u64 requestor_id;
	u64 responder_id;
	u64 ip;
};

struct ras_bert_ia_proc_ctx {
	u16 reg_ctx_type;
	u16 reg_arr_size;
	u32 msr_addr;
	u64 mm_reg_addr;
};

struct ras_bert_sec_mem_err_old {
	u64 validation_bits;
	u64 error_status;
	u64 physical_addr;
	u64 physical_addr_mask;
	u16 node;
	u16 card;
	u16 module;
	u16 bank;
	u16 device;
	u16 row;
	u16 column;
	u16 bit_pos;
	u64 requestor_id;
	u64 responder_id;
	u64 target_id;
	u8 error_type;
} __packed;

struct ras_bert_sec_mem_err {
	u64 validation_bits;
	u64 error_status;
	u64 physical_addr;
	u64 physical_addr_mask;
	u16 node;
	u16 card;
	u16 module;
	u16 bank;
	u16 device;
	u16 row;
	u16 column;
	u16 bit_pos;
	u64 requestor_id;
	u64 responder_id;
	u64 target_id;
	u8 error_type;
	u8 extended;
	u16 rank;
	u16 mem_array_handle;
	u16 mem_dev_handle;
} __packed;

static inline int ras_bert_get_version(struct ras_bert_hest_generic_data *gdata)
{
	return gdata->revision >> 8;
}

static inline void *ras_bert_get_payload(struct ras_bert_hest_generic_data *gdata)
{
	if (ras_bert_get_version(gdata) >= 3)
		return (void *)(((struct ras_bert_hest_generic_data_v300 *)(gdata)) + 1);

	return gdata + 1;
}

static inline int ras_bert_get_size(struct ras_bert_hest_generic_data *gdata)
{
	if (ras_bert_get_version(gdata) >= 3)
		return sizeof(struct ras_bert_hest_generic_data_v300);

	return sizeof(struct ras_bert_hest_generic_data);
}

static inline void *ras_bert_get_next(struct ras_bert_hest_generic_data *gdata)
{
	return (void *)gdata + ras_bert_get_size(gdata) + gdata->error_data_length;
}

static int ras_bert_estatus_check(const struct ras_bert_hest_generic_status *estatus)
{
	struct ras_bert_hest_generic_data *gdata;
	unsigned int data_len, record_size;

	if (estatus->data_length &&
	    estatus->data_length < sizeof(struct ras_bert_hest_generic_data))
		return -EINVAL;
	if (estatus->raw_data_length &&
	    estatus->raw_data_offset < sizeof(*estatus) + estatus->data_length)
		return -EINVAL;

	data_len = estatus->data_length;
	gdata = (struct ras_bert_hest_generic_data *)(estatus + 1);
	while ((void *)gdata - (void *)(estatus + 1) < estatus->data_length) {
		if (ras_bert_get_size(gdata) > data_len)
			return -EINVAL;

		record_size = ras_bert_get_size(gdata) + gdata->error_data_length;
		if (record_size > data_len)
			return -EINVAL;

		data_len -= record_size;
		gdata = ras_bert_get_next(gdata);
	}
	if (data_len)
		return -EINVAL;

	return 0;
}

typedef int (*ras_bert_section_parser)(struct ras_core_context *ras_core,
				       struct ras_bert_hest_generic_data *gdata);

struct ras_bert_section_dispatch {
	const u8 *section_type;
	ras_bert_section_parser parser;
};

static const struct ras_cper_guid ras_bert_guid_crashdump = GPU__CRASHDUMP;
static const struct ras_cper_guid ras_bert_guid_proc_ia = PROC_ERR__SECTION_TYPE;
static const struct ras_cper_guid ras_bert_guid_platform_mem = PLATFORM_MEM__SECTION_TYPE;
static const struct ras_cper_guid ras_bert_guid_pcie = PCIE_ERR__SECTION_TYPE;
static const struct ras_cper_guid ras_bert_guid_smn = SMN_ERR__SECTION_TYPE;

static u64 ras_bert_read_le64(const u8 *data)
{
	u64 value = 0;
	size_t i;

	for (i = 0; i < sizeof(value); i++)
		value |= (u64)data[i] << (i * 8);

	return value;
}

static void ras_bert_fill_boot_ctx(struct ras_boot_err_ctx *ctx,
				   struct ras_bert_hest_generic_data *gdata)
{
	memcpy(ctx->section_type, gdata->section_type, sizeof(ctx->section_type));
	ctx->error_severity = gdata->error_severity;
}

static bool ras_bert_amd_body_valid(const u8 *body, u32 body_len)
{
	u64 valid_bits;
	u32 context_count;
	u32 error_count;
	u32 min_size;

	if (body_len < sizeof(struct crashdump_hdr))
		return false;

	valid_bits = ras_bert_read_le64(body);
	if (valid_bits & ~AMD_CPER_BODY_VALID_MASK)
		return false;
	error_count = RAS_BERT_ERR_INFO_COUNT(valid_bits);
	context_count = RAS_BERT_CONTEXT_COUNT(valid_bits);
	if (!error_count && !context_count)
		return false;

	min_size = sizeof(struct crashdump_hdr) +
		(error_count * sizeof(struct crashdump_error_info)) +
		(context_count * sizeof(struct ras_bert_ia_proc_ctx));

	return min_size <= body_len;
}

static bool ras_bert_fru_matches_device(struct ras_core_context *ras_core,
					struct ras_bert_hest_generic_data *gdata)
{
	struct device_system_info dev_info = { 0 };
	char fru_text[sizeof(gdata->fru_text) + 1] = { 0 };
	unsigned int socket_id;

	if (!(gdata->validation_bits & RAS_BERT_VALID_FRU_STRING))
		return false;
	if (ras_core_get_device_system_info(ras_core, &dev_info))
		return false;

	memcpy(fru_text, gdata->fru_text, sizeof(gdata->fru_text));
	if (sscanf(fru_text, "OAM%u", &socket_id) != 1 &&
	    sscanf(fru_text, "EAM%u", &socket_id) != 1)
		return false;

	return socket_id == dev_info.socket_id;
}

static void ras_bert_log_processor_section(struct ras_core_context *ras_core,
					   struct ras_bert_hest_generic_data *gdata,
					   const struct ras_bert_sec_proc_ia *proc,
					   const struct ras_bert_ia_proc_ctx *ctx_info)
{
	struct ras_boot_err_ctx ctx = { 0 };
	u16 data_size;

	ras_bert_fill_boot_ctx(&ctx, gdata);
	ctx.apic_id = proc->lapic_id;
	if (proc->validation_bits & CPER_PROC_VALID_APIC_ID)
		ctx.flags |= RAS_BOOT_CTX_VALID_APIC_ID;
	ctx.reg_ctx_type = ctx_info->reg_ctx_type;
	ctx.msr_addr = ctx_info->msr_addr;
	ctx.mm_reg_addr = ctx_info->mm_reg_addr;
	data_size = min_t(u16, ctx_info->reg_arr_size, sizeof(ctx.regs));
	ctx.reg_arr_size = data_size;
	memcpy(ctx.regs, ctx_info + 1, data_size);

	ras_log_ring_add_log_event(ras_core, RAS_LOG_EVENT_BOOT, &ctx, sizeof(ctx), NULL);
}

static int ras_bert_record_amd_contexts(struct ras_core_context *ras_core,
					struct ras_bert_hest_generic_data *gdata,
					const u8 *body)
{
	const struct ras_bert_ia_proc_ctx *context;
	u64 valid_bits = ras_bert_read_le64(body);
	u32 error_count = RAS_BERT_ERR_INFO_COUNT(valid_bits);
	u32 context_count = RAS_BERT_CONTEXT_COUNT(valid_bits);
	u32 offset = sizeof(struct crashdump_hdr) +
		(error_count * sizeof(struct crashdump_error_info));
	u32 i;

	for (i = 0; i < context_count; i++) {
		struct ras_boot_err_ctx ctx = { 0 };
		u32 context_size;
		u16 data_size;

		if (offset > gdata->error_data_length ||
		    sizeof(*context) > gdata->error_data_length - offset)
			return -EINVAL;

		context = (const struct ras_bert_ia_proc_ctx *)(body + offset);
		context_size = ALIGN(sizeof(*context) + context->reg_arr_size, 16);
		if (context_size > gdata->error_data_length - offset)
			return -EINVAL;

		ras_bert_fill_boot_ctx(&ctx, gdata);
		ctx.reg_ctx_type = context->reg_ctx_type;
		ctx.msr_addr = context->msr_addr;
		ctx.mm_reg_addr = context->mm_reg_addr;
		data_size = min_t(u16, context->reg_arr_size, sizeof(ctx.regs));
		ctx.reg_arr_size = data_size;
		memcpy(ctx.regs, context + 1, data_size);
		ras_log_ring_add_log_event(ras_core, RAS_LOG_EVENT_BOOT,
					   &ctx, sizeof(ctx), NULL);

		offset += context_size;
	}

	return 0;
}

static int ras_bert_record_section(struct ras_core_context *ras_core,
				   struct ras_bert_hest_generic_data *gdata)
{
	struct aca_bank_ecc err = { 0 };
	struct aca_bank_reg bank = { 0 };
	struct device_system_info dev_info = { 0 };
	struct ras_boot_err_ctx ctx = { 0 };
	u64 *regs = ras_bert_get_payload(gdata);
	u32 data_size;
	int ret;

	if (ras_bert_amd_body_valid((u8 *)regs, gdata->error_data_length)) {
		if (!ras_bert_fru_matches_device(ras_core, gdata))
			return 0;

		return ras_bert_record_amd_contexts(ras_core, gdata,
						    (const u8 *)regs);
	}

	if (gdata->error_data_length < sizeof(u64) * (RAS_CPER_ACA_REG_IPID + 1))
		return 0;

	bank.bank_type = MCE_BANK_TYPE_GPU;
	data_size = min_t(u32, gdata->error_data_length, sizeof(bank.regs));
	memcpy(bank.regs, regs, data_size);
	ret = ras_aca_parse_bank(ras_core, &bank, &err);
	if (ret || ras_core_get_device_system_info(ras_core, &dev_info) ||
	    err.bank_info.socket_id != dev_info.socket_id)
		return 0;

	ras_bert_fill_boot_ctx(&ctx, gdata);
	ctx.reg_ctx_type = CPER_CTX_TYPE__BOOT;
	data_size = min_t(u32, data_size, sizeof(ctx.regs));
	ctx.reg_arr_size = data_size;
	memcpy(ctx.regs, regs, data_size);

	ras_log_ring_add_log_event(ras_core, RAS_LOG_EVENT_BOOT, &ctx, sizeof(ctx), NULL);

	return 0;
}

static int ras_bert_record_proc_ia_section(struct ras_core_context *ras_core,
					   struct ras_bert_hest_generic_data *gdata)
{
	struct ras_bert_ia_err_info *err_info;
	struct ras_bert_ia_proc_ctx *ctx_info;
	struct ras_bert_sec_proc_ia *proc;
	u8 *section_end;
	int ctx_count;
	int err_count;
	int i;

	if (gdata->error_data_length < sizeof(*proc))
		return -EINVAL;

	if (!ras_core->bert_platform_owner)
		return 0;

	proc = ras_bert_get_payload(gdata);
	section_end = (u8 *)proc + gdata->error_data_length;
	err_count = RAS_BERT_ERR_INFO_COUNT(proc->validation_bits);
	ctx_count = RAS_BERT_CONTEXT_COUNT(proc->validation_bits);
	err_info = (struct ras_bert_ia_err_info *)(proc + 1);
	if ((u8 *)err_info > section_end ||
	    err_count * sizeof(*err_info) > section_end - (u8 *)err_info)
		return -EINVAL;

	err_info += err_count;
	ctx_info = (struct ras_bert_ia_proc_ctx *)err_info;
	for (i = 0; i < ctx_count; i++) {
		size_t remaining;
		size_t ctx_size;

		if ((u8 *)ctx_info > section_end ||
		    sizeof(*ctx_info) > section_end - (u8 *)ctx_info)
			return -EINVAL;

		remaining = section_end - (u8 *)ctx_info;
		if (ctx_info->reg_arr_size > remaining - sizeof(*ctx_info))
			return -EINVAL;
		ctx_size = ALIGN(sizeof(*ctx_info) + ctx_info->reg_arr_size, 16);
		if (ctx_size > remaining)
			return -EINVAL;
		if (ctx_info->reg_ctx_type != RAS_BERT_PROC_CTX_TYPE_MSR ||
		    ctx_info->msr_addr < CPER_SMCA_MC0_STATUS_MSR ||
		    (ctx_info->msr_addr - CPER_SMCA_MC0_STATUS_MSR) %
		    CPER_SMCA_BANK_STRIDE)
			goto next_ctx;
		if (ctx_info->reg_arr_size < sizeof(u64))
			goto next_ctx;

		ras_bert_log_processor_section(ras_core, gdata, proc, ctx_info);

next_ctx:
		ctx_info = (struct ras_bert_ia_proc_ctx *)((u8 *)ctx_info + ctx_size);
	}

	return 0;
}

static int ras_bert_record_platform_mem_section(struct ras_core_context *ras_core,
						struct ras_bert_hest_generic_data *gdata)
{
	struct ras_boot_err_ctx ctx = { 0 };
	struct ras_bert_sec_mem_err *mem;
	u16 data_size;

	if (gdata->error_data_length != sizeof(struct ras_bert_sec_mem_err_old) &&
	    gdata->error_data_length != sizeof(struct ras_bert_sec_mem_err))
		return -EINVAL;

	if (!ras_core->bert_platform_owner)
		return 0;

	mem = ras_bert_get_payload(gdata);
	if (gdata->error_data_length > sizeof(ctx.raw_data))
		return -EOVERFLOW;

	ras_bert_fill_boot_ctx(&ctx, gdata);
	data_size = gdata->error_data_length;
	ctx.raw_data_size = data_size;
	memcpy(ctx.raw_data, mem, data_size);

	ras_log_ring_add_log_event(ras_core, RAS_LOG_EVENT_BOOT, &ctx, sizeof(ctx), NULL);

	return 0;
}

static const struct ras_bert_section_dispatch ras_bert_section_dispatch[] = {
	{ ras_bert_guid_crashdump.b, ras_bert_record_section },
	{ ras_bert_guid_pcie.b, ras_bert_record_section },
	{ ras_bert_guid_smn.b, ras_bert_record_section },
	{ ras_bert_guid_proc_ia.b, ras_bert_record_proc_ia_section },
	{ ras_bert_guid_platform_mem.b, ras_bert_record_platform_mem_section },
};

static const struct ras_bert_section_dispatch *
ras_bert_find_section_parser(const u8 *section_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ras_bert_section_dispatch); i++) {
		if (!memcmp(section_type, ras_bert_section_dispatch[i].section_type,
			    CPER_UUID_MAX_SIZE))
			return &ras_bert_section_dispatch[i];
	}

	return NULL;
}

int ras_bert_process_records(struct ras_core_context *ras_core,
			     const void *bert, u32 bert_len)
{
	struct ras_bert_hest_generic_status *estatus;
	struct ras_bert_hest_generic_data *gdata;
	u32 estatus_len;
	int remain;
	int ret;

	if (!ras_core || !bert || bert_len < sizeof(struct ras_bert_hest_generic_status))
		return -EINVAL;

	estatus = (struct ras_bert_hest_generic_status *)bert;
	remain = bert_len;

	while (remain >= sizeof(struct ras_bert_hest_generic_status)) {
		estatus_len = estatus->raw_data_length ?
			      estatus->raw_data_offset + estatus->raw_data_length :
			      sizeof(*estatus) + estatus->data_length;
		if (remain < estatus_len) {
			RAS_DEV_ERR(ras_core->dev, "truncated status block (length: %u).\n",
				    estatus_len);
			return -EINVAL;
		}
		if (!estatus_len)
			return -EINVAL;

		/*
		 * The generic APEI BERT path may have already consumed the boot
		 * error region and cleared block_status. AMDGPU still needs to
		 * parse the cached BERT payload, so do not use block_status to
		 * decide whether this status block contains records.
		 */
		if (ras_bert_estatus_check(estatus)) {
			RAS_DEV_ERR(ras_core->dev, "invalid error record.\n");
			return -EINVAL;
		}

		gdata = (struct ras_bert_hest_generic_data *)(estatus + 1);
		while ((void *)gdata - (void *)(estatus + 1) < estatus->data_length) {
			const struct ras_bert_section_dispatch *parser;

			parser = ras_bert_find_section_parser(gdata->section_type);
			if (parser) {
				ret = parser->parser(ras_core, gdata);
				if (ret)
					return ret;
			} else {
				RAS_DEV_INFO(ras_core->dev, "skip section: %pUl\n",
					     gdata->section_type);
			}

			gdata = ras_bert_get_next(gdata);
		}
		estatus = (struct ras_bert_hest_generic_status *)((u8 *)estatus + estatus_len);
		remain -= estatus_len;
	}

	return 0;
}
#endif
