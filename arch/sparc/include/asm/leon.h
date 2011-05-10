/*
 * Copyright (C) 2004 Konrad Eisele (eiselekd@web.de,konrad@gaisler.com) Gaisler Research
 * Copyright (C) 2004 Stefan Holst (mail@s-holst.de) Uni-Stuttgart
 * Copyright (C) 2009 Daniel Hellstrom (daniel@gaisler.com) Aeroflex Gaisler AB
 * Copyright (C) 2009 Konrad Eisele (konrad@gaisler.com) Aeroflex Gaisler AB
 */

#ifndef LEON_H_INCLUDE
#define LEON_H_INCLUDE

#ifdef CONFIG_SPARC_LEON

#define ASI_LEON_NOCACHE	0x01

#define ASI_LEON_DCACHE_MISS	0x1

#define ASI_LEON_CACHEREGS	0x02
#define ASI_LEON_IFLUSH		0x10
#define ASI_LEON_DFLUSH		0x11

#define ASI_LEON_MMUFLUSH	0x18
#define ASI_LEON_MMUREGS	0x19
#define ASI_LEON_BYPASS		0x1c
#define ASI_LEON_FLUSH_PAGE	0x10

/* mmu register access, ASI_LEON_MMUREGS */
#define LEON_CNR_CTRL		0x000
#define LEON_CNR_CTXP		0x100
#define LEON_CNR_CTX		0x200
#define LEON_CNR_F		0x300
#define LEON_CNR_FADDR		0x400

#define LEON_CNR_CTX_NCTX	256	/*number of MMU ctx */

#define LEON_CNR_CTRL_TLBDIS	0x80000000

#define LEON_MMUTLB_ENT_MAX	64

/*
 * diagnostic access from mmutlb.vhd:
 * 0: pte address
 * 4: pte
 * 8: additional flags
 */
#define LEON_DIAGF_LVL		0x3
#define LEON_DIAGF_WR		0x8
#define LEON_DIAGF_WR_SHIFT	3
#define LEON_DIAGF_HIT		0x10
#define LEON_DIAGF_HIT_SHIFT	4
#define LEON_DIAGF_CTX		0x1fe0
#define LEON_DIAGF_CTX_SHIFT	5
#define LEON_DIAGF_VALID	0x2000
#define LEON_DIAGF_VALID_SHIFT	13

/*
 *  Interrupt Sources
 *
 *  The interrupt source numbers directly map to the trap type and to
 *  the bits used in the Interrupt Clear, Interrupt Force, Interrupt Mask,
 *  and the Interrupt Pending Registers.
 */
#define LEON_INTERRUPT_CORRECTABLE_MEMORY_ERROR	1
#define LEON_INTERRUPT_UART_1_RX_TX		2
#define LEON_INTERRUPT_UART_0_RX_TX		3
#define LEON_INTERRUPT_EXTERNAL_0		4
#define LEON_INTERRUPT_EXTERNAL_1		5
#define LEON_INTERRUPT_EXTERNAL_2		6
#define LEON_INTERRUPT_EXTERNAL_3		7
#define LEON_INTERRUPT_TIMER1			8
#define LEON_INTERRUPT_TIMER2			9
#define LEON_INTERRUPT_EMPTY1			10
#define LEON_INTERRUPT_EMPTY2			11
#define LEON_INTERRUPT_OPEN_ETH			12
#define LEON_INTERRUPT_EMPTY4			13
#define LEON_INTERRUPT_EMPTY5			14
#define LEON_INTERRUPT_EMPTY6			15

/* irq masks */
#define LEON_HARD_INT(x)	(1 << (x))	/* irq 0-15 */
#define LEON_IRQMASK_R		0x0000fffe	/* bit 15- 1 of lregs.irqmask */
#define LEON_IRQPRIO_R		0xfffe0000	/* bit 31-17 of lregs.irqmask */

/* leon uart register definitions */
#define LEON_OFF_UDATA	0x0
#define LEON_OFF_USTAT	0x4
#define LEON_OFF_UCTRL	0x8
#define LEON_OFF_USCAL	0xc

#define LEON_UCTRL_RE	0x01
#define LEON_UCTRL_TE	0x02
#define LEON_UCTRL_RI	0x04
#define LEON_UCTRL_TI	0x08
#define LEON_UCTRL_PS	0x10
#define LEON_UCTRL_PE	0x20
#define LEON_UCTRL_FL	0x40
#define LEON_UCTRL_LB	0x80

#define LEON_USTAT_DR	0x01
#define LEON_USTAT_TS	0x02
#define LEON_USTAT_TH	0x04
#define LEON_USTAT_BR	0x08
#define LEON_USTAT_OV	0x10
#define LEON_USTAT_PE	0x20
#define LEON_USTAT_FE	0x40

#define LEON_MCFG2_SRAMDIS		0x00002000
#define LEON_MCFG2_SDRAMEN		0x00004000
#define LEON_MCFG2_SRAMBANKSZ		0x00001e00	/* [12-9] */
#define LEON_MCFG2_SRAMBANKSZ_SHIFT	9
#define LEON_MCFG2_SDRAMBANKSZ		0x03800000	/* [25-23] */
#define LEON_MCFG2_SDRAMBANKSZ_SHIFT	23

#define LEON_TCNT0_MASK	0x7fffff

#define LEON_USTAT_ERROR (LEON_USTAT_OV | LEON_USTAT_PE | LEON_USTAT_FE)
/* no break yet */

#define ASI_LEON3_SYSCTRL		0x02
#define ASI_LEON3_SYSCTRL_ICFG		0x08
#define ASI_LEON3_SYSCTRL_DCFG		0x0c
#define ASI_LEON3_SYSCTRL_CFG_SNOOPING (1 << 27)
#define ASI_LEON3_SYSCTRL_CFG_SSIZE(c) (1 << ((c >> 20) & 0xf))

#ifndef __ASSEMBLY__

/* do a virtual address read without cache */
static inline unsigned long leon_readnobuffer_reg(unsigned long paddr)
{
	unsigned long retval;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r"(retval) : "r"(paddr), "i"(ASI_LEON_NOCACHE));
	return retval;
}

/* do a physical address bypass write, i.e. for 0x80000000 */
static inline void leon_store_reg(unsigned long paddr, unsigned long value)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : : "r"(value), "r"(paddr),
			     "i"(ASI_LEON_BYPASS) : "memory");
}

/* do a physical address bypass load, i.e. for 0x80000000 */
static inline unsigned long leon_load_reg(unsigned long paddr)
{
	unsigned long retval;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r"(retval) : "r"(paddr), "i"(ASI_LEON_BYPASS));
	return retval;
}

static inline void leon_srmmu_disabletlb(void)
{
	unsigned int retval;
	__asm__ __volatile__("lda [%%g0] %2, %0\n\t" : "=r"(retval) : "r"(0),
			     "i"(ASI_LEON_MMUREGS));
	retval |= LEON_CNR_CTRL_TLBDIS;
	__asm__ __volatile__("sta %0, [%%g0] %2\n\t" : : "r"(retval), "r"(0),
			     "i"(ASI_LEON_MMUREGS) : "memory");
}

static inline void leon_srmmu_enabletlb(void)
{
	unsigned int retval;
	__asm__ __volatile__("lda [%%g0] %2, %0\n\t" : "=r"(retval) : "r"(0),
			     "i"(ASI_LEON_MMUREGS));
	retval = retval & ~LEON_CNR_CTRL_TLBDIS;
	__asm__ __volatile__("sta %0, [%%g0] %2\n\t" : : "r"(retval), "r"(0),
			     "i"(ASI_LEON_MMUREGS) : "memory");
}

/* macro access for leon_load_reg() and leon_store_reg() */
#define LEON3_BYPASS_LOAD_PA(x)	    (leon_load_reg((unsigned long)(x)))
#define LEON3_BYPASS_STORE_PA(x, v) (leon_store_reg((unsigned long)(x), (unsigned long)(v)))
#define LEON3_BYPASS_ANDIN_PA(x, v) LEON3_BYPASS_STORE_PA(x, LEON3_BYPASS_LOAD_PA(x) & v)
#define LEON3_BYPASS_ORIN_PA(x, v)  LEON3_BYPASS_STORE_PA(x, LEON3_BYPASS_LOAD_PA(x) | v)
#define LEON_BYPASS_LOAD_PA(x)      leon_load_reg((unsigned long)(x))
#define LEON_BYPASS_STORE_PA(x, v)  leon_store_reg((unsigned long)(x), (unsigned long)(v))
#define LEON_REGLOAD_PA(x)          leon_load_reg((unsigned long)(x)+LEON_PREGS)
#define LEON_REGSTORE_PA(x, v)      leon_store_reg((unsigned long)(x)+LEON_PREGS, (unsigned long)(v))
#define LEON_REGSTORE_OR_PA(x, v)   LEON_REGSTORE_PA(x, LEON_REGLOAD_PA(x) | (unsigned long)(v))
#define LEON_REGSTORE_AND_PA(x, v)  LEON_REGSTORE_PA(x, LEON_REGLOAD_PA(x) & (unsigned long)(v))

/* macro access for leon_readnobuffer_reg() */
#define LEON_BYPASSCACHE_LOAD_VA(x) leon_readnobuffer_reg((unsigned long)(x))

extern void sparc_leon_eirq_register(int eirq);
extern void leon_init(void);
extern void leon_switch_mm(void);
extern void leon_init_IRQ(void);

extern unsigned long last_valid_pfn;

static inline unsigned long sparc_leon3_get_dcachecfg(void)
{
	unsigned int retval;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r"(retval) :
			     "r"(ASI_LEON3_SYSCTRL_DCFG),
			     "i"(ASI_LEON3_SYSCTRL));
	return retval;
}

/* enable snooping */
static inline void sparc_leon3_enable_snooping(void)
{
	__asm__ __volatile__ ("lda [%%g0] 2, %%l1\n\t"
			  "set 0x800000, %%l2\n\t"
			  "or  %%l2, %%l1, %%l2\n\t"
			  "sta %%l2, [%%g0] 2\n\t" : : : "l1", "l2");
};

static inline int sparc_leon3_snooping_enabled(void)
{
	u32 cctrl;
	__asm__ __volatile__("lda [%%g0] 2, %0\n\t" : "=r"(cctrl));
        return (cctrl >> 23) & 1;
};

static inline void sparc_leon3_disable_cache(void)
{
	__asm__ __volatile__ ("lda [%%g0] 2, %%l1\n\t"
			  "set 0x00000f, %%l2\n\t"
			  "andn  %%l2, %%l1, %%l2\n\t"
			  "sta %%l2, [%%g0] 2\n\t" : : : "l1", "l2");
};

static inline unsigned long sparc_leon3_asr17(void)
{
	u32 asr17;
	__asm__ __volatile__ ("rd %%asr17, %0\n\t" : "=r"(asr17));
	return asr17;
};

static inline int sparc_leon3_cpuid(void)
{
	return sparc_leon3_asr17() >> 28;
}

#endif /*!__ASSEMBLY__*/

#ifdef CONFIG_SMP
# define LEON3_IRQ_RESCHEDULE		13
# define LEON3_IRQ_TICKER		(leon_percpu_timer_dev[0].irq)
# define LEON3_IRQ_CROSS_CALL		15
#endif

#if defined(PAGE_SIZE_LEON_8K)
#define LEON_PAGE_SIZE_LEON 1
#elif defined(PAGE_SIZE_LEON_16K)
#define LEON_PAGE_SIZE_LEON 2)
#else
#define LEON_PAGE_SIZE_LEON 0
#endif

#if LEON_PAGE_SIZE_LEON == 0
/* [ 8, 6, 6 ] + 12 */
#define LEON_PGD_SH    24
#define LEON_PGD_M     0xff
#define LEON_PMD_SH    18
#define LEON_PMD_SH_V  (LEON_PGD_SH-2)
#define LEON_PMD_M     0x3f
#define LEON_PTE_SH    12
#define LEON_PTE_M     0x3f
#elif LEON_PAGE_SIZE_LEON == 1
/* [ 7, 6, 6 ] + 13 */
#define LEON_PGD_SH    25
#define LEON_PGD_M     0x7f
#define LEON_PMD_SH    19
#define LEON_PMD_SH_V  (LEON_PGD_SH-1)
#define LEON_PMD_M     0x3f
#define LEON_PTE_SH    13
#define LEON_PTE_M     0x3f
#elif LEON_PAGE_SIZE_LEON == 2
/* [ 6, 6, 6 ] + 14 */
#define LEON_PGD_SH    26
#define LEON_PGD_M     0x3f
#define LEON_PMD_SH    20
#define LEON_PMD_SH_V  (LEON_PGD_SH-0)
#define LEON_PMD_M     0x3f
#define LEON_PTE_SH    14
#define LEON_PTE_M     0x3f
#elif LEON_PAGE_SIZE_LEON == 3
/* [ 4, 7, 6 ] + 15 */
#define LEON_PGD_SH    28
#define LEON_PGD_M     0x0f
#define LEON_PMD_SH    21
#define LEON_PMD_SH_V  (LEON_PGD_SH-0)
#define LEON_PMD_M     0x7f
#define LEON_PTE_SH    15
#define LEON_PTE_M     0x3f
#else
#error cannot determine LEON_PAGE_SIZE_LEON
#endif

#define PAGE_MIN_SHIFT   (12)
#define PAGE_MIN_SIZE    (1UL << PAGE_MIN_SHIFT)

#define LEON3_XCCR_SETS_MASK  0x07000000UL
#define LEON3_XCCR_SSIZE_MASK 0x00f00000UL

#define LEON2_CCR_DSETS_MASK 0x03000000UL
#define LEON2_CFG_SSIZE_MASK 0x00007000UL

#ifndef __ASSEMBLY__
extern unsigned long srmmu_swprobe(unsigned long vaddr, unsigned long *paddr);
extern void leon_flush_icache_all(void);
extern void leon_flush_dcache_all(void);
extern void leon_flush_cache_all(void);
extern void leon_flush_tlb_all(void);
extern int leon_flush_during_switch;
extern int leon_flush_needed(void);

struct vm_area_struct;
extern void leon_flush_icache_all(void);
extern void leon_flush_dcache_all(void);
extern void leon_flush_pcache_all(struct vm_area_struct *vma, unsigned long page);
extern void leon_flush_cache_all(void);
extern void leon_flush_tlb_all(void);
extern int leon_flush_during_switch;
extern int leon_flush_needed(void);
extern void leon_flush_pcache_all(struct vm_area_struct *vma, unsigned long page);

/* struct that hold LEON3 cache configuration registers */
struct leon3_cacheregs {
	unsigned long ccr;	/* 0x00 - Cache Control Register  */
	unsigned long iccr;     /* 0x08 - Instruction Cache Configuration Register */
	unsigned long dccr;	/* 0x0c - Data Cache Configuration Register */
};

/* struct that hold LEON2 cache configuration register
 * & configuration register
 */
struct leon2_cacheregs {
	unsigned long ccr, cfg;
};

#ifdef __KERNEL__

#include <linux/interrupt.h>

struct device_node;
extern int sparc_leon_eirq_get(int eirq, int cpu);
extern irqreturn_t sparc_leon_eirq_isr(int dummy, void *dev_id);
extern void sparc_leon_eirq_register(int eirq);
extern void leon_clear_clock_irq(void);
extern void leon_load_profile_irq(int cpu, unsigned int limit);
extern void leon_init_timers(irq_handler_t counter_fn);
extern void leon_clear_clock_irq(void);
extern void leon_load_profile_irq(int cpu, unsigned int limit);
extern void leon_trans_init(struct device_node *dp);
extern void leon_node_init(struct device_node *dp, struct device_node ***nextp);
extern void leon_init_IRQ(void);
extern void leon_init(void);
extern unsigned long srmmu_swprobe(unsigned long vaddr, unsigned long *paddr);
extern void init_leon(void);
extern void poke_leonsparc(void);
extern void leon3_getCacheRegs(struct leon3_cacheregs *regs);
extern int leon_flush_needed(void);
extern void leon_switch_mm(void);
extern int srmmu_swprobe_trace;

#ifdef CONFIG_SMP
extern int leon_smp_nrcpus(void);
extern void leon_clear_profile_irq(int cpu);
extern void leon_smp_done(void);
extern void leon_boot_cpus(void);
extern int leon_boot_one_cpu(int i);
void leon_init_smp(void);
extern void cpu_probe(void);
extern void cpu_idle(void);
extern void init_IRQ(void);
extern void cpu_panic(void);
extern int __leon_processor_id(void);
void leon_enable_irq_cpu(unsigned int irq_nr, unsigned int cpu);

extern unsigned int real_irq_entry[], smpleon_ticker[];
extern unsigned int patchme_maybe_smp_msg[];
extern unsigned int t_nmi[], linux_trap_ipi15_leon[];
extern unsigned int linux_trap_ipi15_sun4m[];

#endif /* CONFIG_SMP */

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

/* macros used in leon_mm.c */
#define PFN(x)           ((x) >> PAGE_SHIFT)
#define _pfn_valid(pfn)	 ((pfn < last_valid_pfn) && (pfn >= PFN(phys_base)))
#define _SRMMU_PTE_PMASK_LEON 0xffffffff

#else /* defined(CONFIG_SPARC_LEON) */

/* nop definitions for !LEON case */
#define leon_init() do {} while (0)
#define leon_switch_mm() do {} while (0)
#define leon_init_IRQ() do {} while (0)
#define init_leon() do {} while (0)
#define leon_smp_done() do {} while (0)
#define leon_boot_cpus() do {} while (0)
#define leon_boot_one_cpu(i) 1
#define leon_init_smp() do {} while (0)

#endif /* !defined(CONFIG_SPARC_LEON) */

#endif
