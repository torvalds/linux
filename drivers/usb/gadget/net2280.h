/*
 * NetChip 2280 high/full speed USB device controller.
 * Unlike many such controllers, this one talks PCI.
 */

/*
 * Copyright (C) 2002 NetChip Technology, Inc. (http://www.netchip.com)
 * Copyright (C) 2003 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/usb/net2280.h>

/*-------------------------------------------------------------------------*/

#ifdef	__KERNEL__

/* indexed registers [11.10] are accessed indirectly
 * caller must own the device lock.
 */

static inline u32
get_idx_reg (struct net2280_regs __iomem *regs, u32 index)
{
	writel (index, &regs->idxaddr);
	/* NOTE:  synchs device/cpu memory views */
	return readl (&regs->idxdata);
}

static inline void
set_idx_reg (struct net2280_regs __iomem *regs, u32 index, u32 value)
{
	writel (index, &regs->idxaddr);
	writel (value, &regs->idxdata);
	/* posted, may not be visible yet */
}

#endif	/* __KERNEL__ */


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

#ifdef	__KERNEL__

/* ep a-f highspeed and fullspeed maxpacket, addresses
 * computed from ep->num
 */
#define REG_EP_MAXPKT(dev,num) (((num) + 1) * 0x10 + \
		(((dev)->gadget.speed == USB_SPEED_HIGH) ? 0 : 1))

/*-------------------------------------------------------------------------*/

/* [8.3] for scatter/gather i/o
 * use struct net2280_dma_regs bitfields
 */
struct net2280_dma {
	__le32		dmacount;
	__le32		dmaaddr;		/* the buffer */
	__le32		dmadesc;		/* next dma descriptor */
	__le32		_reserved;
} __attribute__ ((aligned (16)));

/*-------------------------------------------------------------------------*/

/* DRIVER DATA STRUCTURES and UTILITIES */

struct net2280_ep {
	struct usb_ep				ep;
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
						is_in : 1,
						is_iso : 1,
						responded : 1;
};

static inline void allow_status (struct net2280_ep *ep)
{
	/* ep0 only */
	writel (  (1 << CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE)
		| (1 << CLEAR_NAK_OUT_PACKETS)
		| (1 << CLEAR_NAK_OUT_PACKETS_MODE)
		, &ep->regs->ep_rsp);
	ep->stopped = 1;
}

/* count (<= 4) bytes in the next fifo write will be valid */
static inline void set_fifo_bytecount (struct net2280_ep *ep, unsigned count)
{
	writeb (count, 2 + (u8 __iomem *) &ep->regs->ep_cfg);
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
	struct net2280_ep		ep [7];
	struct usb_gadget_driver 	*driver;
	unsigned			enabled : 1,
					protocol_stall : 1,
					softconnect : 1,
					got_irq : 1,
					region : 1;
	u16				chiprev;

	/* pci state used to access those endpoints */
	struct pci_dev			*pdev;
	struct net2280_regs		__iomem *regs;
	struct net2280_usb_regs		__iomem *usb;
	struct net2280_pci_regs		__iomem *pci;
	struct net2280_dma_regs		__iomem *dma;
	struct net2280_dep_regs		__iomem *dep;
	struct net2280_ep_regs		__iomem *epregs;

	struct pci_pool			*requests;
	// statistics...
};

static inline void set_halt (struct net2280_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	writel (  (1 << CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE)
		    /* set NAK_OUT for erratum 0114 */
		| ((ep->dev->chiprev == CHIPREV_1) << SET_NAK_OUT_PACKETS)
		| (1 << SET_ENDPOINT_HALT)
		, &ep->regs->ep_rsp);
}

static inline void clear_halt (struct net2280_ep *ep)
{
	/* ep0 and bulk/intr endpoints */
	writel (  (1 << CLEAR_ENDPOINT_HALT)
		| (1 << CLEAR_ENDPOINT_TOGGLE)
		    /* unless the gadget driver left a short packet in the
		     * fifo, this reverses the erratum 0114 workaround.
		     */
		| ((ep->dev->chiprev == CHIPREV_1) << CLEAR_NAK_OUT_PACKETS)
		, &ep->regs->ep_rsp);
}

#ifdef USE_RDK_LEDS

static inline void net2280_led_init (struct net2280 *dev)
{
	/* LED3 (green) is on during USB activity. note erratum 0113. */
	writel ((1 << GPIO3_LED_SELECT)
		| (1 << GPIO3_OUTPUT_ENABLE)
		| (1 << GPIO2_OUTPUT_ENABLE)
		| (1 << GPIO1_OUTPUT_ENABLE)
		| (1 << GPIO0_OUTPUT_ENABLE)
		, &dev->regs->gpioctl);
}

/* indicate speed with bi-color LED 0/1 */
static inline
void net2280_led_speed (struct net2280 *dev, enum usb_device_speed speed)
{
	u32	val = readl (&dev->regs->gpioctl);
	switch (speed) {
	case USB_SPEED_HIGH:		/* green */
		val &= ~(1 << GPIO0_DATA);
		val |= (1 << GPIO1_DATA);
		break;
	case USB_SPEED_FULL:		/* red */
		val &= ~(1 << GPIO1_DATA);
		val |= (1 << GPIO0_DATA);
		break;
	default:			/* (off/black) */
		val &= ~((1 << GPIO1_DATA) | (1 << GPIO0_DATA));
		break;
	}
	writel (val, &dev->regs->gpioctl);
}

/* indicate power with LED 2 */
static inline void net2280_led_active (struct net2280 *dev, int is_active)
{
	u32	val = readl (&dev->regs->gpioctl);

	// FIXME this LED never seems to turn on.
	if (is_active)
		val |= GPIO2_DATA;
	else
		val &= ~GPIO2_DATA;
	writel (val, &dev->regs->gpioctl);
}
static inline void net2280_led_shutdown (struct net2280 *dev)
{
	/* turn off all four GPIO*_DATA bits */
	writel (readl (&dev->regs->gpioctl) & ~0x0f,
			&dev->regs->gpioctl);
}

#else

#define net2280_led_init(dev)		do { } while (0)
#define net2280_led_speed(dev, speed)	do { } while (0)
#define net2280_led_shutdown(dev)	do { } while (0)

#endif

/*-------------------------------------------------------------------------*/

#define xprintk(dev,level,fmt,args...) \
	printk(level "%s %s: " fmt , driver_name , \
			pci_name(dev->pdev) , ## args)

#ifdef DEBUG
#undef DEBUG
#define DEBUG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DEBUG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDEBUG DEBUG
#else
#define VDEBUG(dev,fmt,args...) \
	do { } while (0)
#endif	/* VERBOSE */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARNING(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)

/*-------------------------------------------------------------------------*/

static inline void start_out_naking (struct net2280_ep *ep)
{
	/* NOTE:  hardware races lurk here, and PING protocol issues */
	writel ((1 << SET_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
	/* synch with device */
	readl (&ep->regs->ep_rsp);
}

#ifdef DEBUG
static inline void assert_out_naking (struct net2280_ep *ep, const char *where)
{
	u32	tmp = readl (&ep->regs->ep_stat);

	if ((tmp & (1 << NAK_OUT_PACKETS)) == 0) {
		DEBUG (ep->dev, "%s %s %08x !NAK\n",
				ep->ep.name, where, tmp);
		writel ((1 << SET_NAK_OUT_PACKETS),
			&ep->regs->ep_rsp);
	}
}
#define ASSERT_OUT_NAKING(ep) assert_out_naking(ep,__func__)
#else
#define ASSERT_OUT_NAKING(ep) do {} while (0)
#endif

static inline void stop_out_naking (struct net2280_ep *ep)
{
	u32	tmp;

	tmp = readl (&ep->regs->ep_stat);
	if ((tmp & (1 << NAK_OUT_PACKETS)) != 0)
		writel ((1 << CLEAR_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
}

#endif	/* __KERNEL__ */
