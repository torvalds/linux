/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/528x/config.c
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	5280, 5281 and 5282 CPUs.
 *
 *	Copyright (C) 1999-2003, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>

/***************************************************************************/

#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)

static void __init m528x_qspi_init(void)
{
	/* setup Port QS for QSPI with gpio CS control */
	__raw_writeb(0x07, MCFGPIO_PQSPAR);
}

#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */

/***************************************************************************/

static void __init m528x_uarts_init(void)
{
	u8 port;

	/* make sure PUAPAR is set for UART0 and UART1 */
	port = readb(MCF5282_GPIO_PUAPAR);
	port |= 0x03 | (0x03 << 2);
	writeb(port, MCFGPIO_PUAPAR);
}

/***************************************************************************/

static void __init m528x_fec_init(void)
{
	u16 v16;

	/* Set multi-function pins to ethernet mode for fec0 */
	v16 = readw(MCFGPIO_PASPAR);
	writew(v16 | 0xf00, MCFGPIO_PASPAR);
	writeb(0xc0, MCFGPIO_PEHLPAR);
}

/***************************************************************************/

#ifdef CONFIG_WILDFIRE
void wildfire_halt(void)
{
	writeb(0, 0x30000007);
	writeb(0x2, 0x30000007);
}
#endif

#ifdef CONFIG_WILDFIREMOD
void wildfiremod_halt(void)
{
	printk(KERN_INFO "WildFireMod hibernating...\n");

	/* Set portE.5 to Digital IO */
	MCF5282_GPIO_PEPAR &= ~(1 << (5 * 2));

	/* Make portE.5 an output */
	MCF5282_GPIO_DDRE |= (1 << 5);

	/* Now toggle portE.5 from low to high */
	MCF5282_GPIO_PORTE &= ~(1 << 5);
	MCF5282_GPIO_PORTE |= (1 << 5);

	printk(KERN_EMERG "Failed to hibernate. Halting!\n");
}
#endif

void __init config_BSP(char *commandp, int size)
{
#ifdef CONFIG_WILDFIRE
	mach_halt = wildfire_halt;
#endif
#ifdef CONFIG_WILDFIREMOD
	mach_halt = wildfiremod_halt;
#endif
	mach_sched_init = hw_timer_init;
	m528x_uarts_init();
	m528x_fec_init();
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	m528x_qspi_init();
#endif
}

/***************************************************************************/
