/*
 * MobiCore Driver Logging Subsystem.
 *
 * The logging subsystem provides the interface between the Mobicore trace
 * buffer and the Linux log
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/miscdevice.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/device.h>

#include "main.h"
#include "debug.h"
#include "ops.h"
#include "logging.h"

/* Default length of the log ring buffer 256KB*/
#define LOG_BUF_SIZE			(64 * PAGE_SIZE)

/* Max Len of a log line for printing */
#define LOG_LINE_SIZE			256

static uint32_t log_size = LOG_BUF_SIZE;

module_param(log_size, uint, 0);
MODULE_PARM_DESC(log_size, "Size of the MobiCore log ringbuffer(256KB def)");

/* Definitions for log version 2 */
#define LOG_TYPE_MASK			(0x0007)
#define LOG_TYPE_CHAR			0
#define LOG_TYPE_INTEGER		1
/* Field length */
#define LOG_LENGTH_MASK			(0x00F8)
#define LOG_LENGTH_SHIFT		3
/* Extra attributes */
#define LOG_EOL				(0x0100)
#define LOG_INTEGER_DECIMAL		(0x0200)
#define LOG_INTEGER_SIGNED		(0x0400)

struct logmsg_struct {
	uint16_t ctrl;			/* Type and format of data */
	uint16_t source;		/* Unique value for each event source */
	uint32_t log_data;		/* Value, if any */
};

static bool prev_eol;			/* Previous char was a EOL */
static uint16_t prev_source;		/* Previous Log source */
static uint32_t log_pos;		/* MobiCore log previous position */
static struct mc_trace_buf *log_buf;	/* MobiCore log buffer structure */
struct task_struct *log_thread;		/* Log Thread task structure */
static char *log_line;			/* Log Line buffer */
static uint32_t log_line_len;		/* Log Line buffer current length */
static int thread_err;

static void log_eol(uint16_t source)
{
	if (!strnlen(log_line, LOG_LINE_SIZE))
		return;

	prev_eol = true;
	/* MobiCore Userspace */
	if (prev_source)
		dev_info(mcd, "%03x|%s\n", prev_source, log_line);
	/* MobiCore kernel */
	else
		dev_info(mcd, "%s\n", log_line);

	log_line_len = 0;
	log_line[0] = 0;
}

/*
 * Collect chars in log_line buffer and output the buffer when it is full.
 * No locking needed because only "mobicore_log" thread updates this buffer.
 */
static void log_char(char ch, uint16_t source)
{
	if (ch == '\n' || ch == '\r') {
		log_eol(source);
		return;
	}

	if (log_line_len >= LOG_LINE_SIZE - 1 || source != prev_source)
		log_eol(source);

	log_line[log_line_len] = ch;
	log_line[log_line_len + 1] = 0;
	log_line_len++;
	prev_eol = false;
	prev_source = source;
}

static const uint8_t HEX2ASCII[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

static void dbg_raw_nro(uint32_t format, uint32_t value, uint16_t source)
{
	int digits = 1;
	uint32_t base = (format & LOG_INTEGER_DECIMAL) ? 10 : 16;
	int width = (format & LOG_LENGTH_MASK) >> LOG_LENGTH_SHIFT;
	int negative = 0;
	uint32_t digit_base = 1;

	if ((format & LOG_INTEGER_SIGNED) != 0 && ((signed int)value) < 0) {
		negative = 1;
		value = (uint32_t)(-(signed int)value);
		width--;
	}

	/* Find length and divider to get largest digit */
	while (value / digit_base >= base) {
		digit_base *= base;
		digits++;
	}

	if (width > digits) {
		char ch = (base == 10) ? ' ' : '0';
		while (width > digits) {
			log_char(ch, source);
			width--;
		}
	}

	if (negative)
		log_char('-', source);

	while (digits-- > 0) {
		uint32_t d = value / digit_base;
		log_char(HEX2ASCII[d], source);
		value = value - d * digit_base;
		digit_base /= base;
	}
}

static void log_msg(struct logmsg_struct *msg)
{
	switch (msg->ctrl & LOG_TYPE_MASK) {
	case LOG_TYPE_CHAR: {
		uint32_t ch;
		ch = msg->log_data;
		while (ch != 0) {
			log_char(ch & 0xFF, msg->source);
			ch >>= 8;
		}
		break;
	}
	case LOG_TYPE_INTEGER: {
		dbg_raw_nro(msg->ctrl, msg->log_data, msg->source);
		break;
	}
	default:
		break;
	}
	if (msg->ctrl & LOG_EOL)
		log_eol(msg->source);
}

static uint32_t process_log(void)
{
	char *last_msg = log_buf->buff + log_buf->write_pos;
	char *buff = log_buf->buff + log_pos;

	while (buff != last_msg) {
		log_msg((struct logmsg_struct *)buff);
		buff += sizeof(struct logmsg_struct);
		/* Wrap around */
		if ((buff + sizeof(struct logmsg_struct)) >
		    ((char *)log_buf + log_size))
			buff = log_buf->buff;
	}
	return buff - log_buf->buff;
}

/* log_worker() - Worker thread processing the log_buf buffer. */
static int log_worker(void *p)
{
	int ret = 0;
	if (log_buf == NULL) {
		ret = -EFAULT;
		goto err_kthread;
	}

	while (!kthread_should_stop()) {
		if (log_buf->write_pos == log_pos)
			schedule_timeout_interruptible(MAX_SCHEDULE_TIMEOUT);

		switch (log_buf->version) {
		case 2:
			log_pos = process_log();
			break;
		default:
			MCDRV_DBG_ERROR(mcd, "Unknown Mobicore log data");
			log_pos = log_buf->write_pos;
			/*
			 * Stop the thread as we have no idea what
			 * happens next
			 */
			ret = -EFAULT;
			goto err_kthread;
		}
	}

err_kthread:
	MCDRV_DBG(mcd, "Logging thread stopped!");
	thread_err = ret;
	/*
	 * Wait until the next kthread_stop() is called, if it was already
	 * called we just slip through, if there is an error signal it and
	 * wait to get the signal
	 */
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	return ret;
}

/*
 * Wake up the log reader thread
 * This should be called from the places where calls into MobiCore have
 * generated some logs(eg, yield, SIQ...)
 */
void mobicore_log_read(void)
{
	if (log_thread == NULL || IS_ERR(log_thread))
		return;

	/*
	 * The thread itself is in some error condition so just get
	 * rid of it
	 */
	if (thread_err != 0) {
		kthread_stop(log_thread);
		log_thread = NULL;
		return;
	}

	wake_up_process(log_thread);
}

/*
 * Setup MobiCore kernel log. It assumes it's running on CORE 0!
 * The fastcall will complain is that is not the case!
 */
long mobicore_log_setup(void)
{
	unsigned long phys_log_buf;
	union fc_generic fc_log;
	struct sched_param param = { .sched_priority = 1 };

	long ret;
	log_pos = 0;
	log_buf = NULL;
	log_thread = NULL;
	log_line = NULL;
	log_line_len = 0;
	prev_eol = false;
	prev_source = 0;
	thread_err = 0;

	/* Sanity check for the log size */
	if (log_size < PAGE_SIZE)
		return -EFAULT;
	else
		log_size = PAGE_ALIGN(log_size);

	log_line = kzalloc(LOG_LINE_SIZE, GFP_KERNEL);
	if (IS_ERR(log_line)) {
		MCDRV_DBG_ERROR(mcd, "failed to allocate log line!");
		return -ENOMEM;
	}

	log_thread = kthread_create(log_worker, NULL, "mc_log");
	if (IS_ERR(log_thread)) {
		MCDRV_DBG_ERROR(mcd, "MobiCore log thread creation failed!");
		ret = -EFAULT;
		goto err_free_line;
	}

	sched_setscheduler(log_thread, SCHED_IDLE, &param);
	/*
	 * We are going to map this buffer into virtual address space in SWd.
	 * To reduce complexity there, we use a contiguous buffer.
	 */
	log_buf = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					   get_order(log_size));
	if (!log_buf) {
		MCDRV_DBG_ERROR(mcd, "Failed to get page for logger!");
		ret = -ENOMEM;
		goto err_stop_kthread;
	}
	phys_log_buf = virt_to_phys(log_buf);

	memset(&fc_log, 0, sizeof(fc_log));
	fc_log.as_in.cmd = MC_FC_NWD_TRACE;
	fc_log.as_in.param[0] = phys_log_buf;
	fc_log.as_in.param[1] = log_size;

	MCDRV_DBG(mcd, "fc_log virt=%p phys=%p ",
		  log_buf, (void *)phys_log_buf);
	mc_fastcall(&fc_log);
	MCDRV_DBG(mcd, "fc_log out ret=0x%08x", fc_log.as_out.ret);

	/* If the setup failed we must free the memory allocated */
	if (fc_log.as_out.ret) {
		MCDRV_DBG_ERROR(mcd, "MobiCore shared traces setup failed!");
		free_pages((unsigned long)log_buf, get_order(log_size));
		log_buf = NULL;
		ret = -EIO;
		goto err_stop_kthread;
	}

	set_task_state(log_thread, TASK_INTERRUPTIBLE);

	MCDRV_DBG(mcd, "fc_log Logger version %u\n", log_buf->version);
	return 0;

err_stop_kthread:
	kthread_stop(log_thread);
	log_thread = NULL;
err_free_line:
	kfree(log_line);
	log_line = NULL;
	return ret;
}

/*
 * Free kernel log components.
 * ATTN: We can't free the log buffer because it's also in use by MobiCore and
 * even if the module is unloaded MobiCore is still running.
 */
void mobicore_log_free(void)
{
	if (log_thread && !IS_ERR(log_thread)) {
		/* We don't really care what the thread returns for exit */
		kthread_stop(log_thread);
	}

	kfree(log_line);
}
