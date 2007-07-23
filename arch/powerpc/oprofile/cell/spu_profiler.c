/*
 * Cell Broadband Engine OProfile Support
 *
 * (C) Copyright IBM Corporation 2006
 *
 * Authors: Maynard Johnson <maynardj@us.ibm.com>
 *	    Carl Love <carll@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/hrtimer.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <asm/cell-pmu.h>
#include "pr_util.h"

#define TRACE_ARRAY_SIZE 1024
#define SCALE_SHIFT 14

static u32 *samples;

static int spu_prof_running;
static unsigned int profiling_interval;

#define NUM_SPU_BITS_TRBUF 16
#define SPUS_PER_TB_ENTRY   4
#define SPUS_PER_NODE	     8

#define SPU_PC_MASK	     0xFFFF

static DEFINE_SPINLOCK(sample_array_lock);
unsigned long sample_array_lock_flags;

void set_spu_profiling_frequency(unsigned int freq_khz, unsigned int cycles_reset)
{
	unsigned long ns_per_cyc;

	if (!freq_khz)
		freq_khz = ppc_proc_freq/1000;

	/* To calculate a timeout in nanoseconds, the basic
	 * formula is ns = cycles_reset * (NSEC_PER_SEC / cpu frequency).
	 * To avoid floating point math, we use the scale math
	 * technique as described in linux/jiffies.h.  We use
	 * a scale factor of SCALE_SHIFT, which provides 4 decimal places
	 * of precision.  This is close enough for the purpose at hand.
	 *
	 * The value of the timeout should be small enough that the hw
	 * trace buffer will not get more then about 1/3 full for the
	 * maximum user specified (the LFSR value) hw sampling frequency.
	 * This is to ensure the trace buffer will never fill even if the
	 * kernel thread scheduling varies under a heavy system load.
	 */

	ns_per_cyc = (USEC_PER_SEC << SCALE_SHIFT)/freq_khz;
	profiling_interval = (ns_per_cyc * cycles_reset) >> SCALE_SHIFT;

}

/*
 * Extract SPU PC from trace buffer entry
 */
static void spu_pc_extract(int cpu, int entry)
{
	/* the trace buffer is 128 bits */
	u64 trace_buffer[2];
	u64 spu_mask;
	int spu;

	spu_mask = SPU_PC_MASK;

	/* Each SPU PC is 16 bits; hence, four spus in each of
	 * the two 64-bit buffer entries that make up the
	 * 128-bit trace_buffer entry.	Process two 64-bit values
	 * simultaneously.
	 * trace[0] SPU PC contents are: 0 1 2 3
	 * trace[1] SPU PC contents are: 4 5 6 7
	 */

	cbe_read_trace_buffer(cpu, trace_buffer);

	for (spu = SPUS_PER_TB_ENTRY-1; spu >= 0; spu--) {
		/* spu PC trace entry is upper 16 bits of the
		 * 18 bit SPU program counter
		 */
		samples[spu * TRACE_ARRAY_SIZE + entry]
			= (spu_mask & trace_buffer[0]) << 2;
		samples[(spu + SPUS_PER_TB_ENTRY) * TRACE_ARRAY_SIZE + entry]
			= (spu_mask & trace_buffer[1]) << 2;

		trace_buffer[0] = trace_buffer[0] >> NUM_SPU_BITS_TRBUF;
		trace_buffer[1] = trace_buffer[1] >> NUM_SPU_BITS_TRBUF;
	}
}

static int cell_spu_pc_collection(int cpu)
{
	u32 trace_addr;
	int entry;

	/* process the collected SPU PC for the node */

	entry = 0;

	trace_addr = cbe_read_pm(cpu, trace_address);
	while (!(trace_addr & CBE_PM_TRACE_BUF_EMPTY)) {
		/* there is data in the trace buffer to process */
		spu_pc_extract(cpu, entry);

		entry++;

		if (entry >= TRACE_ARRAY_SIZE)
			/* spu_samples is full */
			break;

		trace_addr = cbe_read_pm(cpu, trace_address);
	}

	return entry;
}


static enum hrtimer_restart profile_spus(struct hrtimer *timer)
{
	ktime_t kt;
	int cpu, node, k, num_samples, spu_num;

	if (!spu_prof_running)
		goto stop;

	for_each_online_cpu(cpu) {
		if (cbe_get_hw_thread_id(cpu))
			continue;

		node = cbe_cpu_to_node(cpu);

		/* There should only be one kernel thread at a time processing
		 * the samples.	 In the very unlikely case that the processing
		 * is taking a very long time and multiple kernel threads are
		 * started to process the samples.  Make sure only one kernel
		 * thread is working on the samples array at a time.  The
		 * sample array must be loaded and then processed for a given
		 * cpu.	 The sample array is not per cpu.
		 */
		spin_lock_irqsave(&sample_array_lock,
				  sample_array_lock_flags);
		num_samples = cell_spu_pc_collection(cpu);

		if (num_samples == 0) {
			spin_unlock_irqrestore(&sample_array_lock,
					       sample_array_lock_flags);
			continue;
		}

		for (k = 0; k < SPUS_PER_NODE; k++) {
			spu_num = k + (node * SPUS_PER_NODE);
			spu_sync_buffer(spu_num,
					samples + (k * TRACE_ARRAY_SIZE),
					num_samples);
		}

		spin_unlock_irqrestore(&sample_array_lock,
				       sample_array_lock_flags);

	}
	smp_wmb();	/* insure spu event buffer updates are written */
			/* don't want events intermingled... */

	kt = ktime_set(0, profiling_interval);
	if (!spu_prof_running)
		goto stop;
	hrtimer_forward(timer, timer->base->get_time(), kt);
	return HRTIMER_RESTART;

 stop:
	printk(KERN_INFO "SPU_PROF: spu-prof timer ending\n");
	return HRTIMER_NORESTART;
}

static struct hrtimer timer;
/*
 * Entry point for SPU profiling.
 * NOTE:  SPU profiling is done system-wide, not per-CPU.
 *
 * cycles_reset is the count value specified by the user when
 * setting up OProfile to count SPU_CYCLES.
 */
int start_spu_profiling(unsigned int cycles_reset)
{
	ktime_t kt;

	pr_debug("timer resolution: %lu\n", TICK_NSEC);
	kt = ktime_set(0, profiling_interval);
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.expires = kt;
	timer.function = profile_spus;

	/* Allocate arrays for collecting SPU PC samples */
	samples = kzalloc(SPUS_PER_NODE *
			  TRACE_ARRAY_SIZE * sizeof(u32), GFP_KERNEL);

	if (!samples)
		return -ENOMEM;

	spu_prof_running = 1;
	hrtimer_start(&timer, kt, HRTIMER_MODE_REL);

	return 0;
}

void stop_spu_profiling(void)
{
	spu_prof_running = 0;
	hrtimer_cancel(&timer);
	kfree(samples);
	pr_debug("SPU_PROF: stop_spu_profiling issued\n");
}
