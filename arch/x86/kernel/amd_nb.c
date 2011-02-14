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

struct pci_device_id amd_nb_misc_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MISC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_MISC) },
	{}
};
EXPORT_SYMBOL(amd_nb_misc_ids);

const struct amd_nb_bus_dev_range amd_nb_bus_dev_ranges[] __initconst = {
	{ 0x00, 0x18, 0x20 },
	{ 0xff, 0x00, 0x20 },
	{ 0xfe, 0x00, 0x20 },
	{ }
};

struct amd_northbridge_info amd_northbridges;
EXPORT_SYMBOL(amd_northbridges);

static struct pci_dev *next_northbridge(struct pci_dev *dev,
					struct pci_device_id *ids)
{
	do {
		dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev);
		if (!dev)
			break;
	} while (!pci_match_id(ids, dev));
	return dev;
}

int amd_cache_northbridges(void)
{
	int i = 0;
	struct amd_northbridge *nb;
	struct pci_dev *misc;

	if (amd_nb_num())
		return 0;

	misc = NULL;
	while ((misc = next_northbridge(misc, amd_nb_misc_ids)) != NULL)
		i++;

	if (i == 0)
		return 0;

	nb = kzalloc(i * sizeof(struct amd_northbridge), GFP_KERNEL);
	if (!nb)
		return -ENOMEM;

	amd_northbridges.nb = nb;
	amd_northbridges.num = i;

	misc = NULL;
	for (i = 0; i != amd_nb_num(); i++) {
		node_to_amd_nb(i)->misc = misc =
			next_northbridge(misc, amd_nb_misc_ids);
        }

	/* some CPU families (e.g. family 0x11) do not support GART */
	if (boot_cpu_data.x86 == 0xf || boot_cpu_data.x86 == 0x10 ||
	    boot_cpu_data.x86 == 0x15)
		amd_northbridges.flags |= AMD_NB_GART;

	/*
	 * Some CPU families support L3 Cache Index Disable. There are some
	 * limitations because of E382 and E388 on family 0x10.
	 */
	if (boot_cpu_data.x86 == 0x10 &&
	    boot_cpu_data.x86_model >= 0x8 &&
	    (boot_cpu_data.x86_model > 0x9 ||
	     boot_cpu_data.x86_mask >= 0x1))
		amd_northbridges.flags |= AMD_NB_L3_INDEX_DISABLE;

	return 0;
}
EXPORT_SYMBOL_GPL(amd_cache_northbridges);

/* Ignores subdevice/subvendor but as far as I can figure out
   they're useless anyways */
int __init early_is_amd_nb(u32 device)
{
	struct pci_device_id *id;
	u32 vendor = device & 0xffff;
	device >>= 16;
	for (id = amd_nb_misc_ids; id->vendor; id++)
		if (vendor == id->vendor && device == id->device)
			return 1;
	return 0;
}

int amd_cache_gart(void)
{
       int i;

       if (!amd_nb_has_feature(AMD_NB_GART))
               return 0;

       flush_words = kmalloc(amd_nb_num() * sizeof(u32), GFP_KERNEL);
       if (!flush_words) {
               amd_northbridges.flags &= ~AMD_NB_GART;
               return -ENOMEM;
       }

       for (i = 0; i != amd_nb_num(); i++)
               pci_read_config_dword(node_to_amd_nb(i)->misc, 0x9c,
                                     &flush_words[i]);

       return 0;
}

void amd_flush_garts(void)
{
	int flushed, i;
	unsigned long flags;
	static DEFINE_SPINLOCK(gart_lock);

	if (!amd_nb_has_feature(AMD_NB_GART))
		return;

	/* Avoid races between AGP and IOMMU. In theory it's not needed
	   but I'm not sure if the hardware won't lose flush requests
	   when another is pending. This whole thing is so expensive anyways
	   that it doesn't matter to serialize more. -AK */
	spin_lock_irqsave(&gart_lock, flags);
	flushed = 0;
	for (i = 0; i < amd_nb_num(); i++) {
		pci_write_config_dword(node_to_amd_nb(i)->misc, 0x9c,
				       flush_words[i] | 1);
		flushed++;
	}
	for (i = 0; i < amd_nb_num(); i++) {
		u32 w;
		/* Make sure the hardware actually executed the flush*/
		for (;;) {
			pci_read_config_dword(node_to_amd_nb(i)->misc,
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
EXPORT_SYMBOL_GPL(amd_flush_garts);

static __init int init_amd_nbs(void)
{
	int err = 0;

	err = amd_cache_northbridges();

	if (err < 0)
		printk(KERN_NOTICE "AMD NB: Cannot enumerate AMD northbridges.\n");

	if (amd_cache_gart() < 0)
		printk(KERN_NOTICE "AMD NB: Cannot initialize GART flush words, "
		       "GART support disabled.\n");

	return err;
}

/* This has to go after the PCI subsystem */
fs_initcall(init_amd_nbs);
