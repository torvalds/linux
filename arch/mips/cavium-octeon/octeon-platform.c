/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2009 Cavium Networks
 * Copyright (C) 2008 Wind River Systems
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-rnm-defs.h>

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

		/* Asume that CS1 immediately follows. */
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


#define OCTEON_I2C_IO_BASE 0x1180000001000ull
#define OCTEON_I2C_IO_UNIT_OFFSET 0x200

static struct octeon_i2c_data octeon_i2c_data[2];

static int __init octeon_i2c_device_init(void)
{
	struct platform_device *pd;
	int ret = 0;
	int port, num_ports;

	struct resource i2c_resources[] = {
		{
			.flags	= IORESOURCE_MEM,
		}, {
			.flags	= IORESOURCE_IRQ,
		}
	};

	if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN52XX))
		num_ports = 2;
	else
		num_ports = 1;

	for (port = 0; port < num_ports; port++) {
		octeon_i2c_data[port].sys_freq = octeon_get_clock_rate();
		/*FIXME: should be examined. At the moment is set for 100Khz */
		octeon_i2c_data[port].i2c_freq = 100000;

		pd = platform_device_alloc("i2c-octeon", port);
		if (!pd) {
			ret = -ENOMEM;
			goto out;
		}

		pd->dev.platform_data = octeon_i2c_data + port;

		i2c_resources[0].start =
			OCTEON_I2C_IO_BASE + (port * OCTEON_I2C_IO_UNIT_OFFSET);
		i2c_resources[0].end = i2c_resources[0].start + 0x1f;
		switch (port) {
		case 0:
			i2c_resources[1].start = OCTEON_IRQ_TWSI;
			i2c_resources[1].end = OCTEON_IRQ_TWSI;
			break;
		case 1:
			i2c_resources[1].start = OCTEON_IRQ_TWSI2;
			i2c_resources[1].end = OCTEON_IRQ_TWSI2;
			break;
		default:
			BUG();
		}

		ret = platform_device_add_resources(pd,
						    i2c_resources,
						    ARRAY_SIZE(i2c_resources));
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
device_initcall(octeon_i2c_device_init);

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

MODULE_AUTHOR("David Daney <ddaney@caviumnetworks.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Platform driver for Octeon SOC");
