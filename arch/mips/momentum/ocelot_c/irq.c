/*
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (C) 2000, 01, 05 Ralf Baechle (ralf@linux-mips.org)
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
 *
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/bitops.h>
#include <linux/mv643xx.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

extern void uart_irq_init(void);
extern void cpci_irq_init(void);

static struct irqaction cascade_fpga = {
	no_action, IRQF_DISABLED, CPU_MASK_NONE, "cascade via FPGA", NULL, NULL
};

static struct irqaction cascade_mv64340 = {
	no_action, IRQF_DISABLED, CPU_MASK_NONE, "cascade via MV64340", NULL, NULL
};

extern void ll_uart_irq(void);
extern void ll_cpci_irq(void);

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status();

	if (pending & STATUSF_IP0)
		do_IRQ(0);
	else if (pending & STATUSF_IP1)
		do_IRQ(1);
	else if (pending & STATUSF_IP2)
		do_IRQ(2);
	else if (pending & STATUSF_IP3)
		ll_uart_irq();
	else if (pending & STATUSF_IP4)
		do_IRQ(4);
	else if (pending & STATUSF_IP5)
		ll_cpci_irq();
	else if (pending & STATUSF_IP6)
		ll_mv64340_irq();
	else if (pending & STATUSF_IP7)
		do_IRQ(7);
	else
		spurious_interrupt();
}

void __init arch_init_irq(void)
{
	/*
	 * Clear all of the interrupts while we change the able around a bit.
	 * int-handler is not on bootstrap
	 */
	clear_c0_status(ST0_IM);

	mips_cpu_irq_init(0);

	/* set up the cascading interrupts */
	setup_irq(3, &cascade_fpga);
	setup_irq(5, &cascade_fpga);
	setup_irq(6, &cascade_mv64340);

	mv64340_irq_init(16);
	uart_irq_init();
	cpci_irq_init();
}
