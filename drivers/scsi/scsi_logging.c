/*
 * scsi_logging.c
 *
 * Copyright (C) 2014 SUSE Linux Products GmbH
 * Copyright (C) 2014 Hannes Reinecke <hare@suse.de>
 *
 * This file is released under the GPLv2
 */

#include <linux/kernel.h>
#include <linux/atomic.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dbg.h>

#define SCSI_LOG_SPOOLSIZE 4096

#if (SCSI_LOG_SPOOLSIZE / SCSI_LOG_BUFSIZE) > BITS_PER_LONG
#warning SCSI logging bitmask too large
#endif

struct scsi_log_buf {
	char buffer[SCSI_LOG_SPOOLSIZE];
	unsigned long map;
};

static DEFINE_PER_CPU(struct scsi_log_buf, scsi_format_log);

static char *scsi_log_reserve_buffer(size_t *len)
{
	struct scsi_log_buf *buf;
	unsigned long map_bits = sizeof(buf->buffer) / SCSI_LOG_BUFSIZE;
	unsigned long idx = 0;

	preempt_disable();
	buf = this_cpu_ptr(&scsi_format_log);
	idx = find_first_zero_bit(&buf->map, map_bits);
	if (likely(idx < map_bits)) {
		while (test_and_set_bit(idx, &buf->map)) {
			idx = find_next_zero_bit(&buf->map, map_bits, idx);
			if (idx >= map_bits)
				break;
		}
	}
	if (WARN_ON(idx >= map_bits)) {
		preempt_enable();
		return NULL;
	}
	*len = SCSI_LOG_BUFSIZE;
	return buf->buffer + idx * SCSI_LOG_BUFSIZE;
}

static void scsi_log_release_buffer(char *bufptr)
{
	struct scsi_log_buf *buf;
	unsigned long idx;
	int ret;

	buf = this_cpu_ptr(&scsi_format_log);
	if (bufptr >= buf->buffer &&
	    bufptr < buf->buffer + SCSI_LOG_SPOOLSIZE) {
		idx = (bufptr - buf->buffer) / SCSI_LOG_BUFSIZE;
		ret = test_and_clear_bit(idx, &buf->map);
		WARN_ON(!ret);
	}
	preempt_enable();
}

static size_t scmd_format_header(char *logbuf, size_t logbuf_len,
				 struct gendisk *disk, int tag)
{
	size_t off = 0;

	if (disk)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "[%s] ", disk->disk_name);

	if (WARN_ON(off >= logbuf_len))
		return off;

	if (tag >= 0)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "tag#%d ", tag);
	return off;
}

int sdev_prefix_printk(const char *level, const struct scsi_device *sdev,
		       const char *name, const char *fmt, ...)
{
	va_list args;
	char *logbuf;
	size_t off = 0, logbuf_len;
	int ret;

	if (!sdev)
		return 0;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return 0;

	if (name)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "[%s] ", name);
	if (!WARN_ON(off >= logbuf_len)) {
		va_start(args, fmt);
		off += vscnprintf(logbuf + off, logbuf_len - off, fmt, args);
		va_end(args);
	}
	ret = dev_printk(level, &sdev->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);
	return ret;
}
EXPORT_SYMBOL(sdev_prefix_printk);

int scmd_printk(const char *level, const struct scsi_cmnd *scmd,
		const char *fmt, ...)
{
	struct gendisk *disk = scmd->request->rq_disk;
	va_list args;
	char *logbuf;
	size_t off = 0, logbuf_len;
	int ret;

	if (!scmd || !scmd->cmnd)
		return 0;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return 0;
	off = scmd_format_header(logbuf, logbuf_len, disk,
				 scmd->request->tag);
	if (off < logbuf_len) {
		va_start(args, fmt);
		off += vscnprintf(logbuf + off, logbuf_len - off, fmt, args);
		va_end(args);
	}
	ret = dev_printk(level, &scmd->device->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);
	return ret;
}
EXPORT_SYMBOL(scmd_printk);

static size_t scsi_format_opcode_name(char *buffer, size_t buf_len,
				      const unsigned char *cdbp)
{
	int sa, cdb0;
	const char *cdb_name = NULL, *sa_name = NULL;
	size_t off;

	cdb0 = cdbp[0];
	if (cdb0 == VARIABLE_LENGTH_CMD) {
		int len = scsi_varlen_cdb_length(cdbp);

		if (len < 10) {
			off = scnprintf(buffer, buf_len,
					"short variable length command, len=%d",
					len);
			return off;
		}
		sa = (cdbp[8] << 8) + cdbp[9];
	} else
		sa = cdbp[1] & 0x1f;

	if (!scsi_opcode_sa_name(cdb0, sa, &cdb_name, &sa_name)) {
		if (cdb_name)
			off = scnprintf(buffer, buf_len, "%s", cdb_name);
		else {
			off = scnprintf(buffer, buf_len, "opcode=0x%x", cdb0);
			if (WARN_ON(off >= buf_len))
				return off;
			if (cdb0 >= VENDOR_SPECIFIC_CDB)
				off += scnprintf(buffer + off, buf_len - off,
						 " (vendor)");
			else if (cdb0 >= 0x60 && cdb0 < 0x7e)
				off += scnprintf(buffer + off, buf_len - off,
						 " (reserved)");
		}
	} else {
		if (sa_name)
			off = scnprintf(buffer, buf_len, "%s", sa_name);
		else if (cdb_name)
			off = scnprintf(buffer, buf_len, "%s, sa=0x%x",
					cdb_name, sa);
		else
			off = scnprintf(buffer, buf_len,
					"opcode=0x%x, sa=0x%x", cdb0, sa);
	}
	WARN_ON(off >= buf_len);
	return off;
}

size_t __scsi_format_command(char *logbuf, size_t logbuf_len,
			     const unsigned char *cdb, size_t cdb_len)
{
	int len, k;
	size_t off;

	off = scsi_format_opcode_name(logbuf, logbuf_len, cdb);
	if (off >= logbuf_len)
		return off;
	len = scsi_command_size(cdb);
	if (cdb_len < len)
		len = cdb_len;
	/* print out all bytes in cdb */
	for (k = 0; k < len; ++k) {
		if (off > logbuf_len - 3)
			break;
		off += scnprintf(logbuf + off, logbuf_len - off,
				 " %02x", cdb[k]);
	}
	return off;
}
EXPORT_SYMBOL(__scsi_format_command);

void scsi_print_command(struct scsi_cmnd *cmd)
{
	struct gendisk *disk = cmd->request->rq_disk;
	int k;
	char *logbuf;
	size_t off, logbuf_len;

	if (!cmd->cmnd)
		return;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;

	off = scmd_format_header(logbuf, logbuf_len, disk, cmd->request->tag);
	if (off >= logbuf_len)
		goto out_printk;
	off += scnprintf(logbuf + off, logbuf_len - off, "CDB: ");
	if (WARN_ON(off >= logbuf_len))
		goto out_printk;

	off += scsi_format_opcode_name(logbuf + off, logbuf_len - off,
				       cmd->cmnd);
	if (off >= logbuf_len)
		goto out_printk;

	/* print out all bytes in cdb */
	if (cmd->cmd_len > 16) {
		/* Print opcode in one line and use separate lines for CDB */
		off += scnprintf(logbuf + off, logbuf_len - off, "\n");
		dev_printk(KERN_INFO, &cmd->device->sdev_gendev, logbuf);
		scsi_log_release_buffer(logbuf);
		for (k = 0; k < cmd->cmd_len; k += 16) {
			size_t linelen = min(cmd->cmd_len - k, 16);

			logbuf = scsi_log_reserve_buffer(&logbuf_len);
			if (!logbuf)
				break;
			off = scmd_format_header(logbuf, logbuf_len, disk,
						 cmd->request->tag);
			if (!WARN_ON(off > logbuf_len - 58)) {
				off += scnprintf(logbuf + off, logbuf_len - off,
						 "CDB[%02x]: ", k);
				hex_dump_to_buffer(&cmd->cmnd[k], linelen,
						   16, 1, logbuf + off,
						   logbuf_len - off, false);
			}
			dev_printk(KERN_INFO, &cmd->device->sdev_gendev,
				   logbuf);
			scsi_log_release_buffer(logbuf);
		}
		return;
	}
	if (!WARN_ON(off > logbuf_len - 49)) {
		off += scnprintf(logbuf + off, logbuf_len - off, " ");
		hex_dump_to_buffer(cmd->cmnd, cmd->cmd_len, 16, 1,
				   logbuf + off, logbuf_len - off,
				   false);
	}
out_printk:
	dev_printk(KERN_INFO, &cmd->device->sdev_gendev, logbuf);
	scsi_log_release_buffer(logbuf);
}
EXPORT_SYMBOL(scsi_print_command);
