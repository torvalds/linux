/*
 * KFR2R09 board support code
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <asm/clock.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <cpu/sh7724.h>

static int __init kfr2r09_devices_setup(void)
{
	/* enable SCIF1 serial port for YC401 console support */
	gpio_request(GPIO_FN_SCIF1_RXD, NULL);
	gpio_request(GPIO_FN_SCIF1_TXD, NULL);

	return 0;
}
device_initcall(kfr2r09_devices_setup);

/* Return the board specific boot mode pin configuration */
static int kfr2r09_mode_pins(void)
{
	/* MD0=1, MD1=1, MD2=0: Clock Mode 3
	 * MD3=0: 16-bit Area0 Bus Width
	 * MD5=1: Little Endian
	 * MD8=1: Test Mode Disabled
	 */
	return MODE_PIN0 | MODE_PIN1 | MODE_PIN5 | MODE_PIN8;
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_kfr2r09 __initmv = {
	.mv_name		= "kfr2r09",
	.mv_mode_pins		= kfr2r09_mode_pins,
};
