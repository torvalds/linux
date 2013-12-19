/*
 *  linux/arch/arm/mach-integrator/pci_v3.c
 *
 *  PCI functions for V3 host PCI bridge
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd
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
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <video/vga.h>

#include <mach/hardware.h>
#include <mach/platform.h>

#include <asm/mach/map.h>
#include <asm/signal.h>
#include <asm/mach/pci.h>
#include <asm/irq_regs.h>

#include "pci_v3.h"

/*
 * Where in the memory map does PCI live?
 *
 * This represents a fairly liberal usage of address space.  Even though
 * the V3 only has two windows (therefore we need to map stuff on the fly),
 * we maintain the same addresses, even if they're not mapped.
 */
#define PHYS_PCI_MEM_BASE               0x40000000 /* 256M */
#define PHYS_PCI_PRE_BASE               0x50000000 /* 256M */
#define PHYS_PCI_IO_BASE                0x60000000 /* 16M */
#define PHYS_PCI_CONFIG_BASE            0x61000000 /* 16M */
#define PHYS_PCI_V3_BASE                0x62000000 /* 64K */

#define PCI_MEMORY_VADDR               IOMEM(0xe8000000)
#define PCI_CONFIG_VADDR               IOMEM(0xec000000)

/*
 * V3 Local Bus to PCI Bridge definitions
 *
 * Registers (these are taken from page 129 of the EPC User's Manual Rev 1.04
 * All V3 register names are prefaced by V3_ to avoid clashing with any other
 * PCI definitions.  Their names match the user's manual.
 *
 * I'm assuming that I20 is disabled.
 *
 */
#define V3_PCI_VENDOR                   0x00000000
#define V3_PCI_DEVICE                   0x00000002
#define V3_PCI_CMD                      0x00000004
#define V3_PCI_STAT                     0x00000006
#define V3_PCI_CC_REV                   0x00000008
#define V3_PCI_HDR_CFG                  0x0000000C
#define V3_PCI_IO_BASE                  0x00000010
#define V3_PCI_BASE0                    0x00000014
#define V3_PCI_BASE1                    0x00000018
#define V3_PCI_SUB_VENDOR               0x0000002C
#define V3_PCI_SUB_ID                   0x0000002E
#define V3_PCI_ROM                      0x00000030
#define V3_PCI_BPARAM                   0x0000003C
#define V3_PCI_MAP0                     0x00000040
#define V3_PCI_MAP1                     0x00000044
#define V3_PCI_INT_STAT                 0x00000048
#define V3_PCI_INT_CFG                  0x0000004C
#define V3_LB_BASE0                     0x00000054
#define V3_LB_BASE1                     0x00000058
#define V3_LB_MAP0                      0x0000005E
#define V3_LB_MAP1                      0x00000062
#define V3_LB_BASE2                     0x00000064
#define V3_LB_MAP2                      0x00000066
#define V3_LB_SIZE                      0x00000068
#define V3_LB_IO_BASE                   0x0000006E
#define V3_FIFO_CFG                     0x00000070
#define V3_FIFO_PRIORITY                0x00000072
#define V3_FIFO_STAT                    0x00000074
#define V3_LB_ISTAT                     0x00000076
#define V3_LB_IMASK                     0x00000077
#define V3_SYSTEM                       0x00000078
#define V3_LB_CFG                       0x0000007A
#define V3_PCI_CFG                      0x0000007C
#define V3_DMA_PCI_ADR0                 0x00000080
#define V3_DMA_PCI_ADR1                 0x00000090
#define V3_DMA_LOCAL_ADR0               0x00000084
#define V3_DMA_LOCAL_ADR1               0x00000094
#define V3_DMA_LENGTH0                  0x00000088
#define V3_DMA_LENGTH1                  0x00000098
#define V3_DMA_CSR0                     0x0000008B
#define V3_DMA_CSR1                     0x0000009B
#define V3_DMA_CTLB_ADR0                0x0000008C
#define V3_DMA_CTLB_ADR1                0x0000009C
#define V3_DMA_DELAY                    0x000000E0
#define V3_MAIL_DATA                    0x000000C0
#define V3_PCI_MAIL_IEWR                0x000000D0
#define V3_PCI_MAIL_IERD                0x000000D2
#define V3_LB_MAIL_IEWR                 0x000000D4
#define V3_LB_MAIL_IERD                 0x000000D6
#define V3_MAIL_WR_STAT                 0x000000D8
#define V3_MAIL_RD_STAT                 0x000000DA
#define V3_QBA_MAP                      0x000000DC

/*  PCI COMMAND REGISTER bits
 */
#define V3_COMMAND_M_FBB_EN             (1 << 9)
#define V3_COMMAND_M_SERR_EN            (1 << 8)
#define V3_COMMAND_M_PAR_EN             (1 << 6)
#define V3_COMMAND_M_MASTER_EN          (1 << 2)
#define V3_COMMAND_M_MEM_EN             (1 << 1)
#define V3_COMMAND_M_IO_EN              (1 << 0)

/*  SYSTEM REGISTER bits
 */
#define V3_SYSTEM_M_RST_OUT             (1 << 15)
#define V3_SYSTEM_M_LOCK                (1 << 14)

/*  PCI_CFG bits
 */
#define V3_PCI_CFG_M_I2O_EN		(1 << 15)
#define V3_PCI_CFG_M_IO_REG_DIS		(1 << 14)
#define V3_PCI_CFG_M_IO_DIS		(1 << 13)
#define V3_PCI_CFG_M_EN3V		(1 << 12)
#define V3_PCI_CFG_M_RETRY_EN           (1 << 10)
#define V3_PCI_CFG_M_AD_LOW1            (1 << 9)
#define V3_PCI_CFG_M_AD_LOW0            (1 << 8)

/*  PCI_BASE register bits (PCI -> Local Bus)
 */
#define V3_PCI_BASE_M_ADR_BASE          0xFFF00000
#define V3_PCI_BASE_M_ADR_BASEL         0x000FFF00
#define V3_PCI_BASE_M_PREFETCH          (1 << 3)
#define V3_PCI_BASE_M_TYPE              (3 << 1)
#define V3_PCI_BASE_M_IO                (1 << 0)

/*  PCI MAP register bits (PCI -> Local bus)
 */
#define V3_PCI_MAP_M_MAP_ADR            0xFFF00000
#define V3_PCI_MAP_M_RD_POST_INH        (1 << 15)
#define V3_PCI_MAP_M_ROM_SIZE           (3 << 10)
#define V3_PCI_MAP_M_SWAP               (3 << 8)
#define V3_PCI_MAP_M_ADR_SIZE           0x000000F0
#define V3_PCI_MAP_M_REG_EN             (1 << 1)
#define V3_PCI_MAP_M_ENABLE             (1 << 0)

/*
 *  LB_BASE0,1 register bits (Local bus -> PCI)
 */
#define V3_LB_BASE_ADR_BASE		0xfff00000
#define V3_LB_BASE_SWAP			(3 << 8)
#define V3_LB_BASE_ADR_SIZE		(15 << 4)
#define V3_LB_BASE_PREFETCH		(1 << 3)
#define V3_LB_BASE_ENABLE		(1 << 0)

#define V3_LB_BASE_ADR_SIZE_1MB		(0 << 4)
#define V3_LB_BASE_ADR_SIZE_2MB		(1 << 4)
#define V3_LB_BASE_ADR_SIZE_4MB		(2 << 4)
#define V3_LB_BASE_ADR_SIZE_8MB		(3 << 4)
#define V3_LB_BASE_ADR_SIZE_16MB	(4 << 4)
#define V3_LB_BASE_ADR_SIZE_32MB	(5 << 4)
#define V3_LB_BASE_ADR_SIZE_64MB	(6 << 4)
#define V3_LB_BASE_ADR_SIZE_128MB	(7 << 4)
#define V3_LB_BASE_ADR_SIZE_256MB	(8 << 4)
#define V3_LB_BASE_ADR_SIZE_512MB	(9 << 4)
#define V3_LB_BASE_ADR_SIZE_1GB		(10 << 4)
#define V3_LB_BASE_ADR_SIZE_2GB		(11 << 4)

#define v3_addr_to_lb_base(a)	((a) & V3_LB_BASE_ADR_BASE)

/*
 *  LB_MAP0,1 register bits (Local bus -> PCI)
 */
#define V3_LB_MAP_MAP_ADR		0xfff0
#define V3_LB_MAP_TYPE			(7 << 1)
#define V3_LB_MAP_AD_LOW_EN		(1 << 0)

#define V3_LB_MAP_TYPE_IACK		(0 << 1)
#define V3_LB_MAP_TYPE_IO		(1 << 1)
#define V3_LB_MAP_TYPE_MEM		(3 << 1)
#define V3_LB_MAP_TYPE_CONFIG		(5 << 1)
#define V3_LB_MAP_TYPE_MEM_MULTIPLE	(6 << 1)

#define v3_addr_to_lb_map(a)	(((a) >> 16) & V3_LB_MAP_MAP_ADR)

/*
 *  LB_BASE2 register bits (Local bus -> PCI IO)
 */
#define V3_LB_BASE2_ADR_BASE		0xff00
#define V3_LB_BASE2_SWAP		(3 << 6)
#define V3_LB_BASE2_ENABLE		(1 << 0)

#define v3_addr_to_lb_base2(a)	(((a) >> 16) & V3_LB_BASE2_ADR_BASE)

/*
 *  LB_MAP2 register bits (Local bus -> PCI IO)
 */
#define V3_LB_MAP2_MAP_ADR		0xff00

#define v3_addr_to_lb_map2(a)	(((a) >> 16) & V3_LB_MAP2_MAP_ADR)

/*
 * The V3 PCI interface chip in Integrator provides several windows from
 * local bus memory into the PCI memory areas.   Unfortunately, there
 * are not really enough windows for our usage, therefore we reuse
 * one of the windows for access to PCI configuration space.  The
 * memory map is as follows:
 *
 * Local Bus Memory         Usage
 *
 * 40000000 - 4FFFFFFF      PCI memory.  256M non-prefetchable
 * 50000000 - 5FFFFFFF      PCI memory.  256M prefetchable
 * 60000000 - 60FFFFFF      PCI IO.  16M
 * 61000000 - 61FFFFFF      PCI Configuration. 16M
 *
 * There are three V3 windows, each described by a pair of V3 registers.
 * These are LB_BASE0/LB_MAP0, LB_BASE1/LB_MAP1 and LB_BASE2/LB_MAP2.
 * Base0 and Base1 can be used for any type of PCI memory access.   Base2
 * can be used either for PCI I/O or for I20 accesses.  By default, uHAL
 * uses this only for PCI IO space.
 *
 * Normally these spaces are mapped using the following base registers:
 *
 * Usage Local Bus Memory         Base/Map registers used
 *
 * Mem   40000000 - 4FFFFFFF      LB_BASE0/LB_MAP0
 * Mem   50000000 - 5FFFFFFF      LB_BASE1/LB_MAP1
 * IO    60000000 - 60FFFFFF      LB_BASE2/LB_MAP2
 * Cfg   61000000 - 61FFFFFF
 *
 * This means that I20 and PCI configuration space accesses will fail.
 * When PCI configuration accesses are needed (via the uHAL PCI
 * configuration space primitives) we must remap the spaces as follows:
 *
 * Usage Local Bus Memory         Base/Map registers used
 *
 * Mem   40000000 - 4FFFFFFF      LB_BASE0/LB_MAP0
 * Mem   50000000 - 5FFFFFFF      LB_BASE0/LB_MAP0
 * IO    60000000 - 60FFFFFF      LB_BASE2/LB_MAP2
 * Cfg   61000000 - 61FFFFFF      LB_BASE1/LB_MAP1
 *
 * To make this work, the code depends on overlapping windows working.
 * The V3 chip translates an address by checking its range within
 * each of the BASE/MAP pairs in turn (in ascending register number
 * order).  It will use the first matching pair.   So, for example,
 * if the same address is mapped by both LB_BASE0/LB_MAP0 and
 * LB_BASE1/LB_MAP1, the V3 will use the translation from
 * LB_BASE0/LB_MAP0.
 *
 * To allow PCI Configuration space access, the code enlarges the
 * window mapped by LB_BASE0/LB_MAP0 from 256M to 512M.  This occludes
 * the windows currently mapped by LB_BASE1/LB_MAP1 so that it can
 * be remapped for use by configuration cycles.
 *
 * At the end of the PCI Configuration space accesses,
 * LB_BASE1/LB_MAP1 is reset to map PCI Memory.  Finally the window
 * mapped by LB_BASE0/LB_MAP0 is reduced in size from 512M to 256M to
 * reveal the now restored LB_BASE1/LB_MAP1 window.
 *
 * NOTE: We do not set up I2O mapping.  I suspect that this is only
 * for an intelligent (target) device.  Using I2O disables most of
 * the mappings into PCI memory.
 */

/* Filled in by probe */
static void __iomem *pci_v3_base;
/* CPU side memory ranges */
static struct resource conf_mem; /* FIXME: remap this instead of static map */
static struct resource io_mem;
static struct resource non_mem;
static struct resource pre_mem;
/* PCI side memory ranges */
static u64 non_mem_pci;
static u64 non_mem_pci_sz;
static u64 pre_mem_pci;
static u64 pre_mem_pci_sz;

// V3 access routines
#define v3_writeb(o,v) __raw_writeb(v, pci_v3_base + (unsigned int)(o))
#define v3_readb(o)    (__raw_readb(pci_v3_base + (unsigned int)(o)))

#define v3_writew(o,v) __raw_writew(v, pci_v3_base + (unsigned int)(o))
#define v3_readw(o)    (__raw_readw(pci_v3_base + (unsigned int)(o)))

#define v3_writel(o,v) __raw_writel(v, pci_v3_base + (unsigned int)(o))
#define v3_readl(o)    (__raw_readl(pci_v3_base + (unsigned int)(o)))

/*============================================================================
 *
 * routine:	uHALir_PCIMakeConfigAddress()
 *
 * parameters:	bus = which bus
 *              device = which device
 *              function = which function
 *		offset = configuration space register we are interested in
 *
 * description:	this routine will generate a platform dependent config
 *		address.
 *
 * calls:	none
 *
 * returns:	configuration address to play on the PCI bus
 *
 * To generate the appropriate PCI configuration cycles in the PCI
 * configuration address space, you present the V3 with the following pattern
 * (which is very nearly a type 1 (except that the lower two bits are 00 and
 * not 01).   In order for this mapping to work you need to set up one of
 * the local to PCI aperatures to 16Mbytes in length translating to
 * PCI configuration space starting at 0x0000.0000.
 *
 * PCI configuration cycles look like this:
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | |D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:11	Device select bit.
 * 	10:8	Function number
 * 	 7:2	Register number
 *
 * Type 1:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | |B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|1|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:24	reserved
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *
 */
static DEFINE_RAW_SPINLOCK(v3_lock);

#undef V3_LB_BASE_PREFETCH
#define V3_LB_BASE_PREFETCH 0

static void __iomem *v3_open_config_window(struct pci_bus *bus,
					   unsigned int devfn, int offset)
{
	unsigned int address, mapaddress, busnr;

	busnr = bus->number;

	/*
	 * Trap out illegal values
	 */
	BUG_ON(offset > 255);
	BUG_ON(busnr > 255);
	BUG_ON(devfn > 255);

	if (busnr == 0) {
		int slot = PCI_SLOT(devfn);

		/*
		 * local bus segment so need a type 0 config cycle
		 *
		 * build the PCI configuration "address" with one-hot in
		 * A31-A11
		 *
		 * mapaddress:
		 *  3:1 = config cycle (101)
		 *  0   = PCI A1 & A0 are 0 (0)
		 */
		address = PCI_FUNC(devfn) << 8;
		mapaddress = V3_LB_MAP_TYPE_CONFIG;

		if (slot > 12)
			/*
			 * high order bits are handled by the MAP register
			 */
			mapaddress |= 1 << (slot - 5);
		else
			/*
			 * low order bits handled directly in the address
			 */
			address |= 1 << (slot + 11);
	} else {
        	/*
		 * not the local bus segment so need a type 1 config cycle
		 *
		 * address:
		 *  23:16 = bus number
		 *  15:11 = slot number (7:3 of devfn)
		 *  10:8  = func number (2:0 of devfn)
		 *
		 * mapaddress:
		 *  3:1 = config cycle (101)
		 *  0   = PCI A1 & A0 from host bus (1)
		 */
		mapaddress = V3_LB_MAP_TYPE_CONFIG | V3_LB_MAP_AD_LOW_EN;
		address = (busnr << 16) | (devfn << 8);
	}

	/*
	 * Set up base0 to see all 512Mbytes of memory space (not
	 * prefetchable), this frees up base1 for re-use by
	 * configuration memory
	 */
	v3_writel(V3_LB_BASE0, v3_addr_to_lb_base(non_mem.start) |
			V3_LB_BASE_ADR_SIZE_512MB | V3_LB_BASE_ENABLE);

	/*
	 * Set up base1/map1 to point into configuration space.
	 */
	v3_writel(V3_LB_BASE1, v3_addr_to_lb_base(conf_mem.start) |
			V3_LB_BASE_ADR_SIZE_16MB | V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP1, mapaddress);

	return PCI_CONFIG_VADDR + address + offset;
}

static void v3_close_config_window(void)
{
	/*
	 * Reassign base1 for use by prefetchable PCI memory
	 */
	v3_writel(V3_LB_BASE1, v3_addr_to_lb_base(pre_mem.start) |
			V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_PREFETCH |
			V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP1, v3_addr_to_lb_map(pre_mem_pci) |
			V3_LB_MAP_TYPE_MEM_MULTIPLE);

	/*
	 * And shrink base0 back to a 256M window (NOTE: MAP0 already correct)
	 */
	v3_writel(V3_LB_BASE0, v3_addr_to_lb_base(non_mem.start) |
			V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_ENABLE);
}

static int v3_read_config(struct pci_bus *bus, unsigned int devfn, int where,
			  int size, u32 *val)
{
	void __iomem *addr;
	unsigned long flags;
	u32 v;

	raw_spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(bus, devfn, where);

	switch (size) {
	case 1:
		v = __raw_readb(addr);
		break;

	case 2:
		v = __raw_readw(addr);
		break;

	default:
		v = __raw_readl(addr);
		break;
	}

	v3_close_config_window();
	raw_spin_unlock_irqrestore(&v3_lock, flags);

	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

static int v3_write_config(struct pci_bus *bus, unsigned int devfn, int where,
			   int size, u32 val)
{
	void __iomem *addr;
	unsigned long flags;

	raw_spin_lock_irqsave(&v3_lock, flags);
	addr = v3_open_config_window(bus, devfn, where);

	switch (size) {
	case 1:
		__raw_writeb((u8)val, addr);
		__raw_readb(addr);
		break;

	case 2:
		__raw_writew((u16)val, addr);
		__raw_readw(addr);
		break;

	case 4:
		__raw_writel(val, addr);
		__raw_readl(addr);
		break;
	}

	v3_close_config_window();
	raw_spin_unlock_irqrestore(&v3_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_v3_ops = {
	.read	= v3_read_config,
	.write	= v3_write_config,
};

static int __init pci_v3_setup_resources(struct pci_sys_data *sys)
{
	if (request_resource(&iomem_resource, &non_mem)) {
		printk(KERN_ERR "PCI: unable to allocate non-prefetchable "
		       "memory region\n");
		return -EBUSY;
	}
	if (request_resource(&iomem_resource, &pre_mem)) {
		release_resource(&non_mem);
		printk(KERN_ERR "PCI: unable to allocate prefetchable "
		       "memory region\n");
		return -EBUSY;
	}

	/*
	 * the mem resource for this bus
	 * the prefetch mem resource for this bus
	 */
	pci_add_resource_offset(&sys->resources, &non_mem, sys->mem_offset);
	pci_add_resource_offset(&sys->resources, &pre_mem, sys->mem_offset);

	return 1;
}

/*
 * These don't seem to be implemented on the Integrator I have, which
 * means I can't get additional information on the reason for the pm2fb
 * problems.  I suppose I'll just have to mind-meld with the machine. ;)
 */
static void __iomem *ap_syscon_base;
#define INTEGRATOR_SC_PCIENABLE_OFFSET	0x18
#define INTEGRATOR_SC_LBFADDR_OFFSET	0x20
#define INTEGRATOR_SC_LBFCODE_OFFSET	0x24

static int
v3_pci_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);
	unsigned long instr = *(unsigned long *)pc;
#if 0
	char buf[128];

	sprintf(buf, "V3 fault: addr 0x%08lx, FSR 0x%03x, PC 0x%08lx [%08lx] LBFADDR=%08x LBFCODE=%02x ISTAT=%02x\n",
		addr, fsr, pc, instr, __raw_readl(ap_syscon_base + INTEGRATOR_SC_LBFADDR_OFFSET), __raw_readl(ap_syscon_base + INTEGRATOR_SC_LBFCODE_OFFSET) & 255,
		v3_readb(V3_LB_ISTAT));
	printk(KERN_DEBUG "%s", buf);
#endif

	v3_writeb(V3_LB_ISTAT, 0);
	__raw_writel(3, ap_syscon_base + INTEGRATOR_SC_PCIENABLE_OFFSET);

	/*
	 * If the instruction being executed was a read,
	 * make it look like it read all-ones.
	 */
	if ((instr & 0x0c100000) == 0x04100000) {
		int reg = (instr >> 12) & 15;
		unsigned long val;

		if (instr & 0x00400000)
			val = 255;
		else
			val = -1;

		regs->uregs[reg] = val;
		regs->ARM_pc += 4;
		return 0;
	}

	if ((instr & 0x0e100090) == 0x00100090) {
		int reg = (instr >> 12) & 15;

		regs->uregs[reg] = -1;
		regs->ARM_pc += 4;
		return 0;
	}

	return 1;
}

static irqreturn_t v3_irq(int irq, void *devid)
{
#ifdef CONFIG_DEBUG_LL
	struct pt_regs *regs = get_irq_regs();
	unsigned long pc = instruction_pointer(regs);
	unsigned long instr = *(unsigned long *)pc;
	char buf[128];
	extern void printascii(const char *);

	sprintf(buf, "V3 int %d: pc=0x%08lx [%08lx] LBFADDR=%08x LBFCODE=%02x "
		"ISTAT=%02x\n", irq, pc, instr,
		__raw_readl(ap_syscon_base + INTEGRATOR_SC_LBFADDR_OFFSET),
		__raw_readl(ap_syscon_base + INTEGRATOR_SC_LBFCODE_OFFSET) & 255,
		v3_readb(V3_LB_ISTAT));
	printascii(buf);
#endif

	v3_writew(V3_PCI_STAT, 0xf000);
	v3_writeb(V3_LB_ISTAT, 0);
	__raw_writel(3, ap_syscon_base + INTEGRATOR_SC_PCIENABLE_OFFSET);

#ifdef CONFIG_DEBUG_LL
	/*
	 * If the instruction being executed was a read,
	 * make it look like it read all-ones.
	 */
	if ((instr & 0x0c100000) == 0x04100000) {
		int reg = (instr >> 16) & 15;
		sprintf(buf, "   reg%d = %08lx\n", reg, regs->uregs[reg]);
		printascii(buf);
	}
#endif
	return IRQ_HANDLED;
}

static int __init pci_v3_setup(int nr, struct pci_sys_data *sys)
{
	int ret = 0;

	if (!ap_syscon_base)
		return -EINVAL;

	if (nr == 0) {
		sys->mem_offset = non_mem.start;
		ret = pci_v3_setup_resources(sys);
	}

	return ret;
}

/*
 * V3_LB_BASE? - local bus address
 * V3_LB_MAP?  - pci bus address
 */
static void __init pci_v3_preinit(void)
{
	unsigned long flags;
	unsigned int temp;

	pcibios_min_mem = 0x00100000;

	/*
	 * Hook in our fault handler for PCI errors
	 */
	hook_fault_code(4, v3_pci_fault, SIGBUS, 0, "external abort on linefetch");
	hook_fault_code(6, v3_pci_fault, SIGBUS, 0, "external abort on linefetch");
	hook_fault_code(8, v3_pci_fault, SIGBUS, 0, "external abort on non-linefetch");
	hook_fault_code(10, v3_pci_fault, SIGBUS, 0, "external abort on non-linefetch");

	raw_spin_lock_irqsave(&v3_lock, flags);

	/*
	 * Unlock V3 registers, but only if they were previously locked.
	 */
	if (v3_readw(V3_SYSTEM) & V3_SYSTEM_M_LOCK)
		v3_writew(V3_SYSTEM, 0xa05f);

	/*
	 * Setup window 0 - PCI non-prefetchable memory
	 *  Local: 0x40000000 Bus: 0x00000000 Size: 256MB
	 */
	v3_writel(V3_LB_BASE0, v3_addr_to_lb_base(non_mem.start) |
			V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP0, v3_addr_to_lb_map(non_mem_pci) |
			V3_LB_MAP_TYPE_MEM);

	/*
	 * Setup window 1 - PCI prefetchable memory
	 *  Local: 0x50000000 Bus: 0x10000000 Size: 256MB
	 */
	v3_writel(V3_LB_BASE1, v3_addr_to_lb_base(pre_mem.start) |
			V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_PREFETCH |
			V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP1, v3_addr_to_lb_map(pre_mem_pci) |
			V3_LB_MAP_TYPE_MEM_MULTIPLE);

	/*
	 * Setup window 2 - PCI IO
	 */
	v3_writel(V3_LB_BASE2, v3_addr_to_lb_base2(io_mem.start) |
			V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP2, v3_addr_to_lb_map2(0));

	/*
	 * Disable PCI to host IO cycles
	 */
	temp = v3_readw(V3_PCI_CFG) & ~V3_PCI_CFG_M_I2O_EN;
	temp |= V3_PCI_CFG_M_IO_REG_DIS | V3_PCI_CFG_M_IO_DIS;
	v3_writew(V3_PCI_CFG, temp);

	printk(KERN_DEBUG "FIFO_CFG: %04x  FIFO_PRIO: %04x\n",
		v3_readw(V3_FIFO_CFG), v3_readw(V3_FIFO_PRIORITY));

	/*
	 * Set the V3 FIFO such that writes have higher priority than
	 * reads, and local bus write causes local bus read fifo flush.
	 * Same for PCI.
	 */
	v3_writew(V3_FIFO_PRIORITY, 0x0a0a);

	/*
	 * Re-lock the system register.
	 */
	temp = v3_readw(V3_SYSTEM) | V3_SYSTEM_M_LOCK;
	v3_writew(V3_SYSTEM, temp);

	/*
	 * Clear any error conditions, and enable write errors.
	 */
	v3_writeb(V3_LB_ISTAT, 0);
	v3_writew(V3_LB_CFG, v3_readw(V3_LB_CFG) | (1 << 10));
	v3_writeb(V3_LB_IMASK, 0x28);
	__raw_writel(3, ap_syscon_base + INTEGRATOR_SC_PCIENABLE_OFFSET);

	raw_spin_unlock_irqrestore(&v3_lock, flags);
}

static void __init pci_v3_postinit(void)
{
	unsigned int pci_cmd;

	pci_cmd = PCI_COMMAND_MEMORY |
		  PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE;

	v3_writew(V3_PCI_CMD, pci_cmd);

	v3_writeb(V3_LB_ISTAT, ~0x40);
	v3_writeb(V3_LB_IMASK, 0x68);

#if 0
	ret = request_irq(IRQ_AP_LBUSTIMEOUT, lb_timeout, 0, "bus timeout", NULL);
	if (ret)
		printk(KERN_ERR "PCI: unable to grab local bus timeout "
		       "interrupt: %d\n", ret);
#endif

	register_isa_ports(non_mem.start, io_mem.start, 0);
}

/*
 * A small note about bridges and interrupts.  The DECchip 21050 (and
 * later) adheres to the PCI-PCI bridge specification.  This says that
 * the interrupts on the other side of a bridge are swizzled in the
 * following manner:
 *
 * Dev    Interrupt   Interrupt
 *        Pin on      Pin on
 *        Device      Connector
 *
 *   4    A           A
 *        B           B
 *        C           C
 *        D           D
 *
 *   5    A           B
 *        B           C
 *        C           D
 *        D           A
 *
 *   6    A           C
 *        B           D
 *        C           A
 *        D           B
 *
 *   7    A           D
 *        B           A
 *        C           B
 *        D           C
 *
 * Where A = pin 1, B = pin 2 and so on and pin=0 = default = A.
 * Thus, each swizzle is ((pin-1) + (device#-4)) % 4
 */

/*
 * This routine handles multiple bridges.
 */
static u8 __init pci_v3_swizzle(struct pci_dev *dev, u8 *pinp)
{
	if (*pinp == 0)
		*pinp = 1;

	return pci_common_swizzle(dev, pinp);
}

static struct hw_pci pci_v3 __initdata = {
	.swizzle		= pci_v3_swizzle,
	.setup			= pci_v3_setup,
	.nr_controllers		= 1,
	.ops			= &pci_v3_ops,
	.preinit		= pci_v3_preinit,
	.postinit		= pci_v3_postinit,
};

static int __init pci_v3_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	struct resource *res;
	int irq, ret;

	/* Remap the Integrator system controller */
	ap_syscon_base = devm_ioremap(&pdev->dev, INTEGRATOR_SC_BASE, 0x100);
	if (!ap_syscon_base) {
		dev_err(&pdev->dev, "unable to remap the AP syscon for PCIv3\n");
		return -ENODEV;
	}

	/* Device tree probe path */
	if (!np) {
		dev_err(&pdev->dev, "no device tree node for PCIv3\n");
		return -ENODEV;
	}

	if (of_pci_range_parser_init(&parser, np))
		return -EINVAL;

	/* Get base for bridge registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "unable to obtain PCIv3 base\n");
		return -ENODEV;
	}
	pci_v3_base = devm_ioremap(&pdev->dev, res->start,
				   resource_size(res));
	if (!pci_v3_base) {
		dev_err(&pdev->dev, "unable to remap PCIv3 base\n");
		return -ENODEV;
	}

	/* Get and request error IRQ resource */
	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "unable to obtain PCIv3 error IRQ\n");
		return -ENODEV;
	}
	ret = devm_request_irq(&pdev->dev, irq, v3_irq, 0,
			"PCIv3 error", NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to request PCIv3 error IRQ %d (%d)\n", irq, ret);
		return ret;
	}

	for_each_of_pci_range(&parser, &range) {
		if (!range.flags) {
			of_pci_range_to_resource(&range, np, &conf_mem);
			conf_mem.name = "PCIv3 config";
		}
		if (range.flags & IORESOURCE_IO) {
			of_pci_range_to_resource(&range, np, &io_mem);
			io_mem.name = "PCIv3 I/O";
		}
		if ((range.flags & IORESOURCE_MEM) &&
			!(range.flags & IORESOURCE_PREFETCH)) {
			non_mem_pci = range.pci_addr;
			non_mem_pci_sz = range.size;
			of_pci_range_to_resource(&range, np, &non_mem);
			non_mem.name = "PCIv3 non-prefetched mem";
		}
		if ((range.flags & IORESOURCE_MEM) &&
			(range.flags & IORESOURCE_PREFETCH)) {
			pre_mem_pci = range.pci_addr;
			pre_mem_pci_sz = range.size;
			of_pci_range_to_resource(&range, np, &pre_mem);
			pre_mem.name = "PCIv3 prefetched mem";
		}
	}

	if (!conf_mem.start || !io_mem.start ||
	    !non_mem.start || !pre_mem.start) {
		dev_err(&pdev->dev, "missing ranges in device node\n");
		return -EINVAL;
	}

	pci_v3.map_irq = of_irq_parse_and_map_pci;
	pci_common_init_dev(&pdev->dev, &pci_v3);

	return 0;
}

static const struct of_device_id pci_ids[] = {
	{ .compatible = "v3,v360epc-pci", },
	{},
};

static struct platform_driver pci_v3_driver = {
	.driver = {
		.name = "pci-v3",
		.of_match_table = pci_ids,
	},
};

static int __init pci_v3_init(void)
{
	return platform_driver_probe(&pci_v3_driver, pci_v3_probe);
}

subsys_initcall(pci_v3_init);

/*
 * Static mappings for the PCIv3 bridge
 *
 * e8000000	40000000	PCI memory		PHYS_PCI_MEM_BASE	(max 512M)
 * ec000000	61000000	PCI config space	PHYS_PCI_CONFIG_BASE	(max 16M)
 * fee00000	60000000	PCI IO			PHYS_PCI_IO_BASE	(max 16M)
 */
static struct map_desc pci_v3_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	= (unsigned long)PCI_MEMORY_VADDR,
		.pfn		= __phys_to_pfn(PHYS_PCI_MEM_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= (unsigned long)PCI_CONFIG_VADDR,
		.pfn		= __phys_to_pfn(PHYS_PCI_CONFIG_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}
};

int __init pci_v3_early_init(void)
{
	iotable_init(pci_v3_io_desc, ARRAY_SIZE(pci_v3_io_desc));
	vga_base = (unsigned long)PCI_MEMORY_VADDR;
	pci_map_io_early(__phys_to_pfn(PHYS_PCI_IO_BASE));
	return 0;
}
