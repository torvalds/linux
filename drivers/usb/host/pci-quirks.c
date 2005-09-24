/*
 * This file contains code to reset and initialize USB host controllers.
 * Some of it includes work-arounds for PCI hardware and BIOS quirks.
 * It may need to run early during booting -- before USB would normally
 * initialize -- to ensure that Linux doesn't use any legacy modes.
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 *  (and others)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/acpi.h>


/*
 * PIIX3 USB: We have to disable USB interrupts that are
 * hardwired to PIRQD# and may be shared with an
 * external device.
 *
 * Legacy Support Register (LEGSUP):
 *     bit13:  USB PIRQ Enable (USBPIRQDEN),
 *     bit4:   Trap/SMI On IRQ Enable (USBSMIEN).
 *
 * We mask out all r/wc bits, too.
 */
static void __devinit quirk_piix3_usb(struct pci_dev *dev)
{
	u16 legsup;

	pci_read_config_word(dev, 0xc0, &legsup);
	legsup &= 0x50ef;
	pci_write_config_word(dev, 0xc0, legsup);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371SB_2,	quirk_piix3_usb );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371AB_2,	quirk_piix3_usb );


/* FIXME these should be the guts of hcd->reset() methods; resolve all
 * the differences between this version and the HCD's version.
 */

#define UHCI_USBLEGSUP		0xc0		/* legacy support */
#define UHCI_USBCMD		0		/* command register */
#define UHCI_USBSTS		2		/* status register */
#define UHCI_USBINTR		4		/* interrupt register */
#define UHCI_USBLEGSUP_DEFAULT	0x2000		/* only PIRQ enable set */
#define UHCI_USBCMD_RUN		(1 << 0)	/* RUN/STOP bit */
#define UHCI_USBCMD_GRESET	(1 << 2)	/* Global reset */
#define UHCI_USBCMD_CONFIGURE	(1 << 6)	/* config semaphore */
#define UHCI_USBSTS_HALTED	(1 << 5)	/* HCHalted bit */

#define OHCI_CONTROL		0x04
#define OHCI_CMDSTATUS		0x08
#define OHCI_INTRSTATUS		0x0c
#define OHCI_INTRENABLE		0x10
#define OHCI_INTRDISABLE	0x14
#define OHCI_OCR		(1 << 3)	/* ownership change request */
#define OHCI_CTRL_IR		(1 << 8)	/* interrupt routing */
#define OHCI_INTR_OC		(1 << 30)	/* ownership change */

#define EHCI_HCC_PARAMS		0x08		/* extended capabilities */
#define EHCI_USBCMD		0		/* command register */
#define EHCI_USBCMD_RUN		(1 << 0)	/* RUN/STOP bit */
#define EHCI_USBSTS		4		/* status register */
#define EHCI_USBSTS_HALTED	(1 << 12)	/* HCHalted bit */
#define EHCI_USBINTR		8		/* interrupt register */
#define EHCI_USBLEGSUP		0		/* legacy support register */
#define EHCI_USBLEGSUP_BIOS	(1 << 16)	/* BIOS semaphore */
#define EHCI_USBLEGSUP_OS	(1 << 24)	/* OS semaphore */
#define EHCI_USBLEGCTLSTS	4		/* legacy control/status */
#define EHCI_USBLEGCTLSTS_SOOE	(1 << 13)	/* SMI on ownership change */

int usb_early_handoff __devinitdata = 0;
static int __init usb_handoff_early(char *str)
{
	usb_early_handoff = 1;
	return 0;
}
__setup("usb-handoff", usb_handoff_early);

static void __devinit quirk_usb_handoff_uhci(struct pci_dev *pdev)
{
	unsigned long base = 0;
	int wait_time, delta;
	u16 val, sts;
	int i;

	for (i = 0; i < PCI_ROM_RESOURCE; i++)
		if ((pci_resource_flags(pdev, i) & IORESOURCE_IO)) {
			base = pci_resource_start(pdev, i);
			break;
		}

	if (!base)
		return;

	/*
	 * stop controller
	 */
	sts = inw(base + UHCI_USBSTS);
	val = inw(base + UHCI_USBCMD);
	val &= ~(u16)(UHCI_USBCMD_RUN | UHCI_USBCMD_CONFIGURE);
	outw(val, base + UHCI_USBCMD);

	/*
	 * wait while it stops if it was running
	 */
	if ((sts & UHCI_USBSTS_HALTED) == 0)
	{
		wait_time = 1000;
		delta = 100;

		do {
			outw(0x1f, base + UHCI_USBSTS);
			udelay(delta);
			wait_time -= delta;
			val = inw(base + UHCI_USBSTS);
			if (val & UHCI_USBSTS_HALTED)
				break;
		} while (wait_time > 0);
	}

	/*
	 * disable interrupts & legacy support
	 */
	outw(0, base + UHCI_USBINTR);
	outw(0x1f, base + UHCI_USBSTS);
	pci_read_config_word(pdev, UHCI_USBLEGSUP, &val);
	if (val & 0xbf)
		pci_write_config_word(pdev, UHCI_USBLEGSUP, UHCI_USBLEGSUP_DEFAULT);

}

static void __devinit quirk_usb_handoff_ohci(struct pci_dev *pdev)
{
	void __iomem *base;
	int wait_time;

	base = ioremap_nocache(pci_resource_start(pdev, 0),
				     pci_resource_len(pdev, 0));
	if (base == NULL) return;

	if (readl(base + OHCI_CONTROL) & OHCI_CTRL_IR) {
		wait_time = 500; /* 0.5 seconds */
		writel(OHCI_INTR_OC, base + OHCI_INTRENABLE);
		writel(OHCI_OCR, base + OHCI_CMDSTATUS);
		while (wait_time > 0 &&
				readl(base + OHCI_CONTROL) & OHCI_CTRL_IR) {
			wait_time -= 10;
			msleep(10);
		}
	}

	/*
	 * disable interrupts
	 */
	writel(~(u32)0, base + OHCI_INTRDISABLE);
	writel(~(u32)0, base + OHCI_INTRSTATUS);

	iounmap(base);
}

static void __devinit quirk_usb_disable_ehci(struct pci_dev *pdev)
{
	int wait_time, delta;
	void __iomem *base, *op_reg_base;
	u32 hcc_params, val, temp;
	u8 cap_length;

	base = ioremap_nocache(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0));
	if (base == NULL) return;

	cap_length = readb(base);
	op_reg_base = base + cap_length;
	hcc_params = readl(base + EHCI_HCC_PARAMS);
	hcc_params = (hcc_params >> 8) & 0xff;
	if (hcc_params) {
		pci_read_config_dword(pdev,
					hcc_params + EHCI_USBLEGSUP,
					&val);
		if (((val & 0xff) == 1) && (val & EHCI_USBLEGSUP_BIOS)) {
			/*
			 * Ok, BIOS is in smm mode, try to hand off...
			 */
			pci_read_config_dword(pdev,
						hcc_params + EHCI_USBLEGCTLSTS,
						&temp);
			pci_write_config_dword(pdev,
						hcc_params + EHCI_USBLEGCTLSTS,
						temp | EHCI_USBLEGCTLSTS_SOOE);
			val |= EHCI_USBLEGSUP_OS;
			pci_write_config_dword(pdev,
						hcc_params + EHCI_USBLEGSUP,
						val);

			wait_time = 500;
			do {
				msleep(10);
				wait_time -= 10;
				pci_read_config_dword(pdev,
						hcc_params + EHCI_USBLEGSUP,
						&val);
			} while (wait_time && (val & EHCI_USBLEGSUP_BIOS));
			if (!wait_time) {
				/*
				 * well, possibly buggy BIOS...
				 */
				printk(KERN_WARNING "EHCI early BIOS handoff "
						"failed (BIOS bug ?)\n");
				pci_write_config_dword(pdev,
						hcc_params + EHCI_USBLEGSUP,
						EHCI_USBLEGSUP_OS);
				pci_write_config_dword(pdev,
						hcc_params + EHCI_USBLEGCTLSTS,
						0);
			}
		}
	}

	/*
	 * halt EHCI & disable its interrupts in any case
	 */
	val = readl(op_reg_base + EHCI_USBSTS);
	if ((val & EHCI_USBSTS_HALTED) == 0) {
		val = readl(op_reg_base + EHCI_USBCMD);
		val &= ~EHCI_USBCMD_RUN;
		writel(val, op_reg_base + EHCI_USBCMD);

		wait_time = 2000;
		delta = 100;
		do {
			writel(0x3f, op_reg_base + EHCI_USBSTS);
			udelay(delta);
			wait_time -= delta;
			val = readl(op_reg_base + EHCI_USBSTS);
			if ((val == ~(u32)0) || (val & EHCI_USBSTS_HALTED)) {
				break;
			}
		} while (wait_time > 0);
	}
	writel(0, op_reg_base + EHCI_USBINTR);
	writel(0x3f, op_reg_base + EHCI_USBSTS);

	iounmap(base);

	return;
}



static void __devinit quirk_usb_early_handoff(struct pci_dev *pdev)
{
	if (!usb_early_handoff)
		return;

	if (pdev->class == ((PCI_CLASS_SERIAL_USB << 8) | 0x00)) { /* UHCI */
		quirk_usb_handoff_uhci(pdev);
	} else if (pdev->class == ((PCI_CLASS_SERIAL_USB << 8) | 0x10)) { /* OHCI */
		quirk_usb_handoff_ohci(pdev);
	} else if (pdev->class == ((PCI_CLASS_SERIAL_USB << 8) | 0x20)) { /* EHCI */
		quirk_usb_disable_ehci(pdev);
	}

	return;
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, quirk_usb_early_handoff);
