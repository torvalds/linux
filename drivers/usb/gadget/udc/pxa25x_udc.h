// SPDX-License-Identifier: GPL-2.0+
/*
 * Intel PXA25x on-chip full speed USB device controller
 *
 * Copyright (C) 2003 Robert Schwebel <r.schwebel@pengutronix.de>, Pengutronix
 * Copyright (C) 2003 David Brownell
 */

#ifndef __LINUX_USB_GADGET_PXA25X_H
#define __LINUX_USB_GADGET_PXA25X_H

#include <linux/types.h>

/*-------------------------------------------------------------------------*/

/* pxa25x has this (move to include/asm-arm/arch-pxa/pxa-regs.h) */
#define UFNRH_SIR	(1 << 7)	/* SOF interrupt request */
#define UFNRH_SIM	(1 << 6)	/* SOF interrupt mask */
#define UFNRH_IPE14	(1 << 5)	/* ISO packet error, ep14 */
#define UFNRH_IPE9	(1 << 4)	/* ISO packet error, ep9 */
#define UFNRH_IPE4	(1 << 3)	/* ISO packet error, ep4 */

/* pxa255 has this (move to include/asm-arm/arch-pxa/pxa-regs.h) */
#define	UDCCFR		UDC_RES2	/* UDC Control Function Register */
#define UDCCFR_AREN	(1 << 7)	/* ACK response enable (now) */
#define UDCCFR_ACM	(1 << 2)	/* ACK control mode (wait for AREN) */

/* latest pxa255 errata define new "must be one" bits in UDCCFR */
#define	UDCCFR_MB1	(0xff & ~(UDCCFR_AREN|UDCCFR_ACM))

/*-------------------------------------------------------------------------*/

struct pxa25x_udc;

struct pxa25x_ep {
	struct usb_ep				ep;
	struct pxa25x_udc			*dev;

	struct list_head			queue;
	unsigned long				pio_irqs;

	unsigned short				fifo_size;
	u8					bEndpointAddress;
	u8					bmAttributes;

	unsigned				stopped : 1;
	unsigned				dma_fixup : 1;

	/* UDCCS = UDC Control/Status for this EP
	 * UBCR = UDC Byte Count Remaining (contents of OUT fifo)
	 * UDDR = UDC Endpoint Data Register (the fifo)
	 * DRCM = DMA Request Channel Map
	 */
	u32					regoff_udccs;
	u32					regoff_ubcr;
	u32					regoff_uddr;
};

struct pxa25x_request {
	struct usb_request			req;
	struct list_head			queue;
};

enum ep0_state {
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_END_XFER,
	EP0_STALL,
};

#define EP0_FIFO_SIZE	((unsigned)16)
#define BULK_FIFO_SIZE	((unsigned)64)
#define ISO_FIFO_SIZE	((unsigned)256)
#define INT_FIFO_SIZE	((unsigned)8)

struct udc_stats {
	struct ep0stats {
		unsigned long		ops;
		unsigned long		bytes;
	} read, write;
	unsigned long			irqs;
};

#ifdef CONFIG_USB_PXA25X_SMALL
/* when memory's tight, SMALL config saves code+data.  */
#define	PXA_UDC_NUM_ENDPOINTS	3
#endif

#ifndef	PXA_UDC_NUM_ENDPOINTS
#define	PXA_UDC_NUM_ENDPOINTS	16
#endif

struct pxa25x_udc {
	struct usb_gadget			gadget;
	struct usb_gadget_driver		*driver;

	enum ep0_state				ep0state;
	struct udc_stats			stats;
	unsigned				got_irq : 1,
						vbus : 1,
						pullup : 1,
						has_cfr : 1,
						req_pending : 1,
						req_std : 1,
						req_config : 1,
						suspended : 1,
						active : 1;

#define start_watchdog(dev) mod_timer(&dev->timer, jiffies + (HZ/200))
	struct timer_list			timer;

	struct device				*dev;
	struct clk				*clk;
	struct pxa2xx_udc_mach_info		*mach;
	struct usb_phy				*transceiver;
	u64					dma_mask;
	struct pxa25x_ep			ep [PXA_UDC_NUM_ENDPOINTS];

#ifdef CONFIG_USB_GADGET_DEBUG_FS
	struct dentry				*debugfs_udc;
#endif
	void __iomem				*regs;
};
#define to_pxa25x(g)	(container_of((g), struct pxa25x_udc, gadget))

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_ARCH_LUBBOCK
#include <mach/lubbock.h>
/* lubbock can also report usb connect/disconnect irqs */
#endif

static struct pxa25x_udc *the_controller;

/*-------------------------------------------------------------------------*/

/*
 * Debugging support vanishes in non-debug builds.  DBG_NORMAL should be
 * mostly silent during normal use/testing, with no timing side-effects.
 */
#define DBG_NORMAL	1	/* error paths, device state transitions */
#define DBG_VERBOSE	2	/* add some success path trace info */
#define DBG_NOISY	3	/* ... even more: request level */
#define DBG_VERY_NOISY	4	/* ... even more: packet level */

#define DMSG(stuff...)	pr_debug("udc: " stuff)

#ifdef DEBUG

static const char *state_name[] = {
	"EP0_IDLE",
	"EP0_IN_DATA_PHASE", "EP0_OUT_DATA_PHASE",
	"EP0_END_XFER", "EP0_STALL"
};

#ifdef VERBOSE_DEBUG
#    define UDC_DEBUG DBG_VERBOSE
#else
#    define UDC_DEBUG DBG_NORMAL
#endif

static void __maybe_unused
dump_udccr(const char *label)
{
	u32	udccr = UDCCR;
	DMSG("%s %02X =%s%s%s%s%s%s%s%s\n",
		label, udccr,
		(udccr & UDCCR_REM) ? " rem" : "",
		(udccr & UDCCR_RSTIR) ? " rstir" : "",
		(udccr & UDCCR_SRM) ? " srm" : "",
		(udccr & UDCCR_SUSIR) ? " susir" : "",
		(udccr & UDCCR_RESIR) ? " resir" : "",
		(udccr & UDCCR_RSM) ? " rsm" : "",
		(udccr & UDCCR_UDA) ? " uda" : "",
		(udccr & UDCCR_UDE) ? " ude" : "");
}

static void __maybe_unused
dump_udccs0(const char *label)
{
	u32		udccs0 = UDCCS0;

	DMSG("%s %s %02X =%s%s%s%s%s%s%s%s\n",
		label, state_name[the_controller->ep0state], udccs0,
		(udccs0 & UDCCS0_SA) ? " sa" : "",
		(udccs0 & UDCCS0_RNE) ? " rne" : "",
		(udccs0 & UDCCS0_FST) ? " fst" : "",
		(udccs0 & UDCCS0_SST) ? " sst" : "",
		(udccs0 & UDCCS0_DRWF) ? " dwrf" : "",
		(udccs0 & UDCCS0_FTF) ? " ftf" : "",
		(udccs0 & UDCCS0_IPR) ? " ipr" : "",
		(udccs0 & UDCCS0_OPR) ? " opr" : "");
}

static inline u32 udc_ep_get_UDCCS(struct pxa25x_ep *);

static void __maybe_unused
dump_state(struct pxa25x_udc *dev)
{
	u32		tmp;
	unsigned	i;

	DMSG("%s, uicr %02X.%02X, usir %02X.%02x, ufnr %02X.%02X\n",
		state_name[dev->ep0state],
		UICR1, UICR0, USIR1, USIR0, UFNRH, UFNRL);
	dump_udccr("udccr");
	if (dev->has_cfr) {
		tmp = UDCCFR;
		DMSG("udccfr %02X =%s%s\n", tmp,
			(tmp & UDCCFR_AREN) ? " aren" : "",
			(tmp & UDCCFR_ACM) ? " acm" : "");
	}

	if (!dev->driver) {
		DMSG("no gadget driver bound\n");
		return;
	} else
		DMSG("ep0 driver '%s'\n", dev->driver->driver.name);

	dump_udccs0 ("udccs0");
	DMSG("ep0 IN %lu/%lu, OUT %lu/%lu\n",
		dev->stats.write.bytes, dev->stats.write.ops,
		dev->stats.read.bytes, dev->stats.read.ops);

	for (i = 1; i < PXA_UDC_NUM_ENDPOINTS; i++) {
		if (dev->ep[i].ep.desc == NULL)
			continue;
		DMSG ("udccs%d = %02x\n", i, udc_ep_get_UDCCS(&dev->ep[i]));
	}
}

#else

#define	dump_udccr(x)	do{}while(0)
#define	dump_udccs0(x)	do{}while(0)
#define	dump_state(x)	do{}while(0)

#define UDC_DEBUG ((unsigned)0)

#endif

#define DBG(lvl, stuff...) do{if ((lvl) <= UDC_DEBUG) DMSG(stuff);}while(0)

#define ERR(stuff...)		pr_err("udc: " stuff)
#define WARNING(stuff...)	pr_warn("udc: " stuff)
#define INFO(stuff...)		pr_info("udc: " stuff)


#endif /* __LINUX_USB_GADGET_PXA25X_H */
