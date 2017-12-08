// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/ip32/ip32_ints.h>
/*
 * O2 has up to 5 PCI devices connected into the MACE bridge.  The device
 * map looks like this:
 *
 * 0  aic7xxx 0
 * 1  aic7xxx 1
 * 2  expansion slot
 * 3  N/C
 * 4  N/C
 */

#define SCSI0  MACEPCI_SCSI0_IRQ
#define SCSI1  MACEPCI_SCSI1_IRQ
#define INTA0  MACEPCI_SLOT0_IRQ
#define INTA1  MACEPCI_SLOT1_IRQ
#define INTA2  MACEPCI_SLOT2_IRQ
#define INTB   MACEPCI_SHARED0_IRQ
#define INTC   MACEPCI_SHARED1_IRQ
#define INTD   MACEPCI_SHARED2_IRQ
static char irq_tab_mace[][5] = {
      /* Dummy	INT#A  INT#B  INT#C  INT#D */
	{0,	    0,	   0,	  0,	 0}, /* This is placeholder row - never used */
	{0,	SCSI0, SCSI0, SCSI0, SCSI0},
	{0,	SCSI1, SCSI1, SCSI1, SCSI1},
	{0,	INTA0,	INTB,  INTC,  INTD},
	{0,	INTA1,	INTC,  INTD,  INTB},
	{0,	INTA2,	INTD,  INTB,  INTC},
};


/*
 * Given a PCI slot number (a la PCI_SLOT(...)) and the interrupt pin of
 * the device (1-4 => A-D), tell what irq to use.  Note that we don't
 * in theory have slots 4 and 5, and we never normally use the shared
 * irqs.  I suppose a device without a pin A will thank us for doing it
 * right if there exists such a broken piece of crap.
 */
int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_tab_mace[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
