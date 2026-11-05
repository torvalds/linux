// SPDX-License-Identifier: MIT
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
#include "ras.h"
#include "core_status.h"
#include "log_ring.h"
#include "ras_cper.h"
#define ns_to_seconds(ns)   div_u64(ns, NSEC_PER_SEC)

static const struct ras_cper_guid processor_section_type =
	PROC_ERR__SECTION_TYPE;
static const struct ras_cper_guid platform_memory_section_type =
	PLATFORM_MEM__SECTION_TYPE;

static int ras_cper_get_profile(struct ras_core_context *ras_core,
		enum ras_log_event event, struct ras_cper_profile **profile);

static void cper_get_timestamp(struct ras_core_context *ras_core,
		struct ras_cper_timestamp *timestamp, uint64_t utc_second_timestamp)
{
	struct ras_time tm = {0};

	ras_core_convert_timestamp_to_time(ras_core, utc_second_timestamp, &tm);
	timestamp->seconds = tm.tm_sec;
	timestamp->minutes = tm.tm_min;
	timestamp->hours = tm.tm_hour;
	timestamp->flag = 0;
	timestamp->day = tm.tm_mday;
	timestamp->month = tm.tm_mon;
	timestamp->year = tm.tm_year % 100;
	timestamp->century = tm.tm_year / 100;
}

static void fill_section_hdr(struct ras_core_context *ras_core,
		struct cper_section_hdr *hdr, struct ras_cper_guid notify_type,
		enum ras_cper_severity sev, struct ras_log_info *log)
{
	struct device_system_info dev_info = {0};
	char record_id[32];

	hdr->signature[0]		= 'C';
	hdr->signature[1]		= 'P';
	hdr->signature[2]		= 'E';
	hdr->signature[3]		= 'R';
	hdr->revision			= CPER_HDR__REV_1;
	hdr->signature_end		= 0xFFFFFFFF;
	hdr->error_severity		= (sev == RAS_CPER_SEV_RMA ? RAS_CPER_SEV_FATAL_UE : sev);

	hdr->valid_bits.platform_id	= 1;
	hdr->valid_bits.timestamp	= 1;

	ras_core_get_device_system_info(ras_core, &dev_info);

	cper_get_timestamp(ras_core, &hdr->timestamp, ns_to_seconds(log->timestamp));

	snprintf(record_id, sizeof(record_id), "%d:%llX", dev_info.socket_id,
		    RAS_LOG_SEQNO_TO_BATCH_IDX(log->seqno));
	memcpy(hdr->record_id, record_id, 8);

	snprintf(hdr->platform_id, 16, "0x%04X:0x%04X",
		dev_info.vendor_id, dev_info.device_id);
	/* pmfw version should be part of creator_id according to CPER spec */
	snprintf(hdr->creator_id, 16, "%s", CPER_CREATOR_ID__AMDGPU);

	hdr->notify_type = notify_type;
}

static int fill_section_descriptor(struct ras_core_context *ras_core,
		struct cper_section_desc *descriptor, enum ras_cper_severity sev,
		struct ras_cper_guid sec_type, uint32_t sec_offset, uint32_t sec_len)
{
	struct device_system_info dev_info = {0};

	descriptor->revision_minor		= CPER_SEC__MINOR_REV_1;
	descriptor->revision_major		= CPER_SEC__MAJOR_REV_22;
	descriptor->sec_offset		= sec_offset;
	descriptor->sec_length		= sec_len;
	descriptor->valid_bits.fru_text = 1;
	descriptor->flag_bits.primary	= 1;
	descriptor->severity = (sev == RAS_CPER_SEV_RMA ? RAS_CPER_SEV_FATAL_UE : sev);
	descriptor->sec_type			= sec_type;

	ras_core_get_device_system_info(ras_core, &dev_info);

	snprintf(descriptor->fru_text, 20, "OAM%d", dev_info.socket_id);

	if (sev == RAS_CPER_SEV_RMA)
		descriptor->flag_bits.exceed_err_threshold = 1;

	if (sev == RAS_CPER_SEV_NON_FATAL_UE)
		descriptor->flag_bits.latent_err = 1;

	return 0;
}

static int fill_section_fatal(struct ras_core_context *ras_core, void *section,
		struct ras_log_info *log, enum ras_cper_severity sev)
{
	struct cper_section_fatal *fatal = section;

	fatal->hdr.valid_bits = CPER_AMD_ERR_INFO_COUNT(1) |
		CPER_AMD_CONTEXT_COUNT(1);
	fatal->error_info.error_type = GPU__NONSTANDARD_ERROR;
	fatal->data.reg_ctx_type = CPER_CTX_TYPE__CRASH;
	fatal->data.reg_arr_size = sizeof(fatal->data.reg);

	fatal->data.reg.status = log->body.aca_reg.regs[RAS_CPER_ACA_REG_STATUS];
	fatal->data.reg.addr   = log->body.aca_reg.regs[RAS_CPER_ACA_REG_ADDR];
	fatal->data.reg.ipid   = log->body.aca_reg.regs[RAS_CPER_ACA_REG_IPID];
	fatal->data.reg.synd   = log->body.aca_reg.regs[RAS_CPER_ACA_REG_SYND];

	return 0;
}

static int fill_section_runtime(struct ras_core_context *ras_core, void *section,
		struct ras_log_info *log, enum ras_cper_severity sev)
{
	struct cper_section_runtime *runtime = section;

	runtime->hdr.valid_bits.err_info_cnt = 1;
	runtime->hdr.valid_bits.err_context_cnt = 1;

	runtime->descriptor.error_type = GPU__NONSTANDARD_ERROR;
	runtime->descriptor.ms_chk_bits.err_type_valid = 1;
	if (sev == RAS_CPER_SEV_RMA) {
		runtime->descriptor.valid_bits.ms_chk = 1;
		runtime->descriptor.ms_chk_bits.err_type = 1;
		runtime->descriptor.ms_chk_bits.pcc = 1;
	}

	runtime->reg.reg_ctx_type = CPER_CTX_TYPE__CRASH;
	runtime->reg.reg_arr_size = sizeof(runtime->reg.reg_dump);

	runtime->reg.reg_dump[RAS_CPER_ACA_REG_CTL] =
			log->body.aca_reg.regs[ACA_REG_IDX__CTL];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_STATUS] =
			log->body.aca_reg.regs[ACA_REG_IDX__STATUS];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_ADDR] =
			log->body.aca_reg.regs[ACA_REG_IDX__ADDR];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_MISC0] =
			log->body.aca_reg.regs[ACA_REG_IDX__MISC0];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_CONFIG] =
			log->body.aca_reg.regs[ACA_REG_IDX__CONFG];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_IPID] =
			log->body.aca_reg.regs[ACA_REG_IDX__IPID];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_SYND] =
			log->body.aca_reg.regs[ACA_REG_IDX__SYND];

	return 0;
}

static int fill_section_boot(struct ras_core_context *ras_core, void *section,
		struct ras_log_info *log, enum ras_cper_severity sev)
{
	struct cper_section_boot *boot = section;
	struct ras_boot_err_ctx *ctx = &log->body.boot_err_ctx;
	struct crashdump_boot *data = &boot->data;
	u16 data_size;

	boot->hdr.valid_bits = CPER_AMD_ERR_INFO_COUNT(1) |
		CPER_AMD_CONTEXT_COUNT(1);
	boot->error_info.error_type = GPU__NONSTANDARD_ERROR;
	data->reg_ctx_type = ctx->reg_ctx_type;
	data_size = min_t(u16, ctx->reg_arr_size, sizeof(data->msg));
	data->reg_arr_size = data_size;
	memcpy(data->msg, ctx->regs, data_size);

	return 0;
}

static void fill_processor_error_info(struct cper_processor_error_info *error_info,
		u64 status)
{
	u64 check_info = CPER_MS_CHECK_VALID_ERR_TYPE |
		CPER_MS_CHECK_ERR_TYPE_INTERNAL;

	error_info->error_type = PROC_ERR__MS_CHECK_TYPE;
	error_info->valid_bits = CPER_PROC_INFO_VALID_CHECK_INFO;

	if (status & CPER_MCA_STATUS_PCC)
		check_info |= CPER_MS_CHECK_VALID_PCC | CPER_MS_CHECK_PCC;
	if (status & CPER_MCA_STATUS_UNCORRECTED)
		check_info |= CPER_MS_CHECK_VALID_UNCORRECTED |
			CPER_MS_CHECK_UNCORRECTED;
	if (status & CPER_MCA_STATUS_OVERFLOW)
		check_info |= CPER_MS_CHECK_VALID_OVERFLOW |
			CPER_MS_CHECK_OVERFLOW;

	error_info->check_info = check_info;
}

static int fill_section_processor(struct cper_section_processor *processor,
		struct ras_log_info *log)
{
	const struct ras_cpu_mce *mce = &log->body.cpu_mce;
	u64 status = mce->regs[RAS_CPER_ACA_REG_STATUS];
	u64 *reg_dump = processor->context.reg_dump;

	processor->processor.valid_bits = CPER_PROC_VALID_APIC_ID |
		CPER_PROC_ERR_INFO_COUNT(1) | CPER_PROC_CONTEXT_COUNT(1);
	processor->processor.apic_id = mce->apic_id;
	fill_processor_error_info(&processor->error_info, status);
	processor->context.reg_ctx_type = CPER_CTX_TYPE__CRASH;
	processor->context.reg_arr_size = sizeof(processor->context.reg_dump);
	processor->context.msr_addr = CPER_SMCA_MC0_STATUS_MSR +
		(mce->bank * CPER_SMCA_BANK_STRIDE);
	/* The IA processor register array starts at MCA_STATUS. */
	reg_dump[0] = mce->regs[ACA_REG_IDX__STATUS];
	reg_dump[1] = mce->regs[ACA_REG_IDX__ADDR];
	reg_dump[2] = mce->regs[ACA_REG_IDX__MISC0];
	reg_dump[3] = mce->regs[ACA_REG_IDX__CONFG];
	reg_dump[4] = mce->regs[ACA_REG_IDX__IPID];
	reg_dump[5] = mce->regs[ACA_REG_IDX__SYND];
	reg_dump[7] = mce->regs[ACA_REG_IDX__DESTAT];
	reg_dump[8] = mce->regs[ACA_REG_IDX__DEADDR];
	reg_dump[9] = mce->regs[ACA_REG_IDX__CTL_MASK];

	return 0;
}

static int fill_section_boot_processor(struct cper_section_processor *processor,
		struct ras_log_info *log)
{
	const struct ras_boot_err_ctx *ctx = &log->body.boot_err_ctx;
	u64 status = ctx->regs[0];

	processor->processor.valid_bits = CPER_PROC_ERR_INFO_COUNT(1) |
		CPER_PROC_CONTEXT_COUNT(1);
	if (ctx->flags & RAS_BOOT_CTX_VALID_APIC_ID)
		processor->processor.valid_bits |= CPER_PROC_VALID_APIC_ID;
	processor->processor.apic_id = ctx->apic_id;
	fill_processor_error_info(&processor->error_info, status);
	processor->context.reg_ctx_type = ctx->reg_ctx_type;
	processor->context.reg_arr_size = ctx->reg_arr_size;
	processor->context.msr_addr = ctx->msr_addr;
	processor->context.mm_reg_addr = ctx->mm_reg_addr;
	memcpy(processor->context.reg_dump, ctx->regs,
		min_t(size_t, ctx->reg_arr_size,
		      sizeof(processor->context.reg_dump)));

	return 0;
}

static int cper_boot_get_severity(struct ras_log_info *log,
			enum ras_cper_severity *sev)
{
	*sev = log->body.boot_err_ctx.error_severity;
	return 0;
}

static int cper_boot_get_sec_type(struct ras_log_info *log, struct ras_cper_guid *out)
{
	memcpy(out, &log->body.boot_err_ctx.section_type,
		min(sizeof(*out), sizeof(log->body.boot_err_ctx.section_type)));
	return 0;
}

static uint32_t cper_get_record_size(const struct ras_cper_profile *profile,
		uint32_t section_count)
{
	if (profile->build_record_per_log)
		return (RAS_HDR_LEN + RAS_SEC_DESC_LEN + profile->section_size) *
			section_count;

	return RAS_HDR_LEN +
		(RAS_SEC_DESC_LEN + profile->section_size) * section_count;
}

static int cper_build_single_record(struct ras_core_context *ras_core,
		struct cper_section_hdr *hdr, const struct ras_cper_profile *profile,
		struct ras_log_info *batch_logs, uint32_t nr_batch_logs)
{
	enum ras_cper_severity sev = profile->severity;
	struct ras_cper_guid sec_type = profile->sec_type;
	struct ras_cper_guid notify_type = profile->notify_type;
	struct cper_section_desc *descriptor;
	uint32_t desc_off, sec_off;
	void *section;
	uint32_t i;

	if (profile->get_severity)
		profile->get_severity(&batch_logs[0], &sev);

	if (profile->get_sec_type)
		profile->get_sec_type(&batch_logs[0], &sec_type);

	if (profile->get_notify_type)
		profile->get_notify_type(&batch_logs[0], &notify_type);

	fill_section_hdr(ras_core, hdr, notify_type, sev, &batch_logs[0]);
	hdr->sec_cnt = nr_batch_logs;
	hdr->record_length = RAS_HDR_LEN +
		(RAS_SEC_DESC_LEN + profile->section_size) * nr_batch_logs;

	for (i = 0; i < nr_batch_logs; i++) {
		desc_off = RAS_HDR_LEN + (RAS_SEC_DESC_LEN * i);
		sec_off  = RAS_HDR_LEN + (RAS_SEC_DESC_LEN * nr_batch_logs) +
			   (profile->section_size * i);

		descriptor = (struct cper_section_desc *)((uint8_t *)hdr + desc_off);
		section = (uint8_t *)hdr + sec_off;

		fill_section_descriptor(ras_core, descriptor, sev,
			sec_type, sec_off, profile->section_size);

		profile->fill_section(ras_core, section, &batch_logs[i], sev);
	}

	return 0;
}

static int cper_build_multiple_records(struct ras_core_context *ras_core,
		uint8_t *buffer, const struct ras_cper_profile *profile,
		struct ras_log_info *batch_logs, uint32_t nr_batch_logs)
{
	uint32_t rec_len = RAS_HDR_LEN + RAS_SEC_DESC_LEN + profile->section_size;
	uint32_t i;

	for (i = 0; i < nr_batch_logs; i++) {
		struct ras_log_info *log = &batch_logs[i];
		enum ras_cper_severity sev = profile->severity;
		struct ras_cper_guid sec_type = profile->sec_type;
		struct ras_cper_guid notify_type = profile->notify_type;
		struct cper_section_hdr *hdr;
		struct cper_section_desc *descriptor;
		void *section;

		if (profile->get_severity)
			profile->get_severity(log, &sev);

		if (profile->get_sec_type)
			profile->get_sec_type(log, &sec_type);

		if (profile->get_notify_type)
			profile->get_notify_type(log, &notify_type);

		hdr = (struct cper_section_hdr *)(buffer + (i * rec_len));
		descriptor = (struct cper_section_desc *)((uint8_t *)hdr + RAS_HDR_LEN);
		section = (uint8_t *)hdr + RAS_HDR_LEN + RAS_SEC_DESC_LEN;

		fill_section_hdr(ras_core, hdr, notify_type, sev, log);
		hdr->sec_cnt = 1;
		hdr->record_length = rec_len;

		fill_section_descriptor(ras_core, descriptor, sev, sec_type,
			RAS_HDR_LEN + RAS_SEC_DESC_LEN, profile->section_size);

		profile->fill_section(ras_core, section, log, sev);
	}

	return 0;
}

static int cper_generate_boot_processor_records(struct ras_core_context *ras_core,
		u8 *buffer, struct ras_log_info *batch_logs, u32 nr_batch_logs)
{
	u32 offset = 0;
	u32 i;

	for (i = 0; i < nr_batch_logs; i++) {
		const struct ras_boot_err_ctx *ctx = &batch_logs[i].body.boot_err_ctx;
		struct ras_cper_processor_record record = { 0 };
		u16 data_size = min_t(u16, ctx->reg_arr_size,
					 sizeof(record.processor.context.reg_dump));
		u32 section_size = offsetof(struct cper_section_processor,
					    context.reg_dump) + data_size;
		u32 record_size = RAS_HDR_LEN + RAS_SEC_DESC_LEN + section_size;
		enum ras_cper_severity sev = ctx->error_severity;

		fill_section_hdr(ras_core, &record.hdr, BOOT__TYPE, sev,
				 &batch_logs[i]);
		record.hdr.record_length = record_size;
		record.hdr.sec_cnt = 1;
		fill_section_descriptor(ras_core, &record.descriptor, sev,
				PROC_ERR__SECTION_TYPE,
				offsetof(struct ras_cper_processor_record, processor),
				section_size);
		fill_section_boot_processor(&record.processor, &batch_logs[i]);
		memcpy(buffer + offset, &record, record_size);
		offset += record_size;
	}

	return offset;
}

static int cper_generate_runtime_processor_records(struct ras_core_context *ras_core,
		u8 *buffer, struct ras_log_info *batch_logs, u32 nr_batch_logs)
{
	u32 i;

	for (i = 0; i < nr_batch_logs; i++) {
		struct ras_cper_processor_record record = { 0 };
		u64 status = batch_logs[i].body.cpu_mce.regs[ACA_REG_IDX__STATUS];
		enum ras_cper_severity sev;

		if (!(status & CPER_MCA_STATUS_UNCORRECTED) &&
		    !(status & CPER_MCA_STATUS_DEFERRED))
			sev = RAS_CPER_SEV_NON_FATAL_CE;
		else if ((status & CPER_MCA_STATUS_UNCORRECTED) &&
			 (status & CPER_MCA_STATUS_PCC))
			sev = RAS_CPER_SEV_FATAL_UE;
		else
			sev = RAS_CPER_SEV_NON_FATAL_UE;

		fill_section_hdr(ras_core, &record.hdr, CPER_NOTIFY__MCE, sev,
				 &batch_logs[i]);
		record.hdr.revision = CPER_HDR__REV_AMD_CPU;
		record.hdr.record_length = sizeof(record);
		record.hdr.sec_cnt = 1;
		fill_section_descriptor(ras_core, &record.descriptor, sev,
				PROC_ERR__SECTION_TYPE,
				offsetof(struct ras_cper_processor_record, processor),
				RAS_PROC_SEC_LEN);
		record.descriptor.flag_bits.latent_err =
			!!(status & CPER_MCA_STATUS_DEFERRED);
		record.descriptor.revision_minor = CPER_SEC__REV_AMD_CPU & 0xff;
		record.descriptor.revision_major = CPER_SEC__REV_AMD_CPU >> 8;
		fill_section_processor(&record.processor, &batch_logs[i]);
		memcpy(buffer + (i * sizeof(record)), &record, sizeof(record));
	}

	return 0;
}

static bool cper_is_processor_record(struct ras_log_info *log)
{
	return log->event == RAS_LOG_EVENT_CPU_RAS ||
		(log->event == RAS_LOG_EVENT_BOOT &&
		 !memcmp(log->body.boot_err_ctx.section_type,
			 processor_section_type.b, CPER_UUID_MAX_SIZE));
}

static int cper_generate_processor_records(struct ras_core_context *ras_core,
		struct ras_log_info *batch_logs, u32 nr_batch_logs,
		u8 *buffer, u32 buf_len, u32 *real_data_len)
{
	u32 record_size = 0;
	u32 i;
	int ret;

	if (batch_logs[0].event == RAS_LOG_EVENT_CPU_RAS) {
		if (nr_batch_logs > U32_MAX / sizeof(struct ras_cper_processor_record))
			return -EOVERFLOW;
		record_size = sizeof(struct ras_cper_processor_record) * nr_batch_logs;
		if (record_size > buf_len)
			return -ENOMEM;

		ret = cper_generate_runtime_processor_records(ras_core, buffer,
				batch_logs, nr_batch_logs);
		if (ret)
			return ret;
		*real_data_len = record_size;
		return 0;
	}

	for (i = 0; i < nr_batch_logs; i++) {
		const struct ras_boot_err_ctx *ctx = &batch_logs[i].body.boot_err_ctx;
		u16 data_size;
		u32 size;

		if (batch_logs[i].event != RAS_LOG_EVENT_BOOT ||
		    !cper_is_processor_record(&batch_logs[i]))
			return -EINVAL;

		data_size = min_t(u16, ctx->reg_arr_size,
				  sizeof(struct cper_processor_context) -
				  offsetof(struct cper_processor_context, reg_dump));
		size = RAS_HDR_LEN + RAS_SEC_DESC_LEN +
			offsetof(struct cper_section_processor, context.reg_dump) +
			data_size;
		if (record_size > U32_MAX - size)
			return -EOVERFLOW;
		record_size += size;
	}
	if (record_size > buf_len)
		return -ENOMEM;

	ret = cper_generate_boot_processor_records(ras_core, buffer,
			batch_logs, nr_batch_logs);
	if (ret < 0)
		return ret;
	*real_data_len = ret;

	return 0;
}

static bool cper_is_platform_memory_record(struct ras_log_info *log)
{
	return log->event == RAS_LOG_EVENT_BOOT &&
		!memcmp(log->body.boot_err_ctx.section_type,
			platform_memory_section_type.b, CPER_UUID_MAX_SIZE);
}

static int cper_generate_platform_memory_records(struct ras_core_context *ras_core,
		struct ras_log_info *batch_logs, u32 nr_batch_logs,
		u8 *buffer, u32 buf_len, u32 *real_data_len)
{
	u32 record_size = 0;
	u32 offset = 0;
	u32 i;

	for (i = 0; i < nr_batch_logs; i++) {
		const struct ras_boot_err_ctx *ctx = &batch_logs[i].body.boot_err_ctx;
		u32 size;

		if (!cper_is_platform_memory_record(&batch_logs[i]) ||
		    !ctx->raw_data_size ||
		    ctx->raw_data_size > sizeof(ctx->raw_data))
			return -EINVAL;

		size = ALIGN(RAS_HDR_LEN + RAS_SEC_DESC_LEN +
			     ctx->raw_data_size, sizeof(u32));
		if (record_size > U32_MAX - size)
			return -EOVERFLOW;
		record_size += size;
	}
	if (record_size > buf_len)
		return -ENOMEM;

	for (i = 0; i < nr_batch_logs; i++) {
		const struct ras_boot_err_ctx *ctx = &batch_logs[i].body.boot_err_ctx;
		struct cper_section_desc *descriptor;
		struct cper_section_hdr *hdr;
		u32 size = ALIGN(RAS_HDR_LEN + RAS_SEC_DESC_LEN +
				 ctx->raw_data_size, sizeof(u32));

		hdr = (struct cper_section_hdr *)(buffer + offset);
		descriptor = (struct cper_section_desc *)(buffer + offset +
						  RAS_HDR_LEN);
		fill_section_hdr(ras_core, hdr, BOOT__TYPE,
				 ctx->error_severity, &batch_logs[i]);
		hdr->record_length = size;
		hdr->sec_cnt = 1;
		fill_section_descriptor(ras_core, descriptor, ctx->error_severity,
				PLATFORM_MEM__SECTION_TYPE,
				RAS_HDR_LEN + RAS_SEC_DESC_LEN,
				ctx->raw_data_size);
		memcpy(buffer + offset + RAS_HDR_LEN + RAS_SEC_DESC_LEN,
		       ctx->raw_data, ctx->raw_data_size);
		memset(buffer + offset + RAS_HDR_LEN + RAS_SEC_DESC_LEN +
		       ctx->raw_data_size, 0,
		       size - RAS_HDR_LEN - RAS_SEC_DESC_LEN -
		       ctx->raw_data_size);
		offset += size;
	}

	*real_data_len = offset;
	return 0;
}

static enum ras_log_event cper_mce_parse_err_type(struct ras_core_context *ras_core,
						  struct aca_bank_reg *bank)
{
	struct aca_bank_ecc bank_err = {0};

	if (ras_aca_parse_bank(ras_core, bank, &bank_err))
		return RAS_LOG_EVENT_NONE;

	if (bank_err.ue_count)
		return RAS_LOG_EVENT_UE;

	if (bank_err.ce_count)
		return RAS_LOG_EVENT_CE;

	if (bank_err.de_count)
		return RAS_LOG_EVENT_DE;

	return RAS_LOG_EVENT_NONE;
}

int ras_cper_generate_batch_cper(struct ras_core_context *ras_core,
		struct ras_log_info *batch_logs, uint32_t nr_batch_logs,
		uint8_t *buf, uint32_t buf_len, uint32_t *real_data_len)
{
	struct ras_cper *cper = &ras_core->ras_cper;
	struct ras_cper_profile *profile;
	enum ras_log_event event;
	uint32_t record_size;
	int ret = 0;

	if (!batch_logs || !nr_batch_logs || !buf || !buf_len || !real_data_len)
		return -EINVAL;

	*real_data_len = 0;
	if (cper_is_processor_record(&batch_logs[0]))
		return cper_generate_processor_records(ras_core, batch_logs,
			nr_batch_logs, buf, buf_len, real_data_len);
	if (cper_is_platform_memory_record(&batch_logs[0]))
		return cper_generate_platform_memory_records(ras_core, batch_logs,
			nr_batch_logs, buf, buf_len, real_data_len);

	event = batch_logs[0].event;
	if (event == RAS_LOG_EVENT_MCE) {
		struct aca_bank_reg bank = { 0 };

		/* MCE is encoded as 1 record each */
		memcpy(&bank.regs, &batch_logs[0].body.aca_reg.regs, sizeof(bank.regs));
		bank.ecc_type = RAS_ERR_TYPE__MCE;
		event = cper_mce_parse_err_type(ras_core, &bank);
		batch_logs[0].event = event;
	}

	if (event == RAS_LOG_EVENT_BOOT) {
		uint32_t i;

		for (i = 1; i < nr_batch_logs; i++) {
			if (batch_logs[i].event != RAS_LOG_EVENT_BOOT ||
			    memcmp(batch_logs[i].body.boot_err_ctx.section_type,
				   batch_logs[0].body.boot_err_ctx.section_type,
				   CPER_UUID_MAX_SIZE))
				return -EINVAL;
		}
	}

	mutex_lock(&cper->profile_mutex);
	/* All the batch logs share the same event */
	ret = ras_cper_get_profile(ras_core, event, &profile);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev,
			"Unprocessed ras log event: %d, ret:%d\n", event, ret);
		goto out;
	}

	record_size = cper_get_record_size(profile, nr_batch_logs);
	if (record_size > buf_len) {
		ret = -ENOMEM;
		goto out;
	}

	memset(buf, 0, record_size);

	if (profile->build_record_per_log)
		cper_build_multiple_records(ras_core, buf, profile, batch_logs, nr_batch_logs);
	else
		cper_build_single_record(ras_core,
			(struct cper_section_hdr *)buf, profile, batch_logs, nr_batch_logs);

	*real_data_len = record_size;

out:
	mutex_unlock(&cper->profile_mutex);
	return ret;
}

static int ras_cper_get_profile(struct ras_core_context *ras_core,
		enum ras_log_event event, struct ras_cper_profile **profile)
{
	struct ras_cper *cper = &ras_core->ras_cper;

	if (!profile || event < 0 || event >= cper->nr_profiles)
		return -EINVAL;

	if (!cper->profiles)
		return -EPERM;

	if (!cper->profiles[event].fill_section)
		return -ENOENT;

	*profile = &cper->profiles[event];

	return 0;
}

static struct ras_cper_profile ras_ue_profile = {
	.cper_type        = RAS_CPER_TYPE_FATAL,
	.severity         = RAS_CPER_SEV_FATAL_UE,
	.notify_type      = CPER_NOTIFY__MCE,
	.sec_type         = GPU__CRASHDUMP,
	.section_size     = sizeof(struct cper_section_fatal),
	.build_record_per_log = true,
	.fill_section     = fill_section_fatal,
};

static struct ras_cper_profile ras_de_profile = {
	.cper_type        = RAS_CPER_TYPE_RUNTIME,
	.severity         = RAS_CPER_SEV_NON_FATAL_UE,
	.notify_type      = CPER_NOTIFY__MCE,
	.sec_type         = GPU__NONSTANDARD_ERROR,
	.section_size     = sizeof(struct cper_section_runtime),
	.build_record_per_log = false,
	.fill_section     = fill_section_runtime,
};

static struct ras_cper_profile ras_ce_profile = {
	.cper_type        = RAS_CPER_TYPE_RUNTIME,
	.severity         = RAS_CPER_SEV_NON_FATAL_CE,
	.notify_type      = CPER_NOTIFY__CMC,
	.sec_type         = GPU__NONSTANDARD_ERROR,
	.section_size     = sizeof(struct cper_section_runtime),
	.build_record_per_log = false,
	.fill_section     = fill_section_runtime,
};

static struct ras_cper_profile ras_rma_profile = {
	.cper_type        = RAS_CPER_TYPE_RUNTIME,
	.severity         = RAS_CPER_SEV_RMA,
	.notify_type      = CPER_NOTIFY__MCE,
	.sec_type         = GPU__NONSTANDARD_ERROR,
	.section_size     = sizeof(struct cper_section_runtime),
	.build_record_per_log = false,
	.fill_section     = fill_section_runtime,
};

static struct ras_cper_profile ras_boot_profile = {
	.cper_type        = RAS_CPER_TYPE_BOOT,
	.severity         = RAS_CPER_SEV_FATAL_UE,
	.notify_type      = BOOT__TYPE,
	.sec_type         = GPU__CRASHDUMP,
	.section_size     = sizeof(struct cper_section_boot),
	.build_record_per_log = true,
	.get_severity     = cper_boot_get_severity,
	.get_sec_type     = cper_boot_get_sec_type,
	.fill_section     = fill_section_boot,
};

static struct ras_event_profile_map ras_event_profile_maps[] = {
	{RAS_LOG_EVENT_UE,   &ras_ue_profile},
	{RAS_LOG_EVENT_DE,   &ras_de_profile},
	{RAS_LOG_EVENT_CE,   &ras_ce_profile},
	{RAS_LOG_EVENT_RMA,  &ras_rma_profile},
	{RAS_LOG_EVENT_BOOT, &ras_boot_profile},
};

int ras_cper_sw_init(struct ras_core_context *ras_core)
{
	struct ras_cper *cper = &ras_core->ras_cper;
	struct ras_cper_profile *profiles;
	uint32_t nr_profiles = RAS_LOG_EVENT_COUNT_MAX;
	int i, ret = 0;

	profiles = kcalloc(nr_profiles, sizeof(*profiles), GFP_KERNEL);
	if (!profiles)
		return -ENOMEM;

	cper->profiles = profiles;
	cper->nr_profiles = nr_profiles;
	mutex_init(&cper->profile_mutex);

	for (i = 0; i < ARRAY_SIZE(ras_event_profile_maps); i++) {
		ret = ras_cper_register_profile(ras_core,
				ras_event_profile_maps[i].event,
				ras_event_profile_maps[i].profile);
		if (ret) {
			RAS_DEV_ERR(ras_core->dev,
				"Failed to register %u profile. ret:%d\n",
				ras_event_profile_maps[i].event, ret);
			goto out;
		}
	}

	return 0;

out:
	ras_cper_sw_fini(ras_core);
	return ret;
}

int ras_cper_sw_fini(struct ras_core_context *ras_core)
{
	struct ras_cper *cper = &ras_core->ras_cper;

	if (!cper->profiles)
		return 0;

	kfree(cper->profiles);
	cper->profiles = NULL;
	cper->nr_profiles = 0;
	mutex_destroy(&cper->profile_mutex);

	return 0;
}

int ras_cper_register_profile(struct ras_core_context *ras_core,
	enum ras_log_event event, struct ras_cper_profile *profile)
{
	struct ras_cper *cper = &ras_core->ras_cper;
	int ret = 0;

	if (!profile || event >= cper->nr_profiles)
		return -EINVAL;

	if (!cper->profiles)
		return -EPERM;

	mutex_lock(&cper->profile_mutex);
	if (cper->profiles[event].fill_section) {
		ret = -EEXIST;
		goto out;
	}

	memcpy(&cper->profiles[event], profile, sizeof(struct ras_cper_profile));

out:
	mutex_unlock(&cper->profile_mutex);
	return ret;
}

int ras_cper_unregister_profile(struct ras_core_context *ras_core,
	enum ras_log_event event)
{
	struct ras_cper *cper = &ras_core->ras_cper;
	int ret = 0;

	if (event >= cper->nr_profiles)
		return -EINVAL;

	if (!cper->profiles)
		return -EPERM;

	mutex_lock(&cper->profile_mutex);
	if (!cper->profiles[event].fill_section) {
		ret = 0;
		goto out;
	}

	memset(&cper->profiles[event], 0, sizeof(struct ras_cper_profile));

out:
	mutex_unlock(&cper->profile_mutex);
	return ret;
}
