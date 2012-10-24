// include/asm-arm/mach-omap/usb.h

#ifndef	__ASM_ARCH_OMAP_USB_H
#define	__ASM_ARCH_OMAP_USB_H

#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/usb/musb.h>

#define OMAP3_HS_USB_PORTS	3

enum usbhs_omap_port_mode {
	OMAP_USBHS_PORT_MODE_UNUSED,
	OMAP_EHCI_PORT_MODE_PHY,
	OMAP_EHCI_PORT_MODE_TLL,
	OMAP_EHCI_PORT_MODE_HSIC,
	OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM,
	OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM,
	OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM,
	OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM,
	OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM
};

#ifdef CONFIG_ARCH_OMAP2PLUS

struct ehci_hcd_omap_platform_data {
	enum usbhs_omap_port_mode	port_mode[OMAP3_HS_USB_PORTS];
	int				reset_gpio_port[OMAP3_HS_USB_PORTS];
	struct regulator		*regulator[OMAP3_HS_USB_PORTS];
	unsigned			phy_reset:1;
};

struct ohci_hcd_omap_platform_data {
	enum usbhs_omap_port_mode	port_mode[OMAP3_HS_USB_PORTS];
	unsigned			es2_compatibility:1;
};

struct usbhs_omap_platform_data {
	enum usbhs_omap_port_mode		port_mode[OMAP3_HS_USB_PORTS];

	struct ehci_hcd_omap_platform_data	*ehci_data;
	struct ohci_hcd_omap_platform_data	*ohci_data;
};

struct usbtll_omap_platform_data {
	enum usbhs_omap_port_mode		port_mode[OMAP3_HS_USB_PORTS];
};
/*-------------------------------------------------------------------------*/

struct omap_musb_board_data {
	u8	interface_type;
	u8	mode;
	u16	power;
	unsigned extvbus:1;
	void	(*set_phy_power)(u8 on);
	void	(*clear_irq)(void);
	void	(*set_mode)(u8 mode);
	void	(*reset)(void);
};

enum musb_interface    {MUSB_INTERFACE_ULPI, MUSB_INTERFACE_UTMI};

extern int omap_tll_enable(void);
extern int omap_tll_disable(void);

#endif

#endif	/* __ASM_ARCH_OMAP_USB_H */
