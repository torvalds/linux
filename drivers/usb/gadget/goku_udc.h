/*
 * Toshiba TC86C001 ("Goku-S") USB Device Controller driver
 *
 * Copyright (C) 2000-2002 Lineo
 *      by Stuart Lynne, Tom Rushworth, and Bruce Balden
 * Copyright (C) 2002 Toshiba Corporation
 * Copyright (C) 2003 MontaVista Software (source@mvista.com)
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/*
 * PCI BAR 0 points to these registers.
 */
struct goku_udc_regs {
	/* irq management */
	u32	int_status;		/* 0x000 */
	u32	int_enable;
#define INT_SUSPEND		0x00001		/* or resume */
#define INT_USBRESET		0x00002
#define INT_ENDPOINT0		0x00004
#define INT_SETUP		0x00008
#define INT_STATUS		0x00010
#define INT_STATUSNAK		0x00020
#define INT_EPxDATASET(n)	(0x00020 << (n))	/* 0 < n < 4 */
#	define INT_EP1DATASET		0x00040
#	define INT_EP2DATASET		0x00080
#	define INT_EP3DATASET		0x00100
#define INT_EPnNAK(n)		(0x00100 < (n))		/* 0 < n < 4 */
#	define INT_EP1NAK		0x00200
#	define INT_EP2NAK		0x00400
#	define INT_EP3NAK		0x00800
#define INT_SOF			0x01000
#define INT_ERR			0x02000
#define INT_MSTWRSET		0x04000
#define INT_MSTWREND		0x08000
#define INT_MSTWRTMOUT		0x10000
#define INT_MSTRDEND		0x20000
#define INT_SYSERROR		0x40000
#define INT_PWRDETECT		0x80000

#define	INT_DEVWIDE \
	(INT_PWRDETECT|INT_SYSERROR/*|INT_ERR*/|INT_USBRESET|INT_SUSPEND)
#define	INT_EP0 \
	(INT_SETUP|INT_ENDPOINT0/*|INT_STATUS*/|INT_STATUSNAK)

	u32	dma_master;
#define MST_EOPB_DIS		0x0800
#define MST_EOPB_ENA		0x0400
#define MST_TIMEOUT_DIS		0x0200
#define MST_TIMEOUT_ENA		0x0100
#define MST_RD_EOPB		0x0080		/* write-only */
#define MST_RD_RESET		0x0040
#define MST_WR_RESET		0x0020
#define MST_RD_ENA		0x0004		/* 1:start, 0:ignore */
#define MST_WR_ENA		0x0002		/* 1:start, 0:ignore */
#define MST_CONNECTION		0x0001		/* 0 for ep1out/ep2in */

#define MST_R_BITS		(MST_EOPB_DIS|MST_EOPB_ENA \
					|MST_RD_ENA|MST_RD_RESET)
#define MST_W_BITS		(MST_TIMEOUT_DIS|MST_TIMEOUT_ENA \
					|MST_WR_ENA|MST_WR_RESET)
#define MST_RW_BITS		(MST_R_BITS|MST_W_BITS \
					|MST_CONNECTION)

/* these values assume (dma_master & MST_CONNECTION) == 0 */
#define UDC_MSTWR_ENDPOINT        1
#define UDC_MSTRD_ENDPOINT        2

	/* dma master write */
	u32	out_dma_start;
	u32	out_dma_end;
	u32	out_dma_current;

	/* dma master read */
	u32	in_dma_start;
	u32	in_dma_end;
	u32	in_dma_current;

	u32	power_detect;
#define PW_DETECT		0x04
#define PW_RESETB		0x02
#define PW_PULLUP		0x01

	u8	_reserved0 [0x1d8];

	/* endpoint registers */
	u32	ep_fifo [4];		/* 0x200 */
	u8	_reserved1 [0x10];
	u32	ep_mode [4];		/* only 1-3 valid */
	u8	_reserved2 [0x10];

	u32	ep_status [4];
#define EPxSTATUS_TOGGLE	0x40
#define EPxSTATUS_SUSPEND	0x20
#define EPxSTATUS_EP_MASK	(0x07<<2)
#	define EPxSTATUS_EP_READY	(0<<2)
#	define EPxSTATUS_EP_DATAIN	(1<<2)
#	define EPxSTATUS_EP_FULL	(2<<2)
#	define EPxSTATUS_EP_TX_ERR	(3<<2)
#	define EPxSTATUS_EP_RX_ERR	(4<<2)
#	define EPxSTATUS_EP_BUSY	(5<<2)
#	define EPxSTATUS_EP_STALL	(6<<2)
#	define EPxSTATUS_EP_INVALID	(7<<2)
#define EPxSTATUS_FIFO_DISABLE	0x02
#define EPxSTATUS_STAGE_ERROR	0x01

	u8	_reserved3 [0x10];
	u32	EPxSizeLA[4];
#define PACKET_ACTIVE		(1<<7)
#define DATASIZE		0x7f
	u8	_reserved3a [0x10];
	u32	EPxSizeLB[4];		/* only 1,2 valid */
	u8	_reserved3b [0x10];
	u32	EPxSizeHA[4];		/* only 1-3 valid */
	u8	_reserved3c [0x10];
	u32	EPxSizeHB[4];		/* only 1,2 valid */
	u8	_reserved4[0x30];

	/* SETUP packet contents */
	u32	bRequestType;		/* 0x300 */
	u32	bRequest;
	u32	wValueL;
	u32	wValueH;
	u32	wIndexL;
	u32	wIndexH;
	u32	wLengthL;
	u32	wLengthH;

	/* command interaction/handshaking */
	u32	SetupRecv;		/* 0x320 */
	u32	CurrConfig;
	u32	StdRequest;
	u32	Request;
	u32	DataSet;
#define DATASET_A(epnum)	(1<<(2*(epnum)))
#define DATASET_B(epnum)	(2<<(2*(epnum)))
#define DATASET_AB(epnum)	(3<<(2*(epnum)))
	u8	_reserved5[4];

	u32	UsbState;
#define USBSTATE_CONFIGURED	0x04
#define USBSTATE_ADDRESSED	0x02
#define USBSTATE_DEFAULT	0x01

	u32	EOP;

	u32	Command;		/* 0x340 */
#define COMMAND_SETDATA0	2
#define COMMAND_RESET		3
#define COMMAND_STALL		4
#define COMMAND_INVALID		5
#define COMMAND_FIFO_DISABLE	7
#define COMMAND_FIFO_ENABLE	8
#define COMMAND_INIT_DESCRIPTOR	9
#define COMMAND_FIFO_CLEAR	10	/* also stall */
#define COMMAND_STALL_CLEAR	11
#define COMMAND_EP(n)		((n) << 4)

	u32	EPxSingle;
	u8	_reserved6[4];
	u32	EPxBCS;
	u8	_reserved7[8];
	u32	IntControl;
#define ICONTROL_STATUSNAK	1
	u8	_reserved8[4];

	u32	reqmode;	// 0x360 standard request mode, low 8 bits
#define G_REQMODE_SET_INTF	(1<<7)
#define G_REQMODE_GET_INTF	(1<<6)
#define G_REQMODE_SET_CONF	(1<<5)
#define G_REQMODE_GET_CONF	(1<<4)
#define G_REQMODE_GET_DESC	(1<<3)
#define G_REQMODE_SET_FEAT	(1<<2)
#define G_REQMODE_CLEAR_FEAT	(1<<1)
#define G_REQMODE_GET_STATUS	(1<<0)

	u32	ReqMode;
	u8	_reserved9[0x18];
	u32	PortStatus;		/* 0x380 */
	u8	_reserved10[8];
	u32	address;
	u32	buff_test;
	u8	_reserved11[4];
	u32	UsbReady;
	u8	_reserved12[4];
	u32	SetDescStall;		/* 0x3a0 */
	u8	_reserved13[0x45c];

	/* hardware could handle limited GET_DESCRIPTOR duties */
#define	DESC_LEN	0x80
	u32	descriptors[DESC_LEN];	/* 0x800 */
	u8	_reserved14[0x600];

} __attribute__ ((packed));

#define	MAX_FIFO_SIZE	64
#define	MAX_EP0_SIZE	8		/* ep0 fifo is bigger, though */


/*-------------------------------------------------------------------------*/

/* DRIVER DATA STRUCTURES and UTILITIES */

struct goku_ep {
	struct usb_ep				ep;
	struct goku_udc				*dev;
	unsigned long				irqs;

	unsigned				num:8,
						dma:1,
						is_in:1,
						stopped:1;

	/* analogous to a host-side qh */
	struct list_head			queue;
	const struct usb_endpoint_descriptor	*desc;

	u32 __iomem				*reg_fifo;
	u32 __iomem				*reg_mode;
	u32 __iomem				*reg_status;
};

struct goku_request {
	struct usb_request		req;
	struct list_head		queue;

	unsigned			mapped:1;
};

enum ep0state {
	EP0_DISCONNECT,		/* no host */
	EP0_IDLE,		/* between STATUS ack and SETUP report */
	EP0_IN, EP0_OUT,	/* data stage */
	EP0_STATUS,		/* status stage */
	EP0_STALL,		/* data or status stages */
	EP0_SUSPEND,		/* usb suspend */
};

struct goku_udc {
	/* each pci device provides one gadget, several endpoints */
	struct usb_gadget		gadget;
	spinlock_t			lock;
	struct goku_ep			ep[4];
	struct usb_gadget_driver	*driver;

	enum ep0state			ep0state;
	unsigned			got_irq:1,
					got_region:1,
					req_config:1,
					configured:1,
					enabled:1;

	/* pci state used to access those endpoints */
	struct pci_dev			*pdev;
	struct goku_udc_regs __iomem	*regs;
	u32				int_enable;

	/* statistics... */
	unsigned long			irqs;
};

/*-------------------------------------------------------------------------*/

#define xprintk(dev,level,fmt,args...) \
	printk(level "%s %s: " fmt , driver_name , \
			pci_name(dev->pdev) , ## args)

#ifdef DEBUG
#define DBG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DBG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDBG DBG
#else
#define VDBG(dev,fmt,args...) \
	do { } while (0)
#endif	/* VERBOSE */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)

