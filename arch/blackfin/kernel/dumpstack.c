/* Provide basic stack dumping functions
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/kernel.h>
#include <linux/thread_info.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/sched/debug.h>

#include <asm/trace.h>

/*
 * Checks to see if the address pointed to is either a
 * 16-bit CALL instruction, or a 32-bit CALL instruction
 */
static bool is_bfin_call(unsigned short *addr)
{
	unsigned int opcode;

	if (!get_instruction(&opcode, addr))
		return false;

	if ((opcode >= 0x0060 && opcode <= 0x0067) ||
	    (opcode >= 0x0070 && opcode <= 0x0077) ||
	    (opcode >= 0xE3000000 && opcode <= 0xE3FFFFFF))
		return true;

	return false;

}

void show_stack(struct task_struct *task, unsigned long *stack)
{
#ifdef CONFIG_PRINTK
	unsigned int *addr, *endstack, *fp = 0, *frame;
	unsigned short *ins_addr;
	char buf[150];
	unsigned int i, j, ret_addr, frame_no = 0;

	/*
	 * If we have been passed a specific stack, use that one otherwise
	 *    if we have been passed a task structure, use that, otherwise
	 *    use the stack of where the variable "stack" exists
	 */

	if (stack == NULL) {
		if (task) {
			/* We know this is a kernel stack, so this is the start/end */
			stack = (unsigned long *)task->thread.ksp;
			endstack = (unsigned int *)(((unsigned int)(stack) & ~(THREAD_SIZE - 1)) + THREAD_SIZE);
		} else {
			/* print out the existing stack info */
			stack = (unsigned long *)&stack;
			endstack = (unsigned int *)PAGE_ALIGN((unsigned int)stack);
		}
	} else
		endstack = (unsigned int *)PAGE_ALIGN((unsigned int)stack);

	printk(KERN_NOTICE "Stack info:\n");
	decode_address(buf, (unsigned int)stack);
	printk(KERN_NOTICE " SP: [0x%p] %s\n", stack, buf);

	if (!access_ok(VERIFY_READ, stack, (unsigned int)endstack - (unsigned int)stack)) {
		printk(KERN_NOTICE "Invalid stack pointer\n");
		return;
	}

	/* First thing is to look for a frame pointer */
	for (addr = (unsigned int *)((unsigned int)stack & ~0xF); addr < endstack; addr++) {
		if (*addr & 0x1)
			continue;
		ins_addr = (unsigned short *)*addr;
		ins_addr--;
		if (is_bfin_call(ins_addr))
			fp = addr - 1;

		if (fp) {
			/* Let's check to see if it is a frame pointer */
			while (fp >= (addr - 1) && fp < endstack
			       && fp && ((unsigned int) fp & 0x3) == 0)
				fp = (unsigned int *)*fp;
			if (fp == 0 || fp == endstack) {
				fp = addr - 1;
				break;
			}
			fp = 0;
		}
	}
	if (fp) {
		frame = fp;
		printk(KERN_NOTICE " FP: (0x%p)\n", fp);
	} else
		frame = 0;

	/*
	 * Now that we think we know where things are, we
	 * walk the stack again, this time printing things out
	 * incase there is no frame pointer, we still look for
	 * valid return addresses
	 */

	/* First time print out data, next time, print out symbols */
	for (j = 0; j <= 1; j++) {
		if (j)
			printk(KERN_NOTICE "Return addresses in stack:\n");
		else
			printk(KERN_NOTICE " Memory from 0x%08lx to %p", ((long unsigned int)stack & ~0xF), endstack);

		fp = frame;
		frame_no = 0;

		for (addr = (unsigned int *)((unsigned int)stack & ~0xF), i = 0;
		     addr < endstack; addr++, i++) {

			ret_addr = 0;
			if (!j && i % 8 == 0)
				printk(KERN_NOTICE "%p:", addr);

			/* if it is an odd address, or zero, just skip it */
			if (*addr & 0x1 || !*addr)
				goto print;

			ins_addr = (unsigned short *)*addr;

			/* Go back one instruction, and see if it is a CALL */
			ins_addr--;
			ret_addr = is_bfin_call(ins_addr);
 print:
			if (!j && stack == (unsigned long *)addr)
				printk("[%08x]", *addr);
			else if (ret_addr)
				if (j) {
					decode_address(buf, (unsigned int)*addr);
					if (frame == addr) {
						printk(KERN_NOTICE "   frame %2i : %s\n", frame_no, buf);
						continue;
					}
					printk(KERN_NOTICE "    address : %s\n", buf);
				} else
					printk("<%08x>", *addr);
			else if (fp == addr) {
				if (j)
					frame = addr+1;
				else
					printk("(%08x)", *addr);

				fp = (unsigned int *)*addr;
				frame_no++;

			} else if (!j)
				printk(" %08x ", *addr);
		}
		if (!j)
			printk("\n");
	}
#endif
}
EXPORT_SYMBOL(show_stack);

void dump_stack(void)
{
	unsigned long stack;
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int tflags;
#endif
	trace_buffer_save(tflags);
	dump_bfin_trace_buffer();
	dump_stack_print_info(KERN_DEFAULT);
	show_stack(current, &stack);
	trace_buffer_restore(tflags);
}
EXPORT_SYMBOL(dump_stack);
