/**
 * @file op_model_xscale.c
 * XScale Performance Monitor Driver
 *
 * @remark Copyright 2000-2004 Deepak Saxena <dsaxena@mvista.com>
 * @remark Copyright 2000-2004 MontaVista Software Inc
 * @remark Copyright 2004 Dave Jiang <dave.jiang@intel.com>
 * @remark Copyright 2004 Intel Corporation
 * @remark Copyright 2004 Zwane Mwaikambo <zwane@arm.linux.org.uk>
 * @remark Copyright 2004 OProfile Authors
 *
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 */

/* #define DEBUG */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "op_counter.h"
#include "op_arm_model.h"

#define	PMU_ENABLE	0x001	/* Enable counters */
#define PMN_RESET	0x002	/* Reset event counters */
#define	CCNT_RESET	0x004	/* Reset clock counter */
#define	PMU_RESET	(CCNT_RESET | PMN_RESET)
#define PMU_CNT64	0x008	/* Make CCNT count every 64th cycle */

/* TODO do runtime detection */
#ifdef CONFIG_ARCH_IOP310
#define XSCALE_PMU_IRQ  IRQ_XS80200_PMU
#endif
#ifdef CONFIG_ARCH_IOP321
#define XSCALE_PMU_IRQ  IRQ_IOP321_CORE_PMU
#endif
#ifdef CONFIG_ARCH_IOP331
#define XSCALE_PMU_IRQ  IRQ_IOP331_CORE_PMU
#endif
#ifdef CONFIG_ARCH_PXA
#define XSCALE_PMU_IRQ  IRQ_PMU
#endif

/*
 * Different types of events that can be counted by the XScale PMU
 * as used by Oprofile userspace. Here primarily for documentation
 * purposes.
 */

#define EVT_ICACHE_MISS			0x00
#define	EVT_ICACHE_NO_DELIVER		0x01
#define	EVT_DATA_STALL			0x02
#define	EVT_ITLB_MISS			0x03
#define	EVT_DTLB_MISS			0x04
#define	EVT_BRANCH			0x05
#define	EVT_BRANCH_MISS			0x06
#define	EVT_INSTRUCTION			0x07
#define	EVT_DCACHE_FULL_STALL		0x08
#define	EVT_DCACHE_FULL_STALL_CONTIG	0x09
#define	EVT_DCACHE_ACCESS		0x0A
#define	EVT_DCACHE_MISS			0x0B
#define	EVT_DCACE_WRITE_BACK		0x0C
#define	EVT_PC_CHANGED			0x0D
#define	EVT_BCU_REQUEST			0x10
#define	EVT_BCU_FULL			0x11
#define	EVT_BCU_DRAIN			0x12
#define	EVT_BCU_ECC_NO_ELOG		0x14
#define	EVT_BCU_1_BIT_ERR		0x15
#define	EVT_RMW				0x16
/* EVT_CCNT is not hardware defined */
#define EVT_CCNT			0xFE
#define EVT_UNUSED			0xFF

struct pmu_counter {
	volatile unsigned long ovf;
	unsigned long reset_counter;
};

enum { CCNT, PMN0, PMN1, PMN2, PMN3, MAX_COUNTERS };

static struct pmu_counter results[MAX_COUNTERS];

/*
 * There are two versions of the PMU in current XScale processors
 * with differing register layouts and number of performance counters.
 * e.g. IOP321 is xsc1 whilst IOP331 is xsc2.
 * We detect which register layout to use in xscale_detect_pmu()
 */
enum { PMU_XSC1, PMU_XSC2 };

struct pmu_type {
	int id;
	char *name;
	int num_counters;
	unsigned int int_enable;
	unsigned int cnt_ovf[MAX_COUNTERS];
	unsigned int int_mask[MAX_COUNTERS];
};

static struct pmu_type pmu_parms[] = {
	{
		.id		= PMU_XSC1,
		.name		= "arm/xscale1",
		.num_counters	= 3,
		.int_mask	= { [PMN0] = 0x10, [PMN1] = 0x20,
				    [CCNT] = 0x40 },
		.cnt_ovf	= { [CCNT] = 0x400, [PMN0] = 0x100,
				    [PMN1] = 0x200},
	},
	{
		.id		= PMU_XSC2,
		.name		= "arm/xscale2",
		.num_counters	= 5,
		.int_mask	= { [CCNT] = 0x01, [PMN0] = 0x02,
				    [PMN1] = 0x04, [PMN2] = 0x08,
				    [PMN3] = 0x10 },
		.cnt_ovf	= { [CCNT] = 0x01, [PMN0] = 0x02,
				    [PMN1] = 0x04, [PMN2] = 0x08,
				    [PMN3] = 0x10 },
	},
};

static struct pmu_type *pmu;

static void write_pmnc(u32 val)
{
	if (pmu->id == PMU_XSC1) {
		/* upper 4bits and 7, 11 are write-as-0 */
		val &= 0xffff77f;
		__asm__ __volatile__ ("mcr p14, 0, %0, c0, c0, 0" : : "r" (val));
	} else {
		/* bits 4-23 are write-as-0, 24-31 are write ignored */
		val &= 0xf;
		__asm__ __volatile__ ("mcr p14, 0, %0, c0, c1, 0" : : "r" (val));
	}
}

static u32 read_pmnc(void)
{
	u32 val;

	if (pmu->id == PMU_XSC1)
		__asm__ __volatile__ ("mrc p14, 0, %0, c0, c0, 0" : "=r" (val));
	else {
		__asm__ __volatile__ ("mrc p14, 0, %0, c0, c1, 0" : "=r" (val));
		/* bits 1-2 and 4-23 are read-unpredictable */
		val &= 0xff000009;
	}

	return val;
}

static u32 __xsc1_read_counter(int counter)
{
	u32 val = 0;

	switch (counter) {
	case CCNT:
		__asm__ __volatile__ ("mrc p14, 0, %0, c1, c0, 0" : "=r" (val));
		break;
	case PMN0:
		__asm__ __volatile__ ("mrc p14, 0, %0, c2, c0, 0" : "=r" (val));
		break;
	case PMN1:
		__asm__ __volatile__ ("mrc p14, 0, %0, c3, c0, 0" : "=r" (val));
		break;
	}
	return val;
}

static u32 __xsc2_read_counter(int counter)
{
	u32 val = 0;

	switch (counter) {
	case CCNT:
		__asm__ __volatile__ ("mrc p14, 0, %0, c1, c1, 0" : "=r" (val));
		break;
	case PMN0:
		__asm__ __volatile__ ("mrc p14, 0, %0, c0, c2, 0" : "=r" (val));
		break;
	case PMN1:
		__asm__ __volatile__ ("mrc p14, 0, %0, c1, c2, 0" : "=r" (val));
		break;
	case PMN2:
		__asm__ __volatile__ ("mrc p14, 0, %0, c2, c2, 0" : "=r" (val));
		break;
	case PMN3:
		__asm__ __volatile__ ("mrc p14, 0, %0, c3, c2, 0" : "=r" (val));
		break;
	}
	return val;
}

static u32 read_counter(int counter)
{
	u32 val;

	if (pmu->id == PMU_XSC1)
		val = __xsc1_read_counter(counter);
	else
		val = __xsc2_read_counter(counter);

	return val;
}

static void __xsc1_write_counter(int counter, u32 val)
{
	switch (counter) {
	case CCNT:
		__asm__ __volatile__ ("mcr p14, 0, %0, c1, c0, 0" : : "r" (val));
		break;
	case PMN0:
		__asm__ __volatile__ ("mcr p14, 0, %0, c2, c0, 0" : : "r" (val));
		break;
	case PMN1:
		__asm__ __volatile__ ("mcr p14, 0, %0, c3, c0, 0" : : "r" (val));
		break;
	}
}

static void __xsc2_write_counter(int counter, u32 val)
{
	switch (counter) {
	case CCNT:
		__asm__ __volatile__ ("mcr p14, 0, %0, c1, c1, 0" : : "r" (val));
		break;
	case PMN0:
		__asm__ __volatile__ ("mcr p14, 0, %0, c0, c2, 0" : : "r" (val));
		break;
	case PMN1:
		__asm__ __volatile__ ("mcr p14, 0, %0, c1, c2, 0" : : "r" (val));
		break;
	case PMN2:
		__asm__ __volatile__ ("mcr p14, 0, %0, c2, c2, 0" : : "r" (val));
		break;
	case PMN3:
		__asm__ __volatile__ ("mcr p14, 0, %0, c3, c2, 0" : : "r" (val));
		break;
	}
}

static void write_counter(int counter, u32 val)
{
	if (pmu->id == PMU_XSC1)
		__xsc1_write_counter(counter, val);
	else
		__xsc2_write_counter(counter, val);
}

static int xscale_setup_ctrs(void)
{
	u32 evtsel, pmnc;
	int i;

	for (i = CCNT; i < MAX_COUNTERS; i++) {
		if (counter_config[i].enabled)
			continue;

		counter_config[i].event = EVT_UNUSED;
	}

	switch (pmu->id) {
	case PMU_XSC1:
		pmnc = (counter_config[PMN1].event << 20) | (counter_config[PMN0].event << 12);
		pr_debug("xscale_setup_ctrs: pmnc: %#08x\n", pmnc);
		write_pmnc(pmnc);
		break;

	case PMU_XSC2:
		evtsel = counter_config[PMN0].event | (counter_config[PMN1].event << 8) |
			(counter_config[PMN2].event << 16) | (counter_config[PMN3].event << 24);

		pr_debug("xscale_setup_ctrs: evtsel %#08x\n", evtsel);
		__asm__ __volatile__ ("mcr p14, 0, %0, c8, c1, 0" : : "r" (evtsel));
		break;
	}

	for (i = CCNT; i < MAX_COUNTERS; i++) {
		if (counter_config[i].event == EVT_UNUSED) {
			counter_config[i].event = 0;
			pmu->int_enable &= ~pmu->int_mask[i];
			continue;
		}

		results[i].reset_counter = counter_config[i].count;
		write_counter(i, -(u32)counter_config[i].count);
		pmu->int_enable |= pmu->int_mask[i];
		pr_debug("xscale_setup_ctrs: counter%d %#08x from %#08lx\n", i,
			read_counter(i), counter_config[i].count);
	}

	return 0;
}

static void inline __xsc1_check_ctrs(void)
{
	int i;
	u32 pmnc = read_pmnc();

	/* NOTE: there's an A stepping errata that states if an overflow */
	/*       bit already exists and another occurs, the previous     */
	/*       Overflow bit gets cleared. There's no workaround.	 */
	/*	 Fixed in B stepping or later			 	 */

	/* Write the value back to clear the overflow flags. Overflow */
	/* flags remain in pmnc for use below */
	write_pmnc(pmnc & ~PMU_ENABLE);

	for (i = CCNT; i <= PMN1; i++) {
		if (!(pmu->int_mask[i] & pmu->int_enable))
			continue;

		if (pmnc & pmu->cnt_ovf[i])
			results[i].ovf++;
	}
}

static void inline __xsc2_check_ctrs(void)
{
	int i;
	u32 flag = 0, pmnc = read_pmnc();

	pmnc &= ~PMU_ENABLE;
	write_pmnc(pmnc);

	/* read overflow flag register */
	__asm__ __volatile__ ("mrc p14, 0, %0, c5, c1, 0" : "=r" (flag));

	for (i = CCNT; i <= PMN3; i++) {
		if (!(pmu->int_mask[i] & pmu->int_enable))
			continue;

		if (flag & pmu->cnt_ovf[i])
			results[i].ovf++;
	}

	/* writeback clears overflow bits */
	__asm__ __volatile__ ("mcr p14, 0, %0, c5, c1, 0" : : "r" (flag));
}

static irqreturn_t xscale_pmu_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	int i;
	u32 pmnc;

	if (pmu->id == PMU_XSC1)
		__xsc1_check_ctrs();
	else
		__xsc2_check_ctrs();

	for (i = CCNT; i < MAX_COUNTERS; i++) {
		if (!results[i].ovf)
			continue;

		write_counter(i, -(u32)results[i].reset_counter);
		oprofile_add_sample(regs, i);
		results[i].ovf--;
	}

	pmnc = read_pmnc() | PMU_ENABLE;
	write_pmnc(pmnc);

	return IRQ_HANDLED;
}

static void xscale_pmu_stop(void)
{
	u32 pmnc = read_pmnc();

	pmnc &= ~PMU_ENABLE;
	write_pmnc(pmnc);

	free_irq(XSCALE_PMU_IRQ, results);
}

static int xscale_pmu_start(void)
{
	int ret;
	u32 pmnc = read_pmnc();

	ret = request_irq(XSCALE_PMU_IRQ, xscale_pmu_interrupt, SA_INTERRUPT,
			"XScale PMU", (void *)results);

	if (ret < 0) {
		printk(KERN_ERR "oprofile: unable to request IRQ%d for XScale PMU\n",
			XSCALE_PMU_IRQ);
		return ret;
	}

	if (pmu->id == PMU_XSC1)
		pmnc |= pmu->int_enable;
	else {
		__asm__ __volatile__ ("mcr p14, 0, %0, c4, c1, 0" : : "r" (pmu->int_enable));
		pmnc &= ~PMU_CNT64;
	}

	pmnc |= PMU_ENABLE;
	write_pmnc(pmnc);
	pr_debug("xscale_pmu_start: pmnc: %#08x mask: %08x\n", pmnc, pmu->int_enable);
	return 0;
}

static int xscale_detect_pmu(void)
{
	int ret = 0;
	u32 id;

	id = (read_cpuid(CPUID_ID) >> 13) & 0x7;

	switch (id) {
	case 1:
		pmu = &pmu_parms[PMU_XSC1];
		break;
	case 2:
		pmu = &pmu_parms[PMU_XSC2];
		break;
	default:
		ret = -ENODEV;
		break;
	}

	if (!ret) {
		op_xscale_spec.name = pmu->name;
		op_xscale_spec.num_counters = pmu->num_counters;
		pr_debug("xscale_detect_pmu: detected %s PMU\n", pmu->name);
	}

	return ret;
}

struct op_arm_model_spec op_xscale_spec = {
	.init		= xscale_detect_pmu,
	.setup_ctrs	= xscale_setup_ctrs,
	.start		= xscale_pmu_start,
	.stop		= xscale_pmu_stop,
};

