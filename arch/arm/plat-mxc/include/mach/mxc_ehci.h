#ifndef __INCLUDE_ASM_ARCH_MXC_EHCI_H
#define __INCLUDE_ASM_ARCH_MXC_EHCI_H

/* values for portsc field */
#define MXC_EHCI_PHY_LOW_POWER_SUSPEND	(1 << 23)
#define MXC_EHCI_FORCE_FS		(1 << 24)
#define MXC_EHCI_UTMI_8BIT		(0 << 28)
#define MXC_EHCI_UTMI_16BIT		(1 << 28)
#define MXC_EHCI_SERIAL			(1 << 29)
#define MXC_EHCI_MODE_UTMI		(0 << 30)
#define MXC_EHCI_MODE_PHILIPS		(1 << 30)
#define MXC_EHCI_MODE_ULPI		(2 << 30)
#define MXC_EHCI_MODE_SERIAL		(3 << 30)

/* values for flags field */
#define MXC_EHCI_INTERFACE_DIFF_UNI	(0 << 0)
#define MXC_EHCI_INTERFACE_DIFF_BI	(1 << 0)
#define MXC_EHCI_INTERFACE_SINGLE_UNI	(2 << 0)
#define MXC_EHCI_INTERFACE_SINGLE_BI	(3 << 0)
#define MXC_EHCI_INTERFACE_MASK		(0xf)

#define MXC_EHCI_POWER_PINS_ENABLED	(1 << 5)
#define MXC_EHCI_TTL_ENABLED		(1 << 6)

#define MXC_EHCI_INTERNAL_PHY		(1 << 7)
#define MXC_EHCI_IPPUE_DOWN		(1 << 8)
#define MXC_EHCI_IPPUE_UP		(1 << 9)
#define MXC_EHCI_WAKEUP_ENABLED	(1 << 10)
#define MXC_EHCI_ITC_NO_THRESHOLD	(1 << 11)

#define MXC_USBCTRL_OFFSET		0
#define MXC_USB_PHY_CTR_FUNC_OFFSET	0x8
#define MXC_USB_PHY_CTR_FUNC2_OFFSET	0xc
#define MXC_USBH2CTRL_OFFSET		0x14

#define MX5_USBOTHER_REGS_OFFSET	0x800

/* USB_PHY_CTRL_FUNC2*/
#define MX5_USB_UTMI_PHYCTRL1_PLLDIV_MASK		0x3
#define MX5_USB_UTMI_PHYCTRL1_PLLDIV_SHIFT		0

struct mxc_usbh_platform_data {
	int (*init)(struct platform_device *pdev);
	int (*exit)(struct platform_device *pdev);

	unsigned int		 portsc;
	struct usb_phy		*otg;
};

int mx51_initialize_usb_hw(int port, unsigned int flags);
int mx25_initialize_usb_hw(int port, unsigned int flags);
int mx31_initialize_usb_hw(int port, unsigned int flags);
int mx35_initialize_usb_hw(int port, unsigned int flags);
int mx27_initialize_usb_hw(int port, unsigned int flags);

#endif /* __INCLUDE_ASM_ARCH_MXC_EHCI_H */

