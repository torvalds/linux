/***************************************************************************/

/*
 *  linux/arch/m68knommu/platform/520x/config.c
 *
 *  Copyright (C) 2005,      Freescale (www.freescale.com)
 *  Copyright (C) 2005,      Intec Automation (mike@steroidmicros.com)
 *  Copyright (C) 1999-2007, Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
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

static struct mcf_platform_uart m520x_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= MCFINT_VECBASE + MCFINT_UART0,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE2,
		.irq		= MCFINT_VECBASE + MCFINT_UART1,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE3,
		.irq		= MCFINT_VECBASE + MCFINT_UART2,
	},
	{ },
};

static struct platform_device m520x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m520x_uart_platform,
};

static struct resource m520x_fec_resources[] = {
	{
		.start		= MCF_MBAR + 0x30000,
		.end		= MCF_MBAR + 0x30000 + 0x7ff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 64 + 36,
		.end		= 64 + 36,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 64 + 40,
		.end		= 64 + 40,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 64 + 42,
		.end		= 64 + 42,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device m520x_fec = {
	.name			= "fec",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m520x_fec_resources),
	.resource		= m520x_fec_resources,
};

static struct platform_device *m520x_devices[] __initdata = {
	&m520x_uart,
	&m520x_fec,
};

/***************************************************************************/

#define	INTC0	(MCF_MBAR + MCFICM_INTC0)

static void __init m520x_uart_init_line(int line, int irq)
{
	u16 par;
	u8 par2;

	writeb(0x03, INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + line);

	switch (line) {
	case 0:
		par = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
		par |= MCF_GPIO_PAR_UART_PAR_UTXD0 |
		       MCF_GPIO_PAR_UART_PAR_URXD0;
		writew(par, MCF_IPSBAR + MCF_GPIO_PAR_UART);
		break;
	case 1:
		par = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
		par |= MCF_GPIO_PAR_UART_PAR_UTXD1 |
		       MCF_GPIO_PAR_UART_PAR_URXD1;
		writew(par, MCF_IPSBAR + MCF_GPIO_PAR_UART);
		break;
	case 2:
		par2 = readb(MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
		par2 &= ~0x0F;
		par2 |= MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2 |
			MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2;
		writeb(par2, MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
		break;
	}
}

static void __init m520x_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m520x_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m520x_uart_init_line(line, m520x_uart_platform[line].irq);
}

/***************************************************************************/

static void __init m520x_fec_init(void)
{
	u8 v;

	/* Unmask FEC interrupts at ColdFire interrupt controller */
	writeb(0x4, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 + 36);
	writeb(0x4, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 + 40);
	writeb(0x4, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 + 42);

	/* Set multi-function pins to ethernet mode */
	v = readb(MCF_IPSBAR + MCF_GPIO_PAR_FEC);
	writeb(v | 0xf0, MCF_IPSBAR + MCF_GPIO_PAR_FEC);

	v = readb(MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
	writeb(v | 0x0f, MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
}

/***************************************************************************/

static void m520x_cpu_reset(void)
{
	local_irq_disable();
	__raw_writeb(MCF_RCR_SWRESET, MCF_RCR);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_reset = m520x_cpu_reset;
	m520x_uarts_init();
	m520x_fec_init();
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m520x_devices, ARRAY_SIZE(m520x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
