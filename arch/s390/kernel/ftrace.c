/*
 * Dynamic function tracer architecture backend.
 *
 * Copyright IBM Corp. 2009
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

#ifdef CONFIG_DYNAMIC_FTRACE

void ftrace_disable_code(void);
void ftrace_enable_insn(void);

#ifdef CONFIG_64BIT
/*
 * The 64-bit mcount code looks like this:
 *	stg	%r14,8(%r15)		# offset 0
 * >	larl	%r1,<&counter>		# offset 6
 * >	brasl	%r14,_mcount		# offset 12
 *	lg	%r14,8(%r15)		# offset 18
 * Total length is 24 bytes. The middle two instructions of the mcount
 * block get overwritten by ftrace_make_nop / ftrace_make_call.
 * The 64-bit enabled ftrace code block looks like this:
 *	stg	%r14,8(%r15)		# offset 0
 * >	lg	%r1,__LC_FTRACE_FUNC	# offset 6
 * >	lgr	%r0,%r0			# offset 12
 * >	basr	%r14,%r1		# offset 16
 *	lg	%r14,8(%15)		# offset 18
 * The return points of the mcount/ftrace function have the same offset 18.
 * The 64-bit disable ftrace code block looks like this:
 *	stg	%r14,8(%r15)		# offset 0
 * >	jg	.+18			# offset 6
 * >	lgr	%r0,%r0			# offset 12
 * >	basr	%r14,%r1		# offset 16
 *	lg	%r14,8(%15)		# offset 18
 * The jg instruction branches to offset 24 to skip as many instructions
 * as possible.
 */
asm(
	"	.align	4\n"
	"ftrace_disable_code:\n"
	"	jg	0f\n"
	"	lgr	%r0,%r0\n"
	"	basr	%r14,%r1\n"
	"0:\n"
	"	.align	4\n"
	"ftrace_enable_insn:\n"
	"	lg	%r1,"__stringify(__LC_FTRACE_FUNC)"\n");

#define FTRACE_INSN_SIZE	6

#else /* CONFIG_64BIT */
/*
 * The 31-bit mcount code looks like this:
 *	st	%r14,4(%r15)		# offset 0
 * >	bras	%r1,0f			# offset 4
 * >	.long	_mcount			# offset 8
 * >	.long	<&counter>		# offset 12
 * > 0:	l	%r14,0(%r1)		# offset 16
 * >	l	%r1,4(%r1)		# offset 20
 *	basr	%r14,%r14		# offset 24
 *	l	%r14,4(%r15)		# offset 26
 * Total length is 30 bytes. The twenty bytes starting from offset 4
 * to offset 24 get overwritten by ftrace_make_nop / ftrace_make_call.
 * The 31-bit enabled ftrace code block looks like this:
 *	st	%r14,4(%r15)		# offset 0
 * >	l	%r14,__LC_FTRACE_FUNC	# offset 4
 * >	j	0f			# offset 8
 * >	.fill	12,1,0x07		# offset 12
 *   0:	basr	%r14,%r14		# offset 24
 *	l	%r14,4(%r14)		# offset 26
 * The return points of the mcount/ftrace function have the same offset 26.
 * The 31-bit disabled ftrace code block looks like this:
 *	st	%r14,4(%r15)		# offset 0
 * >	j	.+26			# offset 4
 * >	j	0f			# offset 8
 * >	.fill	12,1,0x07		# offset 12
 *   0:	basr	%r14,%r14		# offset 24
 *	l	%r14,4(%r14)		# offset 26
 * The j instruction branches to offset 30 to skip as many instructions
 * as possible.
 */
asm(
	"	.align	4\n"
	"ftrace_disable_code:\n"
	"	j	1f\n"
	"	j	0f\n"
	"	.fill	12,1,0x07\n"
	"0:	basr	%r14,%r14\n"
	"1:\n"
	"	.align	4\n"
	"ftrace_enable_insn:\n"
	"	l	%r14,"__stringify(__LC_FTRACE_FUNC)"\n");

#define FTRACE_INSN_SIZE	4

#endif /* CONFIG_64BIT */


int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
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

#endif /* CONFIG_DYNAMIC_FTRACE */

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

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * Patch the kernel code at ftrace_graph_caller location. The instruction
 * there is branch relative and save to prepare_ftrace_return. To disable
 * the call to prepare_ftrace_return we patch the bras offset to point
 * directly after the instructions. To enable the call we calculate
 * the original offset to prepare_ftrace_return and put it back.
 */
int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned short offset;

	offset = ((void *) prepare_ftrace_return -
		  (void *) ftrace_graph_caller) / 2;
	return probe_kernel_write((void *) ftrace_graph_caller + 2,
				  &offset, sizeof(offset));
}

int ftrace_disable_ftrace_graph_caller(void)
{
	static unsigned short offset = 0x0002;

	return probe_kernel_write((void *) ftrace_graph_caller + 2,
				  &offset, sizeof(offset));
}

#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
