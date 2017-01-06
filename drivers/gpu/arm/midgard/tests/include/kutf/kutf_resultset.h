/*
 *
 * (C) COPYRIGHT 2014, 2017 ARM Limited. All rights reserved.
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
 * @KUTF_RESULT_COUNT:		The current number of possible status messages.
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

	KUTF_RESULT_COUNT
};

/* The maximum size of a kutf_result_status result when
 * converted to a string
 */
#define KUTF_ERROR_MAX_NAME_SIZE 21

#ifdef __KERNEL__

#include <kutf/kutf_mem.h>

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
 * kutf_create_result_set() - Create a new result set
 *                            to which results can be added.
 *
 * Return: The created resultset.
 */
struct kutf_result_set *kutf_create_result_set(void);

/**
 * kutf_add_result() - Add a result to the end of an existing resultset.
 *
 * @mempool:	The memory pool to allocate the result storage from.
 * @set:	The resultset to add the result to.
 * @status:	The result status to add.
 * @message:	The result message to add.
 */
void kutf_add_result(struct kutf_mempool *mempool, struct kutf_result_set *set,
		enum kutf_result_status status, const char *message);

/**
 * kutf_remove_result() - Remove a result from the head of a resultset.
 * @set:	The resultset.
 *
 * Return: result or NULL if there are no further results in the resultset.
 */
struct kutf_result *kutf_remove_result(
		struct kutf_result_set *set);

/**
 * kutf_destroy_result_set() - Free a previously created resultset.
 *
 * @results:	The result set whose resources to free.
 */
void kutf_destroy_result_set(struct kutf_result_set *results);

#endif	/* __KERNEL__ */

#endif	/* _KERNEL_UTF_RESULTSET_H_ */
