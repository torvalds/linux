/*
 * arch/sh/stboard/setup.c
 *
 * Copyright (C) 2001 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * STMicroelectronics ST40STB1 HARP and compatible support.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/harp/harp.h>

const char *get_system_type(void)
{
	return "STB1 Harp";
}

/*
 * Initialize the board
 */
int __init platform_setup(void)
{
#ifdef CONFIG_SH_STB1_HARP
	unsigned long ic8_version, ic36_version;

	ic8_version = ctrl_inl(EPLD_REVID2);
	ic36_version = ctrl_inl(EPLD_REVID1);

        printk("STMicroelectronics STB1 HARP initialisaton\n");
        printk("EPLD versions: IC8: %d.%02d, IC36: %d.%02d\n",
               (ic8_version >> 4) & 0xf, ic8_version & 0xf,
               (ic36_version >> 4) & 0xf, ic36_version & 0xf);
#elif defined(CONFIG_SH_STB1_OVERDRIVE)
	unsigned long version;

	version = ctrl_inl(EPLD_REVID);

        printk("STMicroelectronics STB1 Overdrive initialisaton\n");
        printk("EPLD version: %d.%02d\n",
	       (version >> 4) & 0xf, version & 0xf);
#else
#error Undefined machine
#endif
 
        /* Currently all STB1 chips have problems with the sleep instruction,
         * so disable it here.
         */
	disable_hlt();

	return 0;
}

/*
 * pcibios_map_platform_irq
 *
 * This is board specific and returns the IRQ for a given PCI device.
 * It is used by the PCI code (arch/sh/kernel/st40_pci*)
 *
 */

#define HARP_PCI_IRQ    1
#define HARP_BRIDGE_IRQ 2
#define OVERDRIVE_SLOT0_IRQ 0


int __init pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	switch (slot) {
#ifdef CONFIG_SH_STB1_HARP
	case 2:		/*This is the PCI slot on the */
		return HARP_PCI_IRQ;
	case 1:		/* this is the bridge */
		return HARP_BRIDGE_IRQ;
#elif defined(CONFIG_SH_STB1_OVERDRIVE)
	case 1:
	case 2:
	case 3:
		return slot - 1;
#else
#error Unknown board
#endif
	default:
		return -1;
	}
}

