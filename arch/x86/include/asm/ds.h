/*
 * Debug Store (DS) support
 *
 * This provides a low-level interface to the hardware's Debug Store
 * feature that is used for branch trace store (BTS) and
 * precise-event based sampling (PEBS).
 *
 * It manages:
 * - DS and BTS hardware configuration
 * - buffer overflow handling (to be done)
 * - buffer access
 *
 * It does not do:
 * - security checking (is the caller allowed to trace the task)
 * - buffer allocation (memory accounting)
 *
 *
 * Copyright (C) 2007-2009 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, 2007-2009
 */

#ifndef _ASM_X86_DS_H
#define _ASM_X86_DS_H


#include <linux/types.h>
#include <linux/init.h>
#include <linux/err.h>


#ifdef CONFIG_X86_DS

struct task_struct;
struct ds_context;
struct ds_tracer;
struct bts_tracer;
struct pebs_tracer;

typedef void (*bts_ovfl_callback_t)(struct bts_tracer *);
typedef void (*pebs_ovfl_callback_t)(struct pebs_tracer *);


/*
 * A list of features plus corresponding macros to talk about them in
 * the ds_request function's flags parameter.
 *
 * We use the enum to index an array of corresponding control bits;
 * we use the macro to index a flags bit-vector.
 */
enum ds_feature {
	dsf_bts = 0,
	dsf_bts_kernel,
#define BTS_KERNEL (1 << dsf_bts_kernel)
	/* trace kernel-mode branches */

	dsf_bts_user,
#define BTS_USER (1 << dsf_bts_user)
	/* trace user-mode branches */

	dsf_bts_overflow,
	dsf_bts_max,
	dsf_pebs = dsf_bts_max,

	dsf_pebs_max,
	dsf_ctl_max = dsf_pebs_max,
	dsf_bts_timestamps = dsf_ctl_max,
#define BTS_TIMESTAMPS (1 << dsf_bts_timestamps)
	/* add timestamps into BTS trace */

#define BTS_USER_FLAGS (BTS_KERNEL | BTS_USER | BTS_TIMESTAMPS)
};


/*
 * Request BTS or PEBS
 *
 * Due to alignement constraints, the actual buffer may be slightly
 * smaller than the requested or provided buffer.
 *
 * Returns a pointer to a tracer structure on success, or
 * ERR_PTR(errcode) on failure.
 *
 * The interrupt threshold is independent from the overflow callback
 * to allow users to use their own overflow interrupt handling mechanism.
 *
 * The function might sleep.
 *
 * task: the task to request recording for
 * cpu:  the cpu to request recording for
 * base: the base pointer for the (non-pageable) buffer;
 * size: the size of the provided buffer in bytes
 * ovfl: pointer to a function to be called on buffer overflow;
 *       NULL if cyclic buffer requested
 * th: the interrupt threshold in records from the end of the buffer;
 *     -1 if no interrupt threshold is requested.
 * flags: a bit-mask of the above flags
 */
extern struct bts_tracer *ds_request_bts_task(struct task_struct *task,
					      void *base, size_t size,
					      bts_ovfl_callback_t ovfl,
					      size_t th, unsigned int flags);
extern struct bts_tracer *ds_request_bts_cpu(int cpu, void *base, size_t size,
					     bts_ovfl_callback_t ovfl,
					     size_t th, unsigned int flags);
extern struct pebs_tracer *ds_request_pebs_task(struct task_struct *task,
						void *base, size_t size,
						pebs_ovfl_callback_t ovfl,
						size_t th, unsigned int flags);
extern struct pebs_tracer *ds_request_pebs_cpu(int cpu,
					       void *base, size_t size,
					       pebs_ovfl_callback_t ovfl,
					       size_t th, unsigned int flags);

/*
 * Release BTS or PEBS resources
 * Suspend and resume BTS or PEBS tracing
 *
 * Must be called with irq's enabled.
 *
 * tracer: the tracer handle returned from ds_request_~()
 */
extern void ds_release_bts(struct bts_tracer *tracer);
extern void ds_suspend_bts(struct bts_tracer *tracer);
extern void ds_resume_bts(struct bts_tracer *tracer);
extern void ds_release_pebs(struct pebs_tracer *tracer);
extern void ds_suspend_pebs(struct pebs_tracer *tracer);
extern void ds_resume_pebs(struct pebs_tracer *tracer);

/*
 * Release BTS or PEBS resources
 * Suspend and resume BTS or PEBS tracing
 *
 * Cpu tracers must call this on the traced cpu.
 * Task tracers must call ds_release_~_noirq() for themselves.
 *
 * May be called with irq's disabled.
 *
 * Returns 0 if successful;
 * -EPERM if the cpu tracer does not trace the current cpu.
 * -EPERM if the task tracer does not trace itself.
 *
 * tracer: the tracer handle returned from ds_request_~()
 */
extern int ds_release_bts_noirq(struct bts_tracer *tracer);
extern int ds_suspend_bts_noirq(struct bts_tracer *tracer);
extern int ds_resume_bts_noirq(struct bts_tracer *tracer);
extern int ds_release_pebs_noirq(struct pebs_tracer *tracer);
extern int ds_suspend_pebs_noirq(struct pebs_tracer *tracer);
extern int ds_resume_pebs_noirq(struct pebs_tracer *tracer);


/*
 * The raw DS buffer state as it is used for BTS and PEBS recording.
 *
 * This is the low-level, arch-dependent interface for working
 * directly on the raw trace data.
 */
struct ds_trace {
	/* the number of bts/pebs records */
	size_t n;
	/* the size of a bts/pebs record in bytes */
	size_t size;
	/* pointers into the raw buffer:
	   - to the first entry */
	void *begin;
	/* - one beyond the last entry */
	void *end;
	/* - one beyond the newest entry */
	void *top;
	/* - the interrupt threshold */
	void *ith;
	/* flags given on ds_request() */
	unsigned int flags;
};

/*
 * An arch-independent view on branch trace data.
 */
enum bts_qualifier {
	bts_invalid,
#define BTS_INVALID bts_invalid

	bts_branch,
#define BTS_BRANCH bts_branch

	bts_task_arrives,
#define BTS_TASK_ARRIVES bts_task_arrives

	bts_task_departs,
#define BTS_TASK_DEPARTS bts_task_departs

	bts_qual_bit_size = 4,
	bts_qual_max = (1 << bts_qual_bit_size),
};

struct bts_struct {
	__u64 qualifier;
	union {
		/* BTS_BRANCH */
		struct {
			__u64 from;
			__u64 to;
		} lbr;
		/* BTS_TASK_ARRIVES or BTS_TASK_DEPARTS */
		struct {
			__u64 clock;
			pid_t pid;
		} event;
	} variant;
};


/*
 * The BTS state.
 *
 * This gives access to the raw DS state and adds functions to provide
 * an arch-independent view of the BTS data.
 */
struct bts_trace {
	struct ds_trace ds;

	int (*read)(struct bts_tracer *tracer, const void *at,
		    struct bts_struct *out);
	int (*write)(struct bts_tracer *tracer, const struct bts_struct *in);
};


/*
 * The PEBS state.
 *
 * This gives access to the raw DS state and the PEBS-specific counter
 * reset value.
 */
struct pebs_trace {
	struct ds_trace ds;

	/* the number of valid counters in the below array */
	unsigned int counters;

#define MAX_PEBS_COUNTERS 4
	/* the counter reset value */
	unsigned long long counter_reset[MAX_PEBS_COUNTERS];
};


/*
 * Read the BTS or PEBS trace.
 *
 * Returns a view on the trace collected for the parameter tracer.
 *
 * The view remains valid as long as the traced task is not running or
 * the tracer is suspended.
 * Writes into the trace buffer are not reflected.
 *
 * tracer: the tracer handle returned from ds_request_~()
 */
extern const struct bts_trace *ds_read_bts(struct bts_tracer *tracer);
extern const struct pebs_trace *ds_read_pebs(struct pebs_tracer *tracer);


/*
 * Reset the write pointer of the BTS/PEBS buffer.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_~()
 */
extern int ds_reset_bts(struct bts_tracer *tracer);
extern int ds_reset_pebs(struct pebs_tracer *tracer);

/*
 * Set the PEBS counter reset value.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_pebs()
 * counter: the index of the counter
 * value: the new counter reset value
 */
extern int ds_set_pebs_reset(struct pebs_tracer *tracer,
			     unsigned int counter, u64 value);

/*
 * Initialization
 */
struct cpuinfo_x86;
extern void __cpuinit ds_init_intel(struct cpuinfo_x86 *);

/*
 * Context switch work
 */
extern void ds_switch_to(struct task_struct *prev, struct task_struct *next);

#else /* CONFIG_X86_DS */

struct cpuinfo_x86;
static inline void __cpuinit ds_init_intel(struct cpuinfo_x86 *ignored) {}
static inline void ds_switch_to(struct task_struct *prev,
				struct task_struct *next) {}

#endif /* CONFIG_X86_DS */
#endif /* _ASM_X86_DS_H */
