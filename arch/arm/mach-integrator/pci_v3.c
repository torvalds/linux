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

#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/irqs.h>

#include <asm/signal.h>
#include <asm/mach/pci.h>
#include <asm/irq_regs.h>

#include <asm/hardware/pci_v3.h>

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

// V3 access routines
#define v3_writeb(o,v) __raw_writeb(v, PCI_V3_VADDR + (unsigned int)(o))
#define v3_readb(o)    (__raw_readb(PCI_V3_VADDR + (unsigned int)(o)))

#define v3_writew(o,v) __raw_writew(v, PCI_V3_VADDR + (unsigned int)(o))
#define v3_readw(o)    (__raw_readw(PCI_V3_VADDR + (unsigned int)(o)))

#define v3_writel(o,v) __raw_writel(v, PCI_V3_VADDR + (unsigned int)(o))
#define v3_readl(o)    (__raw_readl(PCI_V3_VADDR + (unsigned int)(o)))

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

#define PCI_BUS_NONMEM_START	0x00000000
#define PCI_BUS_NONMEM_SIZE	SZ_256M

#define PCI_BUS_PREMEM_START	PCI_BUS_NONMEM_START + PCI_BUS_NONMEM_SIZE
#define PCI_BUS_PREMEM_SIZE	SZ_256M

#if PCI_BUS_NONMEM_START & 0x000fffff
#error PCI_BUS_NONMEM_START must be megabyte aligned
#endif
#if PCI_BUS_PREMEM_START & 0x000fffff
#error PCI_BUS_PREMEM_START must be megabyte aligned
#endif

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
	if (offset > 255)
		BUG();
	if (busnr > 255)
		BUG();
	if (devfn > 255)
		BUG();

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
	v3_writel(V3_LB_BASE0, v3_addr_to_lb_base(PHYS_PCI_MEM_BASE) |
			V3_LB_BASE_ADR_SIZE_512MB | V3_LB_BASE_ENABLE);

	/*
	 * Set up base1/map1 to point into configuration space.
	 */
	v3_writel(V3_LB_BASE1, v3_addr_to_lb_base(PHYS_PCI_CONFIG_BASE) |
			V3_LB_BASE_ADR_SIZE_16MB | V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP1, mapaddress);

	return PCI_CONFIG_VADDR + address + offset;
}

static void v3_close_config_window(void)
{
	/*
	 * Reassign base1 for use by prefetchable PCI memory
	 */
	v3_writel(V3_LB_BASE1, v3_addr_to_lb_base(PHYS_PCI_MEM_BASE + SZ_256M) |
			V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_PREFETCH |
			V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP1, v3_addr_to_lb_map(PCI_BUS_PREMEM_START) |
			V3_LB_MAP_TYPE_MEM_MULTIPLE);

	/*
	 * And shrink base0 back to a 256M window (NOTE: MAP0 already correct)
	 */
	v3_writel(V3_LB_BASE0, v3_addr_to_lb_base(PHYS_PCI_MEM_BASE) |
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

struct pci_ops pci_v3_ops = {
	.read	= v3_read_config,
	.write	= v3_write_config,
};

static struct resource non_mem = {
	.name	= "PCI non-prefetchable",
	.start	= PHYS_PCI_MEM_BASE + PCI_BUS_NONMEM_START,
	.end	= PHYS_PCI_MEM_BASE + PCI_BUS_NONMEM_START + PCI_BUS_NONMEM_SIZE - 1,
	.flags	= IORESOURCE_MEM,
};

static struct resource pre_mem = {
	.name	= "PCI prefetchable",
	.start	= PHYS_PCI_MEM_BASE + PCI_BUS_PREMEM_START,
	.end	= PHYS_PCI_MEM_BASE + PCI_BUS_PREMEM_START + PCI_BUS_PREMEM_SIZE - 1,
	.flags	= IORESOURCE_MEM | IORESOURCE_PREFETCH,
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

static irqreturn_t v3_irq(int dummy, void *devid)
{
#ifdef CONFIG_DEBUG_LL
	struct pt_regs *regs = get_irq_regs();
	unsigned long pc = instruction_pointer(regs);
	unsigned long instr = *(unsigned long *)pc;
	char buf[128];
	extern void printascii(const char *);

	sprintf(buf, "V3 int %d: pc=0x%08lx [%08lx] LBFADDR=%08x LBFCODE=%02x "
		"ISTAT=%02x\n", IRQ_AP_V3INT, pc, instr,
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

int __init pci_v3_setup(int nr, struct pci_sys_data *sys)
{
	int ret = 0;

	if (nr == 0) {
		sys->mem_offset = PHYS_PCI_MEM_BASE;
		ret = pci_v3_setup_resources(sys);
		/* Remap the Integrator system controller */
		ap_syscon_base = ioremap(INTEGRATOR_SC_BASE, 0x100);
		if (!ap_syscon_base)
			return -EINVAL;
	}

	return ret;
}

/*
 * V3_LB_BASE? - local bus address
 * V3_LB_MAP?  - pci bus address
 */
void __init pci_v3_preinit(void)
{
	unsigned long flags;
	unsigned int temp;
	int ret;

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
	v3_writel(V3_LB_BASE0, v3_addr_to_lb_base(PHYS_PCI_MEM_BASE) |
			V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP0, v3_addr_to_lb_map(PCI_BUS_NONMEM_START) |
			V3_LB_MAP_TYPE_MEM);

	/*
	 * Setup window 1 - PCI prefetchable memory
	 *  Local: 0x50000000 Bus: 0x10000000 Size: 256MB
	 */
	v3_writel(V3_LB_BASE1, v3_addr_to_lb_base(PHYS_PCI_MEM_BASE + SZ_256M) |
			V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_PREFETCH |
			V3_LB_BASE_ENABLE);
	v3_writew(V3_LB_MAP1, v3_addr_to_lb_map(PCI_BUS_PREMEM_START) |
			V3_LB_MAP_TYPE_MEM_MULTIPLE);

	/*
	 * Setup window 2 - PCI IO
	 */
	v3_writel(V3_LB_BASE2, v3_addr_to_lb_base2(PHYS_PCI_IO_BASE) |
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

	/*
	 * Grab the PCI error interrupt.
	 */
	ret = request_irq(IRQ_AP_V3INT, v3_irq, 0, "V3", NULL);
	if (ret)
		printk(KERN_ERR "PCI: unable to grab PCI error "
		       "interrupt: %d\n", ret);

	raw_spin_unlock_irqrestore(&v3_lock, flags);
}

void __init pci_v3_postinit(void)
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

	register_isa_ports(PHYS_PCI_MEM_BASE, PHYS_PCI_IO_BASE, 0);
}
