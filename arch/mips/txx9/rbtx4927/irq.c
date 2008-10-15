/*
 * Toshiba RBTX4927 specific interrupt handlers
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * I8259A_IRQ_BASE+00
 * I8259A_IRQ_BASE+01 PS2/Keyboard
 * I8259A_IRQ_BASE+02 Cascade RBTX4927-ISA (irqs 8-15)
 * I8259A_IRQ_BASE+03
 * I8259A_IRQ_BASE+04
 * I8259A_IRQ_BASE+05
 * I8259A_IRQ_BASE+06
 * I8259A_IRQ_BASE+07
 * I8259A_IRQ_BASE+08
 * I8259A_IRQ_BASE+09
 * I8259A_IRQ_BASE+10
 * I8259A_IRQ_BASE+11
 * I8259A_IRQ_BASE+12 PS2/Mouse (not supported at this time)
 * I8259A_IRQ_BASE+13
 * I8259A_IRQ_BASE+14 IDE
 * I8259A_IRQ_BASE+15
 *
 * MIPS_CPU_IRQ_BASE+00 Software 0
 * MIPS_CPU_IRQ_BASE+01 Software 1
 * MIPS_CPU_IRQ_BASE+02 Cascade TX4927-CP0
 * MIPS_CPU_IRQ_BASE+03 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+04 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+05 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+06 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+07 CPU TIMER
 *
 * TXX9_IRQ_BASE+00
 * TXX9_IRQ_BASE+01
 * TXX9_IRQ_BASE+02
 * TXX9_IRQ_BASE+03 Cascade RBTX4927-IOC
 * TXX9_IRQ_BASE+04
 * TXX9_IRQ_BASE+05 RBTX4927 RTL-8019AS ethernet
 * TXX9_IRQ_BASE+06
 * TXX9_IRQ_BASE+07
 * TXX9_IRQ_BASE+08 TX4927 SerialIO Channel 0
 * TXX9_IRQ_BASE+09 TX4927 SerialIO Channel 1
 * TXX9_IRQ_BASE+10
 * TXX9_IRQ_BASE+11
 * TXX9_IRQ_BASE+12
 * TXX9_IRQ_BASE+13
 * TXX9_IRQ_BASE+14
 * TXX9_IRQ_BASE+15
 * TXX9_IRQ_BASE+16 TX4927 PCI PCI-C
 * TXX9_IRQ_BASE+17
 * TXX9_IRQ_BASE+18
 * TXX9_IRQ_BASE+19
 * TXX9_IRQ_BASE+20
 * TXX9_IRQ_BASE+21
 * TXX9_IRQ_BASE+22 TX4927 PCI PCI-ERR
 * TXX9_IRQ_BASE+23 TX4927 PCI PCI-PMA (not used)
 * TXX9_IRQ_BASE+24
 * TXX9_IRQ_BASE+25
 * TXX9_IRQ_BASE+26
 * TXX9_IRQ_BASE+27
 * TXX9_IRQ_BASE+28
 * TXX9_IRQ_BASE+29
 * TXX9_IRQ_BASE+30
 * TXX9_IRQ_BASE+31
 *
 * RBTX4927_IRQ_IOC+00 FPCIB0 PCI-D (SouthBridge)
 * RBTX4927_IRQ_IOC+01 FPCIB0 PCI-C (SouthBridge)
 * RBTX4927_IRQ_IOC+02 FPCIB0 PCI-B (SouthBridge/IDE/pin=1,INTR)
 * RBTX4927_IRQ_IOC+03 FPCIB0 PCI-A (SouthBridge/USB/pin=4)
 * RBTX4927_IRQ_IOC+04
 * RBTX4927_IRQ_IOC+05
 * RBTX4927_IRQ_IOC+06
 * RBTX4927_IRQ_IOC+07
 *
 * NOTES:
 * SouthBridge/INTR is mapped to SouthBridge/A=PCI-B/#58
 * SouthBridge/ISA/pin=0 no pci irq used by this device
 * SouthBridge/IDE/pin=1 no pci irq used by this device, using INTR
 * via ISA IRQ14
 * SouthBridge/USB/pin=4 using pci irq SouthBridge/D=PCI-A=#59
 * SouthBridge/PMC/pin=0 no pci irq used by this device
 * SuperIO/PS2/Keyboard, using INTR via ISA IRQ1
 * SuperIO/PS2/Mouse, using INTR via ISA IRQ12 (mouse not currently supported)
 * JP7 is not bus master -- do NOT use -- only 4 pci bus master's
 * allowed -- SouthBridge, JP4, JP5, JP6
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/rbtx4927.h>

static void toshiba_rbtx4927_irq_ioc_enable(unsigned int irq);
static void toshiba_rbtx4927_irq_ioc_disable(unsigned int irq);

#define TOSHIBA_RBTX4927_IOC_NAME "RBTX4927-IOC"
static struct irq_chip toshiba_rbtx4927_irq_ioc_type = {
	.name = TOSHIBA_RBTX4927_IOC_NAME,
	.ack = toshiba_rbtx4927_irq_ioc_disable,
	.mask = toshiba_rbtx4927_irq_ioc_disable,
	.mask_ack = toshiba_rbtx4927_irq_ioc_disable,
	.unmask = toshiba_rbtx4927_irq_ioc_enable,
};

static int toshiba_rbtx4927_irq_nested(int sw_irq)
{
	u8 level3;

	level3 = readb(rbtx4927_imstat_addr) & 0x1f;
	if (unlikely(!level3))
		return -1;
	return RBTX4927_IRQ_IOC + __fls8(level3);
}

static void __init toshiba_rbtx4927_irq_ioc_init(void)
{
	int i;

	/* mask all IOC interrupts */
	writeb(0, rbtx4927_imask_addr);
	/* clear SoftInt interrupts */
	writeb(0, rbtx4927_softint_addr);

	for (i = RBTX4927_IRQ_IOC;
	     i < RBTX4927_IRQ_IOC + RBTX4927_NR_IRQ_IOC; i++)
		set_irq_chip_and_handler(i, &toshiba_rbtx4927_irq_ioc_type,
					 handle_level_irq);
	set_irq_chained_handler(RBTX4927_IRQ_IOCINT, handle_simple_irq);
}

static void toshiba_rbtx4927_irq_ioc_enable(unsigned int irq)
{
	unsigned char v;

	v = readb(rbtx4927_imask_addr);
	v |= (1 << (irq - RBTX4927_IRQ_IOC));
	writeb(v, rbtx4927_imask_addr);
}

static void toshiba_rbtx4927_irq_ioc_disable(unsigned int irq)
{
	unsigned char v;

	v = readb(rbtx4927_imask_addr);
	v &= ~(1 << (irq - RBTX4927_IRQ_IOC));
	writeb(v, rbtx4927_imask_addr);
	mmiowb();
}


static int rbtx4927_irq_dispatch(int pending)
{
	int irq;

	if (pending & STATUSF_IP7)			/* cpu timer */
		irq = MIPS_CPU_IRQ_BASE + 7;
	else if (pending & STATUSF_IP2) {		/* tx4927 pic */
		irq = txx9_irq();
		if (irq == RBTX4927_IRQ_IOCINT)
			irq = toshiba_rbtx4927_irq_nested(irq);
	} else if (pending & STATUSF_IP0)		/* user line 0 */
		irq = MIPS_CPU_IRQ_BASE + 0;
	else if (pending & STATUSF_IP1)			/* user line 1 */
		irq = MIPS_CPU_IRQ_BASE + 1;
	else
		irq = -1;
	return irq;
}

void __init rbtx4927_irq_setup(void)
{
	txx9_irq_dispatch = rbtx4927_irq_dispatch;
	tx4927_irq_init();
	toshiba_rbtx4927_irq_ioc_init();
	/* Onboard 10M Ether: High Active */
	set_irq_type(RBTX4927_RTL_8019_IRQ, IRQF_TRIGGER_HIGH);
}
