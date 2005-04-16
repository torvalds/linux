/* linux/arch/arm/mach-s3c2410/bast-irq.c
 *
 * Copyright (c) 2004 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
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
 *
 * Modifications:
 *     08-Jan-2003 BJD  Moved from central IRQ code
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/ptrace.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/irq.h>
#include <asm/hardware/s3c2410/irq.h>

#if 0
#include <asm/debug-ll.h>
#endif

#define irqdbf(x...)
#define irqdbf2(x...)


/* handle PC104 ISA interrupts from the system CPLD */

/* table of ISA irq nos to the relevant mask... zero means
 * the irq is not implemented
*/
static unsigned char bast_pc104_irqmasks[] = {
	0,   /* 0 */
	0,   /* 1 */
	0,   /* 2 */
	1,   /* 3 */
	0,   /* 4 */
	2,   /* 5 */
	0,   /* 6 */
	4,   /* 7 */
	0,   /* 8 */
	0,   /* 9 */
	8,   /* 10 */
	0,   /* 11 */
	0,   /* 12 */
	0,   /* 13 */
	0,   /* 14 */
	0,   /* 15 */
};

static unsigned char bast_pc104_irqs[] = { 3, 5, 7, 10 };

static void
bast_pc104_mask(unsigned int irqno)
{
	unsigned long temp;

	temp = __raw_readb(BAST_VA_PC104_IRQMASK);
	temp &= ~bast_pc104_irqmasks[irqno];
	__raw_writeb(temp, BAST_VA_PC104_IRQMASK);

	if (temp == 0)
		bast_extint_mask(IRQ_ISA);
}

static void
bast_pc104_ack(unsigned int irqno)
{
	bast_extint_ack(IRQ_ISA);
}

static void
bast_pc104_unmask(unsigned int irqno)
{
	unsigned long temp;

	temp = __raw_readb(BAST_VA_PC104_IRQMASK);
	temp |= bast_pc104_irqmasks[irqno];
	__raw_writeb(temp, BAST_VA_PC104_IRQMASK);

	bast_extint_unmask(IRQ_ISA);
}

static struct bast_pc104_chip = {
	.mask	     = bast_pc104_mask,
	.unmask	     = bast_pc104_unmask,
	.ack	     = bast_pc104_ack
};

static void
bast_irq_pc104_demux(unsigned int irq,
		     struct irqdesc *desc,
		     struct pt_regs *regs)
{
	unsigned int stat;
	unsigned int irqno;
	int i;

	stat = __raw_readb(BAST_VA_PC104_IRQREQ) & 0xf;

	for (i = 0; i < 4 && stat != 0; i++) {
		if (stat & 1) {
			irqno = bast_pc104_irqs[i];
			desc = irq_desc + irqno;

			desc->handle(irqno, desc, regs);
		}

		stat >>= 1;
	}
}
