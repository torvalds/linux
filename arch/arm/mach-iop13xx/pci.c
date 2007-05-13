/*
 * iop13xx PCI support
 * Copyright (c) 2005-2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/pci.h>
#include <linux/delay.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/sizes.h>
#include <asm/mach/pci.h>
#include <asm/arch/pci.h>

#define IOP13XX_PCI_DEBUG 0
#define PRINTK(x...) ((void)(IOP13XX_PCI_DEBUG && printk(x)))

u32 iop13xx_atux_pmmr_offset; /* This offset can change based on strapping */
u32 iop13xx_atue_pmmr_offset; /* This offset can change based on strapping */
static struct pci_bus *pci_bus_atux = 0;
static struct pci_bus *pci_bus_atue = 0;
u32 iop13xx_atue_mem_base;
u32 iop13xx_atux_mem_base;
size_t iop13xx_atue_mem_size;
size_t iop13xx_atux_mem_size;
unsigned long iop13xx_pcibios_min_io = 0;
unsigned long iop13xx_pcibios_min_mem = 0;

EXPORT_SYMBOL(iop13xx_atue_mem_base);
EXPORT_SYMBOL(iop13xx_atux_mem_base);
EXPORT_SYMBOL(iop13xx_atue_mem_size);
EXPORT_SYMBOL(iop13xx_atux_mem_size);

int init_atu = 0; /* Flag to select which ATU(s) to initialize / disable */
static unsigned long atux_trhfa_timeout = 0; /* Trhfa = RST# high to first
						 access */

/* Scan the initialized busses and ioremap the requested memory range
 */
void iop13xx_map_pci_memory(void)
{
	int atu;
	struct pci_bus *bus;
	struct pci_dev *dev;
	resource_size_t end = 0;

	for (atu = 0; atu < 2; atu++) {
		bus = atu ? pci_bus_atue : pci_bus_atux;
		if (bus) {
			list_for_each_entry(dev, &bus->devices, bus_list) {
				int i;
				int max = 7;

				if (dev->subordinate)
					max = DEVICE_COUNT_RESOURCE;

				for (i = 0; i < max; i++) {
					struct resource *res = &dev->resource[i];
					if (res->flags & IORESOURCE_MEM)
						end = max(res->end, end);
				}
			}

			switch(atu) {
			case 0:
				iop13xx_atux_mem_size =
					(end - IOP13XX_PCIX_LOWER_MEM_RA) + 1;

				/* 16MB align the request */
				if (iop13xx_atux_mem_size & (SZ_16M - 1)) {
					iop13xx_atux_mem_size &= ~(SZ_16M - 1);
					iop13xx_atux_mem_size += SZ_16M;
				}

				if (end) {
					iop13xx_atux_mem_base =
					(u32) __arm_ioremap_pfn(
					__phys_to_pfn(IOP13XX_PCIX_LOWER_MEM_PA)
					, 0, iop13xx_atux_mem_size, MT_DEVICE);
					if (!iop13xx_atux_mem_base) {
						printk("%s: atux allocation "
						       "failed\n", __FUNCTION__);
						BUG();
					}
				} else
					iop13xx_atux_mem_size = 0;
				PRINTK("%s: atu: %d bus_size: %d mem_base: %x\n",
				__FUNCTION__, atu, iop13xx_atux_mem_size,
				iop13xx_atux_mem_base);
				break;
			case 1:
				iop13xx_atue_mem_size =
					(end - IOP13XX_PCIE_LOWER_MEM_RA) + 1;

				/* 16MB align the request */
				if (iop13xx_atue_mem_size & (SZ_16M - 1)) {
					iop13xx_atue_mem_size &= ~(SZ_16M - 1);
					iop13xx_atue_mem_size += SZ_16M;
				}

				if (end) {
					iop13xx_atue_mem_base =
					(u32) __arm_ioremap_pfn(
					__phys_to_pfn(IOP13XX_PCIE_LOWER_MEM_PA)
					, 0, iop13xx_atue_mem_size, MT_DEVICE);
					if (!iop13xx_atue_mem_base) {
						printk("%s: atue allocation "
						       "failed\n", __FUNCTION__);
						BUG();
					}
				} else
					iop13xx_atue_mem_size = 0;
				PRINTK("%s: atu: %d bus_size: %d mem_base: %x\n",
				__FUNCTION__, atu, iop13xx_atue_mem_size,
				iop13xx_atue_mem_base);
				break;
			}

			printk("%s: Initialized (%uM @ resource/virtual: %08lx/%08x)\n",
			atu ? "ATUE" : "ATUX",
			(atu ? iop13xx_atue_mem_size : iop13xx_atux_mem_size) /
			SZ_1M,
			atu ? IOP13XX_PCIE_LOWER_MEM_RA :
			IOP13XX_PCIX_LOWER_MEM_RA,
			atu ? iop13xx_atue_mem_base :
			iop13xx_atux_mem_base);
			end = 0;
		}

	}
}

static inline int iop13xx_atu_function(int atu)
{
	int func = 0;
	/* the function number depends on the value of the
	 * IOP13XX_INTERFACE_SEL_PCIX reset strap
	 * see C-Spec section 3.17
	 */
	switch(atu) {
	case IOP13XX_INIT_ATU_ATUX:
		if (__raw_readl(IOP13XX_ESSR0) & IOP13XX_INTERFACE_SEL_PCIX)
			func = 5;
		else
			func = 0;
		break;
	case IOP13XX_INIT_ATU_ATUE:
		if (__raw_readl(IOP13XX_ESSR0) & IOP13XX_INTERFACE_SEL_PCIX)
			func = 0;
		else
			func = 5;
		break;
	default:
		BUG();
	}

	return func;
}

/* iop13xx_atux_cfg_address - format a configuration address for atux
 * @bus: Target bus to access
 * @devfn: Combined device number and function number
 * @where: Desired register's address offset
 *
 * Convert the parameters to a configuration address formatted
 * according the PCI-X 2.0 specification
 */
static u32 iop13xx_atux_cfg_address(struct pci_bus *bus, int devfn, int where)
{
	struct pci_sys_data *sys = bus->sysdata;
	u32 addr;

	if (sys->busnr == bus->number)
		addr = 1 << (PCI_SLOT(devfn) + 16) | (PCI_SLOT(devfn) << 11);
	else
		addr = bus->number << 16 | PCI_SLOT(devfn) << 11 | 1;

	addr |=	PCI_FUNC(devfn) << 8 | ((where & 0xff) & ~3);
	addr |= ((where & 0xf00) >> 8) << 24; /* upper register number */

	return addr;
}

/* iop13xx_atue_cfg_address - format a configuration address for atue
 * @bus: Target bus to access
 * @devfn: Combined device number and function number
 * @where: Desired register's address offset
 *
 * Convert the parameters to an address usable by the ATUE_OCCAR
 */
static u32 iop13xx_atue_cfg_address(struct pci_bus *bus, int devfn, int where)
{
	struct pci_sys_data *sys = bus->sysdata;
	u32 addr;

	PRINTK("iop13xx_atue_cfg_address: bus: %d dev: %d func: %d",
		bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));
	addr = ((u32) bus->number)     << IOP13XX_ATUE_OCCAR_BUS_NUM |
		   ((u32) PCI_SLOT(devfn)) << IOP13XX_ATUE_OCCAR_DEV_NUM |
		   ((u32) PCI_FUNC(devfn)) << IOP13XX_ATUE_OCCAR_FUNC_NUM |
		   (where & ~0x3);

	if (sys->busnr != bus->number)
		addr |= 1; /* type 1 access */

	return addr;
}

/* This routine checks the status of the last configuration cycle.  If an error
 * was detected it returns >0, else it returns a 0.  The errors being checked
 * are parity, master abort, target abort (master and target).  These types of
 * errors occure during a config cycle where there is no device, like during
 * the discovery stage.
 */
static int iop13xx_atux_pci_status(int clear)
{
	unsigned int status;
	int err = 0;

	/*
	 * Check the status registers.
	 */
	status = __raw_readw(IOP13XX_ATUX_ATUSR);
	if (status & IOP_PCI_STATUS_ERROR)
	{
		PRINTK("\t\t\tPCI error: ATUSR %#08x", status);
		if(clear)
			__raw_writew(status & IOP_PCI_STATUS_ERROR,
				IOP13XX_ATUX_ATUSR);
		err = 1;
	}
	status = __raw_readl(IOP13XX_ATUX_ATUISR);
	if (status & IOP13XX_ATUX_ATUISR_ERROR)
	{
		PRINTK("\t\t\tPCI error interrupt:  ATUISR %#08x", status);
		if(clear)
			__raw_writel(status & IOP13XX_ATUX_ATUISR_ERROR,
				IOP13XX_ATUX_ATUISR);
		err = 1;
	}
	return err;
}

/* Simply write the address register and read the configuration
 * data.  Note that the data dependency on %0 encourages an abort
 * to be detected before we return.
 */
static inline u32 iop13xx_atux_read(unsigned long addr)
{
	u32 val;

	__asm__ __volatile__(
		"str	%1, [%2]\n\t"
		"ldr	%0, [%3]\n\t"
		"mov	%0, %0\n\t"
		: "=r" (val)
		: "r" (addr), "r" (IOP13XX_ATUX_OCCAR), "r" (IOP13XX_ATUX_OCCDR));

	return val;
}

/* The read routines must check the error status of the last configuration
 * cycle.  If there was an error, the routine returns all hex f's.
 */
static int
iop13xx_atux_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 *value)
{
	unsigned long addr = iop13xx_atux_cfg_address(bus, devfn, where);
	u32 val = iop13xx_atux_read(addr) >> ((where & 3) * 8);

	if (iop13xx_atux_pci_status(1) || is_atux_occdr_error()) {
		__raw_writel(__raw_readl(IOP13XX_XBG_BECSR) & 3,
			IOP13XX_XBG_BECSR);
		val = 0xffffffff;
	}

	*value = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop13xx_atux_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 value)
{
	unsigned long addr = iop13xx_atux_cfg_address(bus, devfn, where);
	u32 val;

	if (size != 4) {
		val = iop13xx_atux_read(addr);
		if (!iop13xx_atux_pci_status(1) == 0)
			return PCIBIOS_SUCCESSFUL;

		where = (where & 3) * 8;

		if (size == 1)
			val &= ~(0xff << where);
		else
			val &= ~(0xffff << where);

		__raw_writel(val | value << where, IOP13XX_ATUX_OCCDR);
	} else {
		__raw_writel(addr, IOP13XX_ATUX_OCCAR);
		__raw_writel(value, IOP13XX_ATUX_OCCDR);
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop13xx_atux_ops = {
	.read	= iop13xx_atux_read_config,
	.write	= iop13xx_atux_write_config,
};

/* This routine checks the status of the last configuration cycle.  If an error
 * was detected it returns >0, else it returns a 0.  The errors being checked
 * are parity, master abort, target abort (master and target).  These types of
 * errors occure during a config cycle where there is no device, like during
 * the discovery stage.
 */
static int iop13xx_atue_pci_status(int clear)
{
	unsigned int status;
	int err = 0;

	/*
	 * Check the status registers.
	 */

	/* standard pci status register */
	status = __raw_readw(IOP13XX_ATUE_ATUSR);
	if (status & IOP_PCI_STATUS_ERROR) {
		PRINTK("\t\t\tPCI error: ATUSR %#08x", status);
		if(clear)
			__raw_writew(status & IOP_PCI_STATUS_ERROR,
				IOP13XX_ATUE_ATUSR);
		err++;
	}

	/* check the normal status bits in the ATUISR */
	status = __raw_readl(IOP13XX_ATUE_ATUISR);
	if (status & IOP13XX_ATUE_ATUISR_ERROR)	{
		PRINTK("\t\t\tPCI error: ATUISR %#08x", status);
		if (clear)
			__raw_writew(status & IOP13XX_ATUE_ATUISR_ERROR,
				IOP13XX_ATUE_ATUISR);
		err++;

		/* check the PCI-E status if the ATUISR reports an interface error */
		if (status & IOP13XX_ATUE_STAT_PCI_IFACE_ERR) {
			/* get the unmasked errors */
			status = __raw_readl(IOP13XX_ATUE_PIE_STS) &
					~(__raw_readl(IOP13XX_ATUE_PIE_MSK));

			if (status) {
				PRINTK("\t\t\tPCI-E error: ATUE_PIE_STS %#08x",
					__raw_readl(IOP13XX_ATUE_PIE_STS));
				err++;
			} else {
				PRINTK("\t\t\tPCI-E error: ATUE_PIE_STS %#08x",
					__raw_readl(IOP13XX_ATUE_PIE_STS));
				PRINTK("\t\t\tPCI-E error: ATUE_PIE_MSK %#08x",
					__raw_readl(IOP13XX_ATUE_PIE_MSK));
				BUG();
			}

			if(clear)
				__raw_writel(status, IOP13XX_ATUE_PIE_STS);
		}
	}

	return err;
}

static inline int __init
iop13xx_pcie_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
{
	WARN_ON(idsel != 0);

	switch (pin) {
	case 1: return ATUE_INTA;
	case 2: return ATUE_INTB;
	case 3: return ATUE_INTC;
	case 4: return ATUE_INTD;
	default: return -1;
	}
}

static inline u32 iop13xx_atue_read(unsigned long addr)
{
	u32 val;

	__raw_writel(addr, IOP13XX_ATUE_OCCAR);
	val = __raw_readl(IOP13XX_ATUE_OCCDR);

	rmb();

	return val;
}

/* The read routines must check the error status of the last configuration
 * cycle.  If there was an error, the routine returns all hex f's.
 */
static int
iop13xx_atue_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 *value)
{
	u32 val;
	unsigned long addr = iop13xx_atue_cfg_address(bus, devfn, where);

	/* Hide device numbers > 0 on the local PCI-E bus (Type 0 access) */
	if (!PCI_SLOT(devfn) || (addr & 1)) {
		val = iop13xx_atue_read(addr) >> ((where & 3) * 8);
		if( iop13xx_atue_pci_status(1) || is_atue_occdr_error() ) {
			__raw_writel(__raw_readl(IOP13XX_XBG_BECSR) & 3,
				IOP13XX_XBG_BECSR);
			val = 0xffffffff;
		}

		PRINTK("addr=%#0lx, val=%#010x", addr, val);
	} else
		val = 0xffffffff;

	*value = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop13xx_atue_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 value)
{
	unsigned long addr = iop13xx_atue_cfg_address(bus, devfn, where);
	u32 val;

	if (size != 4) {
		val = iop13xx_atue_read(addr);
		if (!iop13xx_atue_pci_status(1) == 0)
			return PCIBIOS_SUCCESSFUL;

		where = (where & 3) * 8;

		if (size == 1)
			val &= ~(0xff << where);
		else
			val &= ~(0xffff << where);

		__raw_writel(val | value << where, IOP13XX_ATUE_OCCDR);
	} else {
		__raw_writel(addr, IOP13XX_ATUE_OCCAR);
		__raw_writel(value, IOP13XX_ATUE_OCCDR);
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop13xx_atue_ops = {
	.read	= iop13xx_atue_read_config,
	.write	= iop13xx_atue_write_config,
};

/* When a PCI device does not exist during config cycles, the XScale gets a
 * bus error instead of returning 0xffffffff.  We can't rely on the ATU status
 * bits to tell us that it was indeed a configuration cycle that caused this
 * error especially in the case when the ATUE link is down.  Instead we rely
 * on data from the south XSI bridge to validate the abort
 */
int
iop13xx_pci_abort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	PRINTK("Data abort: address = 0x%08lx "
		    "fsr = 0x%03x PC = 0x%08lx LR = 0x%08lx",
		addr, fsr, regs->ARM_pc, regs->ARM_lr);

	PRINTK("IOP13XX_XBG_BECSR: %#10x", __raw_readl(IOP13XX_XBG_BECSR));
	PRINTK("IOP13XX_XBG_BERAR: %#10x", __raw_readl(IOP13XX_XBG_BERAR));
	PRINTK("IOP13XX_XBG_BERUAR: %#10x", __raw_readl(IOP13XX_XBG_BERUAR));

	/*  If it was an imprecise abort, then we need to correct the
	 *  return address to be _after_ the instruction.
	 */
	if (fsr & (1 << 10))
		regs->ARM_pc += 4;

	if (is_atue_occdr_error() || is_atux_occdr_error())
		return 0;
	else
		return 1;
}

/* Scan an IOP13XX PCI bus.  nr selects which ATU we use.
 */
struct pci_bus *iop13xx_scan_bus(int nr, struct pci_sys_data *sys)
{
	int which_atu;
	struct pci_bus *bus = NULL;

	switch (init_atu) {
	case IOP13XX_INIT_ATU_ATUX:
		which_atu = nr ? 0 : IOP13XX_INIT_ATU_ATUX;
		break;
	case IOP13XX_INIT_ATU_ATUE:
		which_atu = nr ? 0 : IOP13XX_INIT_ATU_ATUE;
		break;
	case (IOP13XX_INIT_ATU_ATUX | IOP13XX_INIT_ATU_ATUE):
		which_atu = nr ? IOP13XX_INIT_ATU_ATUE : IOP13XX_INIT_ATU_ATUX;
		break;
	default:
		which_atu = 0;
	}

	if (!which_atu) {
		BUG();
		return NULL;
	}

	switch (which_atu) {
	case IOP13XX_INIT_ATU_ATUX:
		if (time_after_eq(jiffies + msecs_to_jiffies(1000),
				  atux_trhfa_timeout))  /* ensure not wrap */
			while(time_before(jiffies, atux_trhfa_timeout))
				udelay(100);

		bus = pci_bus_atux = pci_scan_bus(sys->busnr,
						  &iop13xx_atux_ops,
						  sys);
		break;
	case IOP13XX_INIT_ATU_ATUE:
		bus = pci_bus_atue = pci_scan_bus(sys->busnr,
						  &iop13xx_atue_ops,
						  sys);
		break;
	}

	return bus;
}

/* This function is called from iop13xx_pci_init() after assigning valid
 * values to iop13xx_atue_pmmr_offset.  This is the location for common
 * setup of ATUE for all IOP13XX implementations.
 */
void __init iop13xx_atue_setup(void)
{
	int func = iop13xx_atu_function(IOP13XX_INIT_ATU_ATUE);
	u32 reg_val;

#ifdef CONFIG_PCI_MSI
	/* BAR 0 (inbound msi window) */
	__raw_writel(IOP13XX_MU_BASE_PHYS, IOP13XX_MU_MUBAR);
	__raw_writel(~(IOP13XX_MU_WINDOW_SIZE - 1), IOP13XX_ATUE_IALR0);
	__raw_writel(IOP13XX_MU_BASE_PHYS, IOP13XX_ATUE_IATVR0);
	__raw_writel(IOP13XX_MU_BASE_PCI, IOP13XX_ATUE_IABAR0);
#endif

	/* BAR 1 (1:1 mapping with Physical RAM) */
	/* Set limit and enable */
	__raw_writel(~(IOP13XX_MAX_RAM_SIZE - PHYS_OFFSET - 1) & ~0x1,
			IOP13XX_ATUE_IALR1);
	__raw_writel(0x0, IOP13XX_ATUE_IAUBAR1);

	/* Set base at the top of the reserved address space */
	__raw_writel(PHYS_OFFSET | PCI_BASE_ADDRESS_MEM_TYPE_64 |
			PCI_BASE_ADDRESS_MEM_PREFETCH, IOP13XX_ATUE_IABAR1);

	/* 1:1 mapping with physical ram
	 * (leave big endian byte swap disabled)
	 */
	 __raw_writel(0x0, IOP13XX_ATUE_IAUTVR1);
	 __raw_writel(PHYS_OFFSET, IOP13XX_ATUE_IATVR1);

	/* Outbound window 1 (PCIX/PCIE memory window) */
	/* 32 bit Address Space */
	__raw_writel(0x0, IOP13XX_ATUE_OUMWTVR1);
	/* PA[35:32] */
	__raw_writel(IOP13XX_ATUE_OUMBAR_ENABLE |
			(IOP13XX_PCIE_MEM_PHYS_OFFSET >> 32),
			IOP13XX_ATUE_OUMBAR1);

	/* Setup the I/O Bar
	 * A[35-16] in 31-12
	 */
	__raw_writel(((IOP13XX_PCIE_LOWER_IO_PA >> 0x4) & 0xfffff000),
		IOP13XX_ATUE_OIOBAR);
	__raw_writel(IOP13XX_PCIE_LOWER_IO_BA, IOP13XX_ATUE_OIOWTVR);

	/* clear startup errors */
	iop13xx_atue_pci_status(1);

	/* OIOBAR function number
	 */
	reg_val = __raw_readl(IOP13XX_ATUE_OIOBAR);
	reg_val &= ~0x7;
	reg_val |= func;
	__raw_writel(reg_val, IOP13XX_ATUE_OIOBAR);

	/* OUMBAR function numbers
	 */
	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR0);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR0);

	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR1);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR1);

	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR2);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR2);

	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR3);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR3);

	/* Enable inbound and outbound cycles
	 */
	reg_val = __raw_readw(IOP13XX_ATUE_ATUCMD);
	reg_val |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
			PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	__raw_writew(reg_val, IOP13XX_ATUE_ATUCMD);

	reg_val = __raw_readl(IOP13XX_ATUE_ATUCR);
	reg_val |= IOP13XX_ATUE_ATUCR_OUT_EN |
			IOP13XX_ATUE_ATUCR_IVM;
	__raw_writel(reg_val, IOP13XX_ATUE_ATUCR);
}

void __init iop13xx_atue_disable(void)
{
	u32 reg_val;

	__raw_writew(0x0, IOP13XX_ATUE_ATUCMD);
	__raw_writel(IOP13XX_ATUE_ATUCR_IVM, IOP13XX_ATUE_ATUCR);

	/* wait for cycles to quiesce */
	while (__raw_readl(IOP13XX_ATUE_PCSR) & (IOP13XX_ATUE_PCSR_OUT_Q_BUSY |
					     IOP13XX_ATUE_PCSR_IN_Q_BUSY |
					     IOP13XX_ATUE_PCSR_LLRB_BUSY))
		cpu_relax();

	/* BAR 0 ( Disabled ) */
	__raw_writel(0x0, IOP13XX_ATUE_IAUBAR0);
	__raw_writel(0x0, IOP13XX_ATUE_IABAR0);
	__raw_writel(0x0, IOP13XX_ATUE_IAUTVR0);
	__raw_writel(0x0, IOP13XX_ATUE_IATVR0);
	__raw_writel(0x0, IOP13XX_ATUE_IALR0);
	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR0);
	reg_val &= ~IOP13XX_ATUE_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR0);

	/* BAR 1 ( Disabled ) */
	__raw_writel(0x0, IOP13XX_ATUE_IAUBAR1);
	__raw_writel(0x0, IOP13XX_ATUE_IABAR1);
	__raw_writel(0x0, IOP13XX_ATUE_IAUTVR1);
	__raw_writel(0x0, IOP13XX_ATUE_IATVR1);
	__raw_writel(0x0, IOP13XX_ATUE_IALR1);
	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR1);
	reg_val &= ~IOP13XX_ATUE_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR1);

	/* BAR 2 ( Disabled ) */
	__raw_writel(0x0, IOP13XX_ATUE_IAUBAR2);
	__raw_writel(0x0, IOP13XX_ATUE_IABAR2);
	__raw_writel(0x0, IOP13XX_ATUE_IAUTVR2);
	__raw_writel(0x0, IOP13XX_ATUE_IATVR2);
	__raw_writel(0x0, IOP13XX_ATUE_IALR2);
	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR2);
	reg_val &= ~IOP13XX_ATUE_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR2);

	/* BAR 3 ( Disabled ) */
	reg_val = __raw_readl(IOP13XX_ATUE_OUMBAR3);
	reg_val &= ~IOP13XX_ATUE_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUE_OUMBAR3);

	/* Setup the I/O Bar
	 * A[35-16] in 31-12
	 */
	__raw_writel((IOP13XX_PCIE_LOWER_IO_PA >> 0x4) & 0xfffff000,
			IOP13XX_ATUE_OIOBAR);
	__raw_writel(IOP13XX_PCIE_LOWER_IO_BA, IOP13XX_ATUE_OIOWTVR);
}

/* This function is called from iop13xx_pci_init() after assigning valid
 * values to iop13xx_atux_pmmr_offset.  This is the location for common
 * setup of ATUX for all IOP13XX implementations.
 */
void __init iop13xx_atux_setup(void)
{
	u32 reg_val;
	int func = iop13xx_atu_function(IOP13XX_INIT_ATU_ATUX);

	/* Take PCI-X bus out of reset if bootloader hasn't already.
	 * According to spec, we should wait for 2^25 PCI clocks to meet
	 * the PCI timing parameter Trhfa (RST# high to first access).
	 * This is rarely necessary and often ignored.
	 */
	reg_val = __raw_readl(IOP13XX_ATUX_PCSR);
	if (reg_val & IOP13XX_ATUX_PCSR_P_RSTOUT) {
		int msec = (reg_val >> IOP13XX_ATUX_PCSR_FREQ_OFFSET) & 0x7;
		msec = 1000 / (8-msec); /* bits 100=133MHz, 111=>33MHz */
		__raw_writel(reg_val & ~IOP13XX_ATUX_PCSR_P_RSTOUT,
				IOP13XX_ATUX_PCSR);
		atux_trhfa_timeout = jiffies + msecs_to_jiffies(msec);
	}
	else
		atux_trhfa_timeout = jiffies;

#ifdef CONFIG_PCI_MSI
	/* BAR 0 (inbound msi window) */
	__raw_writel(IOP13XX_MU_BASE_PHYS, IOP13XX_MU_MUBAR);
	__raw_writel(~(IOP13XX_MU_WINDOW_SIZE - 1), IOP13XX_ATUX_IALR0);
	__raw_writel(IOP13XX_MU_BASE_PHYS, IOP13XX_ATUX_IATVR0);
	__raw_writel(IOP13XX_MU_BASE_PCI, IOP13XX_ATUX_IABAR0);
#endif

	/* BAR 1 (1:1 mapping with Physical RAM) */
	/* Set limit and enable */
	__raw_writel(~(IOP13XX_MAX_RAM_SIZE - PHYS_OFFSET - 1) & ~0x1,
			IOP13XX_ATUX_IALR1);
	__raw_writel(0x0, IOP13XX_ATUX_IAUBAR1);

	/* Set base at the top of the reserved address space */
	__raw_writel(PHYS_OFFSET | PCI_BASE_ADDRESS_MEM_TYPE_64 |
			PCI_BASE_ADDRESS_MEM_PREFETCH, IOP13XX_ATUX_IABAR1);

	/* 1:1 mapping with physical ram
	 * (leave big endian byte swap disabled)
	 */
	__raw_writel(0x0, IOP13XX_ATUX_IAUTVR1);
	__raw_writel(PHYS_OFFSET, IOP13XX_ATUX_IATVR1);

	/* Outbound window 1 (PCIX/PCIE memory window) */
	/* 32 bit Address Space */
	__raw_writel(0x0, IOP13XX_ATUX_OUMWTVR1);
	/* PA[35:32] */
	__raw_writel(IOP13XX_ATUX_OUMBAR_ENABLE |
			IOP13XX_PCIX_MEM_PHYS_OFFSET >> 32,
			IOP13XX_ATUX_OUMBAR1);

	/* Setup the I/O Bar
	 * A[35-16] in 31-12
	 */
	__raw_writel((IOP13XX_PCIX_LOWER_IO_PA >> 0x4) & 0xfffff000,
		IOP13XX_ATUX_OIOBAR);
	__raw_writel(IOP13XX_PCIX_LOWER_IO_BA, IOP13XX_ATUX_OIOWTVR);

	/* clear startup errors */
	iop13xx_atux_pci_status(1);

	/* OIOBAR function number
	 */
	reg_val = __raw_readl(IOP13XX_ATUX_OIOBAR);
	reg_val &= ~0x7;
	reg_val |= func;
	__raw_writel(reg_val, IOP13XX_ATUX_OIOBAR);

	/* OUMBAR function numbers
	 */
	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR0);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR0);

	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR1);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR1);

	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR2);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR2);

	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR3);
	reg_val &= ~(IOP13XX_ATU_OUMBAR_FUNC_NUM_MASK <<
			IOP13XX_ATU_OUMBAR_FUNC_NUM);
	reg_val |= func << IOP13XX_ATU_OUMBAR_FUNC_NUM;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR3);

	/* Enable inbound and outbound cycles
	 */
	reg_val = __raw_readw(IOP13XX_ATUX_ATUCMD);
	reg_val |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
		        PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	__raw_writew(reg_val, IOP13XX_ATUX_ATUCMD);

	reg_val = __raw_readl(IOP13XX_ATUX_ATUCR);
	reg_val |= IOP13XX_ATUX_ATUCR_OUT_EN;
	__raw_writel(reg_val, IOP13XX_ATUX_ATUCR);
}

void __init iop13xx_atux_disable(void)
{
	u32 reg_val;

	__raw_writew(0x0, IOP13XX_ATUX_ATUCMD);
	__raw_writel(0x0, IOP13XX_ATUX_ATUCR);

	/* wait for cycles to quiesce */
	while (__raw_readl(IOP13XX_ATUX_PCSR) & (IOP13XX_ATUX_PCSR_OUT_Q_BUSY |
				     IOP13XX_ATUX_PCSR_IN_Q_BUSY))
		cpu_relax();

	/* BAR 0 ( Disabled ) */
	__raw_writel(0x0, IOP13XX_ATUX_IAUBAR0);
	__raw_writel(0x0, IOP13XX_ATUX_IABAR0);
	__raw_writel(0x0, IOP13XX_ATUX_IAUTVR0);
	__raw_writel(0x0, IOP13XX_ATUX_IATVR0);
	__raw_writel(0x0, IOP13XX_ATUX_IALR0);
	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR0);
	reg_val &= ~IOP13XX_ATUX_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR0);

	/* BAR 1 ( Disabled ) */
	__raw_writel(0x0, IOP13XX_ATUX_IAUBAR1);
	__raw_writel(0x0, IOP13XX_ATUX_IABAR1);
	__raw_writel(0x0, IOP13XX_ATUX_IAUTVR1);
	__raw_writel(0x0, IOP13XX_ATUX_IATVR1);
	__raw_writel(0x0, IOP13XX_ATUX_IALR1);
	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR1);
	reg_val &= ~IOP13XX_ATUX_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR1);

	/* BAR 2 ( Disabled ) */
	__raw_writel(0x0, IOP13XX_ATUX_IAUBAR2);
	__raw_writel(0x0, IOP13XX_ATUX_IABAR2);
	__raw_writel(0x0, IOP13XX_ATUX_IAUTVR2);
	__raw_writel(0x0, IOP13XX_ATUX_IATVR2);
	__raw_writel(0x0, IOP13XX_ATUX_IALR2);
	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR2);
	reg_val &= ~IOP13XX_ATUX_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR2);

	/* BAR 3 ( Disabled ) */
	__raw_writel(0x0, IOP13XX_ATUX_IAUBAR3);
	__raw_writel(0x0, IOP13XX_ATUX_IABAR3);
	__raw_writel(0x0, IOP13XX_ATUX_IAUTVR3);
	__raw_writel(0x0, IOP13XX_ATUX_IATVR3);
	__raw_writel(0x0, IOP13XX_ATUX_IALR3);
	reg_val = __raw_readl(IOP13XX_ATUX_OUMBAR3);
	reg_val &= ~IOP13XX_ATUX_OUMBAR_ENABLE;
	__raw_writel(reg_val, IOP13XX_ATUX_OUMBAR3);

	/* Setup the I/O Bar
	* A[35-16] in 31-12
	*/
	__raw_writel((IOP13XX_PCIX_LOWER_IO_PA >> 0x4) & 0xfffff000,
			IOP13XX_ATUX_OIOBAR);
	__raw_writel(IOP13XX_PCIX_LOWER_IO_BA, IOP13XX_ATUX_OIOWTVR);
}

void __init iop13xx_set_atu_mmr_bases(void)
{
	/* Based on ESSR0, determine the ATU X/E offsets */
	switch(__raw_readl(IOP13XX_ESSR0) &
		(IOP13XX_CONTROLLER_ONLY | IOP13XX_INTERFACE_SEL_PCIX)) {
	/* both asserted */
	case 0:
		iop13xx_atux_pmmr_offset = IOP13XX_ATU1_PMMR_OFFSET;
		iop13xx_atue_pmmr_offset = IOP13XX_ATU2_PMMR_OFFSET;
		break;
	/* IOP13XX_CONTROLLER_ONLY = deasserted
	 * IOP13XX_INTERFACE_SEL_PCIX = asserted
	 */
	case IOP13XX_CONTROLLER_ONLY:
		iop13xx_atux_pmmr_offset = IOP13XX_ATU0_PMMR_OFFSET;
		iop13xx_atue_pmmr_offset = IOP13XX_ATU2_PMMR_OFFSET;
		break;
	/* IOP13XX_CONTROLLER_ONLY = asserted
	 * IOP13XX_INTERFACE_SEL_PCIX = deasserted
	 */
	case IOP13XX_INTERFACE_SEL_PCIX:
		iop13xx_atux_pmmr_offset = IOP13XX_ATU1_PMMR_OFFSET;
		iop13xx_atue_pmmr_offset = IOP13XX_ATU2_PMMR_OFFSET;
		break;
	/* both deasserted */
	case IOP13XX_CONTROLLER_ONLY | IOP13XX_INTERFACE_SEL_PCIX:
		iop13xx_atux_pmmr_offset = IOP13XX_ATU2_PMMR_OFFSET;
		iop13xx_atue_pmmr_offset = IOP13XX_ATU0_PMMR_OFFSET;
		break;
	default:
		BUG();
	}
}

void __init iop13xx_atu_select(struct hw_pci *plat_pci)
{
	int i;

	/* set system defaults
	 * note: if "iop13xx_init_atu=" is specified this autodetect
	 * sequence will be bypassed
	 */
	if (init_atu == IOP13XX_INIT_ATU_DEFAULT) {
		/* check for single/dual interface */
		if (__raw_readl(IOP13XX_ESSR0) & IOP13XX_INTERFACE_SEL_PCIX) {
			/* ATUE must be present check the device id
			 * to see if ATUX is present.
			 */
			init_atu |= IOP13XX_INIT_ATU_ATUE;
			switch (__raw_readw(IOP13XX_ATUE_DID) & 0xf0) {
			case 0x70:
			case 0x80:
			case 0xc0:
				init_atu |= IOP13XX_INIT_ATU_ATUX;
				break;
			}
		} else {
			/* ATUX must be present check the device id
			 * to see if ATUE is present.
			 */
			init_atu |= IOP13XX_INIT_ATU_ATUX;
			switch (__raw_readw(IOP13XX_ATUX_DID) & 0xf0) {
			case 0x70:
			case 0x80:
			case 0xc0:
				init_atu |= IOP13XX_INIT_ATU_ATUE;
				break;
			}
		}

		/* check central resource and root complex capability */
		if (init_atu & IOP13XX_INIT_ATU_ATUX)
			if (!(__raw_readl(IOP13XX_ATUX_PCSR) &
				IOP13XX_ATUX_PCSR_CENTRAL_RES))
				init_atu &= ~IOP13XX_INIT_ATU_ATUX;

		if (init_atu & IOP13XX_INIT_ATU_ATUE)
			if (__raw_readl(IOP13XX_ATUE_PCSR) &
				IOP13XX_ATUE_PCSR_END_POINT)
				init_atu &= ~IOP13XX_INIT_ATU_ATUE;
	}

	for (i = 0; i < 2; i++) {
		if((init_atu & (1 << i)) == (1 << i))
			plat_pci->nr_controllers++;
	}
}

void __init iop13xx_pci_init(void)
{
	/* clear pre-existing south bridge errors */
	__raw_writel(__raw_readl(IOP13XX_XBG_BECSR) & 3, IOP13XX_XBG_BECSR);

	/* Setup the Min Address for PCI memory... */
	iop13xx_pcibios_min_mem = IOP13XX_PCIX_LOWER_MEM_BA;

	/* if Linux is given control of an ATU
	 * clear out its prior configuration,
	 * otherwise do not touch the registers
	 */
	if (init_atu & IOP13XX_INIT_ATU_ATUE) {
		iop13xx_atue_disable();
		iop13xx_atue_setup();
	}

	if (init_atu & IOP13XX_INIT_ATU_ATUX) {
		iop13xx_atux_disable();
		iop13xx_atux_setup();
	}

	hook_fault_code(16+6, iop13xx_pci_abort, SIGBUS,
			"imprecise external abort");
}

/* intialize the pci memory space.  handle any combination of
 * atue and atux enabled/disabled
 */
int iop13xx_pci_setup(int nr, struct pci_sys_data *sys)
{
	struct resource *res;
	int which_atu;
	u32 pcixsr, pcsr;

	if (nr > 1)
		return 0;

	res = kmalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("PCI: unable to alloc resources");

	memset(res, 0, sizeof(struct resource) * 2);

	/* 'nr' assumptions:
	 * ATUX is always 0
	 * ATUE is 1 when ATUX is also enabled
	 * ATUE is 0 when ATUX is disabled
	 */
	switch(init_atu) {
	case IOP13XX_INIT_ATU_ATUX:
		which_atu = nr ? 0 : IOP13XX_INIT_ATU_ATUX;
		break;
	case IOP13XX_INIT_ATU_ATUE:
		which_atu = nr ? 0 : IOP13XX_INIT_ATU_ATUE;
		break;
	case (IOP13XX_INIT_ATU_ATUX | IOP13XX_INIT_ATU_ATUE):
		which_atu = nr ? IOP13XX_INIT_ATU_ATUE : IOP13XX_INIT_ATU_ATUX;
		break;
	default:
		which_atu = 0;
	}

	if (!which_atu)
		return 0;

	switch(which_atu) {
	case IOP13XX_INIT_ATU_ATUX:
		pcixsr = __raw_readl(IOP13XX_ATUX_PCIXSR);
		pcixsr &= ~0xffff;
		pcixsr |= sys->busnr << IOP13XX_ATUX_PCIXSR_BUS_NUM |
			  0 << IOP13XX_ATUX_PCIXSR_DEV_NUM |
			  iop13xx_atu_function(IOP13XX_INIT_ATU_ATUX)
				  << IOP13XX_ATUX_PCIXSR_FUNC_NUM;
		__raw_writel(pcixsr, IOP13XX_ATUX_PCIXSR);

		res[0].start = IOP13XX_PCIX_LOWER_IO_PA + IOP13XX_PCIX_IO_BUS_OFFSET;
		res[0].end   = IOP13XX_PCIX_UPPER_IO_PA;
		res[0].name  = "IQ81340 ATUX PCI I/O Space";
		res[0].flags = IORESOURCE_IO;

		res[1].start = IOP13XX_PCIX_LOWER_MEM_RA;
		res[1].end   = IOP13XX_PCIX_UPPER_MEM_RA;
		res[1].name  = "IQ81340 ATUX PCI Memory Space";
		res[1].flags = IORESOURCE_MEM;
		sys->mem_offset = IOP13XX_PCIX_MEM_OFFSET;
		sys->io_offset = IOP13XX_PCIX_LOWER_IO_PA;
		break;
	case IOP13XX_INIT_ATU_ATUE:
		/* Note: the function number field in the PCSR is ro */
		pcsr = __raw_readl(IOP13XX_ATUE_PCSR);
		pcsr &= ~(0xfff8 << 16);
		pcsr |= sys->busnr << IOP13XX_ATUE_PCSR_BUS_NUM |
				0 << IOP13XX_ATUE_PCSR_DEV_NUM;

		__raw_writel(pcsr, IOP13XX_ATUE_PCSR);

		res[0].start = IOP13XX_PCIE_LOWER_IO_PA + IOP13XX_PCIE_IO_BUS_OFFSET;
		res[0].end   = IOP13XX_PCIE_UPPER_IO_PA;
		res[0].name  = "IQ81340 ATUE PCI I/O Space";
		res[0].flags = IORESOURCE_IO;

		res[1].start = IOP13XX_PCIE_LOWER_MEM_RA;
		res[1].end   = IOP13XX_PCIE_UPPER_MEM_RA;
		res[1].name  = "IQ81340 ATUE PCI Memory Space";
		res[1].flags = IORESOURCE_MEM;
		sys->mem_offset = IOP13XX_PCIE_MEM_OFFSET;
		sys->io_offset = IOP13XX_PCIE_LOWER_IO_PA;
		sys->map_irq = iop13xx_pcie_map_irq;
		break;
	default:
		return 0;
	}

	request_resource(&ioport_resource, &res[0]);
	request_resource(&iomem_resource, &res[1]);

	sys->resource[0] = &res[0];
	sys->resource[1] = &res[1];
	sys->resource[2] = NULL;

	return 1;
}

u16 iop13xx_dev_id(void)
{
	if (__raw_readl(IOP13XX_ESSR0) & IOP13XX_INTERFACE_SEL_PCIX)
		return __raw_readw(IOP13XX_ATUE_DID);
	else
		return __raw_readw(IOP13XX_ATUX_DID);
}

static int __init iop13xx_init_atu_setup(char *str)
{
        init_atu = IOP13XX_INIT_ATU_NONE;
        if (str) {
                while (*str != '\0') {
                        switch (*str) {
                        case 'x':
                        case 'X':
                                init_atu |= IOP13XX_INIT_ATU_ATUX;
                                init_atu &= ~IOP13XX_INIT_ATU_NONE;
                                break;
                        case 'e':
                        case 'E':
                                init_atu |= IOP13XX_INIT_ATU_ATUE;
                                init_atu &= ~IOP13XX_INIT_ATU_NONE;
                                break;
                        case ',':
                        case '=':
                                break;
                        default:
                                PRINTK("\"iop13xx_init_atu\" malformed at "
                                            "character: \'%c\'", *str);
                                *(str + 1) = '\0';
                                init_atu = IOP13XX_INIT_ATU_DEFAULT;
                        }
                        str++;
                }
        }
        return 1;
}

__setup("iop13xx_init_atu", iop13xx_init_atu_setup);
