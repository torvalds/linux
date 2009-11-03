/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5249/config.c
 *
 *	Copyright (C) 2002, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>

/***************************************************************************/

static struct mcf_platform_uart m5249_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= 73,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE2,
		.irq		= 74,
	},
	{ },
};

static struct platform_device m5249_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m5249_uart_platform,
};

static struct platform_device *m5249_devices[] __initdata = {
	&m5249_uart,
};

/***************************************************************************/

static void __init m5249_uart_init_line(int line, int irq)
{
	if (line == 0) {
		writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI1, MCF_MBAR + MCFSIM_UART1ICR);
		writeb(irq, MCF_MBAR + MCFUART_BASE1 + MCFUART_UIVR);
		mcf_mapirq2imr(irq, MCFINTC_UART0);
	} else if (line == 1) {
		writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI2, MCF_MBAR + MCFSIM_UART2ICR);
		writeb(irq, MCF_MBAR + MCFUART_BASE2 + MCFUART_UIVR);
		mcf_mapirq2imr(irq, MCFINTC_UART1);
	}
}

static void __init m5249_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m5249_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m5249_uart_init_line(line, m5249_uart_platform[line].irq);
}

/***************************************************************************/

static void __init m5249_timers_init(void)
{
	/* Timer1 is always used as system timer */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI3,
		MCF_MBAR + MCFSIM_TIMER1ICR);
	mcf_mapirq2imr(MCF_IRQ_TIMER, MCFINTC_TIMER1);

#ifdef CONFIG_HIGHPROFILE
	/* Timer2 is to be used as a high speed profile timer  */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL7 | MCFSIM_ICR_PRI3,
		MCF_MBAR + MCFSIM_TIMER2ICR);
	mcf_mapirq2imr(MCF_IRQ_PROFILER, MCFINTC_TIMER2);
#endif
}

/***************************************************************************/

void m5249_cpu_reset(void)
{
	local_irq_disable();
	/* Set watchdog to soft reset, and enabled */
	__raw_writeb(0xc0, MCF_MBAR + MCFSIM_SYPCR);
	for (;;)
		/* wait for watchdog to timeout */;
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_reset = m5249_cpu_reset;
	m5249_timers_init();
	m5249_uarts_init();
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m5249_devices, ARRAY_SIZE(m5249_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
