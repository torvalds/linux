// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015, 2016 Cavium, Inc.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/of_pci.h>
#include <linux/of.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

#if defined(CONFIG_PCI_HOST_THUNDER_ECAM) || (defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS))

static void set_val(u32 v, int where, int size, u32 *val)
{
	int shift = (where & 3) * 8;

	pr_debug("set_val %04x: %08x\n", (unsigned int)(where & ~3), v);
	v >>= shift;
	if (size == 1)
		v &= 0xff;
	else if (size == 2)
		v &= 0xffff;
	*val = v;
}

static int handle_ea_bar(u32 e0, int bar, struct pci_bus *bus,
			 unsigned int devfn, int where, int size, u32 *val)
{
	void __iomem *addr;
	u32 v;

	/* Entries are 16-byte aligned; bits[2,3] select word in entry */
	int where_a = where & 0xc;

	if (where_a == 0) {
		set_val(e0, where, size, val);
		return PCIBIOS_SUCCESSFUL;
	}
	if (where_a == 0x4) {
		addr = bus->ops->map_bus(bus, devfn, bar); /* BAR 0 */
		if (!addr)
			return PCIBIOS_DEVICE_NOT_FOUND;

		v = readl(addr);
		v &= ~0xf;
		v |= 2; /* EA entry-1. Base-L */
		set_val(v, where, size, val);
		return PCIBIOS_SUCCESSFUL;
	}
	if (where_a == 0x8) {
		u32 barl_orig;
		u32 barl_rb;

		addr = bus->ops->map_bus(bus, devfn, bar); /* BAR 0 */
		if (!addr)
			return PCIBIOS_DEVICE_NOT_FOUND;

		barl_orig = readl(addr + 0);
		writel(0xffffffff, addr + 0);
		barl_rb = readl(addr + 0);
		writel(barl_orig, addr + 0);
		/* zeros in unsettable bits */
		v = ~barl_rb & ~3;
		v |= 0xc; /* EA entry-2. Offset-L */
		set_val(v, where, size, val);
		return PCIBIOS_SUCCESSFUL;
	}
	if (where_a == 0xc) {
		addr = bus->ops->map_bus(bus, devfn, bar + 4); /* BAR 1 */
		if (!addr)
			return PCIBIOS_DEVICE_NOT_FOUND;

		v = readl(addr); /* EA entry-3. Base-H */
		set_val(v, where, size, val);
		return PCIBIOS_SUCCESSFUL;
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int thunder_ecam_p2_config_read(struct pci_bus *bus, unsigned int devfn,
				       int where, int size, u32 *val)
{
	struct pci_config_window *cfg = bus->sysdata;
	int where_a = where & ~3;
	void __iomem *addr;
	u32 node_bits;
	u32 v;

	/* EA Base[63:32] may be missing some bits ... */
	switch (where_a) {
	case 0xa8:
	case 0xbc:
	case 0xd0:
	case 0xe4:
		break;
	default:
		return pci_generic_config_read(bus, devfn, where, size, val);
	}

	addr = bus->ops->map_bus(bus, devfn, where_a);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	v = readl(addr);

	/*
	 * Bit 44 of the 64-bit Base must match the same bit in
	 * the config space access window.  Since we are working with
	 * the high-order 32 bits, shift everything down by 32 bits.
	 */
	node_bits = upper_32_bits(cfg->res.start) & (1 << 12);

	v |= node_bits;
	set_val(v, where, size, val);

	return PCIBIOS_SUCCESSFUL;
}

static int thunder_ecam_config_read(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 *val)
{
	u32 v;
	u32 vendor_device;
	u32 class_rev;
	void __iomem *addr;
	int cfg_type;
	int where_a = where & ~3;

	addr = bus->ops->map_bus(bus, devfn, 0xc);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	v = readl(addr);

	/* Check for non type-00 header */
	cfg_type = (v >> 16) & 0x7f;

	addr = bus->ops->map_bus(bus, devfn, 8);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	class_rev = readl(addr);
	if (class_rev == 0xffffffff)
		goto no_emulation;

	if ((class_rev & 0xff) >= 8) {
		/* Pass-2 handling */
		if (cfg_type)
			goto no_emulation;
		return thunder_ecam_p2_config_read(bus, devfn, where,
						   size, val);
	}

	/*
	 * All BARs have fixed addresses specified by the EA
	 * capability; they must return zero on read.
	 */
	if (cfg_type == 0 &&
	    ((where >= 0x10 && where < 0x2c) ||
	     (where >= 0x1a4 && where < 0x1bc))) {
		/* BAR or SR-IOV BAR */
		*val = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	addr = bus->ops->map_bus(bus, devfn, 0);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	vendor_device = readl(addr);
	if (vendor_device == 0xffffffff)
		goto no_emulation;

	pr_debug("%04x:%04x - Fix pass#: %08x, where: %03x, devfn: %03x\n",
		 vendor_device & 0xffff, vendor_device >> 16, class_rev,
		 (unsigned int)where, devfn);

	/* Check for non type-00 header */
	if (cfg_type == 0) {
		bool has_msix;
		bool is_nic = (vendor_device == 0xa01e177d);
		bool is_tns = (vendor_device == 0xa01f177d);

		addr = bus->ops->map_bus(bus, devfn, 0x70);
		if (!addr)
			return PCIBIOS_DEVICE_NOT_FOUND;

		/* E_CAP */
		v = readl(addr);
		has_msix = (v & 0xff00) != 0;

		if (!has_msix && where_a == 0x70) {
			v |= 0xbc00; /* next capability is EA at 0xbc */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xb0) {
			addr = bus->ops->map_bus(bus, devfn, where_a);
			if (!addr)
				return PCIBIOS_DEVICE_NOT_FOUND;

			v = readl(addr);
			if (v & 0xff00)
				pr_err("Bad MSIX cap header: %08x\n", v);
			v |= 0xbc00; /* next capability is EA at 0xbc */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xbc) {
			if (is_nic)
				v = 0x40014; /* EA last in chain, 4 entries */
			else if (is_tns)
				v = 0x30014; /* EA last in chain, 3 entries */
			else if (has_msix)
				v = 0x20014; /* EA last in chain, 2 entries */
			else
				v = 0x10014; /* EA last in chain, 1 entry */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a >= 0xc0 && where_a < 0xd0)
			/* EA entry-0. PP=0, BAR0 Size:3 */
			return handle_ea_bar(0x80ff0003,
					     0x10, bus, devfn, where,
					     size, val);
		if (where_a >= 0xd0 && where_a < 0xe0 && has_msix)
			 /* EA entry-1. PP=0, BAR4 Size:3 */
			return handle_ea_bar(0x80ff0043,
					     0x20, bus, devfn, where,
					     size, val);
		if (where_a >= 0xe0 && where_a < 0xf0 && is_tns)
			/* EA entry-2. PP=0, BAR2, Size:3 */
			return handle_ea_bar(0x80ff0023,
					     0x18, bus, devfn, where,
					     size, val);
		if (where_a >= 0xe0 && where_a < 0xf0 && is_nic)
			/* EA entry-2. PP=4, VF_BAR0 (9), Size:3 */
			return handle_ea_bar(0x80ff0493,
					     0x1a4, bus, devfn, where,
					     size, val);
		if (where_a >= 0xf0 && where_a < 0x100 && is_nic)
			/* EA entry-3. PP=4, VF_BAR4 (d), Size:3 */
			return handle_ea_bar(0x80ff04d3,
					     0x1b4, bus, devfn, where,
					     size, val);
	} else if (cfg_type == 1) {
		bool is_rsl_bridge = devfn == 0x08;
		bool is_rad_bridge = devfn == 0xa0;
		bool is_zip_bridge = devfn == 0xa8;
		bool is_dfa_bridge = devfn == 0xb0;
		bool is_nic_bridge = devfn == 0x10;

		if (where_a == 0x70) {
			addr = bus->ops->map_bus(bus, devfn, where_a);
			if (!addr)
				return PCIBIOS_DEVICE_NOT_FOUND;

			v = readl(addr);
			if (v & 0xff00)
				pr_err("Bad PCIe cap header: %08x\n", v);
			v |= 0xbc00; /* next capability is EA at 0xbc */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xbc) {
			if (is_nic_bridge)
				v = 0x10014; /* EA last in chain, 1 entry */
			else
				v = 0x00014; /* EA last in chain, no entries */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xc0) {
			if (is_rsl_bridge || is_nic_bridge)
				v = 0x0101; /* subordinate:secondary = 1:1 */
			else if (is_rad_bridge)
				v = 0x0202; /* subordinate:secondary = 2:2 */
			else if (is_zip_bridge)
				v = 0x0303; /* subordinate:secondary = 3:3 */
			else if (is_dfa_bridge)
				v = 0x0404; /* subordinate:secondary = 4:4 */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xc4 && is_nic_bridge) {
			/* Enabled, not-Write, SP=ff, PP=05, BEI=6, ES=4 */
			v = 0x80ff0564;
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xc8 && is_nic_bridge) {
			v = 0x00000002; /* Base-L 64-bit */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xcc && is_nic_bridge) {
			v = 0xfffffffe; /* MaxOffset-L 64-bit */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xd0 && is_nic_bridge) {
			v = 0x00008430; /* NIC Base-H */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
		if (where_a == 0xd4 && is_nic_bridge) {
			v = 0x0000000f; /* MaxOffset-H */
			set_val(v, where, size, val);
			return PCIBIOS_SUCCESSFUL;
		}
	}
no_emulation:
	return pci_generic_config_read(bus, devfn, where, size, val);
}

static int thunder_ecam_config_write(struct pci_bus *bus, unsigned int devfn,
				     int where, int size, u32 val)
{
	/*
	 * All BARs have fixed addresses; ignore BAR writes so they
	 * don't get corrupted.
	 */
	if ((where >= 0x10 && where < 0x2c) ||
	    (where >= 0x1a4 && where < 0x1bc))
		/* BAR or SR-IOV BAR */
		return PCIBIOS_SUCCESSFUL;

	return pci_generic_config_write(bus, devfn, where, size, val);
}

const struct pci_ecam_ops pci_thunder_ecam_ops = {
	.pci_ops	= {
		.map_bus        = pci_ecam_map_bus,
		.read           = thunder_ecam_config_read,
		.write          = thunder_ecam_config_write,
	}
};

#ifdef CONFIG_PCI_HOST_THUNDER_ECAM

static const struct of_device_id thunder_ecam_of_match[] = {
	{
		.compatible = "cavium,pci-host-thunder-ecam",
		.data = &pci_thunder_ecam_ops,
	},
	{ },
};

static struct platform_driver thunder_ecam_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = thunder_ecam_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = pci_host_common_probe,
};
builtin_platform_driver(thunder_ecam_driver);

#endif
#endif
