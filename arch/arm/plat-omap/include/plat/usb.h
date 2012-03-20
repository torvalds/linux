// include/asm-arm/mach-omap/usb.h

#ifndef	__ASM_ARCH_OMAP_USB_H
#define	__ASM_ARCH_OMAP_USB_H

#include <linux/usb/musb.h>
#include <plat/board.h>

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

#define OMAP1_OTG_BASE			0xfffb0400
#define OMAP1_UDC_BASE			0xfffb4000
#define OMAP1_OHCI_BASE			0xfffba000

#define OMAP2_OHCI_BASE			0x4805e000
#define OMAP2_UDC_BASE			0x4805e200
#define OMAP2_OTG_BASE			0x4805e300

#ifdef CONFIG_ARCH_OMAP1

#define OTG_BASE			OMAP1_OTG_BASE
#define UDC_BASE			OMAP1_UDC_BASE
#define OMAP_OHCI_BASE			OMAP1_OHCI_BASE

#else

#define OTG_BASE			OMAP2_OTG_BASE
#define UDC_BASE			OMAP2_UDC_BASE
#define OMAP_OHCI_BASE			OMAP2_OHCI_BASE

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

/*
 * FIXME correct answer depends on hmc_mode,
 * as does (on omap1) any nonzero value for config->otg port number
 */
#ifdef	CONFIG_USB_GADGET_OMAP
#define	is_usb0_device(config)	1
#else
#define	is_usb0_device(config)	0
#endif

void omap_otg_init(struct omap_usb_config *config);

#if defined(CONFIG_USB) || defined(CONFIG_USB_MODULE)
void omap1_usb_init(struct omap_usb_config *pdata);
#else
static inline void omap1_usb_init(struct omap_usb_config *pdata)
{
}
#endif

#if defined(CONFIG_ARCH_OMAP_OTG) || defined(CONFIG_ARCH_OMAP_OTG_MODULE)
void omap2_usbfs_init(struct omap_usb_config *pdata);
#else
static inline void omap2_usbfs_init(struct omap_usb_config *pdata)
{
}
#endif

/*-------------------------------------------------------------------------*/

/*
 * OTG and transceiver registers, for OMAPs starting with ARM926
 */
#define OTG_REV				(OTG_BASE + 0x00)
#define OTG_SYSCON_1			(OTG_BASE + 0x04)
#	define	 USB2_TRX_MODE(w)	(((w)>>24)&0x07)
#	define	 USB1_TRX_MODE(w)	(((w)>>20)&0x07)
#	define	 USB0_TRX_MODE(w)	(((w)>>16)&0x07)
#	define	 OTG_IDLE_EN		(1 << 15)
#	define	 HST_IDLE_EN		(1 << 14)
#	define	 DEV_IDLE_EN		(1 << 13)
#	define	 OTG_RESET_DONE		(1 << 2)
#	define	 OTG_SOFT_RESET		(1 << 1)
#define OTG_SYSCON_2			(OTG_BASE + 0x08)
#	define	 OTG_EN			(1 << 31)
#	define	 USBX_SYNCHRO		(1 << 30)
#	define	 OTG_MST16		(1 << 29)
#	define	 SRP_GPDATA		(1 << 28)
#	define	 SRP_GPDVBUS		(1 << 27)
#	define	 SRP_GPUVBUS(w)		(((w)>>24)&0x07)
#	define	 A_WAIT_VRISE(w)	(((w)>>20)&0x07)
#	define	 B_ASE_BRST(w)		(((w)>>16)&0x07)
#	define	 SRP_DPW		(1 << 14)
#	define	 SRP_DATA		(1 << 13)
#	define	 SRP_VBUS		(1 << 12)
#	define	 OTG_PADEN		(1 << 10)
#	define	 HMC_PADEN		(1 << 9)
#	define	 UHOST_EN		(1 << 8)
#	define	 HMC_TLLSPEED		(1 << 7)
#	define	 HMC_TLLATTACH		(1 << 6)
#	define	 OTG_HMC(w)		(((w)>>0)&0x3f)
#define OTG_CTRL			(OTG_BASE + 0x0c)
#	define	 OTG_USB2_EN		(1 << 29)
#	define	 OTG_USB2_DP		(1 << 28)
#	define	 OTG_USB2_DM		(1 << 27)
#	define	 OTG_USB1_EN		(1 << 26)
#	define	 OTG_USB1_DP		(1 << 25)
#	define	 OTG_USB1_DM		(1 << 24)
#	define	 OTG_USB0_EN		(1 << 23)
#	define	 OTG_USB0_DP		(1 << 22)
#	define	 OTG_USB0_DM		(1 << 21)
#	define	 OTG_ASESSVLD		(1 << 20)
#	define	 OTG_BSESSEND		(1 << 19)
#	define	 OTG_BSESSVLD		(1 << 18)
#	define	 OTG_VBUSVLD		(1 << 17)
#	define	 OTG_ID			(1 << 16)
#	define	 OTG_DRIVER_SEL		(1 << 15)
#	define	 OTG_A_SETB_HNPEN	(1 << 12)
#	define	 OTG_A_BUSREQ		(1 << 11)
#	define	 OTG_B_HNPEN		(1 << 9)
#	define	 OTG_B_BUSREQ		(1 << 8)
#	define	 OTG_BUSDROP		(1 << 7)
#	define	 OTG_PULLDOWN		(1 << 5)
#	define	 OTG_PULLUP		(1 << 4)
#	define	 OTG_DRV_VBUS		(1 << 3)
#	define	 OTG_PD_VBUS		(1 << 2)
#	define	 OTG_PU_VBUS		(1 << 1)
#	define	 OTG_PU_ID		(1 << 0)
#define OTG_IRQ_EN			(OTG_BASE + 0x10)	/* 16-bit */
#	define	 DRIVER_SWITCH		(1 << 15)
#	define	 A_VBUS_ERR		(1 << 13)
#	define	 A_REQ_TMROUT		(1 << 12)
#	define	 A_SRP_DETECT		(1 << 11)
#	define	 B_HNP_FAIL		(1 << 10)
#	define	 B_SRP_TMROUT		(1 << 9)
#	define	 B_SRP_DONE		(1 << 8)
#	define	 B_SRP_STARTED		(1 << 7)
#	define	 OPRT_CHG		(1 << 0)
#define OTG_IRQ_SRC			(OTG_BASE + 0x14)	/* 16-bit */
	// same bits as in IRQ_EN
#define OTG_OUTCTRL			(OTG_BASE + 0x18)	/* 16-bit */
#	define	 OTGVPD			(1 << 14)
#	define	 OTGVPU			(1 << 13)
#	define	 OTGPUID		(1 << 12)
#	define	 USB2VDR		(1 << 10)
#	define	 USB2PDEN		(1 << 9)
#	define	 USB2PUEN		(1 << 8)
#	define	 USB1VDR		(1 << 6)
#	define	 USB1PDEN		(1 << 5)
#	define	 USB1PUEN		(1 << 4)
#	define	 USB0VDR		(1 << 2)
#	define	 USB0PDEN		(1 << 1)
#	define	 USB0PUEN		(1 << 0)
#define OTG_TEST			(OTG_BASE + 0x20)	/* 16-bit */
#define OTG_VENDOR_CODE			(OTG_BASE + 0xfc)	/* 16-bit */

/*-------------------------------------------------------------------------*/

/* OMAP1 */
#define	USB_TRANSCEIVER_CTRL		(0xfffe1000 + 0x0064)
#	define	CONF_USB2_UNI_R		(1 << 8)
#	define	CONF_USB1_UNI_R		(1 << 7)
#	define	CONF_USB_PORT0_R(x)	(((x)>>4)&0x7)
#	define	CONF_USB0_ISOLATE_R	(1 << 3)
#	define	CONF_USB_PWRDN_DM_R	(1 << 2)
#	define	CONF_USB_PWRDN_DP_R	(1 << 1)

/* OMAP2 */
#	define	USB_UNIDIR			0x0
#	define	USB_UNIDIR_TLL			0x1
#	define	USB_BIDIR			0x2
#	define	USB_BIDIR_TLL			0x3
#	define	USBTXWRMODEI(port, x)	((x) << (22 - (port * 2)))
#	define	USBT2TLL5PI		(1 << 17)
#	define	USB0PUENACTLOI		(1 << 16)
#	define	USBSTANDBYCTRL		(1 << 15)
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
