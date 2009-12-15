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

struct mxc_usbh_platform_data {
	int (*init)(struct platform_device *pdev);
	int (*exit)(struct platform_device *pdev);

	unsigned int		 portsc;
	unsigned int		 flags;
	struct otg_transceiver	*otg;
};

int mxc_set_usbcontrol(int port, unsigned int flags);

#endif /* __INCLUDE_ASM_ARCH_MXC_EHCI_H */

