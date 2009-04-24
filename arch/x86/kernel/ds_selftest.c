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
#include <linux/smp.h>
#include <linux/cpu.h>

#include <asm/ds.h>


#define BUFFER_SIZE		521	/* Intentionally chose an odd size. */
#define SMALL_BUFFER_SIZE	24	/* A single bts entry. */

struct ds_selftest_bts_conf {
	struct bts_tracer *tracer;
	int error;
	int (*suspend)(struct bts_tracer *);
	int (*resume)(struct bts_tracer *);
};

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
	/*
	 * We allow top in [begin; end], since its not clear when the
	 * overflow adjustment happens: after the increment or before the
	 * write.
	 */
	if ((trace->ds.top < trace->ds.begin) ||
	    (trace->ds.end < trace->ds.top)) {
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
		unsigned long index;
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

static void ds_selftest_bts_cpu(void *arg)
{
	struct ds_selftest_bts_conf *conf = arg;
	const struct bts_trace *trace;
	void *top;

	if (IS_ERR(conf->tracer)) {
		conf->error = PTR_ERR(conf->tracer);
		conf->tracer = NULL;

		printk(KERN_CONT
		       "initialization failed (err: %d)...", conf->error);
		return;
	}

	/* We should meanwhile have enough trace. */
	conf->error = conf->suspend(conf->tracer);
	if (conf->error < 0)
		return;

	/* Let's see if we can access the trace. */
	trace = ds_read_bts(conf->tracer);

	conf->error = ds_selftest_bts_consistency(trace);
	if (conf->error < 0)
		return;

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
	conf->error =
		ds_selftest_bts_read(conf->tracer, trace,
				     trace->ds.begin, trace->ds.top);
	if (conf->error < 0)
		return;

	/*
	 * Let's read the trace again.
	 * Since we suspended tracing, we should get the same result.
	 */
	top = trace->ds.top;

	trace = ds_read_bts(conf->tracer);
	conf->error = ds_selftest_bts_consistency(trace);
	if (conf->error < 0)
		return;

	if (top != trace->ds.top) {
		printk(KERN_CONT "suspend not working...");
		conf->error = -1;
		return;
	}

	/* Let's collect some more trace - see if resume is working. */
	conf->error = conf->resume(conf->tracer);
	if (conf->error < 0)
		return;

	conf->error = conf->suspend(conf->tracer);
	if (conf->error < 0)
		return;

	trace = ds_read_bts(conf->tracer);

	conf->error = ds_selftest_bts_consistency(trace);
	if (conf->error < 0)
		return;

	if (trace->ds.top == top) {
		/*
		 * It is possible but highly unlikely that we got a
		 * buffer overflow and end up at exactly the same
		 * position we started from.
		 * Let's issue a warning and check the full trace.
		 */
		printk(KERN_CONT
		       "no resume progress/overflow...");

		conf->error =
			ds_selftest_bts_read(conf->tracer, trace,
					     trace->ds.begin, trace->ds.end);
	} else if (trace->ds.top < top) {
		/*
		 * We had a buffer overflow - the entire buffer should
		 * contain trace records.
		 */
		conf->error =
			ds_selftest_bts_read(conf->tracer, trace,
					     trace->ds.begin, trace->ds.end);
	} else {
		/*
		 * It is quite likely that the buffer did not overflow.
		 * Let's just check the delta trace.
		 */
		conf->error =
			ds_selftest_bts_read(conf->tracer, trace, top,
					     trace->ds.top);
	}
	if (conf->error < 0)
		return;

	conf->error = 0;
}

static int ds_suspend_bts_wrap(struct bts_tracer *tracer)
{
	ds_suspend_bts(tracer);
	return 0;
}

static int ds_resume_bts_wrap(struct bts_tracer *tracer)
{
	ds_resume_bts(tracer);
	return 0;
}

static void ds_release_bts_noirq_wrap(void *tracer)
{
	(void)ds_release_bts_noirq(tracer);
}

static int ds_selftest_bts_bad_release_noirq(int cpu,
					     struct bts_tracer *tracer)
{
	int error = -EPERM;

	/* Try to release the tracer on the wrong cpu. */
	get_cpu();
	if (cpu != smp_processor_id()) {
		error = ds_release_bts_noirq(tracer);
		if (error != -EPERM)
			printk(KERN_CONT "release on wrong cpu...");
	}
	put_cpu();

	return error ? 0 : -1;
}

static int ds_selftest_bts_bad_request_cpu(int cpu, void *buffer)
{
	struct bts_tracer *tracer;
	int error;

	/* Try to request cpu tracing while task tracing is active. */
	tracer = ds_request_bts_cpu(cpu, buffer, BUFFER_SIZE, NULL,
				    (size_t)-1, BTS_KERNEL);
	error = PTR_ERR(tracer);
	if (!IS_ERR(tracer)) {
		ds_release_bts(tracer);
		error = 0;
	}

	if (error != -EPERM)
		printk(KERN_CONT "cpu/task tracing overlap...");

	return error ? 0 : -1;
}

static int ds_selftest_bts_bad_request_task(void *buffer)
{
	struct bts_tracer *tracer;
	int error;

	/* Try to request cpu tracing while task tracing is active. */
	tracer = ds_request_bts_task(current, buffer, BUFFER_SIZE, NULL,
				    (size_t)-1, BTS_KERNEL);
	error = PTR_ERR(tracer);
	if (!IS_ERR(tracer)) {
		error = 0;
		ds_release_bts(tracer);
	}

	if (error != -EPERM)
		printk(KERN_CONT "task/cpu tracing overlap...");

	return error ? 0 : -1;
}

int ds_selftest_bts(void)
{
	struct ds_selftest_bts_conf conf;
	unsigned char buffer[BUFFER_SIZE], *small_buffer;
	unsigned long irq;
	int cpu;

	printk(KERN_INFO "[ds] bts selftest...");
	conf.error = 0;

	small_buffer = (unsigned char *)ALIGN((unsigned long)buffer, 8) + 8;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		conf.suspend = ds_suspend_bts_wrap;
		conf.resume = ds_resume_bts_wrap;
		conf.tracer =
			ds_request_bts_cpu(cpu, buffer, BUFFER_SIZE,
					   NULL, (size_t)-1, BTS_KERNEL);
		ds_selftest_bts_cpu(&conf);
		if (conf.error >= 0)
			conf.error = ds_selftest_bts_bad_request_task(buffer);
		ds_release_bts(conf.tracer);
		if (conf.error < 0)
			goto out;

		conf.suspend = ds_suspend_bts_noirq;
		conf.resume = ds_resume_bts_noirq;
		conf.tracer =
			ds_request_bts_cpu(cpu, buffer, BUFFER_SIZE,
					   NULL, (size_t)-1, BTS_KERNEL);
		smp_call_function_single(cpu, ds_selftest_bts_cpu, &conf, 1);
		if (conf.error >= 0) {
			conf.error =
				ds_selftest_bts_bad_release_noirq(cpu,
								  conf.tracer);
			/* We must not release the tracer twice. */
			if (conf.error < 0)
				conf.tracer = NULL;
		}
		if (conf.error >= 0)
			conf.error = ds_selftest_bts_bad_request_task(buffer);
		smp_call_function_single(cpu, ds_release_bts_noirq_wrap,
					 conf.tracer, 1);
		if (conf.error < 0)
			goto out;
	}

	conf.suspend = ds_suspend_bts_wrap;
	conf.resume = ds_resume_bts_wrap;
	conf.tracer =
		ds_request_bts_task(current, buffer, BUFFER_SIZE,
				    NULL, (size_t)-1, BTS_KERNEL);
	ds_selftest_bts_cpu(&conf);
	if (conf.error >= 0)
		conf.error = ds_selftest_bts_bad_request_cpu(0, buffer);
	ds_release_bts(conf.tracer);
	if (conf.error < 0)
		goto out;

	conf.suspend = ds_suspend_bts_noirq;
	conf.resume = ds_resume_bts_noirq;
	conf.tracer =
		ds_request_bts_task(current, small_buffer, SMALL_BUFFER_SIZE,
				   NULL, (size_t)-1, BTS_KERNEL);
	local_irq_save(irq);
	ds_selftest_bts_cpu(&conf);
	if (conf.error >= 0)
		conf.error = ds_selftest_bts_bad_request_cpu(0, buffer);
	ds_release_bts_noirq(conf.tracer);
	local_irq_restore(irq);
	if (conf.error < 0)
		goto out;

	conf.error = 0;
 out:
	put_online_cpus();
	printk(KERN_CONT "%s.\n", (conf.error ? "failed" : "passed"));

	return conf.error;
}

int ds_selftest_pebs(void)
{
	return 0;
}
