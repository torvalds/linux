/*
 * Shared support code for AMD K8 northbridges and derivates.
 * Copyright 2006 Andi Kleen, SUSE Labs. Subject to GPLv2.
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/amd_nb.h>

static u32 *flush_words;

struct pci_device_id k8_nb_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MISC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_MISC) },
	{}
};
EXPORT_SYMBOL(k8_nb_ids);

struct k8_northbridge_info k8_northbridges;
EXPORT_SYMBOL(k8_northbridges);

static struct pci_dev *next_k8_northbridge(struct pci_dev *dev)
{
	do {
		dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev);
		if (!dev)
			break;
	} while (!pci_match_id(&k8_nb_ids[0], dev));
	return dev;
}

int cache_k8_northbridges(void)
{
	int i;
	struct pci_dev *dev;

	if (k8_northbridges.num)
		return 0;

	dev = NULL;
	while ((dev = next_k8_northbridge(dev)) != NULL)
		k8_northbridges.num++;

	/* some CPU families (e.g. family 0x11) do not support GART */
	if (boot_cpu_data.x86 == 0xf || boot_cpu_data.x86 == 0x10 ||
	    boot_cpu_data.x86 == 0x15)
		k8_northbridges.gart_supported = 1;

	k8_northbridges.nb_misc = kmalloc((k8_northbridges.num + 1) *
					  sizeof(void *), GFP_KERNEL);
	if (!k8_northbridges.nb_misc)
		return -ENOMEM;

	if (!k8_northbridges.num) {
		k8_northbridges.nb_misc[0] = NULL;
		return 0;
	}

	if (k8_northbridges.gart_supported) {
		flush_words = kmalloc(k8_northbridges.num * sizeof(u32),
				      GFP_KERNEL);
		if (!flush_words) {
			kfree(k8_northbridges.nb_misc);
			return -ENOMEM;
		}
	}

	dev = NULL;
	i = 0;
	while ((dev = next_k8_northbridge(dev)) != NULL) {
		k8_northbridges.nb_misc[i] = dev;
		if (k8_northbridges.gart_supported)
			pci_read_config_dword(dev, 0x9c, &flush_words[i++]);
	}
	k8_northbridges.nb_misc[i] = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(cache_k8_northbridges);

/* Ignores subdevice/subvendor but as far as I can figure out
   they're useless anyways */
int __init early_is_k8_nb(u32 device)
{
	struct pci_device_id *id;
	u32 vendor = device & 0xffff;
	device >>= 16;
	for (id = k8_nb_ids; id->vendor; id++)
		if (vendor == id->vendor && device == id->device)
			return 1;
	return 0;
}

void k8_flush_garts(void)
{
	int flushed, i;
	unsigned long flags;
	static DEFINE_SPINLOCK(gart_lock);

	if (!k8_northbridges.gart_supported)
		return;

	/* Avoid races between AGP and IOMMU. In theory it's not needed
	   but I'm not sure if the hardware won't lose flush requests
	   when another is pending. This whole thing is so expensive anyways
	   that it doesn't matter to serialize more. -AK */
	spin_lock_irqsave(&gart_lock, flags);
	flushed = 0;
	for (i = 0; i < k8_northbridges.num; i++) {
		pci_write_config_dword(k8_northbridges.nb_misc[i], 0x9c,
				       flush_words[i]|1);
		flushed++;
	}
	for (i = 0; i < k8_northbridges.num; i++) {
		u32 w;
		/* Make sure the hardware actually executed the flush*/
		for (;;) {
			pci_read_config_dword(k8_northbridges.nb_misc[i],
					      0x9c, &w);
			if (!(w & 1))
				break;
			cpu_relax();
		}
	}
	spin_unlock_irqrestore(&gart_lock, flags);
	if (!flushed)
		printk("nothing to flush?\n");
}
EXPORT_SYMBOL_GPL(k8_flush_garts);

static __init int init_k8_nbs(void)
{
	int err = 0;

	err = cache_k8_northbridges();

	if (err < 0)
		printk(KERN_NOTICE "K8 NB: Cannot enumerate AMD northbridges.\n");

	return err;
}

/* This has to go after the PCI subsystem */
fs_initcall(init_k8_nbs);
