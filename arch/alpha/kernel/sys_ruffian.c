/*
 *	linux/arch/alpha/kernel/sys_ruffian.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999, 2000 Richard Henderson
 *
 * Code supporting the RUFFIAN.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_cia.h>
#include <asm/tlbflush.h>
#include <asm/8253pit.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


static void __init
ruffian_init_irq(void)
{
	/* Invert 6&7 for i82371 */
	*(vulp)PYXIS_INT_HILO  = 0x000000c0UL; mb();
	*(vulp)PYXIS_INT_CNFG  = 0x00002064UL; mb();	 /* all clear */

	outb(0x11,0xA0);
	outb(0x08,0xA1);
	outb(0x02,0xA1);
	outb(0x01,0xA1);
	outb(0xFF,0xA1);
	
	outb(0x11,0x20);
	outb(0x00,0x21);
	outb(0x04,0x21);
	outb(0x01,0x21);
	outb(0xFF,0x21);
	
	/* Finish writing the 82C59A PIC Operation Control Words */
	outb(0x20,0xA0);
	outb(0x20,0x20);
	
	init_i8259a_irqs();

	/* Not interested in the bogus interrupts (0,3,6),
	   NMI (1), HALT (2), flash (5), or 21142 (8).  */
	init_pyxis_irqs(0x16f0000);

	common_init_isa_dma();
}

#define RUFFIAN_LATCH	((PIT_TICK_RATE + HZ / 2) / HZ)

static void __init
ruffian_init_rtc(void)
{
	/* Ruffian does not have the RTC connected to the CPU timer
	   interrupt.  Instead, it uses the PIT connected to IRQ 0.  */

	/* Setup interval timer.  */
	outb(0x34, 0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb(RUFFIAN_LATCH & 0xff, 0x40);	/* LSB */
	outb(RUFFIAN_LATCH >> 8, 0x40);		/* MSB */

	outb(0xb6, 0x43);		/* pit counter 2: speaker */
	outb(0x31, 0x42);
	outb(0x13, 0x42);

	setup_irq(0, &timer_irqaction);
}

static void
ruffian_kill_arch (int mode)
{
	cia_kill_arch(mode);
#if 0
	/* This only causes re-entry to ARCSBIOS */
	/* Perhaps this works for other PYXIS as well?  */
	*(vuip) PYXIS_RESET = 0x0000dead;
	mb();
#endif
}

/*
 *  Interrupt routing:
 *
 *		Primary bus
 *	  IdSel		INTA	INTB	INTC	INTD
 * 21052   13		  -	  -	  -	  -
 * SIO	   14		 23	  -	  -	  -
 * 21143   15		 44	  -	  -	  -
 * Slot 0  17		 43	 42	 41	 40
 *
 *		Secondary bus
 *	  IdSel		INTA	INTB	INTC	INTD
 * Slot 0   8 (18)	 19	 18	 17	 16
 * Slot 1   9 (19)	 31	 30	 29	 28
 * Slot 2  10 (20)	 27	 26	 25	 24
 * Slot 3  11 (21)	 39	 38	 37	 36
 * Slot 4  12 (22)	 35	 34	 33	 32
 * 53c875  13 (23)	 20	  -	  -	  -
 *
 */

static int __init
ruffian_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
        static char irq_tab[11][5] __initdata = {
	      /*INT  INTA INTB INTC INTD */
		{-1,  -1,  -1,  -1,  -1},  /* IdSel 13,  21052	     */
		{-1,  -1,  -1,  -1,  -1},  /* IdSel 14,  SIO	     */
		{44,  44,  44,  44,  44},  /* IdSel 15,  21143	     */
		{-1,  -1,  -1,  -1,  -1},  /* IdSel 16,  none	     */
		{43,  43,  42,  41,  40},  /* IdSel 17,  64-bit slot */
		/* the next 6 are actually on PCI bus 1, across the bridge */
		{19,  19,  18,  17,  16},  /* IdSel  8,  slot 0	     */
		{31,  31,  30,  29,  28},  /* IdSel  9,  slot 1	     */
		{27,  27,  26,  25,  24},  /* IdSel 10,  slot 2	     */
		{39,  39,  38,  37,  36},  /* IdSel 11,  slot 3	     */
		{35,  35,  34,  33,  32},  /* IdSel 12,  slot 4	     */
		{20,  20,  20,  20,  20},  /* IdSel 13,  53c875	     */
        };
	const long min_idsel = 13, max_idsel = 23, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static u8 __init
ruffian_swizzle(struct pci_dev *dev, u8 *pinp)
{
	int slot, pin = *pinp;

	if (dev->bus->number == 0) {
		slot = PCI_SLOT(dev->devfn);
	}		
	/* Check for the built-in bridge.  */
	else if (PCI_SLOT(dev->bus->self->devfn) == 13) {
		slot = PCI_SLOT(dev->devfn) + 10;
	}
	else 
	{
		/* Must be a card-based bridge.  */
		do {
			if (PCI_SLOT(dev->bus->self->devfn) == 13) {
				slot = PCI_SLOT(dev->devfn) + 10;
				break;
			}
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));

			/* Move up the chain of bridges.  */
			dev = dev->bus->self;
			/* Slot of the next bridge.  */
			slot = PCI_SLOT(dev->devfn);
		} while (dev->bus->self);
	}
	*pinp = pin;
	return slot;
}

#ifdef BUILDING_FOR_MILO
/*
 * The DeskStation Ruffian motherboard firmware does not place
 * the memory size in the PALimpure area.  Therefore, we use
 * the Bank Configuration Registers in PYXIS to obtain the size.
 */
static unsigned long __init
ruffian_get_bank_size(unsigned long offset)
{
	unsigned long bank_addr, bank, ret = 0;

	/* Valid offsets are: 0x800, 0x840 and 0x880
	   since Ruffian only uses three banks.  */
	bank_addr = (unsigned long)PYXIS_MCR + offset;
	bank = *(vulp)bank_addr;

	/* Check BANK_ENABLE */
	if (bank & 0x01) {
		static unsigned long size[] __initdata = {
			0x40000000UL, /* 0x00,   1G */
			0x20000000UL, /* 0x02, 512M */
			0x10000000UL, /* 0x04, 256M */
			0x08000000UL, /* 0x06, 128M */
			0x04000000UL, /* 0x08,  64M */
			0x02000000UL, /* 0x0a,  32M */
			0x01000000UL, /* 0x0c,  16M */
			0x00800000UL, /* 0x0e,   8M */
			0x80000000UL, /* 0x10,   2G */
		};

		bank = (bank & 0x1e) >> 1;
		if (bank < ARRAY_SIZE(size))
			ret = size[bank];
	}

	return ret;
}
#endif /* BUILDING_FOR_MILO */

/*
 * The System Vector
 */

struct alpha_machine_vector ruffian_mv __initmv = {
	.vector_name		= "Ruffian",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	.machine_check		= cia_machine_check,
	.max_isa_dma_address	= ALPHA_RUFFIAN_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= DEFAULT_MEM_BASE,
	.pci_dac_offset		= PYXIS_DAC_OFFSET,

	.nr_irqs		= 48,
	.device_interrupt	= pyxis_device_interrupt,

	.init_arch		= pyxis_init_arch,
	.init_irq		= ruffian_init_irq,
	.init_rtc		= ruffian_init_rtc,
	.init_pci		= cia_init_pci,
	.kill_arch		= ruffian_kill_arch,
	.pci_map_irq		= ruffian_map_irq,
	.pci_swizzle		= ruffian_swizzle,
};
ALIAS_MV(ruffian)
