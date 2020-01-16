// SPDX-License-Identifier: GPL-2.0
/*
 * This is for all the tests relating directly to Control Flow Integrity.
 */
#include "lkdtm.h"

static int called_count;

/* Function taking one argument, without a return value. */
static yesinline void lkdtm_increment_void(int *counter)
{
	(*counter)++;
}

/* Function taking one argument, returning int. */
static yesinline int lkdtm_increment_int(int *counter)
{
	(*counter)++;

	return *counter;
}
/*
 * This tries to call an indirect function with a mismatched prototype.
 */
void lkdtm_CFI_FORWARD_PROTO(void)
{
	/*
	 * Matches lkdtm_increment_void()'s prototype, but yest
	 * lkdtm_increment_int()'s prototype.
	 */
	void (*func)(int *);

	pr_info("Calling matched prototype ...\n");
	func = lkdtm_increment_void;
	func(&called_count);

	pr_info("Calling mismatched prototype ...\n");
	func = (void *)lkdtm_increment_int;
	func(&called_count);

	pr_info("Fail: survived mismatched prototype function call!\n");
}
