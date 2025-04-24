// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic function tracer architecture backend.
 *
 * Copyright IBM Corp. 2009,2014
 *
 *   Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kmsan-checks.h>
#include <linux/cpufeature.h>
#include <linux/kprobes.h>
#include <linux/execmem.h>
#include <trace/syscall.h>
#include <asm/asm-offsets.h>
#include <asm/text-patching.h>
#include <asm/cacheflush.h>
#include <asm/ftrace.lds.h>
#include <asm/nospec-branch.h>
#include <asm/set_memory.h>
#include "entry.h"
#include "ftrace.h"

/*
 * To generate function prologue either gcc's hotpatch feature (since gcc 4.8)
 * or a combination of -pg -mrecord-mcount -mnop-mcount -mfentry flags
 * (since gcc 9 / clang 10) is used.
 * In both cases the original and also the disabled function prologue contains
 * only a single six byte instruction and looks like this:
 * >	brcl	0,0			# offset 0
 * To enable ftrace the code gets patched like above and afterwards looks
 * like this:
 * >	brasl	%r0,ftrace_caller	# offset 0
 *
 * The instruction will be patched by ftrace_make_call / ftrace_make_nop.
 * The ftrace function gets called with a non-standard C function call ABI
 * where r0 contains the return address. It is also expected that the called
 * function only clobbers r0 and r1, but restores r2-r15.
 * For module code we can't directly jump to ftrace caller, but need a
 * trampoline (ftrace_plt), which clobbers also r1.
 */

void *ftrace_func __read_mostly = ftrace_stub;
struct ftrace_insn {
	u16 opc;
	s32 disp;
} __packed;

static const char *ftrace_shared_hotpatch_trampoline(const char **end)
{
	const char *tstart, *tend;

	tstart = ftrace_shared_hotpatch_trampoline_br;
	tend = ftrace_shared_hotpatch_trampoline_br_end;
#ifdef CONFIG_EXPOLINE
	if (!nospec_disable) {
		tstart = ftrace_shared_hotpatch_trampoline_exrl;
		tend = ftrace_shared_hotpatch_trampoline_exrl_end;
	}
#endif /* CONFIG_EXPOLINE */
	if (end)
		*end = tend;
	return tstart;
}

bool ftrace_need_init_nop(void)
{
	return !cpu_has_seq_insn();
}

int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec)
{
	static struct ftrace_hotpatch_trampoline *next_vmlinux_trampoline =
		__ftrace_hotpatch_trampolines_start;
	static const struct ftrace_insn orig = { .opc = 0xc004, .disp = 0 };
	static struct ftrace_hotpatch_trampoline *trampoline;
	struct ftrace_hotpatch_trampoline **next_trampoline;
	struct ftrace_hotpatch_trampoline *trampolines_end;
	struct ftrace_hotpatch_trampoline tmp;
	struct ftrace_insn *insn;
	struct ftrace_insn old;
	const char *shared;
	s32 disp;

	BUILD_BUG_ON(sizeof(struct ftrace_hotpatch_trampoline) !=
		     SIZEOF_FTRACE_HOTPATCH_TRAMPOLINE);

	next_trampoline = &next_vmlinux_trampoline;
	trampolines_end = __ftrace_hotpatch_trampolines_end;
	shared = ftrace_shared_hotpatch_trampoline(NULL);
#ifdef CONFIG_MODULES
	if (mod) {
		next_trampoline = &mod->arch.next_trampoline;
		trampolines_end = mod->arch.trampolines_end;
	}
#endif

	if (WARN_ON_ONCE(*next_trampoline >= trampolines_end))
		return -ENOMEM;
	trampoline = (*next_trampoline)++;

	if (copy_from_kernel_nofault(&old, (void *)rec->ip, sizeof(old)))
		return -EFAULT;
	/* Check for the compiler-generated fentry nop (brcl 0, .). */
	if (WARN_ON_ONCE(memcmp(&orig, &old, sizeof(old))))
		return -EINVAL;

	/* Generate the trampoline. */
	tmp.brasl_opc = 0xc015; /* brasl %r1, shared */
	tmp.brasl_disp = (shared - (const char *)&trampoline->brasl_opc) / 2;
	tmp.interceptor = FTRACE_ADDR;
	tmp.rest_of_intercepted_function = rec->ip + sizeof(struct ftrace_insn);
	s390_kernel_write(trampoline, &tmp, sizeof(tmp));

	/* Generate a jump to the trampoline. */
	disp = ((char *)trampoline - (char *)rec->ip) / 2;
	insn = (struct ftrace_insn *)rec->ip;
	s390_kernel_write(&insn->disp, &disp, sizeof(disp));

	return 0;
}

static struct ftrace_hotpatch_trampoline *ftrace_get_trampoline(struct dyn_ftrace *rec)
{
	struct ftrace_hotpatch_trampoline *trampoline;
	struct ftrace_insn insn;
	s64 disp;
	u16 opc;

	if (copy_from_kernel_nofault(&insn, (void *)rec->ip, sizeof(insn)))
		return ERR_PTR(-EFAULT);
	disp = (s64)insn.disp * 2;
	trampoline = (void *)(rec->ip + disp);
	if (get_kernel_nofault(opc, &trampoline->brasl_opc))
		return ERR_PTR(-EFAULT);
	if (opc != 0xc015)
		return ERR_PTR(-EINVAL);
	return trampoline;
}

static inline struct ftrace_insn
ftrace_generate_branch_insn(unsigned long ip, unsigned long target)
{
	/* brasl r0,target or brcl 0,0 */
	return (struct ftrace_insn){ .opc = target ? 0xc005 : 0xc004,
				     .disp = target ? (target - ip) / 2 : 0 };
}

static int ftrace_patch_branch_insn(unsigned long ip, unsigned long old_target,
				    unsigned long target)
{
	struct ftrace_insn orig = ftrace_generate_branch_insn(ip, old_target);
	struct ftrace_insn new = ftrace_generate_branch_insn(ip, target);
	struct ftrace_insn old;

	if (!IS_ALIGNED(ip, 8))
		return -EINVAL;
	if (copy_from_kernel_nofault(&old, (void *)ip, sizeof(old)))
		return -EFAULT;
	/* Verify that the to be replaced code matches what we expect. */
	if (memcmp(&orig, &old, sizeof(old)))
		return -EINVAL;
	s390_kernel_write((void *)ip, &new, sizeof(new));
	return 0;
}

static int ftrace_modify_trampoline_call(struct dyn_ftrace *rec,
					 unsigned long old_addr,
					 unsigned long addr)
{
	struct ftrace_hotpatch_trampoline *trampoline;
	u64 old;

	trampoline = ftrace_get_trampoline(rec);
	if (IS_ERR(trampoline))
		return PTR_ERR(trampoline);
	if (get_kernel_nofault(old, &trampoline->interceptor))
		return -EFAULT;
	if (old != old_addr)
		return -EINVAL;
	s390_kernel_write(&trampoline->interceptor, &addr, sizeof(addr));
	return 0;
}

int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
		       unsigned long addr)
{
	if (cpu_has_seq_insn())
		return ftrace_patch_branch_insn(rec->ip, old_addr, addr);
	else
		return ftrace_modify_trampoline_call(rec, old_addr, addr);
}

static int ftrace_patch_branch_mask(void *addr, u16 expected, bool enable)
{
	u16 old;
	u8 op;

	if (get_kernel_nofault(old, addr))
		return -EFAULT;
	if (old != expected)
		return -EINVAL;
	/* set mask field to all ones or zeroes */
	op = enable ? 0xf4 : 0x04;
	s390_kernel_write((char *)addr + 1, &op, sizeof(op));
	return 0;
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	/* Expect brcl 0xf,... for the !cpu_has_seq_insn() case */
	if (cpu_has_seq_insn())
		return ftrace_patch_branch_insn(rec->ip, addr, 0);
	else
		return ftrace_patch_branch_mask((void *)rec->ip, 0xc0f4, false);
}

static int ftrace_make_trampoline_call(struct dyn_ftrace *rec, unsigned long addr)
{
	struct ftrace_hotpatch_trampoline *trampoline;

	trampoline = ftrace_get_trampoline(rec);
	if (IS_ERR(trampoline))
		return PTR_ERR(trampoline);
	s390_kernel_write(&trampoline->interceptor, &addr, sizeof(addr));
	/* Expect brcl 0x0,... */
	return ftrace_patch_branch_mask((void *)rec->ip, 0xc004, true);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	if (cpu_has_seq_insn())
		return ftrace_patch_branch_insn(rec->ip, 0, addr);
	else
		return ftrace_make_trampoline_call(rec, addr);
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	ftrace_func = func;
	return 0;
}

void arch_ftrace_update_code(int command)
{
	ftrace_modify_all_code(command);
}

void ftrace_arch_code_modify_post_process(void)
{
	/*
	 * Flush any pre-fetched instructions on all
	 * CPUs to make the new code visible.
	 */
	text_poke_sync_lock();
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	unsigned long *parent = &arch_ftrace_regs(fregs)->regs.gprs[14];
	unsigned long sp = arch_ftrace_regs(fregs)->regs.gprs[15];

	if (unlikely(ftrace_graph_is_dead()))
		return;
	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;
	if (!function_graph_enter_regs(*parent, ip, 0, (unsigned long *)sp, fregs))
		*parent = (unsigned long)&return_to_handler;
}

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_KPROBES_ON_FTRACE
void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
		struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct kprobe_ctlblk *kcb;
	struct pt_regs *regs;
	struct kprobe *p;
	int bit;

	if (unlikely(kprobe_ftrace_disabled))
		return;

	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0)
		return;

	kmsan_unpoison_memory(fregs, ftrace_regs_size());
	regs = ftrace_get_regs(fregs);
	p = get_kprobe((kprobe_opcode_t *)ip);
	if (!regs || unlikely(!p) || kprobe_disabled(p))
		goto out;

	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
		goto out;
	}

	__this_cpu_write(current_kprobe, p);

	kcb = get_kprobe_ctlblk();
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	instruction_pointer_set(regs, ip);

	if (!p->pre_handler || !p->pre_handler(p, regs)) {

		instruction_pointer_set(regs, ip + MCOUNT_INSN_SIZE);

		if (unlikely(p->post_handler)) {
			kcb->kprobe_status = KPROBE_HIT_SSDONE;
			p->post_handler(p, regs, 0);
		}
	}
	__this_cpu_write(current_kprobe, NULL);
out:
	ftrace_test_recursion_unlock(bit);
}
NOKPROBE_SYMBOL(kprobe_ftrace_handler);

int arch_prepare_kprobe_ftrace(struct kprobe *p)
{
	p->ainsn.insn = NULL;
	return 0;
}
#endif
