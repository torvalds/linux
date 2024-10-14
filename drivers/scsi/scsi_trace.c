// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 FUJITSU LIMITED
 * Copyright (C) 2010 Tomohiro Kusumi <kusumi.tomohiro@jp.fujitsu.com>
 */
#include <linux/kernel.h>
#include <linux/trace_seq.h>
#include <linux/unaligned.h>
#include <trace/events/scsi.h>

#define SERVICE_ACTION16(cdb) (cdb[1] & 0x1f)
#define SERVICE_ACTION32(cdb) (get_unaligned_be16(&cdb[8]))

static const char *
scsi_trace_misc(struct trace_seq *, unsigned char *, int);

static const char *
scsi_trace_rw6(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u32 lba, txlen;

	lba = get_unaligned_be24(&cdb[1]) & 0x1fffff;
	/*
	 * From SBC-2: a TRANSFER LENGTH field set to zero specifies that 256
	 * logical blocks shall be read (READ(6)) or written (WRITE(6)).
	 */
	txlen = cdb[4] ? cdb[4] : 256;

	trace_seq_printf(p, "lba=%u txlen=%u", lba, txlen);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw10(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u32 lba, txlen;

	lba = get_unaligned_be32(&cdb[2]);
	txlen = get_unaligned_be16(&cdb[7]);

	trace_seq_printf(p, "lba=%u txlen=%u protect=%u", lba, txlen,
			 cdb[1] >> 5);

	if (cdb[0] == WRITE_SAME)
		trace_seq_printf(p, " unmap=%u", cdb[1] >> 3 & 1);

	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw12(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u32 lba, txlen;

	lba = get_unaligned_be32(&cdb[2]);
	txlen = get_unaligned_be32(&cdb[6]);

	trace_seq_printf(p, "lba=%u txlen=%u protect=%u", lba, txlen,
			 cdb[1] >> 5);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw16(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u64 lba;
	u32 txlen;

	lba = get_unaligned_be64(&cdb[2]);
	txlen = get_unaligned_be32(&cdb[10]);

	trace_seq_printf(p, "lba=%llu txlen=%u protect=%u", lba, txlen,
			 cdb[1] >> 5);

	if (cdb[0] == WRITE_SAME_16)
		trace_seq_printf(p, " unmap=%u", cdb[1] >> 3 & 1);

	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw32(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u64 lba;
	u32 ei_lbrt, txlen;

	switch (SERVICE_ACTION32(cdb)) {
	case READ_32:
		cmd = "READ";
		break;
	case VERIFY_32:
		cmd = "VERIFY";
		break;
	case WRITE_32:
		cmd = "WRITE";
		break;
	case WRITE_SAME_32:
		cmd = "WRITE_SAME";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	lba = get_unaligned_be64(&cdb[12]);
	ei_lbrt = get_unaligned_be32(&cdb[20]);
	txlen = get_unaligned_be32(&cdb[28]);

	trace_seq_printf(p, "%s_32 lba=%llu txlen=%u protect=%u ei_lbrt=%u",
			 cmd, lba, txlen, cdb[10] >> 5, ei_lbrt);

	if (SERVICE_ACTION32(cdb) == WRITE_SAME_32)
		trace_seq_printf(p, " unmap=%u", cdb[10] >> 3 & 1);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_unmap(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	unsigned int regions = get_unaligned_be16(&cdb[7]);

	trace_seq_printf(p, "regions=%u", (regions - 8) / 16);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_service_action_in(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u64 lba;
	u32 alloc_len;

	switch (SERVICE_ACTION16(cdb)) {
	case SAI_READ_CAPACITY_16:
		cmd = "READ_CAPACITY_16";
		break;
	case SAI_GET_LBA_STATUS:
		cmd = "GET_LBA_STATUS";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	lba = get_unaligned_be64(&cdb[2]);
	alloc_len = get_unaligned_be32(&cdb[10]);

	trace_seq_printf(p, "%s lba=%llu alloc_len=%u", cmd, lba, alloc_len);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_maintenance_in(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u32 alloc_len;

	switch (SERVICE_ACTION16(cdb)) {
	case MI_REPORT_IDENTIFYING_INFORMATION:
		cmd = "REPORT_IDENTIFYING_INFORMATION";
		break;
	case MI_REPORT_TARGET_PGS:
		cmd = "REPORT_TARGET_PORT_GROUPS";
		break;
	case MI_REPORT_ALIASES:
		cmd = "REPORT_ALIASES";
		break;
	case MI_REPORT_SUPPORTED_OPERATION_CODES:
		cmd = "REPORT_SUPPORTED_OPERATION_CODES";
		break;
	case MI_REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS:
		cmd = "REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS";
		break;
	case MI_REPORT_PRIORITY:
		cmd = "REPORT_PRIORITY";
		break;
	case MI_REPORT_TIMESTAMP:
		cmd = "REPORT_TIMESTAMP";
		break;
	case MI_MANAGEMENT_PROTOCOL_IN:
		cmd = "MANAGEMENT_PROTOCOL_IN";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	alloc_len = get_unaligned_be32(&cdb[6]);

	trace_seq_printf(p, "%s alloc_len=%u", cmd, alloc_len);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_maintenance_out(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u32 alloc_len;

	switch (SERVICE_ACTION16(cdb)) {
	case MO_SET_IDENTIFYING_INFORMATION:
		cmd = "SET_IDENTIFYING_INFORMATION";
		break;
	case MO_SET_TARGET_PGS:
		cmd = "SET_TARGET_PORT_GROUPS";
		break;
	case MO_CHANGE_ALIASES:
		cmd = "CHANGE_ALIASES";
		break;
	case MO_SET_PRIORITY:
		cmd = "SET_PRIORITY";
		break;
	case MO_SET_TIMESTAMP:
		cmd = "SET_TIMESTAMP";
		break;
	case MO_MANAGEMENT_PROTOCOL_OUT:
		cmd = "MANAGEMENT_PROTOCOL_OUT";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	alloc_len = get_unaligned_be32(&cdb[6]);

	trace_seq_printf(p, "%s alloc_len=%u", cmd, alloc_len);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_zbc_in(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u64 zone_id;
	u32 alloc_len;
	u8 options;

	switch (SERVICE_ACTION16(cdb)) {
	case ZI_REPORT_ZONES:
		cmd = "REPORT_ZONES";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	zone_id = get_unaligned_be64(&cdb[2]);
	alloc_len = get_unaligned_be32(&cdb[10]);
	options = cdb[14] & 0x3f;

	trace_seq_printf(p, "%s zone=%llu alloc_len=%u options=%u partial=%u",
			 cmd, (unsigned long long)zone_id, alloc_len,
			 options, (cdb[14] >> 7) & 1);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_zbc_out(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u64 zone_id;

	switch (SERVICE_ACTION16(cdb)) {
	case ZO_CLOSE_ZONE:
		cmd = "CLOSE_ZONE";
		break;
	case ZO_FINISH_ZONE:
		cmd = "FINISH_ZONE";
		break;
	case ZO_OPEN_ZONE:
		cmd = "OPEN_ZONE";
		break;
	case ZO_RESET_WRITE_POINTER:
		cmd = "RESET_WRITE_POINTER";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	zone_id = get_unaligned_be64(&cdb[2]);

	trace_seq_printf(p, "%s zone=%llu all=%u", cmd,
			 (unsigned long long)zone_id, cdb[14] & 1);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_atomic_write16_out(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	unsigned int boundary_size;
	unsigned int nr_blocks;
	sector_t lba;

	lba = get_unaligned_be64(&cdb[2]);
	boundary_size = get_unaligned_be16(&cdb[10]);
	nr_blocks = get_unaligned_be16(&cdb[12]);

	trace_seq_printf(p, "lba=%llu txlen=%u boundary_size=%u",
			  lba, nr_blocks, boundary_size);

	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_varlen(struct trace_seq *p, unsigned char *cdb, int len)
{
	switch (SERVICE_ACTION32(cdb)) {
	case READ_32:
	case VERIFY_32:
	case WRITE_32:
	case WRITE_SAME_32:
		return scsi_trace_rw32(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}

static const char *
scsi_trace_misc(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_putc(p, '-');
	trace_seq_putc(p, 0);

	return ret;
}

const char *
scsi_trace_parse_cdb(struct trace_seq *p, unsigned char *cdb, int len)
{
	switch (cdb[0]) {
	case READ_6:
	case WRITE_6:
		return scsi_trace_rw6(p, cdb, len);
	case READ_10:
	case VERIFY:
	case WRITE_10:
	case WRITE_SAME:
		return scsi_trace_rw10(p, cdb, len);
	case READ_12:
	case VERIFY_12:
	case WRITE_12:
		return scsi_trace_rw12(p, cdb, len);
	case READ_16:
	case VERIFY_16:
	case WRITE_16:
	case WRITE_SAME_16:
		return scsi_trace_rw16(p, cdb, len);
	case UNMAP:
		return scsi_trace_unmap(p, cdb, len);
	case SERVICE_ACTION_IN_16:
		return scsi_trace_service_action_in(p, cdb, len);
	case VARIABLE_LENGTH_CMD:
		return scsi_trace_varlen(p, cdb, len);
	case MAINTENANCE_IN:
		return scsi_trace_maintenance_in(p, cdb, len);
	case MAINTENANCE_OUT:
		return scsi_trace_maintenance_out(p, cdb, len);
	case ZBC_IN:
		return scsi_trace_zbc_in(p, cdb, len);
	case ZBC_OUT:
		return scsi_trace_zbc_out(p, cdb, len);
	case WRITE_ATOMIC_16:
		return scsi_trace_atomic_write16_out(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}
