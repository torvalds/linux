// SPDX-License-Identifier: GPL-2.0
/*
 * This is for all the tests related to refcount bugs (e.g. overflow,
 * underflow, reaching zero untested, etc).
 */
#include "lkdtm.h"
#include <linux/refcount.h>

static void overflow_check(refcount_t *ref)
{
	switch (refcount_read(ref)) {
	case REFCOUNT_SATURATED:
		pr_info("Overflow detected: saturated\n");
		break;
	case REFCOUNT_MAX:
		pr_warn("Overflow detected: unsafely reset to max\n");
		break;
	default:
		pr_err("Fail: refcount wrapped to %d\n", refcount_read(ref));
	}
}

/*
 * A refcount_inc() above the maximum value of the refcount implementation,
 * should at least saturate, and at most also WARN.
 */
static void lkdtm_REFCOUNT_INC_OVERFLOW(void)
{
	refcount_t over = REFCOUNT_INIT(REFCOUNT_MAX - 1);

	pr_info("attempting good refcount_inc() without overflow\n");
	refcount_dec(&over);
	refcount_inc(&over);

	pr_info("attempting bad refcount_inc() overflow\n");
	refcount_inc(&over);
	refcount_inc(&over);

	overflow_check(&over);
}

/* refcount_add() should behave just like refcount_inc() above. */
static void lkdtm_REFCOUNT_ADD_OVERFLOW(void)
{
	refcount_t over = REFCOUNT_INIT(REFCOUNT_MAX - 1);

	pr_info("attempting good refcount_add() without overflow\n");
	refcount_dec(&over);
	refcount_dec(&over);
	refcount_dec(&over);
	refcount_dec(&over);
	refcount_add(4, &over);

	pr_info("attempting bad refcount_add() overflow\n");
	refcount_add(4, &over);

	overflow_check(&over);
}

/* refcount_inc_not_zero() should behave just like refcount_inc() above. */
static void lkdtm_REFCOUNT_INC_NOT_ZERO_OVERFLOW(void)
{
	refcount_t over = REFCOUNT_INIT(REFCOUNT_MAX);

	pr_info("attempting bad refcount_inc_not_zero() overflow\n");
	if (!refcount_inc_not_zero(&over))
		pr_warn("Weird: refcount_inc_not_zero() reported zero\n");

	overflow_check(&over);
}

/* refcount_add_not_zero() should behave just like refcount_inc() above. */
static void lkdtm_REFCOUNT_ADD_NOT_ZERO_OVERFLOW(void)
{
	refcount_t over = REFCOUNT_INIT(REFCOUNT_MAX);

	pr_info("attempting bad refcount_add_not_zero() overflow\n");
	if (!refcount_add_not_zero(6, &over))
		pr_warn("Weird: refcount_add_not_zero() reported zero\n");

	overflow_check(&over);
}

static void check_zero(refcount_t *ref)
{
	switch (refcount_read(ref)) {
	case REFCOUNT_SATURATED:
		pr_info("Zero detected: saturated\n");
		break;
	case REFCOUNT_MAX:
		pr_warn("Zero detected: unsafely reset to max\n");
		break;
	case 0:
		pr_warn("Still at zero: refcount_inc/add() must not inc-from-0\n");
		break;
	default:
		pr_err("Fail: refcount went crazy: %d\n", refcount_read(ref));
	}
}

/*
 * A refcount_dec(), as opposed to a refcount_dec_and_test(), when it hits
 * zero it should either saturate (when inc-from-zero isn't protected)
 * or stay at zero (when inc-from-zero is protected) and should WARN for both.
 */
static void lkdtm_REFCOUNT_DEC_ZERO(void)
{
	refcount_t zero = REFCOUNT_INIT(2);

	pr_info("attempting good refcount_dec()\n");
	refcount_dec(&zero);

	pr_info("attempting bad refcount_dec() to zero\n");
	refcount_dec(&zero);

	check_zero(&zero);
}

static void check_negative(refcount_t *ref, int start)
{
	/*
	 * refcount_t refuses to move a refcount at all on an
	 * over-sub, so we have to track our starting position instead of
	 * looking only at zero-pinning.
	 */
	if (refcount_read(ref) == start) {
		pr_warn("Still at %d: refcount_inc/add() must not inc-from-0\n",
			start);
		return;
	}

	switch (refcount_read(ref)) {
	case REFCOUNT_SATURATED:
		pr_info("Negative detected: saturated\n");
		break;
	case REFCOUNT_MAX:
		pr_warn("Negative detected: unsafely reset to max\n");
		break;
	default:
		pr_err("Fail: refcount went crazy: %d\n", refcount_read(ref));
	}
}

/* A refcount_dec() going negative should saturate and may WARN. */
static void lkdtm_REFCOUNT_DEC_NEGATIVE(void)
{
	refcount_t neg = REFCOUNT_INIT(0);

	pr_info("attempting bad refcount_dec() below zero\n");
	refcount_dec(&neg);

	check_negative(&neg, 0);
}

/*
 * A refcount_dec_and_test() should act like refcount_dec() above when
 * going negative.
 */
static void lkdtm_REFCOUNT_DEC_AND_TEST_NEGATIVE(void)
{
	refcount_t neg = REFCOUNT_INIT(0);

	pr_info("attempting bad refcount_dec_and_test() below zero\n");
	if (refcount_dec_and_test(&neg))
		pr_warn("Weird: refcount_dec_and_test() reported zero\n");

	check_negative(&neg, 0);
}

/*
 * A refcount_sub_and_test() should act like refcount_dec_and_test()
 * above when going negative.
 */
static void lkdtm_REFCOUNT_SUB_AND_TEST_NEGATIVE(void)
{
	refcount_t neg = REFCOUNT_INIT(3);

	pr_info("attempting bad refcount_sub_and_test() below zero\n");
	if (refcount_sub_and_test(5, &neg))
		pr_warn("Weird: refcount_sub_and_test() reported zero\n");

	check_negative(&neg, 3);
}

/*
 * A refcount_sub_and_test() by zero when the counter is at zero should act like
 * refcount_sub_and_test() above when going negative.
 */
static void lkdtm_REFCOUNT_SUB_AND_TEST_ZERO(void)
{
	refcount_t neg = REFCOUNT_INIT(0);

	pr_info("attempting bad refcount_sub_and_test() at zero\n");
	if (refcount_sub_and_test(0, &neg))
		pr_warn("Weird: refcount_sub_and_test() reported zero\n");

	check_negative(&neg, 0);
}

static void check_from_zero(refcount_t *ref)
{
	switch (refcount_read(ref)) {
	case 0:
		pr_info("Zero detected: stayed at zero\n");
		break;
	case REFCOUNT_SATURATED:
		pr_info("Zero detected: saturated\n");
		break;
	case REFCOUNT_MAX:
		pr_warn("Zero detected: unsafely reset to max\n");
		break;
	default:
		pr_info("Fail: zero not detected, incremented to %d\n",
			refcount_read(ref));
	}
}

/*
 * A refcount_inc() from zero should pin to zero or saturate and may WARN.
 */
static void lkdtm_REFCOUNT_INC_ZERO(void)
{
	refcount_t zero = REFCOUNT_INIT(0);

	pr_info("attempting safe refcount_inc_not_zero() from zero\n");
	if (!refcount_inc_not_zero(&zero)) {
		pr_info("Good: zero detected\n");
		if (refcount_read(&zero) == 0)
			pr_info("Correctly stayed at zero\n");
		else
			pr_err("Fail: refcount went past zero!\n");
	} else {
		pr_err("Fail: Zero not detected!?\n");
	}

	pr_info("attempting bad refcount_inc() from zero\n");
	refcount_inc(&zero);

	check_from_zero(&zero);
}

/*
 * A refcount_add() should act like refcount_inc() above when starting
 * at zero.
 */
static void lkdtm_REFCOUNT_ADD_ZERO(void)
{
	refcount_t zero = REFCOUNT_INIT(0);

	pr_info("attempting safe refcount_add_not_zero() from zero\n");
	if (!refcount_add_not_zero(3, &zero)) {
		pr_info("Good: zero detected\n");
		if (refcount_read(&zero) == 0)
			pr_info("Correctly stayed at zero\n");
		else
			pr_err("Fail: refcount went past zero\n");
	} else {
		pr_err("Fail: Zero not detected!?\n");
	}

	pr_info("attempting bad refcount_add() from zero\n");
	refcount_add(3, &zero);

	check_from_zero(&zero);
}

static void check_saturated(refcount_t *ref)
{
	switch (refcount_read(ref)) {
	case REFCOUNT_SATURATED:
		pr_info("Saturation detected: still saturated\n");
		break;
	case REFCOUNT_MAX:
		pr_warn("Saturation detected: unsafely reset to max\n");
		break;
	default:
		pr_err("Fail: refcount went crazy: %d\n", refcount_read(ref));
	}
}

/*
 * A refcount_inc() from a saturated value should at most warn about
 * being saturated already.
 */
static void lkdtm_REFCOUNT_INC_SATURATED(void)
{
	refcount_t sat = REFCOUNT_INIT(REFCOUNT_SATURATED);

	pr_info("attempting bad refcount_inc() from saturated\n");
	refcount_inc(&sat);

	check_saturated(&sat);
}

/* Should act like refcount_inc() above from saturated. */
static void lkdtm_REFCOUNT_DEC_SATURATED(void)
{
	refcount_t sat = REFCOUNT_INIT(REFCOUNT_SATURATED);

	pr_info("attempting bad refcount_dec() from saturated\n");
	refcount_dec(&sat);

	check_saturated(&sat);
}

/* Should act like refcount_inc() above from saturated. */
static void lkdtm_REFCOUNT_ADD_SATURATED(void)
{
	refcount_t sat = REFCOUNT_INIT(REFCOUNT_SATURATED);

	pr_info("attempting bad refcount_dec() from saturated\n");
	refcount_add(8, &sat);

	check_saturated(&sat);
}

/* Should act like refcount_inc() above from saturated. */
static void lkdtm_REFCOUNT_INC_NOT_ZERO_SATURATED(void)
{
	refcount_t sat = REFCOUNT_INIT(REFCOUNT_SATURATED);

	pr_info("attempting bad refcount_inc_not_zero() from saturated\n");
	if (!refcount_inc_not_zero(&sat))
		pr_warn("Weird: refcount_inc_not_zero() reported zero\n");

	check_saturated(&sat);
}

/* Should act like refcount_inc() above from saturated. */
static void lkdtm_REFCOUNT_ADD_NOT_ZERO_SATURATED(void)
{
	refcount_t sat = REFCOUNT_INIT(REFCOUNT_SATURATED);

	pr_info("attempting bad refcount_add_not_zero() from saturated\n");
	if (!refcount_add_not_zero(7, &sat))
		pr_warn("Weird: refcount_add_not_zero() reported zero\n");

	check_saturated(&sat);
}

/* Should act like refcount_inc() above from saturated. */
static void lkdtm_REFCOUNT_DEC_AND_TEST_SATURATED(void)
{
	refcount_t sat = REFCOUNT_INIT(REFCOUNT_SATURATED);

	pr_info("attempting bad refcount_dec_and_test() from saturated\n");
	if (refcount_dec_and_test(&sat))
		pr_warn("Weird: refcount_dec_and_test() reported zero\n");

	check_saturated(&sat);
}

/* Should act like refcount_inc() above from saturated. */
static void lkdtm_REFCOUNT_SUB_AND_TEST_SATURATED(void)
{
	refcount_t sat = REFCOUNT_INIT(REFCOUNT_SATURATED);

	pr_info("attempting bad refcount_sub_and_test() from saturated\n");
	if (refcount_sub_and_test(8, &sat))
		pr_warn("Weird: refcount_sub_and_test() reported zero\n");

	check_saturated(&sat);
}

/* Used to time the existing atomic_t when used for reference counting */
static void lkdtm_ATOMIC_TIMING(void)
{
	unsigned int i;
	atomic_t count = ATOMIC_INIT(1);

	for (i = 0; i < INT_MAX - 1; i++)
		atomic_inc(&count);

	for (i = INT_MAX; i > 0; i--)
		if (atomic_dec_and_test(&count))
			break;

	if (i != 1)
		pr_err("atomic timing: out of sync up/down cycle: %u\n", i - 1);
	else
		pr_info("atomic timing: done\n");
}

/*
 * This can be compared to ATOMIC_TIMING when implementing fast refcount
 * protections. Looking at the number of CPU cycles tells the real story
 * about performance. For example:
 *    cd /sys/kernel/debug/provoke-crash
 *    perf stat -B -- cat <(echo REFCOUNT_TIMING) > DIRECT
 */
static void lkdtm_REFCOUNT_TIMING(void)
{
	unsigned int i;
	refcount_t count = REFCOUNT_INIT(1);

	for (i = 0; i < INT_MAX - 1; i++)
		refcount_inc(&count);

	for (i = INT_MAX; i > 0; i--)
		if (refcount_dec_and_test(&count))
			break;

	if (i != 1)
		pr_err("refcount: out of sync up/down cycle: %u\n", i - 1);
	else
		pr_info("refcount timing: done\n");
}

static struct crashtype crashtypes[] = {
	CRASHTYPE(REFCOUNT_INC_OVERFLOW),
	CRASHTYPE(REFCOUNT_ADD_OVERFLOW),
	CRASHTYPE(REFCOUNT_INC_NOT_ZERO_OVERFLOW),
	CRASHTYPE(REFCOUNT_ADD_NOT_ZERO_OVERFLOW),
	CRASHTYPE(REFCOUNT_DEC_ZERO),
	CRASHTYPE(REFCOUNT_DEC_NEGATIVE),
	CRASHTYPE(REFCOUNT_DEC_AND_TEST_NEGATIVE),
	CRASHTYPE(REFCOUNT_SUB_AND_TEST_NEGATIVE),
	CRASHTYPE(REFCOUNT_SUB_AND_TEST_ZERO),
	CRASHTYPE(REFCOUNT_INC_ZERO),
	CRASHTYPE(REFCOUNT_ADD_ZERO),
	CRASHTYPE(REFCOUNT_INC_SATURATED),
	CRASHTYPE(REFCOUNT_DEC_SATURATED),
	CRASHTYPE(REFCOUNT_ADD_SATURATED),
	CRASHTYPE(REFCOUNT_INC_NOT_ZERO_SATURATED),
	CRASHTYPE(REFCOUNT_ADD_NOT_ZERO_SATURATED),
	CRASHTYPE(REFCOUNT_DEC_AND_TEST_SATURATED),
	CRASHTYPE(REFCOUNT_SUB_AND_TEST_SATURATED),
	CRASHTYPE(ATOMIC_TIMING),
	CRASHTYPE(REFCOUNT_TIMING),
};

struct crashtype_category refcount_crashtypes = {
	.crashtypes = crashtypes,
	.len	    = ARRAY_SIZE(crashtypes),
};
