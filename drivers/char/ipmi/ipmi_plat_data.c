// SPDX-License-Identifier: GPL-2.0+

/*
 * Add an IPMI platform device.
 */

#include <linux/platform_device.h>
#include "ipmi_plat_data.h"
#include "ipmi_si.h"

struct platform_device *ipmi_platform_add(const char *name, unsigned int inst,
					  struct ipmi_plat_data *p)
{
	struct platform_device *pdev;
	unsigned int num_r = 1, size = 0, pidx = 0;
	struct resource r[4];
	struct property_entry pr[6];
	u32 flags;
	int rv;

	memset(pr, 0, sizeof(pr));
	memset(r, 0, sizeof(r));

	if (p->iftype == IPMI_PLAT_IF_SI) {
		if (p->type == SI_BT)
			size = 3;
		else if (p->type != SI_TYPE_INVALID)
			size = 2;

		if (p->regsize == 0)
			p->regsize = DEFAULT_REGSIZE;
		if (p->regspacing == 0)
			p->regspacing = p->regsize;

		pr[pidx++] = PROPERTY_ENTRY_U8("ipmi-type", p->type);
	} else if (p->iftype == IPMI_PLAT_IF_SSIF) {
		pr[pidx++] = PROPERTY_ENTRY_U16("i2c-addr", p->addr);
	}

	if (p->slave_addr)
		pr[pidx++] = PROPERTY_ENTRY_U8("slave-addr", p->slave_addr);
	pr[pidx++] = PROPERTY_ENTRY_U8("addr-source", p->addr_source);
	if (p->regshift)
		pr[pidx++] = PROPERTY_ENTRY_U8("reg-shift", p->regshift);
	pr[pidx++] = PROPERTY_ENTRY_U8("reg-size", p->regsize);
	/* Last entry must be left NULL to terminate it. */

	pdev = platform_device_alloc(name, inst);
	if (!pdev) {
		pr_err("Error allocating IPMI platform device %s.%d\n",
		       name, inst);
		return NULL;
	}

	if (size == 0)
		/* An invalid or SSIF interface, no resources. */
		goto add_properties;

	/*
	 * Register spacing is derived from the resources in
	 * the IPMI platform code.
	 */

	if (p->space == IPMI_IO_ADDR_SPACE)
		flags = IORESOURCE_IO;
	else
		flags = IORESOURCE_MEM;

	r[0].start = p->addr;
	r[0].end = r[0].start + p->regsize - 1;
	r[0].name = "IPMI Address 1";
	r[0].flags = flags;

	if (size > 1) {
		r[1].start = r[0].start + p->regspacing;
		r[1].end = r[1].start + p->regsize - 1;
		r[1].name = "IPMI Address 2";
		r[1].flags = flags;
		num_r++;
	}

	if (size > 2) {
		r[2].start = r[1].start + p->regspacing;
		r[2].end = r[2].start + p->regsize - 1;
		r[2].name = "IPMI Address 3";
		r[2].flags = flags;
		num_r++;
	}

	if (p->irq) {
		r[num_r].start = p->irq;
		r[num_r].end = p->irq;
		r[num_r].name = "IPMI IRQ";
		r[num_r].flags = IORESOURCE_IRQ;
		num_r++;
	}

	rv = platform_device_add_resources(pdev, r, num_r);
	if (rv) {
		dev_err(&pdev->dev,
			"Unable to add hard-code resources: %d\n", rv);
		goto err;
	}
 add_properties:
	rv = device_create_managed_software_node(&pdev->dev, pr, NULL);
	if (rv) {
		dev_err(&pdev->dev,
			"Unable to add hard-code properties: %d\n", rv);
		goto err;
	}

	rv = platform_device_add(pdev);
	if (rv) {
		dev_err(&pdev->dev,
			"Unable to add hard-code device: %d\n", rv);
		goto err;
	}
	return pdev;

err:
	platform_device_put(pdev);
	return NULL;
}
EXPORT_SYMBOL(ipmi_platform_add);
