/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KERNEL_UTF_RESULTSET_H_
#define _KERNEL_UTF_RESULTSET_H_

/* kutf_resultset.h
 * Functions and structures for handling test results and result sets.
 *
 * This section of the kernel UTF contains structures and functions used for the
 * management of Results and Result Sets.
 */

/**
 * enum kutf_result_status - Status values for a single Test error.
 * @KUTF_RESULT_BENCHMARK:	Result is a meta-result containing benchmark
 *                              results.
 * @KUTF_RESULT_SKIP:		The test was skipped.
 * @KUTF_RESULT_UNKNOWN:	The test has an unknown result.
 * @KUTF_RESULT_PASS:		The test result passed.
 * @KUTF_RESULT_DEBUG:		The test result passed, but raised a debug
 *                              message.
 * @KUTF_RESULT_INFO:		The test result passed, but raised
 *                              an informative message.
 * @KUTF_RESULT_WARN:		The test result passed, but raised a warning
 *                              message.
 * @KUTF_RESULT_FAIL:		The test result failed with a non-fatal error.
 * @KUTF_RESULT_FATAL:		The test result failed with a fatal error.
 * @KUTF_RESULT_ABORT:		The test result failed due to a non-UTF
 *                              assertion failure.
 * @KUTF_RESULT_USERDATA:	User data is ready to be read,
 *                              this is not seen outside the kernel
 * @KUTF_RESULT_USERDATA_WAIT:	Waiting for user data to be sent,
 *                              this is not seen outside the kernel
 * @KUTF_RESULT_TEST_FINISHED:	The test has finished, no more results will
 *                              be produced. This is not seen outside kutf
 */
enum kutf_result_status {
	KUTF_RESULT_BENCHMARK = -3,
	KUTF_RESULT_SKIP    = -2,
	KUTF_RESULT_UNKNOWN = -1,

	KUTF_RESULT_PASS    = 0,
	KUTF_RESULT_DEBUG   = 1,
	KUTF_RESULT_INFO    = 2,
	KUTF_RESULT_WARN    = 3,
	KUTF_RESULT_FAIL    = 4,
	KUTF_RESULT_FATAL   = 5,
	KUTF_RESULT_ABORT   = 6,

	KUTF_RESULT_USERDATA      = 7,
	KUTF_RESULT_USERDATA_WAIT = 8,
	KUTF_RESULT_TEST_FINISHED = 9
};

/* The maximum size of a kutf_result_status result when
 * converted to a string
 */
#define KUTF_ERROR_MAX_NAME_SIZE 21

#ifdef __KERNEL__

#include <kutf/kutf_mem.h>
#include <linux/wait.h>

struct kutf_context;

/**
 * struct kutf_result - Represents a single test result.
 * @node:	Next result in the list of results.
 * @status:	The status summary (pass / warn / fail / etc).
 * @message:	A more verbose status message.
 */
struct kutf_result {
	struct list_head            node;
	enum kutf_result_status     status;
	const char                  *message;
};

/**
 * KUTF_RESULT_SET_WAITING_FOR_INPUT - Test is waiting for user data
 *
 * This flag is set within a struct kutf_result_set whenever the test is blocked
 * waiting for user data. Attempts to dequeue results when this flag is set
 * will cause a dummy %KUTF_RESULT_USERDATA_WAIT result to be produced. This
 * is used to output a warning message and end of file.
 */
#define KUTF_RESULT_SET_WAITING_FOR_INPUT 1

/**
 * struct kutf_result_set - Represents a set of results.
 * @results:	List head of a struct kutf_result list for storing the results
 * @waitq:	Wait queue signalled whenever new results are added.
 * @flags:	Flags see %KUTF_RESULT_SET_WAITING_FOR_INPUT
 */
struct kutf_result_set {
	struct list_head          results;
	wait_queue_head_t         waitq;
	int                       flags;
};

/**
 * kutf_create_result_set() - Create a new result set
 *                            to which results can be added.
 *
 * Return: The created result set.
 */
struct kutf_result_set *kutf_create_result_set(void);

/**
 * kutf_add_result() - Add a result to the end of an existing result set.
 *
 * @context:	The kutf context
 * @status:	The result status to add.
 * @message:	The result message to add.
 *
 * Return: 0 if the result is successfully added. -ENOMEM if allocation fails.
 */
int kutf_add_result(struct kutf_context *context,
		enum kutf_result_status status, const char *message);

/**
 * kutf_remove_result() - Remove a result from the head of a result set.
 * @set:	The result set.
 *
 * This function will block until there is a result to read. The wait is
 * interruptible, so this function will return with an ERR_PTR if interrupted.
 *
 * Return: result or ERR_PTR if interrupted
 */
struct kutf_result *kutf_remove_result(
		struct kutf_result_set *set);

/**
 * kutf_destroy_result_set() - Free a previously created result set.
 *
 * @results:	The result set whose resources to free.
 */
void kutf_destroy_result_set(struct kutf_result_set *results);

/**
 * kutf_set_waiting_for_input() - The test is waiting for userdata
 *
 * @set: The result set to update
 *
 * Causes the result set to always have results and return a fake
 * %KUTF_RESULT_USERDATA_WAIT result.
 */
void kutf_set_waiting_for_input(struct kutf_result_set *set);

/**
 * kutf_clear_waiting_for_input() - The test is no longer waiting for userdata
 *
 * @set: The result set to update
 *
 * Cancels the effect of kutf_set_waiting_for_input()
 */
void kutf_clear_waiting_for_input(struct kutf_result_set *set);

#endif	/* __KERNEL__ */

#endif	/* _KERNEL_UTF_RESULTSET_H_ */
