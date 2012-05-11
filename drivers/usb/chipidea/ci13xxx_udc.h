/*
 * ci13xxx_udc.h - structures, registers, and macros MIPS USB IP core
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Description: MIPS USB IP core family device controller
 *              Structures, registers and logging macros
 */

#ifndef _CI13XXX_h_
#define _CI13XXX_h_

/******************************************************************************
 * DEFINE
 *****************************************************************************/
#define CI13XXX_PAGE_SIZE  4096ul /* page size for TD's */
#define ENDPT_MAX          32
#define CTRL_PAYLOAD_MAX   64
#define RX        0  /* similar to USB_DIR_OUT but can be used as an index */
#define TX        1  /* similar to USB_DIR_IN  but can be used as an index */

/******************************************************************************
 * STRUCTURES
 *****************************************************************************/
/* DMA layout of transfer descriptors */
struct ci13xxx_td {
	/* 0 */
	u32 next;
#define TD_TERMINATE          BIT(0)
#define TD_ADDR_MASK          (0xFFFFFFEUL << 5)
	/* 1 */
	u32 token;
#define TD_STATUS             (0x00FFUL <<  0)
#define TD_STATUS_TR_ERR      BIT(3)
#define TD_STATUS_DT_ERR      BIT(5)
#define TD_STATUS_HALTED      BIT(6)
#define TD_STATUS_ACTIVE      BIT(7)
#define TD_MULTO              (0x0003UL << 10)
#define TD_IOC                BIT(15)
#define TD_TOTAL_BYTES        (0x7FFFUL << 16)
	/* 2 */
	u32 page[5];
#define TD_CURR_OFFSET        (0x0FFFUL <<  0)
#define TD_FRAME_NUM          (0x07FFUL <<  0)
#define TD_RESERVED_MASK      (0x0FFFUL <<  0)
} __attribute__ ((packed));

/* DMA layout of queue heads */
struct ci13xxx_qh {
	/* 0 */
	u32 cap;
#define QH_IOS                BIT(15)
#define QH_MAX_PKT            (0x07FFUL << 16)
#define QH_ZLT                BIT(29)
#define QH_MULT               (0x0003UL << 30)
	/* 1 */
	u32 curr;
	/* 2 - 8 */
	struct ci13xxx_td        td;
	/* 9 */
	u32 RESERVED;
	struct usb_ctrlrequest   setup;
} __attribute__ ((packed));

/* Extension of usb_request */
struct ci13xxx_req {
	struct usb_request   req;
	unsigned             map;
	struct list_head     queue;
	struct ci13xxx_td   *ptr;
	dma_addr_t           dma;
	struct ci13xxx_td   *zptr;
	dma_addr_t           zdma;
};

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

struct ci13xxx;
struct ci13xxx_udc_driver {
	const char	*name;
	/* offset of the capability registers */
	uintptr_t	 capoffset;
	unsigned long	 flags;
#define CI13XXX_REGS_SHARED		BIT(0)
#define CI13XXX_REQUIRE_TRANSCEIVER	BIT(1)
#define CI13XXX_PULLUP_ON_VBUS		BIT(2)
#define CI13XXX_DISABLE_STREAMING	BIT(3)

#define CI13XXX_CONTROLLER_RESET_EVENT		0
#define CI13XXX_CONTROLLER_STOPPED_EVENT	1
	void	(*notify_event) (struct ci13xxx *udc, unsigned event);
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
};

/******************************************************************************
 * REGISTERS
 *****************************************************************************/
/* Default offset of capability registers */
#define DEF_CAPOFFSET		0x100

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

/* HCCPARAMS */
#define HCCPARAMS_LEN         BIT(17)

/* DCCPARAMS */
#define DCCPARAMS_DEN         (0x1F << 0)
#define DCCPARAMS_DC          BIT(7)

/* TESTMODE */
#define TESTMODE_FORCE        BIT(0)

/* USBCMD */
#define USBCMD_RS             BIT(0)
#define USBCMD_RST            BIT(1)
#define USBCMD_SUTW           BIT(13)
#define USBCMD_ATDTW          BIT(14)

/* USBSTS & USBINTR */
#define USBi_UI               BIT(0)
#define USBi_UEI              BIT(1)
#define USBi_PCI              BIT(2)
#define USBi_URI              BIT(6)
#define USBi_SLI              BIT(8)

/* DEVICEADDR */
#define DEVICEADDR_USBADRA    BIT(24)
#define DEVICEADDR_USBADR     (0x7FUL << 25)

/* PORTSC */
#define PORTSC_FPR            BIT(6)
#define PORTSC_SUSP           BIT(7)
#define PORTSC_HSP            BIT(9)
#define PORTSC_PTC            (0x0FUL << 16)

/* DEVLC */
#define DEVLC_PSPD            (0x03UL << 25)
#define    DEVLC_PSPD_HS      (0x02UL << 25)

/* USBMODE */
#define USBMODE_CM            (0x03UL <<  0)
#define    USBMODE_CM_IDLE    (0x00UL <<  0)
#define    USBMODE_CM_DEVICE  (0x02UL <<  0)
#define    USBMODE_CM_HOST    (0x03UL <<  0)
#define USBMODE_SLOM          BIT(3)
#define USBMODE_SDIS          BIT(4)

/* ENDPTCTRL */
#define ENDPTCTRL_RXS         BIT(0)
#define ENDPTCTRL_RXT         (0x03UL <<  2)
#define ENDPTCTRL_RXR         BIT(6)         /* reserved for port 0 */
#define ENDPTCTRL_RXE         BIT(7)
#define ENDPTCTRL_TXS         BIT(16)
#define ENDPTCTRL_TXT         (0x03UL << 18)
#define ENDPTCTRL_TXR         BIT(22)        /* reserved for port 0 */
#define ENDPTCTRL_TXE         BIT(23)

#endif	/* _CI13XXX_h_ */
