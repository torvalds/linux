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

#include <asm/cacheflush.h>
#include <asm/code-patching.h>
#include <asm/ftrace.h>
#include <asm/syscall.h>
#include <asm/inst.h>

/*
 * We generally only have a single long_branch tramp and at most 2 or 3 plt
 * tramps generated. But, we don't use the plt tramps currently. We also allot
 * 2 tramps after .text and .init.text. So, we only end up with around 3 usable
 * tramps in total. Set aside 8 just to be sure.
 */
#define	NUM_FTRACE_TRAMPS	8
static unsigned long ftrace_tramps[NUM_FTRACE_TRAMPS];

static ppc_inst_t
ftrace_call_replace(unsigned long ip, unsigned long addr, int link)
{
	ppc_inst_t op;

	addr = ppc_function_entry((void *)addr);

	/* if (link) set op to 'bl' else 'b' */
	create_branch(&op, (u32 *)ip, addr, link ? BRANCH_SET_LINK : 0);

	return op;
}

static inline int
ftrace_modify_code(unsigned long ip, ppc_inst_t old, ppc_inst_t new)
{
	ppc_inst_t replaced;

	/*
	 * Note:
	 * We are paranoid about modifying text, as if a bug was to happen, it
	 * could cause us to read or write to someplace that could cause harm.
	 * Carefully read and modify the code with probe_kernel_*(), and make
	 * sure what we read is what we expected it to be before modifying it.
	 */

	/* read the text we want to modify */
	if (copy_inst_from_kernel_nofault(&replaced, (void *)ip))
		return -EFAULT;

	/* Make sure it is what we expect it to be */
	if (!ppc_inst_equal(replaced, old)) {
		pr_err("%p: replaced (%08lx) != old (%08lx)", (void *)ip,
		       ppc_inst_as_ulong(replaced), ppc_inst_as_ulong(old));
		return -EINVAL;
	}

	/* replace the text with the new text */
	return patch_instruction((u32 *)ip, new);
}

/*
 * Helper functions that are the same for both PPC64 and PPC32.
 */
static int test_24bit_addr(unsigned long ip, unsigned long addr)
{
	addr = ppc_function_entry((void *)addr);

	return is_offset_in_branch_range(addr - ip);
}

static int is_bl_op(ppc_inst_t op)
{
	return (ppc_inst_val(op) & ~PPC_LI_MASK) == PPC_RAW_BL(0);
}

static int is_b_op(ppc_inst_t op)
{
	return (ppc_inst_val(op) & ~PPC_LI_MASK) == PPC_RAW_BRANCH(0);
}

static unsigned long find_bl_target(unsigned long ip, ppc_inst_t op)
{
	int offset;

	offset = PPC_LI(ppc_inst_val(op));
	/* make it signed */
	if (offset & 0x02000000)
		offset |= 0xfe000000;

	return ip + (long)offset;
}

#ifdef CONFIG_MODULES
static int
__ftrace_make_nop(struct module *mod,
		  struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long entry, ptr, tramp;
	unsigned long ip = rec->ip;
	ppc_inst_t op, pop;

	/* read where this goes */
	if (copy_inst_from_kernel_nofault(&op, (void *)ip)) {
		pr_err("Fetching opcode failed.\n");
		return -EFAULT;
	}

	/* Make sure that this is still a 24bit jump */
	if (!is_bl_op(op)) {
		pr_err("Not expected bl: opcode is %08lx\n", ppc_inst_as_ulong(op));
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

	if (IS_ENABLED(CONFIG_MPROFILE_KERNEL)) {
		if (copy_inst_from_kernel_nofault(&op, (void *)(ip - 4))) {
			pr_err("Fetching instruction at %lx failed.\n", ip - 4);
			return -EFAULT;
		}

		/* We expect either a mflr r0, or a std r0, LRSAVE(r1) */
		if (!ppc_inst_equal(op, ppc_inst(PPC_RAW_MFLR(_R0))) &&
		    !ppc_inst_equal(op, ppc_inst(PPC_INST_STD_LR))) {
			pr_err("Unexpected instruction %08lx around bl _mcount\n",
			       ppc_inst_as_ulong(op));
			return -EINVAL;
		}
	} else if (IS_ENABLED(CONFIG_PPC64)) {
		/*
		 * Check what is in the next instruction. We can see ld r2,40(r1), but
		 * on first pass after boot we will see mflr r0.
		 */
		if (copy_inst_from_kernel_nofault(&op, (void *)(ip + 4))) {
			pr_err("Fetching op failed.\n");
			return -EFAULT;
		}

		if (!ppc_inst_equal(op,  ppc_inst(PPC_INST_LD_TOC))) {
			pr_err("Expected %08lx found %08lx\n", PPC_INST_LD_TOC,
			       ppc_inst_as_ulong(op));
			return -EINVAL;
		}
	}

	/*
	 * When using -mprofile-kernel or PPC32 there is no load to jump over.
	 *
	 * Otherwise our original call site looks like:
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
	if (IS_ENABLED(CONFIG_MPROFILE_KERNEL) || IS_ENABLED(CONFIG_PPC32))
		pop = ppc_inst(PPC_RAW_NOP());
	else
		pop = ppc_inst(PPC_RAW_BRANCH(8));	/* b +8 */

	if (patch_instruction((u32 *)ip, pop)) {
		pr_err("Patching NOP failed.\n");
		return -EPERM;
	}

	return 0;
}
#else
static int __ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec, unsigned long addr)
{
	return 0;
}
#endif /* CONFIG_MODULES */

static unsigned long find_ftrace_tramp(unsigned long ip)
{
	int i;

	/*
	 * We have the compiler generated long_branch tramps at the end
	 * and we prefer those
	 */
	for (i = NUM_FTRACE_TRAMPS - 1; i >= 0; i--)
		if (!ftrace_tramps[i])
			continue;
		else if (is_offset_in_branch_range(ftrace_tramps[i] - ip))
			return ftrace_tramps[i];

	return 0;
}

static int add_ftrace_tramp(unsigned long tramp)
{
	int i;

	for (i = 0; i < NUM_FTRACE_TRAMPS; i++)
		if (!ftrace_tramps[i]) {
			ftrace_tramps[i] = tramp;
			return 0;
		}

	return -1;
}

/*
 * If this is a compiler generated long_branch trampoline (essentially, a
 * trampoline that has a branch to _mcount()), we re-write the branch to
 * instead go to ftrace_[regs_]caller() and note down the location of this
 * trampoline.
 */
static int setup_mcount_compiler_tramp(unsigned long tramp)
{
	int i;
	ppc_inst_t op;
	unsigned long ptr;

	/* Is this a known long jump tramp? */
	for (i = 0; i < NUM_FTRACE_TRAMPS; i++)
		if (ftrace_tramps[i] == tramp)
			return 0;

	/* New trampoline -- read where this goes */
	if (copy_inst_from_kernel_nofault(&op, (void *)tramp)) {
		pr_debug("Fetching opcode failed.\n");
		return -1;
	}

	/* Is this a 24 bit branch? */
	if (!is_b_op(op)) {
		pr_debug("Trampoline is not a long branch tramp.\n");
		return -1;
	}

	/* lets find where the pointer goes */
	ptr = find_bl_target(tramp, op);

	if (ptr != ppc_global_function_entry((void *)_mcount)) {
		pr_debug("Trampoline target %p is not _mcount\n", (void *)ptr);
		return -1;
	}

	/* Let's re-write the tramp to go to ftrace_[regs_]caller */
	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS))
		ptr = ppc_global_function_entry((void *)ftrace_regs_caller);
	else
		ptr = ppc_global_function_entry((void *)ftrace_caller);

	if (patch_branch((u32 *)tramp, ptr, 0)) {
		pr_debug("REL24 out of range!\n");
		return -1;
	}

	if (add_ftrace_tramp(tramp)) {
		pr_debug("No tramp locations left\n");
		return -1;
	}

	return 0;
}

static int __ftrace_make_nop_kernel(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long tramp, ip = rec->ip;
	ppc_inst_t op;

	/* Read where this goes */
	if (copy_inst_from_kernel_nofault(&op, (void *)ip)) {
		pr_err("Fetching opcode failed.\n");
		return -EFAULT;
	}

	/* Make sure that this is still a 24bit jump */
	if (!is_bl_op(op)) {
		pr_err("Not expected bl: opcode is %08lx\n", ppc_inst_as_ulong(op));
		return -EINVAL;
	}

	/* Let's find where the pointer goes */
	tramp = find_bl_target(ip, op);

	pr_devel("ip:%lx jumps to %lx", ip, tramp);

	if (setup_mcount_compiler_tramp(tramp)) {
		/* Are other trampolines reachable? */
		if (!find_ftrace_tramp(ip)) {
			pr_err("No ftrace trampolines reachable from %ps\n",
					(void *)ip);
			return -EINVAL;
		}
	}

	if (patch_instruction((u32 *)ip, ppc_inst(PPC_RAW_NOP()))) {
		pr_err("Patching NOP failed.\n");
		return -EPERM;
	}

	return 0;
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	ppc_inst_t old, new;

	/*
	 * If the calling address is more that 24 bits away,
	 * then we had to use a trampoline to make the call.
	 * Otherwise just update the call site.
	 */
	if (test_24bit_addr(ip, addr)) {
		/* within range */
		old = ftrace_call_replace(ip, addr, 1);
		new = ppc_inst(PPC_RAW_NOP());
		return ftrace_modify_code(ip, old, new);
	} else if (core_kernel_text(ip)) {
		return __ftrace_make_nop_kernel(rec, addr);
	} else if (!IS_ENABLED(CONFIG_MODULES)) {
		return -EINVAL;
	}

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
}

#ifdef CONFIG_MODULES
/*
 * Examine the existing instructions for __ftrace_make_call.
 * They should effectively be a NOP, and follow formal constraints,
 * depending on the ABI. Return false if they don't.
 */
static bool expected_nop_sequence(void *ip, ppc_inst_t op0, ppc_inst_t op1)
{
	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS))
		return ppc_inst_equal(op0, ppc_inst(PPC_RAW_NOP()));
	else
		return ppc_inst_equal(op0, ppc_inst(PPC_RAW_BRANCH(8))) &&
		       ppc_inst_equal(op1, ppc_inst(PPC_INST_LD_TOC));
}

static int
__ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	ppc_inst_t op[2];
	void *ip = (void *)rec->ip;
	unsigned long entry, ptr, tramp;
	struct module *mod = rec->arch.mod;

	/* read where this goes */
	if (copy_inst_from_kernel_nofault(op, ip))
		return -EFAULT;

	if (!IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS) &&
	    copy_inst_from_kernel_nofault(op + 1, ip + 4))
		return -EFAULT;

	if (!expected_nop_sequence(ip, op[0], op[1])) {
		pr_err("Unexpected call sequence at %p: %08lx %08lx\n", ip,
		       ppc_inst_as_ulong(op[0]), ppc_inst_as_ulong(op[1]));
		return -EINVAL;
	}

	/* If we never set up ftrace trampoline(s), then bail */
	if (!mod->arch.tramp ||
	    (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS) && !mod->arch.tramp_regs)) {
		pr_err("No ftrace trampoline\n");
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS) && rec->flags & FTRACE_FL_REGS)
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

	if (patch_branch(ip, tramp, BRANCH_SET_LINK)) {
		pr_err("REL24 out of range!\n");
		return -EINVAL;
	}

	return 0;
}
#else
static int __ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	return 0;
}
#endif /* CONFIG_MODULES */

static int __ftrace_make_call_kernel(struct dyn_ftrace *rec, unsigned long addr)
{
	ppc_inst_t op;
	void *ip = (void *)rec->ip;
	unsigned long tramp, entry, ptr;

	/* Make sure we're being asked to patch branch to a known ftrace addr */
	entry = ppc_global_function_entry((void *)ftrace_caller);
	ptr = ppc_global_function_entry((void *)addr);

	if (ptr != entry && IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS))
		entry = ppc_global_function_entry((void *)ftrace_regs_caller);

	if (ptr != entry) {
		pr_err("Unknown ftrace addr to patch: %ps\n", (void *)ptr);
		return -EINVAL;
	}

	/* Make sure we have a nop */
	if (copy_inst_from_kernel_nofault(&op, ip)) {
		pr_err("Unable to read ftrace location %p\n", ip);
		return -EFAULT;
	}

	if (!ppc_inst_equal(op, ppc_inst(PPC_RAW_NOP()))) {
		pr_err("Unexpected call sequence at %p: %08lx\n",
		       ip, ppc_inst_as_ulong(op));
		return -EINVAL;
	}

	tramp = find_ftrace_tramp((unsigned long)ip);
	if (!tramp) {
		pr_err("No ftrace trampolines reachable from %ps\n", ip);
		return -EINVAL;
	}

	if (patch_branch(ip, tramp, BRANCH_SET_LINK)) {
		pr_err("Error patching branch to ftrace tramp!\n");
		return -EINVAL;
	}

	return 0;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	ppc_inst_t old, new;

	/*
	 * If the calling address is more that 24 bits away,
	 * then we had to use a trampoline to make the call.
	 * Otherwise just update the call site.
	 */
	if (test_24bit_addr(ip, addr)) {
		/* within range */
		old = ppc_inst(PPC_RAW_NOP());
		new = ftrace_call_replace(ip, addr, 1);
		return ftrace_modify_code(ip, old, new);
	} else if (core_kernel_text(ip)) {
		return __ftrace_make_call_kernel(rec, addr);
	} else if (!IS_ENABLED(CONFIG_MODULES)) {
		/* We should not get here without modules */
		return -EINVAL;
	}

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
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
#ifdef CONFIG_MODULES
static int
__ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
					unsigned long addr)
{
	ppc_inst_t op;
	unsigned long ip = rec->ip;
	unsigned long entry, ptr, tramp;
	struct module *mod = rec->arch.mod;

	/* If we never set up ftrace trampolines, then bail */
	if (!mod->arch.tramp || !mod->arch.tramp_regs) {
		pr_err("No ftrace trampoline\n");
		return -EINVAL;
	}

	/* read where this goes */
	if (copy_inst_from_kernel_nofault(&op, (void *)ip)) {
		pr_err("Fetching opcode failed.\n");
		return -EFAULT;
	}

	/* Make sure that this is still a 24bit jump */
	if (!is_bl_op(op)) {
		pr_err("Not expected bl: opcode is %08lx\n", ppc_inst_as_ulong(op));
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
		if (patch_branch((u32 *)ip, addr, BRANCH_SET_LINK)) {
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

	if (patch_branch((u32 *)ip, tramp, BRANCH_SET_LINK)) {
		pr_err("REL24 out of range!\n");
		return -EINVAL;
	}

	return 0;
}
#else
static int __ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr, unsigned long addr)
{
	return 0;
}
#endif

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
			unsigned long addr)
{
	unsigned long ip = rec->ip;
	ppc_inst_t old, new;

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
	} else if (core_kernel_text(ip)) {
		/*
		 * We always patch out of range locations to go to the regs
		 * variant, so there is nothing to do here
		 */
		return 0;
	} else if (!IS_ENABLED(CONFIG_MODULES)) {
		/* We should not get here without modules */
		return -EINVAL;
	}

	/*
	 * Out of range jumps are called from modules.
	 */
	if (!rec->arch.mod) {
		pr_err("No module loaded\n");
		return -EINVAL;
	}

	return __ftrace_modify_call(rec, old_addr, addr);
}
#endif

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	ppc_inst_t old, new;
	int ret;

	old = ppc_inst_read((u32 *)&ftrace_call);
	new = ftrace_call_replace(ip, (unsigned long)func, 1);
	ret = ftrace_modify_code(ip, old, new);

	/* Also update the regs callback function */
	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS) && !ret) {
		ip = (unsigned long)(&ftrace_regs_call);
		old = ppc_inst_read((u32 *)&ftrace_regs_call);
		new = ftrace_call_replace(ip, (unsigned long)func, 1);
		ret = ftrace_modify_code(ip, old, new);
	}

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

#ifdef CONFIG_PPC64
#define PACATOC offsetof(struct paca_struct, kernel_toc)

extern unsigned int ftrace_tramp_text[], ftrace_tramp_init[];

void ftrace_free_init_tramp(void)
{
	int i;

	for (i = 0; i < NUM_FTRACE_TRAMPS && ftrace_tramps[i]; i++)
		if (ftrace_tramps[i] == (unsigned long)ftrace_tramp_init) {
			ftrace_tramps[i] = 0;
			return;
		}
}

int __init ftrace_dyn_arch_init(void)
{
	int i;
	unsigned int *tramp[] = { ftrace_tramp_text, ftrace_tramp_init };
	u32 stub_insns[] = {
		PPC_RAW_LD(_R12, _R13, PACATOC),
		PPC_RAW_ADDIS(_R12, _R12, 0),
		PPC_RAW_ADDI(_R12, _R12, 0),
		PPC_RAW_MTCTR(_R12),
		PPC_RAW_BCTR()
	};
	unsigned long addr;
	long reladdr;

	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS))
		addr = ppc_global_function_entry((void *)ftrace_regs_caller);
	else
		addr = ppc_global_function_entry((void *)ftrace_caller);

	reladdr = addr - kernel_toc_addr();

	if (reladdr >= SZ_2G || reladdr < -(long)SZ_2G) {
		pr_err("Address of %ps out of range of kernel_toc.\n",
				(void *)addr);
		return -1;
	}

	for (i = 0; i < 2; i++) {
		memcpy(tramp[i], stub_insns, sizeof(stub_insns));
		tramp[i][1] |= PPC_HA(reladdr);
		tramp[i][2] |= PPC_LO(reladdr);
		add_ftrace_tramp((unsigned long)tramp[i]);
	}

	return 0;
}
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

extern void ftrace_graph_call(void);
extern void ftrace_graph_stub(void);

static int ftrace_modify_ftrace_graph_caller(bool enable)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);
	unsigned long addr = (unsigned long)(&ftrace_graph_caller);
	unsigned long stub = (unsigned long)(&ftrace_graph_stub);
	ppc_inst_t old, new;

	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_ARGS))
		return 0;

	old = ftrace_call_replace(ip, enable ? stub : addr, 0);
	new = ftrace_call_replace(ip, enable ? addr : stub, 0);

	return ftrace_modify_code(ip, old, new);
}

int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_ftrace_graph_caller(true);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_ftrace_graph_caller(false);
}

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info. Return the address we want to divert to.
 */
static unsigned long
__prepare_ftrace_return(unsigned long parent, unsigned long ip, unsigned long sp)
{
	unsigned long return_hooker;
	int bit;

	if (unlikely(ftrace_graph_is_dead()))
		goto out;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		goto out;

	bit = ftrace_test_recursion_trylock(ip, parent);
	if (bit < 0)
		goto out;

	return_hooker = ppc_function_entry(return_to_handler);

	if (!function_graph_enter(parent, ip, 0, (unsigned long *)sp))
		parent = return_hooker;

	ftrace_test_recursion_unlock(bit);
out:
	return parent;
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	fregs->regs.link = __prepare_ftrace_return(parent_ip, ip, fregs->regs.gpr[1]);
}
#else
unsigned long prepare_ftrace_return(unsigned long parent, unsigned long ip,
				    unsigned long sp)
{
	return __prepare_ftrace_return(parent, ip, sp);
}
#endif
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_PPC64_ELF_ABI_V1
char *arch_ftrace_match_adjust(char *str, const char *search)
{
	if (str[0] == '.' && search[0] != '.')
		return str + 1;
	else
		return str;
}
#endif /* CONFIG_PPC64_ELF_ABI_V1 */
