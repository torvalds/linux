/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5407/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2000, Lineo (www.lineo.com)
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

extern unsigned int mcf_timervector;
extern unsigned int mcf_profilevector;
extern unsigned int mcf_timerlevel;

/***************************************************************************/

static struct mcf_platform_uart m5407_uart_platform[] = {
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

static struct platform_device m5407_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m5407_uart_platform,
};

static struct platform_device *m5407_devices[] __initdata = {
	&m5407_uart,
};

/***************************************************************************/

static void __init m5407_uart_init_line(int line, int irq)
{
	if (line == 0) {
		writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI1, MCF_MBAR + MCFSIM_UART1ICR);
		writeb(irq, MCF_MBAR + MCFUART_BASE1 + MCFUART_UIVR);
		mcf_clrimr(MCFINTC_UART0);
	} else if (line == 1) {
		writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI2, MCF_MBAR + MCFSIM_UART2ICR);
		writeb(irq, MCF_MBAR + MCFUART_BASE2 + MCFUART_UIVR);
		mcf_clrimr(MCFINTC_UART1);
	}
}

static void __init m5407_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m5407_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m5407_uart_init_line(line, m5407_uart_platform[line].irq);
}

/***************************************************************************/

void mcf_settimericr(unsigned int timer, unsigned int level)
{
	volatile unsigned char *icrp;
	unsigned int icr, imr;

	if (timer <= 2) {
		switch (timer) {
		case 2:  icr = MCFSIM_TIMER2ICR; imr = MCFINTC_TIMER2; break;
		default: icr = MCFSIM_TIMER1ICR; imr = MCFINTC_TIMER1; break;
		}

		icrp = (volatile unsigned char *) (MCF_MBAR + icr);
		*icrp = MCFSIM_ICR_AUTOVEC | (level << 2) | MCFSIM_ICR_PRI3;
		mcf_clrimr(imr);
	}
}

/***************************************************************************/

void m5407_cpu_reset(void)
{
	local_irq_disable();
	/* set watchdog to soft reset, and enabled */
	__raw_writeb(0xc0, MCF_MBAR + MCFSIM_SYPCR);
	for (;;)
		/* wait for watchdog to timeout */;
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
#if defined(CONFIG_CLEOPATRA)
	/* Different timer setup - to prevent device clash */
	mcf_timervector = 30;
	mcf_profilevector = 31;
	mcf_timerlevel = 6;
#endif

	mach_reset = m5407_cpu_reset;
}

/***************************************************************************/

static int __init init_BSP(void)
{
	m5407_uarts_init();
	platform_add_devices(m5407_devices, ARRAY_SIZE(m5407_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
