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
	/* Set multi-function pins to ethernet use */
	writeb(readb(MCFGPIO_PAR_FECI2C) | 0xf0, MCFGPIO_PAR_FECI2C);
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
