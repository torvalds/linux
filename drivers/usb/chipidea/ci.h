/*
 * ci.h - common structures, functions, and macros of the ChipIdea driver
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DRIVERS_USB_CHIPIDEA_CI_H
#define __DRIVERS_USB_CHIPIDEA_CI_H

#include <linux/list.h>
#include <linux/irqreturn.h>
#include <linux/usb/gadget.h>

/******************************************************************************
 * DEFINE
 *****************************************************************************/
#define DMA_ADDR_INVALID	(~(dma_addr_t)0)
#define CI13XXX_PAGE_SIZE  4096ul /* page size for TD's */
#define ENDPT_MAX          32

/******************************************************************************
 * STRUCTURES
 *****************************************************************************/
/* Extension of usb_ep */
struct ci13xxx_ep {
	struct usb_ep                          ep;
	u8                                     dir;
	u8                                     num;
	u8                                     type;
	char                                   name[16];
	struct {
		struct list_head   queue;
		struct ci13xxx_qh *ptr;
		dma_addr_t         dma;
	}                                      qh;
	int                                    wedge;

	/* global resources */
	struct ci13xxx                        *udc;
	spinlock_t                            *lock;
	struct device                         *device;
	struct dma_pool                       *td_pool;
};

enum ci_role {
	CI_ROLE_HOST = 0,
	CI_ROLE_GADGET,
	CI_ROLE_END,
};

/**
 * struct ci_role_driver - host/gadget role driver
 * start: start this role
 * stop: stop this role
 * irq: irq handler for this role
 * name: role name string (host/gadget)
 */
struct ci_role_driver {
	int		(*start)(struct ci13xxx *);
	void		(*stop)(struct ci13xxx *);
	irqreturn_t	(*irq)(struct ci13xxx *);
	const char	*name;
};

struct hw_bank {
	unsigned      lpm;    /* is LPM? */
	void __iomem *abs;    /* bus map offset */
	void __iomem *cap;    /* bus map offset + CAP offset */
	void __iomem *op;     /* bus map offset + OP offset */
	size_t        size;   /* bank size */
	void __iomem **regmap;
};

/* CI13XXX UDC descriptor & global resources */
struct ci13xxx {
	spinlock_t		   lock;      /* ctrl register bank access */
	void __iomem              *regs;      /* registers address space */

	struct dma_pool           *qh_pool;   /* DMA pool for queue heads */
	struct dma_pool           *td_pool;   /* DMA pool for transfer descs */
	struct usb_request        *status;    /* ep0 status request */

	struct device             *dev;
	struct usb_gadget          gadget;     /* USB slave device */
	struct ci13xxx_ep          ci13xxx_ep[ENDPT_MAX]; /* extended endpts */
	u32                        ep0_dir;    /* ep0 direction */
	struct ci13xxx_ep          *ep0out, *ep0in;
	unsigned		   hw_ep_max;  /* number of hw endpoints */

	bool			   setaddr;
	u8			   address;
	u8                         remote_wakeup; /* Is remote wakeup feature
							enabled by the host? */
	u8                         suspended;  /* suspended by the host */
	u8                         test_mode;  /* the selected test mode */

	struct hw_bank             hw_bank;
	int			   irq;
	struct usb_gadget_driver  *driver;     /* 3rd party gadget driver */
	struct ci13xxx_udc_driver *udc_driver; /* device controller driver */
	int                        vbus_active; /* is VBUS active */
	struct usb_phy            *transceiver; /* Transceiver struct */
	struct ci_role_driver     *roles[CI_ROLE_END];
	enum ci_role               role;
	bool			   is_otg;
	struct work_struct	   work;
	struct workqueue_struct	  *wq;
};

static inline struct ci_role_driver *ci_role(struct ci13xxx *ci)
{
	BUG_ON(ci->role >= CI_ROLE_END || !ci->roles[ci->role]);
	return ci->roles[ci->role];
}

static inline int ci_role_start(struct ci13xxx *ci, enum ci_role role)
{
	int ret;

	if (role >= CI_ROLE_END)
		return -EINVAL;

	if (!ci->roles[role])
		return -ENXIO;

	ret = ci->roles[role]->start(ci);
	if (!ret)
		ci->role = role;
	return ret;
}

static inline void ci_role_stop(struct ci13xxx *ci)
{
	enum ci_role role = ci->role;

	if (role == CI_ROLE_END)
		return;

	ci->role = CI_ROLE_END;

	ci->roles[role]->stop(ci);
}

/******************************************************************************
 * REGISTERS
 *****************************************************************************/
/* register size */
#define REG_BITS   (32)

/* register indices */
enum ci13xxx_regs {
	CAP_CAPLENGTH,
	CAP_HCCPARAMS,
	CAP_DCCPARAMS,
	CAP_TESTMODE,
	CAP_LAST = CAP_TESTMODE,
	OP_USBCMD,
	OP_USBSTS,
	OP_USBINTR,
	OP_DEVICEADDR,
	OP_ENDPTLISTADDR,
	OP_PORTSC,
	OP_DEVLC,
	OP_OTGSC,
	OP_USBMODE,
	OP_ENDPTSETUPSTAT,
	OP_ENDPTPRIME,
	OP_ENDPTFLUSH,
	OP_ENDPTSTAT,
	OP_ENDPTCOMPLETE,
	OP_ENDPTCTRL,
	/* endptctrl1..15 follow */
	OP_LAST = OP_ENDPTCTRL + ENDPT_MAX / 2,
};

/**
 * ffs_nr: find first (least significant) bit set
 * @x: the word to search
 *
 * This function returns bit number (instead of position)
 */
static inline int ffs_nr(u32 x)
{
	int n = ffs(x);

	return n ? n-1 : 32;
}

/**
 * hw_read: reads from a hw register
 * @reg:  register index
 * @mask: bitfield mask
 *
 * This function returns register contents
 */
static inline u32 hw_read(struct ci13xxx *udc, enum ci13xxx_regs reg, u32 mask)
{
	return ioread32(udc->hw_bank.regmap[reg]) & mask;
}

/**
 * hw_write: writes to a hw register
 * @reg:  register index
 * @mask: bitfield mask
 * @data: new value
 */
static inline void hw_write(struct ci13xxx *udc, enum ci13xxx_regs reg,
			    u32 mask, u32 data)
{
	if (~mask)
		data = (ioread32(udc->hw_bank.regmap[reg]) & ~mask)
			| (data & mask);

	iowrite32(data, udc->hw_bank.regmap[reg]);
}

/**
 * hw_test_and_clear: tests & clears a hw register
 * @reg:  register index
 * @mask: bitfield mask
 *
 * This function returns register contents
 */
static inline u32 hw_test_and_clear(struct ci13xxx *udc, enum ci13xxx_regs reg,
				    u32 mask)
{
	u32 val = ioread32(udc->hw_bank.regmap[reg]) & mask;

	iowrite32(val, udc->hw_bank.regmap[reg]);
	return val;
}

/**
 * hw_test_and_write: tests & writes a hw register
 * @reg:  register index
 * @mask: bitfield mask
 * @data: new value
 *
 * This function returns register contents
 */
static inline u32 hw_test_and_write(struct ci13xxx *udc, enum ci13xxx_regs reg,
				    u32 mask, u32 data)
{
	u32 val = hw_read(udc, reg, ~0);

	hw_write(udc, reg, mask, data);
	return (val & mask) >> ffs_nr(mask);
}

int hw_device_reset(struct ci13xxx *ci);

int hw_port_test_set(struct ci13xxx *ci, u8 mode);

u8 hw_port_test_get(struct ci13xxx *ci);

#endif	/* __DRIVERS_USB_CHIPIDEA_CI_H */
