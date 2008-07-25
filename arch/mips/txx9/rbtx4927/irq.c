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
IRQ  Device
00   RBTX4927-ISA/00
01   RBTX4927-ISA/01 PS2/Keyboard
02   RBTX4927-ISA/02 Cascade RBTX4927-ISA (irqs 8-15)
03   RBTX4927-ISA/03
04   RBTX4927-ISA/04
05   RBTX4927-ISA/05
06   RBTX4927-ISA/06
07   RBTX4927-ISA/07
08   RBTX4927-ISA/08
09   RBTX4927-ISA/09
10   RBTX4927-ISA/10
11   RBTX4927-ISA/11
12   RBTX4927-ISA/12 PS2/Mouse (not supported at this time)
13   RBTX4927-ISA/13
14   RBTX4927-ISA/14 IDE
15   RBTX4927-ISA/15

16   TX4927-CP0/00 Software 0
17   TX4927-CP0/01 Software 1
18   TX4927-CP0/02 Cascade TX4927-CP0
19   TX4927-CP0/03 Multiplexed -- do not use
20   TX4927-CP0/04 Multiplexed -- do not use
21   TX4927-CP0/05 Multiplexed -- do not use
22   TX4927-CP0/06 Multiplexed -- do not use
23   TX4927-CP0/07 CPU TIMER

24   TX4927-PIC/00
25   TX4927-PIC/01
26   TX4927-PIC/02
27   TX4927-PIC/03 Cascade RBTX4927-IOC
28   TX4927-PIC/04
29   TX4927-PIC/05 RBTX4927 RTL-8019AS ethernet
30   TX4927-PIC/06
31   TX4927-PIC/07
32   TX4927-PIC/08 TX4927 SerialIO Channel 0
33   TX4927-PIC/09 TX4927 SerialIO Channel 1
34   TX4927-PIC/10
35   TX4927-PIC/11
36   TX4927-PIC/12
37   TX4927-PIC/13
38   TX4927-PIC/14
39   TX4927-PIC/15
40   TX4927-PIC/16 TX4927 PCI PCI-C
41   TX4927-PIC/17
42   TX4927-PIC/18
43   TX4927-PIC/19
44   TX4927-PIC/20
45   TX4927-PIC/21
46   TX4927-PIC/22 TX4927 PCI PCI-ERR
47   TX4927-PIC/23 TX4927 PCI PCI-PMA (not used)
48   TX4927-PIC/24
49   TX4927-PIC/25
50   TX4927-PIC/26
51   TX4927-PIC/27
52   TX4927-PIC/28
53   TX4927-PIC/29
54   TX4927-PIC/30
55   TX4927-PIC/31

56 RBTX4927-IOC/00 FPCIB0 PCI-D PJ4/A PJ5/B SB/C PJ6/D PJ7/A (SouthBridge/NotUsed)        [RTL-8139=PJ4]
57 RBTX4927-IOC/01 FPCIB0 PCI-C PJ4/D PJ5/A SB/B PJ6/C PJ7/D (SouthBridge/NotUsed)        [RTL-8139=PJ5]
58 RBTX4927-IOC/02 FPCIB0 PCI-B PJ4/C PJ5/D SB/A PJ6/B PJ7/C (SouthBridge/IDE/pin=1,INTR) [RTL-8139=NotSupported]
59 RBTX4927-IOC/03 FPCIB0 PCI-A PJ4/B PJ5/C SB/D PJ6/A PJ7/B (SouthBridge/USB/pin=4)      [RTL-8139=PJ6]
60 RBTX4927-IOC/04
61 RBTX4927-IOC/05
62 RBTX4927-IOC/06
63 RBTX4927-IOC/07

NOTES:
SouthBridge/INTR is mapped to SouthBridge/A=PCI-B/#58
SouthBridge/ISA/pin=0 no pci irq used by this device
SouthBridge/IDE/pin=1 no pci irq used by this device, using INTR via ISA IRQ14
SouthBridge/USB/pin=4 using pci irq SouthBridge/D=PCI-A=#59
SouthBridge/PMC/pin=0 no pci irq used by this device
SuperIO/PS2/Keyboard, using INTR via ISA IRQ1
SuperIO/PS2/Mouse, using INTR via ISA IRQ12 (mouse not currently supported)
JP7 is not bus master -- do NOT use -- only 4 pci bus master's allowed -- SouthBridge, JP4, JP5, JP6
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
	if (level3)
		sw_irq = RBTX4927_IRQ_IOC + fls(level3) - 1;
	return (sw_irq);
}

static void __init toshiba_rbtx4927_irq_ioc_init(void)
{
	int i;

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
