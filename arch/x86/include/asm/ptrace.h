#ifndef _ASM_X86_PTRACE_H
#define _ASM_X86_PTRACE_H

#include <asm/segment.h>
#include <asm/page_types.h>
#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__
#ifdef __i386__

struct pt_regs {
	unsigned long bx;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
	unsigned long bp;
	unsigned long ax;
	unsigned long ds;
	unsigned long es;
	unsigned long fs;
	unsigned long gs;
	unsigned long orig_ax;
	unsigned long ip;
	unsigned long cs;
	unsigned long flags;
	unsigned long sp;
	unsigned long ss;
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
#define profile_pc profile_pc

extern unsigned long
convert_ip_to_linear(struct task_struct *child, struct pt_regs *regs);
extern void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs,
			 int error_code, int si_code);


extern unsigned long syscall_trace_enter_phase1(struct pt_regs *, u32 arch);
extern long syscall_trace_enter_phase2(struct pt_regs *, u32 arch,
				       unsigned long phase1_result);

extern long syscall_trace_enter(struct pt_regs *);
extern void syscall_trace_leave(struct pt_regs *);

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->ax;
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
	return (regs->cs & SEGMENT_RPL_MASK) == USER_RPL;
#else
	return !!(regs->cs & 3);
#endif
}

static inline int user_mode_vm(struct pt_regs *regs)
{
	return user_mode(regs);
}

/*
 * This is the fastest way to check whether regs come from user space.
 * It is unsafe if regs might come from vm86 mode, though -- in vm86
 * mode, all bits of CS and SS are completely under the user's control.
 * The CPU considers vm86 mode to be CPL 3 regardless of CS and SS.
 *
 * Do NOT use this function unless you have already ruled out the
 * possibility that regs came from vm86 mode.
 *
 * We check for RPL != 0 instead of RPL == 3 because we don't use rings
 * 1 or 2 and this is more efficient.
 */
static inline int user_mode_ignore_vm86(struct pt_regs *regs)
{
	return (regs->cs & SEGMENT_RPL_MASK) != 0;
}

static inline int v8086_mode(struct pt_regs *regs)
{
#ifdef CONFIG_X86_32
	return (regs->flags & X86_VM_MASK);
#else
	return 0;	/* No V86 mode support in long mode */
#endif
}

#ifdef CONFIG_X86_64
static inline bool user_64bit_mode(struct pt_regs *regs)
{
#ifndef CONFIG_PARAVIRT
	/*
	 * On non-paravirt systems, this is the only long mode CPL 3
	 * selector.  We do not allow long mode selectors in the LDT.
	 */
	return regs->cs == __USER_CS;
#else
	/* Headers are too twisted for this to go in paravirt.h. */
	return regs->cs == __USER_CS || regs->cs == pv_info.extra_user_64bit_cs;
#endif
}

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

#define GET_IP(regs) ((regs)->ip)
#define GET_FP(regs) ((regs)->bp)
#define GET_USP(regs) ((regs)->sp)

#include <asm-generic/ptrace.h>

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
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
static inline unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
						      unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);
	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}

#define arch_has_single_step()	(1)
#ifdef CONFIG_X86_DEBUGCTLMSR
#define arch_has_block_step()	(1)
#else
#define arch_has_block_step()	(boot_cpu_data.x86 >= 6)
#endif

#define ARCH_HAS_USER_SINGLE_STEP_INFO

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
