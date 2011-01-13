/*
 * Support for Celleb PCI-Express.
 *
 * (C) Copyright 2007-2008 TOSHIBA CORPORATION
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/iommu.h>
#include <asm/byteorder.h>

#include "celleb_scc.h"
#include "celleb_pci.h"

#define PEX_IN(base, off)	in_be32((void __iomem *)(base) + (off))
#define PEX_OUT(base, off, data) out_be32((void __iomem *)(base) + (off), (data))

static void scc_pciex_io_flush(struct iowa_bus *bus)
{
	(void)PEX_IN(bus->phb->cfg_addr, PEXDMRDEN0);
}

/*
 * Memory space access to device on PCIEX
 */
#define PCIEX_MMIO_READ(name, ret)					\
static ret scc_pciex_##name(const PCI_IO_ADDR addr)			\
{									\
	ret val = __do_##name(addr);					\
	scc_pciex_io_flush(iowa_mem_find_bus(addr));			\
	return val;							\
}

#define PCIEX_MMIO_READ_STR(name)					\
static void scc_pciex_##name(const PCI_IO_ADDR addr, void *buf,		\
			     unsigned long count)			\
{									\
	__do_##name(addr, buf, count);					\
	scc_pciex_io_flush(iowa_mem_find_bus(addr));			\
}

PCIEX_MMIO_READ(readb, u8)
PCIEX_MMIO_READ(readw, u16)
PCIEX_MMIO_READ(readl, u32)
PCIEX_MMIO_READ(readq, u64)
PCIEX_MMIO_READ(readw_be, u16)
PCIEX_MMIO_READ(readl_be, u32)
PCIEX_MMIO_READ(readq_be, u64)
PCIEX_MMIO_READ_STR(readsb)
PCIEX_MMIO_READ_STR(readsw)
PCIEX_MMIO_READ_STR(readsl)

static void scc_pciex_memcpy_fromio(void *dest, const PCI_IO_ADDR src,
				    unsigned long n)
{
	__do_memcpy_fromio(dest, src, n);
	scc_pciex_io_flush(iowa_mem_find_bus(src));
}

/*
 * I/O port access to devices on PCIEX.
 */

static inline unsigned long get_bus_address(struct pci_controller *phb,
					    unsigned long port)
{
	return port - ((unsigned long)(phb->io_base_virt) - _IO_BASE);
}

static u32 scc_pciex_read_port(struct pci_controller *phb,
			       unsigned long port, int size)
{
	unsigned int byte_enable;
	unsigned int cmd, shift;
	unsigned long addr;
	u32 data, ret;

	BUG_ON(((port & 0x3ul) + size) > 4);

	addr = get_bus_address(phb, port);
	shift = addr & 0x3ul;
	byte_enable = ((1 << size) - 1) << shift;
	cmd = PEXDCMND_IO_READ | (byte_enable << PEXDCMND_BYTE_EN_SHIFT);
	PEX_OUT(phb->cfg_addr, PEXDADRS, (addr & ~0x3ul));
	PEX_OUT(phb->cfg_addr, PEXDCMND, cmd);
	data = PEX_IN(phb->cfg_addr, PEXDRDATA);
	ret = (data >> (shift * 8)) & (0xFFFFFFFF >> ((4 - size) * 8));

	pr_debug("PCIEX:PIO READ:port=0x%lx, addr=0x%lx, size=%d, be=%x,"
		 " cmd=%x, data=%x, ret=%x\n", port, addr, size, byte_enable,
		 cmd, data, ret);

	return ret;
}

static void scc_pciex_write_port(struct pci_controller *phb,
				 unsigned long port, int size, u32 val)
{
	unsigned int byte_enable;
	unsigned int cmd, shift;
	unsigned long addr;
	u32 data;

	BUG_ON(((port & 0x3ul) + size) > 4);

	addr = get_bus_address(phb, port);
	shift = addr & 0x3ul;
	byte_enable = ((1 << size) - 1) << shift;
	cmd = PEXDCMND_IO_WRITE | (byte_enable << PEXDCMND_BYTE_EN_SHIFT);
	data = (val & (0xFFFFFFFF >> (4 - size) * 8)) << (shift * 8);
	PEX_OUT(phb->cfg_addr, PEXDADRS, (addr & ~0x3ul));
	PEX_OUT(phb->cfg_addr, PEXDCMND, cmd);
	PEX_OUT(phb->cfg_addr, PEXDWDATA, data);

	pr_debug("PCIEX:PIO WRITE:port=0x%lx, addr=%lx, size=%d, val=%x,"
		 " be=%x, cmd=%x, data=%x\n", port, addr, size, val,
		 byte_enable, cmd, data);
}

static u8 __scc_pciex_inb(struct pci_controller *phb, unsigned long port)
{
	return (u8)scc_pciex_read_port(phb, port, 1);
}

static u16 __scc_pciex_inw(struct pci_controller *phb, unsigned long port)
{
	u32 data;
	if ((port & 0x3ul) < 3)
		data = scc_pciex_read_port(phb, port, 2);
	else {
		u32 d1 = scc_pciex_read_port(phb, port, 1);
		u32 d2 = scc_pciex_read_port(phb, port + 1, 1);
		data = d1 | (d2 << 8);
	}
	return (u16)data;
}

static u32 __scc_pciex_inl(struct pci_controller *phb, unsigned long port)
{
	unsigned int mod = port & 0x3ul;
	u32 data;
	if (mod == 0)
		data = scc_pciex_read_port(phb, port, 4);
	else {
		u32 d1 = scc_pciex_read_port(phb, port, 4 - mod);
		u32 d2 = scc_pciex_read_port(phb, port + 1, mod);
		data = d1 | (d2 << (mod * 8));
	}
	return data;
}

static void __scc_pciex_outb(struct pci_controller *phb,
			     u8 val, unsigned long port)
{
	scc_pciex_write_port(phb, port, 1, (u32)val);
}

static void __scc_pciex_outw(struct pci_controller *phb,
			     u16 val, unsigned long port)
{
	if ((port & 0x3ul) < 3)
		scc_pciex_write_port(phb, port, 2, (u32)val);
	else {
		u32 d1 = val & 0x000000FF;
		u32 d2 = (val & 0x0000FF00) >> 8;
		scc_pciex_write_port(phb, port, 1, d1);
		scc_pciex_write_port(phb, port + 1, 1, d2);
	}
}

static void __scc_pciex_outl(struct pci_controller *phb,
			     u32 val, unsigned long port)
{
	unsigned int mod = port & 0x3ul;
	if (mod == 0)
		scc_pciex_write_port(phb, port, 4, val);
	else {
		u32 d1 = val & (0xFFFFFFFFul >> (mod * 8));
		u32 d2 = val >> ((4 - mod) * 8);
		scc_pciex_write_port(phb, port, 4 - mod, d1);
		scc_pciex_write_port(phb, port + 1, mod, d2);
	}
}

#define PCIEX_PIO_FUNC(size, name)					\
static u##size scc_pciex_in##name(unsigned long port)			\
{									\
	struct iowa_bus *bus = iowa_pio_find_bus(port);			\
	u##size data = __scc_pciex_in##name(bus->phb, port);		\
	scc_pciex_io_flush(bus);					\
	return data;							\
}									\
static void scc_pciex_ins##name(unsigned long p, void *b, unsigned long c) \
{									\
	struct iowa_bus *bus = iowa_pio_find_bus(p);			\
	__le##size *dst = b;						\
	for (; c != 0; c--, dst++)					\
		*dst = cpu_to_le##size(__scc_pciex_in##name(bus->phb, p)); \
	scc_pciex_io_flush(bus);					\
}									\
static void scc_pciex_out##name(u##size val, unsigned long port)	\
{									\
	struct iowa_bus *bus = iowa_pio_find_bus(port);			\
	__scc_pciex_out##name(bus->phb, val, port);			\
}									\
static void scc_pciex_outs##name(unsigned long p, const void *b,	\
				 unsigned long c)			\
{									\
	struct iowa_bus *bus = iowa_pio_find_bus(p);			\
	const __le##size *src = b;					\
	for (; c != 0; c--, src++)					\
		__scc_pciex_out##name(bus->phb, le##size##_to_cpu(*src), p); \
}
#define __le8 u8
#define cpu_to_le8(x) (x)
#define le8_to_cpu(x) (x)
PCIEX_PIO_FUNC(8, b)
PCIEX_PIO_FUNC(16, w)
PCIEX_PIO_FUNC(32, l)

static struct ppc_pci_io scc_pciex_ops = {
	.readb = scc_pciex_readb,
	.readw = scc_pciex_readw,
	.readl = scc_pciex_readl,
	.readq = scc_pciex_readq,
	.readw_be = scc_pciex_readw_be,
	.readl_be = scc_pciex_readl_be,
	.readq_be = scc_pciex_readq_be,
	.readsb = scc_pciex_readsb,
	.readsw = scc_pciex_readsw,
	.readsl = scc_pciex_readsl,
	.memcpy_fromio = scc_pciex_memcpy_fromio,
	.inb = scc_pciex_inb,
	.inw = scc_pciex_inw,
	.inl = scc_pciex_inl,
	.outb = scc_pciex_outb,
	.outw = scc_pciex_outw,
	.outl = scc_pciex_outl,
	.insb = scc_pciex_insb,
	.insw = scc_pciex_insw,
	.insl = scc_pciex_insl,
	.outsb = scc_pciex_outsb,
	.outsw = scc_pciex_outsw,
	.outsl = scc_pciex_outsl,
};

static int __init scc_pciex_iowa_init(struct iowa_bus *bus, void *data)
{
	dma_addr_t dummy_page_da;
	void *dummy_page_va;

	dummy_page_va = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!dummy_page_va) {
		pr_err("PCIEX:Alloc dummy_page_va failed\n");
		return -1;
	}

	dummy_page_da = dma_map_single(bus->phb->parent, dummy_page_va,
				       PAGE_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(bus->phb->parent, dummy_page_da)) {
		pr_err("PCIEX:Map dummy page failed.\n");
		kfree(dummy_page_va);
		return -1;
	}

	PEX_OUT(bus->phb->cfg_addr, PEXDMRDADR0, dummy_page_da);

	return 0;
}

/*
 * config space access
 */
#define MK_PEXDADRS(bus_no, dev_no, func_no, addr) \
	((uint32_t)(((addr) & ~0x3UL) | \
	((bus_no) << PEXDADRS_BUSNO_SHIFT) | \
	((dev_no)  << PEXDADRS_DEVNO_SHIFT) | \
	((func_no) << PEXDADRS_FUNCNO_SHIFT)))

#define MK_PEXDCMND_BYTE_EN(addr, size) \
	((((0x1 << (size))-1) << ((addr) & 0x3)) << PEXDCMND_BYTE_EN_SHIFT)
#define MK_PEXDCMND(cmd, addr, size) ((cmd) | MK_PEXDCMND_BYTE_EN(addr, size))

static uint32_t config_read_pciex_dev(unsigned int __iomem *base,
		uint64_t bus_no, uint64_t dev_no, uint64_t func_no,
		uint64_t off, uint64_t size)
{
	uint32_t ret;
	uint32_t addr, cmd;

	addr = MK_PEXDADRS(bus_no, dev_no, func_no, off);
	cmd = MK_PEXDCMND(PEXDCMND_CONFIG_READ, off, size);
	PEX_OUT(base, PEXDADRS, addr);
	PEX_OUT(base, PEXDCMND, cmd);
	ret = (PEX_IN(base, PEXDRDATA)
		>> ((off & (4-size)) * 8)) & ((0x1 << (size * 8)) - 1);
	return ret;
}

static void config_write_pciex_dev(unsigned int __iomem *base, uint64_t bus_no,
	uint64_t dev_no, uint64_t func_no, uint64_t off, uint64_t size,
	uint32_t data)
{
	uint32_t addr, cmd;

	addr = MK_PEXDADRS(bus_no, dev_no, func_no, off);
	cmd = MK_PEXDCMND(PEXDCMND_CONFIG_WRITE, off, size);
	PEX_OUT(base, PEXDADRS, addr);
	PEX_OUT(base, PEXDCMND, cmd);
	PEX_OUT(base, PEXDWDATA,
		(data & ((0x1 << (size * 8)) - 1)) << ((off & (4-size)) * 8));
}

#define MK_PEXCADRS_BYTE_EN(off, len) \
	((((0x1 << (len)) - 1) << ((off) & 0x3)) << PEXCADRS_BYTE_EN_SHIFT)
#define MK_PEXCADRS(cmd, addr, size) \
	((cmd) | MK_PEXCADRS_BYTE_EN(addr, size) | ((addr) & ~0x3))
static uint32_t config_read_pciex_rc(unsigned int __iomem *base,
				     uint32_t where, uint32_t size)
{
	PEX_OUT(base, PEXCADRS, MK_PEXCADRS(PEXCADRS_CMD_READ, where, size));
	return (PEX_IN(base, PEXCRDATA)
		>> ((where & (4 - size)) * 8)) & ((0x1 << (size * 8)) - 1);
}

static void config_write_pciex_rc(unsigned int __iomem *base, uint32_t where,
				  uint32_t size, uint32_t val)
{
	uint32_t data;

	data = (val & ((0x1 << (size * 8)) - 1)) << ((where & (4 - size)) * 8);
	PEX_OUT(base, PEXCADRS, MK_PEXCADRS(PEXCADRS_CMD_WRITE, where, size));
	PEX_OUT(base, PEXCWDATA, data);
}

/* Interfaces */
/* Note: Work-around
 *  On SCC PCIEXC, one device is seen on all 32 dev_no.
 *  As SCC PCIEXC can have only one device on the bus, we look only one dev_no.
 * (dev_no = 1)
 */
static int scc_pciex_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, unsigned int *val)
{
	struct pci_controller *phb = pci_bus_to_host(bus);

	if (bus->number == phb->first_busno && PCI_SLOT(devfn) != 1) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number == 0 && PCI_SLOT(devfn) == 0)
		*val = config_read_pciex_rc(phb->cfg_addr, where, size);
	else
		*val = config_read_pciex_dev(phb->cfg_addr, bus->number,
				PCI_SLOT(devfn), PCI_FUNC(devfn), where, size);

	return PCIBIOS_SUCCESSFUL;
}

static int scc_pciex_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, unsigned int val)
{
	struct pci_controller *phb = pci_bus_to_host(bus);

	if (bus->number == phb->first_busno && PCI_SLOT(devfn) != 1)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == 0 && PCI_SLOT(devfn) == 0)
		config_write_pciex_rc(phb->cfg_addr, where, size, val);
	else
		config_write_pciex_dev(phb->cfg_addr, bus->number,
			PCI_SLOT(devfn), PCI_FUNC(devfn), where, size, val);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops scc_pciex_pci_ops = {
	scc_pciex_read_config,
	scc_pciex_write_config,
};

static void pciex_clear_intr_all(unsigned int __iomem *base)
{
	PEX_OUT(base, PEXAERRSTS, 0xffffffff);
	PEX_OUT(base, PEXPRERRSTS, 0xffffffff);
	PEX_OUT(base, PEXINTSTS, 0xffffffff);
}

#if 0
static void pciex_disable_intr_all(unsigned int *base)
{
	PEX_OUT(base, PEXINTMASK,   0x0);
	PEX_OUT(base, PEXAERRMASK,  0x0);
	PEX_OUT(base, PEXPRERRMASK, 0x0);
	PEX_OUT(base, PEXVDMASK,    0x0);
}
#endif

static void pciex_enable_intr_all(unsigned int __iomem *base)
{
	PEX_OUT(base, PEXINTMASK, 0x0000e7f1);
	PEX_OUT(base, PEXAERRMASK, 0x03ff01ff);
	PEX_OUT(base, PEXPRERRMASK, 0x0001010f);
	PEX_OUT(base, PEXVDMASK, 0x00000001);
}

static void pciex_check_status(unsigned int __iomem *base)
{
	uint32_t err = 0;
	uint32_t intsts, aerr, prerr, rcvcp, lenerr;
	uint32_t maea, maec;

	intsts = PEX_IN(base, PEXINTSTS);
	aerr = PEX_IN(base, PEXAERRSTS);
	prerr = PEX_IN(base, PEXPRERRSTS);
	rcvcp = PEX_IN(base, PEXRCVCPLIDA);
	lenerr = PEX_IN(base, PEXLENERRIDA);

	if (intsts || aerr || prerr || rcvcp || lenerr)
		err = 1;

	pr_info("PCEXC interrupt!!\n");
	pr_info("PEXINTSTS    :0x%08x\n", intsts);
	pr_info("PEXAERRSTS   :0x%08x\n", aerr);
	pr_info("PEXPRERRSTS  :0x%08x\n", prerr);
	pr_info("PEXRCVCPLIDA :0x%08x\n", rcvcp);
	pr_info("PEXLENERRIDA :0x%08x\n", lenerr);

	/* print detail of Protection Error */
	if (intsts & 0x00004000) {
		uint32_t i, n;
		for (i = 0; i < 4; i++) {
			n = 1 << i;
			if (prerr & n) {
				maea = PEX_IN(base, PEXMAEA(i));
				maec = PEX_IN(base, PEXMAEC(i));
				pr_info("PEXMAEC%d     :0x%08x\n", i, maec);
				pr_info("PEXMAEA%d     :0x%08x\n", i, maea);
			}
		}
	}

	if (err)
		pciex_clear_intr_all(base);
}

static irqreturn_t pciex_handle_internal_irq(int irq, void *dev_id)
{
	struct pci_controller *phb = dev_id;

	pr_debug("PCIEX:pciex_handle_internal_irq(irq=%d)\n", irq);

	BUG_ON(phb->cfg_addr == NULL);

	pciex_check_status(phb->cfg_addr);

	return IRQ_HANDLED;
}

static __init int celleb_setup_pciex(struct device_node *node,
				     struct pci_controller *phb)
{
	struct resource	r;
	struct of_irq oirq;
	int virq;

	/* SMMIO registers; used inside this file */
	if (of_address_to_resource(node, 0, &r)) {
		pr_err("PCIEXC:Failed to get config resource.\n");
		return 1;
	}
	phb->cfg_addr = ioremap(r.start, r.end - r.start + 1);
	if (!phb->cfg_addr) {
		pr_err("PCIEXC:Failed to remap SMMIO region.\n");
		return 1;
	}

	/* Not use cfg_data,  cmd and data regs are near address reg */
	phb->cfg_data = NULL;

	/* set pci_ops */
	phb->ops = &scc_pciex_pci_ops;

	/* internal interrupt handler */
	if (of_irq_map_one(node, 1, &oirq)) {
		pr_err("PCIEXC:Failed to map irq\n");
		goto error;
	}
	virq = irq_create_of_mapping(oirq.controller, oirq.specifier,
				     oirq.size);
	if (request_irq(virq, pciex_handle_internal_irq,
			IRQF_DISABLED, "pciex", (void *)phb)) {
		pr_err("PCIEXC:Failed to request irq\n");
		goto error;
	}

	/* enable all interrupts */
	pciex_clear_intr_all(phb->cfg_addr);
	pciex_enable_intr_all(phb->cfg_addr);
	/* MSI: TBD */

	return 0;

error:
	phb->cfg_data = NULL;
	if (phb->cfg_addr)
		iounmap(phb->cfg_addr);
	phb->cfg_addr = NULL;
	return 1;
}

struct celleb_phb_spec celleb_pciex_spec __initdata = {
	.setup = celleb_setup_pciex,
	.ops = &scc_pciex_ops,
	.iowa_init = &scc_pciex_iowa_init,
};
