/* 
 * Written 2000,2002 by Andi Kleen. 
 * 
 * Loosely based on the sparc64 and IA64 32bit emulation loaders.
 * This tricks binfmt_elf.c into loading 32bit binaries using lots 
 * of ugly preprocessor tricks. Talk about very very poor man's inheritance.
 */ 

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/compat.h>
#include <linux/string.h>
#include <linux/binfmts.h>
#include <linux/mm.h>
#include <linux/security.h>
#include <linux/elfcore-compat.h>

#include <asm/segment.h> 
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/user32.h>
#include <asm/sigcontext32.h>
#include <asm/fpu32.h>
#include <asm/i387.h>
#include <asm/uaccess.h>
#include <asm/ia32.h>
#include <asm/vsyscall32.h>

#undef	ELF_ARCH
#undef	ELF_CLASS
#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_386

#undef	elfhdr
#undef	elf_phdr
#undef	elf_note
#undef	elf_addr_t
#define elfhdr		elf32_hdr
#define elf_phdr	elf32_phdr
#define elf_note	elf32_note
#define elf_addr_t	Elf32_Off

#define ELF_NAME "elf/i386"

#define AT_SYSINFO 32
#define AT_SYSINFO_EHDR		33

int sysctl_vsyscall32 = 1;

#undef ARCH_DLINFO
#define ARCH_DLINFO do {  \
	if (sysctl_vsyscall32) { \
		current->mm->context.vdso = (void *)VSYSCALL32_BASE;	\
		NEW_AUX_ENT(AT_SYSINFO, (u32)(u64)VSYSCALL32_VSYSCALL); \
		NEW_AUX_ENT(AT_SYSINFO_EHDR, VSYSCALL32_BASE);    \
	}	\
} while(0)

struct file;

#define IA32_EMULATOR 1

#undef ELF_ET_DYN_BASE

#define ELF_ET_DYN_BASE		(TASK_UNMAPPED_BASE + 0x1000000)

#define jiffies_to_timeval(a,b) do { (b)->tv_usec = 0; (b)->tv_sec = (a)/HZ; }while(0)

#define _GET_SEG(x) \
	({ __u32 seg; asm("movl %%" __stringify(x) ",%0" : "=r"(seg)); seg; })

/* Assumes current==process to be dumped */
#undef	ELF_CORE_COPY_REGS
#define ELF_CORE_COPY_REGS(pr_reg, regs)       		\
	pr_reg[0] = regs->rbx;				\
	pr_reg[1] = regs->rcx;				\
	pr_reg[2] = regs->rdx;				\
	pr_reg[3] = regs->rsi;				\
	pr_reg[4] = regs->rdi;				\
	pr_reg[5] = regs->rbp;				\
	pr_reg[6] = regs->rax;				\
	pr_reg[7] = _GET_SEG(ds);   			\
	pr_reg[8] = _GET_SEG(es);			\
	pr_reg[9] = _GET_SEG(fs);			\
	pr_reg[10] = _GET_SEG(gs);			\
	pr_reg[11] = regs->orig_rax;			\
	pr_reg[12] = regs->rip;				\
	pr_reg[13] = regs->cs;				\
	pr_reg[14] = regs->eflags;			\
	pr_reg[15] = regs->rsp;				\
	pr_reg[16] = regs->ss;


#define elf_prstatus	compat_elf_prstatus
#define elf_prpsinfo	compat_elf_prpsinfo
#define elf_fpregset_t	struct user_i387_ia32_struct
#define	elf_fpxregset_t	struct user32_fxsr_struct
#define user		user32

#undef elf_read_implies_exec
#define elf_read_implies_exec(ex, executable_stack)     (executable_stack != EXSTACK_DISABLE_X)

#define elf_core_copy_regs		elf32_core_copy_regs
static inline void elf32_core_copy_regs(compat_elf_gregset_t *elfregs,
					struct pt_regs *regs)
{
	ELF_CORE_COPY_REGS((&elfregs->ebx), regs)
}

#define elf_core_copy_task_regs		elf32_core_copy_task_regs
static inline int elf32_core_copy_task_regs(struct task_struct *t,
					    compat_elf_gregset_t* elfregs)
{	
	struct pt_regs *pp = task_pt_regs(t);
	ELF_CORE_COPY_REGS((&elfregs->ebx), pp);
	/* fix wrong segments */ 
	elfregs->ds = t->thread.ds;
	elfregs->fs = t->thread.fsindex;
	elfregs->gs = t->thread.gsindex;
	elfregs->es = t->thread.es;
	return 1; 
}

#define elf_core_copy_task_fpregs	elf32_core_copy_task_fpregs
static inline int 
elf32_core_copy_task_fpregs(struct task_struct *tsk, struct pt_regs *regs,
			    elf_fpregset_t *fpu)
{
	struct _fpstate_ia32 *fpstate = (void*)fpu; 
	mm_segment_t oldfs = get_fs();

	if (!tsk_used_math(tsk))
		return 0;
	if (!regs)
		regs = task_pt_regs(tsk);
	if (tsk == current)
		unlazy_fpu(tsk);
	set_fs(KERNEL_DS); 
	save_i387_ia32(tsk, fpstate, regs, 1);
	/* Correct for i386 bug. It puts the fop into the upper 16bits of 
	   the tag word (like FXSAVE), not into the fcs*/ 
	fpstate->cssel |= fpstate->tag & 0xffff0000; 
	set_fs(oldfs); 
	return 1; 
}

#define ELF_CORE_COPY_XFPREGS 1
#define ELF_CORE_XFPREG_TYPE NT_PRXFPREG
#define elf_core_copy_task_xfpregs	elf32_core_copy_task_xfpregs
static inline int 
elf32_core_copy_task_xfpregs(struct task_struct *t, elf_fpxregset_t *xfpu)
{
	struct pt_regs *regs = task_pt_regs(t);
	if (!tsk_used_math(t))
		return 0;
	if (t == current)
		unlazy_fpu(t); 
	memcpy(xfpu, &t->thread.i387.fxsave, sizeof(elf_fpxregset_t));
	xfpu->fcs = regs->cs; 
	xfpu->fos = t->thread.ds; /* right? */ 
	return 1;
}

#undef elf_check_arch
#define elf_check_arch(x) \
	((x)->e_machine == EM_386)

extern int force_personality32;

#undef	ELF_EXEC_PAGESIZE
#undef	ELF_HWCAP
#undef	ELF_PLATFORM
#undef	SET_PERSONALITY
#define ELF_EXEC_PAGESIZE PAGE_SIZE
#define ELF_HWCAP (boot_cpu_data.x86_capability[0])
#define ELF_PLATFORM  ("i686")
#define SET_PERSONALITY(ex, ibcs2)			\
do {							\
	unsigned long new_flags = 0;				\
	if ((ex).e_ident[EI_CLASS] == ELFCLASS32)		\
		new_flags = _TIF_IA32;				\
	if ((current_thread_info()->flags & _TIF_IA32)		\
	    != new_flags)					\
		set_thread_flag(TIF_ABI_PENDING);		\
	else							\
		clear_thread_flag(TIF_ABI_PENDING);		\
	/* XXX This overwrites the user set personality */	\
	current->personality |= force_personality32;		\
} while (0)

/* Override some function names */
#define elf_format			elf32_format

#define init_elf_binfmt			init_elf32_binfmt
#define exit_elf_binfmt			exit_elf32_binfmt

#define load_elf_binary load_elf32_binary

#undef	ELF_PLAT_INIT
#define ELF_PLAT_INIT(r, load_addr)	elf32_init(r)

#undef start_thread
#define start_thread(regs,new_rip,new_rsp) do { \
	asm volatile("movl %0,%%fs" :: "r" (0)); \
	asm volatile("movl %0,%%es; movl %0,%%ds": :"r" (__USER32_DS)); \
	load_gs_index(0); \
	(regs)->rip = (new_rip); \
	(regs)->rsp = (new_rsp); \
	(regs)->eflags = 0x200; \
	(regs)->cs = __USER32_CS; \
	(regs)->ss = __USER32_DS; \
	set_fs(USER_DS); \
} while(0) 


#include <linux/module.h>

MODULE_DESCRIPTION("Binary format loader for compatibility with IA32 ELF binaries."); 
MODULE_AUTHOR("Eric Youngdale, Andi Kleen");

#undef MODULE_DESCRIPTION
#undef MODULE_AUTHOR

static void elf32_init(struct pt_regs *);

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
#define arch_setup_additional_pages syscall32_setup_pages
extern int syscall32_setup_pages(struct linux_binprm *, int exstack);

#include "../../../fs/binfmt_elf.c" 

static void elf32_init(struct pt_regs *regs)
{
	struct task_struct *me = current; 
	regs->rdi = 0;
	regs->rsi = 0;
	regs->rdx = 0;
	regs->rcx = 0;
	regs->rax = 0;
	regs->rbx = 0; 
	regs->rbp = 0; 
	regs->r8 = regs->r9 = regs->r10 = regs->r11 = regs->r12 =
		regs->r13 = regs->r14 = regs->r15 = 0; 
    me->thread.fs = 0; 
	me->thread.gs = 0;
	me->thread.fsindex = 0; 
	me->thread.gsindex = 0;
    me->thread.ds = __USER_DS; 
	me->thread.es = __USER_DS;
}

#ifdef CONFIG_SYSCTL
/* Register vsyscall32 into the ABI table */
#include <linux/sysctl.h>

static ctl_table abi_table2[] = {
	{
		.procname	= "vsyscall32",
		.data		= &sysctl_vsyscall32,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{}
};

static ctl_table abi_root_table2[] = {
	{
		.ctl_name = CTL_ABI,
		.procname = "abi",
		.mode = 0555,
		.child = abi_table2
	},
	{}
};

static __init int ia32_binfmt_init(void)
{ 
	register_sysctl_table(abi_root_table2);
	return 0;
}
__initcall(ia32_binfmt_init);
#endif
