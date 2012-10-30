/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCMF_BUS_H_
#define _BRCMF_BUS_H_

/* The level of bus communication with the dongle */
enum brcmf_bus_state {
	BRCMF_BUS_DOWN,		/* Not ready for frame transfers */
	BRCMF_BUS_LOAD,		/* Download access only (CPU reset) */
	BRCMF_BUS_DATA		/* Ready for frame transfers */
};

struct dngl_stats {
	unsigned long rx_packets;	/* total packets received */
	unsigned long tx_packets;	/* total packets transmitted */
	unsigned long rx_bytes;	/* total bytes received */
	unsigned long tx_bytes;	/* total bytes transmitted */
	unsigned long rx_errors;	/* bad packets received */
	unsigned long tx_errors;	/* packet transmit problems */
	unsigned long rx_dropped;	/* packets dropped by dongle */
	unsigned long tx_dropped;	/* packets dropped by dongle */
	unsigned long multicast;	/* multicast packets received */
};

struct brcmf_bus_dcmd {
	char *name;
	char *param;
	int param_len;
	struct list_head list;
};

/* interface structure between common and bus layer */
struct brcmf_bus {
	u8 type;		/* bus type */
	union {
		struct brcmf_sdio_dev *sdio;
		struct brcmf_usbdev *usb;
	} bus_priv;
	struct brcmf_pub *drvr;	/* pointer to driver pub structure brcmf_pub */
	enum brcmf_bus_state state;
	uint maxctl;		/* Max size rxctl request from proto to bus */
	bool drvr_up;		/* Status flag of driver up/down */
	unsigned long tx_realloc;	/* Tx packets realloced for headroom */
	struct dngl_stats dstats;	/* Stats for dongle-based data */
	u8 align;		/* bus alignment requirement */
	struct list_head dcmd_list;

	/* interface functions pointers */
	/* Stop bus module: clear pending frames, disable data flow */
	void (*brcmf_bus_stop)(struct device *);
	/* Initialize bus module: prepare for communication w/dongle */
	int (*brcmf_bus_init)(struct device *);
	/* Send a data frame to the dongle.  Callee disposes of txp. */
	int (*brcmf_bus_txdata)(struct device *, struct sk_buff *);
	/* Send/receive a control message to/from the dongle.
	 * Expects caller to enforce a single outstanding transaction.
	 */
	int (*brcmf_bus_txctl)(struct device *, unsigned char *, uint);
	int (*brcmf_bus_rxctl)(struct device *, unsigned char *, uint);
};

/*
 * interface functions from common layer
 */

/* Remove any protocol-specific data header. */
extern int brcmf_proto_hdrpull(struct device *dev, int *ifidx,
			       struct sk_buff *rxp);

extern bool brcmf_c_prec_enq(struct device *dev, struct pktq *q,
			 struct sk_buff *pkt, int prec);

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
extern void brcmf_rx_frame(struct device *dev, int ifidx,
			   struct sk_buff_head *rxlist);
static inline void brcmf_rx_packet(struct device *dev, int ifidx,
				   struct sk_buff *pkt)
{
	struct sk_buff_head q;

	skb_queue_head_init(&q);
	skb_queue_tail(&q, pkt);
	brcmf_rx_frame(dev, ifidx, &q);
}

/* Indication from bus module regarding presence/insertion of dongle. */
extern int brcmf_attach(uint bus_hdrlen, struct device *dev);
/* Indication from bus module regarding removal/absence of dongle */
extern void brcmf_detach(struct device *dev);

/* Indication from bus module to change flow-control state */
extern void brcmf_txflowblock(struct device *dev, bool state);

/* Notify tx completion */
extern void brcmf_txcomplete(struct device *dev, struct sk_buff *txp,
			     bool success);

extern int brcmf_bus_start(struct device *dev);

#ifdef CONFIG_BRCMFMAC_SDIO
extern void brcmf_sdio_exit(void);
extern void brcmf_sdio_init(void);
#endif
#ifdef CONFIG_BRCMFMAC_USB
extern void brcmf_usb_exit(void);
extern void brcmf_usb_init(void);
#endif

#endif				/* _BRCMF_BUS_H_ */
