/*
 * Copyright (C) 2010 FUJITSU LIMITED
 * Copyright (C) 2010 Tomohiro Kusumi <kusumi.tomohiro@jp.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/trace_seq.h>
#include <trace/events/scsi.h>

#define SERVICE_ACTION(cdb) ((cdb[8] << 8) | cdb[9])

static const char *
scsi_trace_misc(struct trace_seq *, unsigned char *, int);

static const char *
scsi_trace_rw6(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= ((cdb[1] & 0x1F) << 16);
	lba |=  (cdb[2] << 8);
	lba |=   cdb[3];
	txlen = cdb[4];

	trace_seq_printf(p, "lba=%llu txlen=%llu",
			 (unsigned long long)lba, (unsigned long long)txlen);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw10(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= (cdb[2] << 24);
	lba |= (cdb[3] << 16);
	lba |= (cdb[4] << 8);
	lba |=  cdb[5];
	txlen |= (cdb[7] << 8);
	txlen |=  cdb[8];

	trace_seq_printf(p, "lba=%llu txlen=%llu",
			 (unsigned long long)lba, (unsigned long long)txlen);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw12(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= (cdb[2] << 24);
	lba |= (cdb[3] << 16);
	lba |= (cdb[4] << 8);
	lba |=  cdb[5];
	txlen |= (cdb[6] << 24);
	txlen |= (cdb[7] << 16);
	txlen |= (cdb[8] << 8);
	txlen |=  cdb[9];

	trace_seq_printf(p, "lba=%llu txlen=%llu",
			 (unsigned long long)lba, (unsigned long long)txlen);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw16(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= ((u64)cdb[2] << 56);
	lba |= ((u64)cdb[3] << 48);
	lba |= ((u64)cdb[4] << 40);
	lba |= ((u64)cdb[5] << 32);
	lba |= (cdb[6] << 24);
	lba |= (cdb[7] << 16);
	lba |= (cdb[8] << 8);
	lba |=  cdb[9];
	txlen |= (cdb[10] << 24);
	txlen |= (cdb[11] << 16);
	txlen |= (cdb[12] << 8);
	txlen |=  cdb[13];

	trace_seq_printf(p, "lba=%llu txlen=%llu",
			 (unsigned long long)lba, (unsigned long long)txlen);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw32(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= ((u64)cdb[12] << 56);
	lba |= ((u64)cdb[13] << 48);
	lba |= ((u64)cdb[14] << 40);
	lba |= ((u64)cdb[15] << 32);
	lba |= (cdb[16] << 24);
	lba |= (cdb[17] << 16);
	lba |= (cdb[18] << 8);
	lba |=  cdb[19];
	txlen |= (cdb[28] << 24);
	txlen |= (cdb[29] << 16);
	txlen |= (cdb[30] << 8);
	txlen |=  cdb[31];

	trace_seq_printf(p, "%s_32 lba=%llu txlen=%llu",
			(SERVICE_ACTION(cdb) == READ_32 ? "READ" : "WRITE"),
			(unsigned long long)lba, (unsigned long long)txlen);

	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_varlen(struct trace_seq *p, unsigned char *cdb, int len)
{
	switch (SERVICE_ACTION(cdb)) {
	case READ_32:
	case WRITE_32:
		return scsi_trace_rw32(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}

static const char *
scsi_trace_misc(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;

	trace_seq_printf(p, "-");
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
	case WRITE_10:
		return scsi_trace_rw10(p, cdb, len);
	case READ_12:
	case WRITE_12:
		return scsi_trace_rw12(p, cdb, len);
	case READ_16:
	case WRITE_16:
		return scsi_trace_rw16(p, cdb, len);
	case VARIABLE_LENGTH_CMD:
		return scsi_trace_varlen(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}
