/*
 * This file contains code to reset and initialize USB host controllers.
 * Some of it includes work-arounds for PCI hardware and BIOS quirks.
 * It may need to run early during booting -- before USB would normally
 * initialize -- to ensure that Linux doesn't use any legacy modes.
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 *  (and others)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include "pci-quirks.h"
#include "xhci-ext-caps.h"


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
#define OHCI_FMINTERVAL		0x34
#define OHCI_HCFS		(3 << 6)	/* hc functional state */
#define OHCI_HCR		(1 << 0)	/* host controller reset */
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
#define EHCI_CONFIGFLAG		0x40		/* configured flag register */
#define EHCI_USBLEGSUP		0		/* legacy support register */
#define EHCI_USBLEGSUP_BIOS	(1 << 16)	/* BIOS semaphore */
#define EHCI_USBLEGSUP_OS	(1 << 24)	/* OS semaphore */
#define EHCI_USBLEGCTLSTS	4		/* legacy control/status */
#define EHCI_USBLEGCTLSTS_SOOE	(1 << 13)	/* SMI on ownership change */

/* AMD quirk use */
#define	AB_REG_BAR_LOW		0xe0
#define	AB_REG_BAR_HIGH		0xe1
#define	AB_REG_BAR_SB700	0xf0
#define	AB_INDX(addr)		((addr) + 0x00)
#define	AB_DATA(addr)		((addr) + 0x04)
#define	AX_INDXC		0x30
#define	AX_DATAC		0x34

#define	NB_PCIE_INDX_ADDR	0xe0
#define	NB_PCIE_INDX_DATA	0xe4
#define	PCIE_P_CNTL		0x10040
#define	BIF_NB			0x10002
#define	NB_PIF0_PWRDOWN_0	0x01100012
#define	NB_PIF0_PWRDOWN_1	0x01100013

#define USB_INTEL_XUSB2PR      0xD0
#define USB_INTEL_USB3_PSSEN   0xD8

static struct amd_chipset_info {
	struct pci_dev	*nb_dev;
	struct pci_dev	*smbus_dev;
	int nb_type;
	int sb_type;
	int isoc_reqs;
	int probe_count;
	int probe_result;
} amd_chipset;

static DEFINE_SPINLOCK(amd_lock);

int usb_amd_find_chipset_info(void)
{
	u8 rev = 0;
	unsigned long flags;
	struct amd_chipset_info info;
	int ret;

	spin_lock_irqsave(&amd_lock, flags);

	/* probe only once */
	if (amd_chipset.probe_count > 0) {
		amd_chipset.probe_count++;
		spin_unlock_irqrestore(&amd_lock, flags);
		return amd_chipset.probe_result;
	}
	memset(&info, 0, sizeof(info));
	spin_unlock_irqrestore(&amd_lock, flags);

	info.smbus_dev = pci_get_device(PCI_VENDOR_ID_ATI, 0x4385, NULL);
	if (info.smbus_dev) {
		rev = info.smbus_dev->revision;
		if (rev >= 0x40)
			info.sb_type = 1;
		else if (rev >= 0x30 && rev <= 0x3b)
			info.sb_type = 3;
	} else {
		info.smbus_dev = pci_get_device(PCI_VENDOR_ID_AMD,
						0x780b, NULL);
		if (!info.smbus_dev) {
			ret = 0;
			goto commit;
		}

		rev = info.smbus_dev->revision;
		if (rev >= 0x11 && rev <= 0x18)
			info.sb_type = 2;
	}

	if (info.sb_type == 0) {
		if (info.smbus_dev) {
			pci_dev_put(info.smbus_dev);
			info.smbus_dev = NULL;
		}
		ret = 0;
		goto commit;
	}

	info.nb_dev = pci_get_device(PCI_VENDOR_ID_AMD, 0x9601, NULL);
	if (info.nb_dev) {
		info.nb_type = 1;
	} else {
		info.nb_dev = pci_get_device(PCI_VENDOR_ID_AMD, 0x1510, NULL);
		if (info.nb_dev) {
			info.nb_type = 2;
		} else {
			info.nb_dev = pci_get_device(PCI_VENDOR_ID_AMD,
						     0x9600, NULL);
			if (info.nb_dev)
				info.nb_type = 3;
		}
	}

	ret = info.probe_result = 1;
	printk(KERN_DEBUG "QUIRK: Enable AMD PLL fix\n");

commit:

	spin_lock_irqsave(&amd_lock, flags);
	if (amd_chipset.probe_count > 0) {
		/* race - someone else was faster - drop devices */

		/* Mark that we where here */
		amd_chipset.probe_count++;
		ret = amd_chipset.probe_result;

		spin_unlock_irqrestore(&amd_lock, flags);

		if (info.nb_dev)
			pci_dev_put(info.nb_dev);
		if (info.smbus_dev)
			pci_dev_put(info.smbus_dev);

	} else {
		/* no race - commit the result */
		info.probe_count++;
		amd_chipset = info;
		spin_unlock_irqrestore(&amd_lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(usb_amd_find_chipset_info);

/*
 * The hardware normally enables the A-link power management feature, which
 * lets the system lower the power consumption in idle states.
 *
 * This USB quirk prevents the link going into that lower power state
 * during isochronous transfers.
 *
 * Without this quirk, isochronous stream on OHCI/EHCI/xHCI controllers of
 * some AMD platforms may stutter or have breaks occasionally.
 */
static void usb_amd_quirk_pll(int disable)
{
	u32 addr, addr_low, addr_high, val;
	u32 bit = disable ? 0 : 1;
	unsigned long flags;

	spin_lock_irqsave(&amd_lock, flags);

	if (disable) {
		amd_chipset.isoc_reqs++;
		if (amd_chipset.isoc_reqs > 1) {
			spin_unlock_irqrestore(&amd_lock, flags);
			return;
		}
	} else {
		amd_chipset.isoc_reqs--;
		if (amd_chipset.isoc_reqs > 0) {
			spin_unlock_irqrestore(&amd_lock, flags);
			return;
		}
	}

	if (amd_chipset.sb_type == 1 || amd_chipset.sb_type == 2) {
		outb_p(AB_REG_BAR_LOW, 0xcd6);
		addr_low = inb_p(0xcd7);
		outb_p(AB_REG_BAR_HIGH, 0xcd6);
		addr_high = inb_p(0xcd7);
		addr = addr_high << 8 | addr_low;

		outl_p(0x30, AB_INDX(addr));
		outl_p(0x40, AB_DATA(addr));
		outl_p(0x34, AB_INDX(addr));
		val = inl_p(AB_DATA(addr));
	} else if (amd_chipset.sb_type == 3) {
		pci_read_config_dword(amd_chipset.smbus_dev,
					AB_REG_BAR_SB700, &addr);
		outl(AX_INDXC, AB_INDX(addr));
		outl(0x40, AB_DATA(addr));
		outl(AX_DATAC, AB_INDX(addr));
		val = inl(AB_DATA(addr));
	} else {
		spin_unlock_irqrestore(&amd_lock, flags);
		return;
	}

	if (disable) {
		val &= ~0x08;
		val |= (1 << 4) | (1 << 9);
	} else {
		val |= 0x08;
		val &= ~((1 << 4) | (1 << 9));
	}
	outl_p(val, AB_DATA(addr));

	if (!amd_chipset.nb_dev) {
		spin_unlock_irqrestore(&amd_lock, flags);
		return;
	}

	if (amd_chipset.nb_type == 1 || amd_chipset.nb_type == 3) {
		addr = PCIE_P_CNTL;
		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_ADDR, addr);
		pci_read_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, &val);

		val &= ~(1 | (1 << 3) | (1 << 4) | (1 << 9) | (1 << 12));
		val |= bit | (bit << 3) | (bit << 12);
		val |= ((!bit) << 4) | ((!bit) << 9);
		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, val);

		addr = BIF_NB;
		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_ADDR, addr);
		pci_read_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, &val);
		val &= ~(1 << 8);
		val |= bit << 8;

		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, val);
	} else if (amd_chipset.nb_type == 2) {
		addr = NB_PIF0_PWRDOWN_0;
		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_ADDR, addr);
		pci_read_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, &val);
		if (disable)
			val &= ~(0x3f << 7);
		else
			val |= 0x3f << 7;

		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, val);

		addr = NB_PIF0_PWRDOWN_1;
		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_ADDR, addr);
		pci_read_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, &val);
		if (disable)
			val &= ~(0x3f << 7);
		else
			val |= 0x3f << 7;

		pci_write_config_dword(amd_chipset.nb_dev,
					NB_PCIE_INDX_DATA, val);
	}

	spin_unlock_irqrestore(&amd_lock, flags);
	return;
}

void usb_amd_quirk_pll_disable(void)
{
	usb_amd_quirk_pll(1);
}
EXPORT_SYMBOL_GPL(usb_amd_quirk_pll_disable);

void usb_amd_quirk_pll_enable(void)
{
	usb_amd_quirk_pll(0);
}
EXPORT_SYMBOL_GPL(usb_amd_quirk_pll_enable);

void usb_amd_dev_put(void)
{
	struct pci_dev *nb, *smbus;
	unsigned long flags;

	spin_lock_irqsave(&amd_lock, flags);

	amd_chipset.probe_count--;
	if (amd_chipset.probe_count > 0) {
		spin_unlock_irqrestore(&amd_lock, flags);
		return;
	}

	/* save them to pci_dev_put outside of spinlock */
	nb    = amd_chipset.nb_dev;
	smbus = amd_chipset.smbus_dev;

	amd_chipset.nb_dev = NULL;
	amd_chipset.smbus_dev = NULL;
	amd_chipset.nb_type = 0;
	amd_chipset.sb_type = 0;
	amd_chipset.isoc_reqs = 0;
	amd_chipset.probe_result = 0;

	spin_unlock_irqrestore(&amd_lock, flags);

	if (nb)
		pci_dev_put(nb);
	if (smbus)
		pci_dev_put(smbus);
}
EXPORT_SYMBOL_GPL(usb_amd_dev_put);

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
				__func__, legsup);
		goto reset_needed;
	}

	cmd = inw(base + UHCI_USBCMD);
	if ((cmd & UHCI_USBCMD_RUN) || !(cmd & UHCI_USBCMD_CONFIGURE) ||
			!(cmd & UHCI_USBCMD_EGSM)) {
		dev_dbg(&pdev->dev, "%s: cmd = 0x%04x\n",
				__func__, cmd);
		goto reset_needed;
	}

	intr = inw(base + UHCI_USBINTR);
	if (intr & (~UHCI_USBINTR_RESUME)) {
		dev_dbg(&pdev->dev, "%s: intr = 0x%04x\n",
				__func__, intr);
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
	u32 control;
	u32 fminterval;
	int cnt;

	if (!mmio_resource_enabled(pdev, 0))
		return;

	base = pci_ioremap_bar(pdev, 0);
	if (base == NULL)
		return;

	control = readl(base + OHCI_CONTROL);

/* On PA-RISC, PDC can leave IR set incorrectly; ignore it there. */
#ifdef __hppa__
#define	OHCI_CTRL_MASK		(OHCI_CTRL_RWC | OHCI_CTRL_IR)
#else
#define	OHCI_CTRL_MASK		OHCI_CTRL_RWC

	if (control & OHCI_CTRL_IR) {
		int wait_time = 500; /* arbitrary; 5 seconds */
		writel(OHCI_INTR_OC, base + OHCI_INTRENABLE);
		writel(OHCI_OCR, base + OHCI_CMDSTATUS);
		while (wait_time > 0 &&
				readl(base + OHCI_CONTROL) & OHCI_CTRL_IR) {
			wait_time -= 10;
			msleep(10);
		}
		if (wait_time <= 0)
			dev_warn(&pdev->dev, "OHCI: BIOS handoff failed"
					" (BIOS bug?) %08x\n",
					readl(base + OHCI_CONTROL));
	}
#endif

	/* disable interrupts */
	writel((u32) ~0, base + OHCI_INTRDISABLE);

	/* Reset the USB bus, if the controller isn't already in RESET */
	if (control & OHCI_HCFS) {
		/* Go into RESET, preserving RWC (and possibly IR) */
		writel(control & OHCI_CTRL_MASK, base + OHCI_CONTROL);
		readl(base + OHCI_CONTROL);

		/* drive bus reset for at least 50 ms (7.1.7.5) */
		msleep(50);
	}

	/* software reset of the controller, preserving HcFmInterval */
	fminterval = readl(base + OHCI_FMINTERVAL);
	writel(OHCI_HCR, base + OHCI_CMDSTATUS);

	/* reset requires max 10 us delay */
	for (cnt = 30; cnt > 0; --cnt) {	/* ... allow extra time */
		if ((readl(base + OHCI_CMDSTATUS) & OHCI_HCR) == 0)
			break;
		udelay(1);
	}
	writel(fminterval, base + OHCI_FMINTERVAL);

	/* Now the controller is safely in SUSPEND and nothing can wake it up */
	iounmap(base);
}

static const struct dmi_system_id __devinitconst ehci_dmi_nohandoff_table[] = {
	{
		/*  Pegatron Lucid (ExoPC) */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "EXOPG06411"),
			DMI_MATCH(DMI_BIOS_VERSION, "Lucid-CE-133"),
		},
	},
	{
		/*  Pegatron Lucid (Ordissimo AIRIS) */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "M11JB"),
			DMI_MATCH(DMI_BIOS_VERSION, "Lucid-GE-133"),
		},
	},
	{ }
};

static void __devinit ehci_bios_handoff(struct pci_dev *pdev,
					void __iomem *op_reg_base,
					u32 cap, u8 offset)
{
	int try_handoff = 1, tried_handoff = 0;

	/* The Pegatron Lucid tablet sporadically waits for 98 seconds trying
	 * the handoff on its unused controller.  Skip it. */
	if (pdev->vendor == 0x8086 && pdev->device == 0x283a) {
		if (dmi_check_system(ehci_dmi_nohandoff_table))
			try_handoff = 0;
	}

	if (try_handoff && (cap & EHCI_USBLEGSUP_BIOS)) {
		dev_dbg(&pdev->dev, "EHCI: BIOS handoff\n");

#if 0
/* aleksey_gorelov@phoenix.com reports that some systems need SMI forced on,
 * but that seems dubious in general (the BIOS left it off intentionally)
 * and is known to prevent some systems from booting.  so we won't do this
 * unless maybe we can determine when we're on a system that needs SMI forced.
 */
		/* BIOS workaround (?): be sure the pre-Linux code
		 * receives the SMI
		 */
		pci_read_config_dword(pdev, offset + EHCI_USBLEGCTLSTS, &val);
		pci_write_config_dword(pdev, offset + EHCI_USBLEGCTLSTS,
				       val | EHCI_USBLEGCTLSTS_SOOE);
#endif

		/* some systems get upset if this semaphore is
		 * set for any other reason than forcing a BIOS
		 * handoff..
		 */
		pci_write_config_byte(pdev, offset + 3, 1);
	}

	/* if boot firmware now owns EHCI, spin till it hands it over. */
	if (try_handoff) {
		int msec = 1000;
		while ((cap & EHCI_USBLEGSUP_BIOS) && (msec > 0)) {
			tried_handoff = 1;
			msleep(10);
			msec -= 10;
			pci_read_config_dword(pdev, offset, &cap);
		}
	}

	if (cap & EHCI_USBLEGSUP_BIOS) {
		/* well, possibly buggy BIOS... try to shut it down,
		 * and hope nothing goes too wrong
		 */
		if (try_handoff)
			dev_warn(&pdev->dev, "EHCI: BIOS handoff failed"
				 " (BIOS bug?) %08x\n", cap);
		pci_write_config_byte(pdev, offset + 2, 0);
	}

	/* just in case, always disable EHCI SMIs */
	pci_write_config_dword(pdev, offset + EHCI_USBLEGCTLSTS, 0);

	/* If the BIOS ever owned the controller then we can't expect
	 * any power sessions to remain intact.
	 */
	if (tried_handoff)
		writel(0, op_reg_base + EHCI_CONFIGFLAG);
}

static void __devinit quirk_usb_disable_ehci(struct pci_dev *pdev)
{
	void __iomem *base, *op_reg_base;
	u32	hcc_params, cap, val;
	u8	offset, cap_length;
	int	wait_time, count = 256/4;

	if (!mmio_resource_enabled(pdev, 0))
		return;

	base = pci_ioremap_bar(pdev, 0);
	if (base == NULL)
		return;

	cap_length = readb(base);
	op_reg_base = base + cap_length;

	/* EHCI 0.96 and later may have "extended capabilities"
	 * spec section 5.1 explains the bios handoff, e.g. for
	 * booting from USB disk or using a usb keyboard
	 */
	hcc_params = readl(base + EHCI_HCC_PARAMS);
	offset = (hcc_params >> 8) & 0xff;
	while (offset && --count) {
		pci_read_config_dword(pdev, offset, &cap);

		switch (cap & 0xff) {
		case 1:
			ehci_bios_handoff(pdev, op_reg_base, cap, offset);
			break;
		case 0: /* Illegal reserved cap, set cap=0 so we exit */
			cap = 0; /* then fallthrough... */
		default:
			dev_warn(&pdev->dev, "EHCI: unrecognized capability "
				 "%02x\n", cap & 0xff);
		}
		offset = (cap >> 8) & 0xff;
	}
	if (!count)
		dev_printk(KERN_DEBUG, &pdev->dev, "EHCI: capability loop?\n");

	/*
	 * halt EHCI & disable its interrupts in any case
	 */
	val = readl(op_reg_base + EHCI_USBSTS);
	if ((val & EHCI_USBSTS_HALTED) == 0) {
		val = readl(op_reg_base + EHCI_USBCMD);
		val &= ~EHCI_USBCMD_RUN;
		writel(val, op_reg_base + EHCI_USBCMD);

		wait_time = 2000;
		do {
			writel(0x3f, op_reg_base + EHCI_USBSTS);
			udelay(100);
			wait_time -= 100;
			val = readl(op_reg_base + EHCI_USBSTS);
			if ((val == ~(u32)0) || (val & EHCI_USBSTS_HALTED)) {
				break;
			}
		} while (wait_time > 0);
	}
	writel(0, op_reg_base + EHCI_USBINTR);
	writel(0x3f, op_reg_base + EHCI_USBSTS);

	iounmap(base);
}

/*
 * handshake - spin reading a register until handshake completes
 * @ptr: address of hc register to be read
 * @mask: bits to look at in result of read
 * @done: value of those bits when handshake succeeds
 * @wait_usec: timeout in microseconds
 * @delay_usec: delay in microseconds to wait between polling
 *
 * Polls a register every delay_usec microseconds.
 * Returns 0 when the mask bits have the value done.
 * Returns -ETIMEDOUT if this condition is not true after
 * wait_usec microseconds have passed.
 */
static int handshake(void __iomem *ptr, u32 mask, u32 done,
		int wait_usec, int delay_usec)
{
	u32	result;

	do {
		result = readl(ptr);
		result &= mask;
		if (result == done)
			return 0;
		udelay(delay_usec);
		wait_usec -= delay_usec;
	} while (wait_usec > 0);
	return -ETIMEDOUT;
}

bool usb_is_intel_switchable_xhci(struct pci_dev *pdev)
{
	return pdev->class == PCI_CLASS_SERIAL_USB_XHCI &&
		pdev->vendor == PCI_VENDOR_ID_INTEL &&
		pdev->device == PCI_DEVICE_ID_INTEL_PANTHERPOINT_XHCI;
}
EXPORT_SYMBOL_GPL(usb_is_intel_switchable_xhci);

/*
 * Intel's Panther Point chipset has two host controllers (EHCI and xHCI) that
 * share some number of ports.  These ports can be switched between either
 * controller.  Not all of the ports under the EHCI host controller may be
 * switchable.
 *
 * The ports should be switched over to xHCI before PCI probes for any device
 * start.  This avoids active devices under EHCI being disconnected during the
 * port switchover, which could cause loss of data on USB storage devices, or
 * failed boot when the root file system is on a USB mass storage device and is
 * enumerated under EHCI first.
 *
 * We write into the xHC's PCI configuration space in some Intel-specific
 * registers to switch the ports over.  The USB 3.0 terminations and the USB
 * 2.0 data wires are switched separately.  We want to enable the SuperSpeed
 * terminations before switching the USB 2.0 wires over, so that USB 3.0
 * devices connect at SuperSpeed, rather than at USB 2.0 speeds.
 */
void usb_enable_xhci_ports(struct pci_dev *xhci_pdev)
{
	u32		ports_available;

	ports_available = 0xffffffff;
	/* Write USB3_PSSEN, the USB 3.0 Port SuperSpeed Enable
	 * Register, to turn on SuperSpeed terminations for all
	 * available ports.
	 */
	pci_write_config_dword(xhci_pdev, USB_INTEL_USB3_PSSEN,
			cpu_to_le32(ports_available));

	pci_read_config_dword(xhci_pdev, USB_INTEL_USB3_PSSEN,
			&ports_available);
	dev_dbg(&xhci_pdev->dev, "USB 3.0 ports that are now enabled "
			"under xHCI: 0x%x\n", ports_available);

	ports_available = 0xffffffff;
	/* Write XUSB2PR, the xHC USB 2.0 Port Routing Register, to
	 * switch the USB 2.0 power and data lines over to the xHCI
	 * host.
	 */
	pci_write_config_dword(xhci_pdev, USB_INTEL_XUSB2PR,
			cpu_to_le32(ports_available));

	pci_read_config_dword(xhci_pdev, USB_INTEL_XUSB2PR,
			&ports_available);
	dev_dbg(&xhci_pdev->dev, "USB 2.0 ports that are now switched over "
			"to xHCI: 0x%x\n", ports_available);
}
EXPORT_SYMBOL_GPL(usb_enable_xhci_ports);

/**
 * PCI Quirks for xHCI.
 *
 * Takes care of the handoff between the Pre-OS (i.e. BIOS) and the OS.
 * It signals to the BIOS that the OS wants control of the host controller,
 * and then waits 5 seconds for the BIOS to hand over control.
 * If we timeout, assume the BIOS is broken and take control anyway.
 */
static void __devinit quirk_usb_handoff_xhci(struct pci_dev *pdev)
{
	void __iomem *base;
	int ext_cap_offset;
	void __iomem *op_reg_base;
	u32 val;
	int timeout;

	if (!mmio_resource_enabled(pdev, 0))
		return;

	base = ioremap_nocache(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0));
	if (base == NULL)
		return;

	/*
	 * Find the Legacy Support Capability register -
	 * this is optional for xHCI host controllers.
	 */
	ext_cap_offset = xhci_find_next_cap_offset(base, XHCI_HCC_PARAMS_OFFSET);
	do {
		if (!ext_cap_offset)
			/* We've reached the end of the extended capabilities */
			goto hc_init;
		val = readl(base + ext_cap_offset);
		if (XHCI_EXT_CAPS_ID(val) == XHCI_EXT_CAPS_LEGACY)
			break;
		ext_cap_offset = xhci_find_next_cap_offset(base, ext_cap_offset);
	} while (1);

	/* If the BIOS owns the HC, signal that the OS wants it, and wait */
	if (val & XHCI_HC_BIOS_OWNED) {
		writel(val | XHCI_HC_OS_OWNED, base + ext_cap_offset);

		/* Wait for 5 seconds with 10 microsecond polling interval */
		timeout = handshake(base + ext_cap_offset, XHCI_HC_BIOS_OWNED,
				0, 5000, 10);

		/* Assume a buggy BIOS and take HC ownership anyway */
		if (timeout) {
			dev_warn(&pdev->dev, "xHCI BIOS handoff failed"
					" (BIOS bug ?) %08x\n", val);
			writel(val & ~XHCI_HC_BIOS_OWNED, base + ext_cap_offset);
		}
	}

	val = readl(base + ext_cap_offset + XHCI_LEGACY_CONTROL_OFFSET);
	/* Mask off (turn off) any enabled SMIs */
	val &= XHCI_LEGACY_DISABLE_SMI;
	/* Mask all SMI events bits, RW1C */
	val |= XHCI_LEGACY_SMI_EVENTS;
	/* Disable any BIOS SMIs and clear all SMI events*/
	writel(val, base + ext_cap_offset + XHCI_LEGACY_CONTROL_OFFSET);

	if (usb_is_intel_switchable_xhci(pdev))
		usb_enable_xhci_ports(pdev);
hc_init:
	op_reg_base = base + XHCI_HC_LENGTH(readl(base));

	/* Wait for the host controller to be ready before writing any
	 * operational or runtime registers.  Wait 5 seconds and no more.
	 */
	timeout = handshake(op_reg_base + XHCI_STS_OFFSET, XHCI_STS_CNR, 0,
			5000, 10);
	/* Assume a buggy HC and start HC initialization anyway */
	if (timeout) {
		val = readl(op_reg_base + XHCI_STS_OFFSET);
		dev_warn(&pdev->dev,
				"xHCI HW not ready after 5 sec (HC bug?) "
				"status = 0x%x\n", val);
	}

	/* Send the halt and disable interrupts command */
	val = readl(op_reg_base + XHCI_CMD_OFFSET);
	val &= ~(XHCI_CMD_RUN | XHCI_IRQS);
	writel(val, op_reg_base + XHCI_CMD_OFFSET);

	/* Wait for the HC to halt - poll every 125 usec (one microframe). */
	timeout = handshake(op_reg_base + XHCI_STS_OFFSET, XHCI_STS_HALT, 1,
			XHCI_MAX_HALT_USEC, 125);
	if (timeout) {
		val = readl(op_reg_base + XHCI_STS_OFFSET);
		dev_warn(&pdev->dev,
				"xHCI HW did not halt within %d usec "
				"status = 0x%x\n", XHCI_MAX_HALT_USEC, val);
	}

	iounmap(base);
}

static void __devinit quirk_usb_early_handoff(struct pci_dev *pdev)
{
	/* Skip Netlogic mips SoC's internal PCI USB controller.
	 * This device does not need/support EHCI/OHCI handoff
	 */
	if (pdev->vendor == 0x184e)	/* vendor Netlogic */
		return;
	if (pdev->class != PCI_CLASS_SERIAL_USB_UHCI &&
			pdev->class != PCI_CLASS_SERIAL_USB_OHCI &&
			pdev->class != PCI_CLASS_SERIAL_USB_EHCI &&
			pdev->class != PCI_CLASS_SERIAL_USB_XHCI)
		return;

	if (pci_enable_device(pdev) < 0) {
		dev_warn(&pdev->dev, "Can't enable PCI device, "
				"BIOS handoff failed.\n");
		return;
	}
	if (pdev->class == PCI_CLASS_SERIAL_USB_UHCI)
		quirk_usb_handoff_uhci(pdev);
	else if (pdev->class == PCI_CLASS_SERIAL_USB_OHCI)
		quirk_usb_handoff_ohci(pdev);
	else if (pdev->class == PCI_CLASS_SERIAL_USB_EHCI)
		quirk_usb_disable_ehci(pdev);
	else if (pdev->class == PCI_CLASS_SERIAL_USB_XHCI)
		quirk_usb_handoff_xhci(pdev);
	pci_disable_device(pdev);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_ANY_ID, PCI_ANY_ID,
			PCI_CLASS_SERIAL_USB, 8, quirk_usb_early_handoff);
