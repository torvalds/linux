/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PTRACE_H
#define _ASM_X86_PTRACE_H

#include <asm/segment.h>
#include <asm/page_types.h>
#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__
#ifdef __i386__

struct pt_regs {
	/*
	 * NB: 32-bit x86 CPUs are inconsistent as what happens in the
	 * following cases (where %seg represents a segment register):
	 *
	 * - pushl %seg: some do a 16-bit write and leave the high
	 *   bits alone
	 * - movl %seg, [mem]: some do a 16-bit write despite the movl
	 * - IDT entry: some (e.g. 486) will leave the high bits of CS
	 *   and (if applicable) SS undefined.
	 *
	 * Fortunately, x86-32 doesn't read the high bits on POP or IRET,
	 * so we can just treat all of the segment registers as 16-bit
	 * values.
	 */
	unsigned long bx;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
	unsigned long bp;
	unsigned long ax;
	unsigned short ds;
	unsigned short __dsh;
	unsigned short es;
	unsigned short __esh;
	unsigned short fs;
	unsigned short __fsh;
	/* On interrupt, gs and __gsh store the vector number. */
	unsigned short gs;
	unsigned short __gsh;
	/* On interrupt, this is the error code. */
	unsigned long orig_ax;
	unsigned long ip;
	unsigned short cs;
	unsigned short __csh;
	unsigned long flags;
	unsigned long sp;
	unsigned short ss;
	unsigned short __ssh;
};

#else /* __i386__ */

struct pt_regs {
/*
 * C ABI says these regs are callee-preserved. They aren't saved on kernel entry
 * unless syscall needs a complete, fully filled "struct pt_regs".
 */
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long bp;
	unsigned long bx;
/* These regs are callee-clobbered. Always saved on kernel entry. */
	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long ax;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
/*
 * On syscall entry, this is syscall#. On CPU exception, this is error code.
 * On hw interrupt, it's IRQ number:
 */
	unsigned long orig_ax;
/* Return frame for iretq */
	unsigned long ip;
	unsigned long cs;
	unsigned long flags;
	unsigned long sp;
	unsigned long ss;
/* top of stack page */
};

#endif /* !__i386__ */

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt_types.h>
#endif

struct cpuinfo_x86;
struct task_struct;

extern unsigned long profile_pc(struct pt_regs *regs);

extern unsigned long
convert_ip_to_linear(struct task_struct *child, struct pt_regs *regs);
extern void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs,
			 int error_code, int si_code);


static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->ax;
}

static inline void regs_set_return_value(struct pt_regs *regs, unsigned long rc)
{
	regs->ax = rc;
}

/*
 * user_mode(regs) determines whether a register set came from user
 * mode.  On x86_32, this is true if V8086 mode was enabled OR if the
 * register set was from protected mode with RPL-3 CS value.  This
 * tricky test checks that with one comparison.
 *
 * On x86_64, vm86 mode is mercifully nonexistent, and we don't need
 * the extra check.
 */
static inline int user_mode(struct pt_regs *regs)
{
#ifdef CONFIG_X86_32
	return ((regs->cs & SEGMENT_RPL_MASK) | (regs->flags & X86_VM_MASK)) >= USER_RPL;
#else
	return !!(regs->cs & 3);
#endif
}

static inline int v8086_mode(struct pt_regs *regs)
{
#ifdef CONFIG_X86_32
	return (regs->flags & X86_VM_MASK);
#else
	return 0;	/* No V86 mode support in long mode */
#endif
}

static inline bool user_64bit_mode(struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
#ifndef CONFIG_PARAVIRT_XXL
	/*
	 * On non-paravirt systems, this is the only long mode CPL 3
	 * selector.  We do not allow long mode selectors in the LDT.
	 */
	return regs->cs == __USER_CS;
#else
	/* Headers are too twisted for this to go in paravirt.h. */
	return regs->cs == __USER_CS || regs->cs == pv_info.extra_user_64bit_cs;
#endif
#else /* !CONFIG_X86_64 */
	return false;
#endif
}

#ifdef CONFIG_X86_64
#define current_user_stack_pointer()	current_pt_regs()->sp
#define compat_user_stack_pointer()	current_pt_regs()->sp
#endif

#ifdef CONFIG_X86_32
extern unsigned long kernel_stack_pointer(struct pt_regs *regs);
#else
static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->sp;
}
#endif

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->ip;
}

static inline void instruction_pointer_set(struct pt_regs *regs,
		unsigned long val)
{
	regs->ip = val;
}

static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return regs->bp;
}

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->sp;
}

static inline void user_stack_pointer_set(struct pt_regs *regs,
		unsigned long val)
{
	regs->sp = val;
}

/* Query offset/name of register from its name/offset */
extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);
#define MAX_REG_OFFSET (offsetof(struct pt_regs, ss))

/**
 * regs_get_register() - get register value from its offset
 * @regs:	pt_regs from which register value is gotten.
 * @offset:	offset number of the register.
 *
 * regs_get_register returns the value of a register. The @offset is the
 * offset of the register in struct pt_regs address which specified by @regs.
 * If @offset is bigger than MAX_REG_OFFSET, this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs,
					      unsigned int offset)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return 0;
#ifdef CONFIG_X86_32
	/*
	 * Traps from the kernel do not save sp and ss.
	 * Use the helper function to retrieve sp.
	 */
	if (offset == offsetof(struct pt_regs, sp) &&
	    regs->cs == __KERNEL_CS)
		return kernel_stack_pointer(regs);

	/* The selector fields are 16-bit. */
	if (offset == offsetof(struct pt_regs, cs) ||
	    offset == offsetof(struct pt_regs, ss) ||
	    offset == offsetof(struct pt_regs, ds) ||
	    offset == offsetof(struct pt_regs, es) ||
	    offset == offsetof(struct pt_regs, fs) ||
	    offset == offsetof(struct pt_regs, gs)) {
		return *(u16 *)((unsigned long)regs + offset);

	}
#endif
	return *(unsigned long *)((unsigned long)regs + offset);
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @addr:	address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */
static inline int regs_within_kernel_stack(struct pt_regs *regs,
					   unsigned long addr)
{
	return ((addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1)));
}

/**
 * regs_get_kernel_stack_nth_addr() - get the address of the Nth entry on stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns the address of the @n th entry of the
 * kernel stack which is specified by @regs. If the @n th entry is NOT in
 * the kernel stack, this returns NULL.
 */
static inline unsigned long *regs_get_kernel_stack_nth_addr(struct pt_regs *regs, unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return addr;
	else
		return NULL;
}

/* To avoid include hell, we can't include uaccess.h */
extern long probe_kernel_read(void *dst, const void *src, size_t size);

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack
 * this returns 0.
 */
static inline unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
						      unsigned int n)
{
	unsigned long *addr;
	unsigned long val;
	long ret;

	addr = regs_get_kernel_stack_nth_addr(regs, n);
	if (addr) {
		ret = probe_kernel_read(&val, addr, sizeof(val));
		if (!ret)
			return val;
	}
	return 0;
}

/**
 * regs_get_kernel_argument() - get Nth function argument in kernel
 * @regs:	pt_regs of that context
 * @n:		function argument number (start from 0)
 *
 * regs_get_argument() returns @n th argument of the function call.
 * Note that this chooses most probably assignment, in some case
 * it can be incorrect.
 * This is expected to be called from kprobes or ftrace with regs
 * where the top of stack is the return address.
 */
static inline unsigned long regs_get_kernel_argument(struct pt_regs *regs,
						     unsigned int n)
{
	static const unsigned int argument_offs[] = {
#ifdef __i386__
		offsetof(struct pt_regs, ax),
		offsetof(struct pt_regs, cx),
		offsetof(struct pt_regs, dx),
#define NR_REG_ARGUMENTS 3
#else
		offsetof(struct pt_regs, di),
		offsetof(struct pt_regs, si),
		offsetof(struct pt_regs, dx),
		offsetof(struct pt_regs, cx),
		offsetof(struct pt_regs, r8),
		offsetof(struct pt_regs, r9),
#define NR_REG_ARGUMENTS 6
#endif
	};

	if (n >= NR_REG_ARGUMENTS) {
		n -= NR_REG_ARGUMENTS - 1;
		return regs_get_kernel_stack_nth(regs, n);
	} else
		return regs_get_register(regs, argument_offs[n]);
}

#define arch_has_single_step()	(1)
#ifdef CONFIG_X86_DEBUGCTLMSR
#define arch_has_block_step()	(1)
#else
#define arch_has_block_step()	(boot_cpu_data.x86 >= 6)
#endif

#define ARCH_HAS_USER_SINGLE_STEP_REPORT

/*
 * When hitting ptrace_stop(), we cannot return using SYSRET because
 * that does not restore the full CPU state, only a minimal set.  The
 * ptracer can change arbitrary register values, which is usually okay
 * because the usual ptrace stops run off the signal delivery path which
 * forces IRET; however, ptrace_event() stops happen in arbitrary places
 * in the kernel and don't force IRET path.
 *
 * So force IRET path after a ptrace stop.
 */
#define arch_ptrace_stop_needed(code, info)				\
({									\
	force_iret();							\
	false;								\
})

struct user_desc;
extern int do_get_thread_area(struct task_struct *p, int idx,
			      struct user_desc __user *info);
extern int do_set_thread_area(struct task_struct *p, int idx,
			      struct user_desc __user *info, int can_allocate);

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_PTRACE_H */
