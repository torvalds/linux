/*
 * Toshiba rbtx4927 specific setup
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 * Copyright (C) 1996, 97, 2001, 04  Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2002 MontaVista Software Inc.
 * Author: Michael Pruznick, michael_pruznick@mvista.com
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * Copyright (C) 2004 MontaVista Software Inc.
 * Author: Manish Lachwani, mlachwani@mvista.com
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
#include <linux/pm.h>
#include <linux/platform_device.h>

#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <linux/bootmem.h>
#include <linux/blkdev.h>
#ifdef CONFIG_TOSHIBA_FPCIB0
#include <asm/tx4927/smsc_fdc37m81x.h>
#endif
#include <asm/tx4927/toshiba_rbtx4927.h>
#ifdef CONFIG_PCI
#include <asm/tx4927/tx4927_pci.h>
#endif
#ifdef CONFIG_BLK_DEV_IDEPCI
#include <linux/hdreg.h>
#include <linux/ide.h>
#endif
#ifdef CONFIG_SERIAL_TXX9
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#endif

#undef TOSHIBA_RBTX4927_SETUP_DEBUG

#ifdef TOSHIBA_RBTX4927_SETUP_DEBUG
#define TOSHIBA_RBTX4927_SETUP_NONE        0x00000000

#define TOSHIBA_RBTX4927_SETUP_INFO        ( 1 <<  0 )
#define TOSHIBA_RBTX4927_SETUP_WARN        ( 1 <<  1 )
#define TOSHIBA_RBTX4927_SETUP_EROR        ( 1 <<  2 )

#define TOSHIBA_RBTX4927_SETUP_EFWFU       ( 1 <<  3 )
#define TOSHIBA_RBTX4927_SETUP_SETUP       ( 1 <<  4 )
#define TOSHIBA_RBTX4927_SETUP_TIME_INIT   ( 1 <<  5 )
#define TOSHIBA_RBTX4927_SETUP_TIMER_SETUP ( 1 <<  6 )
#define TOSHIBA_RBTX4927_SETUP_PCIBIOS     ( 1 <<  7 )
#define TOSHIBA_RBTX4927_SETUP_PCI1        ( 1 <<  8 )
#define TOSHIBA_RBTX4927_SETUP_PCI2        ( 1 <<  9 )
#define TOSHIBA_RBTX4927_SETUP_PCI66       ( 1 << 10 )

#define TOSHIBA_RBTX4927_SETUP_ALL         0xffffffff
#endif

#ifdef TOSHIBA_RBTX4927_SETUP_DEBUG
static const u32 toshiba_rbtx4927_setup_debug_flag =
    (TOSHIBA_RBTX4927_SETUP_NONE | TOSHIBA_RBTX4927_SETUP_INFO |
     TOSHIBA_RBTX4927_SETUP_WARN | TOSHIBA_RBTX4927_SETUP_EROR |
     TOSHIBA_RBTX4927_SETUP_EFWFU | TOSHIBA_RBTX4927_SETUP_SETUP |
     TOSHIBA_RBTX4927_SETUP_TIME_INIT | TOSHIBA_RBTX4927_SETUP_TIMER_SETUP
     | TOSHIBA_RBTX4927_SETUP_PCIBIOS | TOSHIBA_RBTX4927_SETUP_PCI1 |
     TOSHIBA_RBTX4927_SETUP_PCI2 | TOSHIBA_RBTX4927_SETUP_PCI66);
#endif

#ifdef TOSHIBA_RBTX4927_SETUP_DEBUG
#define TOSHIBA_RBTX4927_SETUP_DPRINTK(flag,str...) \
        if ( (toshiba_rbtx4927_setup_debug_flag) & (flag) ) \
        { \
           char tmp[100]; \
           sprintf( tmp, str ); \
           printk( "%s(%s:%u)::%s", __FUNCTION__, __FILE__, __LINE__, tmp ); \
        }
#else
#define TOSHIBA_RBTX4927_SETUP_DPRINTK(flag,str...)
#endif

/* These functions are used for rebooting or halting the machine*/
extern void toshiba_rbtx4927_restart(char *command);
extern void toshiba_rbtx4927_halt(void);
extern void toshiba_rbtx4927_power_off(void);

int tx4927_using_backplane = 0;

extern void gt64120_time_init(void);
extern void toshiba_rbtx4927_irq_setup(void);

char *prom_getcmdline(void);

#ifdef CONFIG_PCI
#undef TX4927_SUPPORT_COMMAND_IO
#undef  TX4927_SUPPORT_PCI_66
int tx4927_cpu_clock = 100000000;	/* 100MHz */
unsigned long mips_pci_io_base;
unsigned long mips_pci_io_size;
unsigned long mips_pci_mem_base;
unsigned long mips_pci_mem_size;
/* for legacy I/O, PCI I/O PCI Bus address must be 0 */
unsigned long mips_pci_io_pciaddr = 0;
unsigned long mips_memory_upper;
static int tx4927_ccfg_toeon = 1;
static int tx4927_pcic_trdyto = 0;	/* default: disabled */
unsigned long tx4927_ce_base[8];
void tx4927_pci_setup(void);
void tx4927_reset_pci_pcic(void);
int tx4927_pci66 = 0;		/* 0:auto */
#endif

char *toshiba_name = "";

#ifdef CONFIG_PCI
static void tx4927_pcierr_interrupt(int irq, void *dev_id)
{
#ifdef CONFIG_BLK_DEV_IDEPCI
	/* ignore MasterAbort for ide probing... */
	if (irq == TX4927_IRQ_IRC_PCIERR &&
	    ((tx4927_pcicptr->pcistatus >> 16) & 0xf900) ==
	    PCI_STATUS_REC_MASTER_ABORT) {
		tx4927_pcicptr->pcistatus =
		    (tx4927_pcicptr->
		     pcistatus & 0x0000ffff) | (PCI_STATUS_REC_MASTER_ABORT
						<< 16);

		return;
	}
#endif
	printk("PCI error interrupt (irq 0x%x).\n", irq);

	printk("pcistat:%04x, g2pstatus:%08lx, pcicstatus:%08lx\n",
	       (unsigned short) (tx4927_pcicptr->pcistatus >> 16),
	       tx4927_pcicptr->g2pstatus, tx4927_pcicptr->pcicstatus);
	printk("ccfg:%08lx, tear:%02lx_%08lx\n",
	       (unsigned long) tx4927_ccfgptr->ccfg,
	       (unsigned long) (tx4927_ccfgptr->tear >> 32),
	       (unsigned long) tx4927_ccfgptr->tear);
	show_regs(get_irq_regs());
}

void __init toshiba_rbtx4927_pci_irq_init(void)
{
	return;
}

void tx4927_reset_pci_pcic(void)
{
	/* Reset PCI Bus */
	*tx4927_pcireset_ptr = 1;
	/* Reset PCIC */
	tx4927_ccfgptr->clkctr |= TX4927_CLKCTR_PCIRST;
	udelay(10000);
	/* clear PCIC reset */
	tx4927_ccfgptr->clkctr &= ~TX4927_CLKCTR_PCIRST;
	*tx4927_pcireset_ptr = 0;
}
#endif /* CONFIG_PCI */

#ifdef CONFIG_PCI
void print_pci_status(void)
{
	printk("PCI STATUS %lx\n", tx4927_pcicptr->pcistatus);
	printk("PCIC STATUS %lx\n", tx4927_pcicptr->pcicstatus);
}

extern struct pci_controller tx4927_controller;

static struct pci_dev *fake_pci_dev(struct pci_controller *hose,
				    int top_bus, int busnr, int devfn)
{
	static struct pci_dev dev;
	static struct pci_bus bus;

	dev.sysdata = (void *)hose;
	dev.devfn = devfn;
	bus.number = busnr;
	bus.ops = hose->pci_ops;
	bus.parent = NULL;
	dev.bus = &bus;

	return &dev;
}

#define EARLY_PCI_OP(rw, size, type)                                    \
static int early_##rw##_config_##size(struct pci_controller *hose,      \
        int top_bus, int bus, int devfn, int offset, type value)        \
{                                                                       \
        return pci_##rw##_config_##size(                                \
                fake_pci_dev(hose, top_bus, bus, devfn),                \
                offset, value);                                         \
}

EARLY_PCI_OP(read, byte, u8 *)
EARLY_PCI_OP(read, word, u16 *)
EARLY_PCI_OP(read, dword, u32 *)
EARLY_PCI_OP(write, byte, u8)
EARLY_PCI_OP(write, word, u16)
EARLY_PCI_OP(write, dword, u32)

static int __init tx4927_pcibios_init(void)
{
	unsigned int id;
	u32 pci_devfn;
	int devfn_start = 0;
	int devfn_stop = 0xff;
	int busno = 0; /* One bus on the Toshiba */
	struct pci_controller *hose = &tx4927_controller;

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				       "-\n");

	for (pci_devfn = devfn_start; pci_devfn < devfn_stop; pci_devfn++) {
		early_read_config_dword(hose, busno, busno, pci_devfn,
					PCI_VENDOR_ID, &id);

		if (id == 0xffffffff) {
			continue;
		}

		if (id == 0x94601055) {
			u8 v08_64;
			u32 v32_b0;
			u8 v08_e1;
			char *s = " sb/isa --";

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS, ":%s beg\n",
			     s);

			early_read_config_byte(hose, busno, busno,
					       pci_devfn, 0x64, &v08_64);
			early_read_config_dword(hose, busno, busno,
						pci_devfn, 0xb0, &v32_b0);
			early_read_config_byte(hose, busno, busno,
					       pci_devfn, 0xe1, &v08_e1);

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0x64 = 0x%02x\n", s, v08_64);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0xb0 = 0x%02x\n", s, v32_b0);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0xe1 = 0x%02x\n", s, v08_e1);

			/* serial irq control */
			v08_64 = 0xd0;

			/* serial irq pin */
			v32_b0 |= 0x00010000;

			/* ide irq on isa14 */
			v08_e1 &= 0xf0;
			v08_e1 |= 0x0d;

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0x64 = 0x%02x\n", s, v08_64);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0xb0 = 0x%02x\n", s, v32_b0);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0xe1 = 0x%02x\n", s, v08_e1);

			early_write_config_byte(hose, busno, busno,
						pci_devfn, 0x64, v08_64);
			early_write_config_dword(hose, busno, busno,
						 pci_devfn, 0xb0, v32_b0);
			early_write_config_byte(hose, busno, busno,
						pci_devfn, 0xe1, v08_e1);

#ifdef TOSHIBA_RBTX4927_SETUP_DEBUG
			{
				early_read_config_byte(hose, busno, busno,
						       pci_devfn, 0x64,
						       &v08_64);
				early_read_config_dword(hose, busno, busno,
							pci_devfn, 0xb0,
							&v32_b0);
				early_read_config_byte(hose, busno, busno,
						       pci_devfn, 0xe1,
						       &v08_e1);

				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0x64 = 0x%02x\n", s, v08_64);
				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0xb0 = 0x%02x\n", s, v32_b0);
				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0xe1 = 0x%02x\n", s, v08_e1);
			}
#endif

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS, ":%s end\n",
			     s);
		}

		if (id == 0x91301055) {
			u8 v08_04;
			u8 v08_09;
			u8 v08_41;
			u8 v08_43;
			u8 v08_5c;
			char *s = " sb/ide --";

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS, ":%s beg\n",
			     s);

			early_read_config_byte(hose, busno, busno,
					       pci_devfn, 0x04, &v08_04);
			early_read_config_byte(hose, busno, busno,
					       pci_devfn, 0x09, &v08_09);
			early_read_config_byte(hose, busno, busno,
					       pci_devfn, 0x41, &v08_41);
			early_read_config_byte(hose, busno, busno,
					       pci_devfn, 0x43, &v08_43);
			early_read_config_byte(hose, busno, busno,
					       pci_devfn, 0x5c, &v08_5c);

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0x04 = 0x%02x\n", s, v08_04);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0x09 = 0x%02x\n", s, v08_09);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0x41 = 0x%02x\n", s, v08_41);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0x43 = 0x%02x\n", s, v08_43);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s beg 0x5c = 0x%02x\n", s, v08_5c);

			/* enable ide master/io */
			v08_04 |= (PCI_COMMAND_MASTER | PCI_COMMAND_IO);

			/* enable ide native mode */
			v08_09 |= 0x05;

			/* enable primary ide */
			v08_41 |= 0x80;

			/* enable secondary ide */
			v08_43 |= 0x80;

			/*
			 * !!! DO NOT REMOVE THIS COMMENT IT IS REQUIRED BY SMSC !!!
			 *
			 * This line of code is intended to provide the user with a work
			 * around solution to the anomalies cited in SMSC's anomaly sheet
			 * entitled, "SLC90E66 Functional Rev.J_0.1 Anomalies"".
			 *
			 * !!! DO NOT REMOVE THIS COMMENT IT IS REQUIRED BY SMSC !!!
			 */
			v08_5c |= 0x01;

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0x04 = 0x%02x\n", s, v08_04);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0x09 = 0x%02x\n", s, v08_09);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0x41 = 0x%02x\n", s, v08_41);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0x43 = 0x%02x\n", s, v08_43);
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
			     ":%s mid 0x5c = 0x%02x\n", s, v08_5c);

			early_write_config_byte(hose, busno, busno,
						pci_devfn, 0x5c, v08_5c);
			early_write_config_byte(hose, busno, busno,
						pci_devfn, 0x04, v08_04);
			early_write_config_byte(hose, busno, busno,
						pci_devfn, 0x09, v08_09);
			early_write_config_byte(hose, busno, busno,
						pci_devfn, 0x41, v08_41);
			early_write_config_byte(hose, busno, busno,
						pci_devfn, 0x43, v08_43);

#ifdef TOSHIBA_RBTX4927_SETUP_DEBUG
			{
				early_read_config_byte(hose, busno, busno,
						       pci_devfn, 0x04,
						       &v08_04);
				early_read_config_byte(hose, busno, busno,
						       pci_devfn, 0x09,
						       &v08_09);
				early_read_config_byte(hose, busno, busno,
						       pci_devfn, 0x41,
						       &v08_41);
				early_read_config_byte(hose, busno, busno,
						       pci_devfn, 0x43,
						       &v08_43);
				early_read_config_byte(hose, busno, busno,
						       pci_devfn, 0x5c,
						       &v08_5c);

				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0x04 = 0x%02x\n", s, v08_04);
				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0x09 = 0x%02x\n", s, v08_09);
				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0x41 = 0x%02x\n", s, v08_41);
				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0x43 = 0x%02x\n", s, v08_43);
				TOSHIBA_RBTX4927_SETUP_DPRINTK
				    (TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				     ":%s end 0x5c = 0x%02x\n", s, v08_5c);
			}
#endif

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_PCIBIOS, ":%s end\n",
			     s);
		}

	}

	register_pci_controller(&tx4927_controller);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCIBIOS,
				       "+\n");

	return 0;
}

arch_initcall(tx4927_pcibios_init);

extern struct resource pci_io_resource;
extern struct resource pci_mem_resource;

void tx4927_pci_setup(void)
{
	static int called = 0;
	extern unsigned int tx4927_get_mem_size(void);

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2, "-\n");

	mips_memory_upper = tx4927_get_mem_size() << 20;
	mips_memory_upper += KSEG0;
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=mips_memory_upper\n",
				       mips_memory_upper);
	mips_pci_io_base = TX4927_PCIIO;
	mips_pci_io_size = TX4927_PCIIO_SIZE;
	mips_pci_mem_base = TX4927_PCIMEM;
	mips_pci_mem_size = TX4927_PCIMEM_SIZE;

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=mips_pci_io_base\n",
				       mips_pci_io_base);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=mips_pci_io_size\n",
				       mips_pci_io_size);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=mips_pci_mem_base\n",
				       mips_pci_mem_base);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=mips_pci_mem_size\n",
				       mips_pci_mem_size);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=pci_io_resource.start\n",
				       pci_io_resource.start);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=pci_io_resource.end\n",
				       pci_io_resource.end);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=pci_mem_resource.start\n",
				       pci_mem_resource.start);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=pci_mem_resource.end\n",
				       pci_mem_resource.end);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       "0x%08lx=mips_io_port_base",
				       mips_io_port_base);
	if (!called) {
		printk
		    ("%s PCIC -- DID:%04x VID:%04x RID:%02x Arbiter:%s\n",
		     toshiba_name,
		     (unsigned short) (tx4927_pcicptr->pciid >> 16),
		     (unsigned short) (tx4927_pcicptr->pciid & 0xffff),
		     (unsigned short) (tx4927_pcicptr->pciccrev & 0xff),
		     (!(tx4927_ccfgptr->
			ccfg & TX4927_CCFG_PCIXARB)) ? "External" :
		     "Internal");
		called = 1;
	}
	printk("%s PCIC --%s PCICLK:",toshiba_name,
	       (tx4927_ccfgptr->ccfg & TX4927_CCFG_PCI66) ? " PCI66" : "");
	if (tx4927_ccfgptr->pcfg & TX4927_PCFG_PCICLKEN_ALL) {
		int pciclk = 0;
		if (mips_machtype == MACH_TOSHIBA_RBTX4937)
			switch ((unsigned long) tx4927_ccfgptr->
				ccfg & TX4937_CCFG_PCIDIVMODE_MASK) {
			case TX4937_CCFG_PCIDIVMODE_4:
				pciclk = tx4927_cpu_clock / 4;
				break;
			case TX4937_CCFG_PCIDIVMODE_4_5:
				pciclk = tx4927_cpu_clock * 2 / 9;
				break;
			case TX4937_CCFG_PCIDIVMODE_5:
				pciclk = tx4927_cpu_clock / 5;
				break;
			case TX4937_CCFG_PCIDIVMODE_5_5:
				pciclk = tx4927_cpu_clock * 2 / 11;
				break;
			case TX4937_CCFG_PCIDIVMODE_8:
				pciclk = tx4927_cpu_clock / 8;
				break;
			case TX4937_CCFG_PCIDIVMODE_9:
				pciclk = tx4927_cpu_clock / 9;
				break;
			case TX4937_CCFG_PCIDIVMODE_10:
				pciclk = tx4927_cpu_clock / 10;
				break;
			case TX4937_CCFG_PCIDIVMODE_11:
				pciclk = tx4927_cpu_clock / 11;
				break;
			}

		else
			switch ((unsigned long) tx4927_ccfgptr->
				ccfg & TX4927_CCFG_PCIDIVMODE_MASK) {
			case TX4927_CCFG_PCIDIVMODE_2_5:
				pciclk = tx4927_cpu_clock * 2 / 5;
				break;
			case TX4927_CCFG_PCIDIVMODE_3:
				pciclk = tx4927_cpu_clock / 3;
				break;
			case TX4927_CCFG_PCIDIVMODE_5:
				pciclk = tx4927_cpu_clock / 5;
				break;
			case TX4927_CCFG_PCIDIVMODE_6:
				pciclk = tx4927_cpu_clock / 6;
				break;
			}

		printk("Internal(%dMHz)", pciclk / 1000000);
	} else {
		int pciclk = 0;
		int pciclk_setting = *tx4927_pci_clk_ptr;
		switch (pciclk_setting & TX4927_PCI_CLK_MASK) {
		case TX4927_PCI_CLK_33:
			pciclk = 33333333;
			break;
		case TX4927_PCI_CLK_25:
			pciclk = 25000000;
			break;
		case TX4927_PCI_CLK_66:
			pciclk = 66666666;
			break;
		case TX4927_PCI_CLK_50:
			pciclk = 50000000;
			break;
		}
		printk("External(%dMHz)", pciclk / 1000000);
	}
	printk("\n");



	/* GB->PCI mappings */
	tx4927_pcicptr->g2piomask = (mips_pci_io_size - 1) >> 4;
	tx4927_pcicptr->g2piogbase = mips_pci_io_base |
#ifdef __BIG_ENDIAN
	    TX4927_PCIC_G2PIOGBASE_ECHG
#else
	    TX4927_PCIC_G2PIOGBASE_BSDIS
#endif
	    ;

	tx4927_pcicptr->g2piopbase = 0;

	tx4927_pcicptr->g2pmmask[0] = (mips_pci_mem_size - 1) >> 4;
	tx4927_pcicptr->g2pmgbase[0] = mips_pci_mem_base |
#ifdef __BIG_ENDIAN
	    TX4927_PCIC_G2PMnGBASE_ECHG
#else
	    TX4927_PCIC_G2PMnGBASE_BSDIS
#endif
	    ;
	tx4927_pcicptr->g2pmpbase[0] = mips_pci_mem_base;

	tx4927_pcicptr->g2pmmask[1] = 0;
	tx4927_pcicptr->g2pmgbase[1] = 0;
	tx4927_pcicptr->g2pmpbase[1] = 0;
	tx4927_pcicptr->g2pmmask[2] = 0;
	tx4927_pcicptr->g2pmgbase[2] = 0;
	tx4927_pcicptr->g2pmpbase[2] = 0;


	/* PCI->GB mappings (I/O 256B) */
	tx4927_pcicptr->p2giopbase = 0;	/* 256B */

	/* PCI->GB mappings (MEM 512MB) M0 gets all of memory */
	tx4927_pcicptr->p2gm0plbase = 0;
	tx4927_pcicptr->p2gm0pubase = 0;
	tx4927_pcicptr->p2gmgbase[0] = 0 | TX4927_PCIC_P2GMnGBASE_TMEMEN |
#ifdef __BIG_ENDIAN
	    TX4927_PCIC_P2GMnGBASE_TECHG
#else
	    TX4927_PCIC_P2GMnGBASE_TBSDIS
#endif
	    ;

	/* PCI->GB mappings (MEM 16MB) -not used */
	tx4927_pcicptr->p2gm1plbase = 0xffffffff;
	tx4927_pcicptr->p2gm1pubase = 0xffffffff;
	tx4927_pcicptr->p2gmgbase[1] = 0;

	/* PCI->GB mappings (MEM 1MB) -not used */
	tx4927_pcicptr->p2gm2pbase = 0xffffffff;
	tx4927_pcicptr->p2gmgbase[2] = 0;


	/* Enable Initiator Memory 0 Space, I/O Space, Config */
	tx4927_pcicptr->pciccfg &= TX4927_PCIC_PCICCFG_LBWC_MASK;
	tx4927_pcicptr->pciccfg |=
	    TX4927_PCIC_PCICCFG_IMSE0 | TX4927_PCIC_PCICCFG_IISE |
	    TX4927_PCIC_PCICCFG_ICAE | TX4927_PCIC_PCICCFG_ATR;


	/* Do not use MEMMUL, MEMINF: YMFPCI card causes M_ABORT. */
	tx4927_pcicptr->pcicfg1 = 0;

	if (tx4927_pcic_trdyto >= 0) {
		tx4927_pcicptr->g2ptocnt &= ~0xff;
		tx4927_pcicptr->g2ptocnt |= (tx4927_pcic_trdyto & 0xff);
	}

	/* Clear All Local Bus Status */
	tx4927_pcicptr->pcicstatus = TX4927_PCIC_PCICSTATUS_ALL;
	/* Enable All Local Bus Interrupts */
	tx4927_pcicptr->pcicmask = TX4927_PCIC_PCICSTATUS_ALL;
	/* Clear All Initiator Status */
	tx4927_pcicptr->g2pstatus = TX4927_PCIC_G2PSTATUS_ALL;
	/* Enable All Initiator Interrupts */
	tx4927_pcicptr->g2pmask = TX4927_PCIC_G2PSTATUS_ALL;
	/* Clear All PCI Status Error */
	tx4927_pcicptr->pcistatus =
	    (tx4927_pcicptr->pcistatus & 0x0000ffff) |
	    (TX4927_PCIC_PCISTATUS_ALL << 16);
	/* Enable All PCI Status Error Interrupts */
	tx4927_pcicptr->pcimask = TX4927_PCIC_PCISTATUS_ALL;

	/* PCIC Int => IRC IRQ16 */
	tx4927_pcicptr->pcicfg2 =
	    (tx4927_pcicptr->pcicfg2 & 0xffffff00) | TX4927_IR_PCIC;

	if (!(tx4927_ccfgptr->ccfg & TX4927_CCFG_PCIXARB)) {
		/* XXX */
	} else {
		/* Reset Bus Arbiter */
		tx4927_pcicptr->pbacfg = TX4927_PCIC_PBACFG_RPBA;
		/* Enable Bus Arbiter */
		tx4927_pcicptr->pbacfg = TX4927_PCIC_PBACFG_PBAEN;
	}

	tx4927_pcicptr->pcistatus = PCI_COMMAND_MASTER |
	    PCI_COMMAND_MEMORY |
	    PCI_COMMAND_PARITY | PCI_COMMAND_SERR;

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2,
				       ":pci setup complete:\n");
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI2, "+\n");
}

#endif /* CONFIG_PCI */

void toshiba_rbtx4927_restart(char *command)
{
	printk(KERN_NOTICE "System Rebooting...\n");

	/* enable the s/w reset register */
	reg_wr08(RBTX4927_SW_RESET_ENABLE, RBTX4927_SW_RESET_ENABLE_SET);

	/* wait for enable to be seen */
	while ((reg_rd08(RBTX4927_SW_RESET_ENABLE) &
		RBTX4927_SW_RESET_ENABLE_SET) == 0x00);

	/* do a s/w reset */
	reg_wr08(RBTX4927_SW_RESET_DO, RBTX4927_SW_RESET_DO_SET);

	/* do something passive while waiting for reset */
	local_irq_disable();
	while (1)
		asm_wait();

	/* no return */
}


void toshiba_rbtx4927_halt(void)
{
	printk(KERN_NOTICE "System Halted\n");
	local_irq_disable();
	while (1) {
		asm_wait();
	}
	/* no return */
}

void toshiba_rbtx4927_power_off(void)
{
	toshiba_rbtx4927_halt();
	/* no return */
}

void __init toshiba_rbtx4927_setup(void)
{
	vu32 cp0_config;
	char *argptr;

	printk("CPU is %s\n", toshiba_name);

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       "-\n");

	/* f/w leaves this on at startup */
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       ":Clearing STO_ERL.\n");
	clear_c0_status(ST0_ERL);

	/* enable caches -- HCP5 does this, pmon does not */
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       ":Enabling TX49_CONF_IC,TX49_CONF_DC.\n");
	cp0_config = read_c0_config();
	cp0_config = cp0_config & ~(TX49_CONF_IC | TX49_CONF_DC);
	write_c0_config(cp0_config);

#ifdef TOSHIBA_RBTX4927_SETUP_DEBUG
	{
		extern void dump_cp0(char *);
		dump_cp0("toshiba_rbtx4927_early_fw_fixup");
	}
#endif

	/* setup irq stuff */
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       ":Setting up tx4927 pic.\n");
	TX4927_WR(0xff1ff604, 0x00000400);	/* irq trigger */
	TX4927_WR(0xff1ff608, 0x00000000);	/* irq trigger */

	/* setup serial stuff */
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       ":Setting up tx4927 sio.\n");
	TX4927_WR(0xff1ff314, 0x00000000);	/* h/w flow control off */
	TX4927_WR(0xff1ff414, 0x00000000);	/* h/w flow control off */

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       "+\n");

	set_io_port_base(KSEG1 + TBTX4927_ISA_IO_OFFSET);
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       ":mips_io_port_base=0x%08lx\n",
				       mips_io_port_base);

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       ":Resource\n");
	ioport_resource.end = 0xffffffff;
	iomem_resource.end = 0xffffffff;

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
				       ":ResetRoutines\n");
	_machine_restart = toshiba_rbtx4927_restart;
	_machine_halt = toshiba_rbtx4927_halt;
	pm_power_off = toshiba_rbtx4927_power_off;

#ifdef CONFIG_PCI

	/* PCIC */
	/*
	   * ASSUMPTION: PCIDIVMODE is configured for PCI 33MHz or 66MHz.
	   *
	   * For TX4927:
	   * PCIDIVMODE[12:11]'s initial value is given by S9[4:3] (ON:0, OFF:1).
	   * CPU 166MHz: PCI 66MHz : PCIDIVMODE: 00 (1/2.5)
	   * CPU 200MHz: PCI 66MHz : PCIDIVMODE: 01 (1/3)
	   * CPU 166MHz: PCI 33MHz : PCIDIVMODE: 10 (1/5)
	   * CPU 200MHz: PCI 33MHz : PCIDIVMODE: 11 (1/6)
	   * i.e. S9[3]: ON (83MHz), OFF (100MHz)
	   *
	   * For TX4937:
	   * PCIDIVMODE[12:11]'s initial value is given by S1[5:4] (ON:0, OFF:1)
	   * PCIDIVMODE[10] is 0.
	   * CPU 266MHz: PCI 33MHz : PCIDIVMODE: 000 (1/8)
	   * CPU 266MHz: PCI 66MHz : PCIDIVMODE: 001 (1/4)
	   * CPU 300MHz: PCI 33MHz : PCIDIVMODE: 010 (1/9)
	   * CPU 300MHz: PCI 66MHz : PCIDIVMODE: 011 (1/4.5)
	   * CPU 333MHz: PCI 33MHz : PCIDIVMODE: 100 (1/10)
	   * CPU 333MHz: PCI 66MHz : PCIDIVMODE: 101 (1/5)
	   *
	 */
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI1,
				       "ccfg is %lx, PCIDIVMODE is %x\n",
				       (unsigned long) tx4927_ccfgptr->ccfg,
				       (unsigned long) tx4927_ccfgptr->ccfg &
				       (mips_machtype == MACH_TOSHIBA_RBTX4937 ?
					TX4937_CCFG_PCIDIVMODE_MASK :
					TX4927_CCFG_PCIDIVMODE_MASK));

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_PCI1,
				       "PCI66 mode is %lx, PCI mode is %lx, pci arb is %lx\n",
				       (unsigned long) tx4927_ccfgptr->
				       ccfg & TX4927_CCFG_PCI66,
				       (unsigned long) tx4927_ccfgptr->
				       ccfg & TX4927_CCFG_PCIMIDE,
				       (unsigned long) tx4927_ccfgptr->
				       ccfg & TX4927_CCFG_PCIXARB);

	if (mips_machtype == MACH_TOSHIBA_RBTX4937)
		switch ((unsigned long)tx4927_ccfgptr->
			ccfg & TX4937_CCFG_PCIDIVMODE_MASK) {
		case TX4937_CCFG_PCIDIVMODE_8:
		case TX4937_CCFG_PCIDIVMODE_4:
			tx4927_cpu_clock = 266666666;	/* 266MHz */
			break;
		case TX4937_CCFG_PCIDIVMODE_9:
		case TX4937_CCFG_PCIDIVMODE_4_5:
			tx4927_cpu_clock = 300000000;	/* 300MHz */
			break;
		default:
			tx4927_cpu_clock = 333333333;	/* 333MHz */
		}
	else
		switch ((unsigned long)tx4927_ccfgptr->
			ccfg & TX4927_CCFG_PCIDIVMODE_MASK) {
		case TX4927_CCFG_PCIDIVMODE_2_5:
		case TX4927_CCFG_PCIDIVMODE_5:
			tx4927_cpu_clock = 166666666;	/* 166MHz */
			break;
		default:
			tx4927_cpu_clock = 200000000;	/* 200MHz */
		}

	/* CCFG */
	/* enable Timeout BusError */
	if (tx4927_ccfg_toeon)
		tx4927_ccfgptr->ccfg |= TX4927_CCFG_TOE;

	tx4927_pci_setup();
	if (tx4927_using_backplane == 1)
		printk("backplane board IS installed\n");
	else
		printk("No Backplane \n");

	/* this is on ISA bus behind PCI bus, so need PCI up first */
#ifdef CONFIG_TOSHIBA_FPCIB0
	{
		if (tx4927_using_backplane) {
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_SETUP,
			     ":fpcibo=yes\n");

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_SETUP,
			     ":smsc_fdc37m81x_init()\n");
			smsc_fdc37m81x_init(0x3f0);

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_SETUP,
			     ":smsc_fdc37m81x_config_beg()\n");
			smsc_fdc37m81x_config_beg();

			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_SETUP,
			     ":smsc_fdc37m81x_config_set(KBD)\n");
			smsc_fdc37m81x_config_set(SMSC_FDC37M81X_DNUM,
						  SMSC_FDC37M81X_KBD);
			smsc_fdc37m81x_config_set(SMSC_FDC37M81X_INT, 1);
			smsc_fdc37m81x_config_set(SMSC_FDC37M81X_INT2, 12);
			smsc_fdc37m81x_config_set(SMSC_FDC37M81X_ACTIVE,
						  1);

			smsc_fdc37m81x_config_end();
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_SETUP,
			     ":smsc_fdc37m81x_config_end()\n");
		} else {
			TOSHIBA_RBTX4927_SETUP_DPRINTK
			    (TOSHIBA_RBTX4927_SETUP_SETUP,
			     ":fpcibo=not_found\n");
		}
	}
#else
	{
		TOSHIBA_RBTX4927_SETUP_DPRINTK
		    (TOSHIBA_RBTX4927_SETUP_SETUP, ":fpcibo=no\n");
	}
#endif

#endif /* CONFIG_PCI */

#ifdef CONFIG_SERIAL_TXX9
	{
		extern int early_serial_txx9_setup(struct uart_port *port);
		int i;
		struct uart_port req;
		for(i = 0; i < 2; i++) {
			memset(&req, 0, sizeof(req));
			req.line = i;
			req.iotype = UPIO_MEM;
			req.membase = (char *)(0xff1ff300 + i * 0x100);
			req.mapbase = 0xff1ff300 + i * 0x100;
			req.irq = 32 + i;
			req.flags |= UPF_BUGGY_UART /*HAVE_CTS_LINE*/;
			req.uartclk = 50000000;
			early_serial_txx9_setup(&req);
		}
	}
#ifdef CONFIG_SERIAL_TXX9_CONSOLE
        argptr = prom_getcmdline();
        if (strstr(argptr, "console=") == NULL) {
                strcat(argptr, " console=ttyS0,38400");
        }
#endif
#endif

#ifdef CONFIG_ROOT_NFS
        argptr = prom_getcmdline();
        if (strstr(argptr, "root=") == NULL) {
                strcat(argptr, " root=/dev/nfs rw");
        }
#endif


#ifdef CONFIG_IP_PNP
        argptr = prom_getcmdline();
        if (strstr(argptr, "ip=") == NULL) {
                strcat(argptr, " ip=any");
        }
#endif


	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_SETUP,
			       "+\n");
}

void __init
toshiba_rbtx4927_time_init(void)
{
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_TIME_INIT, "-\n");

	mips_hpt_frequency = tx4927_cpu_clock / 2;

	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_TIME_INIT, "+\n");

}

void __init toshiba_rbtx4927_timer_setup(struct irqaction *irq)
{
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_TIMER_SETUP,
				       "-\n");
	TOSHIBA_RBTX4927_SETUP_DPRINTK(TOSHIBA_RBTX4927_SETUP_TIMER_SETUP,
				       "+\n");
}

static int __init toshiba_rbtx4927_rtc_init(void)
{
	static struct resource __initdata res = {
		.start	= 0x1c010000,
		.end	= 0x1c010000 + 0x800 - 1,
		.flags	= IORESOURCE_MEM,
	};
	struct platform_device *dev =
		platform_device_register_simple("ds1742", -1, &res, 1);
	return IS_ERR(dev) ? PTR_ERR(dev) : 0;
}
device_initcall(toshiba_rbtx4927_rtc_init);

static int __init rbtx4927_ne_init(void)
{
	static struct resource __initdata res[] = {
		{
			.start	= RBTX4927_RTL_8019_BASE,
			.end	= RBTX4927_RTL_8019_BASE + 0x20 - 1,
			.flags	= IORESOURCE_IO,
		}, {
			.start	= RBTX4927_RTL_8019_IRQ,
			.flags	= IORESOURCE_IRQ,
		}
	};
	struct platform_device *dev =
		platform_device_register_simple("ne", -1,
						res, ARRAY_SIZE(res));
	return IS_ERR(dev) ? PTR_ERR(dev) : 0;
}
device_initcall(rbtx4927_ne_init);
