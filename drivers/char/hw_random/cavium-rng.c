/*
 * Hardware Random Number Generator support for Cavium Inc.
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

#define THUNDERX_RNM_ENT_EN     0x1
#define THUNDERX_RNM_RNG_EN     0x2

struct cavium_rng_pf {
	void __iomem *control_status;
};

/* Enable the RNG hardware and activate the VF */
static int cavium_rng_probe(struct pci_dev *pdev,
			const struct pci_device_id *id)
{
	struct	cavium_rng_pf *rng;
	int	iov_err;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	/*Map the RNG control */
	rng->control_status = pcim_iomap(pdev, 0, 0);
	if (!rng->control_status) {
		dev_err(&pdev->dev,
			"Error iomap failed retrieving control_status.\n");
		return -ENOMEM;
	}

	/* Enable the RNG hardware and entropy source */
	writeq(THUNDERX_RNM_RNG_EN | THUNDERX_RNM_ENT_EN,
		rng->control_status);

	pci_set_drvdata(pdev, rng);

	/* Enable the Cavium RNG as a VF */
	iov_err = pci_enable_sriov(pdev, 1);
	if (iov_err != 0) {
		/* Disable the RNG hardware and entropy source */
		writeq(0, rng->control_status);
		dev_err(&pdev->dev,
			"Error initializing RNG virtual function,(%i).\n",
			iov_err);
		return iov_err;
	}

	return 0;
}

/* Disable VF and RNG Hardware */
void  cavium_rng_remove(struct pci_dev *pdev)
{
	struct cavium_rng_pf *rng;

	rng = pci_get_drvdata(pdev);

	/* Remove the VF */
	pci_disable_sriov(pdev);

	/* Disable the RNG hardware and entropy source */
	writeq(0, rng->control_status);
}

static const struct pci_device_id cavium_rng_pf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, 0xa018), 0, 0, 0}, /* Thunder RNM */
	{0,},
};

MODULE_DEVICE_TABLE(pci, cavium_rng_pf_id_table);

static struct pci_driver cavium_rng_pf_driver = {
	.name		= "cavium_rng_pf",
	.id_table	= cavium_rng_pf_id_table,
	.probe		= cavium_rng_probe,
	.remove		= cavium_rng_remove,
};

module_pci_driver(cavium_rng_pf_driver);
MODULE_AUTHOR("Omer Khaliq <okhaliq@caviumnetworks.com>");
MODULE_LICENSE("GPL");
