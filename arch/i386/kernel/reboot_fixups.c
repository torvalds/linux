/*
 * linux/arch/i386/kernel/reboot_fixups.c
 *
 * This is a good place to put board specific reboot fixups.
 *
 * List of supported fixups:
 * geode-gx1/cs5530a - Jaya Kumar <jayalk@intworks.biz>
 *
 */

#include <asm/delay.h>
#include <linux/pci.h>
#include <linux/reboot_fixups.h>

static void cs5530a_warm_reset(struct pci_dev *dev)
{
	/* writing 1 to the reset control register, 0x44 causes the
	cs5530a to perform a system warm reset */
	pci_write_config_byte(dev, 0x44, 0x1);
	udelay(50); /* shouldn't get here but be safe and spin-a-while */
	return;
}

struct device_fixup {
	unsigned int vendor;
	unsigned int device;
	void (*reboot_fixup)(struct pci_dev *);
};

static struct device_fixup fixups_table[] = {
{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_LEGACY, cs5530a_warm_reset },
};

/*
 * we see if any fixup is available for our current hardware. if there
 * is a fixup, we call it and we expect to never return from it. if we
 * do return, we keep looking and then eventually fall back to the
 * standard mach_reboot on return.
 */
void mach_reboot_fixups(void)
{
	struct device_fixup *cur;
	struct pci_dev *dev;
	int i;

	for (i=0; i < ARRAY_SIZE(fixups_table); i++) {
		cur = &(fixups_table[i]);
		dev = pci_get_device(cur->vendor, cur->device, NULL);
		if (!dev)
			continue;

		cur->reboot_fixup(dev);
	}
}

