// include/asm-arm/mach-omap/usb.h

#ifndef	__ASM_ARCH_OMAP_USB_H
#define	__ASM_ARCH_OMAP_USB_H

#include <linux/io.h>
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

struct usbhs_omap_board_data {
	enum usbhs_omap_port_mode	port_mode[OMAP3_HS_USB_PORTS];

	/* have to be valid if phy_reset is true and portx is in phy mode */
	int	reset_gpio_port[OMAP3_HS_USB_PORTS];

	/* Set this to true for ES2.x silicon */
	unsigned			es2_compatibility:1;

	unsigned			phy_reset:1;

	/*
	 * Regulators for USB PHYs.
	 * Each PHY can have a separate regulator.
	 */
	struct regulator		*regulator[OMAP3_HS_USB_PORTS];
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

extern void usb_musb_init(struct omap_musb_board_data *board_data);

extern void usbhs_init(const struct usbhs_omap_board_data *pdata);

extern int omap4430_phy_power(struct device *dev, int ID, int on);
extern int omap4430_phy_set_clk(struct device *dev, int on);
extern int omap4430_phy_init(struct device *dev);
extern int omap4430_phy_exit(struct device *dev);
extern int omap4430_phy_suspend(struct device *dev, int suspend);

#endif

extern void am35x_musb_reset(void);
extern void am35x_musb_phy_power(u8 on);
extern void am35x_musb_clear_irq(void);
extern void am35x_set_mode(u8 musb_mode);
extern void ti81xx_musb_phy_power(u8 on);

/* AM35x */
/* USB 2.0 PHY Control */
#define CONF2_PHY_GPIOMODE	(1 << 23)
#define CONF2_OTGMODE		(3 << 14)
#define CONF2_NO_OVERRIDE	(0 << 14)
#define CONF2_FORCE_HOST	(1 << 14)
#define CONF2_FORCE_DEVICE	(2 << 14)
#define CONF2_FORCE_HOST_VBUS_LOW (3 << 14)
#define CONF2_SESENDEN		(1 << 13)
#define CONF2_VBDTCTEN		(1 << 12)
#define CONF2_REFFREQ_24MHZ	(2 << 8)
#define CONF2_REFFREQ_26MHZ	(7 << 8)
#define CONF2_REFFREQ_13MHZ	(6 << 8)
#define CONF2_REFFREQ		(0xf << 8)
#define CONF2_PHYCLKGD		(1 << 7)
#define CONF2_VBUSSENSE		(1 << 6)
#define CONF2_PHY_PLLON		(1 << 5)
#define CONF2_RESET		(1 << 4)
#define CONF2_PHYPWRDN		(1 << 3)
#define CONF2_OTGPWRDN		(1 << 2)
#define CONF2_DATPOL		(1 << 1)

/* TI81XX specific definitions */
#define USBCTRL0	0x620
#define USBSTAT0	0x624

/* TI816X PHY controls bits */
#define TI816X_USBPHY0_NORMAL_MODE	(1 << 0)
#define TI816X_USBPHY_REFCLK_OSC	(1 << 8)

/* TI814X PHY controls bits */
#define USBPHY_CM_PWRDN		(1 << 0)
#define USBPHY_OTG_PWRDN	(1 << 1)
#define USBPHY_CHGDET_DIS	(1 << 2)
#define USBPHY_CHGDET_RSTRT	(1 << 3)
#define USBPHY_SRCONDM		(1 << 4)
#define USBPHY_SINKONDP		(1 << 5)
#define USBPHY_CHGISINK_EN	(1 << 6)
#define USBPHY_CHGVSRC_EN	(1 << 7)
#define USBPHY_DMPULLUP		(1 << 8)
#define USBPHY_DPPULLUP		(1 << 9)
#define USBPHY_CDET_EXTCTL	(1 << 10)
#define USBPHY_GPIO_MODE	(1 << 12)
#define USBPHY_DPOPBUFCTL	(1 << 13)
#define USBPHY_DMOPBUFCTL	(1 << 14)
#define USBPHY_DPINPUT		(1 << 15)
#define USBPHY_DMINPUT		(1 << 16)
#define USBPHY_DPGPIO_PD	(1 << 17)
#define USBPHY_DMGPIO_PD	(1 << 18)
#define USBPHY_OTGVDET_EN	(1 << 19)
#define USBPHY_OTGSESSEND_EN	(1 << 20)
#define USBPHY_DATA_POLARITY	(1 << 23)

#if defined(CONFIG_ARCH_OMAP1) && defined(CONFIG_USB)
u32 omap1_usb0_init(unsigned nwires, unsigned is_device);
u32 omap1_usb1_init(unsigned nwires);
u32 omap1_usb2_init(unsigned nwires, unsigned alt_pingroup);
#else
static inline u32 omap1_usb0_init(unsigned nwires, unsigned is_device)
{
	return 0;
}
static inline u32 omap1_usb1_init(unsigned nwires)
{
	return 0;

}
static inline u32 omap1_usb2_init(unsigned nwires, unsigned alt_pingroup)
{
	return 0;
}
#endif

#endif	/* __ASM_ARCH_OMAP_USB_H */
