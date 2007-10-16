/*
 * Copyright (C) 2005 Paolo 'Blaisorblade' Giarrusso <blaisorblade@yahoo.it>
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/types.h"
#include "asm/uaccess.h"
#include "asm/ptrace.h"
#include "asm/segment.h"
#include "asm/smp.h"
#include "asm/desc.h"
#include "choose-mode.h"
#include "kern.h"
#include "kern_util.h"
#include "mode_kern.h"
#include "os.h"
#include "mode.h"
#include "skas.h"

/*
 * If needed we can detect when it's uninitialized.
 *
 * These are initialized in an initcall and unchanged thereafter.
 */
static int host_supports_tls = -1;
int host_gdt_entry_tls_min;

int do_set_thread_area_skas(struct user_desc *info)
{
	int ret;
	u32 cpu;

	cpu = get_cpu();
	ret = os_set_thread_area(info, userspace_pid[cpu]);
	put_cpu();
	return ret;
}

int do_get_thread_area_skas(struct user_desc *info)
{
	int ret;
	u32 cpu;

	cpu = get_cpu();
	ret = os_get_thread_area(info, userspace_pid[cpu]);
	put_cpu();
	return ret;
}

/*
 * sys_get_thread_area: get a yet unused TLS descriptor index.
 * XXX: Consider leaving one free slot for glibc usage at first place. This must
 * be done here (and by changing GDT_ENTRY_TLS_* macros) and nowhere else.
 *
 * Also, this must be tested when compiling in SKAS mode with dinamic linking
 * and running against NPTL.
 */
static int get_free_idx(struct task_struct* task)
{
	struct thread_struct *t = &task->thread;
	int idx;

	if (!t->arch.tls_array)
		return GDT_ENTRY_TLS_MIN;

	for (idx = 0; idx < GDT_ENTRY_TLS_ENTRIES; idx++)
		if (!t->arch.tls_array[idx].present)
			return idx + GDT_ENTRY_TLS_MIN;
	return -ESRCH;
}

static inline void clear_user_desc(struct user_desc* info)
{
	/* Postcondition: LDT_empty(info) returns true. */
	memset(info, 0, sizeof(*info));

	/* Check the LDT_empty or the i386 sys_get_thread_area code - we obtain
	 * indeed an empty user_desc.
	 */
	info->read_exec_only = 1;
	info->seg_not_present = 1;
}

#define O_FORCE 1

static int load_TLS(int flags, struct task_struct *to)
{
	int ret = 0;
	int idx;

	for (idx = GDT_ENTRY_TLS_MIN; idx < GDT_ENTRY_TLS_MAX; idx++) {
		struct uml_tls_struct* curr = &to->thread.arch.tls_array[idx - GDT_ENTRY_TLS_MIN];

		/* Actually, now if it wasn't flushed it gets cleared and
		 * flushed to the host, which will clear it.*/
		if (!curr->present) {
			if (!curr->flushed) {
				clear_user_desc(&curr->tls);
				curr->tls.entry_number = idx;
			} else {
				WARN_ON(!LDT_empty(&curr->tls));
				continue;
			}
		}

		if (!(flags & O_FORCE) && curr->flushed)
			continue;

		ret = do_set_thread_area(&curr->tls);
		if (ret)
			goto out;

		curr->flushed = 1;
	}
out:
	return ret;
}

/* Verify if we need to do a flush for the new process, i.e. if there are any
 * present desc's, only if they haven't been flushed.
 */
static inline int needs_TLS_update(struct task_struct *task)
{
	int i;
	int ret = 0;

	for (i = GDT_ENTRY_TLS_MIN; i < GDT_ENTRY_TLS_MAX; i++) {
		struct uml_tls_struct* curr = &task->thread.arch.tls_array[i - GDT_ENTRY_TLS_MIN];

		/* Can't test curr->present, we may need to clear a descriptor
		 * which had a value. */
		if (curr->flushed)
			continue;
		ret = 1;
		break;
	}
	return ret;
}

/* On a newly forked process, the TLS descriptors haven't yet been flushed. So
 * we mark them as such and the first switch_to will do the job.
 */
void clear_flushed_tls(struct task_struct *task)
{
	int i;

	for (i = GDT_ENTRY_TLS_MIN; i < GDT_ENTRY_TLS_MAX; i++) {
		struct uml_tls_struct* curr = &task->thread.arch.tls_array[i - GDT_ENTRY_TLS_MIN];

		/* Still correct to do this, if it wasn't present on the host it
		 * will remain as flushed as it was. */
		if (!curr->present)
			continue;

		curr->flushed = 0;
	}
}

/* In SKAS0 mode, currently, multiple guest threads sharing the same ->mm have a
 * common host process. So this is needed in SKAS0 too.
 *
 * However, if each thread had a different host process (and this was discussed
 * for SMP support) this won't be needed.
 *
 * And this will not need be used when (and if) we'll add support to the host
 * SKAS patch. */

int arch_switch_tls_skas(struct task_struct *from, struct task_struct *to)
{
	if (!host_supports_tls)
		return 0;

	/* We have no need whatsoever to switch TLS for kernel threads; beyond
	 * that, that would also result in us calling os_set_thread_area with
	 * userspace_pid[cpu] == 0, which gives an error. */
	if (likely(to->mm))
		return load_TLS(O_FORCE, to);

	return 0;
}

static int set_tls_entry(struct task_struct* task, struct user_desc *info,
			 int idx, int flushed)
{
	struct thread_struct *t = &task->thread;

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	t->arch.tls_array[idx - GDT_ENTRY_TLS_MIN].tls = *info;
	t->arch.tls_array[idx - GDT_ENTRY_TLS_MIN].present = 1;
	t->arch.tls_array[idx - GDT_ENTRY_TLS_MIN].flushed = flushed;

	return 0;
}

int arch_copy_tls(struct task_struct *new)
{
	struct user_desc info;
	int idx, ret = -EFAULT;

	if (copy_from_user(&info,
			   (void __user *) UPT_ESI(&new->thread.regs.regs),
			   sizeof(info)))
		goto out;

	ret = -EINVAL;
	if (LDT_empty(&info))
		goto out;

	idx = info.entry_number;

	ret = set_tls_entry(new, &info, idx, 0);
out:
	return ret;
}

/* XXX: use do_get_thread_area to read the host value? I'm not at all sure! */
static int get_tls_entry(struct task_struct* task, struct user_desc *info, int idx)
{
	struct thread_struct *t = &task->thread;

	if (!t->arch.tls_array)
		goto clear;

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	if (!t->arch.tls_array[idx - GDT_ENTRY_TLS_MIN].present)
		goto clear;

	*info = t->arch.tls_array[idx - GDT_ENTRY_TLS_MIN].tls;

out:
	/* Temporary debugging check, to make sure that things have been
	 * flushed. This could be triggered if load_TLS() failed.
	 */
	if (unlikely(task == current && !t->arch.tls_array[idx - GDT_ENTRY_TLS_MIN].flushed)) {
		printk(KERN_ERR "get_tls_entry: task with pid %d got here "
				"without flushed TLS.", current->pid);
	}

	return 0;
clear:
	/* When the TLS entry has not been set, the values read to user in the
	 * tls_array are 0 (because it's cleared at boot, see
	 * arch/i386/kernel/head.S:cpu_gdt_table). Emulate that.
	 */
	clear_user_desc(info);
	info->entry_number = idx;
	goto out;
}

asmlinkage int sys_set_thread_area(struct user_desc __user *user_desc)
{
	struct user_desc info;
	int idx, ret;

	if (!host_supports_tls)
		return -ENOSYS;

	if (copy_from_user(&info, user_desc, sizeof(info)))
		return -EFAULT;

	idx = info.entry_number;

	if (idx == -1) {
		idx = get_free_idx(current);
		if (idx < 0)
			return idx;
		info.entry_number = idx;
		/* Tell the user which slot we chose for him.*/
		if (put_user(idx, &user_desc->entry_number))
			return -EFAULT;
	}

	ret = CHOOSE_MODE_PROC(do_set_thread_area_tt, do_set_thread_area_skas, &info);
	if (ret)
		return ret;
	return set_tls_entry(current, &info, idx, 1);
}

/*
 * Perform set_thread_area on behalf of the traced child.
 * Note: error handling is not done on the deferred load, and this differ from
 * i386. However the only possible error are caused by bugs.
 */
int ptrace_set_thread_area(struct task_struct *child, int idx,
		struct user_desc __user *user_desc)
{
	struct user_desc info;

	if (!host_supports_tls)
		return -EIO;

	if (copy_from_user(&info, user_desc, sizeof(info)))
		return -EFAULT;

	return set_tls_entry(child, &info, idx, 0);
}

asmlinkage int sys_get_thread_area(struct user_desc __user *user_desc)
{
	struct user_desc info;
	int idx, ret;

	if (!host_supports_tls)
		return -ENOSYS;

	if (get_user(idx, &user_desc->entry_number))
		return -EFAULT;

	ret = get_tls_entry(current, &info, idx);
	if (ret < 0)
		goto out;

	if (copy_to_user(user_desc, &info, sizeof(info)))
		ret = -EFAULT;

out:
	return ret;
}

/*
 * Perform get_thread_area on behalf of the traced child.
 */
int ptrace_get_thread_area(struct task_struct *child, int idx,
		struct user_desc __user *user_desc)
{
	struct user_desc info;
	int ret;

	if (!host_supports_tls)
		return -EIO;

	ret = get_tls_entry(child, &info, idx);
	if (ret < 0)
		goto out;

	if (copy_to_user(user_desc, &info, sizeof(info)))
		ret = -EFAULT;
out:
	return ret;
}


/* XXX: This part is probably common to i386 and x86-64. Don't create a common
 * file for now, do that when implementing x86-64 support.*/
static int __init __setup_host_supports_tls(void)
{
	check_host_supports_tls(&host_supports_tls, &host_gdt_entry_tls_min);
	if (host_supports_tls) {
		printk(KERN_INFO "Host TLS support detected\n");
		printk(KERN_INFO "Detected host type: ");
		switch (host_gdt_entry_tls_min) {
			case GDT_ENTRY_TLS_MIN_I386:
				printk("i386\n");
				break;
			case GDT_ENTRY_TLS_MIN_X86_64:
				printk("x86_64\n");
				break;
		}
	} else
		printk(KERN_ERR "  Host TLS support NOT detected! "
				"TLS support inside UML will not work\n");
	return 0;
}

__initcall(__setup_host_supports_tls);
