/*
 * Hardware Random Number Generator support for Cavium, Inc.
 * Thunder processor family.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016 Cavium, Inc.
 */

#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

struct cavium_rng {
	struct hwrng ops;
	void __iomem *result;
};

/* Read data from the RNG unit */
static int cavium_rng_read(struct hwrng *rng, void *dat, size_t max, bool wait)
{
	struct cavium_rng *p = container_of(rng, struct cavium_rng, ops);
	unsigned int size = max;

	while (size >= 8) {
		*((u64 *)dat) = readq(p->result);
		size -= 8;
		dat += 8;
	}
	while (size > 0) {
		*((u8 *)dat) = readb(p->result);
		size--;
		dat++;
	}
	return max;
}

/* Map Cavium RNG to an HWRNG object */
static int cavium_rng_probe_vf(struct	pci_dev		*pdev,
			 const struct	pci_device_id	*id)
{
	struct	cavium_rng *rng;
	int	ret;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	/* Map the RNG result */
	rng->result = pcim_iomap(pdev, 0, 0);
	if (!rng->result) {
		dev_err(&pdev->dev, "Error iomap failed retrieving result.\n");
		return -ENOMEM;
	}

	rng->ops.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				       "cavium-rng-%s", dev_name(&pdev->dev));
	if (!rng->ops.name)
		return -ENOMEM;

	rng->ops.read    = cavium_rng_read;
	rng->ops.quality = 1000;

	pci_set_drvdata(pdev, rng);

	ret = devm_hwrng_register(&pdev->dev, &rng->ops);
	if (ret) {
		dev_err(&pdev->dev, "Error registering device as HWRNG.\n");
		return ret;
	}

	return 0;
}


static const struct pci_device_id cavium_rng_vf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, 0xa033), 0, 0, 0},
	{0,},
};
MODULE_DEVICE_TABLE(pci, cavium_rng_vf_id_table);

static struct pci_driver cavium_rng_vf_driver = {
	.name		= "cavium_rng_vf",
	.id_table	= cavium_rng_vf_id_table,
	.probe		= cavium_rng_probe_vf,
};
module_pci_driver(cavium_rng_vf_driver);

MODULE_AUTHOR("Omer Khaliq <okhaliq@caviumnetworks.com>");
MODULE_LICENSE("GPL");
