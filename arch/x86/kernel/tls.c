#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/desc.h>
#include <asm/system.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/proto.h>

/*
 * sys_alloc_thread_area: get a yet unused TLS descriptor index.
 */
static int get_free_idx(void)
{
	struct thread_struct *t = &current->thread;
	int idx;

	for (idx = 0; idx < GDT_ENTRY_TLS_ENTRIES; idx++)
		if (desc_empty(&t->tls_array[idx]))
			return idx + GDT_ENTRY_TLS_MIN;
	return -ESRCH;
}

static void set_tls_desc(struct task_struct *p, int idx,
			 const struct user_desc *info)
{
	struct thread_struct *t = &p->thread;
	struct desc_struct *desc = &t->tls_array[idx - GDT_ENTRY_TLS_MIN];
	int cpu;

	/*
	 * We must not get preempted while modifying the TLS.
	 */
	cpu = get_cpu();

	if (LDT_empty(info))
		desc->a = desc->b = 0;
	else
		fill_ldt(desc, info);

	if (t == &current->thread)
		load_TLS(t, cpu);

	put_cpu();
}

/*
 * Set a given TLS descriptor:
 */
int do_set_thread_area(struct task_struct *p, int idx,
		       struct user_desc __user *u_info,
		       int can_allocate)
{
	struct user_desc info;

	if (copy_from_user(&info, u_info, sizeof(info)))
		return -EFAULT;

	if (idx == -1)
		idx = info.entry_number;

	/*
	 * index -1 means the kernel should try to find and
	 * allocate an empty descriptor:
	 */
	if (idx == -1 && can_allocate) {
		idx = get_free_idx();
		if (idx < 0)
			return idx;
		if (put_user(idx, &u_info->entry_number))
			return -EFAULT;
	}

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	set_tls_desc(p, idx, &info);

	return 0;
}

asmlinkage int sys_set_thread_area(struct user_desc __user *u_info)
{
	return do_set_thread_area(current, -1, u_info, 1);
}


/*
 * Get the current Thread-Local Storage area:
 */

static void fill_user_desc(struct user_desc *info, int idx,
			   const struct desc_struct *desc)

{
	memset(info, 0, sizeof(*info));
	info->entry_number = idx;
	info->base_addr = get_desc_base(desc);
	info->limit = get_desc_limit(desc);
	info->seg_32bit = desc->d;
	info->contents = desc->type >> 2;
	info->read_exec_only = !(desc->type & 2);
	info->limit_in_pages = desc->g;
	info->seg_not_present = !desc->p;
	info->useable = desc->avl;
#ifdef CONFIG_X86_64
	info->lm = desc->l;
#endif
}

int do_get_thread_area(struct task_struct *p, int idx,
		       struct user_desc __user *u_info)
{
	struct user_desc info;

	if (idx == -1 && get_user(idx, &u_info->entry_number))
		return -EFAULT;

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	fill_user_desc(&info, idx,
		       &p->thread.tls_array[idx - GDT_ENTRY_TLS_MIN]);

	if (copy_to_user(u_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

asmlinkage int sys_get_thread_area(struct user_desc __user *u_info)
{
	return do_get_thread_area(current, -1, u_info);
}
