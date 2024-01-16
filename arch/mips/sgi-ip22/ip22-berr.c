// SPDX-License-Identifier: GPL-2.0
/*
 * ip22-berr.c: Bus error handling.
 *
 * Copyright (C) 2002, 2003 Ladislav Michl (ladis@linux-mips.org)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>

#include <asm/addrspace.h>
#include <asm/traps.h>
#include <asm/branch.h>
#include <asm/irq_regs.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/ip22.h>


static unsigned int cpu_err_stat;	/* Status reg for CPU */
static unsigned int gio_err_stat;	/* Status reg for GIO */
static unsigned int cpu_err_addr;	/* Error address reg for CPU */
static unsigned int gio_err_addr;	/* Error address reg for GIO */
static unsigned int extio_stat;
static unsigned int hpc3_berr_stat;	/* Bus error interrupt status */

static void save_and_clear_buserr(void)
{
	/* save status registers */
	cpu_err_addr = sgimc->cerr;
	cpu_err_stat = sgimc->cstat;
	gio_err_addr = sgimc->gerr;
	gio_err_stat = sgimc->gstat;
	extio_stat = ip22_is_fullhouse() ? sgioc->extio : (sgint->errstat << 4);
	hpc3_berr_stat = hpc3c0->bestat;

	sgimc->cstat = sgimc->gstat = 0;
}

#define GIO_ERRMASK	0xff00
#define CPU_ERRMASK	0x3f00

static void print_buserr(void)
{
	if (extio_stat & EXTIO_MC_BUSERR)
		printk(KERN_ERR "MC Bus Error\n");
	if (extio_stat & EXTIO_HPC3_BUSERR)
		printk(KERN_ERR "HPC3 Bus Error 0x%x:<id=0x%x,%s,lane=0x%x>\n",
			hpc3_berr_stat,
			(hpc3_berr_stat & HPC3_BESTAT_PIDMASK) >>
					  HPC3_BESTAT_PIDSHIFT,
			(hpc3_berr_stat & HPC3_BESTAT_CTYPE) ? "PIO" : "DMA",
			hpc3_berr_stat & HPC3_BESTAT_BLMASK);
	if (extio_stat & EXTIO_EISA_BUSERR)
		printk(KERN_ERR "EISA Bus Error\n");
	if (cpu_err_stat & CPU_ERRMASK)
		printk(KERN_ERR "CPU error 0x%x<%s%s%s%s%s%s> @ 0x%08x\n",
			cpu_err_stat,
			cpu_err_stat & SGIMC_CSTAT_RD ? "RD " : "",
			cpu_err_stat & SGIMC_CSTAT_PAR ? "PAR " : "",
			cpu_err_stat & SGIMC_CSTAT_ADDR ? "ADDR " : "",
			cpu_err_stat & SGIMC_CSTAT_SYSAD_PAR ? "SYSAD " : "",
			cpu_err_stat & SGIMC_CSTAT_SYSCMD_PAR ? "SYSCMD " : "",
			cpu_err_stat & SGIMC_CSTAT_BAD_DATA ? "BAD_DATA " : "",
			cpu_err_addr);
	if (gio_err_stat & GIO_ERRMASK)
		printk(KERN_ERR "GIO error 0x%x:<%s%s%s%s%s%s%s%s> @ 0x%08x\n",
			gio_err_stat,
			gio_err_stat & SGIMC_GSTAT_RD ? "RD " : "",
			gio_err_stat & SGIMC_GSTAT_WR ? "WR " : "",
			gio_err_stat & SGIMC_GSTAT_TIME ? "TIME " : "",
			gio_err_stat & SGIMC_GSTAT_PROM ? "PROM " : "",
			gio_err_stat & SGIMC_GSTAT_ADDR ? "ADDR " : "",
			gio_err_stat & SGIMC_GSTAT_BC ? "BC " : "",
			gio_err_stat & SGIMC_GSTAT_PIO_RD ? "PIO_RD " : "",
			gio_err_stat & SGIMC_GSTAT_PIO_WR ? "PIO_WR " : "",
			gio_err_addr);
}

/*
 * MC sends an interrupt whenever bus or parity errors occur. In addition,
 * if the error happened during a CPU read, it also asserts the bus error
 * pin on the R4K. Code in bus error handler save the MC bus error registers
 * and then clear the interrupt when this happens.
 */

void ip22_be_interrupt(int irq)
{
	const int field = 2 * sizeof(unsigned long);
	struct pt_regs *regs = get_irq_regs();

	save_and_clear_buserr();
	print_buserr();
	printk(KERN_ALERT "%s bus error, epc == %0*lx, ra == %0*lx\n",
	       (regs->cp0_cause & 4) ? "Data" : "Instruction",
	       field, regs->cp0_epc, field, regs->regs[31]);
	/* Assume it would be too dangerous to continue ... */
	die_if_kernel("Oops", regs);
	force_sig(SIGBUS);
}

static int ip22_be_handler(struct pt_regs *regs, int is_fixup)
{
	save_and_clear_buserr();
	if (is_fixup)
		return MIPS_BE_FIXUP;
	print_buserr();
	return MIPS_BE_FATAL;
}

void __init ip22_be_init(void)
{
	mips_set_be_handler(ip22_be_handler);
}
