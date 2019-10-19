// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2011 Broadcom Corporation
 */
#ifndef BRCMFMAC_USB_H
#define BRCMFMAC_USB_H

enum brcmf_usb_state {
	BRCMFMAC_USB_STATE_DOWN,
	BRCMFMAC_USB_STATE_DL_FAIL,
	BRCMFMAC_USB_STATE_DL_DONE,
	BRCMFMAC_USB_STATE_UP,
	BRCMFMAC_USB_STATE_SLEEP
};

struct brcmf_stats {
	u32 tx_ctlpkts;
	u32 tx_ctlerrs;
	u32 rx_ctlpkts;
	u32 rx_ctlerrs;
};

struct brcmf_usbdev {
	struct brcmf_bus *bus;
	struct brcmf_usbdev_info *devinfo;
	enum brcmf_usb_state state;
	struct brcmf_stats stats;
	int ntxq, nrxq, rxsize;
	u32 bus_mtu;
	int devid;
	int chiprev; /* chip revsion number */
};

/* IO Request Block (IRB) */
struct brcmf_usbreq {
	struct list_head list;
	struct brcmf_usbdev_info *devinfo;
	struct urb *urb;
	struct sk_buff  *skb;
};

#endif /* BRCMFMAC_USB_H */
