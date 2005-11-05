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
#ifdef CONFIG_USB_DEBUG
#define DEBUG
#else
#undef DEBUG
#endif

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/acpi.h>


#define UHCI_USBLEGSUP		0xc0		/* legacy support */
#define UHCI_USBCMD		0		/* command register */
#define UHCI_USBINTR		4		/* interrupt register */
#define UHCI_USBLEGSUP_RWC	0x8f00		/* the R/WC bits */
#define UHCI_USBLEGSUP_RO	0x5040		/* R/O and reserved bits */
#define UHCI_USBCMD_RUN		0x0001		/* RUN/STOP bit */
#define UHCI_USBCMD_HCRESET	0x0002		/* Host Controller reset */
#define UHCI_USBCMD_EGSM	0x0008		/* Global Suspend Mode */
#define UHCI_USBCMD_CONFIGURE	0x0040		/* Config Flag */
#define UHCI_USBINTR_RESUME	0x0002		/* Resume interrupt enable */

#define OHCI_CONTROL		0x04
#define OHCI_CMDSTATUS		0x08
#define OHCI_INTRSTATUS		0x0c
#define OHCI_INTRENABLE		0x10
#define OHCI_INTRDISABLE	0x14
#define OHCI_OCR		(1 << 3)	/* ownership change request */
#define OHCI_CTRL_RWC		(1 << 9)	/* remote wakeup connected */
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


/*
 * Make sure the controller is completely inactive, unable to
 * generate interrupts or do DMA.
 */
void uhci_reset_hc(struct pci_dev *pdev, unsigned long base)
{
	/* Turn off PIRQ enable and SMI enable.  (This also turns off the
	 * BIOS's USB Legacy Support.)  Turn off all the R/WC bits too.
	 */
	pci_write_config_word(pdev, UHCI_USBLEGSUP, UHCI_USBLEGSUP_RWC);

	/* Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw(UHCI_USBCMD_HCRESET, base + UHCI_USBCMD);
	mb();
	udelay(5);
	if (inw(base + UHCI_USBCMD) & UHCI_USBCMD_HCRESET)
		dev_warn(&pdev->dev, "HCRESET not completed yet!\n");

	/* Just to be safe, disable interrupt requests and
	 * make sure the controller is stopped.
	 */
	outw(0, base + UHCI_USBINTR);
	outw(0, base + UHCI_USBCMD);
}
EXPORT_SYMBOL_GPL(uhci_reset_hc);

/*
 * Initialize a controller that was newly discovered or has just been
 * resumed.  In either case we can't be sure of its previous state.
 *
 * Returns: 1 if the controller was reset, 0 otherwise.
 */
int uhci_check_and_reset_hc(struct pci_dev *pdev, unsigned long base)
{
	u16 legsup;
	unsigned int cmd, intr;

	/*
	 * When restarting a suspended controller, we expect all the
	 * settings to be the same as we left them:
	 *
	 *	PIRQ and SMI disabled, no R/W bits set in USBLEGSUP;
	 *	Controller is stopped and configured with EGSM set;
	 *	No interrupts enabled except possibly Resume Detect.
	 *
	 * If any of these conditions are violated we do a complete reset.
	 */
	pci_read_config_word(pdev, UHCI_USBLEGSUP, &legsup);
	if (legsup & ~(UHCI_USBLEGSUP_RO | UHCI_USBLEGSUP_RWC)) {
		dev_dbg(&pdev->dev, "%s: legsup = 0x%04x\n",
				__FUNCTION__, legsup);
		goto reset_needed;
	}

	cmd = inw(base + UHCI_USBCMD);
	if ((cmd & UHCI_USBCMD_RUN) || !(cmd & UHCI_USBCMD_CONFIGURE) ||
			!(cmd & UHCI_USBCMD_EGSM)) {
		dev_dbg(&pdev->dev, "%s: cmd = 0x%04x\n",
				__FUNCTION__, cmd);
		goto reset_needed;
	}

	intr = inw(base + UHCI_USBINTR);
	if (intr & (~UHCI_USBINTR_RESUME)) {
		dev_dbg(&pdev->dev, "%s: intr = 0x%04x\n",
				__FUNCTION__, intr);
		goto reset_needed;
	}
	return 0;

reset_needed:
	dev_dbg(&pdev->dev, "Performing full reset\n");
	uhci_reset_hc(pdev, base);
	return 1;
}
EXPORT_SYMBOL_GPL(uhci_check_and_reset_hc);

static inline int io_type_enabled(struct pci_dev *pdev, unsigned int mask)
{
	u16 cmd;
	return !pci_read_config_word(pdev, PCI_COMMAND, &cmd) && (cmd & mask);
}

#define pio_enabled(dev) io_type_enabled(dev, PCI_COMMAND_IO)
#define mmio_enabled(dev) io_type_enabled(dev, PCI_COMMAND_MEMORY)

static void __devinit quirk_usb_handoff_uhci(struct pci_dev *pdev)
{
	unsigned long base = 0;
	int i;

	if (!pio_enabled(pdev))
		return;

	for (i = 0; i < PCI_ROM_RESOURCE; i++)
		if ((pci_resource_flags(pdev, i) & IORESOURCE_IO)) {
			base = pci_resource_start(pdev, i);
			break;
		}

	if (base)
		uhci_check_and_reset_hc(pdev, base);
}

static int __devinit mmio_resource_enabled(struct pci_dev *pdev, int idx)
{
	return pci_resource_start(pdev, idx) && mmio_enabled(pdev);
}

static void __devinit quirk_usb_handoff_ohci(struct pci_dev *pdev)
{
	void __iomem *base;
	int wait_time;
	u32 control;

	if (!mmio_resource_enabled(pdev, 0))
		return;

	base = ioremap_nocache(pci_resource_start(pdev, 0),
				     pci_resource_len(pdev, 0));
	if (base == NULL) return;

/* On PA-RISC, PDC can leave IR set incorrectly; ignore it there. */
#ifndef __hppa__
	control = readl(base + OHCI_CONTROL);
	if (control & OHCI_CTRL_IR) {
		wait_time = 500; /* arbitrary; 5 seconds */
		writel(OHCI_INTR_OC, base + OHCI_INTRENABLE);
		writel(OHCI_OCR, base + OHCI_CMDSTATUS);
		while (wait_time > 0 &&
				readl(base + OHCI_CONTROL) & OHCI_CTRL_IR) {
			wait_time -= 10;
			msleep(10);
		}
		if (wait_time <= 0)
			printk(KERN_WARNING "%s %s: early BIOS handoff "
					"failed (BIOS bug ?)\n",
					pdev->dev.bus_id, "OHCI");

		/* reset controller, preserving RWC */
		writel(control & OHCI_CTRL_RWC, base + OHCI_CONTROL);
	}
#endif

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

	if (!mmio_resource_enabled(pdev, 0))
		return;

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
				printk(KERN_WARNING "%s %s: early BIOS handoff "
						"failed (BIOS bug ?)\n",
					pdev->dev.bus_id, "EHCI");
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
	if (pdev->class == PCI_CLASS_SERIAL_USB_UHCI)
		quirk_usb_handoff_uhci(pdev);
	else if (pdev->class == PCI_CLASS_SERIAL_USB_OHCI)
		quirk_usb_handoff_ohci(pdev);
	else if (pdev->class == PCI_CLASS_SERIAL_USB_EHCI)
		quirk_usb_disable_ehci(pdev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, quirk_usb_early_handoff);
