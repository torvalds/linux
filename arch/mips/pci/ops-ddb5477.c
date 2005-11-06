/***********************************************************************
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/ddb5xxx/ddb5477/pci_ops.c
 *     Define the pci_ops for DB5477.
 *
 * Much of the code is derived from the original DDB5074 port by
 * Geert Uytterhoeven <geert@sonycom.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 ***********************************************************************
 */

/*
 * DDB5477 has two PCI channels, external PCI and IOPIC (internal)
 * Therefore we provide two sets of pci_ops.
 */
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/debug.h>

#include <asm/ddb5xxx/ddb5xxx.h>

/*
 * config_swap structure records what set of pdar/pmr are used
 * to access pci config space.  It also provides a place hold the
 * original values for future restoring.
 */
struct pci_config_swap {
	u32 pdar;
	u32 pmr;
	u32 config_base;
	u32 config_size;
	u32 pdar_backup;
	u32 pmr_backup;
};

/*
 * On DDB5477, we have two sets of swap registers, for ext PCI and IOPCI.
 */
struct pci_config_swap ext_pci_swap = {
	DDB_PCIW0,
	DDB_PCIINIT00,
	DDB_PCI0_CONFIG_BASE,
	DDB_PCI0_CONFIG_SIZE
};
struct pci_config_swap io_pci_swap = {
	DDB_IOPCIW0,
	DDB_PCIINIT01,
	DDB_PCI1_CONFIG_BASE,
	DDB_PCI1_CONFIG_SIZE
};


/*
 * access config space
 */
static inline u32 ddb_access_config_base(struct pci_config_swap *swap, u32 bus,	/* 0 means top level bus */
					 u32 slot_num)
{
	u32 pci_addr = 0;
	u32 pciinit_offset = 0;
	u32 virt_addr;
	u32 option;

	/* minimum pdar (window) size is 2MB */
	db_assert(swap->config_size >= (2 << 20));

	db_assert(slot_num < (1 << 5));
	db_assert(bus < (1 << 8));

	/* backup registers */
	swap->pdar_backup = ddb_in32(swap->pdar);
	swap->pmr_backup = ddb_in32(swap->pmr);

	/* set the pdar (pci window) register */
	ddb_set_pdar(swap->pdar, swap->config_base, swap->config_size, 32,	/* 32 bit wide */
		     0,		/* not on local memory bus */
		     0);	/* not visible from PCI bus (N/A) */

	/*
	 * calcuate the absolute pci config addr;
	 * according to the spec, we start scanning from adr:11 (0x800)
	 */
	if (bus == 0) {
		/* type 0 config */
		pci_addr = 0x800 << slot_num;
	} else {
		/* type 1 config */
		pci_addr = (bus << 16) | (slot_num << 11);
	}

	/*
	 * if pci_addr is less than pci config window size,  we set
	 * pciinit_offset to 0 and adjust the virt_address.
	 * Otherwise we will try to adjust pciinit_offset.
	 */
	if (pci_addr < swap->config_size) {
		virt_addr = KSEG1ADDR(swap->config_base + pci_addr);
		pciinit_offset = 0;
	} else {
		db_assert((pci_addr & (swap->config_size - 1)) == 0);
		virt_addr = KSEG1ADDR(swap->config_base);
		pciinit_offset = pci_addr;
	}

	/* set the pmr register */
	option = DDB_PCI_ACCESS_32;
	if (bus != 0)
		option |= DDB_PCI_CFGTYPE1;
	ddb_set_pmr(swap->pmr, DDB_PCICMD_CFG, pciinit_offset, option);

	return virt_addr;
}

static inline void ddb_close_config_base(struct pci_config_swap *swap)
{
	ddb_out32(swap->pdar, swap->pdar_backup);
	ddb_out32(swap->pmr, swap->pmr_backup);
}

static int read_config_dword(struct pci_config_swap *swap,
			     struct pci_bus *bus, u32 devfn, u32 where,
			     u32 * val)
{
	u32 bus_num, slot_num, func_num;
	u32 base;

	db_assert((where & 3) == 0);
	db_assert(where < (1 << 8));

	/* check if the bus is top-level */
	if (bus->parent != NULL) {
		bus_num = bus->number;
		db_assert(bus_num != 0);
	} else {
		bus_num = 0;
	}

	slot_num = PCI_SLOT(devfn);
	func_num = PCI_FUNC(devfn);
	base = ddb_access_config_base(swap, bus_num, slot_num);
	*val = *(volatile u32 *) (base + (func_num << 8) + where);
	ddb_close_config_base(swap);
	return PCIBIOS_SUCCESSFUL;
}

static int read_config_word(struct pci_config_swap *swap,
			    struct pci_bus *bus, u32 devfn, u32 where,
			    u16 * val)
{
	int status;
	u32 result;

	db_assert((where & 1) == 0);

	status = read_config_dword(swap, bus, devfn, where & ~3, &result);
	if (where & 2)
		result >>= 16;
	*val = result & 0xffff;
	return status;
}

static int read_config_byte(struct pci_config_swap *swap,
			    struct pci_bus *bus, u32 devfn, u32 where,
			    u8 * val)
{
	int status;
	u32 result;

	status = read_config_dword(swap, bus, devfn, where & ~3, &result);
	if (where & 1)
		result >>= 8;
	if (where & 2)
		result >>= 16;
	*val = result & 0xff;

	return status;
}

static int write_config_dword(struct pci_config_swap *swap,
			      struct pci_bus *bus, u32 devfn, u32 where,
			      u32 val)
{
	u32 bus_num, slot_num, func_num;
	u32 base;

	db_assert((where & 3) == 0);
	db_assert(where < (1 << 8));

	/* check if the bus is top-level */
	if (bus->parent != NULL) {
		bus_num = bus->number;
		db_assert(bus_num != 0);
	} else {
		bus_num = 0;
	}

	slot_num = PCI_SLOT(devfn);
	func_num = PCI_FUNC(devfn);
	base = ddb_access_config_base(swap, bus_num, slot_num);
	*(volatile u32 *) (base + (func_num << 8) + where) = val;
	ddb_close_config_base(swap);
	return PCIBIOS_SUCCESSFUL;
}

static int write_config_word(struct pci_config_swap *swap,
			     struct pci_bus *bus, u32 devfn, u32 where, u16 val)
{
	int status, shift = 0;
	u32 result;

	db_assert((where & 1) == 0);

	status = read_config_dword(swap, bus, devfn, where & ~3, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;

	if (where & 2)
		shift += 16;
	result &= ~(0xffff << shift);
	result |= val << shift;
	return write_config_dword(swap, bus, devfn, where & ~3, result);
}

static int write_config_byte(struct pci_config_swap *swap,
			     struct pci_bus *bus, u32 devfn, u32 where, u8 val)
{
	int status, shift = 0;
	u32 result;

	status = read_config_dword(swap, bus, devfn, where & ~3, &result);
	if (status != PCIBIOS_SUCCESSFUL)
		return status;

	if (where & 2)
		shift += 16;
	if (where & 1)
		shift += 8;
	result &= ~(0xff << shift);
	result |= val << shift;
	return write_config_dword(swap, bus, devfn, where & ~3, result);
}

#define        MAKE_PCI_OPS(prefix, rw, pciswap, star) \
static int prefix##_##rw##_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 star val) \
{ \
	if (size == 1) \
     		return rw##_config_byte(pciswap, bus, devfn, where, (u8 star)val); \
	else if (size == 2) \
     		return rw##_config_word(pciswap, bus, devfn, where, (u16 star)val); \
	/* Size must be 4 */ \
     	return rw##_config_dword(pciswap, bus, devfn, where, val); \
}

MAKE_PCI_OPS(extpci, read, &ext_pci_swap, *)
MAKE_PCI_OPS(extpci, write, &ext_pci_swap,)

MAKE_PCI_OPS(iopci, read, &io_pci_swap, *)
MAKE_PCI_OPS(iopci, write, &io_pci_swap,)

struct pci_ops ddb5477_ext_pci_ops = {
	.read = extpci_read_config,
	.write = extpci_write_config
};


struct pci_ops ddb5477_io_pci_ops = {
	.read = iopci_read_config,
	.write = iopci_write_config
};
