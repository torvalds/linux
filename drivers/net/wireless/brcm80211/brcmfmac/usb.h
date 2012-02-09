/*
 * Copyright (c) 2011 Broadcom Corporation
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
#ifndef BRCMFMAC_USB_H
#define BRCMFMAC_USB_H

enum brcmf_usb_state {
	BCMFMAC_USB_STATE_DL_PENDING,
	BCMFMAC_USB_STATE_DL_DONE,
	BCMFMAC_USB_STATE_UP,
	BCMFMAC_USB_STATE_DOWN,
	BCMFMAC_USB_STATE_PNP_FWDL,
	BCMFMAC_USB_STATE_DISCONNECT,
	BCMFMAC_USB_STATE_SLEEP
};

enum brcmf_usb_pnp_state {
	BCMFMAC_USB_PNP_DISCONNECT,
	BCMFMAC_USB_PNP_SLEEP,
	BCMFMAC_USB_PNP_RESUME,
};

struct brcmf_stats {
	u32 tx_errors;
	u32 tx_packets;
	u32 tx_multicast;
	u32 tx_ctlpkts;
	u32 tx_ctlerrs;
	u32 tx_dropped;
	u32 tx_flushed;
	u32 rx_errors;
	u32 rx_packets;
	u32 rx_multicast;
	u32 rx_ctlpkts;
	u32 rx_ctlerrs;
	u32 rx_dropped;
	u32 rx_flushed;

};

struct brcmf_usb_attrib {
	int bustype;
	int vid;
	int pid;
	int devid;
	int chiprev; /* chip revsion number */
	int mtu;
	int nchan; /* Data Channels */
	int has_2nd_bulk_in_ep;
};

struct brcmf_usbdev_info;

struct brcmf_usbdev {
	struct brcmf_bus *bus;
	struct brcmf_usbdev_info *devinfo;
	enum brcmf_usb_state state;
	struct brcmf_stats stats;
	int ntxq, nrxq, rxsize;
	u32 bus_mtu;
	struct brcmf_usb_attrib attrib;
};

/* IO Request Block (IRB) */
struct brcmf_usbreq {
	struct list_head list;
	struct brcmf_usbdev_info *devinfo;
	struct urb *urb;
	struct sk_buff  *skb;
};

#endif /* BRCMFMAC_USB_H */
