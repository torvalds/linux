/*
 * Debug Store support - selftest
 *
 *
 * Copyright (C) 2009 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, 2009
 */

#include "ds_selftest.h"

#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/ds.h>


#define DS_SELFTEST_BUFFER_SIZE 1021 /* Intentionally chose an odd size. */


static int ds_selftest_bts_consistency(const struct bts_trace *trace)
{
	int error = 0;

	if (!trace) {
		printk(KERN_CONT "failed to access trace...");
		/* Bail out. Other tests are pointless. */
		return -1;
	}

	if (!trace->read) {
		printk(KERN_CONT "bts read not available...");
		error = -1;
	}

	/* Do some sanity checks on the trace configuration. */
	if (!trace->ds.n) {
		printk(KERN_CONT "empty bts buffer...");
		error = -1;
	}
	if (!trace->ds.size) {
		printk(KERN_CONT "bad bts trace setup...");
		error = -1;
	}
	if (trace->ds.end !=
	    (char *)trace->ds.begin + (trace->ds.n * trace->ds.size)) {
		printk(KERN_CONT "bad bts buffer setup...");
		error = -1;
	}
	if ((trace->ds.top < trace->ds.begin) ||
	    (trace->ds.end <= trace->ds.top)) {
		printk(KERN_CONT "bts top out of bounds...");
		error = -1;
	}

	return error;
}

static int ds_selftest_bts_read(struct bts_tracer *tracer,
				const struct bts_trace *trace,
				const void *from, const void *to)
{
	const unsigned char *at;

	/*
	 * Check a few things which do not belong to this test.
	 * They should be covered by other tests.
	 */
	if (!trace)
		return -1;

	if (!trace->read)
		return -1;

	if (to < from)
		return -1;

	if (from < trace->ds.begin)
		return -1;

	if (trace->ds.end < to)
		return -1;

	if (!trace->ds.size)
		return -1;

	/* Now to the test itself. */
	for (at = from; (void *)at < to; at += trace->ds.size) {
		struct bts_struct bts;
		size_t index;
		int error;

		if (((void *)at - trace->ds.begin) % trace->ds.size) {
			printk(KERN_CONT
			       "read from non-integer index...");
			return -1;
		}
		index = ((void *)at - trace->ds.begin) / trace->ds.size;

		memset(&bts, 0, sizeof(bts));
		error = trace->read(tracer, at, &bts);
		if (error < 0) {
			printk(KERN_CONT
			       "error reading bts trace at [%lu] (0x%p)...",
			       index, at);
			return error;
		}

		switch (bts.qualifier) {
		case BTS_BRANCH:
			break;
		default:
			printk(KERN_CONT
			       "unexpected bts entry %llu at [%lu] (0x%p)...",
			       bts.qualifier, index, at);
			return -1;
		}
	}

	return 0;
}

int ds_selftest_bts(void)
{
	const struct bts_trace *trace;
	struct bts_tracer *tracer;
	int error = 0;
	void *top;
	unsigned char buffer[DS_SELFTEST_BUFFER_SIZE];

	printk(KERN_INFO "[ds] bts selftest...");

	tracer = ds_request_bts(NULL, buffer, DS_SELFTEST_BUFFER_SIZE,
				NULL, (size_t)-1, BTS_KERNEL);
	if (IS_ERR(tracer)) {
		error = PTR_ERR(tracer);
		tracer = NULL;

		printk(KERN_CONT
		       "initialization failed (err: %d)...", error);
		goto out;
	}

	/* The return should already give us enough trace. */
	ds_suspend_bts(tracer);

	/* Let's see if we can access the trace. */
	trace = ds_read_bts(tracer);

	error = ds_selftest_bts_consistency(trace);
	if (error < 0)
		goto out;

	/* If everything went well, we should have a few trace entries. */
	if (trace->ds.top == trace->ds.begin) {
		/*
		 * It is possible but highly unlikely that we got a
		 * buffer overflow and end up at exactly the same
		 * position we started from.
		 * Let's issue a warning, but continue.
		 */
		printk(KERN_CONT "no trace/overflow...");
	}

	/* Let's try to read the trace we collected. */
	error = ds_selftest_bts_read(tracer, trace,
				     trace->ds.begin, trace->ds.top);
	if (error < 0)
		goto out;

	/*
	 * Let's read the trace again.
	 * Since we suspended tracing, we should get the same result.
	 */
	top = trace->ds.top;

	trace = ds_read_bts(tracer);
	error = ds_selftest_bts_consistency(trace);
	if (error < 0)
		goto out;

	if (top != trace->ds.top) {
		printk(KERN_CONT "suspend not working...");
		error = -1;
		goto out;
	}

	/* Let's collect some more trace - see if resume is working. */
	ds_resume_bts(tracer);
	ds_suspend_bts(tracer);

	trace = ds_read_bts(tracer);

	error = ds_selftest_bts_consistency(trace);
	if (error < 0)
		goto out;

	if (trace->ds.top == top) {
		/*
		 * It is possible but highly unlikely that we got a
		 * buffer overflow and end up at exactly the same
		 * position we started from.
		 * Let's issue a warning and check the full trace.
		 */
		printk(KERN_CONT
		       "no resume progress/overflow...");

		error = ds_selftest_bts_read(tracer, trace,
					     trace->ds.begin, trace->ds.end);
	} else if (trace->ds.top < top) {
		/*
		 * We had a buffer overflow - the entire buffer should
		 * contain trace records.
		 */
		error = ds_selftest_bts_read(tracer, trace,
					     trace->ds.begin, trace->ds.end);
	} else {
		/*
		 * It is quite likely that the buffer did not overflow.
		 * Let's just check the delta trace.
		 */
		error = ds_selftest_bts_read(tracer, trace,
					     top, trace->ds.top);
	}
	if (error < 0)
		goto out;

	error = 0;

	/* The final test: release the tracer while tracing is suspended. */
 out:
	ds_release_bts(tracer);

	printk(KERN_CONT "%s.\n", (error ? "failed" : "passed"));

	return error;
}

int ds_selftest_pebs(void)
{
	return 0;
}
