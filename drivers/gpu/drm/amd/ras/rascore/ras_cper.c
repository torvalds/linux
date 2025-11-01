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
#include "ras_core_status.h"
#include "ras_log_ring.h"
#include "ras_cper.h"

static const struct ras_cper_guid MCE	= CPER_NOTIFY__MCE;
static const struct ras_cper_guid CMC	= CPER_NOTIFY__CMC;
static const struct ras_cper_guid BOOT	= BOOT__TYPE;

static const struct ras_cper_guid CRASHDUMP = GPU__CRASHDUMP;
static const struct ras_cper_guid RUNTIME = GPU__NONSTANDARD_ERROR;

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
				struct cper_section_hdr *hdr, enum ras_cper_type type,
				enum ras_cper_severity sev, struct ras_log_info *trace)
{
	struct device_system_info dev_info = {0};
	char record_id[16];

	hdr->signature[0]		= 'C';
	hdr->signature[1]		= 'P';
	hdr->signature[2]		= 'E';
	hdr->signature[3]		= 'R';
	hdr->revision			= CPER_HDR__REV_1;
	hdr->signature_end		= 0xFFFFFFFF;
	hdr->error_severity		= sev;

	hdr->valid_bits.platform_id	= 1;
	hdr->valid_bits.partition_id	= 1;
	hdr->valid_bits.timestamp	= 1;

	ras_core_get_device_system_info(ras_core, &dev_info);

	cper_get_timestamp(ras_core, &hdr->timestamp, trace->timestamp);

	snprintf(record_id, 9, "%d:%llX", dev_info.socket_id,
		    RAS_LOG_SEQNO_TO_BATCH_IDX(trace->seqno));
	memcpy(hdr->record_id, record_id, 8);

	snprintf(hdr->platform_id, 16, "0x%04X:0x%04X",
		dev_info.vendor_id, dev_info.device_id);
	/* pmfw version should be part of creator_id according to CPER spec */
	snprintf(hdr->creator_id, 16, "%s", CPER_CREATOR_ID__AMDGPU);

	switch (type) {
	case RAS_CPER_TYPE_BOOT:
		hdr->notify_type = BOOT;
		break;
	case RAS_CPER_TYPE_FATAL:
	case RAS_CPER_TYPE_RMA:
		hdr->notify_type = MCE;
		break;
	case RAS_CPER_TYPE_RUNTIME:
		if (sev == RAS_CPER_SEV_NON_FATAL_CE)
			hdr->notify_type = CMC;
		else
			hdr->notify_type = MCE;
		break;
	default:
		RAS_DEV_ERR(ras_core->dev, "Unknown CPER Type\n");
		break;
	}
}

static int fill_section_descriptor(struct ras_core_context *ras_core,
					struct cper_section_descriptor *descriptor,
					enum ras_cper_severity sev,
					struct ras_cper_guid sec_type,
					uint32_t section_offset,
					uint32_t section_length)
{
	struct device_system_info dev_info = {0};

	descriptor->revision_minor		= CPER_SEC__MINOR_REV_1;
	descriptor->revision_major		= CPER_SEC__MAJOR_REV_22;
	descriptor->sec_offset		= section_offset;
	descriptor->sec_length		= section_length;
	descriptor->valid_bits.fru_text	= 1;
	descriptor->flag_bits.primary	= 1;
	descriptor->severity			= sev;
	descriptor->sec_type			= sec_type;

	ras_core_get_device_system_info(ras_core, &dev_info);

	snprintf(descriptor->fru_text, 20, "OAM%d", dev_info.socket_id);

	if (sev == RAS_CPER_SEV_RMA)
		descriptor->flag_bits.exceed_err_threshold = 1;

	if (sev == RAS_CPER_SEV_NON_FATAL_UE)
		descriptor->flag_bits.latent_err = 1;

	return 0;
}

static int fill_section_fatal(struct ras_core_context *ras_core,
		struct cper_section_fatal *fatal, struct ras_log_info *trace)
{
	fatal->data.reg_ctx_type = CPER_CTX_TYPE__CRASH;
	fatal->data.reg_arr_size = sizeof(fatal->data.reg);

	fatal->data.reg.status = trace->aca_reg.regs[RAS_CPER_ACA_REG_STATUS];
	fatal->data.reg.addr   = trace->aca_reg.regs[RAS_CPER_ACA_REG_ADDR];
	fatal->data.reg.ipid   = trace->aca_reg.regs[RAS_CPER_ACA_REG_IPID];
	fatal->data.reg.synd   = trace->aca_reg.regs[RAS_CPER_ACA_REG_SYND];

	return 0;
}

static int fill_section_runtime(struct ras_core_context *ras_core,
		struct cper_section_runtime *runtime, struct ras_log_info *trace)
{
	runtime->hdr.valid_bits.err_info_cnt = 1;
	runtime->hdr.valid_bits.err_context_cnt = 1;

	runtime->descriptor.error_type = RUNTIME;
	runtime->descriptor.ms_chk_bits.err_type_valid = 1;

	runtime->reg.reg_ctx_type = CPER_CTX_TYPE__CRASH;
	runtime->reg.reg_arr_size = sizeof(runtime->reg.reg_dump);

	runtime->reg.reg_dump[RAS_CPER_ACA_REG_CTL]    = trace->aca_reg.regs[ACA_REG_IDX__CTL];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_STATUS] = trace->aca_reg.regs[ACA_REG_IDX__STATUS];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_ADDR]   = trace->aca_reg.regs[ACA_REG_IDX__ADDR];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_MISC0]  = trace->aca_reg.regs[ACA_REG_IDX__MISC0];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_CONFIG] = trace->aca_reg.regs[ACA_REG_IDX__CONFG];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_IPID]   = trace->aca_reg.regs[ACA_REG_IDX__IPID];
	runtime->reg.reg_dump[RAS_CPER_ACA_REG_SYND]   = trace->aca_reg.regs[ACA_REG_IDX__SYND];

	return 0;
}

static int cper_generate_runtime_record(struct ras_core_context *ras_core,
	struct cper_section_hdr *hdr, struct ras_log_info **trace_arr, uint32_t arr_num,
		enum ras_cper_severity sev)
{
	struct cper_section_descriptor *descriptor;
	struct cper_section_runtime *runtime;
	int i;

	fill_section_hdr(ras_core, hdr, RAS_CPER_TYPE_RUNTIME, sev, trace_arr[0]);
	hdr->record_length =  RAS_HDR_LEN + ((RAS_SEC_DESC_LEN + RAS_NONSTD_SEC_LEN) * arr_num);
	hdr->sec_cnt = arr_num;
	for (i = 0; i < arr_num; i++) {
		descriptor = (struct cper_section_descriptor *)((uint8_t *)hdr +
			     RAS_SEC_DESC_OFFSET(i));
		runtime = (struct cper_section_runtime *)((uint8_t *)hdr +
			  RAS_NONSTD_SEC_OFFSET(hdr->sec_cnt, i));

		fill_section_descriptor(ras_core, descriptor, sev, RUNTIME,
			RAS_NONSTD_SEC_OFFSET(hdr->sec_cnt, i),
			sizeof(struct cper_section_runtime));
		fill_section_runtime(ras_core, runtime, trace_arr[i]);
	}

	return 0;
}

static int cper_generate_fatal_record(struct ras_core_context *ras_core,
	uint8_t *buffer, struct ras_log_info **trace_arr, uint32_t arr_num)
{
	struct ras_cper_fatal_record record = {0};
	int i = 0;

	for (i = 0; i < arr_num; i++) {
		fill_section_hdr(ras_core, &record.hdr, RAS_CPER_TYPE_FATAL,
				 RAS_CPER_SEV_FATAL_UE, trace_arr[i]);
		record.hdr.record_length =  RAS_HDR_LEN + RAS_SEC_DESC_LEN + RAS_FATAL_SEC_LEN;
		record.hdr.sec_cnt = 1;

		fill_section_descriptor(ras_core, &record.descriptor, RAS_CPER_SEV_FATAL_UE,
					CRASHDUMP, offsetof(struct ras_cper_fatal_record, fatal),
					sizeof(struct cper_section_fatal));

		fill_section_fatal(ras_core, &record.fatal, trace_arr[i]);

		memcpy(buffer + (i * record.hdr.record_length),
				&record, record.hdr.record_length);
	}

	return 0;
}

static int cper_get_record_size(enum ras_cper_type type, uint16_t section_count)
{
	int size = 0;

	size += RAS_HDR_LEN;
	size += (RAS_SEC_DESC_LEN * section_count);

	switch (type) {
	case RAS_CPER_TYPE_RUNTIME:
	case RAS_CPER_TYPE_RMA:
		size += (RAS_NONSTD_SEC_LEN * section_count);
		break;
	case RAS_CPER_TYPE_FATAL:
		size += (RAS_FATAL_SEC_LEN * section_count);
		size += (RAS_HDR_LEN * (section_count - 1));
		break;
	case RAS_CPER_TYPE_BOOT:
		size += (RAS_BOOT_SEC_LEN * section_count);
		break;
	default:
		/* should never reach here */
		break;
	}

	return size;
}

static enum ras_cper_type cper_ras_log_event_to_cper_type(enum ras_log_event event)
{
	switch (event) {
	case RAS_LOG_EVENT_UE:
		return RAS_CPER_TYPE_FATAL;
	case RAS_LOG_EVENT_DE:
	case RAS_LOG_EVENT_CE:
	case RAS_LOG_EVENT_POISON_CREATION:
	case RAS_LOG_EVENT_POISON_CONSUMPTION:
		return RAS_CPER_TYPE_RUNTIME;
	case RAS_LOG_EVENT_RMA:
		return RAS_CPER_TYPE_RMA;
	default:
		/* should never reach here */
		return RAS_CPER_TYPE_RUNTIME;
	}
}

int ras_cper_generate_cper(struct ras_core_context *ras_core,
		struct ras_log_info **trace_list, uint32_t count,
		uint8_t *buf, uint32_t buf_len, uint32_t *real_data_len)
{
	uint8_t *buffer = buf;
	uint64_t buf_size = buf_len;
	int record_size, saved_size = 0;
	struct cper_section_hdr *hdr;

	/* All the batch traces share the same event */
	record_size = cper_get_record_size(
			cper_ras_log_event_to_cper_type(trace_list[0]->event), count);

	if ((record_size + saved_size) > buf_size)
		return -ENOMEM;

	hdr = (struct cper_section_hdr *)(buffer + saved_size);

	switch (trace_list[0]->event) {
	case RAS_LOG_EVENT_RMA:
		cper_generate_runtime_record(ras_core, hdr, trace_list, count, RAS_CPER_SEV_RMA);
		break;
	case RAS_LOG_EVENT_DE:
		cper_generate_runtime_record(ras_core,
			hdr, trace_list, count, RAS_CPER_SEV_NON_FATAL_UE);
		break;
	case RAS_LOG_EVENT_CE:
		cper_generate_runtime_record(ras_core,
			hdr, trace_list, count, RAS_CPER_SEV_NON_FATAL_CE);
		break;
	case RAS_LOG_EVENT_UE:
		cper_generate_fatal_record(ras_core, buffer + saved_size, trace_list, count);
		break;
	default:
		RAS_DEV_WARN(ras_core->dev, "Unprocessed trace event: %d\n", trace_list[0]->event);
		break;
	}

	saved_size += record_size;

	*real_data_len = saved_size;
	return 0;
}
