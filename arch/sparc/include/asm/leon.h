/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2004 Konrad Eisele (eiselekd@web.de,konrad@gaisler.com) Gaisler Research
 * Copyright (C) 2004 Stefan Holst (mail@s-holst.de) Uni-Stuttgart
 * Copyright (C) 2009 Daniel Hellstrom (daniel@gaisler.com) Aeroflex Gaisler AB
 * Copyright (C) 2009 Konrad Eisele (konrad@gaisler.com) Aeroflex Gaisler AB
 */

#ifndef LEON_H_INCLUDE
#define LEON_H_INCLUDE

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

/* irq masks */
#define LEON_HARD_INT(x)	(1 << (x))	/* irq 0-15 */
#define LEON_IRQMASK_R		0x0000fffe	/* bit 15- 1 of lregs.irqmask */
#define LEON_IRQPRIO_R		0xfffe0000	/* bit 31-17 of lregs.irqmask */

#define LEON_MCFG2_SRAMDIS		0x00002000
#define LEON_MCFG2_SDRAMEN		0x00004000
#define LEON_MCFG2_SRAMBANKSZ		0x00001e00	/* [12-9] */
#define LEON_MCFG2_SRAMBANKSZ_SHIFT	9
#define LEON_MCFG2_SDRAMBANKSZ		0x03800000	/* [25-23] */
#define LEON_MCFG2_SDRAMBANKSZ_SHIFT	23

#define LEON_TCNT0_MASK	0x7fffff


#define ASI_LEON3_SYSCTRL		0x02
#define ASI_LEON3_SYSCTRL_ICFG		0x08
#define ASI_LEON3_SYSCTRL_DCFG		0x0c
#define ASI_LEON3_SYSCTRL_CFG_SNOOPING (1 << 27)
#define ASI_LEON3_SYSCTRL_CFG_SSIZE(c) (1 << ((c >> 20) & 0xf))

#ifndef __ASSEMBLY__

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

/* macro access for leon_load_reg() and leon_store_reg() */
#define LEON3_BYPASS_LOAD_PA(x)	    (leon_load_reg((unsigned long)(x)))
#define LEON3_BYPASS_STORE_PA(x, v) (leon_store_reg((unsigned long)(x), (unsigned long)(v)))
#define LEON_BYPASS_LOAD_PA(x)      leon_load_reg((unsigned long)(x))
#define LEON_BYPASS_STORE_PA(x, v)  leon_store_reg((unsigned long)(x), (unsigned long)(v))

void leon_switch_mm(void);
void leon_init_IRQ(void);

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
	return ((cctrl >> 23) & 1) && ((cctrl >> 17) & 1);
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
# define LEON3_IRQ_IPI_DEFAULT		13
# define LEON3_IRQ_TICKER		(leon3_gptimer_irq)
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

#define LEON3_XCCR_SETS_MASK  0x07000000UL
#define LEON3_XCCR_SSIZE_MASK 0x00f00000UL

#define LEON2_CCR_DSETS_MASK 0x03000000UL
#define LEON2_CFG_SSIZE_MASK 0x00007000UL

#ifndef __ASSEMBLY__
struct vm_area_struct;

unsigned long leon_swprobe(unsigned long vaddr, unsigned long *paddr);
void leon_flush_icache_all(void);
void leon_flush_dcache_all(void);
void leon_flush_cache_all(void);
void leon_flush_tlb_all(void);
extern int leon_flush_during_switch;
int leon_flush_needed(void);
void leon_flush_pcache_all(struct vm_area_struct *vma, unsigned long page);

/* struct that hold LEON3 cache configuration registers */
struct leon3_cacheregs {
	unsigned long ccr;	/* 0x00 - Cache Control Register  */
	unsigned long iccr;     /* 0x08 - Instruction Cache Configuration Register */
	unsigned long dccr;	/* 0x0c - Data Cache Configuration Register */
};

#include <linux/irq.h>
#include <linux/interrupt.h>

struct device_node;
struct task_struct;
unsigned int leon_build_device_irq(unsigned int real_irq,
				   irq_flow_handler_t flow_handler,
				   const char *name, int do_ack);
void leon_update_virq_handling(unsigned int virq,
			       irq_flow_handler_t flow_handler,
			       const char *name, int do_ack);
void leon_init_timers(void);
void leon_node_init(struct device_node *dp, struct device_node ***nextp);
void init_leon(void);
void poke_leonsparc(void);
void leon3_getCacheRegs(struct leon3_cacheregs *regs);
extern int leon3_ticker_irq;

#ifdef CONFIG_SMP
int leon_smp_nrcpus(void);
void leon_clear_profile_irq(int cpu);
void leon_smp_done(void);
void leon_boot_cpus(void);
int leon_boot_one_cpu(int i, struct task_struct *);
void leon_init_smp(void);
void leon_enable_irq_cpu(unsigned int irq_nr, unsigned int cpu);
irqreturn_t leon_percpu_timer_interrupt(int irq, void *unused);

extern unsigned int smpleon_ipi[];
extern unsigned int linux_trap_ipi15_leon[];
extern int leon_ipi_irq;

#endif /* CONFIG_SMP */

#endif /* __ASSEMBLY__ */

/* macros used in leon_mm.c */
#define PFN(x)           ((x) >> PAGE_SHIFT)
#define _pfn_valid(pfn)	 ((pfn < last_valid_pfn) && (pfn >= PFN(phys_base)))
#define _SRMMU_PTE_PMASK_LEON 0xffffffff

/*
 * On LEON PCI Memory space is mapped 1:1 with physical address space.
 *
 * I/O space is located at low 64Kbytes in PCI I/O space. The I/O addresses
 * are converted into CPU addresses to virtual addresses that are mapped with
 * MMU to the PCI Host PCI I/O space window which are translated to the low
 * 64Kbytes by the Host controller.
 */

#endif
