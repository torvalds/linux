/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ELF_H
#define _ASM_X86_ELF_H

/*
 * ELF register definitions..
 */
#include <linux/thread_info.h>

#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/auxvec.h>
#include <asm/fsgsbase.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_i387_struct elf_fpregset_t;

#ifdef __i386__

#define R_386_NONE	0
#define R_386_32	1
#define R_386_PC32	2
#define R_386_GOT32	3
#define R_386_PLT32	4
#define R_386_COPY	5
#define R_386_GLOB_DAT	6
#define R_386_JMP_SLOT	7
#define R_386_RELATIVE	8
#define R_386_GOTOFF	9
#define R_386_GOTPC	10
#define R_386_NUM	11

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_386

#else

/* x86-64 relocation types */
#define R_X86_64_NONE		0	/* No reloc */
#define R_X86_64_64		1	/* Direct 64 bit  */
#define R_X86_64_PC32		2	/* PC relative 32 bit signed */
#define R_X86_64_GOT32		3	/* 32 bit GOT entry */
#define R_X86_64_PLT32		4	/* 32 bit PLT address */
#define R_X86_64_COPY		5	/* Copy symbol at runtime */
#define R_X86_64_GLOB_DAT	6	/* Create GOT entry */
#define R_X86_64_JUMP_SLOT	7	/* Create PLT entry */
#define R_X86_64_RELATIVE	8	/* Adjust by program base */
#define R_X86_64_GOTPCREL	9	/* 32 bit signed pc relative
					   offset to GOT */
#define R_X86_64_32		10	/* Direct 32 bit zero extended */
#define R_X86_64_32S		11	/* Direct 32 bit sign extended */
#define R_X86_64_16		12	/* Direct 16 bit zero extended */
#define R_X86_64_PC16		13	/* 16 bit sign extended pc relative */
#define R_X86_64_8		14	/* Direct 8 bit sign extended  */
#define R_X86_64_PC8		15	/* 8 bit sign extended pc relative */
#define R_X86_64_PC64		24	/* Place relative 64-bit signed */

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_X86_64

#endif

#include <asm/vdso.h>

#ifdef CONFIG_X86_64
extern unsigned int vdso64_enabled;
#endif
#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
extern unsigned int vdso32_enabled;
#endif

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch_ia32(x) \
	(((x)->e_machine == EM_386) || ((x)->e_machine == EM_486))

#include <asm/processor.h>

#ifdef CONFIG_X86_32
#include <asm/desc.h>

#define elf_check_arch(x)	elf_check_arch_ia32(x)

/* SVR4/i386 ABI (pages 3-31, 3-32) says that when the program starts %edx
   contains a pointer to a function which might be registered using `atexit'.
   This provides a mean for the dynamic linker to call DT_FINI functions for
   shared libraries that have been loaded before the code runs.

   A value of 0 tells we have no such handler.

   We might as well make sure everything else is cleared too (except for %esp),
   just to make things more deterministic.
 */
#define ELF_PLAT_INIT(_r, load_addr)		\
	do {					\
	_r->bx = 0; _r->cx = 0; _r->dx = 0;	\
	_r->si = 0; _r->di = 0; _r->bp = 0;	\
	_r->ax = 0;				\
} while (0)

/*
 * regs is struct pt_regs, pr_reg is elf_gregset_t (which is
 * now struct_user_regs, they are different)
 */

#define ELF_CORE_COPY_REGS_COMMON(pr_reg, regs)	\
do {						\
	pr_reg[0] = regs->bx;			\
	pr_reg[1] = regs->cx;			\
	pr_reg[2] = regs->dx;			\
	pr_reg[3] = regs->si;			\
	pr_reg[4] = regs->di;			\
	pr_reg[5] = regs->bp;			\
	pr_reg[6] = regs->ax;			\
	pr_reg[7] = regs->ds;			\
	pr_reg[8] = regs->es;			\
	pr_reg[9] = regs->fs;			\
	pr_reg[11] = regs->orig_ax;		\
	pr_reg[12] = regs->ip;			\
	pr_reg[13] = regs->cs;			\
	pr_reg[14] = regs->flags;		\
	pr_reg[15] = regs->sp;			\
	pr_reg[16] = regs->ss;			\
} while (0);

#define ELF_CORE_COPY_REGS(pr_reg, regs)	\
do {						\
	ELF_CORE_COPY_REGS_COMMON(pr_reg, regs);\
	pr_reg[10] = get_user_gs(regs);		\
} while (0);

#define ELF_CORE_COPY_KERNEL_REGS(pr_reg, regs)	\
do {						\
	ELF_CORE_COPY_REGS_COMMON(pr_reg, regs);\
	savesegment(gs, pr_reg[10]);		\
} while (0);

#define ELF_PLATFORM	(utsname()->machine)
#define set_personality_64bit()	do { } while (0)

#else /* CONFIG_X86_32 */

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x)			\
	((x)->e_machine == EM_X86_64)

#define compat_elf_check_arch(x)					\
	(elf_check_arch_ia32(x) ||					\
	 (IS_ENABLED(CONFIG_X86_X32_ABI) && (x)->e_machine == EM_X86_64))

#if __USER32_DS != __USER_DS
# error "The following code assumes __USER32_DS == __USER_DS"
#endif

static inline void elf_common_init(struct thread_struct *t,
				   struct pt_regs *regs, const u16 ds)
{
	/* ax gets execve's return value. */
	/*regs->ax = */ regs->bx = regs->cx = regs->dx = 0;
	regs->si = regs->di = regs->bp = 0;
	regs->r8 = regs->r9 = regs->r10 = regs->r11 = 0;
	regs->r12 = regs->r13 = regs->r14 = regs->r15 = 0;
	t->fsbase = t->gsbase = 0;
	t->fsindex = t->gsindex = 0;
	t->ds = t->es = ds;
}

#define ELF_PLAT_INIT(_r, load_addr)			\
	elf_common_init(&current->thread, _r, 0)

#define	COMPAT_ELF_PLAT_INIT(regs, load_addr)		\
	elf_common_init(&current->thread, regs, __USER_DS)

void compat_start_thread(struct pt_regs *regs, u32 new_ip, u32 new_sp, bool x32);
#define COMPAT_START_THREAD(ex, regs, new_ip, new_sp)	\
	compat_start_thread(regs, new_ip, new_sp, ex->e_machine == EM_X86_64)

void set_personality_ia32(bool);
#define COMPAT_SET_PERSONALITY(ex)			\
	set_personality_ia32((ex).e_machine == EM_X86_64)

#define COMPAT_ELF_PLATFORM			("i686")

/*
 * regs is struct pt_regs, pr_reg is elf_gregset_t (which is
 * now struct_user_regs, they are different). Assumes current is the process
 * getting dumped.
 */

#define ELF_CORE_COPY_REGS(pr_reg, regs)			\
do {								\
	unsigned v;						\
	(pr_reg)[0] = (regs)->r15;				\
	(pr_reg)[1] = (regs)->r14;				\
	(pr_reg)[2] = (regs)->r13;				\
	(pr_reg)[3] = (regs)->r12;				\
	(pr_reg)[4] = (regs)->bp;				\
	(pr_reg)[5] = (regs)->bx;				\
	(pr_reg)[6] = (regs)->r11;				\
	(pr_reg)[7] = (regs)->r10;				\
	(pr_reg)[8] = (regs)->r9;				\
	(pr_reg)[9] = (regs)->r8;				\
	(pr_reg)[10] = (regs)->ax;				\
	(pr_reg)[11] = (regs)->cx;				\
	(pr_reg)[12] = (regs)->dx;				\
	(pr_reg)[13] = (regs)->si;				\
	(pr_reg)[14] = (regs)->di;				\
	(pr_reg)[15] = (regs)->orig_ax;				\
	(pr_reg)[16] = (regs)->ip;				\
	(pr_reg)[17] = (regs)->cs;				\
	(pr_reg)[18] = (regs)->flags;				\
	(pr_reg)[19] = (regs)->sp;				\
	(pr_reg)[20] = (regs)->ss;				\
	(pr_reg)[21] = x86_fsbase_read_cpu();			\
	(pr_reg)[22] = x86_gsbase_read_cpu_inactive();		\
	asm("movl %%ds,%0" : "=r" (v)); (pr_reg)[23] = v;	\
	asm("movl %%es,%0" : "=r" (v)); (pr_reg)[24] = v;	\
	asm("movl %%fs,%0" : "=r" (v)); (pr_reg)[25] = v;	\
	asm("movl %%gs,%0" : "=r" (v)); (pr_reg)[26] = v;	\
} while (0);

/* I'm not sure if we can use '-' here */
#define ELF_PLATFORM       ("x86_64")
extern void set_personality_64bit(void);
extern unsigned int sysctl_vsyscall32;
extern int force_personality32;

#endif /* !CONFIG_X86_32 */

#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	4096

/*
 * This is the base location for PIE (ET_DYN with INTERP) loads. On
 * 64-bit, this is above 4GB to leave the entire 32-bit address
 * space open for things that want to use the area for 32-bit pointers.
 */
#define ELF_ET_DYN_BASE		(mmap_is_ia32() ? 0x000400000UL : \
						  (DEFAULT_MAP_WINDOW / 3 * 2))

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP		(boot_cpu_data.x86_capability[CPUID_1_EDX])

extern u32 elf_hwcap2;

/*
 * HWCAP2 supplies mask with kernel enabled CPU features, so that
 * the application can discover that it can safely use them.
 * The bits are defined in uapi/asm/hwcap2.h.
 */
#define ELF_HWCAP2		(elf_hwcap2)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define SET_PERSONALITY(ex) set_personality_64bit()

/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically.
 *
 * The decision process for determining the results are:
 *
 *                 CPU: | lacks NX*  | has NX, ia32     | has NX, x86_64 |
 * ELF:                 |            |                  |                |
 * ---------------------|------------|------------------|----------------|
 * missing PT_GNU_STACK | exec-all   | exec-all         | exec-none      |
 * PT_GNU_STACK == RWX  | exec-stack | exec-stack       | exec-stack     |
 * PT_GNU_STACK == RW   | exec-none  | exec-none        | exec-none      |
 *
 *  exec-all  : all PROT_READ user mappings are executable, except when
 *              backed by files on a noexec-filesystem.
 *  exec-none : only PROT_EXEC user mappings are executable.
 *  exec-stack: only the stack and PROT_EXEC user mappings are executable.
 *
 *  *this column has no architectural effect: NX markings are ignored by
 *   hardware, but may have behavioral effects when "wants X" collides with
 *   "cannot be X" constraints in memory permission flags, as in
 *   https://lkml.kernel.org/r/20190418055759.GA3155@mellanox.com
 *
 */
#define elf_read_implies_exec(ex, executable_stack)	\
	(mmap_is_ia32() && executable_stack == EXSTACK_DEFAULT)

struct task_struct;

#define	ARCH_DLINFO_IA32						\
do {									\
	if (VDSO_CURRENT_BASE) {					\
		NEW_AUX_ENT(AT_SYSINFO,	VDSO_ENTRY);			\
		NEW_AUX_ENT(AT_SYSINFO_EHDR, VDSO_CURRENT_BASE);	\
	}								\
} while (0)

/*
 * True on X86_32 or when emulating IA32 on X86_64
 */
static inline int mmap_is_ia32(void)
{
	return IS_ENABLED(CONFIG_X86_32) ||
	       (IS_ENABLED(CONFIG_COMPAT) &&
		test_thread_flag(TIF_ADDR32));
}

extern unsigned long task_size_32bit(void);
extern unsigned long task_size_64bit(int full_addr_space);
extern unsigned long get_mmap_base(int is_legacy);
extern bool mmap_address_hint_valid(unsigned long addr, unsigned long len);

#ifdef CONFIG_X86_32

#define __STACK_RND_MASK(is32bit) (0x7ff)
#define STACK_RND_MASK (0x7ff)

#define ARCH_DLINFO		ARCH_DLINFO_IA32

/* update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT entries changes */

#else /* CONFIG_X86_32 */

/* 1GB for 64bit, 8MB for 32bit */
#define __STACK_RND_MASK(is32bit) ((is32bit) ? 0x7ff : 0x3fffff)
#define STACK_RND_MASK __STACK_RND_MASK(mmap_is_ia32())

#define ARCH_DLINFO							\
do {									\
	if (vdso64_enabled)						\
		NEW_AUX_ENT(AT_SYSINFO_EHDR,				\
			    (unsigned long __force)current->mm->context.vdso); \
} while (0)

/* As a historical oddity, the x32 and x86_64 vDSOs are controlled together. */
#define ARCH_DLINFO_X32							\
do {									\
	if (vdso64_enabled)						\
		NEW_AUX_ENT(AT_SYSINFO_EHDR,				\
			    (unsigned long __force)current->mm->context.vdso); \
} while (0)

#define AT_SYSINFO		32

#define COMPAT_ARCH_DLINFO						\
if (exec->e_machine == EM_X86_64)					\
	ARCH_DLINFO_X32;						\
else if (IS_ENABLED(CONFIG_IA32_EMULATION))				\
	ARCH_DLINFO_IA32

#define COMPAT_ELF_ET_DYN_BASE	(TASK_UNMAPPED_BASE + 0x1000000)

#endif /* !CONFIG_X86_32 */

#define VDSO_CURRENT_BASE	((unsigned long)current->mm->context.vdso)

#define VDSO_ENTRY							\
	((unsigned long)current->mm->context.vdso +			\
	 vdso_image_32.sym___kernel_vsyscall)

struct linux_binprm;

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp);
extern int compat_arch_setup_additional_pages(struct linux_binprm *bprm,
					      int uses_interp, bool x32);
#define COMPAT_ARCH_SETUP_ADDITIONAL_PAGES(bprm, ex, interpreter)	\
	compat_arch_setup_additional_pages(bprm, interpreter,		\
					   (ex->e_machine == EM_X86_64))

extern bool arch_syscall_is_vdso_sigreturn(struct pt_regs *regs);

/* Do not change the values. See get_align_mask() */
enum align_flags {
	ALIGN_VA_32	= BIT(0),
	ALIGN_VA_64	= BIT(1),
};

struct va_alignment {
	int flags;
	unsigned long mask;
	unsigned long bits;
} ____cacheline_aligned;

extern struct va_alignment va_align;
extern unsigned long align_vdso_addr(unsigned long);
#endif /* _ASM_X86_ELF_H */
