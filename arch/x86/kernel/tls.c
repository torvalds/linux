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

/*
 * Set a given TLS descriptor:
 */
int do_set_thread_area(struct task_struct *p, int idx,
		       struct user_desc __user *u_info,
		       int can_allocate)
{
	struct thread_struct *t = &p->thread;
	struct user_desc info;
	u32 *desc;
	int cpu;

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

	desc = (u32 *) &t->tls_array[idx - GDT_ENTRY_TLS_MIN];

	/*
	 * We must not get preempted while modifying the TLS.
	 */
	cpu = get_cpu();

	if (LDT_empty(&info)) {
		desc[0] = 0;
		desc[1] = 0;
	} else
		fill_ldt((struct desc_struct *)desc, &info);

	if (t == &current->thread)
		load_TLS(t, cpu);

	put_cpu();
	return 0;
}

asmlinkage int sys_set_thread_area(struct user_desc __user *u_info)
{
	return do_set_thread_area(current, -1, u_info, 1);
}


/*
 * Get the current Thread-Local Storage area:
 */

#define GET_LIMIT(desc)		(((desc)[0] & 0x0ffff) | ((desc)[1] & 0xf0000))
#define GET_32BIT(desc)		(((desc)[1] >> 22) & 1)
#define GET_CONTENTS(desc)	(((desc)[1] >> 10) & 3)
#define GET_WRITABLE(desc)	(((desc)[1] >>  9) & 1)
#define GET_LIMIT_PAGES(desc)	(((desc)[1] >> 23) & 1)
#define GET_PRESENT(desc)	(((desc)[1] >> 15) & 1)
#define GET_USEABLE(desc)	(((desc)[1] >> 20) & 1)
#define GET_LONGMODE(desc)	(((desc)[1] >> 21) & 1)

int do_get_thread_area(struct task_struct *p, int idx,
		       struct user_desc __user *u_info)
{
	struct thread_struct *t = &p->thread;
	struct user_desc info;
	u32 *desc;

	if (idx == -1 && get_user(idx, &u_info->entry_number))
		return -EFAULT;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = (u32 *) &t->tls_array[idx - GDT_ENTRY_TLS_MIN];

	memset(&info, 0, sizeof(struct user_desc));
	info.entry_number = idx;
	info.base_addr = get_desc_base((void *)desc);
	info.limit = GET_LIMIT(desc);
	info.seg_32bit = GET_32BIT(desc);
	info.contents = GET_CONTENTS(desc);
	info.read_exec_only = !GET_WRITABLE(desc);
	info.limit_in_pages = GET_LIMIT_PAGES(desc);
	info.seg_not_present = !GET_PRESENT(desc);
	info.useable = GET_USEABLE(desc);
#ifdef CONFIG_X86_64
	info.lm = GET_LONGMODE(desc);
#endif

	if (copy_to_user(u_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

asmlinkage int sys_get_thread_area(struct user_desc __user *u_info)
{
	return do_get_thread_area(current, -1, u_info);
}
