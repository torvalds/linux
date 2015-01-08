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
#include <scsi/scsi_dbg.h>

#define SCSI_LOG_SPOOLSIZE 4096
#define SCSI_LOG_BUFSIZE 128

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
	va_start(args, fmt);
	off += vscnprintf(logbuf + off, logbuf_len - off, fmt, args);
	va_end(args);
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
	if (disk)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "[%s] ", disk->disk_name);

	if (scmd->request->tag >= 0)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "tag#%d ", scmd->request->tag);
	va_start(args, fmt);
	off += vscnprintf(logbuf + off, logbuf_len - off, fmt, args);
	va_end(args);
	ret = dev_printk(level, &scmd->device->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);
	return ret;
}
EXPORT_SYMBOL(scmd_printk);
