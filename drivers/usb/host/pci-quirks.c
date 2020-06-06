// SPDX-License-Identifier: GPL-2.0
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
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#include <soc/bcm2835/raspberrypi-firmware.h>

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

#define PT_ADDR_INDX		0xE8
#define PT_READ_INDX		0xE4
#define PT_SIG_1_ADDR		0xA520
#define PT_SIG_2_ADDR		0xA521
#define PT_SIG_3_ADDR		0xA522
#define PT_SIG_4_ADDR		0xA523
#define PT_SIG_1_DATA		0x78
#define PT_SIG_2_DATA		0x56
#define PT_SIG_3_DATA		0x34
#define PT_SIG_4_DATA		0x12
#define PT4_P1_REG		0xB521
#define PT4_P2_REG		0xB522
#define PT2_P1_REG		0xD520
#define PT2_P2_REG		0xD521
#define PT1_P1_REG		0xD522
#define PT1_P2_REG		0xD523

#define	NB_PCIE_INDX_ADDR	0xe0
#define	NB_PCIE_INDX_DATA	0xe4
#define	PCIE_P_CNTL		0x10040
#define	BIF_NB			0x10002
#define	NB_PIF0_PWRDOWN_0	0x01100012
#define	NB_PIF0_PWRDOWN_1	0x01100013

#define USB_INTEL_XUSB2PR      0xD0
#define USB_INTEL_USB2PRM      0xD4
#define USB_INTEL_USB3_PSSEN   0xD8
#define USB_INTEL_USB3PRM      0xDC

/* ASMEDIA quirk use */
#define ASMT_DATA_WRITE0_REG	0xF8
#define ASMT_DATA_WRITE1_REG	0xFC
#define ASMT_CONTROL_REG	0xE0
#define ASMT_CONTROL_WRITE_BIT	0x02
#define ASMT_WRITEREG_CMD	0x10423
#define ASMT_FLOWCTL_ADDR	0xFA30
#define ASMT_FLOWCTL_DATA	0xBA
#define ASMT_PSEUDO_DATA	0

/*
 * amd_chipset_gen values represent AMD different chipset generations
 */
enum amd_chipset_gen {
	NOT_AMD_CHIPSET = 0,
	AMD_CHIPSET_SB600,
	AMD_CHIPSET_SB700,
	AMD_CHIPSET_SB800,
	AMD_CHIPSET_HUDSON2,
	AMD_CHIPSET_BOLTON,
	AMD_CHIPSET_YANGTZE,
	AMD_CHIPSET_TAISHAN,
	AMD_CHIPSET_UNKNOWN,
};

struct amd_chipset_type {
	enum amd_chipset_gen gen;
	u8 rev;
};

static struct amd_chipset_info {
	struct pci_dev	*nb_dev;
	struct pci_dev	*smbus_dev;
	int nb_type;
	struct amd_chipset_type sb_type;
	int isoc_reqs;
	int probe_count;
	bool need_pll_quirk;
} amd_chipset;

static DEFINE_SPINLOCK(amd_lock);

/*
 * amd_chipset_sb_type_init - initialize amd chipset southbridge type
 *
 * AMD FCH/SB generation and revision is identified by SMBus controller
 * vendor, device and revision IDs.
 *
 * Returns: 1 if it is an AMD chipset, 0 otherwise.
 */
static int amd_chipset_sb_type_init(struct amd_chipset_info *pinfo)
{
	u8 rev = 0;
	pinfo->sb_type.gen = AMD_CHIPSET_UNKNOWN;

	pinfo->smbus_dev = pci_get_device(PCI_VENDOR_ID_ATI,
			PCI_DEVICE_ID_ATI_SBX00_SMBUS, NULL);
	if (pinfo->smbus_dev) {
		rev = pinfo->smbus_dev->revision;
		if (rev >= 0x10 && rev <= 0x1f)
			pinfo->sb_type.gen = AMD_CHIPSET_SB600;
		else if (rev >= 0x30 && rev <= 0x3f)
			pinfo->sb_type.gen = AMD_CHIPSET_SB700;
		else if (rev >= 0x40 && rev <= 0x4f)
			pinfo->sb_type.gen = AMD_CHIPSET_SB800;
	} else {
		pinfo->smbus_dev = pci_get_device(PCI_VENDOR_ID_AMD,
				PCI_DEVICE_ID_AMD_HUDSON2_SMBUS, NULL);

		if (pinfo->smbus_dev) {
			rev = pinfo->smbus_dev->revision;
			if (rev >= 0x11 && rev <= 0x14)
				pinfo->sb_type.gen = AMD_CHIPSET_HUDSON2;
			else if (rev >= 0x15 && rev <= 0x18)
				pinfo->sb_type.gen = AMD_CHIPSET_BOLTON;
			else if (rev >= 0x39 && rev <= 0x3a)
				pinfo->sb_type.gen = AMD_CHIPSET_YANGTZE;
		} else {
			pinfo->smbus_dev = pci_get_device(PCI_VENDOR_ID_AMD,
							  0x145c, NULL);
			if (pinfo->smbus_dev) {
				rev = pinfo->smbus_dev->revision;
				pinfo->sb_type.gen = AMD_CHIPSET_TAISHAN;
			} else {
				pinfo->sb_type.gen = NOT_AMD_CHIPSET;
				return 0;
			}
		}
	}
	pinfo->sb_type.rev = rev;
	return 1;
}

void sb800_prefetch(struct device *dev, int on)
{
	u16 misc;
	struct pci_dev *pdev = to_pci_dev(dev);

	pci_read_config_word(pdev, 0x50, &misc);
	if (on == 0)
		pci_write_config_word(pdev, 0x50, misc & 0xfcff);
	else
		pci_write_config_word(pdev, 0x50, misc | 0x0300);
}
EXPORT_SYMBOL_GPL(sb800_prefetch);

static void usb_amd_find_chipset_info(void)
{
	unsigned long flags;
	struct amd_chipset_info info;
	info.need_pll_quirk = 0;

	spin_lock_irqsave(&amd_lock, flags);

	/* probe only once */
	if (amd_chipset.probe_count > 0) {
		amd_chipset.probe_count++;
		spin_unlock_irqrestore(&amd_lock, flags);
		return;
	}
	memset(&info, 0, sizeof(info));
	spin_unlock_irqrestore(&amd_lock, flags);

	if (!amd_chipset_sb_type_init(&info)) {
		goto commit;
	}

	switch (info.sb_type.gen) {
	case AMD_CHIPSET_SB700:
		info.need_pll_quirk = info.sb_type.rev <= 0x3B;
		break;
	case AMD_CHIPSET_SB800:
	case AMD_CHIPSET_HUDSON2:
	case AMD_CHIPSET_BOLTON:
		info.need_pll_quirk = 1;
		break;
	default:
		info.need_pll_quirk = 0;
		break;
	}

	if (!info.need_pll_quirk) {
		if (info.smbus_dev) {
			pci_dev_put(info.smbus_dev);
			info.smbus_dev = NULL;
		}
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

	printk(KERN_DEBUG "QUIRK: Enable AMD PLL fix\n");

commit:

	spin_lock_irqsave(&amd_lock, flags);
	if (amd_chipset.probe_count > 0) {
		/* race - someone else was faster - drop devices */

		/* Mark that we where here */
		amd_chipset.probe_count++;

		spin_unlock_irqrestore(&amd_lock, flags);

		pci_dev_put(info.nb_dev);
		pci_dev_put(info.smbus_dev);

	} else {
		/* no race - commit the result */
		info.probe_count++;
		amd_chipset = info;
		spin_unlock_irqrestore(&amd_lock, flags);
	}
}

int usb_hcd_amd_remote_wakeup_quirk(struct pci_dev *pdev)
{
	/* Make sure amd chipset type has already been initialized */
	usb_amd_find_chipset_info();
	if (amd_chipset.sb_type.gen == AMD_CHIPSET_YANGTZE ||
	    amd_chipset.sb_type.gen == AMD_CHIPSET_TAISHAN) {
		dev_dbg(&pdev->dev, "QUIRK: Enable AMD remote wakeup fix\n");
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(usb_hcd_amd_remote_wakeup_quirk);

bool usb_amd_hang_symptom_quirk(void)
{
	u8 rev;

	usb_amd_find_chipset_info();
	rev = amd_chipset.sb_type.rev;
	/* SB600 and old version of SB700 have hang symptom bug */
	return amd_chipset.sb_type.gen == AMD_CHIPSET_SB600 ||
			(amd_chipset.sb_type.gen == AMD_CHIPSET_SB700 &&
			 rev >= 0x3a && rev <= 0x3b);
}
EXPORT_SYMBOL_GPL(usb_amd_hang_symptom_quirk);

bool usb_amd_prefetch_quirk(void)
{
	usb_amd_find_chipset_info();
	/* SB800 needs pre-fetch fix */
	return amd_chipset.sb_type.gen == AMD_CHIPSET_SB800;
}
EXPORT_SYMBOL_GPL(usb_amd_prefetch_quirk);

bool usb_amd_quirk_pll_check(void)
{
	usb_amd_find_chipset_info();
	return amd_chipset.need_pll_quirk;
}
EXPORT_SYMBOL_GPL(usb_amd_quirk_pll_check);

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

	if (amd_chipset.sb_type.gen == AMD_CHIPSET_SB800 ||
			amd_chipset.sb_type.gen == AMD_CHIPSET_HUDSON2 ||
			amd_chipset.sb_type.gen == AMD_CHIPSET_BOLTON) {
		outb_p(AB_REG_BAR_LOW, 0xcd6);
		addr_low = inb_p(0xcd7);
		outb_p(AB_REG_BAR_HIGH, 0xcd6);
		addr_high = inb_p(0xcd7);
		addr = addr_high << 8 | addr_low;

		outl_p(0x30, AB_INDX(addr));
		outl_p(0x40, AB_DATA(addr));
		outl_p(0x34, AB_INDX(addr));
		val = inl_p(AB_DATA(addr));
	} else if (amd_chipset.sb_type.gen == AMD_CHIPSET_SB700 &&
			amd_chipset.sb_type.rev <= 0x3b) {
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

static int usb_asmedia_wait_write(struct pci_dev *pdev)
{
	unsigned long retry_count;
	unsigned char value;

	for (retry_count = 1000; retry_count > 0; --retry_count) {

		pci_read_config_byte(pdev, ASMT_CONTROL_REG, &value);

		if (value == 0xff) {
			dev_err(&pdev->dev, "%s: check_ready ERROR", __func__);
			return -EIO;
		}

		if ((value & ASMT_CONTROL_WRITE_BIT) == 0)
			return 0;

		udelay(50);
	}

	dev_warn(&pdev->dev, "%s: check_write_ready timeout", __func__);
	return -ETIMEDOUT;
}

void usb_asmedia_modifyflowcontrol(struct pci_dev *pdev)
{
	if (usb_asmedia_wait_write(pdev) != 0)
		return;

	/* send command and address to device */
	pci_write_config_dword(pdev, ASMT_DATA_WRITE0_REG, ASMT_WRITEREG_CMD);
	pci_write_config_dword(pdev, ASMT_DATA_WRITE1_REG, ASMT_FLOWCTL_ADDR);
	pci_write_config_byte(pdev, ASMT_CONTROL_REG, ASMT_CONTROL_WRITE_BIT);

	if (usb_asmedia_wait_write(pdev) != 0)
		return;

	/* send data to device */
	pci_write_config_dword(pdev, ASMT_DATA_WRITE0_REG, ASMT_FLOWCTL_DATA);
	pci_write_config_dword(pdev, ASMT_DATA_WRITE1_REG, ASMT_PSEUDO_DATA);
	pci_write_config_byte(pdev, ASMT_CONTROL_REG, ASMT_CONTROL_WRITE_BIT);
}
EXPORT_SYMBOL_GPL(usb_asmedia_modifyflowcontrol);

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
	memset(&amd_chipset.sb_type, 0, sizeof(amd_chipset.sb_type));
	amd_chipset.isoc_reqs = 0;
	amd_chipset.need_pll_quirk = 0;

	spin_unlock_irqrestore(&amd_lock, flags);

	pci_dev_put(nb);
	pci_dev_put(smbus);
}
EXPORT_SYMBOL_GPL(usb_amd_dev_put);

/*
 * Check if port is disabled in BIOS on AMD Promontory host.
 * BIOS Disabled ports may wake on connect/disconnect and need
 * driver workaround to keep them disabled.
 * Returns true if port is marked disabled.
 */
bool usb_amd_pt_check_port(struct device *device, int port)
{
	unsigned char value, port_shift;
	struct pci_dev *pdev;
	u16 reg;

	pdev = to_pci_dev(device);
	pci_write_config_word(pdev, PT_ADDR_INDX, PT_SIG_1_ADDR);

	pci_read_config_byte(pdev, PT_READ_INDX, &value);
	if (value != PT_SIG_1_DATA)
		return false;

	pci_write_config_word(pdev, PT_ADDR_INDX, PT_SIG_2_ADDR);

	pci_read_config_byte(pdev, PT_READ_INDX, &value);
	if (value != PT_SIG_2_DATA)
		return false;

	pci_write_config_word(pdev, PT_ADDR_INDX, PT_SIG_3_ADDR);

	pci_read_config_byte(pdev, PT_READ_INDX, &value);
	if (value != PT_SIG_3_DATA)
		return false;

	pci_write_config_word(pdev, PT_ADDR_INDX, PT_SIG_4_ADDR);

	pci_read_config_byte(pdev, PT_READ_INDX, &value);
	if (value != PT_SIG_4_DATA)
		return false;

	/* Check disabled port setting, if bit is set port is enabled */
	switch (pdev->device) {
	case 0x43b9:
	case 0x43ba:
	/*
	 * device is AMD_PROMONTORYA_4(0x43b9) or PROMONTORYA_3(0x43ba)
	 * PT4_P1_REG bits[7..1] represents USB2.0 ports 6 to 0
	 * PT4_P2_REG bits[6..0] represents ports 13 to 7
	 */
		if (port > 6) {
			reg = PT4_P2_REG;
			port_shift = port - 7;
		} else {
			reg = PT4_P1_REG;
			port_shift = port + 1;
		}
		break;
	case 0x43bb:
	/*
	 * device is AMD_PROMONTORYA_2(0x43bb)
	 * PT2_P1_REG bits[7..5] represents USB2.0 ports 2 to 0
	 * PT2_P2_REG bits[5..0] represents ports 9 to 3
	 */
		if (port > 2) {
			reg = PT2_P2_REG;
			port_shift = port - 3;
		} else {
			reg = PT2_P1_REG;
			port_shift = port + 5;
		}
		break;
	case 0x43bc:
	/*
	 * device is AMD_PROMONTORYA_1(0x43bc)
	 * PT1_P1_REG[7..4] represents USB2.0 ports 3 to 0
	 * PT1_P2_REG[5..0] represents ports 9 to 4
	 */
		if (port > 3) {
			reg = PT1_P2_REG;
			port_shift = port - 4;
		} else {
			reg = PT1_P1_REG;
			port_shift = port + 4;
		}
		break;
	default:
		return false;
	}
	pci_write_config_word(pdev, PT_ADDR_INDX, reg);
	pci_read_config_byte(pdev, PT_READ_INDX, &value);

	return !(value & BIT(port_shift));
}
EXPORT_SYMBOL_GPL(usb_amd_pt_check_port);

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

static void quirk_usb_handoff_uhci(struct pci_dev *pdev)
{
	unsigned long base = 0;
	int i;

	if (!pio_enabled(pdev))
		return;

	for (i = 0; i < PCI_STD_NUM_BARS; i++)
		if ((pci_resource_flags(pdev, i) & IORESOURCE_IO)) {
			base = pci_resource_start(pdev, i);
			break;
		}

	if (base)
		uhci_check_and_reset_hc(pdev, base);
}

static int mmio_resource_enabled(struct pci_dev *pdev, int idx)
{
	return pci_resource_start(pdev, idx) && mmio_enabled(pdev);
}

static void quirk_usb_handoff_ohci(struct pci_dev *pdev)
{
	void __iomem *base;
	u32 control;
	u32 fminterval = 0;
	bool no_fminterval = false;
	int cnt;

	if (!mmio_resource_enabled(pdev, 0))
		return;

	base = pci_ioremap_bar(pdev, 0);
	if (base == NULL)
		return;

	/*
	 * ULi M5237 OHCI controller locks the whole system when accessing
	 * the OHCI_FMINTERVAL offset.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_AL && pdev->device == 0x5237)
		no_fminterval = true;

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
			dev_warn(&pdev->dev,
				 "OHCI: BIOS handoff failed (BIOS bug?) %08x\n",
				 readl(base + OHCI_CONTROL));
	}
#endif

	/* disable interrupts */
	writel((u32) ~0, base + OHCI_INTRDISABLE);

	/* Go into the USB_RESET state, preserving RWC (and possibly IR) */
	writel(control & OHCI_CTRL_MASK, base + OHCI_CONTROL);
	readl(base + OHCI_CONTROL);

	/* software reset of the controller, preserving HcFmInterval */
	if (!no_fminterval)
		fminterval = readl(base + OHCI_FMINTERVAL);

	writel(OHCI_HCR, base + OHCI_CMDSTATUS);

	/* reset requires max 10 us delay */
	for (cnt = 30; cnt > 0; --cnt) {	/* ... allow extra time */
		if ((readl(base + OHCI_CMDSTATUS) & OHCI_HCR) == 0)
			break;
		udelay(1);
	}

	if (!no_fminterval)
		writel(fminterval, base + OHCI_FMINTERVAL);

	/* Now the controller is safely in SUSPEND and nothing can wake it up */
	iounmap(base);
}

static const struct dmi_system_id ehci_dmi_nohandoff_table[] = {
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
			DMI_MATCH(DMI_BIOS_VERSION, "Lucid-"),
		},
	},
	{
		/*  Pegatron Lucid (Ordissimo) */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "Ordissimo"),
			DMI_MATCH(DMI_BIOS_VERSION, "Lucid-"),
		},
	},
	{
		/* HASEE E200 */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "HASEE"),
			DMI_MATCH(DMI_BOARD_NAME, "E210"),
			DMI_MATCH(DMI_BIOS_VERSION, "6.00"),
		},
	},
	{ }
};

static void ehci_bios_handoff(struct pci_dev *pdev,
					void __iomem *op_reg_base,
					u32 cap, u8 offset)
{
	int try_handoff = 1, tried_handoff = 0;

	/*
	 * The Pegatron Lucid tablet sporadically waits for 98 seconds trying
	 * the handoff on its unused controller.  Skip it.
	 *
	 * The HASEE E200 hangs when the semaphore is set (bugzilla #77021).
	 */
	if (pdev->vendor == 0x8086 && (pdev->device == 0x283a ||
			pdev->device == 0x27cc)) {
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
			dev_warn(&pdev->dev,
				 "EHCI: BIOS handoff failed (BIOS bug?) %08x\n",
				 cap);
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

static void quirk_usb_disable_ehci(struct pci_dev *pdev)
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
			cap = 0; /* fall through */
		default:
			dev_warn(&pdev->dev,
				 "EHCI: unrecognized capability %02x\n",
				 cap & 0xff);
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
void usb_enable_intel_xhci_ports(struct pci_dev *xhci_pdev)
{
	u32		ports_available;
	bool		ehci_found = false;
	struct pci_dev	*companion = NULL;

	/* Sony VAIO t-series with subsystem device ID 90a8 is not capable of
	 * switching ports from EHCI to xHCI
	 */
	if (xhci_pdev->subsystem_vendor == PCI_VENDOR_ID_SONY &&
	    xhci_pdev->subsystem_device == 0x90a8)
		return;

	/* make sure an intel EHCI controller exists */
	for_each_pci_dev(companion) {
		if (companion->class == PCI_CLASS_SERIAL_USB_EHCI &&
		    companion->vendor == PCI_VENDOR_ID_INTEL) {
			ehci_found = true;
			break;
		}
	}

	if (!ehci_found)
		return;

	/* Don't switchover the ports if the user hasn't compiled the xHCI
	 * driver.  Otherwise they will see "dead" USB ports that don't power
	 * the devices.
	 */
	if (!IS_ENABLED(CONFIG_USB_XHCI_HCD)) {
		dev_warn(&xhci_pdev->dev,
			 "CONFIG_USB_XHCI_HCD is turned off, defaulting to EHCI.\n");
		dev_warn(&xhci_pdev->dev,
				"USB 3.0 devices will work at USB 2.0 speeds.\n");
		usb_disable_xhci_ports(xhci_pdev);
		return;
	}

	/* Read USB3PRM, the USB 3.0 Port Routing Mask Register
	 * Indicate the ports that can be changed from OS.
	 */
	pci_read_config_dword(xhci_pdev, USB_INTEL_USB3PRM,
			&ports_available);

	dev_dbg(&xhci_pdev->dev, "Configurable ports to enable SuperSpeed: 0x%x\n",
			ports_available);

	/* Write USB3_PSSEN, the USB 3.0 Port SuperSpeed Enable
	 * Register, to turn on SuperSpeed terminations for the
	 * switchable ports.
	 */
	pci_write_config_dword(xhci_pdev, USB_INTEL_USB3_PSSEN,
			ports_available);

	pci_read_config_dword(xhci_pdev, USB_INTEL_USB3_PSSEN,
			&ports_available);
	dev_dbg(&xhci_pdev->dev,
		"USB 3.0 ports that are now enabled under xHCI: 0x%x\n",
		ports_available);

	/* Read XUSB2PRM, xHCI USB 2.0 Port Routing Mask Register
	 * Indicate the USB 2.0 ports to be controlled by the xHCI host.
	 */

	pci_read_config_dword(xhci_pdev, USB_INTEL_USB2PRM,
			&ports_available);

	dev_dbg(&xhci_pdev->dev, "Configurable USB 2.0 ports to hand over to xCHI: 0x%x\n",
			ports_available);

	/* Write XUSB2PR, the xHC USB 2.0 Port Routing Register, to
	 * switch the USB 2.0 power and data lines over to the xHCI
	 * host.
	 */
	pci_write_config_dword(xhci_pdev, USB_INTEL_XUSB2PR,
			ports_available);

	pci_read_config_dword(xhci_pdev, USB_INTEL_XUSB2PR,
			&ports_available);
	dev_dbg(&xhci_pdev->dev,
		"USB 2.0 ports that are now switched over to xHCI: 0x%x\n",
		ports_available);
}
EXPORT_SYMBOL_GPL(usb_enable_intel_xhci_ports);

void usb_disable_xhci_ports(struct pci_dev *xhci_pdev)
{
	pci_write_config_dword(xhci_pdev, USB_INTEL_USB3_PSSEN, 0x0);
	pci_write_config_dword(xhci_pdev, USB_INTEL_XUSB2PR, 0x0);
}
EXPORT_SYMBOL_GPL(usb_disable_xhci_ports);

/**
 * PCI Quirks for xHCI.
 *
 * Takes care of the handoff between the Pre-OS (i.e. BIOS) and the OS.
 * It signals to the BIOS that the OS wants control of the host controller,
 * and then waits 1 second for the BIOS to hand over control.
 * If we timeout, assume the BIOS is broken and take control anyway.
 */
static void quirk_usb_handoff_xhci(struct pci_dev *pdev)
{
	void __iomem *base;
	int ext_cap_offset;
	void __iomem *op_reg_base;
	u32 val;
	int timeout;
	int len = pci_resource_len(pdev, 0);

	if (!mmio_resource_enabled(pdev, 0))
		return;

	base = ioremap(pci_resource_start(pdev, 0), len);
	if (base == NULL)
		return;

	/*
	 * Find the Legacy Support Capability register -
	 * this is optional for xHCI host controllers.
	 */
	ext_cap_offset = xhci_find_next_ext_cap(base, 0, XHCI_EXT_CAPS_LEGACY);

	if (!ext_cap_offset)
		goto hc_init;

	if ((ext_cap_offset + sizeof(val)) > len) {
		/* We're reading garbage from the controller */
		dev_warn(&pdev->dev, "xHCI controller failing to respond");
		goto iounmap;
	}
	val = readl(base + ext_cap_offset);

	/* Auto handoff never worked for these devices. Force it and continue */
	if ((pdev->vendor == PCI_VENDOR_ID_TI && pdev->device == 0x8241) ||
			(pdev->vendor == PCI_VENDOR_ID_RENESAS
			 && pdev->device == 0x0014)) {
		val = (val | XHCI_HC_OS_OWNED) & ~XHCI_HC_BIOS_OWNED;
		writel(val, base + ext_cap_offset);
	}

	/* If the BIOS owns the HC, signal that the OS wants it, and wait */
	if (val & XHCI_HC_BIOS_OWNED) {
		writel(val | XHCI_HC_OS_OWNED, base + ext_cap_offset);

		/* Wait for 1 second with 10 microsecond polling interval */
		timeout = handshake(base + ext_cap_offset, XHCI_HC_BIOS_OWNED,
				0, 1000000, 10);

		/* Assume a buggy BIOS and take HC ownership anyway */
		if (timeout) {
			dev_warn(&pdev->dev,
				 "xHCI BIOS handoff failed (BIOS bug ?) %08x\n",
				 val);
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

hc_init:
	if (pdev->vendor == PCI_VENDOR_ID_INTEL)
		usb_enable_intel_xhci_ports(pdev);

	op_reg_base = base + XHCI_HC_LENGTH(readl(base));

	/* Wait for the host controller to be ready before writing any
	 * operational or runtime registers.  Wait 5 seconds and no more.
	 */
	timeout = handshake(op_reg_base + XHCI_STS_OFFSET, XHCI_STS_CNR, 0,
			5000000, 10);
	/* Assume a buggy HC and start HC initialization anyway */
	if (timeout) {
		val = readl(op_reg_base + XHCI_STS_OFFSET);
		dev_warn(&pdev->dev,
			 "xHCI HW not ready after 5 sec (HC bug?) status = 0x%x\n",
			 val);
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
			 "xHCI HW did not halt within %d usec status = 0x%x\n",
			 XHCI_MAX_HALT_USEC, val);
	}

iounmap:
	iounmap(base);
}

static void quirk_usb_early_handoff(struct pci_dev *pdev)
{
	int ret;

	/* Skip Netlogic mips SoC's internal PCI USB controller.
	 * This device does not need/support EHCI/OHCI handoff
	 */
	if (pdev->vendor == 0x184e)	/* vendor Netlogic */
		return;

	if (pdev->vendor == PCI_VENDOR_ID_VIA && pdev->device == 0x3483) {
		ret = rpi_firmware_init_vl805(pdev);
		if (ret) {
			/* Firmware might be outdated, or something failed */
			dev_warn(&pdev->dev,
				 "Failed to load VL805's firmware: %d. Will continue to attempt to work, but bad things might happen. You should fix this...\n",
				 ret);
		}
	}

	if (pdev->class != PCI_CLASS_SERIAL_USB_UHCI &&
			pdev->class != PCI_CLASS_SERIAL_USB_OHCI &&
			pdev->class != PCI_CLASS_SERIAL_USB_EHCI &&
			pdev->class != PCI_CLASS_SERIAL_USB_XHCI)
		return;

	if (pci_enable_device(pdev) < 0) {
		dev_warn(&pdev->dev,
			 "Can't enable PCI device, BIOS handoff failed.\n");
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
