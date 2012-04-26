/*
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * Thanks goes out to P.A. Semi, Inc for supplying me with a PPC64 box.
 *
 * Added function graph tracer code, taken from x86 that was written
 * by Frederic Weisbecker, and ported to PPC by Steven Rostedt.
 *
 */

#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>

#include <asm/cacheflush.h>
#include <asm/code-patching.h>
#include <asm/ftrace.h>
#include <asm/syscall.h>


#ifdef CONFIG_DYNAMIC_FTRACE
static unsigned int
ftrace_call_replace(unsigned long ip, unsigned long addr, int link)
{
	unsigned int op;

	addr = ppc_function_entry((void *)addr);

	/* if (link) set op to 'bl' else 'b' */
	op = create_branch((unsigned int *)ip, addr, link ? 1 : 0);

	return op;
}

static int
ftrace_modify_code(unsigned long ip, unsigned int old, unsigned int new)
{
	unsigned int replaced;

	/*
	 * Note: Due to modules and __init, code can
	 *  disappear and change, we need to protect against faulting
	 *  as well as code changing. We do this by using the
	 *  probe_kernel_* functions.
	 *
	 * No real locking needed, this code is run through
	 * kstop_machine, or before SMP starts.
	 */

	/* read the text we want to modify */
	if (probe_kernel_read(&replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* Make sure it is what we expect it to be */
	if (replaced != old)
		return -EINVAL;

	/* replace the text with the new text */
	if (patch_instruction((unsigned int *)ip, new))
		return -EPERM;

	return 0;
}

/*
 * Helper functions that are the same for both PPC64 and PPC32.
 */
static int test_24bit_addr(unsigned long ip, unsigned long addr)
{

	/* use the create_branch to verify that this offset can be branched */
	return create_branch((unsigned int *)ip, addr, 0);
}

#ifdef CONFIG_MODULES

static int is_bl_op(unsigned int op)
{
	return (op & 0xfc000003) == 0x48000001;
}

static unsigned long find_bl_target(unsigned long ip, unsigned int op)
{
	static int offset;

	offset = (op & 0x03fffffc);
	/* make it signed */
	if (offset & 0x02000000)
		offset |= 0xfe000000;

	return ip + (long)offset;
}

#ifdef CONFIG_PPC64
static int
__ftrace_make_nop(struct module *mod,
		  struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int op;
	unsigned int jmp[5];
	unsigned long ptr;
	unsigned long ip = rec->ip;
	unsigned long tramp;
	int offset;

	/* read where this goes */
	if (probe_kernel_read(&op, (void *)ip, sizeof(int)))
		return -EFAULT;

	/* Make sure that that this is still a 24bit jump */
	if (!is_bl_op(op)) {
		printk(KERN_ERR "Not expected bl: opcode is %x\n", op);
		return -EINVAL;
	}

	/* lets find where the pointer goes */
	tramp = find_bl_target(ip, op);

	/*
	 * On PPC64 the trampoline looks like:
	 * 0x3d, 0x82, 0x00, 0x00,    addis   r12,r2, <high>
	 * 0x39, 0x8c, 0x00, 0x00,    addi    r12,r12, <low>
	 *   Where the bytes 2,3,6 and 7 make up the 32bit offset
	 *   to the TOC that holds the pointer.
	 *   to jump to.
	 * 0xf8, 0x41, 0x00, 0x28,    std     r2,40(r1)
	 * 0xe9, 0x6c, 0x00, 0x20,    ld      r11,32(r12)
	 *   The actually address is 32 bytes from the offset
	 *   into the TOC.
	 * 0xe8, 0x4c, 0x00, 0x28,    ld      r2,40(r12)
	 */

	pr_devel("ip:%lx jumps to %lx r2: %lx", ip, tramp, mod->arch.toc);

	/* Find where the trampoline jumps to */
	if (probe_kernel_read(jmp, (void *)tramp, sizeof(jmp))) {
		printk(KERN_ERR "Failed to read %lx\n", tramp);
		return -EFAULT;
	}

	pr_devel(" %08x %08x", jmp[0], jmp[1]);

	/* verify that this is what we expect it to be */
	if (((jmp[0] & 0xffff0000) != 0x3d820000) ||
	    ((jmp[1] & 0xffff0000) != 0x398c0000) ||
	    (jmp[2] != 0xf8410028) ||
	    (jmp[3] != 0xe96c0020) ||
	    (jmp[4] != 0xe84c0028)) {
		printk(KERN_ERR "Not a trampoline\n");
		return -EINVAL;
	}

	/* The bottom half is signed extended */
	offset = ((unsigned)((unsigned short)jmp[0]) << 16) +
		(int)((short)jmp[1]);

	pr_devel(" %x ", offset);

	/* get the address this jumps too */
	tramp = mod->arch.toc + offset + 32;
	pr_devel("toc: %lx", tramp);

	if (probe_kernel_read(jmp, (void *)tramp, 8)) {
		printk(KERN_ERR "Failed to read %lx\n", tramp);
		return -EFAULT;
	}

	pr_devel(" %08x %08x\n", jmp[0], jmp[1]);

	ptr = ((unsigned long)jmp[0] << 32) + jmp[1];

	/* This should match what was called */
	if (ptr != ppc_function_entry((void *)addr)) {
		printk(KERN_ERR "addr does not match %lx\n", ptr);
		return -EINVAL;
	}

	/*
	 * We want to nop the line, but the next line is
	 *  0xe8, 0x41, 0x00, 0x28   ld r2,40(r1)
	 * This needs to be turned to a nop too.
	 */
	if (probe_kernel_read(&op, (void *)(ip+4), MCOUNT_INSN_SIZE))
		return -EFAULT;

	if (op != 0xe8410028) {
		printk(KERN_ERR "Next line is not ld! (%08x)\n", op);
		return -EINVAL;
	}

	/*
	 * Milton Miller pointed out that we can not blindly do nops.
	 * If a task was preempted when calling a trace function,
	 * the nops will remove the way to restore the TOC in r2
	 * and the r2 TOC will get corrupted.
	 */

	/*
	 * Replace:
	 *   bl <tramp>  <==== will be replaced with "b 1f"
	 *   ld r2,40(r1)
	 *  1:
	 */
	op = 0x48000008;	/* b +8 */

	if (patch_instruction((unsigned int *)ip, op))
		return -EPERM;

	return 0;
}

#else /* !PPC64 */
static int
__ftrace_make_nop(struct module *mod,
		  struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int op;
	unsigned int jmp[4];
	unsigned long ip = rec->ip;
	unsigned long tramp;

	if (probe_kernel_read(&op, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* Make sure that that this is still a 24bit jump */
	if (!is_bl_op(op)) {
		printk(KERN_ERR "Not expected bl: opcode is %x\n", op);
		return -EINVAL;
	}

	/* lets find where the pointer goes */
	tramp = find_bl_target(ip, op);

	/*
	 * On PPC32 the trampoline looks like:
	 *  0x3d, 0x60, 0x00, 0x00  lis r11,sym@ha
	 *  0x39, 0x6b, 0x00, 0x00  addi r11,r11,sym@l
	 *  0x7d, 0x69, 0x03, 0xa6  mtctr r11
	 *  0x4e, 0x80, 0x04, 0x20  bctr
	 */

	pr_devel("ip:%lx jumps to %lx", ip, tramp);

	/* Find where the trampoline jumps to */
	if (probe_kernel_read(jmp, (void *)tramp, sizeof(jmp))) {
		printk(KERN_ERR "Failed to read %lx\n", tramp);
		return -EFAULT;
	}

	pr_devel(" %08x %08x ", jmp[0], jmp[1]);

	/* verify that this is what we expect it to be */
	if (((jmp[0] & 0xffff0000) != 0x3d600000) ||
	    ((jmp[1] & 0xffff0000) != 0x396b0000) ||
	    (jmp[2] != 0x7d6903a6) ||
	    (jmp[3] != 0x4e800420)) {
		printk(KERN_ERR "Not a trampoline\n");
		return -EINVAL;
	}

	tramp = (jmp[1] & 0xffff) |
		((jmp[0] & 0xffff) << 16);
	if (tramp & 0x8000)
		tramp -= 0x10000;

	pr_devel(" %lx ", tramp);

	if (tramp != addr) {
		printk(KERN_ERR
		       "Trampoline location %08lx does not match addr\n",
		       tramp);
		return -EINVAL;
	}

	op = PPC_INST_NOP;

	if (patch_instruction((unsigned int *)ip, op))
		return -EPERM;

	return 0;
}
#endif /* PPC64 */
#endif /* CONFIG_MODULES */

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned int old, new;

	/*
	 * If the calling address is more that 24 bits away,
	 * then we had to use a trampoline to make the call.
	 * Otherwise just update the call site.
	 */
	if (test_24bit_addr(ip, addr)) {
		/* within range */
		old = ftrace_call_replace(ip, addr, 1);
		new = PPC_INST_NOP;
		return ftrace_modify_code(ip, old, new);
	}

#ifdef CONFIG_MODULES
	/*
	 * Out of range jumps are called from modules.
	 * We should either already have a pointer to the module
	 * or it has been passed in.
	 */
	if (!rec->arch.mod) {
		if (!mod) {
			printk(KERN_ERR "No module loaded addr=%lx\n",
			       addr);
			return -EFAULT;
		}
		rec->arch.mod = mod;
	} else if (mod) {
		if (mod != rec->arch.mod) {
			printk(KERN_ERR
			       "Record mod %p not equal to passed in mod %p\n",
			       rec->arch.mod, mod);
			return -EINVAL;
		}
		/* nothing to do if mod == rec->arch.mod */
	} else
		mod = rec->arch.mod;

	return __ftrace_make_nop(mod, rec, addr);
#else
	/* We should not get here without modules */
	return -EINVAL;
#endif /* CONFIG_MODULES */
}

#ifdef CONFIG_MODULES
#ifdef CONFIG_PPC64
static int
__ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int op[2];
	unsigned long ip = rec->ip;

	/* read where this goes */
	if (probe_kernel_read(op, (void *)ip, MCOUNT_INSN_SIZE * 2))
		return -EFAULT;

	/*
	 * It should be pointing to two nops or
	 *  b +8; ld r2,40(r1)
	 */
	if (((op[0] != 0x48000008) || (op[1] != 0xe8410028)) &&
	    ((op[0] != PPC_INST_NOP) || (op[1] != PPC_INST_NOP))) {
		printk(KERN_ERR "Expected NOPs but have %x %x\n", op[0], op[1]);
		return -EINVAL;
	}

	/* If we never set up a trampoline to ftrace_caller, then bail */
	if (!rec->arch.mod->arch.tramp) {
		printk(KERN_ERR "No ftrace trampoline\n");
		return -EINVAL;
	}

	/* create the branch to the trampoline */
	op[0] = create_branch((unsigned int *)ip,
			      rec->arch.mod->arch.tramp, BRANCH_SET_LINK);
	if (!op[0]) {
		printk(KERN_ERR "REL24 out of range!\n");
		return -EINVAL;
	}

	/* ld r2,40(r1) */
	op[1] = 0xe8410028;

	pr_devel("write to %lx\n", rec->ip);

	if (probe_kernel_write((void *)ip, op, MCOUNT_INSN_SIZE * 2))
		return -EPERM;

	flush_icache_range(ip, ip + 8);

	return 0;
}
#else
static int
__ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int op;
	unsigned long ip = rec->ip;

	/* read where this goes */
	if (probe_kernel_read(&op, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* It should be pointing to a nop */
	if (op != PPC_INST_NOP) {
		printk(KERN_ERR "Expected NOP but have %x\n", op);
		return -EINVAL;
	}

	/* If we never set up a trampoline to ftrace_caller, then bail */
	if (!rec->arch.mod->arch.tramp) {
		printk(KERN_ERR "No ftrace trampoline\n");
		return -EINVAL;
	}

	/* create the branch to the trampoline */
	op = create_branch((unsigned int *)ip,
			   rec->arch.mod->arch.tramp, BRANCH_SET_LINK);
	if (!op) {
		printk(KERN_ERR "REL24 out of range!\n");
		return -EINVAL;
	}

	pr_devel("write to %lx\n", rec->ip);

	if (patch_instruction((unsigned int *)ip, op))
		return -EPERM;

	return 0;
}
#endif /* CONFIG_PPC64 */
#endif /* CONFIG_MODULES */

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned int old, new;

	/*
	 * If the calling address is more that 24 bits away,
	 * then we had to use a trampoline to make the call.
	 * Otherwise just update the call site.
	 */
	if (test_24bit_addr(ip, addr)) {
		/* within range */
		old = PPC_INST_NOP;
		new = ftrace_call_replace(ip, addr, 1);
		return ftrace_modify_code(ip, old, new);
	}

#ifdef CONFIG_MODULES
	/*
	 * Out of range jumps are called from modules.
	 * Being that we are converting from nop, it had better
	 * already have a module defined.
	 */
	if (!rec->arch.mod) {
		printk(KERN_ERR "No module loaded\n");
		return -EINVAL;
	}

	return __ftrace_make_call(rec, addr);
#else
	/* We should not get here without modules */
	return -EINVAL;
#endif /* CONFIG_MODULES */
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned int old, new;
	int ret;

	old = *(unsigned int *)&ftrace_call;
	new = ftrace_call_replace(ip, (unsigned long)func, 1);
	ret = ftrace_modify_code(ip, old, new);

	return ret;
}

static int __ftrace_replace_code(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_addr = (unsigned long)FTRACE_ADDR;
	int ret;

	ret = ftrace_update_record(rec, enable);

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;
	case FTRACE_UPDATE_MAKE_CALL:
		return ftrace_make_call(rec, ftrace_addr);
	case FTRACE_UPDATE_MAKE_NOP:
		return ftrace_make_nop(NULL, rec, ftrace_addr);
	}

	return 0;
}

void ftrace_replace_code(int enable)
{
	struct ftrace_rec_iter *iter;
	struct dyn_ftrace *rec;
	int ret;

	for (iter = ftrace_rec_iter_start(); iter;
	     iter = ftrace_rec_iter_next(iter)) {
		rec = ftrace_rec_iter_record(iter);
		ret = __ftrace_replace_code(rec, enable);
		if (ret) {
			ftrace_bug(ret, rec->ip);
			return;
		}
	}
}

void arch_ftrace_update_code(int command)
{
	if (command & FTRACE_UPDATE_CALLS)
		ftrace_replace_code(1);
	else if (command & FTRACE_DISABLE_CALLS)
		ftrace_replace_code(0);

	if (command & FTRACE_UPDATE_TRACE_FUNC)
		ftrace_update_ftrace_func(ftrace_trace_function);

	if (command & FTRACE_START_FUNC_RET)
		ftrace_enable_ftrace_graph_caller();
	else if (command & FTRACE_STOP_FUNC_RET)
		ftrace_disable_ftrace_graph_caller();
}

int __init ftrace_dyn_arch_init(void *data)
{
	/* caller expects data to be zero */
	unsigned long *p = data;

	*p = 0;

	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

#ifdef CONFIG_DYNAMIC_FTRACE
extern void ftrace_graph_call(void);
extern void ftrace_graph_stub(void);

int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);
	unsigned long addr = (unsigned long)(&ftrace_graph_caller);
	unsigned long stub = (unsigned long)(&ftrace_graph_stub);
	unsigned int old, new;

	old = ftrace_call_replace(ip, stub, 0);
	new = ftrace_call_replace(ip, addr, 0);

	return ftrace_modify_code(ip, old, new);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);
	unsigned long addr = (unsigned long)(&ftrace_graph_caller);
	unsigned long stub = (unsigned long)(&ftrace_graph_stub);
	unsigned int old, new;

	old = ftrace_call_replace(ip, addr, 0);
	new = ftrace_call_replace(ip, stub, 0);

	return ftrace_modify_code(ip, old, new);
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_PPC64
extern void mod_return_to_handler(void);
#endif

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr)
{
	unsigned long old;
	int faulted;
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

#ifdef CONFIG_PPC64
	/* non core kernel code needs to save and restore the TOC */
	if (REGION_ID(self_addr) != KERNEL_REGION_ID)
		return_hooker = (unsigned long)&mod_return_to_handler;
#endif

	return_hooker = ppc_function_entry((void *)return_hooker);

	/*
	 * Protect against fault, even if it shouldn't
	 * happen. This tool is too much intrusive to
	 * ignore such a protection.
	 */
	asm volatile(
		"1: " PPC_LL "%[old], 0(%[parent])\n"
		"2: " PPC_STL "%[return_hooker], 0(%[parent])\n"
		"   li %[faulted], 0\n"
		"3:\n"

		".section .fixup, \"ax\"\n"
		"4: li %[faulted], 1\n"
		"   b 3b\n"
		".previous\n"

		".section __ex_table,\"a\"\n"
			PPC_LONG_ALIGN "\n"
			PPC_LONG "1b,4b\n"
			PPC_LONG "2b,4b\n"
		".previous"

		: [old] "=&r" (old), [faulted] "=r" (faulted)
		: [parent] "r" (parent), [return_hooker] "r" (return_hooker)
		: "memory"
	);

	if (unlikely(faulted)) {
		ftrace_graph_stop();
		WARN_ON(1);
		return;
	}

	if (ftrace_push_return_trace(old, self_addr, &trace.depth, 0) == -EBUSY) {
		*parent = old;
		return;
	}

	trace.func = self_addr;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		current->curr_ret_stack--;
		*parent = old;
	}
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#if defined(CONFIG_FTRACE_SYSCALLS) && defined(CONFIG_PPC64)
unsigned long __init arch_syscall_addr(int nr)
{
	return sys_call_table[nr*2];
}
#endif /* CONFIG_FTRACE_SYSCALLS && CONFIG_PPC64 */
