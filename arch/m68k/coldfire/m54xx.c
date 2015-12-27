/***************************************************************************/

/*
 *	m54xx.c  -- platform support for ColdFire 54xx based boards
 *
 *	Copyright (C) 2010, Philippe De Muyter <phdm@macqel.be>
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <linux/bootmem.h>
#include <asm/pgalloc.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/m54xxsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfclk.h>
#include <asm/m54xxgpt.h>
#ifdef CONFIG_MMU
#include <asm/mmu_context.h>
#include <linux/pfn.h>
#endif

/***************************************************************************/

DEFINE_CLK(pll, "pll.0", MCF_CLK);
DEFINE_CLK(sys, "sys.0", MCF_BUSCLK);
DEFINE_CLK(mcfslt0, "mcfslt.0", MCF_BUSCLK);
DEFINE_CLK(mcfslt1, "mcfslt.1", MCF_BUSCLK);
DEFINE_CLK(mcfuart0, "mcfuart.0", MCF_BUSCLK);
DEFINE_CLK(mcfuart1, "mcfuart.1", MCF_BUSCLK);
DEFINE_CLK(mcfuart2, "mcfuart.2", MCF_BUSCLK);
DEFINE_CLK(mcfuart3, "mcfuart.3", MCF_BUSCLK);

struct clk *mcf_clks[] = {
	&clk_pll,
	&clk_sys,
	&clk_mcfslt0,
	&clk_mcfslt1,
	&clk_mcfuart0,
	&clk_mcfuart1,
	&clk_mcfuart2,
	&clk_mcfuart3,
	NULL
};

/***************************************************************************/

static void __init m54xx_uarts_init(void)
{
	/* enable io pins */
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD, MCFGPIO_PAR_PSC0);
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD | MCF_PAR_PSC_RTS_RTS,
		MCFGPIO_PAR_PSC1);
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD | MCF_PAR_PSC_RTS_RTS |
		MCF_PAR_PSC_CTS_CTS, MCFGPIO_PAR_PSC2);
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD, MCFGPIO_PAR_PSC3);
}

/***************************************************************************/

static void mcf54xx_reset(void)
{
	/* disable interrupts and enable the watchdog */
	asm("movew #0x2700, %sr\n");
	__raw_writel(0, MCF_GPT_GMS0);
	__raw_writel(MCF_GPT_GCIR_CNT(1), MCF_GPT_GCIR0);
	__raw_writel(MCF_GPT_GMS_WDEN | MCF_GPT_GMS_CE | MCF_GPT_GMS_TMS(4),
		MCF_GPT_GMS0);
}

/***************************************************************************/

#ifdef CONFIG_MMU

unsigned long num_pages;

static void __init mcf54xx_bootmem_alloc(void)
{
	unsigned long start_pfn;
	unsigned long memstart;

	/* _rambase and _ramend will be naturally page aligned */
	m68k_memory[0].addr = _rambase;
	m68k_memory[0].size = _ramend - _rambase;

	/* compute total pages in system */
	num_pages = PFN_DOWN(_ramend - _rambase);

	/* page numbers */
	memstart = PAGE_ALIGN(_ramstart);
	min_low_pfn = PFN_DOWN(_rambase);
	start_pfn = PFN_DOWN(memstart);
	max_pfn = max_low_pfn = PFN_DOWN(_ramend);
	high_memory = (void *)_ramend;

	m68k_virt_to_node_shift = fls(_ramend - _rambase - 1) - 6;
	module_fixup(NULL, __start_fixup, __stop_fixup);

	/* setup bootmem data */
	m68k_setup_node(0);
	memstart += init_bootmem_node(NODE_DATA(0), start_pfn,
		min_low_pfn, max_low_pfn);
	free_bootmem_node(NODE_DATA(0), memstart, _ramend - memstart);
}

#endif /* CONFIG_MMU */

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
#ifdef CONFIG_MMU
	mcf54xx_bootmem_alloc();
	mmu_context_init();
#endif
	mach_reset = mcf54xx_reset;
	mach_sched_init = hw_timer_init;
	m54xx_uarts_init();
}

/***************************************************************************/
