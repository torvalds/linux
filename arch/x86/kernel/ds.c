/*
 * Debug Store support
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


#include <asm/ds.h>

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>


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
 * A BTS or PEBS tracer.
 *
 * This holds the configuration of the tracer and serves as a handle
 * to identify tracers.
 */
struct ds_tracer {
	/* the DS context (partially) owned by this tracer */
	struct ds_context *context;
	/* the buffer provided on ds_request() and its size in bytes */
	void *buffer;
	size_t size;
};

struct bts_tracer {
	/* the common DS part */
	struct ds_tracer ds;
	/* buffer overflow notification function */
	bts_ovfl_callback_t ovfl;
};

struct pebs_tracer {
	/* the common DS part */
	struct ds_tracer ds;
	/* buffer overflow notification function */
	pebs_ovfl_callback_t ovfl;
};

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

#define DS_ALIGNMENT (1 << 3)	/* BTS and PEBS buffer alignment */


/*
 * Locking is done only for allocating BTS or PEBS resources.
 */
static spinlock_t ds_lock = __SPIN_LOCK_UNLOCKED(ds_lock);


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
 */
static DEFINE_PER_CPU(struct ds_context *, system_context);

#define this_system_context per_cpu(system_context, smp_processor_id())

static inline struct ds_context *ds_get_context(struct task_struct *task)
{
	struct ds_context **p_context =
		(task ? &task->thread.ds_ctx : &this_system_context);
	struct ds_context *context = *p_context;
	unsigned long irq;

	if (!context) {
		context = kzalloc(sizeof(*context), GFP_KERNEL);
		if (!context)
			return NULL;

		spin_lock_irqsave(&ds_lock, irq);

		if (*p_context) {
			kfree(context);

			context = *p_context;
		} else {
			*p_context = context;

			context->this = p_context;
			context->task = task;

			if (task)
				set_tsk_thread_flag(task, TIF_DS_AREA_MSR);

			if (!task || (task == current))
				wrmsrl(MSR_IA32_DS_AREA,
				       (unsigned long)context->ds);
		}
		spin_unlock_irqrestore(&ds_lock, irq);
	}

	context->count++;

	return context;
}

static inline void ds_put_context(struct ds_context *context)
{
	unsigned long irq;

	if (!context)
		return;

	spin_lock_irqsave(&ds_lock, irq);

	if (--context->count)
		goto out;

	*(context->this) = NULL;

	if (context->task)
		clear_tsk_thread_flag(context->task, TIF_DS_AREA_MSR);

	if (!context->task || (context->task == current))
		wrmsrl(MSR_IA32_DS_AREA, 0);

	kfree(context);
 out:
	spin_unlock_irqrestore(&ds_lock, irq);
}


/*
 * Handle a buffer overflow
 *
 * context: the ds context
 * qual: the buffer type
 */
static void ds_overflow(struct ds_context *context, enum ds_qualifier qual)
{
	switch (qual) {
	case ds_bts: {
		struct bts_tracer *tracer =
			container_of(context->owner[qual],
				     struct bts_tracer, ds);
		if (tracer->ovfl)
			tracer->ovfl(tracer);
	}
		break;
	case ds_pebs: {
		struct pebs_tracer *tracer =
			container_of(context->owner[qual],
				     struct pebs_tracer, ds);
		if (tracer->ovfl)
			tracer->ovfl(tracer);
	}
		break;
	}
}


static void ds_install_ds_config(struct ds_context *context,
				 enum ds_qualifier qual,
				 void *base, size_t size, size_t ith)
{
	unsigned long buffer, adj;

	/* adjust the buffer address and size to meet alignment
	 * constraints:
	 * - buffer is double-word aligned
	 * - size is multiple of record size
	 *
	 * We checked the size at the very beginning; we have enough
	 * space to do the adjustment.
	 */
	buffer = (unsigned long)base;

	adj = ALIGN(buffer, DS_ALIGNMENT) - buffer;
	buffer += adj;
	size   -= adj;

	size /= ds_cfg.sizeof_rec[qual];
	size *= ds_cfg.sizeof_rec[qual];

	ds_set(context->ds, qual, ds_buffer_base, buffer);
	ds_set(context->ds, qual, ds_index, buffer);
	ds_set(context->ds, qual, ds_absolute_maximum, buffer + size);

	/* The value for 'no threshold' is -1, which will set the
	 * threshold outside of the buffer, just like we want it.
	 */
	ds_set(context->ds, qual,
	       ds_interrupt_threshold, buffer + size - ith);
}

static int ds_request(struct ds_tracer *tracer, enum ds_qualifier qual,
		      struct task_struct *task,
		      void *base, size_t size, size_t th)
{
	struct ds_context *context;
	unsigned long irq;
	int error;

	error = -EOPNOTSUPP;
	if (!ds_cfg.sizeof_ds)
		goto out;

	error = -EINVAL;
	if (!base)
		goto out;

	/* we require some space to do alignment adjustments below */
	error = -EINVAL;
	if (size < (DS_ALIGNMENT + ds_cfg.sizeof_rec[qual]))
		goto out;

	if (th != (size_t)-1) {
		th *= ds_cfg.sizeof_rec[qual];

		error = -EINVAL;
		if (size <= th)
			goto out;
	}

	tracer->buffer = base;
	tracer->size = size;

	error = -ENOMEM;
	context = ds_get_context(task);
	if (!context)
		goto out;
	tracer->context = context;


	spin_lock_irqsave(&ds_lock, irq);

	error = -EPERM;
	if (!check_tracer(task))
		goto out_unlock;
	get_tracer(task);

	error = -EPERM;
	if (context->owner[qual])
		goto out_put_tracer;
	context->owner[qual] = tracer;

	spin_unlock_irqrestore(&ds_lock, irq);


	ds_install_ds_config(context, qual, base, size, th);

	return 0;

 out_put_tracer:
	put_tracer(task);
 out_unlock:
	spin_unlock_irqrestore(&ds_lock, irq);
	ds_put_context(context);
	tracer->context = NULL;
 out:
	return error;
}

struct bts_tracer *ds_request_bts(struct task_struct *task,
				  void *base, size_t size,
				  bts_ovfl_callback_t ovfl, size_t th)
{
	struct bts_tracer *tracer;
	int error;

	/* buffer overflow notification is not yet implemented */
	error = -EOPNOTSUPP;
	if (ovfl)
		goto out;

	error = -ENOMEM;
	tracer = kzalloc(sizeof(*tracer), GFP_KERNEL);
	if (!tracer)
		goto out;
	tracer->ovfl = ovfl;

	error = ds_request(&tracer->ds, ds_bts, task, base, size, th);
	if (error < 0)
		goto out_tracer;

	return tracer;

 out_tracer:
	kfree(tracer);
 out:
	return ERR_PTR(error);
}

struct pebs_tracer *ds_request_pebs(struct task_struct *task,
				    void *base, size_t size,
				    pebs_ovfl_callback_t ovfl, size_t th)
{
	struct pebs_tracer *tracer;
	int error;

	/* buffer overflow notification is not yet implemented */
	error = -EOPNOTSUPP;
	if (ovfl)
		goto out;

	error = -ENOMEM;
	tracer = kzalloc(sizeof(*tracer), GFP_KERNEL);
	if (!tracer)
		goto out;
	tracer->ovfl = ovfl;

	error = ds_request(&tracer->ds, ds_pebs, task, base, size, th);
	if (error < 0)
		goto out_tracer;

	return tracer;

 out_tracer:
	kfree(tracer);
 out:
	return ERR_PTR(error);
}

static void ds_release(struct ds_tracer *tracer, enum ds_qualifier qual)
{
	BUG_ON(tracer->context->owner[qual] != tracer);
	tracer->context->owner[qual] = NULL;

	put_tracer(tracer->context->task);
	ds_put_context(tracer->context);
}

int ds_release_bts(struct bts_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	ds_release(&tracer->ds, ds_bts);
	kfree(tracer);

	return 0;
}

int ds_release_pebs(struct pebs_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	ds_release(&tracer->ds, ds_pebs);
	kfree(tracer);

	return 0;
}

static size_t ds_get_index(struct ds_context *context, enum ds_qualifier qual)
{
	unsigned long base, index;

	base  = ds_get(context->ds, qual, ds_buffer_base);
	index = ds_get(context->ds, qual, ds_index);

	return (index - base) / ds_cfg.sizeof_rec[qual];
}

int ds_get_bts_index(struct bts_tracer *tracer, size_t *pos)
{
	if (!tracer)
		return -EINVAL;

	if (!pos)
		return -EINVAL;

	*pos = ds_get_index(tracer->ds.context, ds_bts);

	return 0;
}

int ds_get_pebs_index(struct pebs_tracer *tracer, size_t *pos)
{
	if (!tracer)
		return -EINVAL;

	if (!pos)
		return -EINVAL;

	*pos = ds_get_index(tracer->ds.context, ds_pebs);

	return 0;
}

static size_t ds_get_end(struct ds_context *context, enum ds_qualifier qual)
{
	unsigned long base, max;

	base = ds_get(context->ds, qual, ds_buffer_base);
	max  = ds_get(context->ds, qual, ds_absolute_maximum);

	return (max - base) / ds_cfg.sizeof_rec[qual];
}

int ds_get_bts_end(struct bts_tracer *tracer, size_t *pos)
{
	if (!tracer)
		return -EINVAL;

	if (!pos)
		return -EINVAL;

	*pos = ds_get_end(tracer->ds.context, ds_bts);

	return 0;
}

int ds_get_pebs_end(struct pebs_tracer *tracer, size_t *pos)
{
	if (!tracer)
		return -EINVAL;

	if (!pos)
		return -EINVAL;

	*pos = ds_get_end(tracer->ds.context, ds_pebs);

	return 0;
}

static int ds_access(struct ds_context *context, enum ds_qualifier qual,
		     size_t index, const void **record)
{
	unsigned long base, idx;

	if (!record)
		return -EINVAL;

	base = ds_get(context->ds, qual, ds_buffer_base);
	idx = base + (index * ds_cfg.sizeof_rec[qual]);

	if (idx > ds_get(context->ds, qual, ds_absolute_maximum))
		return -EINVAL;

	*record = (const void *)idx;

	return ds_cfg.sizeof_rec[qual];
}

int ds_access_bts(struct bts_tracer *tracer, size_t index,
		  const void **record)
{
	if (!tracer)
		return -EINVAL;

	return ds_access(tracer->ds.context, ds_bts, index, record);
}

int ds_access_pebs(struct pebs_tracer *tracer, size_t index,
		   const void **record)
{
	if (!tracer)
		return -EINVAL;

	return ds_access(tracer->ds.context, ds_pebs, index, record);
}

static int ds_write(struct ds_context *context, enum ds_qualifier qual,
		    const void *record, size_t size)
{
	int bytes_written = 0;

	if (!record)
		return -EINVAL;

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
			break;

		write_size = min((unsigned long) size, write_end - index);
		memcpy((void *)index, record, write_size);

		record = (const char *)record + write_size;
		size -= write_size;
		bytes_written += write_size;

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
			ds_overflow(context, qual);
	}

	return bytes_written;
}

int ds_write_bts(struct bts_tracer *tracer, const void *record, size_t size)
{
	if (!tracer)
		return -EINVAL;

	return ds_write(tracer->ds.context, ds_bts, record, size);
}

int ds_write_pebs(struct pebs_tracer *tracer, const void *record, size_t size)
{
	if (!tracer)
		return -EINVAL;

	return ds_write(tracer->ds.context, ds_pebs, record, size);
}

static void ds_reset_or_clear(struct ds_context *context,
			      enum ds_qualifier qual, int clear)
{
	unsigned long base, end;

	base = ds_get(context->ds, qual, ds_buffer_base);
	end  = ds_get(context->ds, qual, ds_absolute_maximum);

	if (clear)
		memset((void *)base, 0, end - base);

	ds_set(context->ds, qual, ds_index, base);
}

int ds_reset_bts(struct bts_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	ds_reset_or_clear(tracer->ds.context, ds_bts, /* clear = */ 0);

	return 0;
}

int ds_reset_pebs(struct pebs_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	ds_reset_or_clear(tracer->ds.context, ds_pebs, /* clear = */ 0);

	return 0;
}

int ds_clear_bts(struct bts_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	ds_reset_or_clear(tracer->ds.context, ds_bts, /* clear = */ 1);

	return 0;
}

int ds_clear_pebs(struct pebs_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	ds_reset_or_clear(tracer->ds.context, ds_pebs, /* clear = */ 1);

	return 0;
}

int ds_get_pebs_reset(struct pebs_tracer *tracer, u64 *value)
{
	if (!tracer)
		return -EINVAL;

	if (!value)
		return -EINVAL;

	*value = *(u64 *)(tracer->ds.context->ds + (ds_cfg.sizeof_field * 8));

	return 0;
}

int ds_set_pebs_reset(struct pebs_tracer *tracer, u64 value)
{
	if (!tracer)
		return -EINVAL;

	*(u64 *)(tracer->ds.context->ds + (ds_cfg.sizeof_field * 8)) = value;

	return 0;
}

static const struct ds_configuration ds_cfg_var = {
	.sizeof_ds    = sizeof(long) * 12,
	.sizeof_field = sizeof(long),
	.sizeof_rec[ds_bts]   = sizeof(long) * 3,
#ifdef __i386__
	.sizeof_rec[ds_pebs]  = sizeof(long) * 10
#else
	.sizeof_rec[ds_pebs]  = sizeof(long) * 18
#endif
};
static const struct ds_configuration ds_cfg_64 = {
	.sizeof_ds    = 8 * 12,
	.sizeof_field = 8,
	.sizeof_rec[ds_bts]   = 8 * 3,
#ifdef __i386__
	.sizeof_rec[ds_pebs]  = 8 * 10
#else
	.sizeof_rec[ds_pebs]  = 8 * 18
#endif
};

static inline void
ds_configure(const struct ds_configuration *cfg)
{
	ds_cfg = *cfg;

	printk(KERN_INFO "DS available\n");

	BUG_ON(MAX_SIZEOF_DS < ds_cfg.sizeof_ds);
}

void __cpuinit ds_init_intel(struct cpuinfo_x86 *c)
{
	switch (c->x86) {
	case 0x6:
		switch (c->x86_model) {
		case 0 ... 0xC:
			/* sorry, don't know about them */
			break;
		case 0xD:
		case 0xE: /* Pentium M */
			ds_configure(&ds_cfg_var);
			break;
		default: /* Core2, Atom, ... */
			ds_configure(&ds_cfg_64);
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
	while (leftovers--) {
		put_tracer(context->task);
		ds_put_context(context);
	}
}
