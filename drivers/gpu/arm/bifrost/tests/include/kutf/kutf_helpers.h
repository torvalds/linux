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



#ifndef _KERNEL_UTF_HELPERS_H_
#define _KERNEL_UTF_HELPERS_H_

/* kutf_helpers.h
 * Test helper functions for the kernel UTF test infrastructure.
 *
 * This collection of helper functions are provided as 'stock' implementation
 * helpers for certain features of kutf. Tests can implement common/boilerplate
 * functionality using these, whilst still providing them the option of
 * implementing completely custom functions themselves to use those kutf
 * features.
 */

#include <kutf/kutf_suite.h>
#include <kutf/kutf_mem.h>
#include <linux/wait.h>

/**
 * enum kutf_helper_textbuf_flag - flags for textbufs
 * @KUTF_HELPER_TEXTBUF_FLAG_DYING:	Test is dying, textbuf should not allow
 *                                      writes, nor block on empty.
 */
enum kutf_helper_textbuf_flag {
	KUTF_HELPER_TEXTBUF_FLAG_DYING = (1u << 0),
};

/**
 * struct kutf_helper_textbuf_line - Structure representing a line of text
 *
 * The string itself is stored immediately after this.
 *
 * @node:		List node for the textbuf's textbuf_list
 * @str_size:		Length of the string buffer, including the \0 terminator
 * @str:		'Flexible array' for the string representing the line
 */
struct kutf_helper_textbuf_line {
	struct list_head node;
	int str_size;
	char str[];
};

/**
 * struct kutf_helper_textbuf - Structure to representing sequential lines of
 *                              text
 * @lock:		mutex to hold whilst accessing the structure
 * @nr_user_clients:	Number of userspace clients connected via an open()
 *                      call
 * @mempool:		mempool for allocating lines
 * @scratchpad:		scratch area for receiving text of size max_line_size
 * @used_bytes:		number of valid bytes in the scratchpad
 * @prev_pos:		Previous position userspace has accessed
 * @prev_line_pos:	Previous start of line position userspace has accessed
 * @textbuf_list:	List head to store all the lines of text
 * @max_line_size:	Maximum size in memory allowed for a line of text
 * @max_nr_lines:	Maximum number of lines permitted in this textbuf
 * @nr_lines:		Number of entries in textbuf_list
 * @flags:		Flags indicating state of the textbuf, using values
 *                      from enum kutf_helper_textbuf_flag
 * @user_opened_wq:	Waitq for when there's at least one userspace client
 *                      connected to the textbuf via an open() call
 * @not_full_wq:	Waitq for when the textbuf can be enqueued into/can
 *                      consume data from userspace
 * @not_empty_wq:	Waitq for when the textbuf can be dequeued from/can
 *                      produce data for userspace
 */

struct kutf_helper_textbuf {
	struct mutex lock;
	int nr_user_clients;
	struct kutf_mempool *mempool;
	char *scratchpad;
	int used_bytes;
	loff_t prev_pos;
	loff_t prev_line_pos;
	struct list_head textbuf_list;
	int max_line_size;
	int max_nr_lines;
	int nr_lines;
	unsigned long flags;
	wait_queue_head_t user_opened_wq;
	wait_queue_head_t not_full_wq;
	wait_queue_head_t not_empty_wq;

};

/* stock callbacks for userspace to read from/write to the 'data' file as a
 * textbuf */
extern struct kutf_userdata_ops kutf_helper_textbuf_userdata_ops;

/**
 * kutf_helper_textbuf_init() - init a textbuf for use as a 'data' file
 *                              consumer/producer
 * @textbuf:		textbuf to initialize
 * @mempool:		mempool to allocate from
 * @max_line_size:	maximum line size expected to/from userspace
 * @max_nr_lines:	maximum number of lines to expect to/from userspace
 *
 * Initialize a textbuf so that it can consume writes made to the 'data' file,
 * and produce reads for userspace on the 'data' file. Tests may then read the
 * lines written by userspace, or fill the buffer so it may be read back by
 * userspace.
 *
 * The caller should write the @textbuf pointer into the kutf_context's
 * userdata_producer_priv or userdata_consumer_priv member during fixture
 * creation.
 *
 * Usually a test will have separate textbufs for userspace to write to and
 * read from. Using the same one for both will echo back to the user what they
 * are writing.
 *
 * Lines are understood as being separated by the '\n' character, but no '\n'
 * characters will be observed by the test
 *
 * @max_line_size puts an upper bound on the size of lines in a textbuf,
 * including the \0 terminator. Lines exceeding this will be truncated,
 * effectively ignoring incoming data until the next '\n'
 *
 * Combining this with @max_nr_lines puts an upper bound on the size of the
 * file read in
 *
 * Return:		0 on success, or negative value on error.
 */
int kutf_helper_textbuf_init(struct kutf_helper_textbuf *textbuf,
		struct kutf_mempool *mempool, int max_line_size,
		int max_nr_lines);

/**
 * kutf_helper_textbuf_wait_for_user() - wait for userspace to open the 'data'
 *                                       file
 * @textbuf:		textbuf to wait on
 *
 * This can be used to synchronize with userspace so that subsequent calls to
 * kutf_helper_textbuf_dequeue() and kutf_helper_textbuf_enqueue() should
 * succeed.
 *
 * Waiting is done on a timeout.
 *
 * There is of course no guarantee that userspace will keep the file open after
 * this, but any error in the dequeue/enqueue functions afterwards can be
 * treated as such rather than "we're still waiting for userspace to begin"
 *
 * Return:		0 if waited successfully, -ETIMEDOUT if we exceeded the
 *                      timeout, or some other negative value if there was an
 *                      error during waiting.
 */

int kutf_helper_textbuf_wait_for_user(struct kutf_helper_textbuf *textbuf);


/**
 * kutf_helper_textbuf_dequeue() - dequeue a line from a textbuf
 * @textbuf:		textbuf dequeue a line as a string from
 * @str_size:		pointer to storage to receive the size of the string,
 *                      which includes the '\0' terminator, or NULL if not
 *                      required
 *
 * Dequeue (remove) a line from the start of the textbuf as a string, and
 * return it.
 *
 * If no lines are available, then this will block until a line has been
 * submitted. If a userspace client is not connected and there are no remaining
 * lines, then this function returns NULL instead.
 *
 * The memory for the string comes from the kutf_mempool given during
 * initialization of the textbuf, and shares the same lifetime as it.
 *
 * Return:		pointer to the next line of the textbuf. NULL indicated
 *                      all userspace clients disconnected. An error value to be
 *                      checked with IS_ERR() family of functions if a signal or
 *                      some other error occurred
 */
char *kutf_helper_textbuf_dequeue(struct kutf_helper_textbuf *textbuf,
		int *str_size);

/**
 * kutf_helper_textbuf_enqueue() - enqueue a line to a textbuf
 * @textbuf:		textbuf to enqueue a line as a string to
 * @enqueue_str:	pointer to the string to enqueue to the textbuf
 * @buf_max_size:	maximum size of the buffer holding @enqueue_str
 *
 * Enqueue (add) a line to the end of a textbuf as a string.
 *
 * The caller should avoid placing '\n' characters in their strings, as these
 * will not be split into multiple lines.
 *
 * A copy of the string will be made into the textbuf, so @enqueue_str can be
 * freed immediately after if.the caller wishes to do so.
 *
 * If the maximum amount of lines has been reached, then this will block until
 * a line has been removed to make space. If a userspace client is not
 * connected and there is no space available, then this function returns
 * -EBUSY.
 *
 * Return:		0 on success, or negative value on error
 */
int kutf_helper_textbuf_enqueue(struct kutf_helper_textbuf *textbuf,
		char *enqueue_str, int buf_max_size);

#endif	/* _KERNEL_UTF_HELPERS_H_ */
