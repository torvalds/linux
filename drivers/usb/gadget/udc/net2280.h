/*
 * NetChip 2280 high/full speed USB device controller.
 * Unlike many such controllers, this one talks PCI.
 */

/*
 * Copyright (C) 2002 NetChip Technology, Inc. (http://www.netchip.com)
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2014 Ricardo Ribalda - Qtechnology/AS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/usb/net2280.h>
#include <linux/usb/usb338x.h>

/*-------------------------------------------------------------------------*/

#ifdef	__KERNEL__

/* indexed registers [11.10] are accessed indirectly
 * caller must own the device lock.
 */

static inline u32 get_idx_reg(struct net2280_regs __iomem *regs, u32 index)
{
	writel(index, &regs->idxaddr);
	/* NOTE:  synchs device/cpu memory views */
	return readl(&regs->idxdata);
}

static inline void
set_idx_reg(struct net2280_regs __iomem *regs, u32 index, u32 value)
{
	writel(index, &regs->idxaddr);
	writel(value, &regs->idxdata);
	/* posted, may not be visible yet */
}

#endif	/* __KERNEL__ */

#define PCI_VENDOR_ID_PLX_LEGACY 0x17cc

#define PLX_LEGACY		BIT(0)
#define PLX_2280		BIT(1)
#define PLX_SUPERSPEED		BIT(2)

#define REG_DIAG		0x0
#define     RETRY_COUNTER                                       16
#define     FORCE_PCI_SERR                                      11
#define     FORCE_PCI_INTERRUPT                                 10
#define     FORCE_USB_INTERRUPT                                 9
#define     FORCE_CPU_INTERRUPT                                 8
#define     ILLEGAL_BYTE_ENABLES                                5
#define     FAST_TIMES                                          4
#define     FORCE_RECEIVE_ERROR                                 2
#define     FORCE_TRANSMIT_CRC_ERROR                            0
#define REG_FRAME		0x02	/* from last sof */
#define REG_CHIPREV		0x03	/* in bcd */
#define	REG_HS_NAK_RATE		0x0a	/* NAK per N uframes */

#define	CHIPREV_1	0x0100
#define	CHIPREV_1A	0x0110

/* DEFECT 7374 */
#define DEFECT_7374_NUMBEROF_MAX_WAIT_LOOPS         200
#define DEFECT_7374_PROCESSOR_WAIT_TIME             10

/* ep0 max packet size */
#define EP0_SS_MAX_PACKET_SIZE  0x200
#define EP0_HS_MAX_PACKET_SIZE  0x40
#ifdef	__KERNEL__

/*-------------------------------------------------------------------------*/

/* [8.3] for scatter/gather i/o
 * use struct net2280_dma_regs bitfields
 */
struct net2280_dma {
	__le32		dmacount;
	__le32		dmaaddr;		/* the buffer */
	__le32		dmadesc;		/* next dma descriptor */
	__le32		_reserved;
} __aligned(16);

/*-------------------------------------------------------------------------*/

/* DRIVER DATA STRUCTURES and UTILITIES */

struct net2280_ep {
	struct usb_ep				ep;
	struct net2280_ep_regs __iomem *cfg;
	struct net2280_ep_regs			__iomem *regs;
	struct net2280_dma_regs			__iomem *dma;
	struct net2280_dma			*dummy;
	dma_addr_t				td_dma;	/* of dummy */
	struct net2280				*dev;
	unsigned long				irqs;

	/* analogous to a host-side qh */
	struct list_head			queue;
	const struct usb_endpoint_descriptor	*desc;
	unsigned				num : 8,
						fifo_size : 12,
						in_fifo_validate : 1,
						out_overflow : 1,
						stopped : 1,
						wedged : 1,
						is_in : 1,
						is_iso : 1,
						responded : 1;
};

static inline void allow_status(struct net2280_ep *ep)
{
	/* ep0 only */
	writel(BIT(CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE) |
		BIT(CLEAR_NAK_OUT_PACKETS) |
		BIT(CLEAR_NAK_OUT_PACKETS_MODE),
		&ep->regs->ep_rsp);
	ep->stopped = 1;
}

static inline void allow_status_338x(struct net2280_ep *ep)
{
	/*
	 * Control Status Phase Handshake was set by the chip when the setup
	 * packet arrived. While set, the chip automatically NAKs the host's
	 * Status Phase tokens.
	 */
	writel(BIT(CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE), &ep->regs->ep_rsp);

	ep->stopped = 1;

	/* TD 9.9 Halt Endpoint test.  TD 9.22 set feature test. */
	ep->responded = 0;
}

struct net2280_request {
	struct usb_request		req;
	struct net2280_dma		*td;
	dma_addr_t			td_dma;
	struct list_head		queue;
	unsigned			mapped : 1,
					valid : 1;
};

struct net2280 {
	/* each pci device provides one gadget, several endpoints */
	struct usb_gadget		gadget;
	spinlock_t			lock;
	struct net2280_ep		ep[9];
	struct usb_gadget_driver	*driver;
	unsigned			enabled : 1,
					protocol_stall : 1,
					softconnect : 1,
					got_irq : 1,
					region:1,
					u1_enable:1,
					u2_enable:1,
					ltm_enable:1,
					wakeup_enable:1,
					addressed_state:1,
					bug7734_patched:1;
	u16				chiprev;
	int enhanced_mode;
	int n_ep;
	kernel_ulong_t			quirks;


	/* pci state used to access those endpoints */
	struct pci_dev			*pdev;
	struct net2280_regs		__iomem *regs;
	struct net2280_usb_regs		__iomem *usb;
	struct usb338x_usb_ext_regs	__iomem *usb_ext;
	struct net2280_pci_regs		__iomem *pci;
	struct net2280_dma_regs		__iomem *dma;
	struct net2280_dep_regs		__iomem *dep;
	struct net2280_ep_regs		__iomem *epregs;
	struct usb338x_ll_regs		__iomem *llregs;
	struct usb338x_ll_lfps_regs	__iomem *ll_lfps_regs;
	struct usb338x_ll_tsn_regs	__iomem *ll_tsn_regs;
	struct usb338x_ll_chi_regs	__iomem *ll_chicken_reg;
	struct usb338x_pl_regs		__iomem *plregs;

	struct pci_pool			*requests;
	/* statistics...*/
};

static inline void set_halt(struct net2280_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	writel(BIT(CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE) |
		/* set NAK_OUT for erratum 0114 */
		((ep->dev->chiprev == CHIPREV_1) << SET_NAK_OUT_PACKETS) |
		BIT(SET_ENDPOINT_HALT),
		&ep->regs->ep_rsp);
}

static inline void clear_halt(struct net2280_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	writel(BIT(CLEAR_ENDPOINT_HALT) |
		BIT(CLEAR_ENDPOINT_TOGGLE) |
		    /*
		     * unless the gadget driver left a short packet in the
		     * fifo, this reverses the erratum 0114 workaround.
		     */
		((ep->dev->chiprev == CHIPREV_1) << CLEAR_NAK_OUT_PACKETS),
		&ep->regs->ep_rsp);
}

/*
 * FSM value for Defect 7374 (U1U2 Test) is managed in
 * chip's SCRATCH register:
 */
#define DEFECT7374_FSM_FIELD    28

/* Waiting for Control Read:
 *  - A transition to this state indicates a fresh USB connection,
 *    before the first Setup Packet. The connection speed is not
 *    known. Firmware is waiting for the first Control Read.
 *  - Starting state: This state can be thought of as the FSM's typical
 *    starting state.
 *  - Tip: Upon the first SS Control Read the FSM never
 *    returns to this state.
 */
#define DEFECT7374_FSM_WAITING_FOR_CONTROL_READ BIT(DEFECT7374_FSM_FIELD)

/* Non-SS Control Read:
 *  - A transition to this state indicates detection of the first HS
 *    or FS Control Read.
 *  - Tip: Upon the first SS Control Read the FSM never
 *    returns to this state.
 */
#define	DEFECT7374_FSM_NON_SS_CONTROL_READ (2 << DEFECT7374_FSM_FIELD)

/* SS Control Read:
 *  - A transition to this state indicates detection of the
 *    first SS Control Read.
 *  - This state indicates workaround completion. Workarounds no longer
 *    need to be applied (as long as the chip remains powered up).
 *  - Tip: Once in this state the FSM state does not change (until
 *    the chip's power is lost and restored).
 *  - This can be thought of as the final state of the FSM;
 *    the FSM 'locks-up' in this state until the chip loses power.
 */
#define DEFECT7374_FSM_SS_CONTROL_READ (3 << DEFECT7374_FSM_FIELD)

#ifdef USE_RDK_LEDS

static inline void net2280_led_init(struct net2280 *dev)
{
	/* LED3 (green) is on during USB activity. note erratum 0113. */
	writel(BIT(GPIO3_LED_SELECT) |
		BIT(GPIO3_OUTPUT_ENABLE) |
		BIT(GPIO2_OUTPUT_ENABLE) |
		BIT(GPIO1_OUTPUT_ENABLE) |
		BIT(GPIO0_OUTPUT_ENABLE),
		&dev->regs->gpioctl);
}

/* indicate speed with bi-color LED 0/1 */
static inline
void net2280_led_speed(struct net2280 *dev, enum usb_device_speed speed)
{
	u32	val = readl(&dev->regs->gpioctl);
	switch (speed) {
	case USB_SPEED_SUPER:		/* green + red */
		val |= BIT(GPIO0_DATA) | BIT(GPIO1_DATA);
		break;
	case USB_SPEED_HIGH:		/* green */
		val &= ~BIT(GPIO0_DATA);
		val |= BIT(GPIO1_DATA);
		break;
	case USB_SPEED_FULL:		/* red */
		val &= ~BIT(GPIO1_DATA);
		val |= BIT(GPIO0_DATA);
		break;
	default:			/* (off/black) */
		val &= ~(BIT(GPIO1_DATA) | BIT(GPIO0_DATA));
		break;
	}
	writel(val, &dev->regs->gpioctl);
}

/* indicate power with LED 2 */
static inline void net2280_led_active(struct net2280 *dev, int is_active)
{
	u32	val = readl(&dev->regs->gpioctl);

	/* FIXME this LED never seems to turn on.*/
	if (is_active)
		val |= GPIO2_DATA;
	else
		val &= ~GPIO2_DATA;
	writel(val, &dev->regs->gpioctl);
}

static inline void net2280_led_shutdown(struct net2280 *dev)
{
	/* turn off all four GPIO*_DATA bits */
	writel(readl(&dev->regs->gpioctl) & ~0x0f,
			&dev->regs->gpioctl);
}

#else

#define net2280_led_init(dev)		do { } while (0)
#define net2280_led_speed(dev, speed)	do { } while (0)
#define net2280_led_shutdown(dev)	do { } while (0)

#endif

/*-------------------------------------------------------------------------*/

#define ep_dbg(ndev, fmt, args...) \
	dev_dbg((&((ndev)->pdev->dev)), fmt, ##args)

#define ep_vdbg(ndev, fmt, args...) \
	dev_vdbg((&((ndev)->pdev->dev)), fmt, ##args)

#define ep_info(ndev, fmt, args...) \
	dev_info((&((ndev)->pdev->dev)), fmt, ##args)

#define ep_warn(ndev, fmt, args...) \
	dev_warn((&((ndev)->pdev->dev)), fmt, ##args)

#define ep_err(ndev, fmt, args...) \
	dev_err((&((ndev)->pdev->dev)), fmt, ##args)

/*-------------------------------------------------------------------------*/

static inline void set_fifo_bytecount(struct net2280_ep *ep, unsigned count)
{
	if (ep->dev->pdev->vendor == 0x17cc)
		writeb(count, 2 + (u8 __iomem *) &ep->regs->ep_cfg);
	else{
		u32 tmp = readl(&ep->cfg->ep_cfg) &
					(~(0x07 << EP_FIFO_BYTE_COUNT));
		writel(tmp | (count << EP_FIFO_BYTE_COUNT), &ep->cfg->ep_cfg);
	}
}

static inline void start_out_naking(struct net2280_ep *ep)
{
	/* NOTE:  hardware races lurk here, and PING protocol issues */
	writel(BIT(SET_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
	/* synch with device */
	readl(&ep->regs->ep_rsp);
}

static inline void stop_out_naking(struct net2280_ep *ep)
{
	u32	tmp;

	tmp = readl(&ep->regs->ep_stat);
	if ((tmp & BIT(NAK_OUT_PACKETS)) != 0)
		writel(BIT(CLEAR_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
}


static inline void set_max_speed(struct net2280_ep *ep, u32 max)
{
	u32 reg;
	static const u32 ep_enhanced[9] = { 0x10, 0x60, 0x30, 0x80,
					  0x50, 0x20, 0x70, 0x40, 0x90 };

	if (ep->dev->enhanced_mode) {
		reg = ep_enhanced[ep->num];
		switch (ep->dev->gadget.speed) {
		case USB_SPEED_SUPER:
			reg += 2;
			break;
		case USB_SPEED_FULL:
			reg += 1;
			break;
		case USB_SPEED_HIGH:
		default:
			break;
		}
	} else {
		reg = (ep->num + 1) * 0x10;
		if (ep->dev->gadget.speed != USB_SPEED_HIGH)
			reg += 1;
	}

	set_idx_reg(ep->dev->regs, reg, max);
}

#endif	/* __KERNEL__ */
