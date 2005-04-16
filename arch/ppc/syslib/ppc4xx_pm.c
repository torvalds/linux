/*
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * This an attempt to get Power Management going for the IBM 4xx processor.
 * This was derived from the ppc4xx._setup.c file
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/ibm4xx.h>

void __init
ppc4xx_pm_init(void)
{

	unsigned int value = 0;

	/* turn off unused hardware to save power */
#ifdef CONFIG_405GP
	value |= CPM_DCP;	/* CodePack */
#endif

#if !defined(CONFIG_IBM_OCP_GPIO)
	value |= CPM_GPIO0;
#endif

#if !defined(CONFIG_PPC405_I2C_ADAP)
	value |= CPM_IIC0;
#ifdef CONFIG_STB03xxx
	value |= CPM_IIC1;
#endif
#endif


#if !defined(CONFIG_405_DMA)
	value |= CPM_DMA;
#endif

	mtdcr(DCRN_CPMFR, value);

}
