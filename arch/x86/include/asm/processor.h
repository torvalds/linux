/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PROCESSOR_H
#define _ASM_X86_PROCESSOR_H

#include <asm/processor-flags.h>

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;
struct io_bitmap;
struct vm86;

#include <asm/math_emu.h>
#include <asm/segment.h>
#include <asm/types.h>
#include <uapi/asm/sigcontext.h>
#include <asm/current.h>
#include <asm/cpufeatures.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>
#include <asm/percpu.h>
#include <asm/msr.h>
#include <asm/desc_defs.h>
#include <asm/nops.h>
#include <asm/special_insns.h>
#include <asm/fpu/types.h>
#include <asm/unwind_hints.h>

#include <linux/personality.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/math64.h>
#include <linux/err.h>
#include <linux/irqflags.h>
#include <linux/mem_encrypt.h>

/*
 * We handle most unaligned accesses in hardware.  On the other hand
 * unaligned DMA can be quite expensive on some Nehalem processors.
 *
 * Based on this we disable the IP header alignment in network drivers.
 */
#define NET_IP_ALIGN	0

#define HBP_NUM 4

/*
 * These alignment constraints are for performance in the vSMP case,
 * but in the task_struct case we must also meet hardware imposed
 * alignment requirements of the FPU state:
 */
#ifdef CONFIG_X86_VSMP
# define ARCH_MIN_TASKALIGN		(1 << INTERNODE_CACHE_SHIFT)
# define ARCH_MIN_MMSTRUCT_ALIGN	(1 << INTERNODE_CACHE_SHIFT)
#else
# define ARCH_MIN_TASKALIGN		__alignof__(union fpregs_state)
# define ARCH_MIN_MMSTRUCT_ALIGN	0
#endif

enum tlb_infos {
	ENTRIES,
	NR_INFO
};

extern u16 __read_mostly tlb_lli_4k[NR_INFO];
extern u16 __read_mostly tlb_lli_2m[NR_INFO];
extern u16 __read_mostly tlb_lli_4m[NR_INFO];
extern u16 __read_mostly tlb_lld_4k[NR_INFO];
extern u16 __read_mostly tlb_lld_2m[NR_INFO];
extern u16 __read_mostly tlb_lld_4m[NR_INFO];
extern u16 __read_mostly tlb_lld_1g[NR_INFO];

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head_32.S, so think twice
 *  before touching them. [mj]
 */

struct cpuinfo_x86 {
	__u8			x86;		/* CPU family */
	__u8			x86_vendor;	/* CPU vendor */
	__u8			x86_model;
	__u8			x86_stepping;
#ifdef CONFIG_X86_64
	/* Number of 4K pages in DTLB/ITLB combined(in pages): */
	int			x86_tlbsize;
#endif
	__u8			x86_virt_bits;
	__u8			x86_phys_bits;
	/* CPUID returned core id bits: */
	__u8			x86_coreid_bits;
	__u8			cu_id;
	/* Max extended CPUID function supported: */
	__u32			extended_cpuid_level;
	/* Maximum supported CPUID level, -1=no CPUID: */
	int			cpuid_level;
	/*
	 * Align to size of unsigned long because the x86_capability array
	 * is passed to bitops which require the alignment. Use unnamed
	 * union to enforce the array is aligned to size of unsigned long.
	 */
	union {
		__u32		x86_capability[NCAPINTS + NBUGINTS];
		unsigned long	x86_capability_alignment;
	};
	char			x86_vendor_id[16];
	char			x86_model_id[64];
	/* in KB - valid for CPUS which support this call: */
	unsigned int		x86_cache_size;
	int			x86_cache_alignment;	/* In bytes */
	/* Cache QoS architectural values: */
	int			x86_cache_max_rmid;	/* max index */
	int			x86_cache_occ_scale;	/* scale to bytes */
	int			x86_power;
	unsigned long		loops_per_jiffy;
	/* cpuid returned max cores value: */
	u16			x86_max_cores;
	u16			apicid;
	u16			initial_apicid;
	u16			x86_clflush_size;
	/* number of cores as seen by the OS: */
	u16			booted_cores;
	/* Physical processor id: */
	u16			phys_proc_id;
	/* Logical processor id: */
	u16			logical_proc_id;
	/* Core id: */
	u16			cpu_core_id;
	u16			cpu_die_id;
	u16			logical_die_id;
	/* Index into per_cpu list: */
	u16			cpu_index;
	u32			microcode;
	/* Address space bits used by the cache internally */
	u8			x86_cache_bits;
	unsigned		initialized : 1;
} __randomize_layout;

struct cpuid_regs {
	u32 eax, ebx, ecx, edx;
};

enum cpuid_regs_idx {
	CPUID_EAX = 0,
	CPUID_EBX,
	CPUID_ECX,
	CPUID_EDX,
};

#define X86_VENDOR_INTEL	0
#define X86_VENDOR_CYRIX	1
#define X86_VENDOR_AMD		2
#define X86_VENDOR_UMC		3
#define X86_VENDOR_CENTAUR	5
#define X86_VENDOR_TRANSMETA	7
#define X86_VENDOR_NSC		8
#define X86_VENDOR_HYGON	9
#define X86_VENDOR_ZHAOXIN	10
#define X86_VENDOR_NUM		11

#define X86_VENDOR_UNKNOWN	0xff

/*
 * capabilities of CPUs
 */
extern struct cpuinfo_x86	boot_cpu_data;
extern struct cpuinfo_x86	new_cpu_data;

extern __u32			cpu_caps_cleared[NCAPINTS + NBUGINTS];
extern __u32			cpu_caps_set[NCAPINTS + NBUGINTS];

#ifdef CONFIG_SMP
DECLARE_PER_CPU_READ_MOSTLY(struct cpuinfo_x86, cpu_info);
#define cpu_data(cpu)		per_cpu(cpu_info, cpu)
#else
#define cpu_info		boot_cpu_data
#define cpu_data(cpu)		boot_cpu_data
#endif

extern const struct seq_operations cpuinfo_op;

#define cache_line_size()	(boot_cpu_data.x86_cache_alignment)

extern void cpu_detect(struct cpuinfo_x86 *c);

static inline unsigned long long l1tf_pfn_limit(void)
{
	return BIT_ULL(boot_cpu_data.x86_cache_bits - 1 - PAGE_SHIFT);
}

extern void early_cpu_init(void);
extern void identify_boot_cpu(void);
extern void identify_secondary_cpu(struct cpuinfo_x86 *);
extern void print_cpu_info(struct cpuinfo_x86 *);
void print_cpu_msr(struct cpuinfo_x86 *);

#ifdef CONFIG_X86_32
extern int have_cpuid_p(void);
#else
static inline int have_cpuid_p(void)
{
	return 1;
}
#endif
static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
				unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile("cpuid"
	    : "=a" (*eax),
	      "=b" (*ebx),
	      "=c" (*ecx),
	      "=d" (*edx)
	    : "0" (*eax), "2" (*ecx)
	    : "memory");
}

#define native_cpuid_reg(reg)					\
static inline unsigned int native_cpuid_##reg(unsigned int op)	\
{								\
	unsigned int eax = op, ebx, ecx = 0, edx;		\
								\
	native_cpuid(&eax, &ebx, &ecx, &edx);			\
								\
	return reg;						\
}

/*
 * Native CPUID functions returning a single datum.
 */
native_cpuid_reg(eax)
native_cpuid_reg(ebx)
native_cpuid_reg(ecx)
native_cpuid_reg(edx)

/*
 * Friendlier CR3 helpers.
 */
static inline unsigned long read_cr3_pa(void)
{
	return __read_cr3() & CR3_ADDR_MASK;
}

static inline unsigned long native_read_cr3_pa(void)
{
	return __native_read_cr3() & CR3_ADDR_MASK;
}

static inline void load_cr3(pgd_t *pgdir)
{
	write_cr3(__sme_pa(pgdir));
}

/*
 * Note that while the legacy 'TSS' name comes from 'Task State Segment',
 * on modern x86 CPUs the TSS also holds information important to 64-bit mode,
 * unrelated to the task-switch mechanism:
 */
#ifdef CONFIG_X86_32
/* This is the TSS defined by the hardware. */
struct x86_hw_tss {
	unsigned short		back_link, __blh;
	unsigned long		sp0;
	unsigned short		ss0, __ss0h;
	unsigned long		sp1;

	/*
	 * We don't use ring 1, so ss1 is a convenient scratch space in
	 * the same cacheline as sp0.  We use ss1 to cache the value in
	 * MSR_IA32_SYSENTER_CS.  When we context switch
	 * MSR_IA32_SYSENTER_CS, we first check if the new value being
	 * written matches ss1, and, if it's not, then we wrmsr the new
	 * value and update ss1.
	 *
	 * The only reason we context switch MSR_IA32_SYSENTER_CS is
	 * that we set it to zero in vm86 tasks to avoid corrupting the
	 * stack if we were to go through the sysenter path from vm86
	 * mode.
	 */
	unsigned short		ss1;	/* MSR_IA32_SYSENTER_CS */

	unsigned short		__ss1h;
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

	/*
	 * We store cpu_current_top_of_stack in sp1 so it's always accessible.
	 * Linux does not use ring 1, so sp1 is not otherwise needed.
	 */
	u64			sp1;

	/*
	 * Since Linux does not use ring 2, the 'sp2' slot is unused by
	 * hardware.  entry_SYSCALL_64 uses it as scratch space to stash
	 * the user RSP value.
	 */
	u64			sp2;

	u64			reserved2;
	u64			ist[7];
	u32			reserved3;
	u32			reserved4;
	u16			reserved5;
	u16			io_bitmap_base;

} __attribute__((packed));
#endif

/*
 * IO-bitmap sizes:
 */
#define IO_BITMAP_BITS			65536
#define IO_BITMAP_BYTES			(IO_BITMAP_BITS / BITS_PER_BYTE)
#define IO_BITMAP_LONGS			(IO_BITMAP_BYTES / sizeof(long))

#define IO_BITMAP_OFFSET_VALID_MAP				\
	(offsetof(struct tss_struct, io_bitmap.bitmap) -	\
	 offsetof(struct tss_struct, x86_tss))

#define IO_BITMAP_OFFSET_VALID_ALL				\
	(offsetof(struct tss_struct, io_bitmap.mapall) -	\
	 offsetof(struct tss_struct, x86_tss))

#ifdef CONFIG_X86_IOPL_IOPERM
/*
 * sizeof(unsigned long) coming from an extra "long" at the end of the
 * iobitmap. The limit is inclusive, i.e. the last valid byte.
 */
# define __KERNEL_TSS_LIMIT	\
	(IO_BITMAP_OFFSET_VALID_ALL + IO_BITMAP_BYTES + \
	 sizeof(unsigned long) - 1)
#else
# define __KERNEL_TSS_LIMIT	\
	(offsetof(struct tss_struct, x86_tss) + sizeof(struct x86_hw_tss) - 1)
#endif

/* Base offset outside of TSS_LIMIT so unpriviledged IO causes #GP */
#define IO_BITMAP_OFFSET_INVALID	(__KERNEL_TSS_LIMIT + 1)

struct entry_stack {
	unsigned long		words[64];
};

struct entry_stack_page {
	struct entry_stack stack;
} __aligned(PAGE_SIZE);

/*
 * All IO bitmap related data stored in the TSS:
 */
struct x86_io_bitmap {
	/* The sequence number of the last active bitmap. */
	u64			prev_sequence;

	/*
	 * Store the dirty size of the last io bitmap offender. The next
	 * one will have to do the cleanup as the switch out to a non io
	 * bitmap user will just set x86_tss.io_bitmap_base to a value
	 * outside of the TSS limit. So for sane tasks there is no need to
	 * actually touch the io_bitmap at all.
	 */
	unsigned int		prev_max;

	/*
	 * The extra 1 is there because the CPU will access an
	 * additional byte beyond the end of the IO permission
	 * bitmap. The extra byte must be all 1 bits, and must
	 * be within the limit.
	 */
	unsigned long		bitmap[IO_BITMAP_LONGS + 1];

	/*
	 * Special I/O bitmap to emulate IOPL(3). All bytes zero,
	 * except the additional byte at the end.
	 */
	unsigned long		mapall[IO_BITMAP_LONGS + 1];
};

struct tss_struct {
	/*
	 * The fixed hardware portion.  This must not cross a page boundary
	 * at risk of violating the SDM's advice and potentially triggering
	 * errata.
	 */
	struct x86_hw_tss	x86_tss;

	struct x86_io_bitmap	io_bitmap;
} __aligned(PAGE_SIZE);

DECLARE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw);

/* Per CPU interrupt stacks */
struct irq_stack {
	char		stack[IRQ_STACK_SIZE];
} __aligned(IRQ_STACK_SIZE);

DECLARE_PER_CPU(struct irq_stack *, hardirq_stack_ptr);

#ifdef CONFIG_X86_32
DECLARE_PER_CPU(unsigned long, cpu_current_top_of_stack);
#else
/* The RO copy can't be accessed with this_cpu_xyz(), so use the RW copy. */
#define cpu_current_top_of_stack cpu_tss_rw.x86_tss.sp1
#endif

#ifdef CONFIG_X86_64
struct fixed_percpu_data {
	/*
	 * GCC hardcodes the stack canary as %gs:40.  Since the
	 * irq_stack is the object at %gs:0, we reserve the bottom
	 * 48 bytes of the irq stack for the canary.
	 */
	char		gs_base[40];
	unsigned long	stack_canary;
};

DECLARE_PER_CPU_FIRST(struct fixed_percpu_data, fixed_percpu_data) __visible;
DECLARE_INIT_PER_CPU(fixed_percpu_data);

static inline unsigned long cpu_kernelmode_gs_base(int cpu)
{
	return (unsigned long)per_cpu(fixed_percpu_data.gs_base, cpu);
}

DECLARE_PER_CPU(unsigned int, irq_count);
extern asmlinkage void ignore_sysret(void);

#if IS_ENABLED(CONFIG_KVM)
/* Save actual FS/GS selectors and bases to current->thread */
void save_fsgs_for_kvm(void);
#endif
#else	/* X86_64 */
#ifdef CONFIG_STACKPROTECTOR
/*
 * Make sure stack canary segment base is cached-aligned:
 *   "For Intel Atom processors, avoid non zero segment base address
 *    that is not aligned to cache line boundary at all cost."
 * (Optim Ref Manual Assembly/Compiler Coding Rule 15.)
 */
struct stack_canary {
	char __pad[20];		/* canary at %gs:20 */
	unsigned long canary;
};
DECLARE_PER_CPU_ALIGNED(struct stack_canary, stack_canary);
#endif
/* Per CPU softirq stack pointer */
DECLARE_PER_CPU(struct irq_stack *, softirq_stack_ptr);
#endif	/* X86_64 */

extern unsigned int fpu_kernel_xstate_size;
extern unsigned int fpu_user_xstate_size;

struct perf_event;

typedef struct {
	unsigned long		seg;
} mm_segment_t;

struct thread_struct {
	/* Cached TLS descriptors: */
	struct desc_struct	tls_array[GDT_ENTRY_TLS_ENTRIES];
#ifdef CONFIG_X86_32
	unsigned long		sp0;
#endif
	unsigned long		sp;
#ifdef CONFIG_X86_32
	unsigned long		sysenter_cs;
#else
	unsigned short		es;
	unsigned short		ds;
	unsigned short		fsindex;
	unsigned short		gsindex;
#endif

#ifdef CONFIG_X86_64
	unsigned long		fsbase;
	unsigned long		gsbase;
#else
	/*
	 * XXX: this could presumably be unsigned short.  Alternatively,
	 * 32-bit kernels could be taught to use fsindex instead.
	 */
	unsigned long fs;
	unsigned long gs;
#endif

	/* Save middle states of ptrace breakpoints */
	struct perf_event	*ptrace_bps[HBP_NUM];
	/* Debug status used for traps, single steps, etc... */
	unsigned long           debugreg6;
	/* Keep track of the exact dr7 value set by the user */
	unsigned long           ptrace_dr7;
	/* Fault info: */
	unsigned long		cr2;
	unsigned long		trap_nr;
	unsigned long		error_code;
#ifdef CONFIG_VM86
	/* Virtual 86 mode info */
	struct vm86		*vm86;
#endif
	/* IO permissions: */
	struct io_bitmap	*io_bitmap;

	/*
	 * IOPL. Priviledge level dependent I/O permission which is
	 * emulated via the I/O bitmap to prevent user space from disabling
	 * interrupts.
	 */
	unsigned long		iopl_emul;

	mm_segment_t		addr_limit;

	unsigned int		sig_on_uaccess_err:1;
	unsigned int		uaccess_err:1;	/* uaccess failed */

	/* Floating point and extended processor state */
	struct fpu		fpu;
	/*
	 * WARNING: 'fpu' is dynamically-sized.  It *MUST* be at
	 * the end.
	 */
};

/* Whitelist the FPU state from the task_struct for hardened usercopy. */
static inline void arch_thread_struct_whitelist(unsigned long *offset,
						unsigned long *size)
{
	*offset = offsetof(struct thread_struct, fpu.state);
	*size = fpu_kernel_xstate_size;
}

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_COMPAT		0x0002	/* 32bit syscall active (64BIT)*/

static inline void
native_load_sp0(unsigned long sp0)
{
	this_cpu_write(cpu_tss_rw.x86_tss.sp0, sp0);
}

static inline void native_swapgs(void)
{
#ifdef CONFIG_X86_64
	asm volatile("swapgs" ::: "memory");
#endif
}

static inline unsigned long current_top_of_stack(void)
{
	/*
	 *  We can't read directly from tss.sp0: sp0 on x86_32 is special in
	 *  and around vm86 mode and sp0 on x86_64 is special because of the
	 *  entry trampoline.
	 */
	return this_cpu_read_stable(cpu_current_top_of_stack);
}

static inline bool on_thread_stack(void)
{
	return (unsigned long)(current_top_of_stack() -
			       current_stack_pointer) < THREAD_SIZE;
}

#ifdef CONFIG_PARAVIRT_XXL
#include <asm/paravirt.h>
#else
#define __cpuid			native_cpuid

static inline void load_sp0(unsigned long sp0)
{
	native_load_sp0(sp0);
}

#endif /* CONFIG_PARAVIRT_XXL */

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

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
static __always_inline void rep_nop(void)
{
	asm volatile("rep; nop" ::: "memory");
}

static __always_inline void cpu_relax(void)
{
	rep_nop();
}

/*
 * This function forces the icache and prefetched instruction stream to
 * catch up with reality in two very specific cases:
 *
 *  a) Text was modified using one virtual address and is about to be executed
 *     from the same physical page at a different virtual address.
 *
 *  b) Text was modified on a different CPU, may subsequently be
 *     executed on this CPU, and you want to make sure the new version
 *     gets executed.  This generally means you're calling this in a IPI.
 *
 * If you're calling this for a different reason, you're probably doing
 * it wrong.
 */
static inline void sync_core(void)
{
	/*
	 * There are quite a few ways to do this.  IRET-to-self is nice
	 * because it works on every CPU, at any CPL (so it's compatible
	 * with paravirtualization), and it never exits to a hypervisor.
	 * The only down sides are that it's a bit slow (it seems to be
	 * a bit more than 2x slower than the fastest options) and that
	 * it unmasks NMIs.  The "push %cs" is needed because, in
	 * paravirtual environments, __KERNEL_CS may not be a valid CS
	 * value when we do IRET directly.
	 *
	 * In case NMI unmasking or performance ever becomes a problem,
	 * the next best option appears to be MOV-to-CR2 and an
	 * unconditional jump.  That sequence also works on all CPUs,
	 * but it will fault at CPL3 (i.e. Xen PV).
	 *
	 * CPUID is the conventional way, but it's nasty: it doesn't
	 * exist on some 486-like CPUs, and it usually exits to a
	 * hypervisor.
	 *
	 * Like all of Linux's memory ordering operations, this is a
	 * compiler barrier as well.
	 */
#ifdef CONFIG_X86_32
	asm volatile (
		"pushfl\n\t"
		"pushl %%cs\n\t"
		"pushl $1f\n\t"
		"iret\n\t"
		"1:"
		: ASM_CALL_CONSTRAINT : : "memory");
#else
	unsigned int tmp;

	asm volatile (
		UNWIND_HINT_SAVE
		"mov %%ss, %0\n\t"
		"pushq %q0\n\t"
		"pushq %%rsp\n\t"
		"addq $8, (%%rsp)\n\t"
		"pushfq\n\t"
		"mov %%cs, %0\n\t"
		"pushq %q0\n\t"
		"pushq $1f\n\t"
		"iretq\n\t"
		UNWIND_HINT_RESTORE
		"1:"
		: "=&r" (tmp), ASM_CALL_CONSTRAINT : : "cc", "memory");
#endif
}

extern void select_idle_routine(const struct cpuinfo_x86 *c);
extern void amd_e400_c1e_apic_setup(void);

extern unsigned long		boot_option_idle_override;

enum idle_boot_override {IDLE_NO_OVERRIDE=0, IDLE_HALT, IDLE_NOMWAIT,
			 IDLE_POLL};

extern void enable_sep_cpu(void);
extern int sysenter_setup(void);


/* Defined in head.S */
extern struct desc_ptr		early_gdt_descr;

extern void switch_to_new_gdt(int);
extern void load_direct_gdt(int);
extern void load_fixmap_gdt(int);
extern void load_percpu_segment(int);
extern void cpu_init(void);
extern void cr4_init(void);

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

extern void set_task_blockstep(struct task_struct *task, bool on);

/* Boot loader type from the setup header: */
extern int			bootloader_type;
extern int			bootloader_version;

extern char			ignore_fpu_irq;

#define HAVE_ARCH_PICK_MMAP_LAYOUT 1
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

#ifdef CONFIG_X86_32
# define BASE_PREFETCH		""
# define ARCH_HAS_PREFETCH
#else
# define BASE_PREFETCH		"prefetcht0 %P1"
#endif

/*
 * Prefetch instructions for Pentium III (+) and AMD Athlon (+)
 *
 * It's not worth to care about 3dnow prefetches for the K6
 * because they are microcoded there and very slow.
 */
static inline void prefetch(const void *x)
{
	alternative_input(BASE_PREFETCH, "prefetchnta %P1",
			  X86_FEATURE_XMM,
			  "m" (*(const char *)x));
}

/*
 * 3dnow prefetch to get an exclusive cache line.
 * Useful for spinlocks to avoid one state transition in the
 * cache coherency protocol:
 */
static inline void prefetchw(const void *x)
{
	alternative_input(BASE_PREFETCH, "prefetchw %P1",
			  X86_FEATURE_3DNOWPREFETCH,
			  "m" (*(const char *)x));
}

static inline void spin_lock_prefetch(const void *x)
{
	prefetchw(x);
}

#define TOP_OF_INIT_STACK ((unsigned long)&init_stack + sizeof(init_stack) - \
			   TOP_OF_KERNEL_STACK_PADDING)

#define task_top_of_stack(task) ((unsigned long)(task_pt_regs(task) + 1))

#define task_pt_regs(task) \
({									\
	unsigned long __ptr = (unsigned long)task_stack_page(task);	\
	__ptr += THREAD_SIZE - TOP_OF_KERNEL_STACK_PADDING;		\
	((struct pt_regs *)__ptr) - 1;					\
})

#ifdef CONFIG_X86_32
/*
 * User space process size: 3GB (default).
 */
#define IA32_PAGE_OFFSET	PAGE_OFFSET
#define TASK_SIZE		PAGE_OFFSET
#define TASK_SIZE_LOW		TASK_SIZE
#define TASK_SIZE_MAX		TASK_SIZE
#define DEFAULT_MAP_WINDOW	TASK_SIZE
#define STACK_TOP		TASK_SIZE
#define STACK_TOP_MAX		STACK_TOP

#define INIT_THREAD  {							  \
	.sp0			= TOP_OF_INIT_STACK,			  \
	.sysenter_cs		= __KERNEL_CS,				  \
	.addr_limit		= KERNEL_DS,				  \
}

#define KSTK_ESP(task)		(task_pt_regs(task)->sp)

#else
/*
 * User space process size.  This is the first address outside the user range.
 * There are a few constraints that determine this:
 *
 * On Intel CPUs, if a SYSCALL instruction is at the highest canonical
 * address, then that syscall will enter the kernel with a
 * non-canonical return address, and SYSRET will explode dangerously.
 * We avoid this particular problem by preventing anything executable
 * from being mapped at the maximum canonical address.
 *
 * On AMD CPUs in the Ryzen family, there's a nasty bug in which the
 * CPUs malfunction if they execute code from the highest canonical page.
 * They'll speculate right off the end of the canonical space, and
 * bad things happen.  This is worked around in the same way as the
 * Intel problem.
 *
 * With page table isolation enabled, we map the LDT in ... [stay tuned]
 */
#define TASK_SIZE_MAX	((1UL << __VIRTUAL_MASK_SHIFT) - PAGE_SIZE)

#define DEFAULT_MAP_WINDOW	((1UL << 47) - PAGE_SIZE)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define IA32_PAGE_OFFSET	((current->personality & ADDR_LIMIT_3GB) ? \
					0xc0000000 : 0xFFFFe000)

#define TASK_SIZE_LOW		(test_thread_flag(TIF_ADDR32) ? \
					IA32_PAGE_OFFSET : DEFAULT_MAP_WINDOW)
#define TASK_SIZE		(test_thread_flag(TIF_ADDR32) ? \
					IA32_PAGE_OFFSET : TASK_SIZE_MAX)
#define TASK_SIZE_OF(child)	((test_tsk_thread_flag(child, TIF_ADDR32)) ? \
					IA32_PAGE_OFFSET : TASK_SIZE_MAX)

#define STACK_TOP		TASK_SIZE_LOW
#define STACK_TOP_MAX		TASK_SIZE_MAX

#define INIT_THREAD  {						\
	.addr_limit		= KERNEL_DS,			\
}

extern unsigned long KSTK_ESP(struct task_struct *task);

#endif /* CONFIG_X86_64 */

extern void start_thread(struct pt_regs *regs, unsigned long new_ip,
					       unsigned long new_sp);

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define __TASK_UNMAPPED_BASE(task_size)	(PAGE_ALIGN(task_size / 3))
#define TASK_UNMAPPED_BASE		__TASK_UNMAPPED_BASE(TASK_SIZE_LOW)

#define KSTK_EIP(task)		(task_pt_regs(task)->ip)

/* Get/set a process' ability to use the timestamp counter instruction */
#define GET_TSC_CTL(adr)	get_tsc_mode((adr))
#define SET_TSC_CTL(val)	set_tsc_mode((val))

extern int get_tsc_mode(unsigned long adr);
extern int set_tsc_mode(unsigned int val);

DECLARE_PER_CPU(u64, msr_misc_features_shadow);

/* Register/unregister a process' MPX related resource */
#define MPX_ENABLE_MANAGEMENT()	mpx_enable_management()
#define MPX_DISABLE_MANAGEMENT()	mpx_disable_management()

#ifdef CONFIG_X86_INTEL_MPX
extern int mpx_enable_management(void);
extern int mpx_disable_management(void);
#else
static inline int mpx_enable_management(void)
{
	return -EINVAL;
}
static inline int mpx_disable_management(void)
{
	return -EINVAL;
}
#endif /* CONFIG_X86_INTEL_MPX */

#ifdef CONFIG_CPU_SUP_AMD
extern u16 amd_get_nb_id(int cpu);
extern u32 amd_get_nodes_per_socket(void);
#else
static inline u16 amd_get_nb_id(int cpu)		{ return 0; }
static inline u32 amd_get_nodes_per_socket(void)	{ return 0; }
#endif

static inline uint32_t hypervisor_cpuid_base(const char *sig, uint32_t leaves)
{
	uint32_t base, eax, signature[3];

	for (base = 0x40000000; base < 0x40010000; base += 0x100) {
		cpuid(base, &eax, &signature[0], &signature[1], &signature[2]);

		if (!memcmp(sig, signature, 12) &&
		    (leaves == 0 || ((eax - base) >= leaves)))
			return base;
	}

	return 0;
}

extern unsigned long arch_align_stack(unsigned long sp);
void free_init_pages(const char *what, unsigned long begin, unsigned long end);
extern void free_kernel_image_pages(const char *what, void *begin, void *end);

void default_idle(void);
#ifdef	CONFIG_XEN
bool xen_set_default_idle(void);
#else
#define xen_set_default_idle 0
#endif

void stop_this_cpu(void *dummy);
void microcode_check(void);

enum l1tf_mitigations {
	L1TF_MITIGATION_OFF,
	L1TF_MITIGATION_FLUSH_NOWARN,
	L1TF_MITIGATION_FLUSH,
	L1TF_MITIGATION_FLUSH_NOSMT,
	L1TF_MITIGATION_FULL,
	L1TF_MITIGATION_FULL_FORCE
};

extern enum l1tf_mitigations l1tf_mitigation;

enum mds_mitigations {
	MDS_MITIGATION_OFF,
	MDS_MITIGATION_FULL,
	MDS_MITIGATION_VMWERV,
};

enum taa_mitigations {
	TAA_MITIGATION_OFF,
	TAA_MITIGATION_UCODE_NEEDED,
	TAA_MITIGATION_VERW,
	TAA_MITIGATION_TSX_DISABLED,
};

#endif /* _ASM_X86_PROCESSOR_H */
