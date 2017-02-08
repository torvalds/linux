/**
 * @file buffer_sync.c
 *
 * @remark Copyright 2002-2009 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Barry Kasindorf
 * @author Robert Richter <robert.richter@amd.com>
 *
 * This is the core of the buffer management. Each
 * CPU buffer is processed and entered into the
 * global event buffer. Such processing is necessary
 * in several circumstances, mentioned below.
 *
 * The processing does the job of converting the
 * transitory EIP value into a persistent dentry/offset
 * value that the profiler can record at its leisure.
 *
 * See fs/dcookies.c for a description of the dentry/offset
 * objects.
 */

#include <linux/file.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/dcookies.h>
#include <linux/profile.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/gfp.h>

#include "oprofile_stats.h"
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"

static LIST_HEAD(dying_tasks);
static LIST_HEAD(dead_tasks);
static cpumask_var_t marked_cpus;
static DEFINE_SPINLOCK(task_mortuary);
static void process_task_mortuary(void);

/* Take ownership of the task struct and place it on the
 * list for processing. Only after two full buffer syncs
 * does the task eventually get freed, because by then
 * we are sure we will not reference it again.
 * Can be invoked from softirq via RCU callback due to
 * call_rcu() of the task struct, hence the _irqsave.
 */
static int
task_free_notify(struct notifier_block *self, unsigned long val, void *data)
{
	unsigned long flags;
	struct task_struct *task = data;
	spin_lock_irqsave(&task_mortuary, flags);
	list_add(&task->tasks, &dying_tasks);
	spin_unlock_irqrestore(&task_mortuary, flags);
	return NOTIFY_OK;
}


/* The task is on its way out. A sync of the buffer means we can catch
 * any remaining samples for this task.
 */
static int
task_exit_notify(struct notifier_block *self, unsigned long val, void *data)
{
	/* To avoid latency problems, we only process the current CPU,
	 * hoping that most samples for the task are on this CPU
	 */
	sync_buffer(raw_smp_processor_id());
	return 0;
}


/* The task is about to try a do_munmap(). We peek at what it's going to
 * do, and if it's an executable region, process the samples first, so
 * we don't lose any. This does not have to be exact, it's a QoI issue
 * only.
 */
static int
munmap_notify(struct notifier_block *self, unsigned long val, void *data)
{
	unsigned long addr = (unsigned long)data;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *mpnt;

	down_read(&mm->mmap_sem);

	mpnt = find_vma(mm, addr);
	if (mpnt && mpnt->vm_file && (mpnt->vm_flags & VM_EXEC)) {
		up_read(&mm->mmap_sem);
		/* To avoid latency problems, we only process the current CPU,
		 * hoping that most samples for the task are on this CPU
		 */
		sync_buffer(raw_smp_processor_id());
		return 0;
	}

	up_read(&mm->mmap_sem);
	return 0;
}


/* We need to be told about new modules so we don't attribute to a previously
 * loaded module, or drop the samples on the floor.
 */
static int
module_load_notify(struct notifier_block *self, unsigned long val, void *data)
{
#ifdef CONFIG_MODULES
	if (val != MODULE_STATE_COMING)
		return 0;

	/* FIXME: should we process all CPU buffers ? */
	mutex_lock(&buffer_mutex);
	add_event_entry(ESCAPE_CODE);
	add_event_entry(MODULE_LOADED_CODE);
	mutex_unlock(&buffer_mutex);
#endif
	return 0;
}


static struct notifier_block task_free_nb = {
	.notifier_call	= task_free_notify,
};

static struct notifier_block task_exit_nb = {
	.notifier_call	= task_exit_notify,
};

static struct notifier_block munmap_nb = {
	.notifier_call	= munmap_notify,
};

static struct notifier_block module_load_nb = {
	.notifier_call = module_load_notify,
};

static void free_all_tasks(void)
{
	/* make sure we don't leak task structs */
	process_task_mortuary();
	process_task_mortuary();
}

int sync_start(void)
{
	int err;

	if (!zalloc_cpumask_var(&marked_cpus, GFP_KERNEL))
		return -ENOMEM;

	err = task_handoff_register(&task_free_nb);
	if (err)
		goto out1;
	err = profile_event_register(PROFILE_TASK_EXIT, &task_exit_nb);
	if (err)
		goto out2;
	err = profile_event_register(PROFILE_MUNMAP, &munmap_nb);
	if (err)
		goto out3;
	err = register_module_notifier(&module_load_nb);
	if (err)
		goto out4;

	start_cpu_work();

out:
	return err;
out4:
	profile_event_unregister(PROFILE_MUNMAP, &munmap_nb);
out3:
	profile_event_unregister(PROFILE_TASK_EXIT, &task_exit_nb);
out2:
	task_handoff_unregister(&task_free_nb);
	free_all_tasks();
out1:
	free_cpumask_var(marked_cpus);
	goto out;
}


void sync_stop(void)
{
	end_cpu_work();
	unregister_module_notifier(&module_load_nb);
	profile_event_unregister(PROFILE_MUNMAP, &munmap_nb);
	profile_event_unregister(PROFILE_TASK_EXIT, &task_exit_nb);
	task_handoff_unregister(&task_free_nb);
	barrier();			/* do all of the above first */

	flush_cpu_work();

	free_all_tasks();
	free_cpumask_var(marked_cpus);
}


/* Optimisation. We can manage without taking the dcookie sem
 * because we cannot reach this code without at least one
 * dcookie user still being registered (namely, the reader
 * of the event buffer). */
static inline unsigned long fast_get_dcookie(const struct path *path)
{
	unsigned long cookie;

	if (path->dentry->d_flags & DCACHE_COOKIE)
		return (unsigned long)path->dentry;
	get_dcookie(path, &cookie);
	return cookie;
}


/* Look up the dcookie for the task's mm->exe_file,
 * which corresponds loosely to "application name". This is
 * not strictly necessary but allows oprofile to associate
 * shared-library samples with particular applications
 */
static unsigned long get_exec_dcookie(struct mm_struct *mm)
{
	unsigned long cookie = NO_COOKIE;
	struct file *exe_file;

	if (!mm)
		goto done;

	exe_file = get_mm_exe_file(mm);
	if (!exe_file)
		goto done;

	cookie = fast_get_dcookie(&exe_file->f_path);
	fput(exe_file);
done:
	return cookie;
}


/* Convert the EIP value of a sample into a persistent dentry/offset
 * pair that can then be added to the global event buffer. We make
 * sure to do this lookup before a mm->mmap modification happens so
 * we don't lose track.
 *
 * The caller must ensure the mm is not nil (ie: not a kernel thread).
 */
static unsigned long
lookup_dcookie(struct mm_struct *mm, unsigned long addr, off_t *offset)
{
	unsigned long cookie = NO_COOKIE;
	struct vm_area_struct *vma;

	down_read(&mm->mmap_sem);
	for (vma = find_vma(mm, addr); vma; vma = vma->vm_next) {

		if (addr < vma->vm_start || addr >= vma->vm_end)
			continue;

		if (vma->vm_file) {
			cookie = fast_get_dcookie(&vma->vm_file->f_path);
			*offset = (vma->vm_pgoff << PAGE_SHIFT) + addr -
				vma->vm_start;
		} else {
			/* must be an anonymous map */
			*offset = addr;
		}

		break;
	}

	if (!vma)
		cookie = INVALID_COOKIE;
	up_read(&mm->mmap_sem);

	return cookie;
}

static unsigned long last_cookie = INVALID_COOKIE;

static void add_cpu_switch(int i)
{
	add_event_entry(ESCAPE_CODE);
	add_event_entry(CPU_SWITCH_CODE);
	add_event_entry(i);
	last_cookie = INVALID_COOKIE;
}

static void add_kernel_ctx_switch(unsigned int in_kernel)
{
	add_event_entry(ESCAPE_CODE);
	if (in_kernel)
		add_event_entry(KERNEL_ENTER_SWITCH_CODE);
	else
		add_event_entry(KERNEL_EXIT_SWITCH_CODE);
}

static void
add_user_ctx_switch(struct task_struct const *task, unsigned long cookie)
{
	add_event_entry(ESCAPE_CODE);
	add_event_entry(CTX_SWITCH_CODE);
	add_event_entry(task->pid);
	add_event_entry(cookie);
	/* Another code for daemon back-compat */
	add_event_entry(ESCAPE_CODE);
	add_event_entry(CTX_TGID_CODE);
	add_event_entry(task->tgid);
}


static void add_cookie_switch(unsigned long cookie)
{
	add_event_entry(ESCAPE_CODE);
	add_event_entry(COOKIE_SWITCH_CODE);
	add_event_entry(cookie);
}


static void add_trace_begin(void)
{
	add_event_entry(ESCAPE_CODE);
	add_event_entry(TRACE_BEGIN_CODE);
}

static void add_data(struct op_entry *entry, struct mm_struct *mm)
{
	unsigned long code, pc, val;
	unsigned long cookie;
	off_t offset;

	if (!op_cpu_buffer_get_data(entry, &code))
		return;
	if (!op_cpu_buffer_get_data(entry, &pc))
		return;
	if (!op_cpu_buffer_get_size(entry))
		return;

	if (mm) {
		cookie = lookup_dcookie(mm, pc, &offset);

		if (cookie == NO_COOKIE)
			offset = pc;
		if (cookie == INVALID_COOKIE) {
			atomic_inc(&oprofile_stats.sample_lost_no_mapping);
			offset = pc;
		}
		if (cookie != last_cookie) {
			add_cookie_switch(cookie);
			last_cookie = cookie;
		}
	} else
		offset = pc;

	add_event_entry(ESCAPE_CODE);
	add_event_entry(code);
	add_event_entry(offset);	/* Offset from Dcookie */

	while (op_cpu_buffer_get_data(entry, &val))
		add_event_entry(val);
}

static inline void add_sample_entry(unsigned long offset, unsigned long event)
{
	add_event_entry(offset);
	add_event_entry(event);
}


/*
 * Add a sample to the global event buffer. If possible the
 * sample is converted into a persistent dentry/offset pair
 * for later lookup from userspace. Return 0 on failure.
 */
static int
add_sample(struct mm_struct *mm, struct op_sample *s, int in_kernel)
{
	unsigned long cookie;
	off_t offset;

	if (in_kernel) {
		add_sample_entry(s->eip, s->event);
		return 1;
	}

	/* add userspace sample */

	if (!mm) {
		atomic_inc(&oprofile_stats.sample_lost_no_mm);
		return 0;
	}

	cookie = lookup_dcookie(mm, s->eip, &offset);

	if (cookie == INVALID_COOKIE) {
		atomic_inc(&oprofile_stats.sample_lost_no_mapping);
		return 0;
	}

	if (cookie != last_cookie) {
		add_cookie_switch(cookie);
		last_cookie = cookie;
	}

	add_sample_entry(offset, s->event);

	return 1;
}


static void release_mm(struct mm_struct *mm)
{
	if (!mm)
		return;
	mmput(mm);
}

static inline int is_code(unsigned long val)
{
	return val == ESCAPE_CODE;
}


/* Move tasks along towards death. Any tasks on dead_tasks
 * will definitely have no remaining references in any
 * CPU buffers at this point, because we use two lists,
 * and to have reached the list, it must have gone through
 * one full sync already.
 */
static void process_task_mortuary(void)
{
	unsigned long flags;
	LIST_HEAD(local_dead_tasks);
	struct task_struct *task;
	struct task_struct *ttask;

	spin_lock_irqsave(&task_mortuary, flags);

	list_splice_init(&dead_tasks, &local_dead_tasks);
	list_splice_init(&dying_tasks, &dead_tasks);

	spin_unlock_irqrestore(&task_mortuary, flags);

	list_for_each_entry_safe(task, ttask, &local_dead_tasks, tasks) {
		list_del(&task->tasks);
		free_task(task);
	}
}


static void mark_done(int cpu)
{
	int i;

	cpumask_set_cpu(cpu, marked_cpus);

	for_each_online_cpu(i) {
		if (!cpumask_test_cpu(i, marked_cpus))
			return;
	}

	/* All CPUs have been processed at least once,
	 * we can process the mortuary once
	 */
	process_task_mortuary();

	cpumask_clear(marked_cpus);
}


/* FIXME: this is not sufficient if we implement syscall barrier backtrace
 * traversal, the code switch to sb_sample_start at first kernel enter/exit
 * switch so we need a fifth state and some special handling in sync_buffer()
 */
typedef enum {
	sb_bt_ignore = -2,
	sb_buffer_start,
	sb_bt_start,
	sb_sample_start,
} sync_buffer_state;

/* Sync one of the CPU's buffers into the global event buffer.
 * Here we need to go through each batch of samples punctuated
 * by context switch notes, taking the task's mmap_sem and doing
 * lookup in task->mm->mmap to convert EIP into dcookie/offset
 * value.
 */
void sync_buffer(int cpu)
{
	struct mm_struct *mm = NULL;
	struct mm_struct *oldmm;
	unsigned long val;
	struct task_struct *new;
	unsigned long cookie = 0;
	int in_kernel = 1;
	sync_buffer_state state = sb_buffer_start;
	unsigned int i;
	unsigned long available;
	unsigned long flags;
	struct op_entry entry;
	struct op_sample *sample;

	mutex_lock(&buffer_mutex);

	add_cpu_switch(cpu);

	op_cpu_buffer_reset(cpu);
	available = op_cpu_buffer_entries(cpu);

	for (i = 0; i < available; ++i) {
		sample = op_cpu_buffer_read_entry(&entry, cpu);
		if (!sample)
			break;

		if (is_code(sample->eip)) {
			flags = sample->event;
			if (flags & TRACE_BEGIN) {
				state = sb_bt_start;
				add_trace_begin();
			}
			if (flags & KERNEL_CTX_SWITCH) {
				/* kernel/userspace switch */
				in_kernel = flags & IS_KERNEL;
				if (state == sb_buffer_start)
					state = sb_sample_start;
				add_kernel_ctx_switch(flags & IS_KERNEL);
			}
			if (flags & USER_CTX_SWITCH
			    && op_cpu_buffer_get_data(&entry, &val)) {
				/* userspace context switch */
				new = (struct task_struct *)val;
				oldmm = mm;
				release_mm(oldmm);
				mm = get_task_mm(new);
				if (mm != oldmm)
					cookie = get_exec_dcookie(mm);
				add_user_ctx_switch(new, cookie);
			}
			if (op_cpu_buffer_get_size(&entry))
				add_data(&entry, mm);
			continue;
		}

		if (state < sb_bt_start)
			/* ignore sample */
			continue;

		if (add_sample(mm, sample, in_kernel))
			continue;

		/* ignore backtraces if failed to add a sample */
		if (state == sb_bt_start) {
			state = sb_bt_ignore;
			atomic_inc(&oprofile_stats.bt_lost_no_mapping);
		}
	}
	release_mm(mm);

	mark_done(cpu);

	mutex_unlock(&buffer_mutex);
}

/* The function can be used to add a buffer worth of data directly to
 * the kernel buffer. The buffer is assumed to be a circular buffer.
 * Take the entries from index start and end at index end, wrapping
 * at max_entries.
 */
void oprofile_put_buff(unsigned long *buf, unsigned int start,
		       unsigned int stop, unsigned int max)
{
	int i;

	i = start;

	mutex_lock(&buffer_mutex);
	while (i != stop) {
		add_event_entry(buf[i++]);

		if (i >= max)
			i = 0;
	}

	mutex_unlock(&buffer_mutex);
}

