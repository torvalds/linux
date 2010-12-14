/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
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
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/jmr3927.h>

#if JMR3927_IRQ_END > NR_IRQS
#error JMR3927_IRQ_END > NR_IRQS
#endif

/*
 * CP0_STATUS is a thread's resource (saved/restored on context switch).
 * So disable_irq/enable_irq MUST handle IOC/IRC registers.
 */
static void mask_irq_ioc(unsigned int irq)
{
	/* 0: mask */
	unsigned int irq_nr = irq - JMR3927_IRQ_IOC;
	unsigned char imask = jmr3927_ioc_reg_in(JMR3927_IOC_INTM_ADDR);
	unsigned int bit = 1 << irq_nr;
	jmr3927_ioc_reg_out(imask & ~bit, JMR3927_IOC_INTM_ADDR);
	/* flush write buffer */
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR);
}
static void unmask_irq_ioc(unsigned int irq)
{
	/* 0: mask */
	unsigned int irq_nr = irq - JMR3927_IRQ_IOC;
	unsigned char imask = jmr3927_ioc_reg_in(JMR3927_IOC_INTM_ADDR);
	unsigned int bit = 1 << irq_nr;
	jmr3927_ioc_reg_out(imask | bit, JMR3927_IOC_INTM_ADDR);
	/* flush write buffer */
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR);
}

static int jmr3927_ioc_irqroute(void)
{
	unsigned char istat = jmr3927_ioc_reg_in(JMR3927_IOC_INTS2_ADDR);
	int i;

	for (i = 0; i < JMR3927_NR_IRQ_IOC; i++) {
		if (istat & (1 << i))
			return JMR3927_IRQ_IOC + i;
	}
	return -1;
}

static int jmr3927_irq_dispatch(int pending)
{
	int irq;

	if ((pending & CAUSEF_IP7) == 0)
		return -1;
	irq = (pending >> CAUSEB_IP2) & 0x0f;
	irq += JMR3927_IRQ_IRC;
	if (irq == JMR3927_IRQ_IOCINT)
		irq = jmr3927_ioc_irqroute();
	return irq;
}

static struct irq_chip jmr3927_irq_ioc = {
	.name = "jmr3927_ioc",
	.ack = mask_irq_ioc,
	.mask = mask_irq_ioc,
	.mask_ack = mask_irq_ioc,
	.unmask = unmask_irq_ioc,
};

void __init jmr3927_irq_setup(void)
{
	int i;

	txx9_irq_dispatch = jmr3927_irq_dispatch;
	/* Now, interrupt control disabled, */
	/* all IRC interrupts are masked, */
	/* all IRC interrupt mode are Low Active. */

	/* mask all IOC interrupts */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_INTM_ADDR);
	/* setup IOC interrupt mode (SOFT:High Active, Others:Low Active) */
	jmr3927_ioc_reg_out(JMR3927_IOC_INTF_SOFT, JMR3927_IOC_INTP_ADDR);

	/* clear PCI Soft interrupts */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_INTS1_ADDR);
	/* clear PCI Reset interrupts */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);

	tx3927_irq_init();
	for (i = JMR3927_IRQ_IOC; i < JMR3927_IRQ_IOC + JMR3927_NR_IRQ_IOC; i++)
		set_irq_chip_and_handler(i, &jmr3927_irq_ioc, handle_level_irq);

	/* setup IOC interrupt 1 (PCI, MODEM) */
	set_irq_chained_handler(JMR3927_IRQ_IOCINT, handle_simple_irq);
}
