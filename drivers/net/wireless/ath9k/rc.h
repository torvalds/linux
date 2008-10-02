/*
 * Copyright (c) 2004 Sam Leffler, Errno Consulting
 * Copyright (c) 2004 Video54 Technologies, Inc.
 * Copyright (c) 2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef RC_H
#define RC_H

#include "ath9k.h"
/*
 * Interface definitions for transmit rate control modules for the
 * Atheros driver.
 *
 * A rate control module is responsible for choosing the transmit rate
 * for each data frame.  Management+control frames are always sent at
 * a fixed rate.
 *
 * Only one module may be present at a time; the driver references
 * rate control interfaces by symbol name.  If multiple modules are
 * to be supported we'll need to switch to a registration-based scheme
 * as is currently done, for example, for authentication modules.
 *
 * An instance of the rate control module is attached to each device
 * at attach time and detached when the device is destroyed.  The module
 * may associate data with each device and each node (station).  Both
 * sets of storage are opaque except for the size of the per-node storage
 * which must be provided when the module is attached.
 *
 * The rate control module is notified for each state transition and
 * station association/reassociation.  Otherwise it is queried for a
 * rate for each outgoing frame and provided status from each transmitted
 * frame.  Any ancillary processing is the responsibility of the module
 * (e.g. if periodic processing is required then the module should setup
 * it's own timer).
 *
 * In addition to the transmit rate for each frame the module must also
 * indicate the number of attempts to make at the specified rate.  If this
 * number is != ATH_TXMAXTRY then an additional callback is made to setup
 * additional transmit state.  The rate control code is assumed to write
 * this additional data directly to the transmit descriptor.
 */

struct ath_softc;

#define TRUE 1
#define FALSE 0

#define ATH_RATE_MAX	30
#define MCS_SET_SIZE	128

enum ieee80211_fixed_rate_mode {
	IEEE80211_FIXED_RATE_NONE  = 0,
	IEEE80211_FIXED_RATE_MCS   = 1  /* HT rates */
};

/*
 * Use the hal os glue code to get ms time
 */
#define IEEE80211_RATE_IDX_ENTRY(val, idx) (((val&(0xff<<(idx*8)))>>(idx*8)))

#define SHORT_PRE 1
#define LONG_PRE 0

#define WLAN_PHY_HT_20_SS       WLAN_RC_PHY_HT_20_SS
#define WLAN_PHY_HT_20_DS       WLAN_RC_PHY_HT_20_DS
#define WLAN_PHY_HT_20_DS_HGI   WLAN_RC_PHY_HT_20_DS_HGI
#define WLAN_PHY_HT_40_SS       WLAN_RC_PHY_HT_40_SS
#define WLAN_PHY_HT_40_SS_HGI   WLAN_RC_PHY_HT_40_SS_HGI
#define WLAN_PHY_HT_40_DS       WLAN_RC_PHY_HT_40_DS
#define WLAN_PHY_HT_40_DS_HGI   WLAN_RC_PHY_HT_40_DS_HGI

#define WLAN_PHY_OFDM	PHY_OFDM
#define WLAN_PHY_CCK	PHY_CCK

#define TRUE_20		0x2
#define TRUE_40		0x4
#define TRUE_2040	(TRUE_20|TRUE_40)
#define TRUE_ALL	(TRUE_2040|TRUE)

enum {
	WLAN_RC_PHY_HT_20_SS = 4,
	WLAN_RC_PHY_HT_20_DS,
	WLAN_RC_PHY_HT_40_SS,
	WLAN_RC_PHY_HT_40_DS,
	WLAN_RC_PHY_HT_20_SS_HGI,
	WLAN_RC_PHY_HT_20_DS_HGI,
	WLAN_RC_PHY_HT_40_SS_HGI,
	WLAN_RC_PHY_HT_40_DS_HGI,
	WLAN_RC_PHY_MAX
};

#define WLAN_RC_PHY_DS(_phy)   ((_phy == WLAN_RC_PHY_HT_20_DS)           \
	|| (_phy == WLAN_RC_PHY_HT_40_DS)        \
	|| (_phy == WLAN_RC_PHY_HT_20_DS_HGI)    \
	|| (_phy == WLAN_RC_PHY_HT_40_DS_HGI))
#define WLAN_RC_PHY_40(_phy)   ((_phy == WLAN_RC_PHY_HT_40_SS)           \
	|| (_phy == WLAN_RC_PHY_HT_40_DS)        \
	|| (_phy == WLAN_RC_PHY_HT_40_SS_HGI)    \
	|| (_phy == WLAN_RC_PHY_HT_40_DS_HGI))
#define WLAN_RC_PHY_SGI(_phy)  ((_phy == WLAN_RC_PHY_HT_20_SS_HGI)      \
	|| (_phy == WLAN_RC_PHY_HT_20_DS_HGI)   \
	|| (_phy == WLAN_RC_PHY_HT_40_SS_HGI)   \
	|| (_phy == WLAN_RC_PHY_HT_40_DS_HGI))

#define WLAN_RC_PHY_HT(_phy)    (_phy >= WLAN_RC_PHY_HT_20_SS)

/* Returns the capflag mode */
#define WLAN_RC_CAP_MODE(capflag) (((capflag & WLAN_RC_HT_FLAG) ?	\
		(capflag & WLAN_RC_40_FLAG) ? TRUE_40 : TRUE_20 : TRUE))

/* Return TRUE if flag supports HT20 && client supports HT20 or
 * return TRUE if flag supports HT40 && client supports HT40.
 * This is used becos some rates overlap between HT20/HT40.
 */

#define WLAN_RC_PHY_HT_VALID(flag, capflag) (((flag & TRUE_20) && !(capflag \
				& WLAN_RC_40_FLAG)) || ((flag & TRUE_40) && \
				  (capflag & WLAN_RC_40_FLAG)))

#define WLAN_RC_DS_FLAG         (0x01)
#define WLAN_RC_40_FLAG         (0x02)
#define WLAN_RC_SGI_FLAG        (0x04)
#define WLAN_RC_HT_FLAG         (0x08)

/* Index into the rate table */
#define INIT_RATE_MAX_20	23
#define INIT_RATE_MAX_40	40

#define RATE_TABLE_SIZE		64

/* XXX: Convert to kdoc */
struct ath_rate_table {
	int rate_cnt;
	struct {
		int valid;            /* Valid for use in rate control */
		int valid_single_stream;/* Valid for use in rate control
					for single stream operation */
		u8 phy;              /* CCK/OFDM/TURBO/XR */
		u32 ratekbps;         /* Rate in Kbits per second */
		u32 user_ratekbps;     /* User rate in KBits per second */
		u8 ratecode;         /* rate that goes into
					hw descriptors */
		u8 short_preamble;    /* Mask for enabling short preamble
						in rate code for CCK */
		u8 dot11rate;        /* Value that goes into supported
					rates info element of MLME */
		u8 ctrl_rate;      /* Index of next lower basic rate,
					used for duration computation */
		int8_t rssi_ack_validmin;  /* Rate control related */
		int8_t rssi_ack_deltamin;  /* Rate control related */
		u8 base_index;        /* base rate index */
		u8 cw40index;        /* 40cap rate index */
		u8 sgi_index;         /* shortgi rate index */
		u8 ht_index;          /* shortgi rate index */
		u32 max_4ms_framelen;   /* Maximum frame length(bytes)
						for 4ms tx duration */
	} info[RATE_TABLE_SIZE];
	u32 probe_interval;        /* interval for ratectrl to
					probe for other rates */
	u32 rssi_reduce_interval;   /* interval for ratectrl
						to reduce RSSI */
	u8 initial_ratemax;   /* the initial ratemax value used
					in ath_rc_sib_update() */
};

#define ATH_RC_PROBE_ALLOWED            0x00000001
#define ATH_RC_MINRATE_LASTRATE         0x00000002
#define ATH_RC_SHORT_PREAMBLE           0x00000004

struct ath_rc_series {
	u8    rix;
	u8    tries;
	u8    flags;
	u32   max_4ms_framelen;
};

/* rcs_flags definition */
#define ATH_RC_DS_FLAG               0x01
#define ATH_RC_CW40_FLAG             0x02    /* CW 40 */
#define ATH_RC_SGI_FLAG              0x04    /* Short Guard Interval */
#define ATH_RC_HT_FLAG               0x08    /* HT */
#define ATH_RC_RTSCTS_FLAG           0x10    /* RTS-CTS */

/*
 * State structures for new rate adaptation code
 */
#define	MAX_TX_RATE_TBL	        64
#define MAX_TX_RATE_PHY         48

struct ath_tx_ratectrl_state {
	int8_t rssi_thres; /* required rssi for this rate (dB) */
	u8 per; /* recent estimate of packet error rate (%) */
};

struct ath_tx_ratectrl {
	struct ath_tx_ratectrl_state state[MAX_TX_RATE_TBL]; /* state */
	int8_t rssi_last;            /* last ack rssi */
	int8_t rssi_last_lookup;	/* last ack rssi used for lookup */
	int8_t rssi_last_prev;	/* previous last ack rssi */
	int8_t rssi_last_prev2;	/* 2nd previous last ack rssi */
	int32_t rssi_sum_cnt;        /* count of rssi_sum for averaging */
	int32_t rssi_sum_rate;       /* rate that we are averaging */
	int32_t rssi_sum;           /* running sum of rssi for averaging */
	u32 valid_txrate_mask;   /* mask of valid rates */
	u8 rate_table_size;      /* rate table size */
	u8 rate_max;            /* max rate that has recently worked */
	u8 probe_rate;          /* rate we are probing at */
	u32 rssi_time;          /* msec timestamp for last ack rssi */
	u32 rssi_down_time;      /* msec timestamp for last down step */
	u32 probe_time;         /* msec timestamp for last probe */
	u8 hw_maxretry_pktcnt;   /* num packets since we got
					HW max retry error */
	u8 max_valid_rate;       /* maximum number of valid rate */
	u8 valid_rate_index[MAX_TX_RATE_TBL]; /* valid rate index */
	u32 per_down_time;       /* msec timstamp for last
					PER down step */

	/* 11n state */
	u8  valid_phy_ratecnt[WLAN_RC_PHY_MAX]; /* valid rate count */
	u8  valid_phy_rateidx[WLAN_RC_PHY_MAX][MAX_TX_RATE_TBL];
	u8  rc_phy_mode;
	u8  rate_max_phy;        /* Phy index for the max rate */
	u32 rate_max_lastused;   /* msec timstamp of when we
					last used rateMaxPhy */
	u32 probe_interval;     /* interval for ratectrl to probe
					for other rates */
};

struct ath_rateset {
	u8 rs_nrates;
	u8 rs_rates[ATH_RATE_MAX];
};

/* per-device state */
struct ath_rate_softc {
	/* phy tables that contain rate control data */
	const void *hw_rate_table[ATH9K_MODE_MAX];
	int fixedrix;	/* -1 or index of fixed rate */
};

/* per-node state */
struct ath_rate_node {
	struct ath_tx_ratectrl tx_ratectrl;	/* rate control state proper */
	u32 prev_data_rix;	/* rate idx of last data frame */

	/* map of rate ix -> negotiated rate set ix */
	u8 rixmap[MAX_TX_RATE_TBL];

	/* map of ht rate ix -> negotiated rate set ix */
	u8 ht_rixmap[MAX_TX_RATE_TBL];

	u8 ht_cap;		/* ht capabilities */
	u8 ant_tx;		/* current transmit antenna */

	u8 single_stream;   /* When TRUE, only single
				stream Tx possible */
	struct ath_rateset neg_rates;	/* Negotiated rates */
	struct ath_rateset neg_ht_rates;	/* Negotiated HT rates */
	struct ath_rate_softc *asc; /* back pointer to atheros softc */
	struct ath_vap *avp;	/* back pointer to vap */
};

/* Driver data of ieee80211_tx_info */
struct ath_tx_info_priv {
	struct ath_rc_series rcs[4];
	struct ath_tx_status tx;
	int n_frames;
	int n_bad_frames;
	u8 min_rate;
};

/*
 * Attach/detach a rate control module.
 */
struct ath_rate_softc *ath_rate_attach(struct ath_hal *ah);
void ath_rate_detach(struct ath_rate_softc *asc);

/*
 * Update/reset rate control state for 802.11 state transitions.
 * Important mostly as the analog to ath_rate_newassoc when operating
 * in station mode.
 */
void ath_rc_node_update(struct ieee80211_hw *hw, struct ath_rate_node *rc_priv);
void ath_rate_newstate(struct ath_softc *sc, struct ath_vap *avp);

/*
 * Return the tx rate series.
 */
void ath_rate_findrate(struct ath_softc *sc, struct ath_rate_node *ath_rc_priv,
		       int num_tries, int num_rates,
		       unsigned int rcflag, struct ath_rc_series[],
		       int *is_probe, int isretry);
/*
 * Return rate index for given Dot11 Rate.
 */
u8 ath_rate_findrateix(struct ath_softc *sc,
			     u8 dot11_rate);

/* Routines to register/unregister rate control algorithm */
int ath_rate_control_register(void);
void ath_rate_control_unregister(void);

#endif /* RC_H */
