// SPDX-License-Identifier: GPL-2.0
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/kernel.h>

/*
 * These functions are used early on before PCI scanning is done
 * and all of the pci_dev and pci_bus structures have been created.
 */
static struct pci_dev *fake_pci_dev(struct pci_channel *hose,
	int top_bus, int busnr, int devfn)
{
	static struct pci_dev dev;
	static struct pci_bus bus;

	dev.bus = &bus;
	dev.sysdata = hose;
	dev.devfn = devfn;
	bus.number = busnr;
	bus.sysdata = hose;
	bus.ops = hose->pci_ops;

	if(busnr != top_bus)
		/* Fake a parent bus structure. */
		bus.parent = &bus;
	else
		bus.parent = NULL;

	return &dev;
}

#define EARLY_PCI_OP(rw, size, type)					\
int __init early_##rw##_config_##size(struct pci_channel *hose,		\
	int top_bus, int bus, int devfn, int offset, type value)	\
{									\
	return pci_##rw##_config_##size(				\
		fake_pci_dev(hose, top_bus, bus, devfn),		\
		offset, value);						\
}

EARLY_PCI_OP(read, byte, u8 *)
EARLY_PCI_OP(read, word, u16 *)
EARLY_PCI_OP(read, dword, u32 *)
EARLY_PCI_OP(write, byte, u8)
EARLY_PCI_OP(write, word, u16)
EARLY_PCI_OP(write, dword, u32)

int __init pci_is_66mhz_capable(struct pci_channel *hose,
				int top_bus, int current_bus)
{
	u32 pci_devfn;
	unsigned short vid;
	int cap66 = -1;
	u16 stat;

	printk(KERN_INFO "PCI: Checking 66MHz capabilities...\n");

	for (pci_devfn = 0; pci_devfn < 0xff; pci_devfn++) {
		if (PCI_FUNC(pci_devfn))
			continue;
		if (early_read_config_word(hose, top_bus, current_bus,
					   pci_devfn, PCI_VENDOR_ID, &vid) !=
		    PCIBIOS_SUCCESSFUL)
			continue;
		if (vid == 0xffff)
			continue;

		/* check 66MHz capability */
		if (cap66 < 0)
			cap66 = 1;
		if (cap66) {
			early_read_config_word(hose, top_bus, current_bus,
					       pci_devfn, PCI_STATUS, &stat);
			if (!(stat & PCI_STATUS_66MHZ)) {
				printk(KERN_DEBUG
				       "PCI: %02x:%02x not 66MHz capable.\n",
				       current_bus, pci_devfn);
				cap66 = 0;
				break;
			}
		}
	}

	return cap66 > 0;
}

static void pcibios_enable_err(unsigned long __data)
{
	struct pci_channel *hose = (struct pci_channel *)__data;

	del_timer(&hose->err_timer);
	printk(KERN_DEBUG "PCI: re-enabling error IRQ.\n");
	enable_irq(hose->err_irq);
}

static void pcibios_enable_serr(unsigned long __data)
{
	struct pci_channel *hose = (struct pci_channel *)__data;

	del_timer(&hose->serr_timer);
	printk(KERN_DEBUG "PCI: re-enabling system error IRQ.\n");
	enable_irq(hose->serr_irq);
}

void pcibios_enable_timers(struct pci_channel *hose)
{
	if (hose->err_irq) {
		init_timer(&hose->err_timer);
		hose->err_timer.data = (unsigned long)hose;
		hose->err_timer.function = pcibios_enable_err;
	}

	if (hose->serr_irq) {
		init_timer(&hose->serr_timer);
		hose->serr_timer.data = (unsigned long)hose;
		hose->serr_timer.function = pcibios_enable_serr;
	}
}

/*
 * A simple handler for the regular PCI status errors, called from IRQ
 * context.
 */
unsigned int pcibios_handle_status_errors(unsigned long addr,
					  unsigned int status,
					  struct pci_channel *hose)
{
	unsigned int cmd = 0;

	if (status & PCI_STATUS_REC_MASTER_ABORT) {
		printk(KERN_DEBUG "PCI: master abort, pc=0x%08lx\n", addr);
		cmd |= PCI_STATUS_REC_MASTER_ABORT;
	}

	if (status & PCI_STATUS_REC_TARGET_ABORT) {
		printk(KERN_DEBUG "PCI: target abort: ");
		pcibios_report_status(PCI_STATUS_REC_TARGET_ABORT |
				      PCI_STATUS_SIG_TARGET_ABORT |
				      PCI_STATUS_REC_MASTER_ABORT, 1);
		printk("\n");

		cmd |= PCI_STATUS_REC_TARGET_ABORT;
	}

	if (status & (PCI_STATUS_PARITY | PCI_STATUS_DETECTED_PARITY)) {
		printk(KERN_DEBUG "PCI: parity error detected: ");
		pcibios_report_status(PCI_STATUS_PARITY |
				      PCI_STATUS_DETECTED_PARITY, 1);
		printk("\n");

		cmd |= PCI_STATUS_PARITY | PCI_STATUS_DETECTED_PARITY;

		/* Now back off of the IRQ for awhile */
		if (hose->err_irq) {
			disable_irq_nosync(hose->err_irq);
			hose->err_timer.expires = jiffies + HZ;
			add_timer(&hose->err_timer);
		}
	}

	return cmd;
}
