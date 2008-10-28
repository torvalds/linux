/* linux/arch/arm/mach-s3c2400/gpio.c
 *
 * Copyright (c) 2006 Lucas Correia Villa Real <lucasvr@gobolinux.org>
 *
 * S3C2400 GPIO support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <mach/regs-gpio.h>

int s3c2400_gpio_getirq(unsigned int pin)
{
	if (pin < S3C2410_GPE0 || pin > S3C2400_GPE7_EINT7)
		return -1;  /* not valid interrupts */

	return (pin - S3C2410_GPE0) + IRQ_EINT0;
}

EXPORT_SYMBOL(s3c2400_gpio_getirq);
