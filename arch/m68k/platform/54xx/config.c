/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/54xx/config.c
 *
 *	Copyright (C) 2010, Philippe De Muyter <phdm@macqel.be>
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/m54xxsim.h>
#include <asm/mcfuart.h>
#include <asm/m54xxgpt.h>

/***************************************************************************/

static struct mcf_platform_uart m54xx_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= 64 + 35,
	},
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE2,
		.irq		= 64 + 34,
	},
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE3,
		.irq		= 64 + 33,
	},
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE4,
		.irq		= 64 + 32,
	},
};

static struct platform_device m54xx_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m54xx_uart_platform,
};

static struct platform_device *m54xx_devices[] __initdata = {
	&m54xx_uart,
};


/***************************************************************************/

static void __init m54xx_uart_init_line(int line, int irq)
{
	int rts_cts;

	/* enable io pins */
	switch (line) {
	case 0:
		rts_cts = 0; break;
	case 1:
		rts_cts = MCF_PAR_PSC_RTS_RTS; break;
	case 2:
		rts_cts = MCF_PAR_PSC_RTS_RTS | MCF_PAR_PSC_CTS_CTS; break;
	case 3:
		rts_cts = 0; break;
	}
	__raw_writeb(MCF_PAR_PSC_TXD | rts_cts | MCF_PAR_PSC_RXD,
						MCF_MBAR + MCF_PAR_PSC(line));
}

static void __init m54xx_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m54xx_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m54xx_uart_init_line(line, m54xx_uart_platform[line].irq);
}

/***************************************************************************/

static void mcf54xx_reset(void)
{
	/* disable interrupts and enable the watchdog */
	asm("movew #0x2700, %sr\n");
	__raw_writel(0, MCF_MBAR + MCF_GPT_GMS0);
	__raw_writel(MCF_GPT_GCIR_CNT(1), MCF_MBAR + MCF_GPT_GCIR0);
	__raw_writel(MCF_GPT_GMS_WDEN | MCF_GPT_GMS_CE | MCF_GPT_GMS_TMS(4),
						MCF_MBAR + MCF_GPT_GMS0);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_reset = mcf54xx_reset;
	m54xx_uarts_init();
}

/***************************************************************************/

static int __init init_BSP(void)
{

	platform_add_devices(m54xx_devices, ARRAY_SIZE(m54xx_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
