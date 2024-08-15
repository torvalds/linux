// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2011 Marvell International Ltd. All rights reserved.
 */

#ifndef __MV_UDC_H
#define __MV_UDC_H

#define VUSBHS_MAX_PORTS	8

#define DQH_ALIGNMENT		2048
#define DTD_ALIGNMENT		64
#define DMA_BOUNDARY		4096

#define EP_DIR_IN	1
#define EP_DIR_OUT	0

#define DMA_ADDR_INVALID	(~(dma_addr_t)0)

#define EP0_MAX_PKT_SIZE	64
/* ep0 transfer state */
#define WAIT_FOR_SETUP		0
#define DATA_STATE_XMIT		1
#define DATA_STATE_NEED_ZLP	2
#define WAIT_FOR_OUT_STATUS	3
#define DATA_STATE_RECV		4

#define CAPLENGTH_MASK		(0xff)
#define DCCPARAMS_DEN_MASK	(0x1f)

#define HCSPARAMS_PPC		(0x10)

/* Frame Index Register Bit Masks */
#define USB_FRINDEX_MASKS	0x3fff

/* Command Register Bit Masks */
#define USBCMD_RUN_STOP				(0x00000001)
#define USBCMD_CTRL_RESET			(0x00000002)
#define USBCMD_SETUP_TRIPWIRE_SET		(0x00002000)
#define USBCMD_SETUP_TRIPWIRE_CLEAR		(~USBCMD_SETUP_TRIPWIRE_SET)

#define USBCMD_ATDTW_TRIPWIRE_SET		(0x00004000)
#define USBCMD_ATDTW_TRIPWIRE_CLEAR		(~USBCMD_ATDTW_TRIPWIRE_SET)

/* bit 15,3,2 are for frame list size */
#define USBCMD_FRAME_SIZE_1024			(0x00000000) /* 000 */
#define USBCMD_FRAME_SIZE_512			(0x00000004) /* 001 */
#define USBCMD_FRAME_SIZE_256			(0x00000008) /* 010 */
#define USBCMD_FRAME_SIZE_128			(0x0000000C) /* 011 */
#define USBCMD_FRAME_SIZE_64			(0x00008000) /* 100 */
#define USBCMD_FRAME_SIZE_32			(0x00008004) /* 101 */
#define USBCMD_FRAME_SIZE_16			(0x00008008) /* 110 */
#define USBCMD_FRAME_SIZE_8			(0x0000800C) /* 111 */

#define EPCTRL_TX_ALL_MASK			(0xFFFF0000)
#define EPCTRL_RX_ALL_MASK			(0x0000FFFF)

#define EPCTRL_TX_DATA_TOGGLE_RST		(0x00400000)
#define EPCTRL_TX_EP_STALL			(0x00010000)
#define EPCTRL_RX_EP_STALL			(0x00000001)
#define EPCTRL_RX_DATA_TOGGLE_RST		(0x00000040)
#define EPCTRL_RX_ENABLE			(0x00000080)
#define EPCTRL_TX_ENABLE			(0x00800000)
#define EPCTRL_CONTROL				(0x00000000)
#define EPCTRL_ISOCHRONOUS			(0x00040000)
#define EPCTRL_BULK				(0x00080000)
#define EPCTRL_INT				(0x000C0000)
#define EPCTRL_TX_TYPE				(0x000C0000)
#define EPCTRL_RX_TYPE				(0x0000000C)
#define EPCTRL_DATA_TOGGLE_INHIBIT		(0x00000020)
#define EPCTRL_TX_EP_TYPE_SHIFT			(18)
#define EPCTRL_RX_EP_TYPE_SHIFT			(2)

#define EPCOMPLETE_MAX_ENDPOINTS		(16)

/* endpoint list address bit masks */
#define USB_EP_LIST_ADDRESS_MASK              0xfffff800

#define PORTSCX_W1C_BITS			0x2a
#define PORTSCX_PORT_RESET			0x00000100
#define PORTSCX_PORT_POWER			0x00001000
#define PORTSCX_FORCE_FULL_SPEED_CONNECT	0x01000000
#define PORTSCX_PAR_XCVR_SELECT			0xC0000000
#define PORTSCX_PORT_FORCE_RESUME		0x00000040
#define PORTSCX_PORT_SUSPEND			0x00000080
#define PORTSCX_PORT_SPEED_FULL			0x00000000
#define PORTSCX_PORT_SPEED_LOW			0x04000000
#define PORTSCX_PORT_SPEED_HIGH			0x08000000
#define PORTSCX_PORT_SPEED_MASK			0x0C000000

/* USB MODE Register Bit Masks */
#define USBMODE_CTRL_MODE_IDLE			0x00000000
#define USBMODE_CTRL_MODE_DEVICE		0x00000002
#define USBMODE_CTRL_MODE_HOST			0x00000003
#define USBMODE_CTRL_MODE_RSV			0x00000001
#define USBMODE_SETUP_LOCK_OFF			0x00000008
#define USBMODE_STREAM_DISABLE			0x00000010

/* USB STS Register Bit Masks */
#define USBSTS_INT			0x00000001
#define USBSTS_ERR			0x00000002
#define USBSTS_PORT_CHANGE		0x00000004
#define USBSTS_FRM_LST_ROLL		0x00000008
#define USBSTS_SYS_ERR			0x00000010
#define USBSTS_IAA			0x00000020
#define USBSTS_RESET			0x00000040
#define USBSTS_SOF			0x00000080
#define USBSTS_SUSPEND			0x00000100
#define USBSTS_HC_HALTED		0x00001000
#define USBSTS_RCL			0x00002000
#define USBSTS_PERIODIC_SCHEDULE	0x00004000
#define USBSTS_ASYNC_SCHEDULE		0x00008000


/* Interrupt Enable Register Bit Masks */
#define USBINTR_INT_EN                          (0x00000001)
#define USBINTR_ERR_INT_EN                      (0x00000002)
#define USBINTR_PORT_CHANGE_DETECT_EN           (0x00000004)

#define USBINTR_ASYNC_ADV_AAE                   (0x00000020)
#define USBINTR_ASYNC_ADV_AAE_ENABLE            (0x00000020)
#define USBINTR_ASYNC_ADV_AAE_DISABLE           (0xFFFFFFDF)

#define USBINTR_RESET_EN                        (0x00000040)
#define USBINTR_SOF_UFRAME_EN                   (0x00000080)
#define USBINTR_DEVICE_SUSPEND                  (0x00000100)

#define USB_DEVICE_ADDRESS_MASK			(0xfe000000)
#define USB_DEVICE_ADDRESS_BIT_SHIFT		(25)

struct mv_cap_regs {
	u32	caplength_hciversion;
	u32	hcsparams;	/* HC structural parameters */
	u32	hccparams;	/* HC Capability Parameters*/
	u32	reserved[5];
	u32	dciversion;	/* DC version number and reserved 16 bits */
	u32	dccparams;	/* DC Capability Parameters */
};

struct mv_op_regs {
	u32	usbcmd;		/* Command register */
	u32	usbsts;		/* Status register */
	u32	usbintr;	/* Interrupt enable */
	u32	frindex;	/* Frame index */
	u32	reserved1[1];
	u32	deviceaddr;	/* Device Address */
	u32	eplistaddr;	/* Endpoint List Address */
	u32	ttctrl;		/* HOST TT status and control */
	u32	burstsize;	/* Programmable Burst Size */
	u32	txfilltuning;	/* Host Transmit Pre-Buffer Packet Tuning */
	u32	reserved[4];
	u32	epnak;		/* Endpoint NAK */
	u32	epnaken;	/* Endpoint NAK Enable */
	u32	configflag;	/* Configured Flag register */
	u32	portsc[VUSBHS_MAX_PORTS]; /* Port Status/Control x, x = 1..8 */
	u32	otgsc;
	u32	usbmode;	/* USB Host/Device mode */
	u32	epsetupstat;	/* Endpoint Setup Status */
	u32	epprime;	/* Endpoint Initialize */
	u32	epflush;	/* Endpoint De-initialize */
	u32	epstatus;	/* Endpoint Status */
	u32	epcomplete;	/* Endpoint Interrupt On Complete */
	u32	epctrlx[16];	/* Endpoint Control, where x = 0.. 15 */
	u32	mcr;		/* Mux Control */
	u32	isr;		/* Interrupt Status */
	u32	ier;		/* Interrupt Enable */
};

struct mv_udc {
	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;
	spinlock_t			lock;
	struct completion		*done;
	struct platform_device		*dev;
	int				irq;

	struct mv_cap_regs __iomem	*cap_regs;
	struct mv_op_regs __iomem	*op_regs;
	void __iomem                    *phy_regs;
	unsigned int			max_eps;
	struct mv_dqh			*ep_dqh;
	size_t				ep_dqh_size;
	dma_addr_t			ep_dqh_dma;

	struct dma_pool			*dtd_pool;
	struct mv_ep			*eps;

	struct mv_dtd			*dtd_head;
	struct mv_dtd			*dtd_tail;
	unsigned int			dtd_entries;

	struct mv_req			*status_req;
	struct usb_ctrlrequest		local_setup_buff;

	unsigned int		resume_state;	/* USB state to resume */
	unsigned int		usb_state;	/* USB current state */
	unsigned int		ep0_state;	/* Endpoint zero state */
	unsigned int		ep0_dir;

	unsigned int		dev_addr;
	unsigned int		test_mode;

	int			errors;
	unsigned		softconnect:1,
				vbus_active:1,
				remote_wakeup:1,
				softconnected:1,
				force_fs:1,
				clock_gating:1,
				active:1,
				stopped:1;      /* stop bit is setted */

	struct work_struct	vbus_work;
	struct workqueue_struct *qwork;

	struct usb_phy		*transceiver;

	struct mv_usb_platform_data     *pdata;

	/* some SOC has mutiple clock sources for USB*/
	struct clk      *clk;
};

/* endpoint data structure */
struct mv_ep {
	struct usb_ep		ep;
	struct mv_udc		*udc;
	struct list_head	queue;
	struct mv_dqh		*dqh;
	u32			direction;
	char			name[14];
	unsigned		stopped:1,
				wedge:1,
				ep_type:2,
				ep_num:8;
};

/* request data structure */
struct mv_req {
	struct usb_request	req;
	struct mv_dtd		*dtd, *head, *tail;
	struct mv_ep		*ep;
	struct list_head	queue;
	unsigned int            test_mode;
	unsigned		dtd_count;
	unsigned		mapped:1;
};

#define EP_QUEUE_HEAD_MULT_POS			30
#define EP_QUEUE_HEAD_ZLT_SEL			0x20000000
#define EP_QUEUE_HEAD_MAX_PKT_LEN_POS		16
#define EP_QUEUE_HEAD_MAX_PKT_LEN(ep_info)	(((ep_info)>>16)&0x07ff)
#define EP_QUEUE_HEAD_IOS			0x00008000
#define EP_QUEUE_HEAD_NEXT_TERMINATE		0x00000001
#define EP_QUEUE_HEAD_IOC			0x00008000
#define EP_QUEUE_HEAD_MULTO			0x00000C00
#define EP_QUEUE_HEAD_STATUS_HALT		0x00000040
#define EP_QUEUE_HEAD_STATUS_ACTIVE		0x00000080
#define EP_QUEUE_CURRENT_OFFSET_MASK		0x00000FFF
#define EP_QUEUE_HEAD_NEXT_POINTER_MASK		0xFFFFFFE0
#define EP_QUEUE_FRINDEX_MASK			0x000007FF
#define EP_MAX_LENGTH_TRANSFER			0x4000

struct mv_dqh {
	/* Bits 16..26 Bit 15 is Interrupt On Setup */
	u32	max_packet_length;
	u32	curr_dtd_ptr;		/* Current dTD Pointer */
	u32	next_dtd_ptr;		/* Next dTD Pointer */
	/* Total bytes (16..30), IOC (15), INT (8), STS (0-7) */
	u32	size_ioc_int_sts;
	u32	buff_ptr0;		/* Buffer pointer Page 0 (12-31) */
	u32	buff_ptr1;		/* Buffer pointer Page 1 (12-31) */
	u32	buff_ptr2;		/* Buffer pointer Page 2 (12-31) */
	u32	buff_ptr3;		/* Buffer pointer Page 3 (12-31) */
	u32	buff_ptr4;		/* Buffer pointer Page 4 (12-31) */
	u32	reserved1;
	/* 8 bytes of setup data that follows the Setup PID */
	u8	setup_buffer[8];
	u32	reserved2[4];
};


#define DTD_NEXT_TERMINATE		(0x00000001)
#define DTD_IOC				(0x00008000)
#define DTD_STATUS_ACTIVE		(0x00000080)
#define DTD_STATUS_HALTED		(0x00000040)
#define DTD_STATUS_DATA_BUFF_ERR	(0x00000020)
#define DTD_STATUS_TRANSACTION_ERR	(0x00000008)
#define DTD_RESERVED_FIELDS		(0x00007F00)
#define DTD_ERROR_MASK			(0x68)
#define DTD_ADDR_MASK			(0xFFFFFFE0)
#define DTD_PACKET_SIZE			0x7FFF0000
#define DTD_LENGTH_BIT_POS		(16)

struct mv_dtd {
	u32	dtd_next;
	u32	size_ioc_sts;
	u32	buff_ptr0;		/* Buffer pointer Page 0 */
	u32	buff_ptr1;		/* Buffer pointer Page 1 */
	u32	buff_ptr2;		/* Buffer pointer Page 2 */
	u32	buff_ptr3;		/* Buffer pointer Page 3 */
	u32	buff_ptr4;		/* Buffer pointer Page 4 */
	u32	scratch_ptr;
	/* 32 bytes */
	dma_addr_t td_dma;		/* dma address for this td */
	struct mv_dtd *next_dtd_virt;
};

#endif
