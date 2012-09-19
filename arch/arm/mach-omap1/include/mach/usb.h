/*
 * FIXME correct answer depends on hmc_mode,
 * as does (on omap1) any nonzero value for config->otg port number
 */
#ifdef	CONFIG_USB_GADGET_OMAP
#define	is_usb0_device(config)	1
#else
#define	is_usb0_device(config)	0
#endif

struct omap_usb_config {
	/* Configure drivers according to the connectors on your board:
	 *  - "A" connector (rectagular)
	 *	... for host/OHCI use, set "register_host".
	 *  - "B" connector (squarish) or "Mini-B"
	 *	... for device/gadget use, set "register_dev".
	 *  - "Mini-AB" connector (very similar to Mini-B)
	 *	... for OTG use as device OR host, initialize "otg"
	 */
	unsigned	register_host:1;
	unsigned	register_dev:1;
	u8		otg;	/* port number, 1-based:  usb1 == 2 */

	u8		hmc_mode;

	/* implicitly true if otg:  host supports remote wakeup? */
	u8		rwc;

	/* signaling pins used to talk to transceiver on usbN:
	 *  0 == usbN unused
	 *  2 == usb0-only, using internal transceiver
	 *  3 == 3 wire bidirectional
	 *  4 == 4 wire bidirectional
	 *  6 == 6 wire unidirectional (or TLL)
	 */
	u8		pins[3];

	struct platform_device *udc_device;
	struct platform_device *ohci_device;
	struct platform_device *otg_device;

	u32 (*usb0_init)(unsigned nwires, unsigned is_device);
	u32 (*usb1_init)(unsigned nwires);
	u32 (*usb2_init)(unsigned nwires, unsigned alt_pingroup);

	int (*ocpi_enable)(void);
};

void omap_otg_init(struct omap_usb_config *config);

#if defined(CONFIG_USB) || defined(CONFIG_USB_MODULE)
void omap1_usb_init(struct omap_usb_config *pdata);
#else
static inline void omap1_usb_init(struct omap_usb_config *pdata)
{
}
#endif

#define OMAP1_OTG_BASE			0xfffb0400
#define OMAP1_UDC_BASE			0xfffb4000
#define OMAP1_OHCI_BASE			0xfffba000

#define OMAP2_OHCI_BASE			0x4805e000
#define OMAP2_UDC_BASE			0x4805e200
#define OMAP2_OTG_BASE			0x4805e300
#define OTG_BASE			OMAP1_OTG_BASE
#define UDC_BASE			OMAP1_UDC_BASE
#define OMAP_OHCI_BASE			OMAP1_OHCI_BASE

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
