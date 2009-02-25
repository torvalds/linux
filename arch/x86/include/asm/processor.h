#ifndef _ASM_X86_PROCESSOR_H
#define _ASM_X86_PROCESSOR_H

#include <asm/processor-flags.h>

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

#include <asm/vm86.h>
#include <asm/math_emu.h>
#include <asm/segment.h>
#include <asm/types.h>
#include <asm/sigcontext.h>
#include <asm/current.h>
#include <asm/cpufeature.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/percpu.h>
#include <asm/msr.h>
#include <asm/desc_defs.h>
#include <asm/nops.h>
#include <asm/ds.h>

#include <linux/personality.h>
#include <linux/cpumask.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/init.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
static inline void *current_text_addr(void)
{
	void *pc;

	asm volatile("mov $1f, %0; 1:":"=r" (pc));

	return pc;
}

#ifdef CONFIG_X86_VSMP
# define ARCH_MIN_TASKALIGN		(1 << INTERNODE_CACHE_SHIFT)
# define ARCH_MIN_MMSTRUCT_ALIGN	(1 << INTERNODE_CACHE_SHIFT)
#else
# define ARCH_MIN_TASKALIGN		16
# define ARCH_MIN_MMSTRUCT_ALIGN	0
#endif

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

struct cpuinfo_x86 {
	__u8			x86;		/* CPU family */
	__u8			x86_vendor;	/* CPU vendor */
	__u8			x86_model;
	__u8			x86_mask;
#ifdef CONFIG_X86_32
	char			wp_works_ok;	/* It doesn't on 386's */

	/* Problems on some 486Dx4's and old 386's: */
	char			hlt_works_ok;
	char			hard_math;
	char			rfu;
	char			fdiv_bug;
	char			f00f_bug;
	char			coma_bug;
	char			pad0;
#else
	/* Number of 4K pages in DTLB/ITLB combined(in pages): */
	int			 x86_tlbsize;
	__u8			x86_virt_bits;
	__u8			x86_phys_bits;
#endif
	/* CPUID returned core id bits: */
	__u8			x86_coreid_bits;
	/* Max extended CPUID function supported: */
	__u32			extended_cpuid_level;
	/* Maximum supported CPUID level, -1=no CPUID: */
	int			cpuid_level;
	__u32			x86_capability[NCAPINTS];
	char			x86_vendor_id[16];
	char			x86_model_id[64];
	/* in KB - valid for CPUS which support this call: */
	int			x86_cache_size;
	int			x86_cache_alignment;	/* In bytes */
	int			x86_power;
	unsigned long		loops_per_jiffy;
#ifdef CONFIG_SMP
	/* cpus sharing the last level cache: */
	cpumask_t		llc_shared_map;
#endif
	/* cpuid returned max cores value: */
	u16			 x86_max_cores;
	u16			apicid;
	u16			initial_apicid;
	u16			x86_clflush_size;
#ifdef CONFIG_SMP
	/* number of cores as seen by the OS: */
	u16			booted_cores;
	/* Physical processor id: */
	u16			phys_proc_id;
	/* Core id: */
	u16			cpu_core_id;
	/* Index into per_cpu list: */
	u16			cpu_index;
#endif
	unsigned int		x86_hyper_vendor;
} __attribute__((__aligned__(SMP_CACHE_BYTES)));

#define X86_VENDOR_INTEL	0
#define X86_VENDOR_CYRIX	1
#define X86_VENDOR_AMD		2
#define X86_VENDOR_UMC		3
#define X86_VENDOR_CENTAUR	5
#define X86_VENDOR_TRANSMETA	7
#define X86_VENDOR_NSC		8
#define X86_VENDOR_NUM		9

#define X86_VENDOR_UNKNOWN	0xff

#define X86_HYPER_VENDOR_NONE  0
#define X86_HYPER_VENDOR_VMWARE 1

/*
 * capabilities of CPUs
 */
extern struct cpuinfo_x86	boot_cpu_data;
extern struct cpuinfo_x86	new_cpu_data;

extern struct tss_struct	doublefault_tss;
extern __u32			cleared_cpu_caps[NCAPINTS];

#ifdef CONFIG_SMP
DECLARE_PER_CPU(struct cpuinfo_x86, cpu_info);
#define cpu_data(cpu)		per_cpu(cpu_info, cpu)
#define current_cpu_data	__get_cpu_var(cpu_info)
#else
#define cpu_data(cpu)		boot_cpu_data
#define current_cpu_data	boot_cpu_data
#endif

extern const struct seq_operations cpuinfo_op;

static inline int hlt_works(int cpu)
{
#ifdef CONFIG_X86_32
	return cpu_data(cpu).hlt_works_ok;
#else
	return 1;
#endif
}

#define cache_line_size()	(boot_cpu_data.x86_cache_alignment)

extern void cpu_detect(struct cpuinfo_x86 *c);

extern struct pt_regs *idle_regs(struct pt_regs *);

extern void early_cpu_init(void);
extern void identify_boot_cpu(void);
extern void identify_secondary_cpu(struct cpuinfo_x86 *);
extern void print_cpu_info(struct cpuinfo_x86 *);
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
extern unsigned int init_intel_cacheinfo(struct cpuinfo_x86 *c);
extern unsigned short num_cache_leaves;

extern void detect_extended_topology(struct cpuinfo_x86 *c);
extern void detect_ht(struct cpuinfo_x86 *c);

static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
				unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm("cpuid"
	    : "=a" (*eax),
	      "=b" (*ebx),
	      "=c" (*ecx),
	      "=d" (*edx)
	    : "0" (*eax), "2" (*ecx));
}

static inline void load_cr3(pgd_t *pgdir)
{
	write_cr3(__pa(pgdir));
}

#ifdef CONFIG_X86_32
/* This is the TSS defined by the hardware. */
struct x86_hw_tss {
	unsigned short		back_link, __blh;
	unsigned long		sp0;
	unsigned short		ss0, __ss0h;
	unsigned long		sp1;
	/* ss1 caches MSR_IA32_SYSENTER_CS: */
	unsigned short		ss1, __ss1h;
	unsigned long		sp2;
	unsigned short		ss2, __ss2h;
	unsigned long		__cr3;
	unsigned long		ip;
	unsigned long		flags;
	unsigned long		ax;
	unsigned long		cx;
	unsigned long		dx;
	unsigned long		bx;
	unsigned long		sp;
	unsigned long		bp;
	unsigned long		si;
	unsigned long		di;
	unsigned short		es, __esh;
	unsigned short		cs, __csh;
	unsigned short		ss, __ssh;
	unsigned short		ds, __dsh;
	unsigned short		fs, __fsh;
	unsigned short		gs, __gsh;
	unsigned short		ldt, __ldth;
	unsigned short		trace;
	unsigned short		io_bitmap_base;

} __attribute__((packed));
#else
struct x86_hw_tss {
	u32			reserved1;
	u64			sp0;
	u64			sp1;
	u64			sp2;
	u64			reserved2;
	u64			ist[7];
	u32			reserved3;
	u32			reserved4;
	u16			reserved5;
	u16			io_bitmap_base;

} __attribute__((packed)) ____cacheline_aligned;
#endif

/*
 * IO-bitmap sizes:
 */
#define IO_BITMAP_BITS			65536
#define IO_BITMAP_BYTES			(IO_BITMAP_BITS/8)
#define IO_BITMAP_LONGS			(IO_BITMAP_BYTES/sizeof(long))
#define IO_BITMAP_OFFSET		offsetof(struct tss_struct, io_bitmap)
#define INVALID_IO_BITMAP_OFFSET	0x8000
#define INVALID_IO_BITMAP_OFFSET_LAZY	0x9000

struct tss_struct {
	/*
	 * The hardware state:
	 */
	struct x86_hw_tss	x86_tss;

	/*
	 * The extra 1 is there because the CPU will access an
	 * additional byte beyond the end of the IO permission
	 * bitmap. The extra byte must be all 1 bits, and must
	 * be within the limit.
	 */
	unsigned long		io_bitmap[IO_BITMAP_LONGS + 1];
	/*
	 * Cache the current maximum and the last task that used the bitmap:
	 */
	unsigned long		io_bitmap_max;
	struct thread_struct	*io_bitmap_owner;

	/*
	 * .. and then another 0x100 bytes for the emergency kernel stack:
	 */
	unsigned long		stack[64];

} ____cacheline_aligned;

DECLARE_PER_CPU(struct tss_struct, init_tss);

/*
 * Save the original ist values for checking stack pointers during debugging
 */
struct orig_ist {
	unsigned long		ist[7];
};

#define	MXCSR_DEFAULT		0x1f80

struct i387_fsave_struct {
	u32			cwd;	/* FPU Control Word		*/
	u32			swd;	/* FPU Status Word		*/
	u32			twd;	/* FPU Tag Word			*/
	u32			fip;	/* FPU IP Offset		*/
	u32			fcs;	/* FPU IP Selector		*/
	u32			foo;	/* FPU Operand Pointer Offset	*/
	u32			fos;	/* FPU Operand Pointer Selector	*/

	/* 8*10 bytes for each FP-reg = 80 bytes:			*/
	u32			st_space[20];

	/* Software status information [not touched by FSAVE ]:		*/
	u32			status;
};

struct i387_fxsave_struct {
	u16			cwd; /* Control Word			*/
	u16			swd; /* Status Word			*/
	u16			twd; /* Tag Word			*/
	u16			fop; /* Last Instruction Opcode		*/
	union {
		struct {
			u64	rip; /* Instruction Pointer		*/
			u64	rdp; /* Data Pointer			*/
		};
		struct {
			u32	fip; /* FPU IP Offset			*/
			u32	fcs; /* FPU IP Selector			*/
			u32	foo; /* FPU Operand Offset		*/
			u32	fos; /* FPU Operand Selector		*/
		};
	};
	u32			mxcsr;		/* MXCSR Register State */
	u32			mxcsr_mask;	/* MXCSR Mask		*/

	/* 8*16 bytes for each FP-reg = 128 bytes:			*/
	u32			st_space[32];

	/* 16*16 bytes for each XMM-reg = 256 bytes:			*/
	u32			xmm_space[64];

	u32			padding[12];

	union {
		u32		padding1[12];
		u32		sw_reserved[12];
	};

} __attribute__((aligned(16)));

struct i387_soft_struct {
	u32			cwd;
	u32			swd;
	u32			twd;
	u32			fip;
	u32			fcs;
	u32			foo;
	u32			fos;
	/* 8*10 bytes for each FP-reg = 80 bytes: */
	u32			st_space[20];
	u8			ftop;
	u8			changed;
	u8			lookahead;
	u8			no_update;
	u8			rm;
	u8			alimit;
	struct math_emu_info	*info;
	u32			entry_eip;
};

struct xsave_hdr_struct {
	u64 xstate_bv;
	u64 reserved1[2];
	u64 reserved2[5];
} __attribute__((packed));

struct xsave_struct {
	struct i387_fxsave_struct i387;
	struct xsave_hdr_struct xsave_hdr;
	/* new processor state extensions will go here */
} __attribute__ ((packed, aligned (64)));

union thread_xstate {
	struct i387_fsave_struct	fsave;
	struct i387_fxsave_struct	fxsave;
	struct i387_soft_struct		soft;
	struct xsave_struct		xsave;
};

#ifdef CONFIG_X86_64
DECLARE_PER_CPU(struct orig_ist, orig_ist);
#endif

extern void print_cpu_info(struct cpuinfo_x86 *);
extern unsigned int xstate_size;
extern void free_thread_xstate(struct task_struct *);
extern struct kmem_cache *task_xstate_cachep;
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
extern unsigned int init_intel_cacheinfo(struct cpuinfo_x86 *c);
extern unsigned short num_cache_leaves;

struct thread_struct {
	/* Cached TLS descriptors: */
	struct desc_struct	tls_array[GDT_ENTRY_TLS_ENTRIES];
	unsigned long		sp0;
	unsigned long		sp;
#ifdef CONFIG_X86_32
	unsigned long		sysenter_cs;
#else
	unsigned long		usersp;	/* Copy from PDA */
	unsigned short		es;
	unsigned short		ds;
	unsigned short		fsindex;
	unsigned short		gsindex;
#endif
	unsigned long		ip;
	unsigned long		fs;
	unsigned long		gs;
	/* Hardware debugging registers: */
	unsigned long		debugreg0;
	unsigned long		debugreg1;
	unsigned long		debugreg2;
	unsigned long		debugreg3;
	unsigned long		debugreg6;
	unsigned long		debugreg7;
	/* Fault info: */
	unsigned long		cr2;
	unsigned long		trap_no;
	unsigned long		error_code;
	/* floating point and extended processor state */
	union thread_xstate	*xstate;
#ifdef CONFIG_X86_32
	/* Virtual 86 mode info */
	struct vm86_struct __user *vm86_info;
	unsigned long		screen_bitmap;
	unsigned long		v86flags;
	unsigned long		v86mask;
	unsigned long		saved_sp0;
	unsigned int		saved_fs;
	unsigned int		saved_gs;
#endif
	/* IO permissions: */
	unsigned long		*io_bitmap_ptr;
	unsigned long		iopl;
	/* Max allowed port in the bitmap, in bytes: */
	unsigned		io_bitmap_max;
/* MSR_IA32_DEBUGCTLMSR value to switch in if TIF_DEBUGCTLMSR is set.  */
	unsigned long	debugctlmsr;
#ifdef CONFIG_X86_DS
/* Debug Store context; see include/asm-x86/ds.h; goes into MSR_IA32_DS_AREA */
	struct ds_context	*ds_ctx;
#endif /* CONFIG_X86_DS */
#ifdef CONFIG_X86_PTRACE_BTS
/* the signal to send on a bts buffer overflow */
	unsigned int	bts_ovfl_signal;
#endif /* CONFIG_X86_PTRACE_BTS */
};

static inline unsigned long native_get_debugreg(int regno)
{
	unsigned long val = 0;	/* Damn you, gcc! */

	switch (regno) {
	case 0:
		asm("mov %%db0, %0" :"=r" (val));
		break;
	case 1:
		asm("mov %%db1, %0" :"=r" (val));
		break;
	case 2:
		asm("mov %%db2, %0" :"=r" (val));
		break;
	case 3:
		asm("mov %%db3, %0" :"=r" (val));
		break;
	case 6:
		asm("mov %%db6, %0" :"=r" (val));
		break;
	case 7:
		asm("mov %%db7, %0" :"=r" (val));
		break;
	default:
		BUG();
	}
	return val;
}

static inline void native_set_debugreg(int regno, unsigned long value)
{
	switch (regno) {
	case 0:
		asm("mov %0, %%db0"	::"r" (value));
		break;
	case 1:
		asm("mov %0, %%db1"	::"r" (value));
		break;
	case 2:
		asm("mov %0, %%db2"	::"r" (value));
		break;
	case 3:
		asm("mov %0, %%db3"	::"r" (value));
		break;
	case 6:
		asm("mov %0, %%db6"	::"r" (value));
		break;
	case 7:
		asm("mov %0, %%db7"	::"r" (value));
		break;
	default:
		BUG();
	}
}

/*
 * Set IOPL bits in EFLAGS from given mask
 */
static inline void native_set_iopl_mask(unsigned mask)
{
#ifdef CONFIG_X86_32
	unsigned int reg;

	asm volatile ("pushfl;"
		      "popl %0;"
		      "andl %1, %0;"
		      "orl %2, %0;"
		      "pushl %0;"
		      "popfl"
		      : "=&r" (reg)
		      : "i" (~X86_EFLAGS_IOPL), "r" (mask));
#endif
}

static inline void
native_load_sp0(struct tss_struct *tss, struct thread_struct *thread)
{
	tss->x86_tss.sp0 = thread->sp0;
#ifdef CONFIG_X86_32
	/* Only happens when SEP is enabled, no need to test "SEP"arately: */
	if (unlikely(tss->x86_tss.ss1 != thread->sysenter_cs)) {
		tss->x86_tss.ss1 = thread->sysenter_cs;
		wrmsr(MSR_IA32_SYSENTER_CS, thread->sysenter_cs, 0);
	}
#endif
}

static inline void native_swapgs(void)
{
#ifdef CONFIG_X86_64
	asm volatile("swapgs" ::: "memory");
#endif
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define __cpuid			native_cpuid
#define paravirt_enabled()	0

/*
 * These special macros can be used to get or set a debugging register
 */
#define get_debugreg(var, register)				\
	(var) = native_get_debugreg(register)
#define set_debugreg(value, register)				\
	native_set_debugreg(register, value)

static inline void load_sp0(struct tss_struct *tss,
			    struct thread_struct *thread)
{
	native_load_sp0(tss, thread);
}

#define set_iopl_mask native_set_iopl_mask
#endif /* CONFIG_PARAVIRT */

/*
 * Save the cr4 feature set we're using (ie
 * Pentium 4MB enable and PPro Global page
 * enable), so that any CPU's that boot up
 * after us can get the correct flags.
 */
extern unsigned long		mmu_cr4_features;

static inline void set_in_cr4(unsigned long mask)
{
	unsigned cr4;

	mmu_cr4_features |= mask;
	cr4 = read_cr4();
	cr4 |= mask;
	write_cr4(cr4);
}

static inline void clear_in_cr4(unsigned long mask)
{
	unsigned cr4;

	mmu_cr4_features &= ~mask;
	cr4 = read_cr4();
	cr4 &= ~mask;
	write_cr4(cr4);
}

typedef struct {
	unsigned long		seg;
} mm_segment_t;


/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy state */
extern void prepare_to_copy(struct task_struct *tsk);

unsigned long get_wchan(struct task_struct *p);

/*
 * Generic CPUID function
 * clear %ecx since some cpus (Cyrix MII) do not set or clear %ecx
 * resulting in stale register contents being returned.
 */
static inline void cpuid(unsigned int op,
			 unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = 0;
	__cpuid(eax, ebx, ecx, edx);
}

/* Some CPUID calls want 'count' to be placed in ecx */
static inline void cpuid_count(unsigned int op, int count,
			       unsigned int *eax, unsigned int *ebx,
			       unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = count;
	__cpuid(eax, ebx, ecx, edx);
}

/*
 * CPUID functions returning a single datum
 */
static inline unsigned int cpuid_eax(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return eax;
}

static inline unsigned int cpuid_ebx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ebx;
}

static inline unsigned int cpuid_ecx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ecx;
}

static inline unsigned int cpuid_edx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return edx;
}

/* REP NOP (PAUSE) is a good thing to insert into busy-wait loops. */
static inline void rep_nop(void)
{
	asm volatile("rep; nop" ::: "memory");
}

static inline void cpu_relax(void)
{
	rep_nop();
}

/* Stop speculative execution: */
static inline void sync_core(void)
{
	int tmp;

	asm volatile("cpuid" : "=a" (tmp) : "0" (1)
		     : "ebx", "ecx", "edx", "memory");
}

static inline void __monitor(const void *eax, unsigned long ecx,
			     unsigned long edx)
{
	/* "monitor %eax, %ecx, %edx;" */
	asm volatile(".byte 0x0f, 0x01, 0xc8;"
		     :: "a" (eax), "c" (ecx), "d"(edx));
}

static inline void __mwait(unsigned long eax, unsigned long ecx)
{
	/* "mwait %eax, %ecx;" */
	asm volatile(".byte 0x0f, 0x01, 0xc9;"
		     :: "a" (eax), "c" (ecx));
}

static inline void __sti_mwait(unsigned long eax, unsigned long ecx)
{
	trace_hardirqs_on();
	/* "mwait %eax, %ecx;" */
	asm volatile("sti; .byte 0x0f, 0x01, 0xc9;"
		     :: "a" (eax), "c" (ecx));
}

extern void mwait_idle_with_hints(unsigned long eax, unsigned long ecx);

extern void select_idle_routine(const struct cpuinfo_x86 *c);

extern unsigned long		boot_option_idle_override;
extern unsigned long		idle_halt;
extern unsigned long		idle_nomwait;

/*
 * on systems with caches, caches must be flashed as the absolute
 * last instruction before going into a suspended halt.  Otherwise,
 * dirty data can linger in the cache and become stale on resume,
 * leading to strange errors.
 *
 * perform a variety of operations to guarantee that the compiler
 * will not reorder instructions.  wbinvd itself is serializing
 * so the processor will not reorder.
 *
 * Systems without cache can just go into halt.
 */
static inline void wbinvd_halt(void)
{
	mb();
	/* check for clflush to determine if wbinvd is legal */
	if (cpu_has_clflush)
		asm volatile("cli; wbinvd; 1: hlt; jmp 1b" : : : "memory");
	else
		while (1)
			halt();
}

extern void enable_sep_cpu(void);
extern int sysenter_setup(void);

/* Defined in head.S */
extern struct desc_ptr		early_gdt_descr;

extern void cpu_set_gdt(int);
extern void switch_to_new_gdt(void);
extern void cpu_init(void);
extern void init_gdt(int cpu);

static inline unsigned long get_debugctlmsr(void)
{
    unsigned long debugctlmsr = 0;

#ifndef CONFIG_X86_DEBUGCTLMSR
	if (boot_cpu_data.x86 < 6)
		return 0;
#endif
	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctlmsr);

    return debugctlmsr;
}

static inline void update_debugctlmsr(unsigned long debugctlmsr)
{
#ifndef CONFIG_X86_DEBUGCTLMSR
	if (boot_cpu_data.x86 < 6)
		return;
#endif
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctlmsr);
}

/*
 * from system description table in BIOS. Mostly for MCA use, but
 * others may find it useful:
 */
extern unsigned int		machine_id;
extern unsigned int		machine_submodel_id;
extern unsigned int		BIOS_revision;

/* Boot loader type from the setup header: */
extern int			bootloader_type;

extern char			ignore_fpu_irq;

#define HAVE_ARCH_PICK_MMAP_LAYOUT 1
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

#ifdef CONFIG_X86_32
# define BASE_PREFETCH		ASM_NOP4
# define ARCH_HAS_PREFETCH
#else
# define BASE_PREFETCH		"prefetcht0 (%1)"
#endif

/*
 * Prefetch instructions for Pentium III (+) and AMD Athlon (+)
 *
 * It's not worth to care about 3dnow prefetches for the K6
 * because they are microcoded there and very slow.
 */
static inline void prefetch(const void *x)
{
	alternative_input(BASE_PREFETCH,
			  "prefetchnta (%1)",
			  X86_FEATURE_XMM,
			  "r" (x));
}

/*
 * 3dnow prefetch to get an exclusive cache line.
 * Useful for spinlocks to avoid one state transition in the
 * cache coherency protocol:
 */
static inline void prefetchw(const void *x)
{
	alternative_input(BASE_PREFETCH,
			  "prefetchw (%1)",
			  X86_FEATURE_3DNOW,
			  "r" (x));
}

static inline void spin_lock_prefetch(const void *x)
{
	prefetchw(x);
}

#ifdef CONFIG_X86_32
/*
 * User space process size: 3GB (default).
 */
#define TASK_SIZE		PAGE_OFFSET
#define STACK_TOP		TASK_SIZE
#define STACK_TOP_MAX		STACK_TOP

#define INIT_THREAD  {							  \
	.sp0			= sizeof(init_stack) + (long)&init_stack, \
	.vm86_info		= NULL,					  \
	.sysenter_cs		= __KERNEL_CS,				  \
	.io_bitmap_ptr		= NULL,					  \
	.fs			= __KERNEL_PERCPU,			  \
}

/*
 * Note that the .io_bitmap member must be extra-big. This is because
 * the CPU will access an additional byte beyond the end of the IO
 * permission bitmap. The extra byte must be all 1 bits, and must
 * be within the limit.
 */
#define INIT_TSS  {							  \
	.x86_tss = {							  \
		.sp0		= sizeof(init_stack) + (long)&init_stack, \
		.ss0		= __KERNEL_DS,				  \
		.ss1		= __KERNEL_CS,				  \
		.io_bitmap_base	= INVALID_IO_BITMAP_OFFSET,		  \
	 },								  \
	.io_bitmap		= { [0 ... IO_BITMAP_LONGS] = ~0 },	  \
}

extern unsigned long thread_saved_pc(struct task_struct *tsk);

#define THREAD_SIZE_LONGS      (THREAD_SIZE/sizeof(unsigned long))
#define KSTK_TOP(info)                                                 \
({                                                                     \
       unsigned long *__ptr = (unsigned long *)(info);                 \
       (unsigned long)(&__ptr[THREAD_SIZE_LONGS]);                     \
})

/*
 * The below -8 is to reserve 8 bytes on top of the ring0 stack.
 * This is necessary to guarantee that the entire "struct pt_regs"
 * is accessable even if the CPU haven't stored the SS/ESP registers
 * on the stack (interrupt gate does not save these registers
 * when switching to the same priv ring).
 * Therefore beware: accessing the ss/esp fields of the
 * "struct pt_regs" is possible, but they may contain the
 * completely wrong values.
 */
#define task_pt_regs(task)                                             \
({                                                                     \
       struct pt_regs *__regs__;                                       \
       __regs__ = (struct pt_regs *)(KSTK_TOP(task_stack_page(task))-8); \
       __regs__ - 1;                                                   \
})

#define KSTK_ESP(task)		(task_pt_regs(task)->sp)

#else
/*
 * User space process size. 47bits minus one guard page.
 */
#define TASK_SIZE64	((1UL << 47) - PAGE_SIZE)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define IA32_PAGE_OFFSET	((current->personality & ADDR_LIMIT_3GB) ? \
					0xc0000000 : 0xFFFFe000)

#define TASK_SIZE		(test_thread_flag(TIF_IA32) ? \
					IA32_PAGE_OFFSET : TASK_SIZE64)
#define TASK_SIZE_OF(child)	((test_tsk_thread_flag(child, TIF_IA32)) ? \
					IA32_PAGE_OFFSET : TASK_SIZE64)

#define STACK_TOP		TASK_SIZE
#define STACK_TOP_MAX		TASK_SIZE64

#define INIT_THREAD  { \
	.sp0 = (unsigned long)&init_stack + sizeof(init_stack) \
}

#define INIT_TSS  { \
	.x86_tss.sp0 = (unsigned long)&init_stack + sizeof(init_stack) \
}

/*
 * Return saved PC of a blocked thread.
 * What is this good for? it will be always the scheduler or ret_from_fork.
 */
#define thread_saved_pc(t)	(*(unsigned long *)((t)->thread.sp - 8))

#define task_pt_regs(tsk)	((struct pt_regs *)(tsk)->thread.sp0 - 1)
#define KSTK_ESP(tsk)		-1 /* sorry. doesn't work for syscall. */
#endif /* CONFIG_X86_64 */

extern void start_thread(struct pt_regs *regs, unsigned long new_ip,
					       unsigned long new_sp);

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 3))

#define KSTK_EIP(task)		(task_pt_regs(task)->ip)

/* Get/set a process' ability to use the timestamp counter instruction */
#define GET_TSC_CTL(adr)	get_tsc_mode((adr))
#define SET_TSC_CTL(val)	set_tsc_mode((val))

extern int get_tsc_mode(unsigned long adr);
extern int set_tsc_mode(unsigned int val);

#endif /* _ASM_X86_PROCESSOR_H */
