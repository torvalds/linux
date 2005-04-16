/*
 * Cobalt Qube/Raq PCI support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2002, 2003 by Ralf Baechle
 * Copyright (C) 2001, 2002, 2003 by Liam Davies (ldavies@agile.tv)
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/pci.h>
#include <asm/io.h>
#include <asm/gt64120.h>

#include <asm/cobalt/cobalt.h>

extern int cobalt_board_id;

static void qube_raq_via_bmIDE_fixup(struct pci_dev *dev)
{
	unsigned short cfgword;
	unsigned char lt;

	/* Enable Bus Mastering and fast back to back. */
	pci_read_config_word(dev, PCI_COMMAND, &cfgword);
	cfgword |= (PCI_COMMAND_FAST_BACK | PCI_COMMAND_MASTER);
	pci_write_config_word(dev, PCI_COMMAND, cfgword);

	/* Enable both ide interfaces. ROM only enables primary one.  */
	pci_write_config_byte(dev, 0x40, 0xb);

	/* Set latency timer to reasonable value. */
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lt);
	if (lt < 64)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 7);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1,
	 qube_raq_via_bmIDE_fixup);

static void qube_raq_galileo_fixup(struct pci_dev *dev)
{
	unsigned short galileo_id;

	/* Fix PCI latency-timer and cache-line-size values in Galileo
	 * host bridge.
	 */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 7);

	/*
	 * On all machines prior to Q2, we had the STOP line disconnected
	 * from Galileo to VIA on PCI.  The new Galileo does not function
	 * correctly unless we have it connected.
	 *
	 * Therefore we must set the disconnect/retry cycle values to
	 * something sensible when using the new Galileo.
	 */
	pci_read_config_word(dev, PCI_REVISION_ID, &galileo_id);
	galileo_id &= 0xff;	/* mask off class info */
	if (galileo_id >= 0x10) {
		/* New Galileo, assumes PCI stop line to VIA is connected. */
		GALILEO_OUTL(0x4020, GT_PCI0_TOR_OFS);
	} else if (galileo_id == 0x1 || galileo_id == 0x2) {
		signed int timeo;
		/* XXX WE MUST DO THIS ELSE GALILEO LOCKS UP! -DaveM */
		timeo = GALILEO_INL(GT_PCI0_TOR_OFS);
		/* Old Galileo, assumes PCI STOP line to VIA is disconnected. */
		GALILEO_OUTL(0xffff, GT_PCI0_TOR_OFS);
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_GALILEO, PCI_ANY_ID,
	 qube_raq_galileo_fixup);

static char irq_tab_cobalt[] __initdata = {
  [COBALT_PCICONF_CPU]     = 0,
  [COBALT_PCICONF_ETH0]    = COBALT_ETH0_IRQ,
  [COBALT_PCICONF_RAQSCSI] = COBALT_SCSI_IRQ,
  [COBALT_PCICONF_VIA]     = 0,
  [COBALT_PCICONF_PCISLOT] = COBALT_QUBE_SLOT_IRQ,
  [COBALT_PCICONF_ETH1]    = COBALT_ETH1_IRQ
};

static char irq_tab_raq2[] __initdata = {
  [COBALT_PCICONF_CPU]     = 0,
  [COBALT_PCICONF_ETH0]    = COBALT_ETH0_IRQ,
  [COBALT_PCICONF_RAQSCSI] = COBALT_RAQ_SCSI_IRQ,
  [COBALT_PCICONF_VIA]     = 0,
  [COBALT_PCICONF_PCISLOT] = COBALT_QUBE_SLOT_IRQ,
  [COBALT_PCICONF_ETH1]    = COBALT_ETH1_IRQ
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (cobalt_board_id == COBALT_BRD_ID_RAQ2)
		return irq_tab_raq2[slot];

	return irq_tab_cobalt[slot];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
