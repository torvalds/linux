/* 
 * Written 2000,2002 by Andi Kleen. 
 * 
 * Loosely based on the sparc64 and IA64 32bit emulation loaders.
 * This tricks binfmt_elf.c into loading 32bit binaries using lots 
 * of ugly preprocessor tricks. Talk about very very poor man's inheritance.
 */ 
#include <linux/types.h>
#include <linux/config.h> 
#include <linux/stddef.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/compat.h>
#include <linux/string.h>
#include <linux/binfmts.h>
#include <linux/mm.h>
#include <linux/security.h>

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

#define ELF_NAME "elf/i386"

#define AT_SYSINFO 32
#define AT_SYSINFO_EHDR		33

int sysctl_vsyscall32 = 1;

#define ARCH_DLINFO do {  \
	if (sysctl_vsyscall32) { \
	NEW_AUX_ENT(AT_SYSINFO, (u32)(u64)VSYSCALL32_VSYSCALL); \
	NEW_AUX_ENT(AT_SYSINFO_EHDR, VSYSCALL32_BASE);    \
	}	\
} while(0)

struct file;
struct elf_phdr; 

#define IA32_EMULATOR 1

#define ELF_ET_DYN_BASE		(TASK_UNMAPPED_BASE + 0x1000000)

#undef ELF_ARCH
#define ELF_ARCH EM_386

#undef ELF_CLASS
#define ELF_CLASS ELFCLASS32

#define ELF_DATA	ELFDATA2LSB

#define USE_ELF_CORE_DUMP 1

/* Overwrite elfcore.h */ 
#define _LINUX_ELFCORE_H 1
typedef unsigned int elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct32) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/*
 * These macros parameterize elf_core_dump in fs/binfmt_elf.c to write out
 * extra segments containing the vsyscall DSO contents.  Dumping its
 * contents makes post-mortem fully interpretable later without matching up
 * the same kernel and hardware config to see what PC values meant.
 * Dumping its extra ELF program headers includes all the other information
 * a debugger needs to easily find how the vsyscall DSO was being used.
 */
#define ELF_CORE_EXTRA_PHDRS		(VSYSCALL32_EHDR->e_phnum)
#define ELF_CORE_WRITE_EXTRA_PHDRS					      \
do {									      \
	const struct elf32_phdr *const vsyscall_phdrs =			      \
		(const struct elf32_phdr *) (VSYSCALL32_BASE		      \
					   + VSYSCALL32_EHDR->e_phoff);	      \
	int i;								      \
	Elf32_Off ofs = 0;						      \
	for (i = 0; i < VSYSCALL32_EHDR->e_phnum; ++i) {		      \
		struct elf32_phdr phdr = vsyscall_phdrs[i];		      \
		if (phdr.p_type == PT_LOAD) {				      \
			BUG_ON(ofs != 0);				      \
			ofs = phdr.p_offset = offset;			      \
			phdr.p_memsz = PAGE_ALIGN(phdr.p_memsz);	      \
			phdr.p_filesz = phdr.p_memsz;			      \
			offset += phdr.p_filesz;			      \
		}							      \
		else							      \
			phdr.p_offset += ofs;				      \
		phdr.p_paddr = 0; /* match other core phdrs */		      \
		DUMP_WRITE(&phdr, sizeof(phdr));			      \
	}								      \
} while (0)
#define ELF_CORE_WRITE_EXTRA_DATA					      \
do {									      \
	const struct elf32_phdr *const vsyscall_phdrs =			      \
		(const struct elf32_phdr *) (VSYSCALL32_BASE		      \
					   + VSYSCALL32_EHDR->e_phoff);	      \
	int i;								      \
	for (i = 0; i < VSYSCALL32_EHDR->e_phnum; ++i) {		      \
		if (vsyscall_phdrs[i].p_type == PT_LOAD)		      \
			DUMP_WRITE((void *) (u64) vsyscall_phdrs[i].p_vaddr,	      \
				   PAGE_ALIGN(vsyscall_phdrs[i].p_memsz));    \
	}								      \
} while (0)

struct elf_siginfo
{
	int	si_signo;			/* signal number */
	int	si_code;			/* extra code */
	int	si_errno;			/* errno */
};

#define jiffies_to_timeval(a,b) do { (b)->tv_usec = 0; (b)->tv_sec = (a)/HZ; }while(0)

struct elf_prstatus
{
	struct elf_siginfo pr_info;	/* Info associated with signal */
	short	pr_cursig;		/* Current signal */
	unsigned int pr_sigpend;	/* Set of pending signals */
	unsigned int pr_sighold;	/* Set of held signals */
	pid_t	pr_pid;
	pid_t	pr_ppid;
	pid_t	pr_pgrp;
	pid_t	pr_sid;
	struct compat_timeval pr_utime;	/* User time */
	struct compat_timeval pr_stime;	/* System time */
	struct compat_timeval pr_cutime;	/* Cumulative user time */
	struct compat_timeval pr_cstime;	/* Cumulative system time */
	elf_gregset_t pr_reg;	/* GP registers */
	int pr_fpvalid;		/* True if math co-processor being used.  */
};

#define ELF_PRARGSZ	(80)	/* Number of chars for args */

struct elf_prpsinfo
{
	char	pr_state;	/* numeric process state */
	char	pr_sname;	/* char for pr_state */
	char	pr_zomb;	/* zombie */
	char	pr_nice;	/* nice val */
	unsigned int pr_flag;	/* flags */
	__u16	pr_uid;
	__u16	pr_gid;
	pid_t	pr_pid, pr_ppid, pr_pgrp, pr_sid;
	/* Lots missing */
	char	pr_fname[16];	/* filename of executable */
	char	pr_psargs[ELF_PRARGSZ];	/* initial part of arg list */
};

#define __STR(x) #x
#define STR(x) __STR(x)

#define _GET_SEG(x) \
	({ __u32 seg; asm("movl %%" STR(x) ",%0" : "=r"(seg)); seg; })

/* Assumes current==process to be dumped */
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

#define user user32

#define __ASM_X86_64_ELF_H 1
#define elf_read_implies_exec(ex, have_pt_gnu_stack)	(!(have_pt_gnu_stack))
//#include <asm/ia32.h>
#include <linux/elf.h>

typedef struct user_i387_ia32_struct elf_fpregset_t;
typedef struct user32_fxsr_struct elf_fpxregset_t;


static inline void elf_core_copy_regs(elf_gregset_t *elfregs, struct pt_regs *regs)
{
	ELF_CORE_COPY_REGS((*elfregs), regs)
}

static inline int elf_core_copy_task_regs(struct task_struct *t, elf_gregset_t* elfregs)
{	
	struct pt_regs *pp = (struct pt_regs *)(t->thread.rsp0);
	--pp;
	ELF_CORE_COPY_REGS((*elfregs), pp);
	/* fix wrong segments */ 
	(*elfregs)[7] = t->thread.ds; 
	(*elfregs)[9] = t->thread.fsindex; 
	(*elfregs)[10] = t->thread.gsindex; 
	(*elfregs)[8] = t->thread.es; 	
	return 1; 
}

static inline int 
elf_core_copy_task_fpregs(struct task_struct *tsk, struct pt_regs *regs, elf_fpregset_t *fpu)
{
	struct _fpstate_ia32 *fpstate = (void*)fpu; 
	mm_segment_t oldfs = get_fs();

	if (!tsk_used_math(tsk))
		return 0;
	if (!regs)
		regs = (struct pt_regs *)tsk->thread.rsp0;
	--regs;
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
static inline int 
elf_core_copy_task_xfpregs(struct task_struct *t, elf_fpxregset_t *xfpu)
{
	struct pt_regs *regs = ((struct pt_regs *)(t->thread.rsp0))-1; 
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

#define ELF_PLAT_INIT(r, load_addr)	elf32_init(r)
#define setup_arg_pages(bprm, stack_top, exec_stack) \
	ia32_setup_arg_pages(bprm, stack_top, exec_stack)
int ia32_setup_arg_pages(struct linux_binprm *bprm, unsigned long stack_top, int executable_stack);

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


#define elf_map elf32_map

#include <linux/module.h>

MODULE_DESCRIPTION("Binary format loader for compatibility with IA32 ELF binaries."); 
MODULE_AUTHOR("Eric Youngdale, Andi Kleen");

#undef MODULE_DESCRIPTION
#undef MODULE_AUTHOR

#define elf_addr_t __u32

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

int setup_arg_pages(struct linux_binprm *bprm, unsigned long stack_top, int executable_stack)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	struct mm_struct *mm = current->mm;
	int i, ret;

	stack_base = IA32_STACK_TOP - MAX_ARG_PAGES * PAGE_SIZE;
	mm->arg_start = bprm->p + stack_base;

	bprm->p += stack_base;
	if (bprm->loader)
		bprm->loader += stack_base;
	bprm->exec += stack_base;

	mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!mpnt) 
		return -ENOMEM; 

	memset(mpnt, 0, sizeof(*mpnt));

	down_write(&mm->mmap_sem);
	{
		mpnt->vm_mm = mm;
		mpnt->vm_start = PAGE_MASK & (unsigned long) bprm->p;
		mpnt->vm_end = IA32_STACK_TOP;
		if (executable_stack == EXSTACK_ENABLE_X)
			mpnt->vm_flags = VM_STACK_FLAGS |  VM_EXEC;
		else if (executable_stack == EXSTACK_DISABLE_X)
			mpnt->vm_flags = VM_STACK_FLAGS & ~VM_EXEC;
		else
			mpnt->vm_flags = VM_STACK_FLAGS;
 		mpnt->vm_page_prot = (mpnt->vm_flags & VM_EXEC) ? 
 			PAGE_COPY_EXEC : PAGE_COPY;
		if ((ret = insert_vm_struct(mm, mpnt))) {
			up_write(&mm->mmap_sem);
			kmem_cache_free(vm_area_cachep, mpnt);
			return ret;
		}
		mm->stack_vm = mm->total_vm = vma_pages(mpnt);
	} 

	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page *page = bprm->page[i];
		if (page) {
			bprm->page[i] = NULL;
			install_arg_page(mpnt, page, stack_base);
		}
		stack_base += PAGE_SIZE;
	}
	up_write(&mm->mmap_sem);
	
	return 0;
}

static unsigned long
elf32_map (struct file *filep, unsigned long addr, struct elf_phdr *eppnt, int prot, int type)
{
	unsigned long map_addr;
	struct task_struct *me = current; 

	down_write(&me->mm->mmap_sem);
	map_addr = do_mmap(filep, ELF_PAGESTART(addr),
			   eppnt->p_filesz + ELF_PAGEOFFSET(eppnt->p_vaddr), prot, 
			   type,
			   eppnt->p_offset - ELF_PAGEOFFSET(eppnt->p_vaddr));
	up_write(&me->mm->mmap_sem);
	return(map_addr);
}

#ifdef CONFIG_SYSCTL
/* Register vsyscall32 into the ABI table */
#include <linux/sysctl.h>

static ctl_table abi_table2[] = {
	{ 99, "vsyscall32", &sysctl_vsyscall32, sizeof(int), 0644, NULL,
	  proc_dointvec },
	{ 0, }
}; 

static ctl_table abi_root_table2[] = { 
	{ .ctl_name = CTL_ABI, .procname = "abi", .mode = 0555, 
	  .child = abi_table2 }, 
	{ 0 }, 
}; 

static __init int ia32_binfmt_init(void)
{ 
	register_sysctl_table(abi_root_table2, 1);
	return 0;
}
__initcall(ia32_binfmt_init);
#endif
