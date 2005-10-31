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
#include <asm/uaccess.h>

struct frame_head {
	struct frame_head * ebp;
	unsigned long ret;
} __attribute__((packed));

static struct frame_head *
dump_backtrace(struct frame_head * head)
{
	struct frame_head bufhead[2];

	/* Also check accessibility of one struct frame_head beyond */
	if (!access_ok(VERIFY_READ, head, sizeof(bufhead)))
		return NULL;
	if (__copy_from_user_inatomic(bufhead, head, sizeof(bufhead)))
		return NULL;

	oprofile_add_trace(bufhead[0].ret);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= bufhead[0].ebp)
		return NULL;

	return bufhead[0].ebp;
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

	while (depth-- && head)
		head = dump_backtrace(head);
}
