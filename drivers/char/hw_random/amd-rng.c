/*
 * RNG driver for AMD RNGs
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * with the majority of the code coming from:
 *
 * Hardware driver for the Intel/AMD/VIA Random Number Generators (RNG)
 * (c) Copyright 2003 Red Hat Inc <jgarzik@redhat.com>
 *
 * derived from
 *
 * Hardware driver for the AMD 768 Random Number Generator (RNG)
 * (c) Copyright 2001 Red Hat Inc
 *
 * derived from
 *
 * Hardware driver for Intel i810 Random Number Generator (RNG)
 * Copyright 2000,2001 Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2000,2001 Philipp Rumpf <prumpf@mandrakesoft.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#define DRV_NAME "AMD768-HWRNG"

#define RNGDATA		0x00
#define RNGDONE		0x04
#define PMBASE_OFFSET	0xF0
#define PMBASE_SIZE	8

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static const struct pci_device_id pci_tbl[] = {
	{ PCI_VDEVICE(AMD, 0x7443), 0, },
	{ PCI_VDEVICE(AMD, 0x746b), 0, },
	{ 0, },	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, pci_tbl);

struct amd768_priv {
	void __iomem *iobase;
	struct pci_dev *pcidev;
};

static int amd_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	u32 *data = buf;
	struct amd768_priv *priv = (struct amd768_priv *)rng->priv;
	size_t read = 0;
	/* We will wait at maximum one time per read */
	int timeout = max / 4 + 1;

	/*
	 * RNG data is available when RNGDONE is set to 1
	 * New random numbers are generated approximately 128 microseconds
	 * after RNGDATA is read
	 */
	while (read < max) {
		if (ioread32(priv->iobase + RNGDONE) == 0) {
			if (wait) {
				/* Delay given by datasheet */
				usleep_range(128, 196);
				if (timeout-- == 0)
					return read;
			} else {
				return 0;
			}
		} else {
			*data = ioread32(priv->iobase + RNGDATA);
			data++;
			read += 4;
		}
	}

	return read;
}

static int amd_rng_init(struct hwrng *rng)
{
	struct amd768_priv *priv = (struct amd768_priv *)rng->priv;
	u8 rnen;

	pci_read_config_byte(priv->pcidev, 0x40, &rnen);
	rnen |= BIT(7);	/* RNG on */
	pci_write_config_byte(priv->pcidev, 0x40, rnen);

	pci_read_config_byte(priv->pcidev, 0x41, &rnen);
	rnen |= BIT(7);	/* PMIO enable */
	pci_write_config_byte(priv->pcidev, 0x41, rnen);

	return 0;
}

static void amd_rng_cleanup(struct hwrng *rng)
{
	struct amd768_priv *priv = (struct amd768_priv *)rng->priv;
	u8 rnen;

	pci_read_config_byte(priv->pcidev, 0x40, &rnen);
	rnen &= ~BIT(7);	/* RNG off */
	pci_write_config_byte(priv->pcidev, 0x40, rnen);
}

static struct hwrng amd_rng = {
	.name		= "amd",
	.init		= amd_rng_init,
	.cleanup	= amd_rng_cleanup,
	.read		= amd_rng_read,
};

static int __init mod_init(void)
{
	int err = -ENODEV;
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *ent;
	u32 pmbase;
	struct amd768_priv *priv;

	for_each_pci_dev(pdev) {
		ent = pci_match_id(pci_tbl, pdev);
		if (ent)
			goto found;
	}
	/* Device not found. */
	return -ENODEV;

found:
	err = pci_read_config_dword(pdev, 0x58, &pmbase);
	if (err)
		return err;

	pmbase &= 0x0000FF00;
	if (pmbase == 0)
		return -EIO;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (!devm_request_region(&pdev->dev, pmbase + PMBASE_OFFSET,
				PMBASE_SIZE, DRV_NAME)) {
		dev_err(&pdev->dev, DRV_NAME " region 0x%x already in use!\n",
			pmbase + 0xF0);
		return -EBUSY;
	}

	priv->iobase = devm_ioport_map(&pdev->dev, pmbase + PMBASE_OFFSET,
			PMBASE_SIZE);
	if (!priv->iobase) {
		pr_err(DRV_NAME "Cannot map ioport\n");
		return -ENOMEM;
	}

	amd_rng.priv = (unsigned long)priv;
	priv->pcidev = pdev;

	pr_info(DRV_NAME " detected\n");
	return devm_hwrng_register(&pdev->dev, &amd_rng);
}

static void __exit mod_exit(void)
{
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR("The Linux Kernel team");
MODULE_DESCRIPTION("H/W RNG driver for AMD chipsets");
MODULE_LICENSE("GPL");
