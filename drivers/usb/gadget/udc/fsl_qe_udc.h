/*
 * drivers/usb/gadget/qe_udc.h
 *
 * Copyright (C) 2006-2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * 	Xiaobo Xie <X.Xie@freescale.com>
 * 	Li Yang <leoli@freescale.com>
 *
 * Description:
 * Freescale USB device/endpoint management registers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef __FSL_QE_UDC_H
#define __FSL_QE_UDC_H

/* SoC type */
#define PORT_CPM	0
#define PORT_QE		1

#define USB_MAX_ENDPOINTS               4
#define USB_MAX_PIPES                   USB_MAX_ENDPOINTS
#define USB_EP0_MAX_SIZE		64
#define USB_MAX_CTRL_PAYLOAD            0x4000
#define USB_BDRING_LEN			16
#define USB_BDRING_LEN_RX		256
#define USB_BDRING_LEN_TX		16
#define MIN_EMPTY_BDS			128
#define MAX_DATA_BDS			8
#define USB_CRC_SIZE			2
#define USB_DIR_BOTH			0x88
#define R_BUF_MAXSIZE			0x800
#define USB_EP_PARA_ALIGNMENT		32

/* USB Mode Register bit define */
#define USB_MODE_EN		0x01
#define USB_MODE_HOST		0x02
#define USB_MODE_TEST		0x04
#define USB_MODE_SFTE		0x08
#define USB_MODE_RESUME		0x40
#define USB_MODE_LSS		0x80

/* USB Slave Address Register Mask */
#define USB_SLVADDR_MASK	0x7F

/* USB Endpoint register define */
#define USB_EPNUM_MASK		0xF000
#define USB_EPNUM_SHIFT		12

#define USB_TRANS_MODE_SHIFT	8
#define USB_TRANS_CTR		0x0000
#define USB_TRANS_INT		0x0100
#define USB_TRANS_BULK		0x0200
#define USB_TRANS_ISO		0x0300

#define USB_EP_MF		0x0020
#define USB_EP_RTE		0x0010

#define USB_THS_SHIFT		2
#define USB_THS_MASK		0x000c
#define USB_THS_NORMAL		0x0
#define USB_THS_IGNORE_IN	0x0004
#define USB_THS_NACK		0x0008
#define USB_THS_STALL		0x000c

#define USB_RHS_SHIFT   	0
#define USB_RHS_MASK		0x0003
#define USB_RHS_NORMAL  	0x0
#define USB_RHS_IGNORE_OUT	0x0001
#define USB_RHS_NACK		0x0002
#define USB_RHS_STALL		0x0003

#define USB_RTHS_MASK		0x000f

/* USB Command Register define */
#define USB_CMD_STR_FIFO	0x80
#define USB_CMD_FLUSH_FIFO	0x40
#define USB_CMD_ISFT		0x20
#define USB_CMD_DSFT		0x10
#define USB_CMD_EP_MASK		0x03

/* USB Event and Mask Register define */
#define USB_E_MSF_MASK		0x0800
#define USB_E_SFT_MASK		0x0400
#define USB_E_RESET_MASK	0x0200
#define USB_E_IDLE_MASK		0x0100
#define USB_E_TXE4_MASK		0x0080
#define USB_E_TXE3_MASK		0x0040
#define USB_E_TXE2_MASK		0x0020
#define USB_E_TXE1_MASK		0x0010
#define USB_E_SOF_MASK		0x0008
#define USB_E_BSY_MASK		0x0004
#define USB_E_TXB_MASK		0x0002
#define USB_E_RXB_MASK		0x0001
#define USBER_ALL_CLEAR 	0x0fff

#define USB_E_DEFAULT_DEVICE   (USB_E_RESET_MASK | USB_E_TXE4_MASK | \
				USB_E_TXE3_MASK | USB_E_TXE2_MASK | \
				USB_E_TXE1_MASK | USB_E_BSY_MASK | \
				USB_E_TXB_MASK | USB_E_RXB_MASK)

#define USB_E_TXE_MASK         (USB_E_TXE4_MASK | USB_E_TXE3_MASK|\
				 USB_E_TXE2_MASK | USB_E_TXE1_MASK)
/* USB Status Register define */
#define USB_IDLE_STATUS_MASK	0x01

/* USB Start of Frame Timer */
#define USB_USSFT_MASK		0x3FFF

/* USB Frame Number Register */
#define USB_USFRN_MASK		0xFFFF

struct usb_device_para{
	u16	epptr[4];
	u32	rstate;
	u32	rptr;
	u16	frame_n;
	u16	rbcnt;
	u32	rtemp;
	u32	rxusb_data;
	u16	rxuptr;
	u8	reso[2];
	u32	softbl;
	u8	sofucrctemp;
};

struct usb_ep_para{
	u16	rbase;
	u16	tbase;
	u8	rbmr;
	u8	tbmr;
	u16	mrblr;
	u16	rbptr;
	u16	tbptr;
	u32	tstate;
	u32	tptr;
	u16	tcrc;
	u16	tbcnt;
	u32	ttemp;
	u16	txusbu_ptr;
	u8	reserve[2];
};

#define USB_BUSMODE_GBL		0x20
#define USB_BUSMODE_BO_MASK	0x18
#define USB_BUSMODE_BO_SHIFT	0x3
#define USB_BUSMODE_BE		0x2
#define USB_BUSMODE_CETM	0x04
#define USB_BUSMODE_DTB		0x02

/* Endpoint basic handle */
#define ep_index(EP)		((EP)->ep.desc->bEndpointAddress & 0xF)
#define ep_maxpacket(EP)	((EP)->ep.maxpacket)
#define ep_is_in(EP)	((ep_index(EP) == 0) ? (EP->udc->ep0_dir == \
			USB_DIR_IN) : ((EP)->ep.desc->bEndpointAddress \
			& USB_DIR_IN) == USB_DIR_IN)

/* ep0 transfer state */
#define WAIT_FOR_SETUP          0
#define DATA_STATE_XMIT         1
#define DATA_STATE_NEED_ZLP     2
#define WAIT_FOR_OUT_STATUS     3
#define DATA_STATE_RECV         4

/* ep tramsfer mode */
#define USBP_TM_CTL	0
#define USBP_TM_ISO	1
#define USBP_TM_BULK	2
#define USBP_TM_INT	3

/*-----------------------------------------------------------------------------
	USB RX And TX DATA Frame
 -----------------------------------------------------------------------------*/
struct qe_frame{
	u8 *data;
	u32 len;
	u32 status;
	u32 info;

	void *privdata;
	struct list_head node;
};

/* Frame structure, info field. */
#define PID_DATA0              0x80000000 /* Data toggle zero */
#define PID_DATA1              0x40000000 /* Data toggle one  */
#define PID_SETUP              0x20000000 /* setup bit */
#define SETUP_STATUS           0x10000000 /* setup status bit */
#define SETADDR_STATUS         0x08000000 /* setupup address status bit */
#define NO_REQ                 0x04000000 /* Frame without request */
#define HOST_DATA              0x02000000 /* Host data frame */
#define FIRST_PACKET_IN_FRAME  0x01000000 /* first packet in the frame */
#define TOKEN_FRAME            0x00800000 /* Host token frame */
#define ZLP                    0x00400000 /* Zero length packet */
#define IN_TOKEN_FRAME         0x00200000 /* In token package */
#define OUT_TOKEN_FRAME        0x00100000 /* Out token package */
#define SETUP_TOKEN_FRAME      0x00080000 /* Setup token package */
#define STALL_FRAME            0x00040000 /* Stall handshake */
#define NACK_FRAME             0x00020000 /* Nack handshake */
#define NO_PID                 0x00010000 /* No send PID */
#define NO_CRC                 0x00008000 /* No send CRC */
#define HOST_COMMAND           0x00004000 /* Host command frame   */

/* Frame status field */
/* Receive side */
#define FRAME_OK               0x00000000 /* Frame transmitted or received OK */
#define FRAME_ERROR            0x80000000 /* Error occurred on frame */
#define START_FRAME_LOST       0x40000000 /* START_FRAME_LOST */
#define END_FRAME_LOST         0x20000000 /* END_FRAME_LOST */
#define RX_ER_NONOCT           0x10000000 /* Rx Non Octet Aligned Packet */
#define RX_ER_BITSTUFF         0x08000000 /* Frame Aborted --Received packet
					     with bit stuff error */
#define RX_ER_CRC              0x04000000 /* Received packet with CRC error */
#define RX_ER_OVERUN           0x02000000 /* Over-run occurred on reception */
#define RX_ER_PID              0x01000000 /* Wrong PID received */
/* Tranmit side */
#define TX_ER_NAK              0x00800000 /* Received NAK handshake */
#define TX_ER_STALL            0x00400000 /* Received STALL handshake */
#define TX_ER_TIMEOUT          0x00200000 /* Transmit time out */
#define TX_ER_UNDERUN          0x00100000 /* Transmit underrun */
#define FRAME_INPROGRESS       0x00080000 /* Frame is being transmitted */
#define ER_DATA_UNDERUN        0x00040000 /* Frame is shorter then expected */
#define ER_DATA_OVERUN         0x00020000 /* Frame is longer then expected */

/* QE USB frame operation functions */
#define frame_get_length(frm) (frm->len)
#define frame_set_length(frm, leng) (frm->len = leng)
#define frame_get_data(frm) (frm->data)
#define frame_set_data(frm, dat) (frm->data = dat)
#define frame_get_info(frm) (frm->info)
#define frame_set_info(frm, inf) (frm->info = inf)
#define frame_get_status(frm) (frm->status)
#define frame_set_status(frm, stat) (frm->status = stat)
#define frame_get_privdata(frm) (frm->privdata)
#define frame_set_privdata(frm, dat) (frm->privdata = dat)

static inline void qe_frame_clean(struct qe_frame *frm)
{
	frame_set_data(frm, NULL);
	frame_set_length(frm, 0);
	frame_set_status(frm, FRAME_OK);
	frame_set_info(frm, 0);
	frame_set_privdata(frm, NULL);
}

static inline void qe_frame_init(struct qe_frame *frm)
{
	qe_frame_clean(frm);
	INIT_LIST_HEAD(&(frm->node));
}

struct qe_req {
	struct usb_request req;
	struct list_head queue;
	/* ep_queue() func will add
	 a request->queue into a udc_ep->queue 'd tail */
	struct qe_ep *ep;
	unsigned mapped:1;
};

struct qe_ep {
	struct usb_ep ep;
	struct list_head queue;
	struct qe_udc *udc;
	struct usb_gadget *gadget;

	u8 state;

	struct qe_bd __iomem *rxbase;
	struct qe_bd __iomem *n_rxbd;
	struct qe_bd __iomem *e_rxbd;

	struct qe_bd __iomem *txbase;
	struct qe_bd __iomem *n_txbd;
	struct qe_bd __iomem *c_txbd;

	struct qe_frame *rxframe;
	u8 *rxbuffer;
	dma_addr_t rxbuf_d;
	u8 rxbufmap;
	unsigned char localnack;
	int has_data;

	struct qe_frame *txframe;
	struct qe_req *tx_req;
	int sent;  /*data already sent */
	int last;  /*data sent in the last time*/

	u8 dir;
	u8 epnum;
	u8 tm; /* transfer mode */
	u8 data01;
	u8 init;

	u8 already_seen;
	u8 enable_tasklet;
	u8 setup_stage;
	u32 last_io;            /* timestamp */

	char name[14];

	unsigned double_buf:1;
	unsigned stopped:1;
	unsigned fnf:1;
	unsigned has_dma:1;

	u8 ackwait;
	u8 dma_channel;
	u16 dma_counter;
	int lch;

	struct timer_list timer;
};

struct qe_udc {
	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	struct device *dev;
	struct qe_ep eps[USB_MAX_ENDPOINTS];
	struct usb_ctrlrequest local_setup_buff;
	spinlock_t lock;	/* lock for set/config qe_udc */
	unsigned long soc_type;		/* QE or CPM soc */

	struct qe_req *status_req;	/* ep0 status request */

	/* USB and EP Parameter Block pointer */
	struct usb_device_para __iomem *usb_param;
	struct usb_ep_para __iomem *ep_param[4];

	u32 max_pipes;          /* Device max pipes */
	u32 max_use_endpts;     /* Max endpointes to be used */
	u32 bus_reset;          /* Device is bus reseting */
	u32 resume_state;       /* USB state to resume*/
	u32 usb_state;          /* USB current state */
	u32 usb_next_state;     /* USB next state */
	u32 ep0_state;          /* Enpoint zero state */
	u32 ep0_dir;            /* Enpoint zero direction: can be
				USB_DIR_IN or USB_DIR_OUT*/
	u32 usb_sof_count;      /* SOF count */
	u32 errors;             /* USB ERRORs count */

	u8 *tmpbuf;
	u32 c_start;
	u32 c_end;

	u8 *nullbuf;
	u8 *statusbuf;
	dma_addr_t nullp;
	u8 nullmap;
	u8 device_address;	/* Device USB address */

	unsigned int usb_clock;
	unsigned int usb_irq;
	struct usb_ctlr __iomem *usb_regs;

	struct tasklet_struct rx_tasklet;

	struct completion *done;	/* to make sure release() is done */
};

#define EP_STATE_IDLE	0
#define EP_STATE_NACK	1
#define EP_STATE_STALL	2

/*
 * transmit BD's status
 */
#define T_R           0x80000000         /* ready bit */
#define T_W           0x20000000         /* wrap bit */
#define T_I           0x10000000         /* interrupt on completion */
#define T_L           0x08000000         /* last */
#define T_TC          0x04000000         /* transmit CRC */
#define T_CNF         0x02000000         /* wait for  transmit confirm */
#define T_LSP         0x01000000         /* Low-speed transaction */
#define T_PID         0x00c00000         /* packet id */
#define T_NAK         0x00100000         /* No ack. */
#define T_STAL        0x00080000         /* Stall received */
#define T_TO          0x00040000         /* time out */
#define T_UN          0x00020000         /* underrun */

#define DEVICE_T_ERROR    (T_UN | T_TO)
#define HOST_T_ERROR      (T_UN | T_TO | T_NAK | T_STAL)
#define DEVICE_T_BD_MASK  DEVICE_T_ERROR
#define HOST_T_BD_MASK    HOST_T_ERROR

#define T_PID_SHIFT   6
#define T_PID_DATA0   0x00800000         /* Data 0 toggle */
#define T_PID_DATA1   0x00c00000         /* Data 1 toggle */

/*
 * receive BD's status
 */
#define R_E           0x80000000         /* buffer empty */
#define R_W           0x20000000         /* wrap bit */
#define R_I           0x10000000         /* interrupt on reception */
#define R_L           0x08000000         /* last */
#define R_F           0x04000000         /* first */
#define R_PID         0x00c00000         /* packet id */
#define R_NO          0x00100000         /* Rx Non Octet Aligned Packet */
#define R_AB          0x00080000         /* Frame Aborted */
#define R_CR          0x00040000         /* CRC Error */
#define R_OV          0x00020000         /* Overrun */

#define R_ERROR       (R_NO | R_AB | R_CR | R_OV)
#define R_BD_MASK     R_ERROR

#define R_PID_DATA0   0x00000000
#define R_PID_DATA1   0x00400000
#define R_PID_SETUP   0x00800000

#define CPM_USB_STOP_TX 0x2e600000
#define CPM_USB_RESTART_TX 0x2e600000
#define CPM_USB_STOP_TX_OPCODE 0x0a
#define CPM_USB_RESTART_TX_OPCODE 0x0b
#define CPM_USB_EP_SHIFT 5

#endif  /* __FSL_QE_UDC_H */
