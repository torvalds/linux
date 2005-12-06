/*
 * arch/arm/mach-iop3xx/common.c
 *
 * Common routines shared across all IOP3xx implementations
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2003 (c) MontaVista, Software, Inc.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/delay.h>
#include <asm/hardware.h>

/*
 * Shared variables
 */
unsigned long iop3xx_pcibios_min_io = 0;
unsigned long iop3xx_pcibios_min_mem = 0;

#ifdef CONFIG_ARCH_EP80219
#include <linux/kernel.h>
/*
 * Default power-off for EP80219
 */

static inline void ep80219_send_to_pic(__u8 c) {
}

void ep80219_power_off(void)
{
	/*
     * This function will send a SHUTDOWN_COMPLETE message to the PIC controller
     * over I2C.  We are not using the i2c subsystem since we are going to power
     * off and it may be removed
     */

	/* Send the Address byte w/ the start condition */
	*IOP321_IDBR1 = 0x60;
	*IOP321_ICR1 = 0xE9;
    mdelay(1);

	/* Send the START_MSG byte w/ no start or stop condition */
	*IOP321_IDBR1 = 0x0F;
	*IOP321_ICR1 = 0xE8;
    mdelay(1);

	/* Send the SHUTDOWN_COMPLETE Message ID byte w/ no start or stop condition */
	*IOP321_IDBR1 = 0x03;
	*IOP321_ICR1 = 0xE8;
    mdelay(1);

	/* Send an ignored byte w/ stop condition */
	*IOP321_IDBR1 = 0x00;
	*IOP321_ICR1 = 0xEA;

	while (1) ;
}

#include <linux/init.h>
#include <linux/pm.h>

static int __init ep80219_init(void)
{
	pm_power_off = ep80219_power_off;
	return 0;
}
arch_initcall(ep80219_init);
#endif
