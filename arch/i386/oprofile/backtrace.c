/**
 * @file backtrace.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author David Smith
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>

struct frame_head {
	struct frame_head * ebp;
	unsigned long ret;
} __attribute__((packed));

static struct frame_head *
dump_backtrace(struct frame_head * head)
{
	oprofile_add_trace(head->ret);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= head->ebp)
		return NULL;

	return head->ebp;
}

/* check that the page(s) containing the frame head are present */
static int pages_present(struct frame_head * head)
{
	struct mm_struct * mm = current->mm;

	/* FIXME: only necessary once per page */
	if (!check_user_page_readable(mm, (unsigned long)head))
		return 0;

	return check_user_page_readable(mm, (unsigned long)(head + 1));
}

/*
 * |             | /\ Higher addresses
 * |             |
 * --------------- stack base (address of current_thread_info)
 * | thread info |
 * .             .
 * |    stack    |
 * --------------- saved regs->ebp value if valid (frame_head address)
 * .             .
 * --------------- struct pt_regs stored on stack (struct pt_regs *)
 * |             |
 * .             .
 * |             |
 * --------------- %esp
 * |             |
 * |             | \/ Lower addresses
 *
 * Thus, &pt_regs <-> stack base restricts the valid(ish) ebp values
 */
#ifdef CONFIG_FRAME_POINTER
static int valid_kernel_stack(struct frame_head * head, struct pt_regs * regs)
{
	unsigned long headaddr = (unsigned long)head;
	unsigned long stack = (unsigned long)regs;
	unsigned long stack_base = (stack & ~(THREAD_SIZE - 1)) + THREAD_SIZE;

	return headaddr > stack && headaddr < stack_base;
}
#else
/* without fp, it's just junk */
static int valid_kernel_stack(struct frame_head * head, struct pt_regs * regs)
{
	return 0;
}
#endif


void
x86_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	struct frame_head *head;

#ifdef CONFIG_X86_64
	head = (struct frame_head *)regs->rbp;
#else
	head = (struct frame_head *)regs->ebp;
#endif

	if (!user_mode_vm(regs)) {
		while (depth-- && valid_kernel_stack(head, regs))
			head = dump_backtrace(head);
		return;
	}

#ifdef CONFIG_SMP
	if (!spin_trylock(&current->mm->page_table_lock))
		return;
#endif

	while (depth-- && head && pages_present(head))
		head = dump_backtrace(head);

#ifdef CONFIG_SMP
	spin_unlock(&current->mm->page_table_lock);
#endif
}
