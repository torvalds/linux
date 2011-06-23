/*
 * Copyright 2010 Wolfgang Grandegger <wg@denx.de>
 *
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

char irq_tab_alchemy[][5] __initdata = {
	[0] = { -1, AU1500_PCI_INTA, AU1500_PCI_INTB, 0xff, 0xff },
};

static void gpr_reset(char *c)
{
	/* switch System-LED to orange (red# and green# on) */
	alchemy_gpio_direction_output(4, 0);
	alchemy_gpio_direction_output(5, 0);

	/* trigger watchdog to reset board in 200ms */
	printk(KERN_EMERG "Triggering watchdog soft reset...\n");
	raw_local_irq_disable();
	alchemy_gpio_direction_output(1, 0);
	udelay(1);
	alchemy_gpio_set_value(1, 1);
	while (1)
		cpu_wait();
}

static void gpr_power_off(void)
{
	while (1)
		cpu_wait();
}

void __init board_setup(void)
{
	printk(KERN_INFO "Trapeze ITS GPR board\n");

	pm_power_off = gpr_power_off;
	_machine_halt = gpr_power_off;
	_machine_restart = gpr_reset;

	/* Enable UART1/3 */
	alchemy_uart_enable(AU1000_UART3_PHYS_ADDR);
	alchemy_uart_enable(AU1000_UART1_PHYS_ADDR);

	/* Take away Reset of UMTS-card */
	alchemy_gpio_direction_output(215, 1);

#ifdef CONFIG_PCI
#if defined(__MIPSEB__)
	au_writel(0xf | (2 << 6) | (1 << 4), Au1500_PCI_CFG);
#else
	au_writel(0xf, Au1500_PCI_CFG);
#endif
#endif
}
