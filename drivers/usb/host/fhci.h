// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale QUICC Engine USB Host Controller Driver
 *
 * Copyright (c) Freescale Semicondutor, Inc. 2006.
 *               Shlomi Gridish <gridish@freescale.com>
 *               Jerry Huang <Chang-Ming.Huang@freescale.com>
 * Copyright (c) Logic Product Development, Inc. 2007
 *               Peter Barada <peterb@logicpd.com>
 * Copyright (c) MontaVista Software, Inc. 2008.
 *               Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __FHCI_H
#define __FHCI_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <soc/fsl/qe/qe.h>
#include <soc/fsl/qe/immap_qe.h>

#define USB_CLOCK	48000000

#define FHCI_PRAM_SIZE 0x100

#define MAX_EDS		32
#define MAX_TDS		32


/* CRC16 field size */
#define CRC_SIZE 2

/* USB protocol overhead for each frame transmitted from the host */
#define PROTOCOL_OVERHEAD 7

/* Packet structure, info field */
#define PKT_PID_DATA0		0x80000000 /* PID - Data toggle zero */
#define PKT_PID_DATA1		0x40000000 /* PID - Data toggle one  */
#define PKT_PID_SETUP		0x20000000 /* PID - Setup bit */
#define PKT_SETUP_STATUS	0x10000000 /* Setup status bit */
#define PKT_SETADDR_STATUS	0x08000000 /* Set address status bit */
#define PKT_SET_HOST_LAST	0x04000000 /* Last data packet */
#define PKT_HOST_DATA		0x02000000 /* Data packet */
#define PKT_FIRST_IN_FRAME	0x01000000 /* First packet in the frame */
#define PKT_TOKEN_FRAME		0x00800000 /* Token packet */
#define PKT_ZLP			0x00400000 /* Zero length packet */
#define PKT_IN_TOKEN_FRAME	0x00200000 /* IN token packet */
#define PKT_OUT_TOKEN_FRAME	0x00100000 /* OUT token packet */
#define PKT_SETUP_TOKEN_FRAME	0x00080000 /* SETUP token packet */
#define PKT_STALL_FRAME		0x00040000 /* STALL packet */
#define PKT_NACK_FRAME		0x00020000 /* NACK packet */
#define PKT_NO_PID		0x00010000 /* No PID */
#define PKT_NO_CRC		0x00008000 /* don't append CRC */
#define PKT_HOST_COMMAND	0x00004000 /* Host command packet */
#define PKT_DUMMY_PACKET	0x00002000 /* Dummy packet, used for mmm */
#define PKT_LOW_SPEED_PACKET	0x00001000 /* Low-Speed packet */

#define TRANS_OK		(0)
#define TRANS_INPROGRESS	(-1)
#define TRANS_DISCARD		(-2)
#define TRANS_FAIL		(-3)

#define PS_INT		0
#define PS_DISCONNECTED	1
#define PS_CONNECTED	2
#define PS_READY	3
#define PS_MISSING	4

/* Transfer Descriptor status field */
#define USB_TD_OK		0x00000000 /* TD transmited or received ok */
#define USB_TD_INPROGRESS	0x80000000 /* TD is being transmitted */
#define USB_TD_RX_ER_NONOCT	0x40000000 /* Tx Non Octet Aligned Packet */
#define USB_TD_RX_ER_BITSTUFF	0x20000000 /* Frame Aborted-Received pkt */
#define USB_TD_RX_ER_CRC	0x10000000 /* CRC error */
#define USB_TD_RX_ER_OVERUN	0x08000000 /* Over - run occurred */
#define USB_TD_RX_ER_PID	0x04000000 /* wrong PID received */
#define USB_TD_RX_DATA_UNDERUN	0x02000000 /* shorter than expected */
#define USB_TD_RX_DATA_OVERUN	0x01000000 /* longer than expected */
#define USB_TD_TX_ER_NAK	0x00800000 /* NAK handshake */
#define USB_TD_TX_ER_STALL	0x00400000 /* STALL handshake */
#define USB_TD_TX_ER_TIMEOUT	0x00200000 /* transmit time out */
#define USB_TD_TX_ER_UNDERUN	0x00100000 /* transmit underrun */

#define USB_TD_ERROR (USB_TD_RX_ER_NONOCT | USB_TD_RX_ER_BITSTUFF | \
		USB_TD_RX_ER_CRC | USB_TD_RX_ER_OVERUN | USB_TD_RX_ER_PID | \
		USB_TD_RX_DATA_UNDERUN | USB_TD_RX_DATA_OVERUN | \
		USB_TD_TX_ER_NAK | USB_TD_TX_ER_STALL | \
		USB_TD_TX_ER_TIMEOUT | USB_TD_TX_ER_UNDERUN)

/* Transfer Descriptor toggle field */
#define USB_TD_TOGGLE_DATA0	0
#define USB_TD_TOGGLE_DATA1	1
#define USB_TD_TOGGLE_CARRY	2

/* #define MULTI_DATA_BUS */

/* Bus mode register RBMR/TBMR */
#define BUS_MODE_GBL	0x20	/* Global snooping */
#define BUS_MODE_BO	0x18	/* Byte ordering */
#define BUS_MODE_BO_BE	0x10	/* Byte ordering - Big-endian */
#define BUS_MODE_DTB	0x02	/* Data bus */

/* FHCI QE USB Register Description */

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

/* Freescale USB HOST */
struct fhci_pram {
	__be16 ep_ptr[4];	/* Endpoint porter reg */
	__be32 rx_state;	/* Rx internal state */
	__be32 rx_ptr;		/* Rx internal data pointer */
	__be16 frame_num;	/* Frame number */
	__be16 rx_cnt;		/* Rx byte count */
	__be32 rx_temp;		/* Rx temp */
	__be32 rx_data_temp;	/* Rx data temp */
	__be16 rx_u_ptr;	/* Rx microcode return address temp */
	u8 reserved1[2];	/* reserved area */
	__be32 sof_tbl;		/* SOF lookup table pointer */
	u8 sof_u_crc_temp;	/* SOF micorcode CRC5 temp reg */
	u8 reserved2[0xdb];
};

/* Freescale USB Endpoint*/
struct fhci_ep_pram {
	__be16 rx_base;		/* Rx BD base address */
	__be16 tx_base;		/* Tx BD base address */
	u8 rx_func_code;	/* Rx function code */
	u8 tx_func_code;	/* Tx function code */
	__be16 rx_buff_len;	/* Rx buffer length */
	__be16 rx_bd_ptr;	/* Rx BD pointer */
	__be16 tx_bd_ptr;	/* Tx BD pointer */
	__be32 tx_state;	/* Tx internal state */
	__be32 tx_ptr;		/* Tx internal data pointer */
	__be16 tx_crc;		/* temp transmit CRC */
	__be16 tx_cnt;		/* Tx byte count */
	__be32 tx_temp;		/* Tx temp */
	__be16 tx_u_ptr;	/* Tx microcode return address temp */
	__be16 reserved;
};

struct fhci_controller_list {
	struct list_head ctrl_list;	/* control endpoints */
	struct list_head bulk_list;	/* bulk endpoints */
	struct list_head iso_list;	/* isochronous endpoints */
	struct list_head intr_list;	/* interruput endpoints */
	struct list_head done_list;	/* done transfers */
};

struct virtual_root_hub {
	int dev_num;	/* USB address of the root hub */
	u32 feature;	/* indicates what feature has been set */
	struct usb_hub_status hub;
	struct usb_port_status port;
};

enum fhci_gpios {
	GPIO_USBOE = 0,
	GPIO_USBTP,
	GPIO_USBTN,
	GPIO_USBRP,
	GPIO_USBRN,
	/* these are optional */
	GPIO_SPEED,
	GPIO_POWER,
	NUM_GPIOS,
};

enum fhci_pins {
	PIN_USBOE = 0,
	PIN_USBTP,
	PIN_USBTN,
	NUM_PINS,
};

struct fhci_hcd {
	enum qe_clock fullspeed_clk;
	enum qe_clock lowspeed_clk;
	struct qe_pin *pins[NUM_PINS];
	int gpios[NUM_GPIOS];
	bool alow_gpios[NUM_GPIOS];

	struct qe_usb_ctlr __iomem *regs; /* I/O memory used to communicate */
	struct fhci_pram __iomem *pram;	/* Parameter RAM */
	struct gtm_timer *timer;

	spinlock_t lock;
	struct fhci_usb *usb_lld; /* Low-level driver */
	struct virtual_root_hub *vroot_hub; /* the virtual root hub */
	int active_urbs;
	struct fhci_controller_list *hc_list;
	struct tasklet_struct *process_done_task; /* tasklet for done list */

	struct list_head empty_eds;
	struct list_head empty_tds;

#ifdef CONFIG_FHCI_DEBUG
	int usb_irq_stat[13];
	struct dentry *dfs_root;
	struct dentry *dfs_regs;
	struct dentry *dfs_irq_stat;
#endif
};

#define USB_FRAME_USAGE 90
#define FRAME_TIME_USAGE (USB_FRAME_USAGE*10)	/* frame time usage */
#define SW_FIX_TIME_BETWEEN_TRANSACTION 150	/* SW */
#define MAX_BYTES_PER_FRAME (USB_FRAME_USAGE*15)
#define MAX_PERIODIC_FRAME_USAGE 90

/* transaction type */
enum fhci_ta_type {
	FHCI_TA_IN = 0,	/* input transaction */
	FHCI_TA_OUT,	/* output transaction */
	FHCI_TA_SETUP,	/* setup transaction */
};

/* transfer mode */
enum fhci_tf_mode {
	FHCI_TF_CTRL = 0,
	FHCI_TF_ISO,
	FHCI_TF_BULK,
	FHCI_TF_INTR,
};

enum fhci_speed {
	FHCI_FULL_SPEED,
	FHCI_LOW_SPEED,
};

/* endpoint state */
enum fhci_ed_state {
	FHCI_ED_NEW = 0, /* pipe is new */
	FHCI_ED_OPER,    /* pipe is operating */
	FHCI_ED_URB_DEL, /* pipe is in hold because urb is being deleted */
	FHCI_ED_SKIP,    /* skip this pipe */
	FHCI_ED_HALTED,  /* pipe is halted */
};

enum fhci_port_status {
	FHCI_PORT_POWER_OFF = 0,
	FHCI_PORT_DISABLED,
	FHCI_PORT_DISCONNECTING,
	FHCI_PORT_WAITING,	/* waiting for connection */
	FHCI_PORT_FULL,		/* full speed connected */
	FHCI_PORT_LOW,		/* low speed connected */
};

enum fhci_mem_alloc {
	MEM_CACHABLE_SYS = 0x00000001,	/* primary DDR,cachable */
	MEM_NOCACHE_SYS = 0x00000004,	/* primary DDR,non-cachable */
	MEM_SECONDARY = 0x00000002,	/* either secondary DDR or SDRAM */
	MEM_PRAM = 0x00000008,		/* multi-user RAM identifier */
};

/* USB default parameters*/
#define DEFAULT_RING_LEN	8
#define DEFAULT_DATA_MEM	MEM_CACHABLE_SYS

struct ed {
	u8 dev_addr;		/* device address */
	u8 ep_addr;		/* endpoint address */
	enum fhci_tf_mode mode;	/* USB transfer mode */
	enum fhci_speed speed;
	unsigned int max_pkt_size;
	enum fhci_ed_state state;
	struct list_head td_list; /* a list of all queued TD to this pipe */
	struct list_head node;

	/* read only parameters, should be cleared upon initialization */
	u8 toggle_carry;	/* toggle carry from the last TD submitted */
	u16 next_iso;		/* time stamp of next queued ISO transfer */
	struct td *td_head;	/* a pointer to the current TD handled */
};

struct td {
	void *data;		 /* a pointer to the data buffer */
	unsigned int len;	 /* length of the data to be submitted */
	unsigned int actual_len; /* actual bytes transferred on this td */
	enum fhci_ta_type type;	 /* transaction type */
	u8 toggle;		 /* toggle for next trans. within this TD */
	u16 iso_index;		 /* ISO transaction index */
	u16 start_frame;	 /* start frame time stamp */
	u16 interval;		 /* interval between trans. (for ISO/Intr) */
	u32 status;		 /* status of the TD */
	struct ed *ed;		 /* a handle to the corresponding ED */
	struct urb *urb;	 /* a handle to the corresponding URB */
	bool ioc;		 /* Inform On Completion */
	struct list_head node;

	/* read only parameters should be cleared upon initialization */
	struct packet *pkt;
	int nak_cnt;
	int error_cnt;
	struct list_head frame_lh;
};

struct packet {
	u8 *data;	/* packet data */
	u32 len;	/* packet length */
	u32 status;	/* status of the packet - equivalent to the status
			 * field for the corresponding structure td */
	u32 info;	/* packet information */
	void __iomem *priv_data; /* private data of the driver (TDs or BDs) */
};

/* struct for each URB */
#define URB_INPROGRESS	0
#define URB_DEL		1

/* URB states (state field) */
#define US_BULK		0
#define US_BULK0	1

/* three setup states */
#define US_CTRL_SETUP	2
#define US_CTRL_DATA	1
#define US_CTRL_ACK	0

#define EP_ZERO	0

struct urb_priv {
	int num_of_tds;
	int tds_cnt;
	int state;

	struct td **tds;
	struct ed *ed;
	struct timer_list time_out;
};

struct endpoint {
	/* Pointer to ep parameter RAM */
	struct fhci_ep_pram __iomem *ep_pram_ptr;

	/* Host transactions */
	struct usb_td __iomem *td_base; /* first TD in the ring */
	struct usb_td __iomem *conf_td; /* next TD for confirm after transac */
	struct usb_td __iomem *empty_td;/* next TD for new transaction req. */
	struct kfifo empty_frame_Q;  /* Empty frames list to use */
	struct kfifo conf_frame_Q;   /* frames passed to TDs,waiting for tx */
	struct kfifo dummy_packets_Q;/* dummy packets for the CRC overun */

	bool already_pushed_dummy_bd;
};

/* struct for each 1mSec frame time */
#define FRAME_IS_TRANSMITTED		0x00
#define FRAME_TIMER_END_TRANSMISSION	0x01
#define FRAME_DATA_END_TRANSMISSION	0x02
#define FRAME_END_TRANSMISSION		0x03
#define FRAME_IS_PREPARED		0x04

struct fhci_time_frame {
	u16 frame_num;	 /* frame number */
	u16 total_bytes; /* total bytes submitted within this frame */
	u8 frame_status; /* flag that indicates to stop fill this frame */
	struct list_head tds_list; /* all tds of this frame */
};

/* internal driver structure*/
struct fhci_usb {
	u16 saved_msk;		 /* saving of the USB mask register */
	struct endpoint *ep0;	 /* pointer for endpoint0 structure */
	int intr_nesting_cnt;	 /* interrupt nesting counter */
	u16 max_frame_usage;	 /* max frame time usage,in micro-sec */
	u16 max_bytes_per_frame; /* max byte can be tx in one time frame */
	u32 sw_transaction_time; /* sw complete trans time,in micro-sec */
	struct fhci_time_frame *actual_frame;
	struct fhci_controller_list *hc_list;	/* main structure for hc */
	struct virtual_root_hub *vroot_hub;
	enum fhci_port_status port_status;	/* v_rh port status */

	u32 (*transfer_confirm)(struct fhci_hcd *fhci);

	struct fhci_hcd *fhci;
};

/*
 * Various helpers and prototypes below.
 */

static inline u16 get_frame_num(struct fhci_hcd *fhci)
{
	return in_be16(&fhci->pram->frame_num) & 0x07ff;
}

#define fhci_dbg(fhci, fmt, args...) \
		dev_dbg(fhci_to_hcd(fhci)->self.controller, fmt, ##args)
#define fhci_vdbg(fhci, fmt, args...) \
		dev_vdbg(fhci_to_hcd(fhci)->self.controller, fmt, ##args)
#define fhci_err(fhci, fmt, args...) \
		dev_err(fhci_to_hcd(fhci)->self.controller, fmt, ##args)
#define fhci_info(fhci, fmt, args...) \
		dev_info(fhci_to_hcd(fhci)->self.controller, fmt, ##args)
#define fhci_warn(fhci, fmt, args...) \
		dev_warn(fhci_to_hcd(fhci)->self.controller, fmt, ##args)

static inline struct fhci_hcd *hcd_to_fhci(struct usb_hcd *hcd)
{
	return (struct fhci_hcd *)hcd->hcd_priv;
}

static inline struct usb_hcd *fhci_to_hcd(struct fhci_hcd *fhci)
{
	return container_of((void *)fhci, struct usb_hcd, hcd_priv);
}

/* fifo of pointers */
static inline int cq_new(struct kfifo *fifo, int size)
{
	return kfifo_alloc(fifo, size * sizeof(void *), GFP_KERNEL);
}

static inline void cq_delete(struct kfifo *kfifo)
{
	kfifo_free(kfifo);
}

static inline unsigned int cq_howmany(struct kfifo *kfifo)
{
	return kfifo_len(kfifo) / sizeof(void *);
}

static inline int cq_put(struct kfifo *kfifo, void *p)
{
	return kfifo_in(kfifo, (void *)&p, sizeof(p));
}

static inline void *cq_get(struct kfifo *kfifo)
{
	unsigned int sz;
	void *p;

	sz = kfifo_out(kfifo, (void *)&p, sizeof(p));
	if (sz != sizeof(p))
		return NULL;

	return p;
}

/* fhci-hcd.c */
void fhci_start_sof_timer(struct fhci_hcd *fhci);
void fhci_stop_sof_timer(struct fhci_hcd *fhci);
u16 fhci_get_sof_timer_count(struct fhci_usb *usb);
void fhci_usb_enable_interrupt(struct fhci_usb *usb);
void fhci_usb_disable_interrupt(struct fhci_usb *usb);
int fhci_ioports_check_bus_state(struct fhci_hcd *fhci);

/* fhci-mem.c */
void fhci_recycle_empty_td(struct fhci_hcd *fhci, struct td *td);
void fhci_recycle_empty_ed(struct fhci_hcd *fhci, struct ed *ed);
struct ed *fhci_get_empty_ed(struct fhci_hcd *fhci);
struct td *fhci_td_fill(struct fhci_hcd *fhci, struct urb *urb,
			struct urb_priv *urb_priv, struct ed *ed, u16 index,
			enum fhci_ta_type type, int toggle, u8 *data, u32 len,
			u16 interval, u16 start_frame, bool ioc);
void fhci_add_tds_to_ed(struct ed *ed, struct td **td_list, int number);

/* fhci-hub.c */
void fhci_config_transceiver(struct fhci_hcd *fhci,
			enum fhci_port_status status);
void fhci_port_disable(struct fhci_hcd *fhci);
void fhci_port_enable(void *lld);
void fhci_io_port_generate_reset(struct fhci_hcd *fhci);
void fhci_port_reset(void *lld);
int fhci_hub_status_data(struct usb_hcd *hcd, char *buf);
int fhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
		     u16 wIndex, char *buf, u16 wLength);

/* fhci-tds.c */
void fhci_flush_bds(struct fhci_usb *usb);
void fhci_flush_actual_frame(struct fhci_usb *usb);
u32 fhci_host_transaction(struct fhci_usb *usb, struct packet *pkt,
			  enum fhci_ta_type trans_type, u8 dest_addr,
			  u8 dest_ep, enum fhci_tf_mode trans_mode,
			  enum fhci_speed dest_speed, u8 data_toggle);
void fhci_host_transmit_actual_frame(struct fhci_usb *usb);
void fhci_tx_conf_interrupt(struct fhci_usb *usb);
void fhci_push_dummy_bd(struct endpoint *ep);
u32 fhci_create_ep(struct fhci_usb *usb, enum fhci_mem_alloc data_mem,
		   u32 ring_len);
void fhci_init_ep_registers(struct fhci_usb *usb,
			    struct endpoint *ep,
			    enum fhci_mem_alloc data_mem);
void fhci_ep0_free(struct fhci_usb *usb);

/* fhci-sched.c */
extern struct tasklet_struct fhci_tasklet;
void fhci_transaction_confirm(struct fhci_usb *usb, struct packet *pkt);
void fhci_flush_all_transmissions(struct fhci_usb *usb);
void fhci_schedule_transactions(struct fhci_usb *usb);
void fhci_device_connected_interrupt(struct fhci_hcd *fhci);
void fhci_device_disconnected_interrupt(struct fhci_hcd *fhci);
void fhci_queue_urb(struct fhci_hcd *fhci, struct urb *urb);
u32 fhci_transfer_confirm_callback(struct fhci_hcd *fhci);
irqreturn_t fhci_irq(struct usb_hcd *hcd);
irqreturn_t fhci_frame_limit_timer_irq(int irq, void *_hcd);

/* fhci-q.h */
void fhci_urb_complete_free(struct fhci_hcd *fhci, struct urb *urb);
struct td *fhci_remove_td_from_ed(struct ed *ed);
struct td *fhci_remove_td_from_frame(struct fhci_time_frame *frame);
void fhci_move_td_from_ed_to_done_list(struct fhci_usb *usb, struct ed *ed);
struct td *fhci_peek_td_from_frame(struct fhci_time_frame *frame);
void fhci_add_td_to_frame(struct fhci_time_frame *frame, struct td *td);
struct td *fhci_remove_td_from_done_list(struct fhci_controller_list *p_list);
void fhci_done_td(struct urb *urb, struct td *td);
void fhci_del_ed_list(struct fhci_hcd *fhci, struct ed *ed);

#ifdef CONFIG_FHCI_DEBUG

void fhci_dbg_isr(struct fhci_hcd *fhci, int usb_er);
void fhci_dfs_destroy(struct fhci_hcd *fhci);
void fhci_dfs_create(struct fhci_hcd *fhci);

#else

static inline void fhci_dbg_isr(struct fhci_hcd *fhci, int usb_er) {}
static inline void fhci_dfs_destroy(struct fhci_hcd *fhci) {}
static inline void fhci_dfs_create(struct fhci_hcd *fhci) {}

#endif /* CONFIG_FHCI_DEBUG */

#endif /* __FHCI_H */
