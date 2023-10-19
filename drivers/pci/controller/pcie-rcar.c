// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Renesas R-Car SoCs
 *  Copyright (C) 2014-2020 Renesas Electronics Europe Ltd
 *
 * Author: Phil Edworthy <phil.edworthy@renesas.com>
 */

#include <linux/delay.h>
#include <linux/pci.h>

#include "pcie-rcar.h"

void rcar_pci_write_reg(struct rcar_pcie *pcie, u32 val, unsigned int reg)
{
	writel(val, pcie->base + reg);
}

u32 rcar_pci_read_reg(struct rcar_pcie *pcie, unsigned int reg)
{
	return readl(pcie->base + reg);
}

void rcar_rmw32(struct rcar_pcie *pcie, int where, u32 mask, u32 data)
{
	unsigned int shift = BITS_PER_BYTE * (where & 3);
	u32 val = rcar_pci_read_reg(pcie, where & ~3);

	val &= ~(mask << shift);
	val |= data << shift;
	rcar_pci_write_reg(pcie, val, where & ~3);
}

int rcar_pcie_wait_for_phyrdy(struct rcar_pcie *pcie)
{
	unsigned int timeout = 10;

	while (timeout--) {
		if (rcar_pci_read_reg(pcie, PCIEPHYSR) & PHYRDY)
			return 0;

		msleep(5);
	}

	return -ETIMEDOUT;
}

int rcar_pcie_wait_for_dl(struct rcar_pcie *pcie)
{
	unsigned int timeout = 10000;

	while (timeout--) {
		if ((rcar_pci_read_reg(pcie, PCIETSTR) & DATA_LINK_ACTIVE))
			return 0;

		udelay(5);
		cpu_relax();
	}

	return -ETIMEDOUT;
}

void rcar_pcie_set_outbound(struct rcar_pcie *pcie, int win,
			    struct resource_entry *window)
{
	/* Setup PCIe address space mappings for each resource */
	struct resource *res = window->res;
	resource_size_t res_start;
	resource_size_t size;
	u32 mask;

	rcar_pci_write_reg(pcie, 0x00000000, PCIEPTCTLR(win));

	/*
	 * The PAMR mask is calculated in units of 128Bytes, which
	 * keeps things pretty simple.
	 */
	size = resource_size(res);
	if (size > 128)
		mask = (roundup_pow_of_two(size) / SZ_128) - 1;
	else
		mask = 0x0;
	rcar_pci_write_reg(pcie, mask << 7, PCIEPAMR(win));

	if (res->flags & IORESOURCE_IO)
		res_start = pci_pio_to_address(res->start) - window->offset;
	else
		res_start = res->start - window->offset;

	rcar_pci_write_reg(pcie, upper_32_bits(res_start), PCIEPAUR(win));
	rcar_pci_write_reg(pcie, lower_32_bits(res_start) & ~0x7F,
			   PCIEPALR(win));

	/* First resource is for IO */
	mask = PAR_ENABLE;
	if (res->flags & IORESOURCE_IO)
		mask |= IO_SPACE;

	rcar_pci_write_reg(pcie, mask, PCIEPTCTLR(win));
}

void rcar_pcie_set_inbound(struct rcar_pcie *pcie, u64 cpu_addr,
			   u64 pci_addr, u64 flags, int idx, bool host)
{
	/*
	 * Set up 64-bit inbound regions as the range parser doesn't
	 * distinguish between 32 and 64-bit types.
	 */
	if (host)
		rcar_pci_write_reg(pcie, lower_32_bits(pci_addr),
				   PCIEPRAR(idx));
	rcar_pci_write_reg(pcie, lower_32_bits(cpu_addr), PCIELAR(idx));
	rcar_pci_write_reg(pcie, flags, PCIELAMR(idx));

	if (host)
		rcar_pci_write_reg(pcie, upper_32_bits(pci_addr),
				   PCIEPRAR(idx + 1));
	rcar_pci_write_reg(pcie, upper_32_bits(cpu_addr), PCIELAR(idx + 1));
	rcar_pci_write_reg(pcie, 0, PCIELAMR(idx + 1));
}
