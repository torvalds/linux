/*****************************************************************************
*
* Filename:      mcs7780.h
* Version:       0.2-alpha
* Description:   Irda MosChip USB Dongle
* Status:        Experimental
* Authors:       Lukasz Stelmach <stlman@poczta.fm>
*		 Brian Pugh <bpugh@cs.pdx.edu>
*
*       Copyright (C) 2005, Lukasz Stelmach <stlman@poczta.fm>
*       Copyright (C) 2005, Brian Pugh <bpugh@cs.pdx.edu>
*
*       This program is free software; you can redistribute it and/or modify
*       it under the terms of the GNU General Public License as published by
*       the Free Software Foundation; either version 2 of the License, or
*       (at your option) any later version.
*
*       This program is distributed in the hope that it will be useful,
*       but WITHOUT ANY WARRANTY; without even the implied warranty of
*       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*       GNU General Public License for more details.
*
*       You should have received a copy of the GNU General Public License
*       along with this program; if not, write to the Free Software
*       Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/
#ifndef _MCS7780_H
#define _MCS7780_H

#define MCS_MODE_SIR		0
#define MCS_MODE_MIR		1
#define MCS_MODE_FIR		2

#define MCS_CTRL_TIMEOUT	500
#define MCS_XMIT_TIMEOUT	500
/* Possible transceiver types */
#define MCS_TSC_VISHAY		0	/* Vishay TFD, default choice */
#define MCS_TSC_AGILENT		1	/* Agilent 3602/3600 */
#define MCS_TSC_SHARP		2	/* Sharp GP2W1000YP */

/* Requests */
#define MCS_RD_RTYPE 0xC0
#define MCS_WR_RTYPE 0x40
#define MCS_RDREQ    0x0F
#define MCS_WRREQ    0x0E

/* Register 0x00 */
#define MCS_MODE_REG	0
#define MCS_FIR		((__u16)0x0001)
#define MCS_SIR16US	((__u16)0x0002)
#define MCS_BBTG	((__u16)0x0004)
#define MCS_ASK		((__u16)0x0008)
#define MCS_PARITY	((__u16)0x0010)

/* SIR/MIR speed constants */
#define MCS_SPEED_SHIFT	    5
#define MCS_SPEED_MASK	    ((__u16)0x00E0)
#define MCS_SPEED(x)	    ((x & MCS_SPEED_MASK) >> MCS_SPEED_SHIFT)
#define MCS_SPEED_2400	    ((0 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)
#define MCS_SPEED_9600	    ((1 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)
#define MCS_SPEED_19200	    ((2 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)
#define MCS_SPEED_38400	    ((3 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)
#define MCS_SPEED_57600	    ((4 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)
#define MCS_SPEED_115200    ((5 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)
#define MCS_SPEED_576000    ((6 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)
#define MCS_SPEED_1152000   ((7 << MCS_SPEED_SHIFT) & MCS_SPEED_MASK)

#define MCS_PLLPWDN	((__u16)0x0100)
#define MCS_DRIVER	((__u16)0x0200)
#define MCS_DTD		((__u16)0x0400)
#define MCS_DIR		((__u16)0x0800)
#define MCS_SIPEN	((__u16)0x1000)
#define MCS_SENDSIP	((__u16)0x2000)
#define MCS_CHGDIR	((__u16)0x4000)
#define MCS_RESET	((__u16)0x8000)

/* Register 0x02 */
#define MCS_XCVR_REG	2
#define MCS_MODE0	((__u16)0x0001)
#define MCS_STFIR	((__u16)0x0002)
#define MCS_XCVR_CONF	((__u16)0x0004)
#define MCS_RXFAST	((__u16)0x0008)
/* TXCUR [6:4] */
#define MCS_TXCUR_SHIFT	4
#define MCS_TXCUR_MASK	((__u16)0x0070)
#define MCS_TXCUR(x)	((x & MCS_TXCUR_MASK) >> MCS_TXCUR_SHIFT)
#define MCS_SETTXCUR(x,y) \
	((x & ~MCS_TXCUR_MASK) | (y << MCS_TXCUR_SHIFT) & MCS_TXCUR_MASK)

#define MCS_MODE1	((__u16)0x0080)
#define MCS_SMODE0	((__u16)0x0100)
#define MCS_SMODE1	((__u16)0x0200)
#define MCS_INVTX	((__u16)0x0400)
#define MCS_INVRX	((__u16)0x0800)

#define MCS_MINRXPW_REG	4

#define MCS_RESV_REG 7
#define MCS_IRINTX	((__u16)0x0001)
#define MCS_IRINRX	((__u16)0x0002)

struct mcs_cb {
	struct usb_device *usbdev;	/* init: probe_irda */
	struct net_device *netdev;	/* network layer */
	struct irlap_cb *irlap;	/* The link layer we are binded to */
	struct qos_info qos;
	unsigned int speed;	/* Current speed */
	unsigned int new_speed;	/* new speed */

	struct work_struct work; /* Change speed work */

	struct sk_buff *tx_pending;
	char in_buf[4096];	/* transmit/receive buffer */
	char out_buf[4096];	/* transmit/receive buffer */
	__u8 *fifo_status;

	iobuff_t rx_buff;	/* receive unwrap state machine */
	struct timeval rx_time;
	spinlock_t lock;
	int receiving;

	__u8 ep_in;
	__u8 ep_out;

	struct urb *rx_urb;
	struct urb *tx_urb;

	int transceiver_type;
	int sir_tweak;
	int receive_mode;
};

static int mcs_set_reg(struct mcs_cb *mcs, __u16 reg, __u16 val);
static int mcs_get_reg(struct mcs_cb *mcs, __u16 reg, __u16 * val);

static inline int mcs_setup_transceiver_vishay(struct mcs_cb *mcs);
static inline int mcs_setup_transceiver_agilent(struct mcs_cb *mcs);
static inline int mcs_setup_transceiver_sharp(struct mcs_cb *mcs);
static inline int mcs_setup_transceiver(struct mcs_cb *mcs);
static inline int mcs_wrap_sir_skb(struct sk_buff *skb, __u8 * buf);
static unsigned mcs_wrap_fir_skb(const struct sk_buff *skb, __u8 *buf);
static unsigned mcs_wrap_mir_skb(const struct sk_buff *skb, __u8 *buf);
static void mcs_unwrap_mir(struct mcs_cb *mcs, __u8 *buf, int len);
static void mcs_unwrap_fir(struct mcs_cb *mcs, __u8 *buf, int len);
static inline int mcs_setup_urbs(struct mcs_cb *mcs);
static inline int mcs_receive_start(struct mcs_cb *mcs);
static inline int mcs_find_endpoints(struct mcs_cb *mcs,
				     struct usb_host_endpoint *ep, int epnum);

static int mcs_speed_change(struct mcs_cb *mcs);

static int mcs_net_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd);
static int mcs_net_close(struct net_device *netdev);
static int mcs_net_open(struct net_device *netdev);

static void mcs_receive_irq(struct urb *urb);
static void mcs_send_irq(struct urb *urb);
static netdev_tx_t mcs_hard_xmit(struct sk_buff *skb,
				       struct net_device *netdev);

static int mcs_probe(struct usb_interface *intf,
		     const struct usb_device_id *id);
static void mcs_disconnect(struct usb_interface *intf);

#endif				/* _MCS7780_H */
