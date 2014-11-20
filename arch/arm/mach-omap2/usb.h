#include <linux/platform_data/usb-omap.h>

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

struct usbhs_phy_data {
	int port;		/* 1 indexed port number */
	int reset_gpio;
	int vcc_gpio;
	bool vcc_polarity;	/* 1 active high, 0 active low */
};

extern void usb_musb_init(struct omap_musb_board_data *board_data);
extern void usbhs_init(struct usbhs_omap_platform_data *pdata);
extern int usbhs_init_phys(struct usbhs_phy_data *phy, int num_phys);

extern void am35x_musb_reset(void);
extern void am35x_musb_phy_power(u8 on);
extern void am35x_musb_clear_irq(void);
extern void am35x_set_mode(u8 musb_mode);
extern void ti81xx_musb_phy_power(u8 on);

