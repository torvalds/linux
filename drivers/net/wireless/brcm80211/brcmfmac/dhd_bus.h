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

#include "dhd_dbg.h"

/* IDs of the 6 default common rings of msgbuf protocol */
#define BRCMF_H2D_MSGRING_CONTROL_SUBMIT	0
#define BRCMF_H2D_MSGRING_RXPOST_SUBMIT		1
#define BRCMF_D2H_MSGRING_CONTROL_COMPLETE	2
#define BRCMF_D2H_MSGRING_TX_COMPLETE		3
#define BRCMF_D2H_MSGRING_RX_COMPLETE		4

#define BRCMF_NROF_H2D_COMMON_MSGRINGS		2
#define BRCMF_NROF_D2H_COMMON_MSGRINGS		3
#define BRCMF_NROF_COMMON_MSGRINGS	(BRCMF_NROF_H2D_COMMON_MSGRINGS + \
					 BRCMF_NROF_D2H_COMMON_MSGRINGS)

/* The level of bus communication with the dongle */
enum brcmf_bus_state {
	BRCMF_BUS_UNKNOWN,	/* Not determined yet */
	BRCMF_BUS_NOMEDIUM,	/* No medium access to dongle */
	BRCMF_BUS_DOWN,		/* Not ready for frame transfers */
	BRCMF_BUS_LOAD,		/* Download access only (CPU reset) */
	BRCMF_BUS_DATA		/* Ready for frame transfers */
};

/* The level of bus communication with the dongle */
enum brcmf_bus_protocol_type {
	BRCMF_PROTO_BCDC,
	BRCMF_PROTO_MSGBUF
};

struct brcmf_bus_dcmd {
	char *name;
	char *param;
	int param_len;
	struct list_head list;
};

/**
 * struct brcmf_bus_ops - bus callback operations.
 *
 * @preinit: execute bus/device specific dongle init commands (optional).
 * @init: prepare for communication with dongle.
 * @stop: clear pending frames, disable data flow.
 * @txdata: send a data frame to the dongle. When the data
 *	has been transferred, the common driver must be
 *	notified using brcmf_txcomplete(). The common
 *	driver calls this function with interrupts
 *	disabled.
 * @txctl: transmit a control request message to dongle.
 * @rxctl: receive a control response message from dongle.
 * @gettxq: obtain a reference of bus transmit queue (optional).
 *
 * This structure provides an abstract interface towards the
 * bus specific driver. For control messages to common driver
 * will assure there is only one active transaction. Unless
 * indicated otherwise these callbacks are mandatory.
 */
struct brcmf_bus_ops {
	int (*preinit)(struct device *dev);
	void (*stop)(struct device *dev);
	int (*txdata)(struct device *dev, struct sk_buff *skb);
	int (*txctl)(struct device *dev, unsigned char *msg, uint len);
	int (*rxctl)(struct device *dev, unsigned char *msg, uint len);
	struct pktq * (*gettxq)(struct device *dev);
};


/**
 * struct brcmf_bus_msgbuf - bus ringbuf if in case of msgbuf.
 *
 * @commonrings: commonrings which are always there.
 * @flowrings: commonrings which are dynamically created and destroyed for data.
 * @rx_dataoffset: if set then all rx data has this this offset.
 * @max_rxbufpost: maximum number of buffers to post for rx.
 * @nrof_flowrings: number of flowrings.
 */
struct brcmf_bus_msgbuf {
	struct brcmf_commonring *commonrings[BRCMF_NROF_COMMON_MSGRINGS];
	struct brcmf_commonring **flowrings;
	u32 rx_dataoffset;
	u32 max_rxbufpost;
	u32 nrof_flowrings;
};


/**
 * struct brcmf_bus - interface structure between common and bus layer
 *
 * @bus_priv: pointer to private bus device.
 * @proto_type: protocol type, bcdc or msgbuf
 * @dev: device pointer of bus device.
 * @drvr: public driver information.
 * @state: operational state of the bus interface.
 * @maxctl: maximum size for rxctl request message.
 * @tx_realloc: number of tx packets realloced for headroom.
 * @dstats: dongle-based statistical data.
 * @dcmd_list: bus/device specific dongle initialization commands.
 * @chip: device identifier of the dongle chip.
 * @chiprev: revision of the dongle chip.
 */
struct brcmf_bus {
	union {
		struct brcmf_sdio_dev *sdio;
		struct brcmf_usbdev *usb;
	} bus_priv;
	enum brcmf_bus_protocol_type proto_type;
	struct device *dev;
	struct brcmf_pub *drvr;
	enum brcmf_bus_state state;
	uint maxctl;
	unsigned long tx_realloc;
	u32 chip;
	u32 chiprev;
	bool always_use_fws_queue;

	struct brcmf_bus_ops *ops;
	struct brcmf_bus_msgbuf *msgbuf;
};

/*
 * callback wrappers
 */
static inline int brcmf_bus_preinit(struct brcmf_bus *bus)
{
	if (!bus->ops->preinit)
		return 0;
	return bus->ops->preinit(bus->dev);
}

static inline void brcmf_bus_stop(struct brcmf_bus *bus)
{
	bus->ops->stop(bus->dev);
}

static inline int brcmf_bus_txdata(struct brcmf_bus *bus, struct sk_buff *skb)
{
	return bus->ops->txdata(bus->dev, skb);
}

static inline
int brcmf_bus_txctl(struct brcmf_bus *bus, unsigned char *msg, uint len)
{
	return bus->ops->txctl(bus->dev, msg, len);
}

static inline
int brcmf_bus_rxctl(struct brcmf_bus *bus, unsigned char *msg, uint len)
{
	return bus->ops->rxctl(bus->dev, msg, len);
}

static inline
struct pktq *brcmf_bus_gettxq(struct brcmf_bus *bus)
{
	if (!bus->ops->gettxq)
		return ERR_PTR(-ENOENT);

	return bus->ops->gettxq(bus->dev);
}

static inline bool brcmf_bus_ready(struct brcmf_bus *bus)
{
	return bus->state == BRCMF_BUS_LOAD || bus->state == BRCMF_BUS_DATA;
}

static inline void brcmf_bus_change_state(struct brcmf_bus *bus,
					  enum brcmf_bus_state new_state)
{
	/* NOMEDIUM is permanent */
	if (bus->state == BRCMF_BUS_NOMEDIUM)
		return;

	brcmf_dbg(TRACE, "%d -> %d\n", bus->state, new_state);
	bus->state = new_state;
}

/*
 * interface functions from common layer
 */

bool brcmf_c_prec_enq(struct device *dev, struct pktq *q, struct sk_buff *pkt,
		      int prec);

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
void brcmf_rx_frame(struct device *dev, struct sk_buff *rxp);

/* Indication from bus module regarding presence/insertion of dongle. */
int brcmf_attach(struct device *dev);
/* Indication from bus module regarding removal/absence of dongle */
void brcmf_detach(struct device *dev);
/* Indication from bus module that dongle should be reset */
void brcmf_dev_reset(struct device *dev);
/* Indication from bus module to change flow-control state */
void brcmf_txflowblock(struct device *dev, bool state);

/* Notify the bus has transferred the tx packet to firmware */
void brcmf_txcomplete(struct device *dev, struct sk_buff *txp, bool success);

int brcmf_bus_start(struct device *dev);
s32 brcmf_iovar_data_set(struct device *dev, char *name, void *data,
				u32 len);
void brcmf_bus_add_txhdrlen(struct device *dev, uint len);

#ifdef CONFIG_BRCMFMAC_SDIO
void brcmf_sdio_exit(void);
void brcmf_sdio_init(void);
void brcmf_sdio_register(void);
#endif
#ifdef CONFIG_BRCMFMAC_USB
void brcmf_usb_exit(void);
void brcmf_usb_register(void);
#endif

#endif				/* _BRCMF_BUS_H_ */
