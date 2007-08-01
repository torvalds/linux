#ifndef _ASM_IA64_IA32_PRIV_H
#define _ASM_IA64_IA32_PRIV_H


#include <asm/ia32.h>

#ifdef CONFIG_IA32_SUPPORT

#include <linux/binfmts.h>
#include <linux/compat.h>
#include <linux/rbtree.h>

#include <asm/processor.h>

/*
 * 32 bit structures for IA32 support.
 */

#define IA32_PAGE_SIZE		(1UL << IA32_PAGE_SHIFT)
#define IA32_PAGE_MASK		(~(IA32_PAGE_SIZE - 1))
#define IA32_PAGE_ALIGN(addr)	(((addr) + IA32_PAGE_SIZE - 1) & IA32_PAGE_MASK)
#define IA32_CLOCKS_PER_SEC	100	/* Cast in stone for IA32 Linux */

/*
 * partially mapped pages provide precise accounting of which 4k sub pages
 * are mapped and which ones are not, thereby improving IA-32 compatibility.
 */
struct ia64_partial_page {
	struct ia64_partial_page *next; /* linked list, sorted by address */
	struct rb_node		pp_rb;
	/* 64K is the largest "normal" page supported by ia64 ABI. So 4K*64
	 * should suffice.*/
	unsigned long		bitmap;
	unsigned int		base;
};

struct ia64_partial_page_list {
	struct ia64_partial_page *pp_head; /* list head, points to the lowest
					   * addressed partial page */
	struct rb_root		ppl_rb;
	struct ia64_partial_page *pp_hint; /* pp_hint->next is the last
					   * accessed partial page */
	atomic_t		pp_count; /* reference count */
};

#if PAGE_SHIFT > IA32_PAGE_SHIFT
struct ia64_partial_page_list* ia32_init_pp_list (void);
#else
# define ia32_init_pp_list()	0
#endif

/* sigcontext.h */
/*
 * As documented in the iBCS2 standard..
 *
 * The first part of "struct _fpstate" is just the
 * normal i387 hardware setup, the extra "status"
 * word is used to save the coprocessor status word
 * before entering the handler.
 */
struct _fpreg_ia32 {
       unsigned short significand[4];
       unsigned short exponent;
};

struct _fpxreg_ia32 {
        unsigned short significand[4];
        unsigned short exponent;
        unsigned short padding[3];
};

struct _xmmreg_ia32 {
        unsigned int element[4];
};


struct _fpstate_ia32 {
       unsigned int    cw,
		       sw,
		       tag,
		       ipoff,
		       cssel,
		       dataoff,
		       datasel;
       struct _fpreg_ia32      _st[8];
       unsigned short  status;
       unsigned short  magic;          /* 0xffff = regular FPU data only */

       /* FXSR FPU environment */
       unsigned int         _fxsr_env[6];   /* FXSR FPU env is ignored */
       unsigned int         mxcsr;
       unsigned int         reserved;
       struct _fpxreg_ia32  _fxsr_st[8];    /* FXSR FPU reg data is ignored */
       struct _xmmreg_ia32  _xmm[8];
       unsigned int         padding[56];
};

struct sigcontext_ia32 {
       unsigned short gs, __gsh;
       unsigned short fs, __fsh;
       unsigned short es, __esh;
       unsigned short ds, __dsh;
       unsigned int edi;
       unsigned int esi;
       unsigned int ebp;
       unsigned int esp;
       unsigned int ebx;
       unsigned int edx;
       unsigned int ecx;
       unsigned int eax;
       unsigned int trapno;
       unsigned int err;
       unsigned int eip;
       unsigned short cs, __csh;
       unsigned int eflags;
       unsigned int esp_at_signal;
       unsigned short ss, __ssh;
       unsigned int fpstate;		/* really (struct _fpstate_ia32 *) */
       unsigned int oldmask;
       unsigned int cr2;
};

/* user.h */
/*
 * IA32 (Pentium III/4) FXSR, SSE support
 *
 * Provide support for the GDB 5.0+ PTRACE_{GET|SET}FPXREGS requests for
 * interacting with the FXSR-format floating point environment.  Floating
 * point data can be accessed in the regular format in the usual manner,
 * and both the standard and SIMD floating point data can be accessed via
 * the new ptrace requests.  In either case, changes to the FPU environment
 * will be reflected in the task's state as expected.
 */
struct ia32_user_i387_struct {
	int	cwd;
	int	swd;
	int	twd;
	int	fip;
	int	fcs;
	int	foo;
	int	fos;
	/* 8*10 bytes for each FP-reg = 80 bytes */
	struct _fpreg_ia32 	st_space[8];
};

struct ia32_user_fxsr_struct {
	unsigned short	cwd;
	unsigned short	swd;
	unsigned short	twd;
	unsigned short	fop;
	int	fip;
	int	fcs;
	int	foo;
	int	fos;
	int	mxcsr;
	int	reserved;
	int	st_space[32];	/* 8*16 bytes for each FP-reg = 128 bytes */
	int	xmm_space[32];	/* 8*16 bytes for each XMM-reg = 128 bytes */
	int	padding[56];
};

/* signal.h */
#define IA32_SET_SA_HANDLER(ka,handler,restorer)				\
				((ka)->sa.sa_handler = (__sighandler_t)		\
					(((unsigned long)(restorer) << 32)	\
					 | ((handler) & 0xffffffff)))
#define IA32_SA_HANDLER(ka)	((unsigned long) (ka)->sa.sa_handler & 0xffffffff)
#define IA32_SA_RESTORER(ka)	((unsigned long) (ka)->sa.sa_handler >> 32)

#define __IA32_NR_sigreturn 119
#define __IA32_NR_rt_sigreturn 173

struct sigaction32 {
       unsigned int sa_handler;		/* Really a pointer, but need to deal with 32 bits */
       unsigned int sa_flags;
       unsigned int sa_restorer;	/* Another 32 bit pointer */
       compat_sigset_t sa_mask;		/* A 32 bit mask */
};

struct old_sigaction32 {
       unsigned int  sa_handler;	/* Really a pointer, but need to deal
					     with 32 bits */
       compat_old_sigset_t sa_mask;		/* A 32 bit mask */
       unsigned int sa_flags;
       unsigned int sa_restorer;	/* Another 32 bit pointer */
};

typedef struct sigaltstack_ia32 {
	unsigned int	ss_sp;
	int		ss_flags;
	unsigned int	ss_size;
} stack_ia32_t;

struct ucontext_ia32 {
	unsigned int	  uc_flags;
	unsigned int	  uc_link;
	stack_ia32_t	  uc_stack;
	struct sigcontext_ia32 uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

struct stat64 {
	unsigned long long	st_dev;
	unsigned char	__pad0[4];
	unsigned int	__st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned long long	st_rdev;
	unsigned char	__pad3[4];
	unsigned int	st_size_lo;
	unsigned int	st_size_hi;
	unsigned int	st_blksize;
	unsigned int	st_blocks;	/* Number 512-byte blocks allocated. */
	unsigned int	__pad4;		/* future possible st_blocks high bits */
	unsigned int	st_atime;
	unsigned int	st_atime_nsec;
	unsigned int	st_mtime;
	unsigned int	st_mtime_nsec;
	unsigned int	st_ctime;
	unsigned int	st_ctime_nsec;
	unsigned int	st_ino_lo;
	unsigned int	st_ino_hi;
};

typedef struct compat_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[((128/sizeof(int)) - 3)];

		/* kill() */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;		/* timer id */
			int _overrun;		/* overrun count */
			char _pad[sizeof(unsigned int) - sizeof(int)];
			compat_sigval_t _sigval;	/* same as below */
			int _sys_private;       /* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			unsigned int _pid;	/* which child */
			unsigned int _uid;	/* sender's uid */
			int _status;		/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			unsigned int _addr;	/* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} compat_siginfo_t;

struct old_linux32_dirent {
	u32	d_ino;
	u32	d_offset;
	u16	d_namlen;
	char	d_name[1];
};

/*
 * IA-32 ELF specific definitions for IA-64.
 */

#define _ASM_IA64_ELF_H		/* Don't include elf.h */

#include <linux/sched.h>
#include <asm/processor.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_386)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_386

#define IA32_STACK_TOP		IA32_PAGE_OFFSET
#define IA32_GATE_OFFSET	IA32_PAGE_OFFSET
#define IA32_GATE_END		IA32_PAGE_OFFSET + PAGE_SIZE

/*
 * The system segments (GDT, TSS, LDT) have to be mapped below 4GB so the IA-32 engine can
 * access them.
 */
#define IA32_GDT_OFFSET		(IA32_PAGE_OFFSET + PAGE_SIZE)
#define IA32_TSS_OFFSET		(IA32_PAGE_OFFSET + 2*PAGE_SIZE)
#define IA32_LDT_OFFSET		(IA32_PAGE_OFFSET + 3*PAGE_SIZE)

#define ELF_EXEC_PAGESIZE	IA32_PAGE_SIZE

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.
 * Typical use of this is to invoke "./ld.so someprog" to test out a
 * new version of the loader.  We need to make sure that it is out of
 * the way of the program that it will "exec", and that there is
 * sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE		(IA32_PAGE_OFFSET/3 + 0x1000000)

void ia64_elf32_init(struct pt_regs *regs);
#define ELF_PLAT_INIT(_r, load_addr)	ia64_elf32_init(_r)

/* This macro yields a bitmask that programs can use to figure out
   what instruction set this CPU supports.  */
#define ELF_HWCAP	0

/* This macro yields a string that ld.so will use to load
   implementation specific libraries for optimization.  Not terribly
   relevant until we have real hardware to play with... */
#define ELF_PLATFORM	NULL

#ifdef __KERNEL__
# define SET_PERSONALITY(EX,IBCS2)				\
	(current->personality = (IBCS2) ? PER_SVR4 : PER_LINUX)
#endif

#define IA32_EFLAG	0x200

/*
 * IA-32 ELF specific definitions for IA-64.
 */

#define __USER_CS      0x23
#define __USER_DS      0x2B

/*
 * The per-cpu GDT has 32 entries: see <asm-i386/segment.h>
 */
#define GDT_ENTRIES 32

#define GDT_SIZE	(GDT_ENTRIES * 8)

#define TSS_ENTRY 14
#define LDT_ENTRY	(TSS_ENTRY + 1)

#define IA32_SEGSEL_RPL		(0x3 << 0)
#define IA32_SEGSEL_TI		(0x1 << 2)
#define IA32_SEGSEL_INDEX_SHIFT	3

#define _TSS			((unsigned long) TSS_ENTRY << IA32_SEGSEL_INDEX_SHIFT)
#define _LDT			((unsigned long) LDT_ENTRY << IA32_SEGSEL_INDEX_SHIFT)

#define IA32_SEG_BASE		16
#define IA32_SEG_TYPE		40
#define IA32_SEG_SYS		44
#define IA32_SEG_DPL		45
#define IA32_SEG_P		47
#define IA32_SEG_HIGH_LIMIT	48
#define IA32_SEG_AVL		52
#define IA32_SEG_DB		54
#define IA32_SEG_G		55
#define IA32_SEG_HIGH_BASE	56

#define IA32_SEG_DESCRIPTOR(base, limit, segtype, nonsysseg, dpl, segpresent, avl, segdb, gran)	\
	       (((limit) & 0xffff)								\
		| (((unsigned long) (base) & 0xffffff) << IA32_SEG_BASE)			\
		| ((unsigned long) (segtype) << IA32_SEG_TYPE)					\
		| ((unsigned long) (nonsysseg) << IA32_SEG_SYS)					\
		| ((unsigned long) (dpl) << IA32_SEG_DPL)					\
		| ((unsigned long) (segpresent) << IA32_SEG_P)					\
		| ((((unsigned long) (limit) >> 16) & 0xf) << IA32_SEG_HIGH_LIMIT)		\
		| ((unsigned long) (avl) << IA32_SEG_AVL)					\
		| ((unsigned long) (segdb) << IA32_SEG_DB)					\
		| ((unsigned long) (gran) << IA32_SEG_G)					\
		| ((((unsigned long) (base) >> 24) & 0xff) << IA32_SEG_HIGH_BASE))

#define SEG_LIM		32
#define SEG_TYPE	52
#define SEG_SYS		56
#define SEG_DPL		57
#define SEG_P		59
#define SEG_AVL		60
#define SEG_DB		62
#define SEG_G		63

/* Unscramble an IA-32 segment descriptor into the IA-64 format.  */
#define IA32_SEG_UNSCRAMBLE(sd)									 \
	(   (((sd) >> IA32_SEG_BASE) & 0xffffff) | ((((sd) >> IA32_SEG_HIGH_BASE) & 0xff) << 24) \
	 | ((((sd) & 0xffff) | ((((sd) >> IA32_SEG_HIGH_LIMIT) & 0xf) << 16)) << SEG_LIM)	 \
	 | ((((sd) >> IA32_SEG_TYPE) & 0xf) << SEG_TYPE)					 \
	 | ((((sd) >> IA32_SEG_SYS) & 0x1) << SEG_SYS)						 \
	 | ((((sd) >> IA32_SEG_DPL) & 0x3) << SEG_DPL)						 \
	 | ((((sd) >> IA32_SEG_P) & 0x1) << SEG_P)						 \
	 | ((((sd) >> IA32_SEG_AVL) & 0x1) << SEG_AVL)						 \
	 | ((((sd) >> IA32_SEG_DB) & 0x1) << SEG_DB)						 \
	 | ((((sd) >> IA32_SEG_G) & 0x1) << SEG_G))

#define IA32_IOBASE	0x2000000000000000UL /* Virtual address for I/O space */

#define IA32_CR0	0x80000001	/* Enable PG and PE bits */
#define IA32_CR4	0x600		/* MMXEX and FXSR on */

/*
 *  IA32 floating point control registers starting values
 */

#define IA32_FSR_DEFAULT	0x55550000		/* set all tag bits */
#define IA32_FCR_DEFAULT	0x17800000037fUL	/* extended precision, all masks */

#define IA32_PTRACE_GETREGS	12
#define IA32_PTRACE_SETREGS	13
#define IA32_PTRACE_GETFPREGS	14
#define IA32_PTRACE_SETFPREGS	15
#define IA32_PTRACE_GETFPXREGS	18
#define IA32_PTRACE_SETFPXREGS	19

#define ia32_start_thread(regs,new_ip,new_sp) do {				\
	set_fs(USER_DS);							\
	ia64_psr(regs)->cpl = 3;	/* set user mode */			\
	ia64_psr(regs)->ri = 0;		/* clear return slot number */		\
	ia64_psr(regs)->is = 1;		/* IA-32 instruction set */		\
	regs->cr_iip = new_ip;							\
	regs->ar_rsc = 0xc;		/* enforced lazy mode, priv. level 3 */	\
	regs->ar_rnat = 0;							\
	regs->loadrs = 0;							\
	regs->r12 = new_sp;							\
} while (0)

/*
 * Local Descriptor Table (LDT) related declarations.
 */

#define IA32_LDT_ENTRIES	8192		/* Maximum number of LDT entries supported. */
#define IA32_LDT_ENTRY_SIZE	8		/* The size of each LDT entry. */

#define LDT_entry_a(info) \
	((((info)->base_addr & 0x0000ffff) << 16) | ((info)->limit & 0x0ffff))

#define LDT_entry_b(info)				\
	(((info)->base_addr & 0xff000000) |		\
	(((info)->base_addr & 0x00ff0000) >> 16) |	\
	((info)->limit & 0xf0000) |			\
	(((info)->read_exec_only ^ 1) << 9) |		\
	((info)->contents << 10) |			\
	(((info)->seg_not_present ^ 1) << 15) |		\
	((info)->seg_32bit << 22) |			\
	((info)->limit_in_pages << 23) |		\
	((info)->useable << 20) |			\
	0x7100)

#define LDT_empty(info) (			\
	(info)->base_addr	== 0	&&	\
	(info)->limit		== 0	&&	\
	(info)->contents	== 0	&&	\
	(info)->read_exec_only	== 1	&&	\
	(info)->seg_32bit	== 0	&&	\
	(info)->limit_in_pages	== 0	&&	\
	(info)->seg_not_present	== 1	&&	\
	(info)->useable		== 0	)

static inline void
load_TLS (struct thread_struct *t, unsigned int cpu)
{
	extern unsigned long *cpu_gdt_table[NR_CPUS];

	memcpy(cpu_gdt_table[cpu] + GDT_ENTRY_TLS_MIN + 0, &t->tls_array[0], sizeof(long));
	memcpy(cpu_gdt_table[cpu] + GDT_ENTRY_TLS_MIN + 1, &t->tls_array[1], sizeof(long));
	memcpy(cpu_gdt_table[cpu] + GDT_ENTRY_TLS_MIN + 2, &t->tls_array[2], sizeof(long));
}

struct ia32_user_desc {
	unsigned int entry_number;
	unsigned int base_addr;
	unsigned int limit;
	unsigned int seg_32bit:1;
	unsigned int contents:2;
	unsigned int read_exec_only:1;
	unsigned int limit_in_pages:1;
	unsigned int seg_not_present:1;
	unsigned int useable:1;
};

struct linux_binprm;

extern void ia32_init_addr_space (struct pt_regs *regs);
extern int ia32_setup_arg_pages (struct linux_binprm *bprm, int exec_stack);
extern unsigned long ia32_do_mmap (struct file *, unsigned long, unsigned long, int, int, loff_t);
extern void ia32_load_segment_descriptors (struct task_struct *task);

#define ia32f2ia64f(dst,src)			\
do {						\
	ia64_ldfe(6,src);			\
	ia64_stop();				\
	ia64_stf_spill(dst, 6);			\
} while(0)

#define ia64f2ia32f(dst,src)			\
do {						\
	ia64_ldf_fill(6, src);			\
	ia64_stop();				\
	ia64_stfe(dst, 6);			\
} while(0)

struct user_regs_struct32 {
	__u32 ebx, ecx, edx, esi, edi, ebp, eax;
	unsigned short ds, __ds, es, __es;
	unsigned short fs, __fs, gs, __gs;
	__u32 orig_eax, eip;
	unsigned short cs, __cs;
	__u32 eflags, esp;
	unsigned short ss, __ss;
};

/* Prototypes for use in elfcore32.h */
extern int save_ia32_fpstate (struct task_struct *, struct ia32_user_i387_struct __user *);
extern int save_ia32_fpxstate (struct task_struct *, struct ia32_user_fxsr_struct __user *);

#endif /* !CONFIG_IA32_SUPPORT */

#endif /* _ASM_IA64_IA32_PRIV_H */
