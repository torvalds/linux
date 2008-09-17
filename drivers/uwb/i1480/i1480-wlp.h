/*
 * Intel 1480 Wireless UWB Link
 * WLP specific definitions
 *
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 */

#ifndef __i1480_wlp_h__
#define __i1480_wlp_h__

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/uwb.h>
#include <linux/if_ether.h>
#include <asm/byteorder.h>

/* New simplified header format? */
#undef WLP_HDR_FMT_2 		/* FIXME: rename */

/**
 * Values of the Delivery ID & Type field when PCA or DRP
 *
 * The Delivery ID & Type field in the WLP TX header indicates whether
 * the frame is PCA or DRP. This is done based on the high level bit of
 * this field.
 * We use this constant to test if the traffic is PCA or DRP as follows:
 * if (wlp_tx_hdr_delivery_id_type(wlp_tx_hdr) & WLP_DRP)
 * 	this is DRP traffic
 * else
 * 	this is PCA traffic
 */
enum deliver_id_type_bit {
	WLP_DRP = 8,
};

/**
 * WLP TX header
 *
 * Indicates UWB/WLP-specific transmission parameters for a network
 * packet.
 */
struct wlp_tx_hdr {
	/* dword 0 */
	struct uwb_dev_addr dstaddr;
	u8                  key_index;
	u8                  mac_params;
	/* dword 1 */
	u8                  phy_params;
#ifndef WLP_HDR_FMT_2
	u8                  reserved;
	__le16              oui01;              /* FIXME: not so sure if __le16 or u8[2] */
	/* dword 2 */
	u8                  oui2;               /*        if all LE, it could be merged */
	__le16              prid;
#endif
} __attribute__((packed));

static inline int wlp_tx_hdr_delivery_id_type(const struct wlp_tx_hdr *hdr)
{
	return hdr->mac_params & 0x0f;
}

static inline int wlp_tx_hdr_ack_policy(const struct wlp_tx_hdr *hdr)
{
	return (hdr->mac_params >> 4) & 0x07;
}

static inline int wlp_tx_hdr_rts_cts(const struct wlp_tx_hdr *hdr)
{
	return (hdr->mac_params >> 7) & 0x01;
}

static inline void wlp_tx_hdr_set_delivery_id_type(struct wlp_tx_hdr *hdr, int id)
{
	hdr->mac_params = (hdr->mac_params & ~0x0f) | id;
}

static inline void wlp_tx_hdr_set_ack_policy(struct wlp_tx_hdr *hdr,
					     enum uwb_ack_pol policy)
{
	hdr->mac_params = (hdr->mac_params & ~0x70) | (policy << 4);
}

static inline void wlp_tx_hdr_set_rts_cts(struct wlp_tx_hdr *hdr, int rts_cts)
{
	hdr->mac_params = (hdr->mac_params & ~0x80) | (rts_cts << 7);
}

static inline enum uwb_phy_rate wlp_tx_hdr_phy_rate(const struct wlp_tx_hdr *hdr)
{
	return hdr->phy_params & 0x0f;
}

static inline int wlp_tx_hdr_tx_power(const struct wlp_tx_hdr *hdr)
{
	return (hdr->phy_params >> 4) & 0x0f;
}

static inline void wlp_tx_hdr_set_phy_rate(struct wlp_tx_hdr *hdr, enum uwb_phy_rate rate)
{
	hdr->phy_params = (hdr->phy_params & ~0x0f) | rate;
}

static inline void wlp_tx_hdr_set_tx_power(struct wlp_tx_hdr *hdr, int pwr)
{
	hdr->phy_params = (hdr->phy_params & ~0xf0) | (pwr << 4);
}


/**
 * WLP RX header
 *
 * Provides UWB/WLP-specific transmission data for a received
 * network packet.
 */
struct wlp_rx_hdr {
	/* dword 0 */
	struct uwb_dev_addr dstaddr;
	struct uwb_dev_addr srcaddr;
	/* dword 1 */
	u8 		    LQI;
	s8		    RSSI;
	u8		    reserved3;
#ifndef WLP_HDR_FMT_2
	u8 		    oui0;
	/* dword 2 */
	__le16		    oui12;
	__le16		    prid;
#endif
} __attribute__((packed));


/** User configurable options for WLP */
struct wlp_options {
	struct mutex mutex; /* access to user configurable options*/
	struct wlp_tx_hdr def_tx_hdr;	/* default tx hdr */
	u8 pca_base_priority;
	u8 bw_alloc; /*index into bw_allocs[] for PCA/DRP reservations*/
};


static inline
void wlp_options_init(struct wlp_options *options)
{
	mutex_init(&options->mutex);
	wlp_tx_hdr_set_ack_policy(&options->def_tx_hdr, UWB_ACK_INM);
	wlp_tx_hdr_set_rts_cts(&options->def_tx_hdr, 1);
	/* FIXME: default to phy caps */
	wlp_tx_hdr_set_phy_rate(&options->def_tx_hdr, UWB_PHY_RATE_480);
#ifndef WLP_HDR_FMT_2
	options->def_tx_hdr.prid = cpu_to_le16(0x0000);
#endif
}


/* sysfs helpers */

extern ssize_t uwb_pca_base_priority_store(struct wlp_options *,
					   const char *, size_t);
extern ssize_t uwb_pca_base_priority_show(const struct wlp_options *, char *);
extern ssize_t uwb_bw_alloc_store(struct wlp_options *, const char *, size_t);
extern ssize_t uwb_bw_alloc_show(const struct wlp_options *, char *);
extern ssize_t uwb_ack_policy_store(struct wlp_options *,
				    const char *, size_t);
extern ssize_t uwb_ack_policy_show(const struct wlp_options *, char *);
extern ssize_t uwb_rts_cts_store(struct wlp_options *, const char *, size_t);
extern ssize_t uwb_rts_cts_show(const struct wlp_options *, char *);
extern ssize_t uwb_phy_rate_store(struct wlp_options *, const char *, size_t);
extern ssize_t uwb_phy_rate_show(const struct wlp_options *, char *);


/** Simple bandwidth allocation (temporary and too simple) */
struct wlp_bw_allocs {
	const char *name;
	struct {
		u8 mask, stream;
	} tx, rx;
};


#endif /* #ifndef __i1480_wlp_h__ */
