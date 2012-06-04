/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5307/config.c
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
#include <asm/mcfgpio.h>
#include <asm/mcfwdebug.h>

/***************************************************************************/

/*
 *	Some platforms need software versions of the GPIO data registers.
 */
unsigned short ppdata;
unsigned char ledbank = 0xff;

/***************************************************************************/

struct mcf_gpio_chip mcf_gpio_chips[] = {
	MCFGPS(PP, 0, 16, MCFSIM_PADDR, MCFSIM_PADAT, MCFSIM_PADAT),
};

unsigned int mcf_gpio_chips_size = ARRAY_SIZE(mcf_gpio_chips);

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
#if defined(CONFIG_NETtel) || \
    defined(CONFIG_SECUREEDGEMP3) || defined(CONFIG_CLEOPATRA)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#endif

	mach_sched_init = hw_timer_init;

	/* Only support the external interrupts on their primary level */
	mcf_mapirq2imr(25, MCFINTC_EINT1);
	mcf_mapirq2imr(27, MCFINTC_EINT3);
	mcf_mapirq2imr(29, MCFINTC_EINT5);
	mcf_mapirq2imr(31, MCFINTC_EINT7);

#ifdef CONFIG_BDM_DISABLE
	/*
	 * Disable the BDM clocking.  This also turns off most of the rest of
	 * the BDM device.  This is good for EMC reasons. This option is not
	 * incompatible with the memory protection option.
	 */
	wdebug(MCFDEBUG_CSR, MCFDEBUG_CSR_PSTCLK);
#endif
}

/***************************************************************************/
