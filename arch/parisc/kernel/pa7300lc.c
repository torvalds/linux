/*
 *   linux/arch/parisc/kernel/pa7300lc.c
 *	- PA7300LC-specific functions	
 *
 *   Copyright (C) 2000 Philipp Rumpf */

#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>

/* CPU register indices */

#define MIOC_STATUS	0xf040
#define MIOC_CONTROL	0xf080
#define MDERRADD	0xf0e0
#define DMAERR		0xf0e8
#define DIOERR		0xf0ec
#define HIDMAMEM	0xf0f4

/* this returns the HPA of the CPU it was called on */
static u32 cpu_hpa(void)
{
	return 0xfffb0000;
}

static void pa7300lc_lpmc(int code, struct pt_regs *regs)
{
	u32 hpa;
	printk(KERN_WARNING "LPMC on CPU %d\n", smp_processor_id());

	show_regs(regs);

	hpa = cpu_hpa();
	printk(KERN_WARNING
		"MIOC_CONTROL %08x\n" "MIOC_STATUS  %08x\n"
		"MDERRADD     %08x\n" "DMAERR       %08x\n"
		"DIOERR       %08x\n" "HIDMAMEM     %08x\n",
		gsc_readl(hpa+MIOC_CONTROL), gsc_readl(hpa+MIOC_STATUS),
		gsc_readl(hpa+MDERRADD), gsc_readl(hpa+DMAERR),
		gsc_readl(hpa+DIOERR), gsc_readl(hpa+HIDMAMEM));
}

void pa7300lc_init(void)
{
	cpu_lpmc = pa7300lc_lpmc;
}
