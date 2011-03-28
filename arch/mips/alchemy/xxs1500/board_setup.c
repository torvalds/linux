/*
 * Copyright 2000-2003, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <asm/reboot.h>
#include <asm/mach-au1x00/au1000.h>

#include <prom.h>

static void xxs1500_reset(char *c)
{
	/* Jump to the reset vector */
	__asm__ __volatile__("jr\t%0"::"r"(0xbfc00000));
}

static void xxs1500_power_off(void)
{
	while (1)
		asm volatile (
		"	.set	mips32					\n"
		"	wait						\n"
		"	.set	mips0					\n");
}

void __init board_setup(void)
{
	u32 pin_func;

	pm_power_off = xxs1500_power_off;
	_machine_halt = xxs1500_power_off;
	_machine_restart = xxs1500_reset;

	alchemy_gpio1_input_enable();
	alchemy_gpio2_enable();

	/* Set multiple use pins (UART3/GPIO) to UART (it's used as UART too) */
	pin_func  = au_readl(SYS_PINFUNC) & ~SYS_PF_UR3;
	pin_func |= SYS_PF_UR3;
	au_writel(pin_func, SYS_PINFUNC);

	/* Enable UART */
	au_writel(0x01, UART3_ADDR + UART_MOD_CNTRL); /* clock enable (CE) */
	mdelay(10);
	au_writel(0x03, UART3_ADDR + UART_MOD_CNTRL); /* CE and "enable" */
	mdelay(10);

	/* Enable DTR = USB power up */
	au_writel(0x01, UART3_ADDR + UART_MCR); /* UART_MCR_DTR is 0x01??? */

#ifdef CONFIG_PCI
#if defined(__MIPSEB__)
	au_writel(0xf | (2 << 6) | (1 << 4), Au1500_PCI_CFG);
#else
	au_writel(0xf, Au1500_PCI_CFG);
#endif
#endif
}

static int __init xxs1500_init_irq(void)
{
	set_irq_type(AU1500_GPIO204_INT, IRQF_TRIGGER_HIGH);
	set_irq_type(AU1500_GPIO201_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO202_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO203_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO205_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO207_INT, IRQF_TRIGGER_LOW);

	set_irq_type(AU1500_GPIO0_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO1_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO2_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO3_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO4_INT, IRQF_TRIGGER_LOW); /* CF irq */
	set_irq_type(AU1500_GPIO5_INT, IRQF_TRIGGER_LOW);

	return 0;
}
arch_initcall(xxs1500_init_irq);
