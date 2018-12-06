// SPDX-License-Identifier: GPL-2.0
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

#define pr_fmt(fmt) "ftrace-powerpc: " fmt

#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>

#include <asm/asm-prototypes.h>
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
	 * Note:
	 * We are paranoid about modifying text, as if a bug was to happen, it
	 * could cause us to read or write to someplace that could cause harm.
	 * Carefully read and modify the code with probe_kernel_*(), and make
	 * sure what we read is what we expected it to be before modifying it.
	 */

	/* read the text we want to modify */
	if (probe_kernel_read(&replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* Make sure it is what we expect it to be */
	if (replaced != old) {
		pr_err("%p: replaced (%#x) != old (%#x)",
		(void *)ip, replaced, old);
		return -EINVAL;
	}

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
	addr = ppc_function_entry((void *)addr);

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
	unsigned long entry, ptr, tramp;
	unsigned long ip = rec->ip;
	unsigned int op, pop;

	/* read where this goes */
	if (probe_kernel_read(&op, (void *)ip, sizeof(int))) {
		pr_err("Fetching opcode failed.\n");
		return -EFAULT;
	}

	/* Make sure that that this is still a 24bit jump */
	if (!is_bl_op(op)) {
		pr_err("Not expected bl: opcode is %x\n", op);
		return -EINVAL;
	}

	/* lets find where the pointer goes */
	tramp = find_bl_target(ip, op);

	pr_devel("ip:%lx jumps to %lx", ip, tramp);

	if (module_trampoline_target(mod, tramp, &ptr)) {
		pr_err("Failed to get trampoline target\n");
		return -EFAULT;
	}

	pr_devel("trampoline target %lx", ptr);

	entry = ppc_global_function_entry((void *)addr);
	/* This should match what was called */
	if (ptr != entry) {
		pr_err("addr %lx does not match expected %lx\n", ptr, entry);
		return -EINVAL;
	}

#ifdef CONFIG_MPROFILE_KERNEL
	/* When using -mkernel_profile there is no load to jump over */
	pop = PPC_INST_NOP;

	if (probe_kernel_read(&op, (void *)(ip - 4), 4)) {
		pr_err("Fetching instruction at %lx failed.\n", ip - 4);
		return -EFAULT;
	}

	/* We expect either a mflr r0, or a std r0, LRSAVE(r1) */
	if (op != PPC_INST_MFLR && op != PPC_INST_STD_LR) {
		pr_err("Unexpected instruction %08x around bl _mcount\n", op);
		return -EINVAL;
	}
#else
	/*
	 * Our original call site looks like:
	 *
	 * bl <tramp>
	 * ld r2,XX(r1)
	 *
	 * Milton Miller pointed out that we can not simply nop the branch.
	 * If a task was preempted when calling a trace function, the nops
	 * will remove the way to restore the TOC in r2 and the r2 TOC will
	 * get corrupted.
	 *
	 * Use a b +8 to jump over the load.
	 */

	pop = PPC_INST_BRANCH | 8;	/* b +8 */

	/*
	 * Check what is in the next instruction. We can see ld r2,40(r1), but
	 * on first pass after boot we will see mflr r0.
	 */
	if (probe_kernel_read(&op, (void *)(ip+4), MCOUNT_INSN_SIZE)) {
		pr_err("Fetching op failed.\n");
		return -EFAULT;
	}

	if (op != PPC_INST_LD_TOC) {
		pr_err("Expected %08x found %08x\n", PPC_INST_LD_TOC, op);
		return -EINVAL;
	}
#endif /* CONFIG_MPROFILE_KERNEL */

	if (patch_instruction((unsigned int *)ip, pop)) {
		pr_err("Patching NOP failed.\n");
		return -EPERM;
	}

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
		pr_err("Not expected bl: opcode is %x\n", op);
		return -EINVAL;
	}

	/* lets find where the pointer goes */
	tramp = find_bl_target(ip, op);

	/*
	 * On PPC32 the trampoline looks like:
	 *  0x3d, 0x80, 0x00, 0x00  lis r12,sym@ha
	 *  0x39, 0x8c, 0x00, 0x00  addi r12,r12,sym@l
	 *  0x7d, 0x89, 0x03, 0xa6  mtctr r12
	 *  0x4e, 0x80, 0x04, 0x20  bctr
	 */

	pr_devel("ip:%lx jumps to %lx", ip, tramp);

	/* Find where the trampoline jumps to */
	if (probe_kernel_read(jmp, (void *)tramp, sizeof(jmp))) {
		pr_err("Failed to read %lx\n", tramp);
		return -EFAULT;
	}

	pr_devel(" %08x %08x ", jmp[0], jmp[1]);

	/* verify that this is what we expect it to be */
	if (((jmp[0] & 0xffff0000) != 0x3d800000) ||
	    ((jmp[1] & 0xffff0000) != 0x398c0000) ||
	    (jmp[2] != 0x7d8903a6) ||
	    (jmp[3] != 0x4e800420)) {
		pr_err("Not a trampoline\n");
		return -EINVAL;
	}

	tramp = (jmp[1] & 0xffff) |
		((jmp[0] & 0xffff) << 16);
	if (tramp & 0x8000)
		tramp -= 0x10000;

	pr_devel(" %lx ", tramp);

	if (tramp != addr) {
		pr_err("Trampoline location %08lx does not match addr\n",
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
			pr_err("No module loaded addr=%lx\n", addr);
			return -EFAULT;
		}
		rec->arch.mod = mod;
	} else if (mod) {
		if (mod != rec->arch.mod) {
			pr_err("Record mod %p not equal to passed in mod %p\n",
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
/*
 * Examine the existing instructions for __ftrace_make_call.
 * They should effectively be a NOP, and follow formal constraints,
 * depending on the ABI. Return false if they don't.
 */
#ifndef CONFIG_MPROFILE_KERNEL
static int
expected_nop_sequence(void *ip, unsigned int op0, unsigned int op1)
{
	/*
	 * We expect to see:
	 *
	 * b +8
	 * ld r2,XX(r1)
	 *
	 * The load offset is different depending on the ABI. For simplicity
	 * just mask it out when doing the compare.
	 */
	if ((op0 != 0x48000008) || ((op1 & 0xffff0000) != 0xe8410000))
		return 0;
	return 1;
}
#else
static int
expected_nop_sequence(void *ip, unsigned int op0, unsigned int op1)
{
	/* look for patched "NOP" on ppc64 with -mprofile-kernel */
	if (op0 != PPC_INST_NOP)
		return 0;
	return 1;
}
#endif

static int
__ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int op[2];
	void *ip = (void *)rec->ip;
	unsigned long entry, ptr, tramp;
	struct module *mod = rec->arch.mod;

	/* read where this goes */
	if (probe_kernel_read(op, ip, sizeof(op)))
		return -EFAULT;

	if (!expected_nop_sequence(ip, op[0], op[1])) {
		pr_err("Unexpected call sequence at %p: %x %x\n",
		ip, op[0], op[1]);
		return -EINVAL;
	}

	/* If we never set up ftrace trampoline(s), then bail */
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	if (!mod->arch.tramp || !mod->arch.tramp_regs) {
#else
	if (!mod->arch.tramp) {
#endif
		pr_err("No ftrace trampoline\n");
		return -EINVAL;
	}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	if (rec->flags & FTRACE_FL_REGS)
		tramp = mod->arch.tramp_regs;
	else
#endif
		tramp = mod->arch.tramp;

	if (module_trampoline_target(mod, tramp, &ptr)) {
		pr_err("Failed to get trampoline target\n");
		return -EFAULT;
	}

	pr_devel("trampoline target %lx", ptr);

	entry = ppc_global_function_entry((void *)addr);
	/* This should match what was called */
	if (ptr != entry) {
		pr_err("addr %lx does not match expected %lx\n", ptr, entry);
		return -EINVAL;
	}

	/* Ensure branch is within 24 bits */
	if (!create_branch(ip, tramp, BRANCH_SET_LINK)) {
		pr_err("Branch out of range\n");
		return -EINVAL;
	}

	if (patch_branch(ip, tramp, BRANCH_SET_LINK)) {
		pr_err("REL24 out of range!\n");
		return -EINVAL;
	}

	return 0;
}

#else  /* !CONFIG_PPC64: */
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
		pr_err("Expected NOP but have %x\n", op);
		return -EINVAL;
	}

	/* If we never set up a trampoline to ftrace_caller, then bail */
	if (!rec->arch.mod->arch.tramp) {
		pr_err("No ftrace trampoline\n");
		return -EINVAL;
	}

	/* create the branch to the trampoline */
	op = create_branch((unsigned int *)ip,
			   rec->arch.mod->arch.tramp, BRANCH_SET_LINK);
	if (!op) {
		pr_err("REL24 out of range!\n");
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
		pr_err("No module loaded\n");
		return -EINVAL;
	}

	return __ftrace_make_call(rec, addr);
#else
	/* We should not get here without modules */
	return -EINVAL;
#endif /* CONFIG_MODULES */
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
#ifdef CONFIG_MODULES
static int
__ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
					unsigned long addr)
{
	unsigned int op;
	unsigned long ip = rec->ip;
	unsigned long entry, ptr, tramp;
	struct module *mod = rec->arch.mod;

	/* If we never set up ftrace trampolines, then bail */
	if (!mod->arch.tramp || !mod->arch.tramp_regs) {
		pr_err("No ftrace trampoline\n");
		return -EINVAL;
	}

	/* read where this goes */
	if (probe_kernel_read(&op, (void *)ip, sizeof(int))) {
		pr_err("Fetching opcode failed.\n");
		return -EFAULT;
	}

	/* Make sure that that this is still a 24bit jump */
	if (!is_bl_op(op)) {
		pr_err("Not expected bl: opcode is %x\n", op);
		return -EINVAL;
	}

	/* lets find where the pointer goes */
	tramp = find_bl_target(ip, op);
	entry = ppc_global_function_entry((void *)old_addr);

	pr_devel("ip:%lx jumps to %lx", ip, tramp);

	if (tramp != entry) {
		/* old_addr is not within range, so we must have used a trampoline */
		if (module_trampoline_target(mod, tramp, &ptr)) {
			pr_err("Failed to get trampoline target\n");
			return -EFAULT;
		}

		pr_devel("trampoline target %lx", ptr);

		/* This should match what was called */
		if (ptr != entry) {
			pr_err("addr %lx does not match expected %lx\n", ptr, entry);
			return -EINVAL;
		}
	}

	/* The new target may be within range */
	if (test_24bit_addr(ip, addr)) {
		/* within range */
		if (patch_branch((unsigned int *)ip, addr, BRANCH_SET_LINK)) {
			pr_err("REL24 out of range!\n");
			return -EINVAL;
		}

		return 0;
	}

	if (rec->flags & FTRACE_FL_REGS)
		tramp = mod->arch.tramp_regs;
	else
		tramp = mod->arch.tramp;

	if (module_trampoline_target(mod, tramp, &ptr)) {
		pr_err("Failed to get trampoline target\n");
		return -EFAULT;
	}

	pr_devel("trampoline target %lx", ptr);

	entry = ppc_global_function_entry((void *)addr);
	/* This should match what was called */
	if (ptr != entry) {
		pr_err("addr %lx does not match expected %lx\n", ptr, entry);
		return -EINVAL;
	}

	/* Ensure branch is within 24 bits */
	if (!create_branch((unsigned int *)ip, tramp, BRANCH_SET_LINK)) {
		pr_err("Branch out of range\n");
		return -EINVAL;
	}

	if (patch_branch((unsigned int *)ip, tramp, BRANCH_SET_LINK)) {
		pr_err("REL24 out of range!\n");
		return -EINVAL;
	}

	return 0;
}
#endif

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
			unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned int old, new;

	/*
	 * If the calling address is more that 24 bits away,
	 * then we had to use a trampoline to make the call.
	 * Otherwise just update the call site.
	 */
	if (test_24bit_addr(ip, addr) && test_24bit_addr(ip, old_addr)) {
		/* within range */
		old = ftrace_call_replace(ip, old_addr, 1);
		new = ftrace_call_replace(ip, addr, 1);
		return ftrace_modify_code(ip, old, new);
	}

#ifdef CONFIG_MODULES
	/*
	 * Out of range jumps are called from modules.
	 */
	if (!rec->arch.mod) {
		pr_err("No module loaded\n");
		return -EINVAL;
	}

	return __ftrace_modify_call(rec, old_addr, addr);
#else
	/* We should not get here without modules */
	return -EINVAL;
#endif /* CONFIG_MODULES */
}
#endif

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned int old, new;
	int ret;

	old = *(unsigned int *)&ftrace_call;
	new = ftrace_call_replace(ip, (unsigned long)func, 1);
	ret = ftrace_modify_code(ip, old, new);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	/* Also update the regs callback function */
	if (!ret) {
		ip = (unsigned long)(&ftrace_regs_call);
		old = *(unsigned int *)&ftrace_regs_call;
		new = ftrace_call_replace(ip, (unsigned long)func, 1);
		ret = ftrace_modify_code(ip, old, new);
	}
#endif

	return ret;
}

/*
 * Use the default ftrace_modify_all_code, but without
 * stop_machine().
 */
void arch_ftrace_update_code(int command)
{
	ftrace_modify_all_code(command);
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

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

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info. Return the address we want to divert to.
 */
unsigned long prepare_ftrace_return(unsigned long parent, unsigned long ip)
{
	unsigned long return_hooker;

	if (unlikely(ftrace_graph_is_dead()))
		goto out;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		goto out;

	return_hooker = ppc_function_entry(return_to_handler);

	if (!function_graph_enter(parent, ip, 0, NULL))
		parent = return_hooker;
out:
	return parent;
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#if defined(CONFIG_FTRACE_SYSCALLS) && defined(CONFIG_PPC64)
unsigned long __init arch_syscall_addr(int nr)
{
	return sys_call_table[nr*2];
}
#endif /* CONFIG_FTRACE_SYSCALLS && CONFIG_PPC64 */

#ifdef PPC64_ELF_ABI_v1
char *arch_ftrace_match_adjust(char *str, const char *search)
{
	if (str[0] == '.' && search[0] != '.')
		return str + 1;
	else
		return str;
}
#endif /* PPC64_ELF_ABI_v1 */
