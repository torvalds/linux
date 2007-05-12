/*
 * Platform device setup for Marvell mv64360/mv64460 host bridges (Discovery)
 *
 * Author: Dale Farnsworth <dale@farnsworth.org>
 *
 * 2007 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mv643xx.h>
#include <linux/platform_device.h>

#include <asm/prom.h>

/*
 * These functions provide the necessary setup for the mv64x60 drivers.
 * These drivers are unusual in that they work on both the MIPS and PowerPC
 * architectures.  Because of that, the drivers do not support the normal
 * PowerPC of_platform_bus_type.  They support platform_bus_type instead.
 */

/*
 * Create MPSC platform devices
 */
static int __init mv64x60_mpsc_register_shared_pdev(struct device_node *np)
{
	struct platform_device *pdev;
	struct resource r[2];
	struct mpsc_shared_pdata pdata;
	const phandle *ph;
	struct device_node *mpscrouting, *mpscintr;
	int err;

	ph = of_get_property(np, "mpscrouting", NULL);
	mpscrouting = of_find_node_by_phandle(*ph);
	if (!mpscrouting)
		return -ENODEV;

	err = of_address_to_resource(mpscrouting, 0, &r[0]);
	of_node_put(mpscrouting);
	if (err)
		return err;

	ph = of_get_property(np, "mpscintr", NULL);
	mpscintr = of_find_node_by_phandle(*ph);
	if (!mpscintr)
		return -ENODEV;

	err = of_address_to_resource(mpscintr, 0, &r[1]);
	of_node_put(mpscintr);
	if (err)
		return err;

	memset(&pdata, 0, sizeof(pdata));

	pdev = platform_device_alloc(MPSC_SHARED_NAME, 0);
	if (!pdev)
		return -ENOMEM;

	err = platform_device_add_resources(pdev, r, 2);
	if (err)
		goto error;

	err = platform_device_add_data(pdev, &pdata, sizeof(pdata));
	if (err)
		goto error;

	err = platform_device_add(pdev);
	if (err)
		goto error;

	return 0;

error:
	platform_device_put(pdev);
	return err;
}


static int __init mv64x60_mpsc_device_setup(struct device_node *np, int id)
{
	struct resource r[5];
	struct mpsc_pdata pdata;
	struct platform_device *pdev;
	const unsigned int *prop;
	const phandle *ph;
	struct device_node *sdma, *brg;
	int err;
	int port_number;

	/* only register the shared platform device the first time through */
	if (id == 0 && (err = mv64x60_mpsc_register_shared_pdev(np)))
		return err;

	memset(r, 0, sizeof(r));

	err = of_address_to_resource(np, 0, &r[0]);
	if (err)
		return err;

	of_irq_to_resource(np, 0, &r[4]);

	ph = of_get_property(np, "sdma", NULL);
	sdma = of_find_node_by_phandle(*ph);
	if (!sdma)
		return -ENODEV;

	of_irq_to_resource(sdma, 0, &r[3]);
	err = of_address_to_resource(sdma, 0, &r[1]);
	of_node_put(sdma);
	if (err)
		return err;

	ph = of_get_property(np, "brg", NULL);
	brg = of_find_node_by_phandle(*ph);
	if (!brg)
		return -ENODEV;

	err = of_address_to_resource(brg, 0, &r[2]);
	of_node_put(brg);
	if (err)
		return err;

	prop = of_get_property(np, "block-index", NULL);
	if (!prop)
		return -ENODEV;
	port_number = *(int *)prop;

	memset(&pdata, 0, sizeof(pdata));

	pdata.cache_mgmt = 1; /* All current revs need this set */

	prop = of_get_property(np, "max_idle", NULL);
	if (prop)
		pdata.max_idle = *prop;

	prop = of_get_property(brg, "current-speed", NULL);
	if (prop)
		pdata.default_baud = *prop;

	/* Default is 8 bits, no parity, no flow control */
	pdata.default_bits = 8;
	pdata.default_parity = 'n';
	pdata.default_flow = 'n';

	prop = of_get_property(np, "chr_1", NULL);
	if (prop)
		pdata.chr_1_val = *prop;

	prop = of_get_property(np, "chr_2", NULL);
	if (prop)
		pdata.chr_2_val = *prop;

	prop = of_get_property(np, "chr_10", NULL);
	if (prop)
		pdata.chr_10_val = *prop;

	prop = of_get_property(np, "mpcr", NULL);
	if (prop)
		pdata.mpcr_val = *prop;

	prop = of_get_property(brg, "bcr", NULL);
	if (prop)
		pdata.bcr_val = *prop;

	pdata.brg_can_tune = 1; /* All current revs need this set */

	prop = of_get_property(brg, "clock-src", NULL);
	if (prop)
		pdata.brg_clk_src = *prop;

	prop = of_get_property(brg, "clock-frequency", NULL);
	if (prop)
		pdata.brg_clk_freq = *prop;

	pdev = platform_device_alloc(MPSC_CTLR_NAME, port_number);
	if (!pdev)
		return -ENOMEM;

	err = platform_device_add_resources(pdev, r, 5);
	if (err)
		goto error;

	err = platform_device_add_data(pdev, &pdata, sizeof(pdata));
	if (err)
		goto error;

	err = platform_device_add(pdev);
	if (err)
		goto error;

	return 0;

error:
	platform_device_put(pdev);
	return err;
}

static int __init mv64x60_device_setup(void)
{
	struct device_node *np = NULL;
	int id;
	int err;

	for (id = 0;
	     (np = of_find_compatible_node(np, "serial", "marvell,mpsc")); id++)
		if ((err = mv64x60_mpsc_device_setup(np, id)))
			goto error;

	return 0;

error:
	of_node_put(np);
	return err;
}
arch_initcall(mv64x60_device_setup);
