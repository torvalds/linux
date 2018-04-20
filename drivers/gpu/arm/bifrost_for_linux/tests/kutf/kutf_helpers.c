/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/* Kernel UTF test helpers */
#include <kutf/kutf_helpers.h>

/* 10s timeout for user thread to open the 'data' file once the test is started */
#define USERDATA_WAIT_TIMEOUT_MS 10000
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/preempt.h>
#include <linux/wait.h>
#include <linux/uaccess.h>


int kutf_helper_textbuf_init(struct kutf_helper_textbuf *textbuf,
		struct kutf_mempool *mempool, int max_line_size,
		int max_nr_lines)
{
	textbuf->scratchpad = kutf_mempool_alloc(mempool, max_line_size);

	if (!textbuf->scratchpad)
		return -ENOMEM;

	mutex_init(&textbuf->lock);
	textbuf->nr_user_clients = 0;
	textbuf->mempool = mempool;
	textbuf->used_bytes = 0;
	textbuf->prev_pos = 0;
	textbuf->prev_line_pos = 0;
	INIT_LIST_HEAD(&textbuf->textbuf_list);
	textbuf->max_line_size = max_line_size;
	textbuf->max_nr_lines = max_nr_lines;
	textbuf->nr_lines = 0;
	textbuf->flags = 0ul;
	init_waitqueue_head(&textbuf->user_opened_wq);
	init_waitqueue_head(&textbuf->not_full_wq);
	init_waitqueue_head(&textbuf->not_empty_wq);

	return 0;
}
EXPORT_SYMBOL(kutf_helper_textbuf_init);

/**
 * kutf_helper_textbuf_open() - Notify that userspace has opened the 'data'
 *                              file for a textbuf
 *
 * @priv:		private pointer from a kutf_userdata_exchange, which
 *                      should be a pointer to a struct kutf_helper_textbuf
 *
 * Return:		0 on success, or negative value on error.
 */
static int kutf_helper_textbuf_open(void *priv)
{
	struct kutf_helper_textbuf *textbuf = priv;
	int ret;

	ret = mutex_lock_interruptible(&textbuf->lock);
	if (ret)
		return -ERESTARTSYS;

	++(textbuf->nr_user_clients);
	wake_up(&textbuf->user_opened_wq);

	mutex_unlock(&textbuf->lock);
	return ret;
}

/**
 * kutf_helper_textbuf_release() - Notify that userspace has closed the 'data'
 *                                 file for a textbuf
 *
 * @priv:		private pointer from a kutf_userdata_exchange, which
 *                      should be a pointer to a struct kutf_helper_textbuf
 */
static void kutf_helper_textbuf_release(void *priv)
{
	struct kutf_helper_textbuf *textbuf = priv;

	/* Shouldn't use interruptible variants here because if a signal is
	 * pending, we can't abort and restart the call */
	mutex_lock(&textbuf->lock);

	--(textbuf->nr_user_clients);
	if (!textbuf->nr_user_clients) {
		/* All clients disconnected, wakeup kernel-side waiters */
		wake_up(&textbuf->not_full_wq);
		wake_up(&textbuf->not_empty_wq);
	}

	mutex_unlock(&textbuf->lock);
}

/**
 * kutf_helper_textbuf_notify_test_ended() - Notify that the test has ended
 *
 * @priv:		private pointer from a kutf_userdata_exchange, which
 *                      should be a pointer to a struct kutf_helper_textbuf
 *
 * After this call, userspace should be allowed to finish remaining reads but
 * not make new ones, and not be allowed to make new writes.
 */
static void kutf_helper_textbuf_notify_test_ended(void *priv)
{
	struct kutf_helper_textbuf *textbuf = priv;

	/* Shouldn't use interruptible variants here because if a signal is
	 * pending, we can't abort and restart the call */
	mutex_lock(&textbuf->lock);

	textbuf->flags |= KUTF_HELPER_TEXTBUF_FLAG_DYING;

	/* Consumers waiting due to being full should wake up and abort */
	wake_up(&textbuf->not_full_wq);
	/* Producers waiting due to being empty should wake up and abort */
	wake_up(&textbuf->not_empty_wq);

	mutex_unlock(&textbuf->lock);
}

/* Collect text in a textbuf scratchpad up to (but excluding) specified
 * newline_off, and add it as a textbuf_line
 *
 * newline_off is permissible to be at the character after the end of the
 * scratchpad (i.e. equal to textbuf->max_line_size), for handling when the
 * line was longer than the size of the scratchpad. Nevertheless, the resulting
 * size of the line is kept at textbuf->max_line_size, including the '\0'
 * terminator. That is, the string length will be textbuf->max_line_size-1.
 *
 * Remaining characters strictly after newline_off are moved to the beginning
 * of the scratchpad, to allow space for a longer line to be collected. This
 * means the character specified at newline_off will be removed from/no longer
 * be within the valid region of the scratchpad
 *
 * Returns number of bytes the scratchpad was shortened by, or an error
 * otherwise
 */
static size_t collect_line(struct kutf_helper_textbuf *textbuf, int newline_off)
{
	/* '\n' terminator will be replaced as '\0' */
	int str_buf_size;
	struct kutf_helper_textbuf_line *textbuf_line;
	char *str_start;
	int bytes_remain;
	char *scratch = textbuf->scratchpad;
	int nextline_off;

	str_buf_size = newline_off + 1;
	if (str_buf_size > textbuf->max_line_size)
		str_buf_size = textbuf->max_line_size;

	/* String is stored immediately after the line */
	textbuf_line = kutf_mempool_alloc(textbuf->mempool, str_buf_size + sizeof(struct kutf_helper_textbuf_line));
	if (!textbuf_line)
		return -ENOMEM;

	str_start = &textbuf_line->str[0];

	/* Copy in string, excluding the terminating '\n' character, replacing
	 * it with '\0' */
	strncpy(str_start, scratch, str_buf_size - 1);
	str_start[str_buf_size-1] = '\0';
	textbuf_line->str_size = str_buf_size;

	/* Append to the textbuf */
	list_add_tail(&textbuf_line->node, &textbuf->textbuf_list);
	++(textbuf->nr_lines);

	/* Move the rest of the scratchpad to the start */
	nextline_off = newline_off + 1;
	if (nextline_off > textbuf->used_bytes)
		nextline_off =  textbuf->used_bytes;

	bytes_remain = textbuf->used_bytes - nextline_off;
	memmove(scratch, scratch + nextline_off, bytes_remain);
	textbuf->used_bytes = bytes_remain;

	/* Wakeup anyone blocked on empty */
	wake_up(&textbuf->not_empty_wq);

	return nextline_off;
}

/* Buffer size for truncating a string to its newline.
 * Allocated on the stack, so keep it moderately small (within PAGE_SIZE) */
#define TRUNCATE_BUF_SZ 512

/* Discard input from a userbuf up to a newline, then collect what was in the
 * scratchpad into a new textbuf line */
static ssize_t collect_longline_truncate(struct kutf_helper_textbuf *textbuf,
		const char  __user *userbuf, size_t userbuf_len)
{
	ssize_t bytes_processed = 0;

	while (userbuf_len > 0) {
		int userbuf_copy_sz = userbuf_len;
		size_t res;
		char *newline_ptr;
		char truncate_buf[TRUNCATE_BUF_SZ];

		if (userbuf_len > TRUNCATE_BUF_SZ)
			userbuf_copy_sz = TRUNCATE_BUF_SZ;
		else
			userbuf_copy_sz = (int)userbuf_len;

		/* copy what we can */
		res = copy_from_user(truncate_buf, userbuf, userbuf_copy_sz);
		if (res == userbuf_copy_sz)
			return -EFAULT;
		userbuf_copy_sz -= res;

		/* Search for newline in what was copied */
		newline_ptr = strnchr(truncate_buf, userbuf_copy_sz, '\n');

		if (newline_ptr) {
			ssize_t sres;
			/* Newline found: collect scratchpad and exit out */
			int newline_off = newline_ptr - truncate_buf;

			sres = collect_line(textbuf, textbuf->used_bytes);
			if (sres < 0)
				return sres;

			bytes_processed += newline_off + 1;
			break;
		}

		/* Newline not yet found: advance to the next part to copy */
		userbuf += userbuf_copy_sz;
		userbuf_len -= userbuf_copy_sz;
		bytes_processed += userbuf_copy_sz;
	}

	return bytes_processed;
}

/**
 * kutf_helper_textbuf_consume() - 'data' file consumer function for writing to
 *                                 a textbuf
 * @priv:		private pointer from a kutf_userdata_exchange, which
 *                      should be a pointer to a struct kutf_helper_textbuf to
 *                      write into
 * @userbuf:		the userspace buffer to read from
 * @userbuf_len:	size of the userspace buffer
 * @ppos:		the current position in the buffer
 *
 * This consumer function is used as a write consumer for the 'data' file,
 * receiving data that has been written to the 'data' file by userspace. It
 * will read from the userspace buffer @userbuf and separates it into '\n'
 * delimited lines for the textbuf pointed to by @priv .
 *
 * If there is insufficient space in textbuf, then it will block until there is
 * space - for example, a kernel-side test calls
 * kutf_helper_textbuf_dequeue(). Since this is expected to be called in the
 * context of a syscall, the call can only be cancelled by sending an
 * appropriate signal to the userspace process.
 *
 * The current position @ppos is advanced by the number of bytes successfully
 * read.
 *
 * Return:		the number of bytes read, or negative value on error.
 */
static ssize_t kutf_helper_textbuf_consume(void *priv,
		const char  __user *userbuf, size_t userbuf_len, loff_t *ppos)
{
	struct kutf_helper_textbuf *textbuf = priv;
	int userbuf_copy_sz;
	char *next_newline_ptr;
	size_t bytes_processed = 0;
	int newdata_off;
	ssize_t ret;

	ret = mutex_lock_interruptible(&textbuf->lock);
	if (ret)
		return -ERESTARTSYS;

	/* Validate input */
	if (*ppos < 0) {
		ret = -EINVAL;
		goto out_unlock;
	}
	if (!userbuf_len) {
		ret = 0;
		goto out_unlock;
	}

	while (textbuf->nr_lines >= textbuf->max_nr_lines &&
			!(textbuf->flags & KUTF_HELPER_TEXTBUF_FLAG_DYING)) {
		/* Block on kernel-side dequeue making space available
		 * NOTE: should also handle O_NONBLOCK */
		mutex_unlock(&textbuf->lock);
		ret = wait_event_interruptible(textbuf->not_full_wq,
				(textbuf->nr_lines < textbuf->max_nr_lines ||
				(textbuf->flags & KUTF_HELPER_TEXTBUF_FLAG_DYING)));
		if (ret)
			return -ERESTARTSYS;
		ret = mutex_lock_interruptible(&textbuf->lock);
		if (ret)
			return -ERESTARTSYS;
	}

	if (textbuf->flags & KUTF_HELPER_TEXTBUF_FLAG_DYING) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (textbuf->prev_pos != *ppos && textbuf->used_bytes) {
		/* Seeking causes a new line to occur:
		 * Truncate what data was there into a textbuf-line, and reset
		 * the buffer */
		ret = collect_line(textbuf, textbuf->used_bytes);
		if (ret < 0)
			goto finish;
	} else if (textbuf->used_bytes >= (textbuf->max_line_size - 1)) {
		/* Line too long discard input until we find a '\n' */
		ret = collect_longline_truncate(textbuf, userbuf, userbuf_len);

		if (ret < 0)
			goto finish;

		/* Update userbuf with how much was processed, which may be the
		 * entire buffer now */
		userbuf += ret;
		userbuf_len -= ret;
		bytes_processed += ret;

		/* If there's buffer remaining and we fault later (e.g. can't
		 * read or OOM) ensure ppos is updated */
		*ppos += ret;

		/* recheck in case entire buffer processed */
		if (!userbuf_len)
			goto finish;
	}

	/* An extra line may've been added, ensure we don't overfill */
	if (textbuf->nr_lines >= textbuf->max_nr_lines)
		goto finish_noerr;

	userbuf_copy_sz = userbuf_len;

	/* Copy in as much as we can */
	if (userbuf_copy_sz > textbuf->max_line_size - textbuf->used_bytes)
		userbuf_copy_sz = textbuf->max_line_size - textbuf->used_bytes;

	ret = copy_from_user(textbuf->scratchpad + textbuf->used_bytes, userbuf, userbuf_copy_sz);
	if (ret == userbuf_copy_sz) {
		ret = -EFAULT;
		goto finish;
	}
	userbuf_copy_sz -= ret;

	newdata_off = textbuf->used_bytes;
	textbuf->used_bytes += userbuf_copy_sz;

	while (textbuf->used_bytes && textbuf->nr_lines < textbuf->max_nr_lines) {
		int new_bytes_remain = textbuf->used_bytes - newdata_off;
		/* Find a new line - only the new part should be checked */
		next_newline_ptr = strnchr(textbuf->scratchpad + newdata_off, new_bytes_remain, '\n');

		if (next_newline_ptr) {
			int newline_off = next_newline_ptr - textbuf->scratchpad;

			/* if found, collect up to it, then memmove the rest */
			/* reset positions and see if we can fill any further */
			/* repeat until run out of data or line is filled */
			ret = collect_line(textbuf, newline_off);

			/* If filled up or OOM, rollback the remaining new
			 * data. Instead we'll try to grab it next time we're
			 * called */
			if (textbuf->nr_lines >= textbuf->max_nr_lines || ret < 0)
				textbuf->used_bytes = newdata_off;

			if (ret < 0)
				goto finish;

			/* Fix up ppos etc in case we'll be ending the loop */
			*ppos += ret - newdata_off;
			bytes_processed += ret - newdata_off;
			newdata_off = 0;
		} else {
			/* there's bytes left, but no new-line, so try to fill up next time */
			*ppos += new_bytes_remain;
			bytes_processed += new_bytes_remain;
			break;
		}
	}

finish_noerr:
	ret = bytes_processed;
finish:
	textbuf->prev_pos = *ppos;
out_unlock:
	mutex_unlock(&textbuf->lock);

	return ret;
}

/**
 * kutf_helper_textbuf_produce() - 'data' file producer function for reading
 *                                 from a textbuf
 * @priv:		private pointer from a kutf_userdata_exchange, which
 *                      should be a pointer to a struct kutf_helper_textbuf to
 *                      read from
 * @userbuf:		the userspace buffer to write to
 * @userbuf_len:	size of the userspace buffer
 * @ppos:		the current position in the buffer
 *
 * This producer function is used as a read producer for the 'data' file,
 * allowing userspace to read from the 'data' file. It will write to the
 * userspace buffer @userbuf, taking lines from the textbuf pointed to by
 * @priv, separating each line with '\n'.
 *
 * If there is no data in the textbuf, then it will block until some appears -
 * for example, a kernel-side test calls kutf_helper_textbuf_enqueue(). Since
 * this is expected to be called in the context of a syscall, the call can only
 * be cancelled by sending an appropriate signal to the userspace process.
 *
 * The current position @ppos is advanced by the number of bytes successfully
 * written.
 *
 * Return:		the number of bytes written, or negative value on error
 */
static ssize_t kutf_helper_textbuf_produce(void *priv, char  __user *userbuf,
		size_t userbuf_len, loff_t *ppos)
{
	struct kutf_helper_textbuf *textbuf = priv;
	loff_t pos_offset;
	struct kutf_helper_textbuf_line *line = NULL;
	int line_start_pos;
	size_t bytes_processed = 0;
	ssize_t ret;
	int copy_length;

	ret = mutex_lock_interruptible(&textbuf->lock);
	if (ret)
		return -ERESTARTSYS;

	/* Validate input */
	if (*ppos < 0) {
		ret = -EINVAL;
		goto finish;
	}
	if (!userbuf_len) {
		ret = 0;
		goto finish;
	}

	/* Seeking to before the beginning of the line will have the effect of
	 * resetting the position to the start of the current data, since we've
	 * already discarded previous data */
	if (*ppos < textbuf->prev_line_pos)
		textbuf->prev_line_pos = *ppos;

	while (!line) {
		int needs_wake = 0;

		pos_offset = *ppos - textbuf->prev_line_pos;
		line_start_pos = 0;

		/* Find the line for the offset, emptying the textbuf as we go */
		while (!list_empty(&textbuf->textbuf_list)) {
			int line_end_pos;

			line = list_first_entry(&textbuf->textbuf_list, struct kutf_helper_textbuf_line, node);

			/* str_size used in line_end_pos because lines implicitly have
			 * a '\n', but we count the '\0' string terminator as that */
			line_end_pos = line_start_pos + line->str_size;

			if (pos_offset < line_end_pos)
				break;

			line_start_pos += line->str_size;
			/* Only discard a line when we're sure it's finished
			 * with, to avoid awkward rollback conditions if we've
			 * had to block */
			list_del(&line->node);
			--(textbuf->nr_lines);
			line = NULL;
			needs_wake = 1;
		}

		/* Update the start of the line pos for next time we're called */
		textbuf->prev_line_pos += line_start_pos;

		/* If space was freed up, wake waiters */
		if (needs_wake)
			wake_up(&textbuf->not_full_wq);
;
		if (!line) {
			/* Only check before waiting, to ensure if the test
			 * does the last enqueue and immediately finishes, then
			 * we'll go back round the loop to receive the line
			 * instead of just dying straight away */
			if (textbuf->flags & KUTF_HELPER_TEXTBUF_FLAG_DYING) {
				/* Indicate EOF rather than an error */
				ret = 0;
				goto finish;
			}

			/* No lines found, block for new ones
			 * NOTE: should also handle O_NONBLOCK */
			mutex_unlock(&textbuf->lock);
			ret = wait_event_interruptible(textbuf->not_empty_wq,
					(textbuf->nr_lines > 0 ||
					(textbuf->flags & KUTF_HELPER_TEXTBUF_FLAG_DYING)));

			/* signals here are not restartable */
			if (ret)
				return ret;
			ret = mutex_lock_interruptible(&textbuf->lock);
			if (ret)
				return ret;
		}

	}


	/* Find offset within the line, guaranteed to be within line->str_size */
	pos_offset -= line_start_pos;

	while (userbuf_len && line) {
		/* Copy at most to the end of string, excluding terminator */
		copy_length = line->str_size - 1 - pos_offset;
		if (copy_length > userbuf_len)
			copy_length = userbuf_len;

		if (copy_length) {
			ret = copy_to_user(userbuf, &line->str[pos_offset], copy_length);
			if (ret == copy_length) {
				ret = -EFAULT;
				goto finish;
			}
			copy_length -= ret;

			userbuf += copy_length;
			userbuf_len -= copy_length;
			bytes_processed += copy_length;
			*ppos += copy_length;
			if (ret)
				goto finish_noerr;
		}

		/* Add terminator if one was needed */
		if (userbuf_len) {
			copy_length = 1;
			ret = copy_to_user(userbuf, "\n", copy_length);
			if (ret == copy_length) {
				ret = -EFAULT;
				goto finish;
			}
			copy_length -= ret;

			userbuf += copy_length;
			userbuf_len -= copy_length;
			bytes_processed += copy_length;
			*ppos += copy_length;
		} else {
			/* string wasn't completely copied this time - try to
			 * finish it next call */
			break;
		}

		/* Line Completed - only now can safely delete it */
		textbuf->prev_line_pos += line->str_size;
		list_del(&line->node);
		--(textbuf->nr_lines);
		line = NULL;
		/* Space freed up, wake up waiters */
		wake_up(&textbuf->not_full_wq);

		/* Pick the next line  */
		if (!list_empty(&textbuf->textbuf_list)) {
			line = list_first_entry(&textbuf->textbuf_list, struct kutf_helper_textbuf_line, node);
			pos_offset = 0;
		}
		/* if no more lines, we've copied at least some bytes, so only
		 * need to block on new lines the next time we're called */
	}

finish_noerr:
	ret = bytes_processed;
finish:
	mutex_unlock(&textbuf->lock);

	return ret;
}

int kutf_helper_textbuf_wait_for_user(struct kutf_helper_textbuf *textbuf)
{
	int err;
	unsigned long now;
	unsigned long timeout_jiffies = msecs_to_jiffies(USERDATA_WAIT_TIMEOUT_MS);
	unsigned long time_end;
	int ret = 0;

	/* Mutex locking using non-interruptible variants, since a signal to
	 * the user process will generally have to wait until we finish the
	 * test, because we can't restart the test. The exception is where
	 * we're blocked on a waitq */
	mutex_lock(&textbuf->lock);

	now = jiffies;
	time_end = now + timeout_jiffies;

	while (!textbuf->nr_user_clients && time_before_eq(now, time_end)) {
		unsigned long time_to_wait = time_end - now;
		/* No users yet, block or timeout */
		mutex_unlock(&textbuf->lock);
		/* Use interruptible here - in case we block for a long time
		 * and want to kill the user process */
		err = wait_event_interruptible_timeout(textbuf->user_opened_wq,
				(textbuf->nr_user_clients > 0), time_to_wait);
		/* Any error is not restartable due to how kutf runs tests */
		if (err < 0)
			return -EINTR;
		mutex_lock(&textbuf->lock);

		now = jiffies;
	}
	if (!textbuf->nr_user_clients)
		ret = -ETIMEDOUT;

	mutex_unlock(&textbuf->lock);

	return ret;
}
EXPORT_SYMBOL(kutf_helper_textbuf_wait_for_user);

char *kutf_helper_textbuf_dequeue(struct kutf_helper_textbuf *textbuf,
		int *str_size)
{
	struct kutf_helper_textbuf_line *line;
	char *ret = NULL;

	/* Mutex locking using non-interruptible variants, since a signal to
	 * the user process will generally have to wait until we finish the
	 * test, because we can't restart the test. The exception is where
	 * we're blocked on a waitq */
	mutex_lock(&textbuf->lock);

	while (list_empty(&textbuf->textbuf_list)) {
		int err;

		if (!textbuf->nr_user_clients) {
			/* No user-side clients - error */
			goto out;
		}

		/* No lines found, block for new ones from user-side consumer */
		mutex_unlock(&textbuf->lock);
		/* Use interruptible here - in case we block for a long time
		 * and want to kill the user process */
		err = wait_event_interruptible(textbuf->not_empty_wq,
				(textbuf->nr_lines > 0 || !textbuf->nr_user_clients));
		/* Any error is not restartable due to how kutf runs tests */
		if (err)
			return ERR_PTR(-EINTR);
		mutex_lock(&textbuf->lock);
	}

	line = list_first_entry(&textbuf->textbuf_list, struct kutf_helper_textbuf_line, node);
	list_del(&line->node);
	--(textbuf->nr_lines);
	/* Space freed up, wake up waiters */
	wake_up(&textbuf->not_full_wq);

	if (str_size)
		*str_size = line->str_size;

	ret = &line->str[0];

out:
	mutex_unlock(&textbuf->lock);
	return ret;
}
EXPORT_SYMBOL(kutf_helper_textbuf_dequeue);

int kutf_helper_textbuf_enqueue(struct kutf_helper_textbuf *textbuf,
		char *enqueue_str, int buf_max_size)
{
	struct kutf_helper_textbuf_line *textbuf_line;
	int str_size = strnlen(enqueue_str, buf_max_size) + 1;
	char *str_start;
	int ret = 0;

	/* Mutex locking using non-interruptible variants, since a signal to
	 * the user process will generally have to wait until we finish the
	 * test, because we can't restart the test. The exception is where
	 * we're blocked on a waitq */
	mutex_lock(&textbuf->lock);

	if (str_size > textbuf->max_line_size)
		str_size = textbuf->max_line_size;

	while (textbuf->nr_lines >= textbuf->max_nr_lines) {
		if (!textbuf->nr_user_clients) {
			/* No user-side clients - error */
			ret = -EBUSY;
			goto out;
		}

		/* Block on user-side producer making space available */
		mutex_unlock(&textbuf->lock);
		/* Use interruptible here - in case we block for a long time
		 * and want to kill the user process */
		ret = wait_event_interruptible(textbuf->not_full_wq,
				(textbuf->nr_lines < textbuf->max_nr_lines || !textbuf->nr_user_clients));
		/* Any error is not restartable due to how kutf runs tests */
		if (ret)
			return -EINTR;
		mutex_lock(&textbuf->lock);
	}

	/* String is stored immediately after the line */
	textbuf_line = kutf_mempool_alloc(textbuf->mempool, str_size + sizeof(struct kutf_helper_textbuf_line));
	if (!textbuf_line) {
		ret = -ENOMEM;
		goto out;
	}

	str_start = &textbuf_line->str[0];

	/* Copy in string */
	strncpy(str_start, enqueue_str, str_size);
	/* Enforce the '\0' termination */
	str_start[str_size-1] = '\0';
	textbuf_line->str_size = str_size;

	/* Append to the textbuf */
	list_add_tail(&textbuf_line->node, &textbuf->textbuf_list);
	++(textbuf->nr_lines);

	/* Wakeup anyone blocked on empty */
	wake_up(&textbuf->not_empty_wq);

out:
	mutex_unlock(&textbuf->lock);
	return ret;
}
EXPORT_SYMBOL(kutf_helper_textbuf_enqueue);


struct kutf_userdata_ops kutf_helper_textbuf_userdata_ops = {
	.open = kutf_helper_textbuf_open,
	.release = kutf_helper_textbuf_release,
	.notify_ended = kutf_helper_textbuf_notify_test_ended,
	.consumer = kutf_helper_textbuf_consume,
	.producer = kutf_helper_textbuf_produce,
};
EXPORT_SYMBOL(kutf_helper_textbuf_userdata_ops);
