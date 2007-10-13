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
		if (desc_empty((struct n_desc_struct *)(t->tls_array) + idx))
			return idx + GDT_ENTRY_TLS_MIN;
	return -ESRCH;
}

/*
 * Set a given TLS descriptor:
 * When you want addresses > 32bit use arch_prctl() 
 */
int do_set_thread_area(struct thread_struct *t, struct user_desc __user *u_info)
{
	struct user_desc info;
	struct n_desc_struct *desc;
	int cpu, idx;

	if (copy_from_user(&info, u_info, sizeof(info)))
		return -EFAULT;

	idx = info.entry_number;

	/*
	 * index -1 means the kernel should try to find and
	 * allocate an empty descriptor:
	 */
	if (idx == -1) {
		idx = get_free_idx();
		if (idx < 0)
			return idx;
		if (put_user(idx, &u_info->entry_number))
			return -EFAULT;
	}

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = ((struct n_desc_struct *)t->tls_array) + idx - GDT_ENTRY_TLS_MIN;

	/*
	 * We must not get preempted while modifying the TLS.
	 */
	cpu = get_cpu();

	if (LDT_empty(&info)) {
		desc->a = 0;
		desc->b = 0;
	} else {
		desc->a = LDT_entry_a(&info);
		desc->b = LDT_entry_b(&info);
	}
	if (t == &current->thread)
		load_TLS(t, cpu);

	put_cpu();
	return 0;
}

asmlinkage long sys32_set_thread_area(struct user_desc __user *u_info)
{ 
	return do_set_thread_area(&current->thread, u_info); 
} 


/*
 * Get the current Thread-Local Storage area:
 */

#define GET_BASE(desc) ( \
	(((desc)->a >> 16) & 0x0000ffff) | \
	(((desc)->b << 16) & 0x00ff0000) | \
	( (desc)->b        & 0xff000000)   )

#define GET_LIMIT(desc) ( \
	((desc)->a & 0x0ffff) | \
	 ((desc)->b & 0xf0000) )
	
#define GET_32BIT(desc)		(((desc)->b >> 22) & 1)
#define GET_CONTENTS(desc)	(((desc)->b >> 10) & 3)
#define GET_WRITABLE(desc)	(((desc)->b >>  9) & 1)
#define GET_LIMIT_PAGES(desc)	(((desc)->b >> 23) & 1)
#define GET_PRESENT(desc)	(((desc)->b >> 15) & 1)
#define GET_USEABLE(desc)	(((desc)->b >> 20) & 1)
#define GET_LONGMODE(desc)	(((desc)->b >> 21) & 1)

int do_get_thread_area(struct thread_struct *t, struct user_desc __user *u_info)
{
	struct user_desc info;
	struct n_desc_struct *desc;
	int idx;

	if (get_user(idx, &u_info->entry_number))
		return -EFAULT;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = ((struct n_desc_struct *)t->tls_array) + idx - GDT_ENTRY_TLS_MIN;

	memset(&info, 0, sizeof(struct user_desc));
	info.entry_number = idx;
	info.base_addr = GET_BASE(desc);
	info.limit = GET_LIMIT(desc);
	info.seg_32bit = GET_32BIT(desc);
	info.contents = GET_CONTENTS(desc);
	info.read_exec_only = !GET_WRITABLE(desc);
	info.limit_in_pages = GET_LIMIT_PAGES(desc);
	info.seg_not_present = !GET_PRESENT(desc);
	info.useable = GET_USEABLE(desc);
	info.lm = GET_LONGMODE(desc);

	if (copy_to_user(u_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

asmlinkage long sys32_get_thread_area(struct user_desc __user *u_info)
{
	return do_get_thread_area(&current->thread, u_info);
} 


int ia32_child_tls(struct task_struct *p, struct pt_regs *childregs)
{
	struct n_desc_struct *desc;
	struct user_desc info;
	struct user_desc __user *cp;
	int idx;
	
	cp = (void __user *)childregs->rsi;
	if (copy_from_user(&info, cp, sizeof(info)))
		return -EFAULT;
	if (LDT_empty(&info))
		return -EINVAL;
	
	idx = info.entry_number;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;
	
	desc = (struct n_desc_struct *)(p->thread.tls_array) + idx - GDT_ENTRY_TLS_MIN;
	desc->a = LDT_entry_a(&info);
	desc->b = LDT_entry_b(&info);

	return 0;
}
