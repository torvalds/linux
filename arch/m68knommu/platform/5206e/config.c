/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5206e/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

/***************************************************************************/

void coldfire_reset(void);

/***************************************************************************/

static struct mcf_platform_uart m5206e_uart_platform[] = {
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

static struct platform_device m5206e_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m5206e_uart_platform,
};

static struct platform_device *m5206e_devices[] __initdata = {
	&m5206e_uart,
};

/***************************************************************************/

static void __init m5206e_uart_init_line(int line, int irq)
{
	if (line == 0) {
		writel(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI1, MCF_MBAR + MCFSIM_UART1ICR);
		writeb(irq, MCFUART_BASE1 + MCFUART_UIVR);
		mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_UART1);
	} else if (line == 1) {
		writel(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI2, MCF_MBAR + MCFSIM_UART2ICR);
		writeb(irq, MCFUART_BASE2 + MCFUART_UIVR);
		mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_UART2);
	}
}

static void __init m5206e_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m5206e_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m5206e_uart_init_line(line, m5206e_uart_platform[line].irq);
}

/***************************************************************************/

void mcf_autovector(unsigned int vec)
{
	volatile unsigned char  *mbar;
	unsigned char		icr;

	if ((vec >= 25) && (vec <= 31)) {
		vec -= 25;
		mbar = (volatile unsigned char *) MCF_MBAR;
		icr = MCFSIM_ICR_AUTOVEC | (vec << 3);
		*(mbar + MCFSIM_ICR1 + vec) = icr;
		vec = 0x1 << (vec + 1);
		mcf_setimr(mcf_getimr() & ~vec);
	}
}

/***************************************************************************/

void mcf_settimericr(unsigned int timer, unsigned int level)
{
	volatile unsigned char *icrp;
	unsigned int icr, imr;

	if (timer <= 2) {
		switch (timer) {
		case 2:  icr = MCFSIM_TIMER2ICR; imr = MCFSIM_IMR_TIMER2; break;
		default: icr = MCFSIM_TIMER1ICR; imr = MCFSIM_IMR_TIMER1; break;
		}

		icrp = (volatile unsigned char *) (MCF_MBAR + icr);
		*icrp = MCFSIM_ICR_AUTOVEC | (level << 2) | MCFSIM_ICR_PRI3;
		mcf_setimr(mcf_getimr() & ~imr);
	}
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mcf_setimr(MCFSIM_IMR_MASKALL);

#if defined(CONFIG_NETtel)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#endif /* CONFIG_NETtel */

	mach_reset = coldfire_reset;
}

/***************************************************************************/

static int __init init_BSP(void)
{
	m5206e_uarts_init();
	platform_add_devices(m5206e_devices, ARRAY_SIZE(m5206e_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
