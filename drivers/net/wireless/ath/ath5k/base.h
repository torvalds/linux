/*-
 * Copyright (c) 2002-2007 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 */

/*
 * Definitions for the Atheros Wireless LAN controller driver.
 */
#ifndef _DEV_ATH_ATHVAR_H
#define _DEV_ATH_ATHVAR_H

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/wireless.h>
#include <linux/if_ether.h>
#include <linux/leds.h>
#include <linux/rfkill.h>
#include <linux/workqueue.h>

#include "ath5k.h"
#include "debug.h"
#include "ani.h"

#include "../regd.h"
#include "../ath.h"

#define	ATH_RXBUF	40		/* number of RX buffers */
#define	ATH_TXBUF	200		/* number of TX buffers */
#define ATH_BCBUF	4		/* number of beacon buffers */
#define ATH5K_TXQ_LEN_MAX	(ATH_TXBUF / 4)		/* bufs per queue */
#define ATH5K_TXQ_LEN_LOW	(ATH5K_TXQ_LEN_MAX / 2)	/* low mark */

struct ath5k_buf {
	struct list_head	list;
	struct ath5k_desc	*desc;	/* virtual addr of desc */
	dma_addr_t		daddr;	/* physical addr of desc */
	struct sk_buff		*skb;	/* skbuff for buf */
	dma_addr_t		skbaddr;/* physical addr of skb data */
};

/*
 * Data transmit queue state.  One of these exists for each
 * hardware transmit queue.  Packets sent to us from above
 * are assigned to queues based on their priority.  Not all
 * devices support a complete set of hardware transmit queues.
 * For those devices the array sc_ac2q will map multiple
 * priorities to fewer hardware queues (typically all to one
 * hardware queue).
 */
struct ath5k_txq {
	unsigned int		qnum;	/* hardware q number */
	u32			*link;	/* link ptr in last TX desc */
	struct list_head	q;	/* transmit queue */
	spinlock_t		lock;	/* lock on q and link */
	bool			setup;
	int			txq_len; /* number of queued buffers */
	int			txq_max; /* max allowed num of queued buffers */
	bool			txq_poll_mark;
	unsigned int		txq_stuck;	/* informational counter */
};

#define ATH5K_LED_MAX_NAME_LEN 31

/*
 * State for LED triggers
 */
struct ath5k_led
{
	char name[ATH5K_LED_MAX_NAME_LEN + 1];	/* name of the LED in sysfs */
	struct ath5k_softc *sc;			/* driver state */
	struct led_classdev led_dev;		/* led classdev */
};

/* Rfkill */
struct ath5k_rfkill {
	/* GPIO PIN for rfkill */
	u16 gpio;
	/* polarity of rfkill GPIO PIN */
	bool polarity;
	/* RFKILL toggle tasklet */
	struct tasklet_struct toggleq;
};

/* statistics */
struct ath5k_statistics {
	/* antenna use */
	unsigned int antenna_rx[5];	/* frames count per antenna RX */
	unsigned int antenna_tx[5];	/* frames count per antenna TX */

	/* frame errors */
	unsigned int rx_all_count;	/* all RX frames, including errors */
	unsigned int tx_all_count;	/* all TX frames, including errors */
	unsigned int rx_bytes_count;	/* all RX bytes, including errored pks
					 * and the MAC headers for each packet
					 */
	unsigned int tx_bytes_count;	/* all TX bytes, including errored pkts
					 * and the MAC headers and padding for
					 * each packet.
					 */
	unsigned int rxerr_crc;
	unsigned int rxerr_phy;
	unsigned int rxerr_phy_code[32];
	unsigned int rxerr_fifo;
	unsigned int rxerr_decrypt;
	unsigned int rxerr_mic;
	unsigned int rxerr_proc;
	unsigned int rxerr_jumbo;
	unsigned int txerr_retry;
	unsigned int txerr_fifo;
	unsigned int txerr_filt;

	/* MIB counters */
	unsigned int ack_fail;
	unsigned int rts_fail;
	unsigned int rts_ok;
	unsigned int fcs_error;
	unsigned int beacons;

	unsigned int mib_intr;
	unsigned int rxorn_intr;
	unsigned int rxeol_intr;
};

#if CHAN_DEBUG
#define ATH_CHAN_MAX	(26+26+26+200+200)
#else
#define ATH_CHAN_MAX	(14+14+14+252+20)
#endif

struct ath5k_vif {
	bool			assoc; /* are we associated or not */
	enum nl80211_iftype	opmode;
	int			bslot;
	struct ath5k_buf	*bbuf; /* beacon buffer */
	u8			lladdr[ETH_ALEN];
};

/* Software Carrier, keeps track of the driver state
 * associated with an instance of a device */
struct ath5k_softc {
	struct pci_dev		*pdev;
	struct device		*dev;		/* for dma mapping */
	int irq;
	u16 devid;
	void __iomem		*iobase;	/* address of the device */
	struct mutex		lock;		/* dev-level lock */
	struct ieee80211_hw	*hw;		/* IEEE 802.11 common */
	struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];
	struct ieee80211_channel channels[ATH_CHAN_MAX];
	struct ieee80211_rate	rates[IEEE80211_NUM_BANDS][AR5K_MAX_RATES];
	s8			rate_idx[IEEE80211_NUM_BANDS][AR5K_MAX_RATES];
	enum nl80211_iftype	opmode;
	struct ath5k_hw		*ah;		/* Atheros HW */

#ifdef CONFIG_ATH5K_DEBUG
	struct ath5k_dbg_info	debug;		/* debug info */
#endif /* CONFIG_ATH5K_DEBUG */

	struct ath5k_buf	*bufptr;	/* allocated buffer ptr */
	struct ath5k_desc	*desc;		/* TX/RX descriptors */
	dma_addr_t		desc_daddr;	/* DMA (physical) address */
	size_t			desc_len;	/* size of TX/RX descriptors */

	DECLARE_BITMAP(status, 5);
#define ATH_STAT_INVALID	0		/* disable hardware accesses */
#define ATH_STAT_MRRETRY	1		/* multi-rate retry support */
#define ATH_STAT_PROMISC	2
#define ATH_STAT_LEDSOFT	3		/* enable LED gpio status */
#define ATH_STAT_STARTED	4		/* opened & irqs enabled */

	unsigned int		filter_flags;	/* HW flags, AR5K_RX_FILTER_* */
	struct ieee80211_channel *curchan;	/* current h/w channel */

	u16			nvifs;

	enum ath5k_int		imask;		/* interrupt mask copy */

	u8			lladdr[ETH_ALEN];
	u8			bssidmask[ETH_ALEN];

	unsigned int		led_pin,	/* GPIO pin for driving LED */
				led_on;		/* pin setting for LED on */

	struct work_struct	reset_work;	/* deferred chip reset */

	unsigned int		rxbufsize;	/* rx size based on mtu */
	struct list_head	rxbuf;		/* receive buffer */
	spinlock_t		rxbuflock;
	u32			*rxlink;	/* link ptr in last RX desc */
	struct tasklet_struct	rxtq;		/* rx intr tasklet */
	struct ath5k_led	rx_led;		/* rx led */

	struct list_head	txbuf;		/* transmit buffer */
	spinlock_t		txbuflock;
	unsigned int		txbuf_len;	/* buf count in txbuf list */
	struct ath5k_txq	txqs[AR5K_NUM_TX_QUEUES];	/* tx queues */
	struct tasklet_struct	txtq;		/* tx intr tasklet */
	struct ath5k_led	tx_led;		/* tx led */

	struct ath5k_rfkill	rf_kill;

	struct tasklet_struct	calib;		/* calibration tasklet */

	spinlock_t		block;		/* protects beacon */
	struct tasklet_struct	beacontq;	/* beacon intr tasklet */
	struct list_head	bcbuf;		/* beacon buffer */
	struct ieee80211_vif	*bslot[ATH_BCBUF];
	u16			num_ap_vifs;
	u16			num_adhoc_vifs;
	unsigned int		bhalq,		/* SW q for outgoing beacons */
				bmisscount,	/* missed beacon transmits */
				bintval,	/* beacon interval in TU */
				bsent;
	unsigned int		nexttbtt;	/* next beacon time in TU */
	struct ath5k_txq	*cabq;		/* content after beacon */

	int 			power_level;	/* Requested tx power in dbm */
	bool			assoc;		/* associate state */
	bool			enable_beacon;	/* true if beacons are on */

	struct ath5k_statistics	stats;

	struct ath5k_ani_state	ani_state;
	struct tasklet_struct	ani_tasklet;	/* ANI calibration */

	struct delayed_work	tx_complete_work;

	struct survey_info	survey;		/* collected survey info */
};

struct ath5k_vif_iter_data {
	const u8	*hw_macaddr;
	u8		mask[ETH_ALEN];
	u8		active_mac[ETH_ALEN]; /* first active MAC */
	bool		need_set_hw_addr;
	bool		found_active;
	bool		any_assoc;
	enum nl80211_iftype opmode;
	int n_stas;
};
void ath5k_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif);


#define ath5k_hw_hasbssidmask(_ah) \
	(ath5k_hw_get_capability(_ah, AR5K_CAP_BSSIDMASK, 0, NULL) == 0)
#define ath5k_hw_hasveol(_ah) \
	(ath5k_hw_get_capability(_ah, AR5K_CAP_VEOL, 0, NULL) == 0)

#endif
