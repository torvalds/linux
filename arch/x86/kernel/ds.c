/*
 * Debug Store support
 *
 * This provides a low-level interface to the hardware's Debug Store
 * feature that is used for branch trace store (BTS) and
 * precise-event based sampling (PEBS).
 *
 * It manages:
 * - per-thread and per-cpu allocation of BTS and PEBS
 * - buffer memory allocation (optional)
 * - buffer overflow handling
 * - buffer access
 *
 * It assumes:
 * - get_task_struct on all parameter tasks
 * - current is allowed to trace parameter tasks
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, 2007-2008
 */


#ifdef CONFIG_X86_DS

#include <asm/ds.h>

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>


/*
 * The configuration for a particular DS hardware implementation.
 */
struct ds_configuration {
	/* the size of the DS structure in bytes */
	unsigned char  sizeof_ds;
	/* the size of one pointer-typed field in the DS structure in bytes;
	   this covers the first 8 fields related to buffer management. */
	unsigned char  sizeof_field;
	/* the size of a BTS/PEBS record in bytes */
	unsigned char  sizeof_rec[2];
};
static struct ds_configuration ds_cfg;


/*
 * Debug Store (DS) save area configuration (see Intel64 and IA32
 * Architectures Software Developer's Manual, section 18.5)
 *
 * The DS configuration consists of the following fields; different
 * architetures vary in the size of those fields.
 * - double-word aligned base linear address of the BTS buffer
 * - write pointer into the BTS buffer
 * - end linear address of the BTS buffer (one byte beyond the end of
 *   the buffer)
 * - interrupt pointer into BTS buffer
 *   (interrupt occurs when write pointer passes interrupt pointer)
 * - double-word aligned base linear address of the PEBS buffer
 * - write pointer into the PEBS buffer
 * - end linear address of the PEBS buffer (one byte beyond the end of
 *   the buffer)
 * - interrupt pointer into PEBS buffer
 *   (interrupt occurs when write pointer passes interrupt pointer)
 * - value to which counter is reset following counter overflow
 *
 * Later architectures use 64bit pointers throughout, whereas earlier
 * architectures use 32bit pointers in 32bit mode.
 *
 *
 * We compute the base address for the first 8 fields based on:
 * - the field size stored in the DS configuration
 * - the relative field position
 * - an offset giving the start of the respective region
 *
 * This offset is further used to index various arrays holding
 * information for BTS and PEBS at the respective index.
 *
 * On later 32bit processors, we only access the lower 32bit of the
 * 64bit pointer fields. The upper halves will be zeroed out.
 */

enum ds_field {
	ds_buffer_base = 0,
	ds_index,
	ds_absolute_maximum,
	ds_interrupt_threshold,
};

enum ds_qualifier {
	ds_bts  = 0,
	ds_pebs
};

static inline unsigned long ds_get(const unsigned char *base,
				   enum ds_qualifier qual, enum ds_field field)
{
	base += (ds_cfg.sizeof_field * (field + (4 * qual)));
	return *(unsigned long *)base;
}

static inline void ds_set(unsigned char *base, enum ds_qualifier qual,
			  enum ds_field field, unsigned long value)
{
	base += (ds_cfg.sizeof_field * (field + (4 * qual)));
	(*(unsigned long *)base) = value;
}


/*
 * Locking is done only for allocating BTS or PEBS resources and for
 * guarding context and buffer memory allocation.
 *
 * Most functions require the current task to own the ds context part
 * they are going to access. All the locking is done when validating
 * access to the context.
 */
static spinlock_t ds_lock = __SPIN_LOCK_UNLOCKED(ds_lock);

/*
 * Validate that the current task is allowed to access the BTS/PEBS
 * buffer of the parameter task.
 *
 * Returns 0, if access is granted; -Eerrno, otherwise.
 */
static inline int ds_validate_access(struct ds_context *context,
				     enum ds_qualifier qual)
{
	if (!context)
		return -EPERM;

	if (context->owner[qual] == current)
		return 0;

	return -EPERM;
}


/*
 * We either support (system-wide) per-cpu or per-thread allocation.
 * We distinguish the two based on the task_struct pointer, where a
 * NULL pointer indicates per-cpu allocation for the current cpu.
 *
 * Allocations are use-counted. As soon as resources are allocated,
 * further allocations must be of the same type (per-cpu or
 * per-thread). We model this by counting allocations (i.e. the number
 * of tracers of a certain type) for one type negatively:
 *   =0  no tracers
 *   >0  number of per-thread tracers
 *   <0  number of per-cpu tracers
 *
 * The below functions to get and put tracers and to check the
 * allocation type require the ds_lock to be held by the caller.
 *
 * Tracers essentially gives the number of ds contexts for a certain
 * type of allocation.
 */
static long tracers;

static inline void get_tracer(struct task_struct *task)
{
	tracers += (task ? 1 : -1);
}

static inline void put_tracer(struct task_struct *task)
{
	tracers -= (task ? 1 : -1);
}

static inline int check_tracer(struct task_struct *task)
{
	return (task ? (tracers >= 0) : (tracers <= 0));
}


/*
 * The DS context is either attached to a thread or to a cpu:
 * - in the former case, the thread_struct contains a pointer to the
 *   attached context.
 * - in the latter case, we use a static array of per-cpu context
 *   pointers.
 *
 * Contexts are use-counted. They are allocated on first access and
 * deallocated when the last user puts the context.
 *
 * We distinguish between an allocating and a non-allocating get of a
 * context:
 * - the allocating get is used for requesting BTS/PEBS resources. It
 *   requires the caller to hold the global ds_lock.
 * - the non-allocating get is used for all other cases. A
 *   non-existing context indicates an error. It acquires and releases
 *   the ds_lock itself for obtaining the context.
 *
 * A context and its DS configuration are allocated and deallocated
 * together. A context always has a DS configuration of the
 * appropriate size.
 */
static DEFINE_PER_CPU(struct ds_context *, system_context);

#define this_system_context per_cpu(system_context, smp_processor_id())

/*
 * Returns the pointer to the parameter task's context or to the
 * system-wide context, if task is NULL.
 *
 * Increases the use count of the returned context, if not NULL.
 */
static inline struct ds_context *ds_get_context(struct task_struct *task)
{
	struct ds_context *context;

	spin_lock(&ds_lock);

	context = (task ? task->thread.ds_ctx : this_system_context);
	if (context)
		context->count++;

	spin_unlock(&ds_lock);

	return context;
}

/*
 * Same as ds_get_context, but allocates the context and it's DS
 * structure, if necessary; returns NULL; if out of memory.
 *
 * pre: requires ds_lock to be held
 */
static inline struct ds_context *ds_alloc_context(struct task_struct *task)
{
	struct ds_context **p_context =
		(task ? &task->thread.ds_ctx : &this_system_context);
	struct ds_context *context = *p_context;

	if (!context) {
		context = kzalloc(sizeof(*context), GFP_KERNEL);

		if (!context)
			return NULL;

		context->ds = kzalloc(ds_cfg.sizeof_ds, GFP_KERNEL);
		if (!context->ds) {
			kfree(context);
			return NULL;
		}

		*p_context = context;

		context->this = p_context;
		context->task = task;

		if (task)
			set_tsk_thread_flag(task, TIF_DS_AREA_MSR);

		if (!task || (task == current))
			wrmsr(MSR_IA32_DS_AREA, (unsigned long)context->ds, 0);

		get_tracer(task);
	}

	context->count++;

	return context;
}

/*
 * Decreases the use count of the parameter context, if not NULL.
 * Deallocates the context, if the use count reaches zero.
 */
static inline void ds_put_context(struct ds_context *context)
{
	if (!context)
		return;

	spin_lock(&ds_lock);

	if (--context->count)
		goto out;

	*(context->this) = NULL;

	if (context->task)
		clear_tsk_thread_flag(context->task, TIF_DS_AREA_MSR);

	if (!context->task || (context->task == current))
		wrmsrl(MSR_IA32_DS_AREA, 0);

	put_tracer(context->task);

	/* free any leftover buffers from tracers that did not
	 * deallocate them properly. */
	kfree(context->buffer[ds_bts]);
	kfree(context->buffer[ds_pebs]);
	kfree(context->ds);
	kfree(context);
 out:
	spin_unlock(&ds_lock);
}


/*
 * Handle a buffer overflow
 *
 * task: the task whose buffers are overflowing;
 *       NULL for a buffer overflow on the current cpu
 * context: the ds context
 * qual: the buffer type
 */
static void ds_overflow(struct task_struct *task, struct ds_context *context,
			enum ds_qualifier qual)
{
	if (!context)
		return;

	if (context->callback[qual])
		(*context->callback[qual])(task);

	/* todo: do some more overflow handling */
}


/*
 * Allocate a non-pageable buffer of the parameter size.
 * Checks the memory and the locked memory rlimit.
 *
 * Returns the buffer, if successful;
 *         NULL, if out of memory or rlimit exceeded.
 *
 * size: the requested buffer size in bytes
 * pages (out): if not NULL, contains the number of pages reserved
 */
static inline void *ds_allocate_buffer(size_t size, unsigned int *pages)
{
	unsigned long rlim, vm, pgsz;
	void *buffer;

	pgsz = PAGE_ALIGN(size) >> PAGE_SHIFT;

	rlim = current->signal->rlim[RLIMIT_AS].rlim_cur >> PAGE_SHIFT;
	vm   = current->mm->total_vm  + pgsz;
	if (rlim < vm)
		return NULL;

	rlim = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur >> PAGE_SHIFT;
	vm   = current->mm->locked_vm  + pgsz;
	if (rlim < vm)
		return NULL;

	buffer = kzalloc(size, GFP_KERNEL);
	if (!buffer)
		return NULL;

	current->mm->total_vm  += pgsz;
	current->mm->locked_vm += pgsz;

	if (pages)
		*pages = pgsz;

	return buffer;
}

static int ds_request(struct task_struct *task, void *base, size_t size,
		      ds_ovfl_callback_t ovfl, enum ds_qualifier qual)
{
	struct ds_context *context;
	unsigned long buffer, adj;
	const unsigned long alignment = (1 << 3);
	int error = 0;

	if (!ds_cfg.sizeof_ds)
		return -EOPNOTSUPP;

	/* we require some space to do alignment adjustments below */
	if (size < (alignment + ds_cfg.sizeof_rec[qual]))
		return -EINVAL;

	/* buffer overflow notification is not yet implemented */
	if (ovfl)
		return -EOPNOTSUPP;


	spin_lock(&ds_lock);

	if (!check_tracer(task))
		return -EPERM;

	error = -ENOMEM;
	context = ds_alloc_context(task);
	if (!context)
		goto out_unlock;

	error = -EALREADY;
	if (context->owner[qual] == current)
		goto out_unlock;
	error = -EPERM;
	if (context->owner[qual] != NULL)
		goto out_unlock;
	context->owner[qual] = current;

	spin_unlock(&ds_lock);


	error = -ENOMEM;
	if (!base) {
		base = ds_allocate_buffer(size, &context->pages[qual]);
		if (!base)
			goto out_release;

		context->buffer[qual]   = base;
	}
	error = 0;

	context->callback[qual] = ovfl;

	/* adjust the buffer address and size to meet alignment
	 * constraints:
	 * - buffer is double-word aligned
	 * - size is multiple of record size
	 *
	 * We checked the size at the very beginning; we have enough
	 * space to do the adjustment.
	 */
	buffer = (unsigned long)base;

	adj = ALIGN(buffer, alignment) - buffer;
	buffer += adj;
	size   -= adj;

	size /= ds_cfg.sizeof_rec[qual];
	size *= ds_cfg.sizeof_rec[qual];

	ds_set(context->ds, qual, ds_buffer_base, buffer);
	ds_set(context->ds, qual, ds_index, buffer);
	ds_set(context->ds, qual, ds_absolute_maximum, buffer + size);

	if (ovfl) {
		/* todo: select a suitable interrupt threshold */
	} else
		ds_set(context->ds, qual,
		       ds_interrupt_threshold, buffer + size + 1);

	/* we keep the context until ds_release */
	return error;

 out_release:
	context->owner[qual] = NULL;
	ds_put_context(context);
	return error;

 out_unlock:
	spin_unlock(&ds_lock);
	ds_put_context(context);
	return error;
}

int ds_request_bts(struct task_struct *task, void *base, size_t size,
		   ds_ovfl_callback_t ovfl)
{
	return ds_request(task, base, size, ovfl, ds_bts);
}

int ds_request_pebs(struct task_struct *task, void *base, size_t size,
		    ds_ovfl_callback_t ovfl)
{
	return ds_request(task, base, size, ovfl, ds_pebs);
}

static int ds_release(struct task_struct *task, enum ds_qualifier qual)
{
	struct ds_context *context;
	int error;

	context = ds_get_context(task);
	error = ds_validate_access(context, qual);
	if (error < 0)
		goto out;

	kfree(context->buffer[qual]);
	context->buffer[qual] = NULL;

	current->mm->total_vm  -= context->pages[qual];
	current->mm->locked_vm -= context->pages[qual];
	context->pages[qual] = 0;
	context->owner[qual] = NULL;

	/*
	 * we put the context twice:
	 *   once for the ds_get_context
	 *   once for the corresponding ds_request
	 */
	ds_put_context(context);
 out:
	ds_put_context(context);
	return error;
}

int ds_release_bts(struct task_struct *task)
{
	return ds_release(task, ds_bts);
}

int ds_release_pebs(struct task_struct *task)
{
	return ds_release(task, ds_pebs);
}

static int ds_get_index(struct task_struct *task, size_t *pos,
			enum ds_qualifier qual)
{
	struct ds_context *context;
	unsigned long base, index;
	int error;

	context = ds_get_context(task);
	error = ds_validate_access(context, qual);
	if (error < 0)
		goto out;

	base  = ds_get(context->ds, qual, ds_buffer_base);
	index = ds_get(context->ds, qual, ds_index);

	error = ((index - base) / ds_cfg.sizeof_rec[qual]);
	if (pos)
		*pos = error;
 out:
	ds_put_context(context);
	return error;
}

int ds_get_bts_index(struct task_struct *task, size_t *pos)
{
	return ds_get_index(task, pos, ds_bts);
}

int ds_get_pebs_index(struct task_struct *task, size_t *pos)
{
	return ds_get_index(task, pos, ds_pebs);
}

static int ds_get_end(struct task_struct *task, size_t *pos,
		      enum ds_qualifier qual)
{
	struct ds_context *context;
	unsigned long base, end;
	int error;

	context = ds_get_context(task);
	error = ds_validate_access(context, qual);
	if (error < 0)
		goto out;

	base = ds_get(context->ds, qual, ds_buffer_base);
	end  = ds_get(context->ds, qual, ds_absolute_maximum);

	error = ((end - base) / ds_cfg.sizeof_rec[qual]);
	if (pos)
		*pos = error;
 out:
	ds_put_context(context);
	return error;
}

int ds_get_bts_end(struct task_struct *task, size_t *pos)
{
	return ds_get_end(task, pos, ds_bts);
}

int ds_get_pebs_end(struct task_struct *task, size_t *pos)
{
	return ds_get_end(task, pos, ds_pebs);
}

static int ds_access(struct task_struct *task, size_t index,
		     const void **record, enum ds_qualifier qual)
{
	struct ds_context *context;
	unsigned long base, idx;
	int error;

	if (!record)
		return -EINVAL;

	context = ds_get_context(task);
	error = ds_validate_access(context, qual);
	if (error < 0)
		goto out;

	base = ds_get(context->ds, qual, ds_buffer_base);
	idx = base + (index * ds_cfg.sizeof_rec[qual]);

	error = -EINVAL;
	if (idx > ds_get(context->ds, qual, ds_absolute_maximum))
		goto out;

	*record = (const void *)idx;
	error = ds_cfg.sizeof_rec[qual];
 out:
	ds_put_context(context);
	return error;
}

int ds_access_bts(struct task_struct *task, size_t index, const void **record)
{
	return ds_access(task, index, record, ds_bts);
}

int ds_access_pebs(struct task_struct *task, size_t index, const void **record)
{
	return ds_access(task, index, record, ds_pebs);
}

static int ds_write(struct task_struct *task, const void *record, size_t size,
		    enum ds_qualifier qual, int force)
{
	struct ds_context *context;
	int error;

	if (!record)
		return -EINVAL;

	error = -EPERM;
	context = ds_get_context(task);
	if (!context)
		goto out;

	if (!force) {
		error = ds_validate_access(context, qual);
		if (error < 0)
			goto out;
	}

	error = 0;
	while (size) {
		unsigned long base, index, end, write_end, int_th;
		unsigned long write_size, adj_write_size;

		/*
		 * write as much as possible without producing an
		 * overflow interrupt.
		 *
		 * interrupt_threshold must either be
		 * - bigger than absolute_maximum or
		 * - point to a record between buffer_base and absolute_maximum
		 *
		 * index points to a valid record.
		 */
		base   = ds_get(context->ds, qual, ds_buffer_base);
		index  = ds_get(context->ds, qual, ds_index);
		end    = ds_get(context->ds, qual, ds_absolute_maximum);
		int_th = ds_get(context->ds, qual, ds_interrupt_threshold);

		write_end = min(end, int_th);

		/* if we are already beyond the interrupt threshold,
		 * we fill the entire buffer */
		if (write_end <= index)
			write_end = end;

		if (write_end <= index)
			goto out;

		write_size = min((unsigned long) size, write_end - index);
		memcpy((void *)index, record, write_size);

		record = (const char *)record + write_size;
		size  -= write_size;
		error += write_size;

		adj_write_size = write_size / ds_cfg.sizeof_rec[qual];
		adj_write_size *= ds_cfg.sizeof_rec[qual];

		/* zero out trailing bytes */
		memset((char *)index + write_size, 0,
		       adj_write_size - write_size);
		index += adj_write_size;

		if (index >= end)
			index = base;
		ds_set(context->ds, qual, ds_index, index);

		if (index >= int_th)
			ds_overflow(task, context, qual);
	}

 out:
	ds_put_context(context);
	return error;
}

int ds_write_bts(struct task_struct *task, const void *record, size_t size)
{
	return ds_write(task, record, size, ds_bts, /* force = */ 0);
}

int ds_write_pebs(struct task_struct *task, const void *record, size_t size)
{
	return ds_write(task, record, size, ds_pebs, /* force = */ 0);
}

int ds_unchecked_write_bts(struct task_struct *task,
			   const void *record, size_t size)
{
	return ds_write(task, record, size, ds_bts, /* force = */ 1);
}

int ds_unchecked_write_pebs(struct task_struct *task,
			    const void *record, size_t size)
{
	return ds_write(task, record, size, ds_pebs, /* force = */ 1);
}

static int ds_reset_or_clear(struct task_struct *task,
			     enum ds_qualifier qual, int clear)
{
	struct ds_context *context;
	unsigned long base, end;
	int error;

	context = ds_get_context(task);
	error = ds_validate_access(context, qual);
	if (error < 0)
		goto out;

	base = ds_get(context->ds, qual, ds_buffer_base);
	end  = ds_get(context->ds, qual, ds_absolute_maximum);

	if (clear)
		memset((void *)base, 0, end - base);

	ds_set(context->ds, qual, ds_index, base);

	error = 0;
 out:
	ds_put_context(context);
	return error;
}

int ds_reset_bts(struct task_struct *task)
{
	return ds_reset_or_clear(task, ds_bts, /* clear = */ 0);
}

int ds_reset_pebs(struct task_struct *task)
{
	return ds_reset_or_clear(task, ds_pebs, /* clear = */ 0);
}

int ds_clear_bts(struct task_struct *task)
{
	return ds_reset_or_clear(task, ds_bts, /* clear = */ 1);
}

int ds_clear_pebs(struct task_struct *task)
{
	return ds_reset_or_clear(task, ds_pebs, /* clear = */ 1);
}

int ds_get_pebs_reset(struct task_struct *task, u64 *value)
{
	struct ds_context *context;
	int error;

	if (!value)
		return -EINVAL;

	context = ds_get_context(task);
	error = ds_validate_access(context, ds_pebs);
	if (error < 0)
		goto out;

	*value = *(u64 *)(context->ds + (ds_cfg.sizeof_field * 8));

	error = 0;
 out:
	ds_put_context(context);
	return error;
}

int ds_set_pebs_reset(struct task_struct *task, u64 value)
{
	struct ds_context *context;
	int error;

	context = ds_get_context(task);
	error = ds_validate_access(context, ds_pebs);
	if (error < 0)
		goto out;

	*(u64 *)(context->ds + (ds_cfg.sizeof_field * 8)) = value;

	error = 0;
 out:
	ds_put_context(context);
	return error;
}

static const struct ds_configuration ds_cfg_var = {
	.sizeof_ds    = sizeof(long) * 12,
	.sizeof_field = sizeof(long),
	.sizeof_rec[ds_bts]   = sizeof(long) * 3,
	.sizeof_rec[ds_pebs]  = sizeof(long) * 10
};
static const struct ds_configuration ds_cfg_64 = {
	.sizeof_ds    = 8 * 12,
	.sizeof_field = 8,
	.sizeof_rec[ds_bts]   = 8 * 3,
	.sizeof_rec[ds_pebs]  = 8 * 10
};

static inline void
ds_configure(const struct ds_configuration *cfg)
{
	ds_cfg = *cfg;
}

void __cpuinit ds_init_intel(struct cpuinfo_x86 *c)
{
	switch (c->x86) {
	case 0x6:
		switch (c->x86_model) {
		case 0xD:
		case 0xE: /* Pentium M */
			ds_configure(&ds_cfg_var);
			break;
		case 0xF: /* Core2 */
		case 0x1C: /* Atom */
			ds_configure(&ds_cfg_64);
			break;
		default:
			/* sorry, don't know about them */
			break;
		}
		break;
	case 0xF:
		switch (c->x86_model) {
		case 0x0:
		case 0x1:
		case 0x2: /* Netburst */
			ds_configure(&ds_cfg_var);
			break;
		default:
			/* sorry, don't know about them */
			break;
		}
		break;
	default:
		/* sorry, don't know about them */
		break;
	}
}

void ds_free(struct ds_context *context)
{
	/* This is called when the task owning the parameter context
	 * is dying. There should not be any user of that context left
	 * to disturb us, anymore. */
	unsigned long leftovers = context->count;
	while (leftovers--)
		ds_put_context(context);
}
#endif /* CONFIG_X86_DS */
