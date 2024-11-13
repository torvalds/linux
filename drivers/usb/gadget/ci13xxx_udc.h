/* SPDX-License-Identifier: GPL-2.0-only */
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

/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */


#ifndef _CI13XXX_h_
#define _CI13XXX_h_

/******************************************************************************
 * DEFINE
 *****************************************************************************/
#define CI13XXX_PAGE_SIZE	4096ul /* page size for TD's */
#define ENDPT_MAX		(32)
#define CTRL_PAYLOAD_MAX	(64)
#define RX			(0)  /* similar to USB_DIR_OUT but can be used as an index */
#define TX			(1)  /* similar to USB_DIR_IN  but can be used as an index */

/* UDC private data:
 *  16MSb - Vendor ID | 16 LSb Vendor private data
 */
#define CI13XX_REQ_VENDOR_ID(id)	(id & 0xFFFF0000UL)

#define MSM_ETD_TYPE			BIT(1)
#define MSM_EP_PIPE_ID_RESET_VAL	0x1F001F

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
} __packed __aligned(4);

/* DMA layout of queue heads */
struct ci13xxx_qh {
	/* 0 */
	u32 cap;
#define QH_IOS                BIT(15)
#define QH_MAX_PKT            (0x07FFUL << 16)
#define QH_ZLT                BIT(29)
#define QH_MULT               (0x0003UL << 30)
#define QH_MULT_SHIFT         11
	/* 1 */
	u32 curr;
	/* 2 - 8 */
	struct ci13xxx_td        td;
	/* 9 */
	u32 RESERVED;
	struct usb_ctrlrequest   setup;
} __packed __aligned(4);

/* cache of larger request's original attributes */
struct ci13xxx_multi_req {
	unsigned int	     len;
	unsigned int	     actual;
	void                *buf;
};

/* Extension of usb_request */
struct ci13xxx_req {
	struct usb_request   req;
	unsigned int	     map;
	struct list_head     queue;
	struct ci13xxx_td   *ptr;
	dma_addr_t           dma;
	struct ci13xxx_td   *zptr;
	dma_addr_t           zdma;
	struct ci13xxx_multi_req multi;
};

/* Extension of usb_ep */
struct ci13xxx_ep {
	struct usb_ep                          ep;
	const struct usb_endpoint_descriptor  *desc;
	u8                                     dir;
	u8                                     num;
	u8                                     type;
	char                                   name[16];
	struct {
		struct list_head   queue;
		struct ci13xxx_qh *ptr;
		dma_addr_t         dma;
	}                                      qh;
	struct list_head                       rw_queue;
	int                                    wedge;

	/* global resources */
	spinlock_t                            *lock;
	struct device                         *device;
	struct dma_pool                       *td_pool;
	struct ci13xxx_td                     *last_zptr;
	dma_addr_t                            last_zdma;
	unsigned long                         dTD_update_fail_count;
	unsigned long                         dTD_active_re_q_count;
	unsigned long			      prime_fail_count;
	int				      prime_timer_count;
	struct timer_list		      prime_timer;

	bool                                  multi_req;
};

struct ci13xxx;
struct ci13xxx_udc_driver {
	const char	*name;
	unsigned long	 flags;
	unsigned int nz_itc;
#define CI13XXX_REGS_SHARED		BIT(0)
#define CI13XXX_REQUIRE_TRANSCEIVER	BIT(1)
#define CI13XXX_PULLUP_ON_VBUS		BIT(2)
#define CI13XXX_DISABLE_STREAMING	BIT(3)
#define CI13XXX_ZERO_ITC		BIT(4)
#define CI13XXX_ENABLE_AHB2AHB_BYPASS	BIT(6)

#define CI13XXX_CONTROLLER_RESET_EVENT			0
#define CI13XXX_CONTROLLER_CONNECT_EVENT		1
#define CI13XXX_CONTROLLER_SUSPEND_EVENT		2
#define CI13XXX_CONTROLLER_REMOTE_WAKEUP_EVENT		3
#define CI13XXX_CONTROLLER_RESUME_EVENT			4
#define CI13XXX_CONTROLLER_DISCONNECT_EVENT		5
#define CI13XXX_CONTROLLER_UDC_STARTED_EVENT		6
#define CI13XXX_CONTROLLER_ERROR_EVENT			7

	void	(*notify_event)(struct ci13xxx *udc, unsigned int event);
	bool    (*in_lpm)(struct ci13xxx *udc);
};

/* CI13XXX UDC descriptor & global resources */
struct ci13xxx {
	spinlock_t		  *lock;      /* ctrl register bank access */
	void __iomem              *regs;      /* registers address space */

	struct dma_pool           *qh_pool;   /* DMA pool for queue heads */
	struct dma_pool           *td_pool;   /* DMA pool for transfer descs */
	struct usb_request        *status;    /* ep0 status request */
	void                      *status_buf;/* GET_STATUS buffer */

	struct usb_gadget          gadget;     /* USB slave device */
	struct ci13xxx_ep          ci13xxx_ep[ENDPT_MAX]; /* extended endpts */
	u32                        ep0_dir;    /* ep0 direction */
#define ep0out ci13xxx_ep[0]
#define ep0in  ci13xxx_ep[hw_ep_max / 2]
	u8                         remote_wakeup; /* host-enabled remote wakeup */
	u8                         suspended;  /* suspended by the host */
	u8                         configured;  /* is device configured */
	u8                         test_mode;  /* the selected test mode */
	bool                       rw_pending; /* Remote wakeup pending flag */
	struct delayed_work        rw_work;    /* remote wakeup delayed work */
	struct usb_gadget_driver  *driver;     /* 3rd party gadget driver */
	struct ci13xxx_udc_driver *udc_driver; /* device controller driver */
	int                        vbus_active; /* is VBUS active */
	int                        softconnect; /* is pull-up enable allowed */
	unsigned long dTD_update_fail_count;
	struct usb_phy            *transceiver; /* Transceiver struct */
	bool                      skip_flush;   /*
						 * skip flushing remaining EP
						 * upon flush timeout for the
						 * first EP.
						 */
	bool			  l1_supported; /* is LPM supported */
};

extern struct ci13xxx *_udc;

/******************************************************************************
 * REGISTERS
 *****************************************************************************/
/* register size */
#define REG_BITS		(32)

/* HCCPARAMS */
#define HCCPARAMS_LEN		BIT(17)

/* DCCPARAMS */
#define DCCPARAMS_DEN		(0x1F << 0)
#define DCCPARAMS_DC		BIT(7)

/* TESTMODE */
#define TESTMODE_FORCE		BIT(0)

/* AHB_MODE */
#define AHB2AHB_BYPASS		BIT(31)

/* USBCMD */
#define USBCMD_RS		BIT(0)
#define USBCMD_RST		BIT(1)
#define USBCMD_SUTW		BIT(13)
#define USBCMD_ATDTW		BIT(14)

/* USBSTS & USBINTR */
#define USBi_UI			BIT(0)
#define USBi_UEI		BIT(1)
#define USBi_PCI		BIT(2)
#define USBi_URI		BIT(6)
#define USBi_SLI		BIT(8)

/* DEVICEADDR */
#define DEVICEADDR_USBADRA	BIT(24)
#define DEVICEADDR_USBADR	(0x7FUL << 25)

/* PORTSC */
#define PORTSC_FPR		BIT(6)
#define PORTSC_SUSP		BIT(7)
#define PORTSC_PR		BIT(8)
#define PORTSC_HSP		BIT(9)
#define PORTSC_PTC		(0x0FUL << 16)

/* DEVLC */
#define DEVLC_PSPD		(0x03UL << 25)
#define DEVLC_PSPD_HS		(0x02UL << 25)

/* USBMODE */
#define USBMODE_CM		(0x03UL <<  0)
#define USBMODE_CM_IDLE		(0x00UL <<  0)
#define USBMODE_CM_DEVICE	(0x02UL <<  0)
#define USBMODE_CM_HOST		(0x03UL <<  0)
#define USBMODE_SLOM		BIT(3)
#define USBMODE_SDIS		BIT(4)
#define USBCMD_ITC(n)		(n << 16) /* n = 0, 1, 2, 4, 8, 16, 32, 64 */
#define USBCMD_ITC_MASK		(0xFF << 16)

/* ENDPTCTRL */
#define ENDPTCTRL_RXS		BIT(0)
#define ENDPTCTRL_RXT		(0x03UL <<  2)
#define ENDPTCTRL_RXR		BIT(6)         /* reserved for port 0 */
#define ENDPTCTRL_RXE		BIT(7)
#define ENDPTCTRL_TXS		BIT(16)
#define ENDPTCTRL_TXT		(0x03UL << 18)
#define ENDPTCTRL_TXR		BIT(22)        /* reserved for port 0 */
#define ENDPTCTRL_TXE		BIT(23)

/******************************************************************************
 * LOGGING
 *****************************************************************************/
#define ci13xxx_printk(level, format, args...) \
do { \
	if (_udc == NULL) \
		pr_err(level "[%s] " format "\n", __func__, ## args); \
	else \
		dev_printk(level, _udc->gadget.dev.parent, \
			   "[%s] " format "\n", __func__, ## args); \
} while (0)

#ifndef err
#define err(format, args...)    ci13xxx_printk(KERN_ERR, format, ## args)
#endif

#define warn(format, args...)   ci13xxx_printk(KERN_WARNING, format, ## args)
#define info(format, args...)   ci13xxx_printk(KERN_INFO, format, ## args)

#ifdef TRACE
#define trace(format, args...)      ci13xxx_printk(KERN_DEBUG, format, ## args)
#define dbg_trace(format, args...)  dev_dbg(dev, format, ##args)
#else
#define trace(format, args...)      do {} while (0)
#define dbg_trace(format, args...)  do {} while (0)
#endif


int hw_device_reset(struct ci13xxx *udc);
int udc_probe(struct ci13xxx_udc_driver *driver, struct device *dev,
		void __iomem *regs);
void udc_remove(void);
irqreturn_t udc_irq(void);
int ci13xxx_pullup(struct usb_gadget *_gadget, int is_active);

#endif	/* _CI13XXX_h_ */
