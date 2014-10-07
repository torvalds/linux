/*
 * Dynamic function tracer architecture backend.
 *
 * Copyright IBM Corp. 2009,2014
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *		Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kprobes.h>
#include <trace/syscall.h>
#include <asm/asm-offsets.h>
#include "entry.h"

void mcount_replace_code(void);
void ftrace_disable_code(void);
void ftrace_enable_insn(void);

/*
 * The mcount code looks like this:
 *	stg	%r14,8(%r15)		# offset 0
 *	larl	%r1,<&counter>		# offset 6
 *	brasl	%r14,_mcount		# offset 12
 *	lg	%r14,8(%r15)		# offset 18
 * Total length is 24 bytes. The complete mcount block initially gets replaced
 * by ftrace_make_nop. Subsequent calls to ftrace_make_call / ftrace_make_nop
 * only patch the jg/lg instruction within the block.
 * Note: we do not patch the first instruction to an unconditional branch,
 * since that would break kprobes/jprobes. It is easier to leave the larl
 * instruction in and only modify the second instruction.
 * The enabled ftrace code block looks like this:
 *	larl	%r0,.+24		# offset 0
 * >	lg	%r1,__LC_FTRACE_FUNC	# offset 6
 *	br	%r1			# offset 12
 *	brcl	0,0			# offset 14
 *	brc	0,0			# offset 20
 * The ftrace function gets called with a non-standard C function call ABI
 * where r0 contains the return address. It is also expected that the called
 * function only clobbers r0 and r1, but restores r2-r15.
 * The return point of the ftrace function has offset 24, so execution
 * continues behind the mcount block.
 *	larl	%r0,.+24		# offset 0
 * >	jg	.+18			# offset 6
 *	br	%r1			# offset 12
 *	brcl	0,0			# offset 14
 *	brc	0,0			# offset 20
 * The jg instruction branches to offset 24 to skip as many instructions
 * as possible.
 */
asm(
	"	.align	4\n"
	"mcount_replace_code:\n"
	"	larl	%r0,0f\n"
	"ftrace_disable_code:\n"
	"	jg	0f\n"
	"	br	%r1\n"
	"	brcl	0,0\n"
	"	brc	0,0\n"
	"0:\n"
	"	.align	4\n"
	"ftrace_enable_insn:\n"
	"	lg	%r1,"__stringify(__LC_FTRACE_FUNC)"\n");

#define MCOUNT_BLOCK_SIZE	24
#define MCOUNT_INSN_OFFSET	6
#define FTRACE_INSN_SIZE	6

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
		       unsigned long addr)
{
	return 0;
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	/* Initial replacement of the whole mcount block */
	if (addr == MCOUNT_ADDR) {
		if (probe_kernel_write((void *) rec->ip - MCOUNT_INSN_OFFSET,
				       mcount_replace_code,
				       MCOUNT_BLOCK_SIZE))
			return -EPERM;
		return 0;
	}
	if (probe_kernel_write((void *) rec->ip, ftrace_disable_code,
			       MCOUNT_INSN_SIZE))
		return -EPERM;
	return 0;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	if (probe_kernel_write((void *) rec->ip, ftrace_enable_insn,
			       FTRACE_INSN_SIZE))
		return -EPERM;
	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	return 0;
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * Hook the return address and push it in the stack of return addresses
 * in current thread info.
 */
unsigned long __kprobes prepare_ftrace_return(unsigned long parent,
					      unsigned long ip)
{
	struct ftrace_graph_ent trace;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		goto out;
	ip = (ip & PSW_ADDR_INSN) - MCOUNT_INSN_SIZE;
	trace.func = ip;
	trace.depth = current->curr_ret_stack + 1;
	/* Only trace if the calling function expects to. */
	if (!ftrace_graph_entry(&trace))
		goto out;
	if (ftrace_push_return_trace(parent, ip, &trace.depth, 0) == -EBUSY)
		goto out;
	parent = (unsigned long) return_to_handler;
out:
	return parent;
}

/*
 * Patch the kernel code at ftrace_graph_caller location. The instruction
 * there is branch relative and save to prepare_ftrace_return. To disable
 * the call to prepare_ftrace_return we patch the bras offset to point
 * directly after the instructions. To enable the call we calculate
 * the original offset to prepare_ftrace_return and put it back.
 */

int ftrace_enable_ftrace_graph_caller(void)
{
	static unsigned short offset = 0x0002;

	return probe_kernel_write((void *) ftrace_graph_caller + 2,
				  &offset, sizeof(offset));
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned short offset;

	offset = ((void *) &ftrace_graph_caller_end -
		  (void *) ftrace_graph_caller) / 2;
	return probe_kernel_write((void *) ftrace_graph_caller + 2,
				  &offset, sizeof(offset));
}

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
