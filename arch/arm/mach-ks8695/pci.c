/*
 * arch/arm/mach-ks8695/pci.c
 *
 *  Copyright (C) 2003, Micrel Semiconductors
 *  Copyright (C) 2006, Greg Ungerer <gerg@snapgear.com>
 *  Copyright (C) 2006, Ben Dooks
 *  Copyright (C) 2007, Andrew Victor
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
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/signal.h>
#include <asm/mach/pci.h>
#include <mach/hardware.h>

#include <mach/devices.h>
#include <mach/regs-pci.h>


static int pci_dbg;
static int pci_cfg_dbg;


static void ks8695_pci_setupconfig(unsigned int bus_nr, unsigned int devfn, unsigned int where)
{
	unsigned long pbca;

	pbca = PBCA_ENABLE | (where & ~3);
	pbca |= PCI_SLOT(devfn) << 11 ;
	pbca |= PCI_FUNC(devfn) << 8;
	pbca |= bus_nr << 16;

	if (bus_nr == 0) {
		/* use Type-0 transaction */
		__raw_writel(pbca, KS8695_PCI_VA + KS8695_PBCA);
	} else {
		/* use Type-1 transaction */
		__raw_writel(pbca | PBCA_TYPE1, KS8695_PCI_VA + KS8695_PBCA);
	}
}


/*
 * The KS8695 datasheet prohibits anything other than 32bit accesses
 * to the IO registers, so all our configuration must be done with
 * 32bit operations, and the correct bit masking and shifting.
 */

static int ks8695_pci_readconfig(struct pci_bus *bus,
			unsigned int devfn, int where, int size, u32 *value)
{
	ks8695_pci_setupconfig(bus->number, devfn, where);

	*value = __raw_readl(KS8695_PCI_VA +  KS8695_PBCD);

	switch (size) {
		case 4:
			break;
		case 2:
			*value = *value >> ((where & 2) * 8);
			*value &= 0xffff;
			break;
		case 1:
			*value = *value >> ((where & 3) * 8);
			*value &= 0xff;
			break;
	}

	if (pci_cfg_dbg) {
		printk("read: %d,%08x,%02x,%d: %08x (%08x)\n",
			bus->number, devfn, where, size, *value,
			__raw_readl(KS8695_PCI_VA +  KS8695_PBCD));
	}

	return PCIBIOS_SUCCESSFUL;
}

static int ks8695_pci_writeconfig(struct pci_bus *bus,
			unsigned int devfn, int where, int size, u32 value)
{
	unsigned long tmp;

	if (pci_cfg_dbg) {
		printk("write: %d,%08x,%02x,%d: %08x\n",
			bus->number, devfn, where, size, value);
	}

	ks8695_pci_setupconfig(bus->number, devfn, where);

	switch (size) {
		case 4:
			__raw_writel(value, KS8695_PCI_VA +  KS8695_PBCD);
			break;
		case 2:
			tmp = __raw_readl(KS8695_PCI_VA +  KS8695_PBCD);
			tmp &= ~(0xffff << ((where & 2) * 8));
			tmp |= value << ((where & 2) * 8);

			__raw_writel(tmp, KS8695_PCI_VA +  KS8695_PBCD);
			break;
		case 1:
			tmp = __raw_readl(KS8695_PCI_VA +  KS8695_PBCD);
			tmp &= ~(0xff << ((where & 3) * 8));
			tmp |= value << ((where & 3) * 8);

			__raw_writel(tmp, KS8695_PCI_VA +  KS8695_PBCD);
			break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static void ks8695_local_writeconfig(int where, u32 value)
{
	ks8695_pci_setupconfig(0, 0, where);
	__raw_writel(value, KS8695_PCI_VA + KS8695_PBCD);
}

static struct pci_ops ks8695_pci_ops = {
	.read	= ks8695_pci_readconfig,
	.write	= ks8695_pci_writeconfig,
};

static struct pci_bus* __init ks8695_pci_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_bus(sys->busnr, &ks8695_pci_ops, sys);
}

static struct resource pci_mem = {
	.name	= "PCI Memory space",
	.start	= KS8695_PCIMEM_PA,
	.end	= KS8695_PCIMEM_PA + (KS8695_PCIMEM_SIZE - 1),
	.flags	= IORESOURCE_MEM,
};

static struct resource pci_io = {
	.name	= "PCI IO space",
	.start	= KS8695_PCIIO_PA,
	.end	= KS8695_PCIIO_PA + (KS8695_PCIIO_SIZE - 1),
	.flags	= IORESOURCE_IO,
};

static int __init ks8695_pci_setup(int nr, struct pci_sys_data *sys)
{
	if (nr > 0)
		return 0;

	request_resource(&iomem_resource, &pci_mem);
	request_resource(&ioport_resource, &pci_io);

	sys->resource[0] = &pci_io;
	sys->resource[1] = &pci_mem;
	sys->resource[2] = NULL;

	/* Assign and enable processor bridge */
	ks8695_local_writeconfig(PCI_BASE_ADDRESS_0, KS8695_PCIMEM_PA);

	/* Enable bus-master & Memory Space access */
	ks8695_local_writeconfig(PCI_COMMAND, PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);

	/* Set cache-line size & latency. */
	ks8695_local_writeconfig(PCI_CACHE_LINE_SIZE, (32 << 8) | (L1_CACHE_BYTES / sizeof(u32)));

	/* Reserve PCI memory space for PCI-AHB resources */
	if (!request_mem_region(KS8695_PCIMEM_PA, SZ_64M, "PCI-AHB Bridge")) {
		printk(KERN_ERR "Cannot allocate PCI-AHB Bridge memory.\n");
		return -EBUSY;
	}

	return 1;
}

static inline unsigned int size_mask(unsigned long size)
{
	return (~size) + 1;
}

static int ks8695_pci_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);
	unsigned long instr = *(unsigned long *)pc;
	unsigned long cmdstat;

	cmdstat = __raw_readl(KS8695_PCI_VA + KS8695_CRCFCS);

	printk(KERN_ERR "PCI abort: address = 0x%08lx fsr = 0x%03x PC = 0x%08lx LR = 0x%08lx [%s%s%s%s%s]\n",
		addr, fsr, regs->ARM_pc, regs->ARM_lr,
		cmdstat & (PCI_STATUS_SIG_TARGET_ABORT << 16) ? "GenTarget" : " ",
		cmdstat & (PCI_STATUS_REC_TARGET_ABORT << 16) ? "RecvTarget" : " ",
		cmdstat & (PCI_STATUS_REC_MASTER_ABORT << 16) ? "MasterAbort" : " ",
		cmdstat & (PCI_STATUS_SIG_SYSTEM_ERROR << 16) ? "SysError" : " ",
		cmdstat & (PCI_STATUS_DETECTED_PARITY << 16)  ? "Parity" : " "
	);

	__raw_writel(cmdstat, KS8695_PCI_VA + KS8695_CRCFCS);

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

static void __init ks8695_pci_preinit(void)
{
	/* make software reset to avoid freeze if PCI bus was messed up */
	__raw_writel(0x80000000, KS8695_PCI_VA + KS8695_PBCS);

	/* stage 1 initialization, subid, subdevice = 0x0001 */
	__raw_writel(0x00010001, KS8695_PCI_VA + KS8695_CRCSID);

	/* stage 2 initialization */
	/* prefetch limits with 16 words, retry enable */
	__raw_writel(0x40000000, KS8695_PCI_VA + KS8695_PBCS);

	/* configure memory mapping */
	__raw_writel(KS8695_PCIMEM_PA, KS8695_PCI_VA + KS8695_PMBA);
	__raw_writel(size_mask(KS8695_PCIMEM_SIZE), KS8695_PCI_VA + KS8695_PMBAM);
	__raw_writel(KS8695_PCIMEM_PA, KS8695_PCI_VA + KS8695_PMBAT);
	__raw_writel(0, KS8695_PCI_VA + KS8695_PMBAC);

	/* configure IO mapping */
	__raw_writel(KS8695_PCIIO_PA, KS8695_PCI_VA + KS8695_PIOBA);
	__raw_writel(size_mask(KS8695_PCIIO_SIZE), KS8695_PCI_VA + KS8695_PIOBAM);
	__raw_writel(KS8695_PCIIO_PA, KS8695_PCI_VA + KS8695_PIOBAT);
	__raw_writel(0, KS8695_PCI_VA + KS8695_PIOBAC);

	/* hook in fault handlers */
	hook_fault_code(8, ks8695_pci_fault, SIGBUS, 0, "external abort on non-linefetch");
	hook_fault_code(10, ks8695_pci_fault, SIGBUS, 0, "external abort on non-linefetch");
}

static void ks8695_show_pciregs(void)
{
	if (!pci_dbg)
		return;

	printk(KERN_INFO "PCI: CRCFID = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_CRCFID));
	printk(KERN_INFO "PCI: CRCFCS = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_CRCFCS));
	printk(KERN_INFO "PCI: CRCFRV = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_CRCFRV));
	printk(KERN_INFO "PCI: CRCFLT = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_CRCFLT));
	printk(KERN_INFO "PCI: CRCBMA = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_CRCBMA));
	printk(KERN_INFO "PCI: CRCSID = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_CRCSID));
	printk(KERN_INFO "PCI: CRCFIT = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_CRCFIT));

	printk(KERN_INFO "PCI: PBM    = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PBM));
	printk(KERN_INFO "PCI: PBCS   = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PBCS));

	printk(KERN_INFO "PCI: PMBA   = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PMBA));
	printk(KERN_INFO "PCI: PMBAC  = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PMBAC));
	printk(KERN_INFO "PCI: PMBAM  = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PMBAM));
	printk(KERN_INFO "PCI: PMBAT  = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PMBAT));

	printk(KERN_INFO "PCI: PIOBA  = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PIOBA));
	printk(KERN_INFO "PCI: PIOBAC = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PIOBAC));
	printk(KERN_INFO "PCI: PIOBAM = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PIOBAM));
	printk(KERN_INFO "PCI: PIOBAT = %08x\n", __raw_readl(KS8695_PCI_VA + KS8695_PIOBAT));
}


static struct hw_pci ks8695_pci __initdata = {
	.nr_controllers	= 1,
	.preinit	= ks8695_pci_preinit,
	.setup		= ks8695_pci_setup,
	.scan		= ks8695_pci_scan_bus,
	.postinit	= NULL,
	.swizzle	= pci_std_swizzle,
	.map_irq	= NULL,
};

void __init ks8695_init_pci(struct ks8695_pci_cfg *cfg)
{
	if (__raw_readl(KS8695_PCI_VA + KS8695_CRCFRV) & CFRV_GUEST) {
		printk("PCI: KS8695 in guest mode, not initialising\n");
		return;
	}

	pcibios_min_io = 0;
	pcibios_min_mem = 0;

	printk(KERN_INFO "PCI: Initialising\n");
	ks8695_show_pciregs();

	/* set Mode */
	__raw_writel(cfg->mode << 29, KS8695_PCI_VA + KS8695_PBM);

	ks8695_pci.map_irq = cfg->map_irq;	/* board-specific map_irq method */

	pci_common_init(&ks8695_pci);
}
