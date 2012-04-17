/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/523x/config.c
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	523x CPUs.
 *
 *	Copyright (C) 1999-2005, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfgpio.h>

/***************************************************************************/

struct mcf_gpio_chip mcf_gpio_chips[] = {
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
	MCFGPF(ETPU, 104, 3),
};

unsigned int mcf_gpio_chips_size = ARRAY_SIZE(mcf_gpio_chips);

/***************************************************************************/

#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)

static void __init m523x_qspi_init(void)
{
	u16 par;

	/* setup QSPS pins for QSPI with gpio CS control */
	writeb(0x1f, MCFGPIO_PAR_QSPI);
	/* and CS2 & CS3 as gpio */
	par = readw(MCFGPIO_PAR_TIMER);
	par &= 0x3f3f;
	writew(par, MCFGPIO_PAR_TIMER);
}

#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */

/***************************************************************************/

static void __init m523x_fec_init(void)
{
	u16 par;
	u8 v;

	/* Set multi-function pins to ethernet use */
	par = readw(MCF_IPSBAR + 0x100082);
	writew(par | 0xf00, MCF_IPSBAR + 0x100082);
	v = readb(MCF_IPSBAR + 0x100078);
	writeb(v | 0xc0, MCF_IPSBAR + 0x100078);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_sched_init = hw_timer_init;
	m523x_fec_init();
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	m523x_qspi_init();
#endif
}

/***************************************************************************/
