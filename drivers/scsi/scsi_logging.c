// SPDX-License-Identifier: GPL-2.0-only
/*
 * scsi_logging.c
 *
 * Copyright (C) 2014 SUSE Linux Products GmbH
 * Copyright (C) 2014 Hannes Reinecke <hare@suse.de>
 */

#include <linux/kernel.h>
#include <linux/atomic.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dbg.h>

static char *scsi_log_reserve_buffer(size_t *len)
{
	*len = 128;
	return kmalloc(*len, GFP_ATOMIC);
}

static void scsi_log_release_buffer(char *bufptr)
{
	kfree(bufptr);
}

static inline const char *scmd_name(const struct scsi_cmnd *scmd)
{
	struct request *rq = scsi_cmd_to_rq((struct scsi_cmnd *)scmd);

	if (!rq->q->disk)
		return NULL;
	return rq->q->disk->disk_name;
}

static size_t sdev_format_header(char *logbuf, size_t logbuf_len,
				 const char *name, int tag)
{
	size_t off = 0;

	if (name)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "[%s] ", name);

	if (WARN_ON(off >= logbuf_len))
		return off;

	if (tag >= 0)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "tag#%d ", tag);
	return off;
}

void sdev_prefix_printk(const char *level, const struct scsi_device *sdev,
			const char *name, const char *fmt, ...)
{
	va_list args;
	char *logbuf;
	size_t off = 0, logbuf_len;

	if (!sdev)
		return;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;

	if (name)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "[%s] ", name);
	if (!WARN_ON(off >= logbuf_len)) {
		va_start(args, fmt);
		off += vscnprintf(logbuf + off, logbuf_len - off, fmt, args);
		va_end(args);
	}
	dev_printk(level, &sdev->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);
}
EXPORT_SYMBOL(sdev_prefix_printk);

void scmd_printk(const char *level, const struct scsi_cmnd *scmd,
		const char *fmt, ...)
{
	va_list args;
	char *logbuf;
	size_t off = 0, logbuf_len;

	if (!scmd || !scmd->cmnd)
		return;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;
	off = sdev_format_header(logbuf, logbuf_len, scmd_name(scmd),
				 scsi_cmd_to_rq((struct scsi_cmnd *)scmd)->tag);
	if (off < logbuf_len) {
		va_start(args, fmt);
		off += vscnprintf(logbuf + off, logbuf_len - off, fmt, args);
		va_end(args);
	}
	dev_printk(level, &scmd->device->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);
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
	int k;
	char *logbuf;
	size_t off, logbuf_len;

	if (!cmd->cmnd)
		return;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;

	off = sdev_format_header(logbuf, logbuf_len,
				 scmd_name(cmd), scsi_cmd_to_rq(cmd)->tag);
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
		dev_printk(KERN_INFO, &cmd->device->sdev_gendev, "%s", logbuf);
		for (k = 0; k < cmd->cmd_len; k += 16) {
			size_t linelen = min(cmd->cmd_len - k, 16);

			off = sdev_format_header(logbuf, logbuf_len,
						 scmd_name(cmd),
						 scsi_cmd_to_rq(cmd)->tag);
			if (!WARN_ON(off > logbuf_len - 58)) {
				off += scnprintf(logbuf + off, logbuf_len - off,
						 "CDB[%02x]: ", k);
				hex_dump_to_buffer(&cmd->cmnd[k], linelen,
						   16, 1, logbuf + off,
						   logbuf_len - off, false);
			}
			dev_printk(KERN_INFO, &cmd->device->sdev_gendev, "%s",
				   logbuf);
		}
		goto out;
	}
	if (!WARN_ON(off > logbuf_len - 49)) {
		off += scnprintf(logbuf + off, logbuf_len - off, " ");
		hex_dump_to_buffer(cmd->cmnd, cmd->cmd_len, 16, 1,
				   logbuf + off, logbuf_len - off,
				   false);
	}
out_printk:
	dev_printk(KERN_INFO, &cmd->device->sdev_gendev, "%s", logbuf);
out:
	scsi_log_release_buffer(logbuf);
}
EXPORT_SYMBOL(scsi_print_command);

static size_t
scsi_format_extd_sense(char *buffer, size_t buf_len,
		       unsigned char asc, unsigned char ascq)
{
	size_t off = 0;
	const char *extd_sense_fmt = NULL;
	const char *extd_sense_str = scsi_extd_sense_format(asc, ascq,
							    &extd_sense_fmt);

	if (extd_sense_str) {
		off = scnprintf(buffer, buf_len, "Add. Sense: %s",
				extd_sense_str);
		if (extd_sense_fmt)
			off += scnprintf(buffer + off, buf_len - off,
					 "(%s%x)", extd_sense_fmt, ascq);
	} else {
		if (asc >= 0x80)
			off = scnprintf(buffer, buf_len, "<<vendor>>");
		off += scnprintf(buffer + off, buf_len - off,
				 "ASC=0x%x ", asc);
		if (ascq >= 0x80)
			off += scnprintf(buffer + off, buf_len - off,
					 "<<vendor>>");
		off += scnprintf(buffer + off, buf_len - off,
				 "ASCQ=0x%x ", ascq);
	}
	return off;
}

static size_t
scsi_format_sense_hdr(char *buffer, size_t buf_len,
		      const struct scsi_sense_hdr *sshdr)
{
	const char *sense_txt;
	size_t off;

	off = scnprintf(buffer, buf_len, "Sense Key : ");
	sense_txt = scsi_sense_key_string(sshdr->sense_key);
	if (sense_txt)
		off += scnprintf(buffer + off, buf_len - off,
				 "%s ", sense_txt);
	else
		off += scnprintf(buffer + off, buf_len - off,
				 "0x%x ", sshdr->sense_key);
	off += scnprintf(buffer + off, buf_len - off,
		scsi_sense_is_deferred(sshdr) ? "[deferred] " : "[current] ");

	if (sshdr->response_code >= 0x72)
		off += scnprintf(buffer + off, buf_len - off, "[descriptor] ");
	return off;
}

static void
scsi_log_dump_sense(const struct scsi_device *sdev, const char *name, int tag,
		    const unsigned char *sense_buffer, int sense_len)
{
	char *logbuf;
	size_t logbuf_len;
	int i;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;

	for (i = 0; i < sense_len; i += 16) {
		int len = min(sense_len - i, 16);
		size_t off;

		off = sdev_format_header(logbuf, logbuf_len,
					 name, tag);
		hex_dump_to_buffer(&sense_buffer[i], len, 16, 1,
				   logbuf + off, logbuf_len - off,
				   false);
		dev_printk(KERN_INFO, &sdev->sdev_gendev, "%s", logbuf);
	}
	scsi_log_release_buffer(logbuf);
}

static void
scsi_log_print_sense_hdr(const struct scsi_device *sdev, const char *name,
			 int tag, const struct scsi_sense_hdr *sshdr)
{
	char *logbuf;
	size_t off, logbuf_len;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;
	off = sdev_format_header(logbuf, logbuf_len, name, tag);
	off += scsi_format_sense_hdr(logbuf + off, logbuf_len - off, sshdr);
	dev_printk(KERN_INFO, &sdev->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;
	off = sdev_format_header(logbuf, logbuf_len, name, tag);
	off += scsi_format_extd_sense(logbuf + off, logbuf_len - off,
				      sshdr->asc, sshdr->ascq);
	dev_printk(KERN_INFO, &sdev->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);
}

static void
scsi_log_print_sense(const struct scsi_device *sdev, const char *name, int tag,
		     const unsigned char *sense_buffer, int sense_len)
{
	struct scsi_sense_hdr sshdr;

	if (scsi_normalize_sense(sense_buffer, sense_len, &sshdr))
		scsi_log_print_sense_hdr(sdev, name, tag, &sshdr);
	else
		scsi_log_dump_sense(sdev, name, tag, sense_buffer, sense_len);
}

/*
 * Print normalized SCSI sense header with a prefix.
 */
void
scsi_print_sense_hdr(const struct scsi_device *sdev, const char *name,
		     const struct scsi_sense_hdr *sshdr)
{
	scsi_log_print_sense_hdr(sdev, name, -1, sshdr);
}
EXPORT_SYMBOL(scsi_print_sense_hdr);

/* Normalize and print sense buffer with name prefix */
void __scsi_print_sense(const struct scsi_device *sdev, const char *name,
			const unsigned char *sense_buffer, int sense_len)
{
	scsi_log_print_sense(sdev, name, -1, sense_buffer, sense_len);
}
EXPORT_SYMBOL(__scsi_print_sense);

/* Normalize and print sense buffer in SCSI command */
void scsi_print_sense(const struct scsi_cmnd *cmd)
{
	scsi_log_print_sense(cmd->device, scmd_name(cmd),
			     scsi_cmd_to_rq((struct scsi_cmnd *)cmd)->tag,
			     cmd->sense_buffer, SCSI_SENSE_BUFFERSIZE);
}
EXPORT_SYMBOL(scsi_print_sense);

void scsi_print_result(const struct scsi_cmnd *cmd, const char *msg,
		       int disposition)
{
	char *logbuf;
	size_t off, logbuf_len;
	const char *mlret_string = scsi_mlreturn_string(disposition);
	const char *hb_string = scsi_hostbyte_string(cmd->result);
	unsigned long cmd_age = (jiffies - cmd->jiffies_at_alloc) / HZ;

	logbuf = scsi_log_reserve_buffer(&logbuf_len);
	if (!logbuf)
		return;

	off = sdev_format_header(logbuf, logbuf_len, scmd_name(cmd),
				 scsi_cmd_to_rq((struct scsi_cmnd *)cmd)->tag);

	if (off >= logbuf_len)
		goto out_printk;

	if (msg) {
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "%s: ", msg);
		if (WARN_ON(off >= logbuf_len))
			goto out_printk;
	}
	if (mlret_string)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "%s ", mlret_string);
	else
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "UNKNOWN(0x%02x) ", disposition);
	if (WARN_ON(off >= logbuf_len))
		goto out_printk;

	off += scnprintf(logbuf + off, logbuf_len - off, "Result: ");
	if (WARN_ON(off >= logbuf_len))
		goto out_printk;

	if (hb_string)
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "hostbyte=%s ", hb_string);
	else
		off += scnprintf(logbuf + off, logbuf_len - off,
				 "hostbyte=0x%02x ", host_byte(cmd->result));
	if (WARN_ON(off >= logbuf_len))
		goto out_printk;

	off += scnprintf(logbuf + off, logbuf_len - off,
			 "driverbyte=DRIVER_OK ");

	off += scnprintf(logbuf + off, logbuf_len - off,
			 "cmd_age=%lus", cmd_age);

out_printk:
	dev_printk(KERN_INFO, &cmd->device->sdev_gendev, "%s", logbuf);
	scsi_log_release_buffer(logbuf);
}
EXPORT_SYMBOL(scsi_print_result);
