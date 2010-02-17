/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "log.h"

#define LOG_BUF_MASK (log_buf_len-1)
#define LOG_BUF(idx) (log_buf[(idx) & LOG_BUF_MASK])

static char log_buf[LOG_BUF_LEN];
static int log_buf_len = LOG_BUF_LEN;
static unsigned long log_start;
static unsigned long log_end;
uint8_t log_level;

static DEFINE_SPINLOCK(logbuf_lock);

const struct file_operations proc_log_operations = {
	.open           = log_open,
	.release        = log_release,
	.read           = log_read,
	.write          = log_write,
	.poll           = log_poll,
};

static DECLARE_WAIT_QUEUE_HEAD(log_wait);

static void emit_log_char(char c)
{
	LOG_BUF(log_end) = c;
	log_end++;

	if (log_end - log_start > log_buf_len)
		log_start = log_end - log_buf_len;
}

static int fdebug_log(char *fmt, ...)
{
	int printed_len;
	char *p;
	va_list args;
	static char debug_log_buf[256];
	unsigned long flags;

	spin_lock_irqsave(&logbuf_lock, flags);
	va_start(args, fmt);
	printed_len = vscnprintf(debug_log_buf, sizeof(debug_log_buf), fmt,
				 args);
	va_end(args);

	for (p = debug_log_buf; *p != 0; p++)
		emit_log_char(*p);

	spin_unlock_irqrestore(&logbuf_lock, flags);

	wake_up(&log_wait);

	return 0;
}

int debug_log(int type, char *fmt, ...)
{
	va_list args;
	int retval = 0;
	char tmp_log_buf[256];

	/* only critical information get into the official kernel log */
	if (type == LOG_TYPE_CRIT) {
		va_start(args, fmt);
		vscnprintf(tmp_log_buf, sizeof(tmp_log_buf), fmt, args);
		printk(KERN_ERR "batman-adv: %s", tmp_log_buf);
		va_end(args);
	}

	if ((type == LOG_TYPE_CRIT) || (log_level & type)) {
		va_start(args, fmt);
		vscnprintf(tmp_log_buf, sizeof(tmp_log_buf), fmt, args);
		fdebug_log("[%10u] %s", (jiffies / HZ), tmp_log_buf);
		va_end(args);
	}

	return retval;
}

int log_open(struct inode *inode, struct file *file)
{
	inc_module_count();
	return 0;
}

int log_release(struct inode *inode, struct file *file)
{
	dec_module_count();
	return 0;
}

ssize_t log_read(struct file *file, char __user *buf, size_t count,
		 loff_t *ppos)
{
	int error, i = 0;
	char c;
	unsigned long flags;

	if ((file->f_flags & O_NONBLOCK) && !(log_end - log_start))
		return -EAGAIN;

	if ((!buf) || (count < 0))
		return -EINVAL;

	if (count == 0)
		return 0;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	error = wait_event_interruptible(log_wait, (log_start - log_end));

	if (error)
		return error;

	spin_lock_irqsave(&logbuf_lock, flags);

	while ((!error) && (log_start != log_end) && (i < count)) {
		c = LOG_BUF(log_start);

		log_start++;

		spin_unlock_irqrestore(&logbuf_lock, flags);

		error = __put_user(c, buf);

		spin_lock_irqsave(&logbuf_lock, flags);

		buf++;
		i++;

	}

	spin_unlock_irqrestore(&logbuf_lock, flags);

	if (!error)
		return i;

	return error;
}

ssize_t log_write(struct file *file, const char __user *buf, size_t count,
		  loff_t *ppos)
{
	return count;
}

unsigned int log_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &log_wait, wait);

	if (log_end - log_start)
		return POLLIN | POLLRDNORM;

	return 0;
}
