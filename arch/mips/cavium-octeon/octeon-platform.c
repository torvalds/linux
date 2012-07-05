/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2011 Cavium Networks
 * Copyright (C) 2008 Wind River Systems
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-rnm-defs.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>

static struct octeon_cf_data octeon_cf_data;

static int __init octeon_cf_device_init(void)
{
	union cvmx_mio_boot_reg_cfgx mio_boot_reg_cfg;
	unsigned long base_ptr, region_base, region_size;
	struct platform_device *pd;
	struct resource cf_resources[3];
	unsigned int num_resources;
	int i;
	int ret = 0;

	/* Setup octeon-cf platform device if present. */
	base_ptr = 0;
	if (octeon_bootinfo->major_version == 1
		&& octeon_bootinfo->minor_version >= 1) {
		if (octeon_bootinfo->compact_flash_common_base_addr)
			base_ptr =
				octeon_bootinfo->compact_flash_common_base_addr;
	} else {
		base_ptr = 0x1d000800;
	}

	if (!base_ptr)
		return ret;

	/* Find CS0 region. */
	for (i = 0; i < 8; i++) {
		mio_boot_reg_cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(i));
		region_base = mio_boot_reg_cfg.s.base << 16;
		region_size = (mio_boot_reg_cfg.s.size + 1) << 16;
		if (mio_boot_reg_cfg.s.en && base_ptr >= region_base
		    && base_ptr < region_base + region_size)
			break;
	}
	if (i >= 7) {
		/* i and i + 1 are CS0 and CS1, both must be less than 8. */
		goto out;
	}
	octeon_cf_data.base_region = i;
	octeon_cf_data.is16bit = mio_boot_reg_cfg.s.width;
	octeon_cf_data.base_region_bias = base_ptr - region_base;
	memset(cf_resources, 0, sizeof(cf_resources));
	num_resources = 0;
	cf_resources[num_resources].flags	= IORESOURCE_MEM;
	cf_resources[num_resources].start	= region_base;
	cf_resources[num_resources].end	= region_base + region_size - 1;
	num_resources++;


	if (!(base_ptr & 0xfffful)) {
		/*
		 * Boot loader signals availability of DMA (true_ide
		 * mode) by setting low order bits of base_ptr to
		 * zero.
		 */

		/* Assume that CS1 immediately follows. */
		mio_boot_reg_cfg.u64 =
			cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(i + 1));
		region_base = mio_boot_reg_cfg.s.base << 16;
		region_size = (mio_boot_reg_cfg.s.size + 1) << 16;
		if (!mio_boot_reg_cfg.s.en)
			goto out;

		cf_resources[num_resources].flags	= IORESOURCE_MEM;
		cf_resources[num_resources].start	= region_base;
		cf_resources[num_resources].end	= region_base + region_size - 1;
		num_resources++;

		octeon_cf_data.dma_engine = 0;
		cf_resources[num_resources].flags	= IORESOURCE_IRQ;
		cf_resources[num_resources].start	= OCTEON_IRQ_BOOTDMA;
		cf_resources[num_resources].end	= OCTEON_IRQ_BOOTDMA;
		num_resources++;
	} else {
		octeon_cf_data.dma_engine = -1;
	}

	pd = platform_device_alloc("pata_octeon_cf", -1);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}
	pd->dev.platform_data = &octeon_cf_data;

	ret = platform_device_add_resources(pd, cf_resources, num_resources);
	if (ret)
		goto fail;

	ret = platform_device_add(pd);
	if (ret)
		goto fail;

	return ret;
fail:
	platform_device_put(pd);
out:
	return ret;
}
device_initcall(octeon_cf_device_init);

/* Octeon Random Number Generator.  */
static int __init octeon_rng_device_init(void)
{
	struct platform_device *pd;
	int ret = 0;

	struct resource rng_resources[] = {
		{
			.flags	= IORESOURCE_MEM,
			.start	= XKPHYS_TO_PHYS(CVMX_RNM_CTL_STATUS),
			.end	= XKPHYS_TO_PHYS(CVMX_RNM_CTL_STATUS) + 0xf
		}, {
			.flags	= IORESOURCE_MEM,
			.start	= cvmx_build_io_address(8, 0),
			.end	= cvmx_build_io_address(8, 0) + 0x7
		}
	};

	pd = platform_device_alloc("octeon_rng", -1);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = platform_device_add_resources(pd, rng_resources,
					    ARRAY_SIZE(rng_resources));
	if (ret)
		goto fail;

	ret = platform_device_add(pd);
	if (ret)
		goto fail;

	return ret;
fail:
	platform_device_put(pd);

out:
	return ret;
}
device_initcall(octeon_rng_device_init);

/* Octeon SMI/MDIO interface.  */
static int __init octeon_mdiobus_device_init(void)
{
	struct platform_device *pd;
	int ret = 0;

	if (octeon_is_simulation())
		return 0; /* No mdio in the simulator. */

	/* The bus number is the platform_device id.  */
	pd = platform_device_alloc("mdio-octeon", 0);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = platform_device_add(pd);
	if (ret)
		goto fail;

	return ret;
fail:
	platform_device_put(pd);

out:
	return ret;

}
device_initcall(octeon_mdiobus_device_init);

/* Octeon mgmt port Ethernet interface.  */
static int __init octeon_mgmt_device_init(void)
{
	struct platform_device *pd;
	int ret = 0;
	int port, num_ports;

	struct resource mgmt_port_resource = {
		.flags	= IORESOURCE_IRQ,
		.start	= -1,
		.end	= -1
	};

	if (!OCTEON_IS_MODEL(OCTEON_CN56XX) && !OCTEON_IS_MODEL(OCTEON_CN52XX))
		return 0;

	if (OCTEON_IS_MODEL(OCTEON_CN56XX))
		num_ports = 1;
	else
		num_ports = 2;

	for (port = 0; port < num_ports; port++) {
		pd = platform_device_alloc("octeon_mgmt", port);
		if (!pd) {
			ret = -ENOMEM;
			goto out;
		}
		/* No DMA restrictions */
		pd->dev.coherent_dma_mask = DMA_BIT_MASK(64);
		pd->dev.dma_mask = &pd->dev.coherent_dma_mask;

		switch (port) {
		case 0:
			mgmt_port_resource.start = OCTEON_IRQ_MII0;
			break;
		case 1:
			mgmt_port_resource.start = OCTEON_IRQ_MII1;
			break;
		default:
			BUG();
		}
		mgmt_port_resource.end = mgmt_port_resource.start;

		ret = platform_device_add_resources(pd, &mgmt_port_resource, 1);

		if (ret)
			goto fail;

		ret = platform_device_add(pd);
		if (ret)
			goto fail;
	}
	return ret;
fail:
	platform_device_put(pd);

out:
	return ret;

}
device_initcall(octeon_mgmt_device_init);

#ifdef CONFIG_USB

static int __init octeon_ehci_device_init(void)
{
	struct platform_device *pd;
	int ret = 0;

	struct resource usb_resources[] = {
		{
			.flags	= IORESOURCE_MEM,
		}, {
			.flags	= IORESOURCE_IRQ,
		}
	};

	/* Only Octeon2 has ehci/ohci */
	if (!OCTEON_IS_MODEL(OCTEON_CN63XX))
		return 0;

	if (octeon_is_simulation() || usb_disabled())
		return 0; /* No USB in the simulator. */

	pd = platform_device_alloc("octeon-ehci", 0);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	usb_resources[0].start = 0x00016F0000000000ULL;
	usb_resources[0].end = usb_resources[0].start + 0x100;

	usb_resources[1].start = OCTEON_IRQ_USB0;
	usb_resources[1].end = OCTEON_IRQ_USB0;

	ret = platform_device_add_resources(pd, usb_resources,
					    ARRAY_SIZE(usb_resources));
	if (ret)
		goto fail;

	ret = platform_device_add(pd);
	if (ret)
		goto fail;

	return ret;
fail:
	platform_device_put(pd);
out:
	return ret;
}
device_initcall(octeon_ehci_device_init);

static int __init octeon_ohci_device_init(void)
{
	struct platform_device *pd;
	int ret = 0;

	struct resource usb_resources[] = {
		{
			.flags	= IORESOURCE_MEM,
		}, {
			.flags	= IORESOURCE_IRQ,
		}
	};

	/* Only Octeon2 has ehci/ohci */
	if (!OCTEON_IS_MODEL(OCTEON_CN63XX))
		return 0;

	if (octeon_is_simulation() || usb_disabled())
		return 0; /* No USB in the simulator. */

	pd = platform_device_alloc("octeon-ohci", 0);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	usb_resources[0].start = 0x00016F0000000400ULL;
	usb_resources[0].end = usb_resources[0].start + 0x100;

	usb_resources[1].start = OCTEON_IRQ_USB0;
	usb_resources[1].end = OCTEON_IRQ_USB0;

	ret = platform_device_add_resources(pd, usb_resources,
					    ARRAY_SIZE(usb_resources));
	if (ret)
		goto fail;

	ret = platform_device_add(pd);
	if (ret)
		goto fail;

	return ret;
fail:
	platform_device_put(pd);
out:
	return ret;
}
device_initcall(octeon_ohci_device_init);

#endif /* CONFIG_USB */

static struct of_device_id __initdata octeon_ids[] = {
	{ .compatible = "simple-bus", },
	{ .compatible = "cavium,octeon-6335-uctl", },
	{ .compatible = "cavium,octeon-3860-bootbus", },
	{ .compatible = "cavium,mdio-mux", },
	{ .compatible = "gpio-leds", },
	{},
};

static bool __init octeon_has_88e1145(void)
{
	return !OCTEON_IS_MODEL(OCTEON_CN52XX) &&
	       !OCTEON_IS_MODEL(OCTEON_CN6XXX) &&
	       !OCTEON_IS_MODEL(OCTEON_CN56XX);
}

static void __init octeon_fdt_set_phy(int eth, int phy_addr)
{
	const __be32 *phy_handle;
	const __be32 *alt_phy_handle;
	const __be32 *reg;
	u32 phandle;
	int phy;
	int alt_phy;
	const char *p;
	int current_len;
	char new_name[20];

	phy_handle = fdt_getprop(initial_boot_params, eth, "phy-handle", NULL);
	if (!phy_handle)
		return;

	phandle = be32_to_cpup(phy_handle);
	phy = fdt_node_offset_by_phandle(initial_boot_params, phandle);

	alt_phy_handle = fdt_getprop(initial_boot_params, eth, "cavium,alt-phy-handle", NULL);
	if (alt_phy_handle) {
		u32 alt_phandle = be32_to_cpup(alt_phy_handle);
		alt_phy = fdt_node_offset_by_phandle(initial_boot_params, alt_phandle);
	} else {
		alt_phy = -1;
	}

	if (phy_addr < 0 || phy < 0) {
		/* Delete the PHY things */
		fdt_nop_property(initial_boot_params, eth, "phy-handle");
		/* This one may fail */
		fdt_nop_property(initial_boot_params, eth, "cavium,alt-phy-handle");
		if (phy >= 0)
			fdt_nop_node(initial_boot_params, phy);
		if (alt_phy >= 0)
			fdt_nop_node(initial_boot_params, alt_phy);
		return;
	}

	if (phy_addr >= 256 && alt_phy > 0) {
		const struct fdt_property *phy_prop;
		struct fdt_property *alt_prop;
		u32 phy_handle_name;

		/* Use the alt phy node instead.*/
		phy_prop = fdt_get_property(initial_boot_params, eth, "phy-handle", NULL);
		phy_handle_name = phy_prop->nameoff;
		fdt_nop_node(initial_boot_params, phy);
		fdt_nop_property(initial_boot_params, eth, "phy-handle");
		alt_prop = fdt_get_property_w(initial_boot_params, eth, "cavium,alt-phy-handle", NULL);
		alt_prop->nameoff = phy_handle_name;
		phy = alt_phy;
	}

	phy_addr &= 0xff;

	if (octeon_has_88e1145()) {
		fdt_nop_property(initial_boot_params, phy, "marvell,reg-init");
		memset(new_name, 0, sizeof(new_name));
		strcpy(new_name, "marvell,88e1145");
		p = fdt_getprop(initial_boot_params, phy, "compatible",
				&current_len);
		if (p && current_len >= strlen(new_name))
			fdt_setprop_inplace(initial_boot_params, phy,
					"compatible", new_name, current_len);
	}

	reg = fdt_getprop(initial_boot_params, phy, "reg", NULL);
	if (phy_addr == be32_to_cpup(reg))
		return;

	fdt_setprop_inplace_cell(initial_boot_params, phy, "reg", phy_addr);

	snprintf(new_name, sizeof(new_name), "ethernet-phy@%x", phy_addr);

	p = fdt_get_name(initial_boot_params, phy, &current_len);
	if (p && current_len == strlen(new_name))
		fdt_set_name(initial_boot_params, phy, new_name);
	else
		pr_err("Error: could not rename ethernet phy: <%s>", p);
}

static void __init octeon_fdt_set_mac_addr(int n, u64 *pmac)
{
	u8 new_mac[6];
	u64 mac = *pmac;
	int r;

	new_mac[0] = (mac >> 40) & 0xff;
	new_mac[1] = (mac >> 32) & 0xff;
	new_mac[2] = (mac >> 24) & 0xff;
	new_mac[3] = (mac >> 16) & 0xff;
	new_mac[4] = (mac >> 8) & 0xff;
	new_mac[5] = mac & 0xff;

	r = fdt_setprop_inplace(initial_boot_params, n, "local-mac-address",
				new_mac, sizeof(new_mac));

	if (r) {
		pr_err("Setting \"local-mac-address\" failed %d", r);
		return;
	}
	*pmac = mac + 1;
}

static void __init octeon_fdt_rm_ethernet(int node)
{
	const __be32 *phy_handle;

	phy_handle = fdt_getprop(initial_boot_params, node, "phy-handle", NULL);
	if (phy_handle) {
		u32 ph = be32_to_cpup(phy_handle);
		int p = fdt_node_offset_by_phandle(initial_boot_params, ph);
		if (p >= 0)
			fdt_nop_node(initial_boot_params, p);
	}
	fdt_nop_node(initial_boot_params, node);
}

static void __init octeon_fdt_pip_port(int iface, int i, int p, int max, u64 *pmac)
{
	char name_buffer[20];
	int eth;
	int phy_addr;
	int ipd_port;

	snprintf(name_buffer, sizeof(name_buffer), "ethernet@%x", p);
	eth = fdt_subnode_offset(initial_boot_params, iface, name_buffer);
	if (eth < 0)
		return;
	if (p > max) {
		pr_debug("Deleting port %x:%x\n", i, p);
		octeon_fdt_rm_ethernet(eth);
		return;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		ipd_port = (0x100 * i) + (0x10 * p) + 0x800;
	else
		ipd_port = 16 * i + p;

	phy_addr = cvmx_helper_board_get_mii_address(ipd_port);
	octeon_fdt_set_phy(eth, phy_addr);
	octeon_fdt_set_mac_addr(eth, pmac);
}

static void __init octeon_fdt_pip_iface(int pip, int idx, u64 *pmac)
{
	char name_buffer[20];
	int iface;
	int p;
	int count;

	count = cvmx_helper_interface_enumerate(idx);

	snprintf(name_buffer, sizeof(name_buffer), "interface@%d", idx);
	iface = fdt_subnode_offset(initial_boot_params, pip, name_buffer);
	if (iface < 0)
		return;

	for (p = 0; p < 16; p++)
		octeon_fdt_pip_port(iface, idx, p, count - 1, pmac);
}

int __init octeon_prune_device_tree(void)
{
	int i, max_port, uart_mask;
	const char *pip_path;
	const char *alias_prop;
	char name_buffer[20];
	int aliases;
	u64 mac_addr_base;

	if (fdt_check_header(initial_boot_params))
		panic("Corrupt Device Tree.");

	aliases = fdt_path_offset(initial_boot_params, "/aliases");
	if (aliases < 0) {
		pr_err("Error: No /aliases node in device tree.");
		return -EINVAL;
	}


	mac_addr_base =
		((octeon_bootinfo->mac_addr_base[0] & 0xffull)) << 40 |
		((octeon_bootinfo->mac_addr_base[1] & 0xffull)) << 32 |
		((octeon_bootinfo->mac_addr_base[2] & 0xffull)) << 24 |
		((octeon_bootinfo->mac_addr_base[3] & 0xffull)) << 16 |
		((octeon_bootinfo->mac_addr_base[4] & 0xffull)) << 8 |
		(octeon_bootinfo->mac_addr_base[5] & 0xffull);

	if (OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX))
		max_port = 2;
	else if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN68XX))
		max_port = 1;
	else
		max_port = 0;

	if (octeon_bootinfo->board_type == CVMX_BOARD_TYPE_NIC10E)
		max_port = 0;

	for (i = 0; i < 2; i++) {
		int mgmt;
		snprintf(name_buffer, sizeof(name_buffer),
			 "mix%d", i);
		alias_prop = fdt_getprop(initial_boot_params, aliases,
					name_buffer, NULL);
		if (alias_prop) {
			mgmt = fdt_path_offset(initial_boot_params, alias_prop);
			if (mgmt < 0)
				continue;
			if (i >= max_port) {
				pr_debug("Deleting mix%d\n", i);
				octeon_fdt_rm_ethernet(mgmt);
				fdt_nop_property(initial_boot_params, aliases,
						 name_buffer);
			} else {
				int phy_addr = cvmx_helper_board_get_mii_address(CVMX_HELPER_BOARD_MGMT_IPD_PORT + i);
				octeon_fdt_set_phy(mgmt, phy_addr);
				octeon_fdt_set_mac_addr(mgmt, &mac_addr_base);
			}
		}
	}

	pip_path = fdt_getprop(initial_boot_params, aliases, "pip", NULL);
	if (pip_path) {
		int pip = fdt_path_offset(initial_boot_params, pip_path);
		if (pip  >= 0)
			for (i = 0; i <= 4; i++)
				octeon_fdt_pip_iface(pip, i, &mac_addr_base);
	}

	/* I2C */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX) ||
	    OCTEON_IS_MODEL(OCTEON_CN63XX) ||
	    OCTEON_IS_MODEL(OCTEON_CN68XX) ||
	    OCTEON_IS_MODEL(OCTEON_CN56XX))
		max_port = 2;
	else
		max_port = 1;

	for (i = 0; i < 2; i++) {
		int i2c;
		snprintf(name_buffer, sizeof(name_buffer),
			 "twsi%d", i);
		alias_prop = fdt_getprop(initial_boot_params, aliases,
					name_buffer, NULL);

		if (alias_prop) {
			i2c = fdt_path_offset(initial_boot_params, alias_prop);
			if (i2c < 0)
				continue;
			if (i >= max_port) {
				pr_debug("Deleting twsi%d\n", i);
				fdt_nop_node(initial_boot_params, i2c);
				fdt_nop_property(initial_boot_params, aliases,
						 name_buffer);
			}
		}
	}

	/* SMI/MDIO */
	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		max_port = 4;
	else if (OCTEON_IS_MODEL(OCTEON_CN52XX) ||
		 OCTEON_IS_MODEL(OCTEON_CN63XX) ||
		 OCTEON_IS_MODEL(OCTEON_CN56XX))
		max_port = 2;
	else
		max_port = 1;

	for (i = 0; i < 2; i++) {
		int i2c;
		snprintf(name_buffer, sizeof(name_buffer),
			 "smi%d", i);
		alias_prop = fdt_getprop(initial_boot_params, aliases,
					name_buffer, NULL);

		if (alias_prop) {
			i2c = fdt_path_offset(initial_boot_params, alias_prop);
			if (i2c < 0)
				continue;
			if (i >= max_port) {
				pr_debug("Deleting smi%d\n", i);
				fdt_nop_node(initial_boot_params, i2c);
				fdt_nop_property(initial_boot_params, aliases,
						 name_buffer);
			}
		}
	}

	/* Serial */
	uart_mask = 3;

	/* Right now CN52XX is the only chip with a third uart */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX))
		uart_mask |= 4; /* uart2 */

	for (i = 0; i < 3; i++) {
		int uart;
		snprintf(name_buffer, sizeof(name_buffer),
			 "uart%d", i);
		alias_prop = fdt_getprop(initial_boot_params, aliases,
					name_buffer, NULL);

		if (alias_prop) {
			uart = fdt_path_offset(initial_boot_params, alias_prop);
			if (uart_mask & (1 << i))
				continue;
			pr_debug("Deleting uart%d\n", i);
			fdt_nop_node(initial_boot_params, uart);
			fdt_nop_property(initial_boot_params, aliases,
					 name_buffer);
		}
	}

	/* Compact Flash */
	alias_prop = fdt_getprop(initial_boot_params, aliases,
				 "cf0", NULL);
	if (alias_prop) {
		union cvmx_mio_boot_reg_cfgx mio_boot_reg_cfg;
		unsigned long base_ptr, region_base, region_size;
		unsigned long region1_base = 0;
		unsigned long region1_size = 0;
		int cs, bootbus;
		bool is_16bit = false;
		bool is_true_ide = false;
		__be32 new_reg[6];
		__be32 *ranges;
		int len;

		int cf = fdt_path_offset(initial_boot_params, alias_prop);
		base_ptr = 0;
		if (octeon_bootinfo->major_version == 1
			&& octeon_bootinfo->minor_version >= 1) {
			if (octeon_bootinfo->compact_flash_common_base_addr)
				base_ptr = octeon_bootinfo->compact_flash_common_base_addr;
		} else {
			base_ptr = 0x1d000800;
		}

		if (!base_ptr)
			goto no_cf;

		/* Find CS0 region. */
		for (cs = 0; cs < 8; cs++) {
			mio_boot_reg_cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(cs));
			region_base = mio_boot_reg_cfg.s.base << 16;
			region_size = (mio_boot_reg_cfg.s.size + 1) << 16;
			if (mio_boot_reg_cfg.s.en && base_ptr >= region_base
				&& base_ptr < region_base + region_size) {
				is_16bit = mio_boot_reg_cfg.s.width;
				break;
			}
		}
		if (cs >= 7) {
			/* cs and cs + 1 are CS0 and CS1, both must be less than 8. */
			goto no_cf;
		}

		if (!(base_ptr & 0xfffful)) {
			/*
			 * Boot loader signals availability of DMA (true_ide
			 * mode) by setting low order bits of base_ptr to
			 * zero.
			 */

			/* Asume that CS1 immediately follows. */
			mio_boot_reg_cfg.u64 =
				cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(cs + 1));
			region1_base = mio_boot_reg_cfg.s.base << 16;
			region1_size = (mio_boot_reg_cfg.s.size + 1) << 16;
			if (!mio_boot_reg_cfg.s.en)
				goto no_cf;
			is_true_ide = true;

		} else {
			fdt_nop_property(initial_boot_params, cf, "cavium,true-ide");
			fdt_nop_property(initial_boot_params, cf, "cavium,dma-engine-handle");
			if (!is_16bit) {
				__be32 width = cpu_to_be32(8);
				fdt_setprop_inplace(initial_boot_params, cf,
						"cavium,bus-width", &width, sizeof(width));
			}
		}
		new_reg[0] = cpu_to_be32(cs);
		new_reg[1] = cpu_to_be32(0);
		new_reg[2] = cpu_to_be32(0x10000);
		new_reg[3] = cpu_to_be32(cs + 1);
		new_reg[4] = cpu_to_be32(0);
		new_reg[5] = cpu_to_be32(0x10000);
		fdt_setprop_inplace(initial_boot_params, cf,
				    "reg",  new_reg, sizeof(new_reg));

		bootbus = fdt_parent_offset(initial_boot_params, cf);
		if (bootbus < 0)
			goto no_cf;
		ranges = fdt_getprop_w(initial_boot_params, bootbus, "ranges", &len);
		if (!ranges || len < (5 * 8 * sizeof(__be32)))
			goto no_cf;

		ranges[(cs * 5) + 2] = cpu_to_be32(region_base >> 32);
		ranges[(cs * 5) + 3] = cpu_to_be32(region_base & 0xffffffff);
		ranges[(cs * 5) + 4] = cpu_to_be32(region_size);
		if (is_true_ide) {
			cs++;
			ranges[(cs * 5) + 2] = cpu_to_be32(region1_base >> 32);
			ranges[(cs * 5) + 3] = cpu_to_be32(region1_base & 0xffffffff);
			ranges[(cs * 5) + 4] = cpu_to_be32(region1_size);
		}
		goto end_cf;
no_cf:
		fdt_nop_node(initial_boot_params, cf);

end_cf:
		;
	}

	/* 8 char LED */
	alias_prop = fdt_getprop(initial_boot_params, aliases,
				 "led0", NULL);
	if (alias_prop) {
		union cvmx_mio_boot_reg_cfgx mio_boot_reg_cfg;
		unsigned long base_ptr, region_base, region_size;
		int cs, bootbus;
		__be32 new_reg[6];
		__be32 *ranges;
		int len;
		int led = fdt_path_offset(initial_boot_params, alias_prop);

		base_ptr = octeon_bootinfo->led_display_base_addr;
		if (base_ptr == 0)
			goto no_led;
		/* Find CS0 region. */
		for (cs = 0; cs < 8; cs++) {
			mio_boot_reg_cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(cs));
			region_base = mio_boot_reg_cfg.s.base << 16;
			region_size = (mio_boot_reg_cfg.s.size + 1) << 16;
			if (mio_boot_reg_cfg.s.en && base_ptr >= region_base
				&& base_ptr < region_base + region_size)
				break;
		}

		if (cs > 7)
			goto no_led;

		new_reg[0] = cpu_to_be32(cs);
		new_reg[1] = cpu_to_be32(0x20);
		new_reg[2] = cpu_to_be32(0x20);
		new_reg[3] = cpu_to_be32(cs);
		new_reg[4] = cpu_to_be32(0);
		new_reg[5] = cpu_to_be32(0x20);
		fdt_setprop_inplace(initial_boot_params, led,
				    "reg",  new_reg, sizeof(new_reg));

		bootbus = fdt_parent_offset(initial_boot_params, led);
		if (bootbus < 0)
			goto no_led;
		ranges = fdt_getprop_w(initial_boot_params, bootbus, "ranges", &len);
		if (!ranges || len < (5 * 8 * sizeof(__be32)))
			goto no_led;

		ranges[(cs * 5) + 2] = cpu_to_be32(region_base >> 32);
		ranges[(cs * 5) + 3] = cpu_to_be32(region_base & 0xffffffff);
		ranges[(cs * 5) + 4] = cpu_to_be32(region_size);
		goto end_led;

no_led:
		fdt_nop_node(initial_boot_params, led);
end_led:
		;
	}

	/* OHCI/UHCI USB */
	alias_prop = fdt_getprop(initial_boot_params, aliases,
				 "uctl", NULL);
	if (alias_prop) {
		int uctl = fdt_path_offset(initial_boot_params, alias_prop);

		if (uctl >= 0 && (!OCTEON_IS_MODEL(OCTEON_CN6XXX) ||
				  octeon_bootinfo->board_type == CVMX_BOARD_TYPE_NIC2E)) {
			pr_debug("Deleting uctl\n");
			fdt_nop_node(initial_boot_params, uctl);
			fdt_nop_property(initial_boot_params, aliases, "uctl");
		} else if (octeon_bootinfo->board_type == CVMX_BOARD_TYPE_NIC10E ||
			   octeon_bootinfo->board_type == CVMX_BOARD_TYPE_NIC4E) {
			/* Missing "refclk-type" defaults to crystal. */
			fdt_nop_property(initial_boot_params, uctl, "refclk-type");
		}
	}

	return 0;
}

static int __init octeon_publish_devices(void)
{
	return of_platform_bus_probe(NULL, octeon_ids, NULL);
}
device_initcall(octeon_publish_devices);

MODULE_AUTHOR("David Daney <ddaney@caviumnetworks.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Platform driver for Octeon SOC");
