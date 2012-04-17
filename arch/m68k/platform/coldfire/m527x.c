/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/527x/config.c
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	5270/5271 CPUs.
 *
 *	Copyright (C) 1999-2004, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2004, SnapGear Inc. (www.snapgear.com)
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
#include <asm/mcfgpio.h>

/***************************************************************************/

struct mcf_gpio_chip mcf_gpio_chips[] = {
#if defined(CONFIG_M5271)
	MCFGPS(PIRQ, 1, 7, MCFEPORT_EPDDR, MCFEPORT_EPDR, MCFEPORT_EPPDR),
	MCFGPF(ADDR, 13, 3),
	MCFGPF(DATAH, 16, 8),
	MCFGPF(DATAL, 24, 8),
	MCFGPF(BUSCTL, 32, 8),
	MCFGPF(BS, 40, 4),
	MCFGPF(CS, 49, 7),
	MCFGPF(SDRAM, 56, 6),
	MCFGPF(FECI2C, 64, 4),
	MCFGPF(UARTH, 72, 2),
	MCFGPF(UARTL, 80, 8),
	MCFGPF(QSPI, 88, 5),
	MCFGPF(TIMER, 96, 8),
#elif defined(CONFIG_M5275)
	MCFGPS(PIRQ, 1, 7, MCFEPORT_EPDDR, MCFEPORT_EPDR, MCFEPORT_EPPDR),
	MCFGPF(BUSCTL, 8, 8),
	MCFGPF(ADDR, 21, 3),
	MCFGPF(CS, 25, 7),
	MCFGPF(FEC0H, 32, 8),
	MCFGPF(FEC0L, 40, 8),
	MCFGPF(FECI2C, 48, 6),
	MCFGPF(QSPI, 56, 7),
	MCFGPF(SDRAM, 64, 8),
	MCFGPF(TIMERH, 72, 4),
	MCFGPF(TIMERL, 80, 4),
	MCFGPF(UARTL, 88, 8),
	MCFGPF(FEC1H, 96, 8),
	MCFGPF(FEC1L, 104, 8),
	MCFGPF(BS, 114, 2),
	MCFGPF(IRQ, 121, 7),
	MCFGPF(USBH, 128, 1),
	MCFGPF(USBL, 136, 8),
	MCFGPF(UARTH, 144, 4),
#endif
};

unsigned int mcf_gpio_chips_size = ARRAY_SIZE(mcf_gpio_chips);

/***************************************************************************/

#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)

static void __init m527x_qspi_init(void)
{
#if defined(CONFIG_M5271)
	u16 par;

	/* setup QSPS pins for QSPI with gpio CS control */
	writeb(0x1f, MCFGPIO_PAR_QSPI);
	/* and CS2 & CS3 as gpio */
	par = readw(MCFGPIO_PAR_TIMER);
	par &= 0x3f3f;
	writew(par, MCFGPIO_PAR_TIMER);
#elif defined(CONFIG_M5275)
	/* setup QSPS pins for QSPI with gpio CS control */
	writew(0x003e, MCFGPIO_PAR_QSPI);
#endif
}

#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */

/***************************************************************************/

static void __init m527x_uarts_init(void)
{
	u16 sepmask;

	/*
	 * External Pin Mask Setting & Enable External Pin for Interface
	 */
	sepmask = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
	sepmask |= UART0_ENABLE_MASK | UART1_ENABLE_MASK | UART2_ENABLE_MASK;
	writew(sepmask, MCF_IPSBAR + MCF_GPIO_PAR_UART);
}

/***************************************************************************/

static void __init m527x_fec_init(void)
{
	u16 par;
	u8 v;

	/* Set multi-function pins to ethernet mode for fec0 */
#if defined(CONFIG_M5271)
	v = readb(MCF_IPSBAR + 0x100047);
	writeb(v | 0xf0, MCF_IPSBAR + 0x100047);
#else
	par = readw(MCF_IPSBAR + 0x100082);
	writew(par | 0xf00, MCF_IPSBAR + 0x100082);
	v = readb(MCF_IPSBAR + 0x100078);
	writeb(v | 0xc0, MCF_IPSBAR + 0x100078);

	/* Set multi-function pins to ethernet mode for fec1 */
	par = readw(MCF_IPSBAR + 0x100082);
	writew(par | 0xa0, MCF_IPSBAR + 0x100082);
	v = readb(MCF_IPSBAR + 0x100079);
	writeb(v | 0xc0, MCF_IPSBAR + 0x100079);
#endif
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_sched_init = hw_timer_init;
	m527x_uarts_init();
	m527x_fec_init();
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	m527x_qspi_init();
#endif
}

/***************************************************************************/
