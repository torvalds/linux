/*
 * Debug Store (DS) support
 *
 * This provides a low-level interface to the hardware's Debug Store
 * feature that is used for branch trace store (BTS) and
 * precise-event based sampling (PEBS).
 *
 * It manages:
 * - per-thread and per-cpu allocation of BTS and PEBS
 * - buffer overflow handling (to be done)
 * - buffer access
 *
 * It assumes:
 * - get_task_struct on all traced tasks
 * - current is allowed to trace tasks
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, 2007-2008
 */

#ifndef _ASM_X86_DS_H
#define _ASM_X86_DS_H


#include <linux/types.h>
#include <linux/init.h>
#include <linux/err.h>


#ifdef CONFIG_X86_DS

struct task_struct;
struct ds_tracer;
struct bts_tracer;
struct pebs_tracer;

typedef void (*bts_ovfl_callback_t)(struct bts_tracer *);
typedef void (*pebs_ovfl_callback_t)(struct pebs_tracer *);

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
 * task: the task to request recording for;
 *       NULL for per-cpu recording on the current cpu
 * base: the base pointer for the (non-pageable) buffer;
 * size: the size of the provided buffer in bytes
 * ovfl: pointer to a function to be called on buffer overflow;
 *       NULL if cyclic buffer requested
 * th: the interrupt threshold in records from the end of the buffer;
 *     -1 if no interrupt threshold is requested.
 */
extern struct bts_tracer *ds_request_bts(struct task_struct *task,
					 void *base, size_t size,
					 bts_ovfl_callback_t ovfl, size_t th);
extern struct pebs_tracer *ds_request_pebs(struct task_struct *task,
					   void *base, size_t size,
					   pebs_ovfl_callback_t ovfl,
					   size_t th);

/*
 * Release BTS or PEBS resources
 *
 * Returns 0 on success; -Eerrno otherwise
 *
 * tracer: the tracer handle returned from ds_request_~()
 */
extern int ds_release_bts(struct bts_tracer *tracer);
extern int ds_release_pebs(struct pebs_tracer *tracer);

/*
 * Get the (array) index of the write pointer.
 * (assuming an array of BTS/PEBS records)
 *
 * Returns 0 on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_~()
 * pos (out): will hold the result
 */
extern int ds_get_bts_index(struct bts_tracer *tracer, size_t *pos);
extern int ds_get_pebs_index(struct pebs_tracer *tracer, size_t *pos);

/*
 * Get the (array) index one record beyond the end of the array.
 * (assuming an array of BTS/PEBS records)
 *
 * Returns 0 on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_~()
 * pos (out): will hold the result
 */
extern int ds_get_bts_end(struct bts_tracer *tracer, size_t *pos);
extern int ds_get_pebs_end(struct pebs_tracer *tracer, size_t *pos);

/*
 * Provide a pointer to the BTS/PEBS record at parameter index.
 * (assuming an array of BTS/PEBS records)
 *
 * The pointer points directly into the buffer. The user is
 * responsible for copying the record.
 *
 * Returns the size of a single record on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_~()
 * index: the index of the requested record
 * record (out): pointer to the requested record
 */
extern int ds_access_bts(struct bts_tracer *tracer,
			 size_t index, const void **record);
extern int ds_access_pebs(struct pebs_tracer *tracer,
			  size_t index, const void **record);

/*
 * Write one or more BTS/PEBS records at the write pointer index and
 * advance the write pointer.
 *
 * If size is not a multiple of the record size, trailing bytes are
 * zeroed out.
 *
 * May result in one or more overflow notifications.
 *
 * If called during overflow handling, that is, with index >=
 * interrupt threshold, the write will wrap around.
 *
 * An overflow notification is given if and when the interrupt
 * threshold is reached during or after the write.
 *
 * Returns the number of bytes written or -Eerrno.
 *
 * tracer: the tracer handle returned from ds_request_~()
 * buffer: the buffer to write
 * size: the size of the buffer
 */
extern int ds_write_bts(struct bts_tracer *tracer,
			const void *buffer, size_t size);
extern int ds_write_pebs(struct pebs_tracer *tracer,
			 const void *buffer, size_t size);

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
 * Clear the BTS/PEBS buffer and reset the write pointer.
 * The entire buffer will be zeroed out.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_~()
 */
extern int ds_clear_bts(struct bts_tracer *tracer);
extern int ds_clear_pebs(struct pebs_tracer *tracer);

/*
 * Provide the PEBS counter reset value.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_pebs()
 * value (out): the counter reset value
 */
extern int ds_get_pebs_reset(struct pebs_tracer *tracer, u64 *value);

/*
 * Set the PEBS counter reset value.
 *
 * Returns 0 on success; -Eerrno on error
 *
 * tracer: the tracer handle returned from ds_request_pebs()
 * value: the new counter reset value
 */
extern int ds_set_pebs_reset(struct pebs_tracer *tracer, u64 value);

/*
 * Initialization
 */
struct cpuinfo_x86;
extern void __cpuinit ds_init_intel(struct cpuinfo_x86 *);



/*
 * The DS context - part of struct thread_struct.
 */
#define MAX_SIZEOF_DS (12 * 8)

struct ds_context {
	/* pointer to the DS configuration; goes into MSR_IA32_DS_AREA */
	unsigned char ds[MAX_SIZEOF_DS];
	/* the owner of the BTS and PEBS configuration, respectively */
	struct ds_tracer  *owner[2];
	/* use count */
	unsigned long count;
	/* a pointer to the context location inside the thread_struct
	 * or the per_cpu context array */
	struct ds_context **this;
	/* a pointer to the task owning this context, or NULL, if the
	 * context is owned by a cpu */
	struct task_struct *task;
};

/* called by exit_thread() to free leftover contexts */
extern void ds_free(struct ds_context *context);

#else /* CONFIG_X86_DS */

struct cpuinfo_x86;
static inline void __cpuinit ds_init_intel(struct cpuinfo_x86 *ignored) {}

#endif /* CONFIG_X86_DS */
#endif /* _ASM_X86_DS_H */
