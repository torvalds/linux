/*
 * Support for SCC external PCI
 *
 * (C) Copyright 2004-2007 TOSHIBA CORPORATION
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
#include <linux/threads.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pci_regs.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>

#include "scc.h"
#include "pci.h"
#include "interrupt.h"

#define MAX_PCI_DEVICES   32
#define MAX_PCI_FUNCTIONS  8

#define iob()  __asm__ __volatile__("eieio; sync":::"memory")

static inline volatile void __iomem *celleb_epci_get_epci_base(
					struct pci_controller *hose)
{
	/*
	 * Note:
	 * Celleb epci uses cfg_addr as a base address for
	 * epci control registers.
	 */

	return hose->cfg_addr;
}

static inline volatile void __iomem *celleb_epci_get_epci_cfg(
					struct pci_controller *hose)
{
	/*
	 * Note:
	 * Celleb epci uses cfg_data as a base address for
	 * configuration area for epci devices.
	 */

	return hose->cfg_data;
}

#if 0 /* test code for epci dummy read */
static void celleb_epci_dummy_read(struct pci_dev *dev)
{
	volatile void __iomem *epci_base;
	struct device_node *node;
	struct pci_controller *hose;
	u32 val;

	node = (struct device_node *)dev->bus->sysdata;
	hose = pci_find_hose_for_OF_device(node);

	if (!hose)
		return;

	epci_base = celleb_epci_get_epci_base(hose);

	val = in_be32(epci_base + SCC_EPCI_WATRP);
	iosync();

	return;
}
#endif

static inline void clear_and_disable_master_abort_interrupt(
					struct pci_controller *hose)
{
	volatile void __iomem *epci_base, *reg;
	epci_base = celleb_epci_get_epci_base(hose);
	reg = epci_base + PCI_COMMAND;
	out_be32(reg, in_be32(reg) | (PCI_STATUS_REC_MASTER_ABORT << 16));
}

static int celleb_epci_check_abort(struct pci_controller *hose,
				   volatile void __iomem *addr)
{
	volatile void __iomem *reg, *epci_base;
	u32 val;

	iob();
	epci_base = celleb_epci_get_epci_base(hose);

	reg = epci_base + PCI_COMMAND;
	val = in_be32(reg);

	if (val & (PCI_STATUS_REC_MASTER_ABORT << 16)) {
		out_be32(reg,
			 (val & 0xffff) | (PCI_STATUS_REC_MASTER_ABORT << 16));

		/* clear PCI Controller error, FRE, PMFE */
		reg = epci_base + SCC_EPCI_STATUS;
		out_be32(reg, SCC_EPCI_INT_PAI);

		reg = epci_base + SCC_EPCI_VCSR;
		val = in_be32(reg) & 0xffff;
		val |= SCC_EPCI_VCSR_FRE;
		out_be32(reg, val);

		reg = epci_base + SCC_EPCI_VISTAT;
		out_be32(reg, SCC_EPCI_VISTAT_PMFE);
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	return PCIBIOS_SUCCESSFUL;
}

static volatile void __iomem *celleb_epci_make_config_addr(
					struct pci_controller *hose,
					unsigned int devfn, int where)
{
	volatile void __iomem *addr;
	struct pci_bus *bus = hose->bus;

	if (bus->self)
		addr = celleb_epci_get_epci_cfg(hose) +
		       (((bus->number & 0xff) << 16)
		        | ((devfn & 0xff) << 8)
		        | (where & 0xff)
		        | 0x01000000);
	else
		addr = celleb_epci_get_epci_cfg(hose) +
		       (((devfn & 0xff) << 8) | (where & 0xff));

	pr_debug("EPCI: config_addr = 0x%p\n", addr);

	return addr;
}

static int celleb_epci_read_config(struct pci_bus *bus,
			unsigned int devfn, int where, int size, u32 * val)
{
	volatile void __iomem *epci_base, *addr;
	struct device_node *node;
	struct pci_controller *hose;

	/* allignment check */
	BUG_ON(where % size);

	node = (struct device_node *)bus->sysdata;
	hose = pci_find_hose_for_OF_device(node);

	if (!celleb_epci_get_epci_cfg(hose))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == hose->first_busno && devfn == 0) {
		/* EPCI controller self */

		epci_base = celleb_epci_get_epci_base(hose);
		addr = epci_base + where;

		switch (size) {
		case 1:
			*val = in_8(addr);
			break;
		case 2:
			*val = in_be16(addr);
			break;
		case 4:
			*val = in_be32(addr);
			break;
		default:
			return PCIBIOS_DEVICE_NOT_FOUND;
		}

	} else {

		clear_and_disable_master_abort_interrupt(hose);
		addr = celleb_epci_make_config_addr(hose, devfn, where);

		switch (size) {
		case 1:
			*val = in_8(addr);
			break;
		case 2:
			*val = in_le16(addr);
			break;
		case 4:
			*val = in_le32(addr);
			break;
		default:
			return PCIBIOS_DEVICE_NOT_FOUND;
		}
	}

	pr_debug("EPCI: "
		 "addr=0x%p, devfn=0x%x, where=0x%x, size=0x%x, val=0x%x\n",
		 addr, devfn, where, size, *val);

	return celleb_epci_check_abort(hose, NULL);
}

static int celleb_epci_write_config(struct pci_bus *bus,
			unsigned int devfn, int where, int size, u32 val)
{
	volatile void __iomem *epci_base, *addr;
	struct device_node *node;
	struct pci_controller *hose;

	/* allignment check */
	BUG_ON(where % size);

	node = (struct device_node *)bus->sysdata;
	hose = pci_find_hose_for_OF_device(node);


	if (!celleb_epci_get_epci_cfg(hose))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == hose->first_busno && devfn == 0) {
		/* EPCI controller self */

		epci_base = celleb_epci_get_epci_base(hose);
		addr = epci_base + where;

		switch (size) {
		case 1:
			out_8(addr, val);
			break;
		case 2:
			out_be16(addr, val);
			break;
		case 4:
			out_be32(addr, val);
			break;
		default:
			return PCIBIOS_DEVICE_NOT_FOUND;
		}

	} else {

		clear_and_disable_master_abort_interrupt(hose);
		addr = celleb_epci_make_config_addr(hose, devfn, where);

		switch (size) {
		case 1:
			out_8(addr, val);
			break;
		case 2:
			out_le16(addr, val);
			break;
		case 4:
			out_le32(addr, val);
			break;
		default:
			return PCIBIOS_DEVICE_NOT_FOUND;
		}
	}

	return celleb_epci_check_abort(hose, addr);
}

struct pci_ops celleb_epci_ops = {
	celleb_epci_read_config,
	celleb_epci_write_config,
};

/* to be moved in FW */
static int __devinit celleb_epci_init(struct pci_controller *hose)
{
	u32 val;
	volatile void __iomem *reg, *epci_base;
	int hwres = 0;

	epci_base = celleb_epci_get_epci_base(hose);

	/* PCI core reset(Internal bus and PCI clock) */
	reg = epci_base + SCC_EPCI_CKCTRL;
	val = in_be32(reg);
	if (val == 0x00030101)
		hwres = 1;
	else {
		val &= ~(SCC_EPCI_CKCTRL_CRST0 | SCC_EPCI_CKCTRL_CRST1);
		out_be32(reg, val);

		/* set PCI core clock */
		val = in_be32(reg);
		val |= (SCC_EPCI_CKCTRL_OCLKEN | SCC_EPCI_CKCTRL_LCLKEN);
		out_be32(reg, val);

		/* release PCI core reset (internal bus) */
		val = in_be32(reg);
		val |= SCC_EPCI_CKCTRL_CRST0;
		out_be32(reg, val);

		/* set PCI clock select */
		reg = epci_base + SCC_EPCI_CLKRST;
		val = in_be32(reg);
		val &= ~SCC_EPCI_CLKRST_CKS_MASK;
		val |= SCC_EPCI_CLKRST_CKS_2;
		out_be32(reg, val);

		/* set arbiter */
		reg = epci_base + SCC_EPCI_ABTSET;
		out_be32(reg, 0x0f1f001f);	/* temporary value */

		/* buffer on */
		reg = epci_base + SCC_EPCI_CLKRST;
		val = in_be32(reg);
		val |= SCC_EPCI_CLKRST_BC;
		out_be32(reg, val);

		/* PCI clock enable */
		val = in_be32(reg);
		val |= SCC_EPCI_CLKRST_PCKEN;
		out_be32(reg, val);

		/* release PCI core reset (all) */
		reg = epci_base + SCC_EPCI_CKCTRL;
		val = in_be32(reg);
		val |= (SCC_EPCI_CKCTRL_CRST0 | SCC_EPCI_CKCTRL_CRST1);
		out_be32(reg, val);

		/* set base translation registers. (already set by Beat) */

		/* set base address masks. (already set by Beat) */
	}

	/* release interrupt masks and clear all interrupts */
	reg = epci_base + SCC_EPCI_INTSET;
	out_be32(reg, 0x013f011f);	/* all interrupts enable */
	reg = epci_base + SCC_EPCI_VIENAB;
	val = SCC_EPCI_VIENAB_PMPEE | SCC_EPCI_VIENAB_PMFEE;
	out_be32(reg, val);
	reg = epci_base + SCC_EPCI_STATUS;
	out_be32(reg, 0xffffffff);
	reg = epci_base + SCC_EPCI_VISTAT;
	out_be32(reg, 0xffffffff);

	/* disable PCI->IB address translation */
	reg = epci_base + SCC_EPCI_VCSR;
	val = in_be32(reg);
	val &= ~(SCC_EPCI_VCSR_DR | SCC_EPCI_VCSR_AT);
	out_be32(reg, val);

	/* set base addresses. (no need to set?) */

	/* memory space, bus master enable */
	reg = epci_base + PCI_COMMAND;
	val = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	out_be32(reg, val);

	/* endian mode setup */
	reg = epci_base + SCC_EPCI_ECMODE;
	val = 0x00550155;
	out_be32(reg, val);

	/* set control option */
	reg = epci_base + SCC_EPCI_CNTOPT;
	val = in_be32(reg);
	val |= SCC_EPCI_CNTOPT_O2PMB;
	out_be32(reg, val);

	/* XXX: temporay: set registers for address conversion setup */
	reg = epci_base + SCC_EPCI_CNF10_REG;
	out_be32(reg, 0x80000008);
	reg = epci_base + SCC_EPCI_CNF14_REG;
	out_be32(reg, 0x40000008);

	reg = epci_base + SCC_EPCI_BAM0;
	out_be32(reg, 0x80000000);
	reg = epci_base + SCC_EPCI_BAM1;
	out_be32(reg, 0xe0000000);

	reg = epci_base + SCC_EPCI_PVBAT;
	out_be32(reg, 0x80000000);

	if (!hwres) {
		/* release external PCI reset */
		reg = epci_base + SCC_EPCI_CLKRST;
		val = in_be32(reg);
		val |= SCC_EPCI_CLKRST_PCIRST;
		out_be32(reg, val);
	}

	return 0;
}

int __devinit celleb_setup_epci(struct device_node *node,
				struct pci_controller *hose)
{
	struct resource r;

	pr_debug("PCI: celleb_setup_epci()\n");

	/*
	 * Note:
	 * Celleb epci uses cfg_addr and cfg_data member of
	 * pci_controller structure in irregular way.
	 *
	 * cfg_addr is used to map for control registers of
	 * celleb epci.
	 *
	 * cfg_data is used for configuration area of devices
	 * on Celleb epci buses.
	 */

	if (of_address_to_resource(node, 0, &r))
		goto error;
	hose->cfg_addr = ioremap(r.start, (r.end - r.start + 1));
	if (!hose->cfg_addr)
		goto error;
	pr_debug("EPCI: cfg_addr map 0x%016lx->0x%016lx + 0x%016lx\n",
		 r.start, (unsigned long)hose->cfg_addr,
		(r.end - r.start + 1));

	if (of_address_to_resource(node, 2, &r))
		goto error;
	hose->cfg_data = ioremap(r.start, (r.end - r.start + 1));
	if (!hose->cfg_data)
		goto error;
	pr_debug("EPCI: cfg_data map 0x%016lx->0x%016lx + 0x%016lx\n",
		 r.start, (unsigned long)hose->cfg_data,
		(r.end - r.start + 1));

	celleb_epci_init(hose);

	return 0;

error:
	return 1;
}
