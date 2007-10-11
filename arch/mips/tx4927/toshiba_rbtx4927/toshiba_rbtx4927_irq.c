/*
 * linux/arch/mips/tx4927/toshiba_rbtx4927/toshiba_rbtx4927_irq.c
 *
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
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timex.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/wbflush.h>
#include <linux/bootmem.h>
#include <linux/blkdev.h>
#ifdef CONFIG_TOSHIBA_FPCIB0
#include <asm/i8259.h>
#include <asm/tx4927/smsc_fdc37m81x.h>
#endif
#include <asm/tx4927/toshiba_rbtx4927.h>


#undef TOSHIBA_RBTX4927_IRQ_DEBUG

#ifdef TOSHIBA_RBTX4927_IRQ_DEBUG
#define TOSHIBA_RBTX4927_IRQ_NONE        0x00000000

#define TOSHIBA_RBTX4927_IRQ_INFO          ( 1 <<  0 )
#define TOSHIBA_RBTX4927_IRQ_WARN          ( 1 <<  1 )
#define TOSHIBA_RBTX4927_IRQ_EROR          ( 1 <<  2 )

#define TOSHIBA_RBTX4927_IRQ_IOC_INIT      ( 1 << 10 )
#define TOSHIBA_RBTX4927_IRQ_IOC_ENABLE    ( 1 << 13 )
#define TOSHIBA_RBTX4927_IRQ_IOC_DISABLE   ( 1 << 14 )

#define TOSHIBA_RBTX4927_SETUP_ALL         0xffffffff
#endif


#ifdef TOSHIBA_RBTX4927_IRQ_DEBUG
static const u32 toshiba_rbtx4927_irq_debug_flag =
    (TOSHIBA_RBTX4927_IRQ_NONE | TOSHIBA_RBTX4927_IRQ_INFO |
     TOSHIBA_RBTX4927_IRQ_WARN | TOSHIBA_RBTX4927_IRQ_EROR
//                                                 | TOSHIBA_RBTX4927_IRQ_IOC_INIT
//                                                 | TOSHIBA_RBTX4927_IRQ_IOC_ENABLE
//                                                 | TOSHIBA_RBTX4927_IRQ_IOC_DISABLE
    );
#endif


#ifdef TOSHIBA_RBTX4927_IRQ_DEBUG
#define TOSHIBA_RBTX4927_IRQ_DPRINTK(flag,str...) \
        if ( (toshiba_rbtx4927_irq_debug_flag) & (flag) ) \
        { \
           char tmp[100]; \
           sprintf( tmp, str ); \
           printk( "%s(%s:%u)::%s", __FUNCTION__, __FILE__, __LINE__, tmp ); \
        }
#else
#define TOSHIBA_RBTX4927_IRQ_DPRINTK(flag, str...)
#endif




#define TOSHIBA_RBTX4927_IRQ_IOC_RAW_BEG   0
#define TOSHIBA_RBTX4927_IRQ_IOC_RAW_END   7

#define TOSHIBA_RBTX4927_IRQ_IOC_BEG  ((TX4927_IRQ_PIC_END+1)+TOSHIBA_RBTX4927_IRQ_IOC_RAW_BEG)	/* 56 */
#define TOSHIBA_RBTX4927_IRQ_IOC_END  ((TX4927_IRQ_PIC_END+1)+TOSHIBA_RBTX4927_IRQ_IOC_RAW_END)	/* 63 */

#define TOSHIBA_RBTX4927_IRQ_NEST_IOC_ON_PIC TX4927_IRQ_NEST_EXT_ON_PIC
#define TOSHIBA_RBTX4927_IRQ_NEST_ISA_ON_IOC (TOSHIBA_RBTX4927_IRQ_IOC_BEG+2)

extern int tx4927_using_backplane;

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
#define TOSHIBA_RBTX4927_IOC_INTR_ENAB (void __iomem *)0xbc002000UL
#define TOSHIBA_RBTX4927_IOC_INTR_STAT (void __iomem *)0xbc002006UL


u32 bit2num(u32 num)
{
	u32 i;

	for (i = 0; i < (sizeof(num) * 8); i++) {
		if (num & (1 << i)) {
			return (i);
		}
	}
	return (0);
}

int toshiba_rbtx4927_irq_nested(int sw_irq)
{
	u32 level3;

	level3 = readb(TOSHIBA_RBTX4927_IOC_INTR_STAT) & 0x1f;
	if (level3) {
		sw_irq = TOSHIBA_RBTX4927_IRQ_IOC_BEG + bit2num(level3);
		if (sw_irq != TOSHIBA_RBTX4927_IRQ_NEST_ISA_ON_IOC) {
			goto RETURN;
		}
	}
#ifdef CONFIG_TOSHIBA_FPCIB0
	if (tx4927_using_backplane) {
		int irq = i8259_irq();
		if (irq >= 0)
			sw_irq = irq;
	}
#endif

      RETURN:
	return (sw_irq);
}

static struct irqaction toshiba_rbtx4927_irq_ioc_action = {
	.handler	= no_action,
	.flags		= IRQF_SHARED,
	.mask		= CPU_MASK_NONE,
	.name		= TOSHIBA_RBTX4927_IOC_NAME
};


/**********************************************************************************/
/* Functions for ioc                                                              */
/**********************************************************************************/


static void __init toshiba_rbtx4927_irq_ioc_init(void)
{
	int i;

	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_IOC_INIT,
				     "beg=%d end=%d\n",
				     TOSHIBA_RBTX4927_IRQ_IOC_BEG,
				     TOSHIBA_RBTX4927_IRQ_IOC_END);

	for (i = TOSHIBA_RBTX4927_IRQ_IOC_BEG;
	     i <= TOSHIBA_RBTX4927_IRQ_IOC_END; i++)
		set_irq_chip_and_handler(i, &toshiba_rbtx4927_irq_ioc_type,
					 handle_level_irq);

	setup_irq(TOSHIBA_RBTX4927_IRQ_NEST_IOC_ON_PIC,
		  &toshiba_rbtx4927_irq_ioc_action);
}

static void toshiba_rbtx4927_irq_ioc_enable(unsigned int irq)
{
	volatile unsigned char v;

	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_IOC_ENABLE,
				     "irq=%d\n", irq);

	if (irq < TOSHIBA_RBTX4927_IRQ_IOC_BEG
	    || irq > TOSHIBA_RBTX4927_IRQ_IOC_END) {
		TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_EROR,
					     "bad irq=%d\n", irq);
		panic("\n");
	}

	v = readb(TOSHIBA_RBTX4927_IOC_INTR_ENAB);
	v |= (1 << (irq - TOSHIBA_RBTX4927_IRQ_IOC_BEG));
	writeb(v, TOSHIBA_RBTX4927_IOC_INTR_ENAB);
}


static void toshiba_rbtx4927_irq_ioc_disable(unsigned int irq)
{
	volatile unsigned char v;

	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_IOC_DISABLE,
				     "irq=%d\n", irq);

	if (irq < TOSHIBA_RBTX4927_IRQ_IOC_BEG
	    || irq > TOSHIBA_RBTX4927_IRQ_IOC_END) {
		TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_EROR,
					     "bad irq=%d\n", irq);
		panic("\n");
	}

	v = readb(TOSHIBA_RBTX4927_IOC_INTR_ENAB);
	v &= ~(1 << (irq - TOSHIBA_RBTX4927_IRQ_IOC_BEG));
	writeb(v, TOSHIBA_RBTX4927_IOC_INTR_ENAB);
	mmiowb();
}


void __init arch_init_irq(void)
{
	extern void tx4927_irq_init(void);

	tx4927_irq_init();
	toshiba_rbtx4927_irq_ioc_init();
#ifdef CONFIG_TOSHIBA_FPCIB0
	if (tx4927_using_backplane)
		init_i8259_irqs();
#endif
	/* Onboard 10M Ether: High Active */
	set_irq_type(RBTX4927_RTL_8019_IRQ, IRQF_TRIGGER_HIGH);

	wbflush();
}

void toshiba_rbtx4927_irq_dump(char *key)
{
#ifdef TOSHIBA_RBTX4927_IRQ_DEBUG
	{
		u32 i, j = 0;
		for (i = 0; i < NR_IRQS; i++) {
			if (strcmp(irq_desc[i].chip->name, "none")
			    == 0)
				continue;

			if ((i >= 1)
			    && (irq_desc[i - 1].chip->name ==
				irq_desc[i].chip->name)) {
				j++;
			} else {
				j = 0;
			}
			TOSHIBA_RBTX4927_IRQ_DPRINTK
			    (TOSHIBA_RBTX4927_IRQ_INFO,
			     "%s irq=0x%02x/%3d s=0x%08x h=0x%08x a=0x%08x ah=0x%08x d=%1d n=%s/%02d\n",
			     key, i, i, irq_desc[i].status,
			     (u32) irq_desc[i].chip,
			     (u32) irq_desc[i].action,
			     (u32) (irq_desc[i].action ? irq_desc[i].
				    action->handler : 0),
			     irq_desc[i].depth,
			     irq_desc[i].chip->name, j);
		}
	}
#endif
}

void toshiba_rbtx4927_irq_dump_pics(char *s)
{
	u32 level0_m;
	u32 level0_s;
	u32 level1_m;
	u32 level1_s;
	u32 level2;
	u32 level2_p;
	u32 level2_s;
	u32 level3_m;
	u32 level3_s;
	u32 level4_m;
	u32 level4_s;
	u32 level5_m;
	u32 level5_s;

	if (s == NULL)
		s = "null";

	level0_m = (read_c0_status() & 0x0000ff00) >> 8;
	level0_s = (read_c0_cause() & 0x0000ff00) >> 8;

	level1_m = level0_m;
	level1_s = level0_s & 0x87;

	level2 = __raw_readl((void __iomem *)0xff1ff6a0UL);
	level2_p = (((level2 & 0x10000)) ? 0 : 1);
	level2_s = (((level2 & 0x1f) == 0x1f) ? 0 : (level2 & 0x1f));

	level3_m = readb(TOSHIBA_RBTX4927_IOC_INTR_ENAB) & 0x1f;
	level3_s = readb(TOSHIBA_RBTX4927_IOC_INTR_STAT) & 0x1f;

	level4_m = inb(0x21);
	outb(0x0A, 0x20);
	level4_s = inb(0x20);

	level5_m = inb(0xa1);
	outb(0x0A, 0xa0);
	level5_s = inb(0xa0);

	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO,
				     "dump_raw_pic() ");
	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO,
				     "cp0:m=0x%02x/s=0x%02x ", level0_m,
				     level0_s);
	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO,
				     "cp0:m=0x%02x/s=0x%02x ", level1_m,
				     level1_s);
	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO,
				     "pic:e=0x%02x/s=0x%02x ", level2_p,
				     level2_s);
	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO,
				     "ioc:m=0x%02x/s=0x%02x ", level3_m,
				     level3_s);
	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO,
				     "sbm:m=0x%02x/s=0x%02x ", level4_m,
				     level4_s);
	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO,
				     "sbs:m=0x%02x/s=0x%02x ", level5_m,
				     level5_s);
	TOSHIBA_RBTX4927_IRQ_DPRINTK(TOSHIBA_RBTX4927_IRQ_INFO, "[%s]\n",
				     s);
}
