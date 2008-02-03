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
 * (c) Copyright 2001 Red Hat Inc <alan@redhat.com>
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <asm/io.h>


#define PFX	KBUILD_MODNAME ": "


/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static const struct pci_device_id pci_tbl[] = {
	{ 0x1022, 0x7443, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
	{ 0x1022, 0x746b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
	{ 0, },	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct pci_dev *amd_pdev;


static int amd_rng_data_present(struct hwrng *rng, int wait)
{
	u32 pmbase = (u32)rng->priv;
	int data, i;

	for (i = 0; i < 20; i++) {
		data = !!(inl(pmbase + 0xF4) & 1);
		if (data || !wait)
			break;
		udelay(10);
	}
	return data;
}

static int amd_rng_data_read(struct hwrng *rng, u32 *data)
{
	u32 pmbase = (u32)rng->priv;

	*data = inl(pmbase + 0xF0);

	return 4;
}

static int amd_rng_init(struct hwrng *rng)
{
	u8 rnen;

	pci_read_config_byte(amd_pdev, 0x40, &rnen);
	rnen |= (1 << 7);	/* RNG on */
	pci_write_config_byte(amd_pdev, 0x40, rnen);

	pci_read_config_byte(amd_pdev, 0x41, &rnen);
	rnen |= (1 << 7);	/* PMIO enable */
	pci_write_config_byte(amd_pdev, 0x41, rnen);

	return 0;
}

static void amd_rng_cleanup(struct hwrng *rng)
{
	u8 rnen;

	pci_read_config_byte(amd_pdev, 0x40, &rnen);
	rnen &= ~(1 << 7);	/* RNG off */
	pci_write_config_byte(amd_pdev, 0x40, rnen);
}


static struct hwrng amd_rng = {
	.name		= "amd",
	.init		= amd_rng_init,
	.cleanup	= amd_rng_cleanup,
	.data_present	= amd_rng_data_present,
	.data_read	= amd_rng_data_read,
};


static int __init mod_init(void)
{
	int err = -ENODEV;
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *ent;
	u32 pmbase;

	for_each_pci_dev(pdev) {
		ent = pci_match_id(pci_tbl, pdev);
		if (ent)
			goto found;
	}
	/* Device not found. */
	goto out;

found:
	err = pci_read_config_dword(pdev, 0x58, &pmbase);
	if (err)
		goto out;
	err = -EIO;
	pmbase &= 0x0000FF00;
	if (pmbase == 0)
		goto out;
	amd_rng.priv = (unsigned long)pmbase;
	amd_pdev = pdev;

	printk(KERN_INFO "AMD768 RNG detected\n");
	err = hwrng_register(&amd_rng);
	if (err) {
		printk(KERN_ERR PFX "RNG registering failed (%d)\n",
		       err);
		goto out;
	}
out:
	return err;
}

static void __exit mod_exit(void)
{
	hwrng_unregister(&amd_rng);
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR("The Linux Kernel team");
MODULE_DESCRIPTION("H/W RNG driver for AMD chipsets");
MODULE_LICENSE("GPL");
