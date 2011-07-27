/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_agn_rs_h__
#define __iwl_agn_rs_h__

struct iwl_rate_info {
	u8 plcp;	/* uCode API:  IWL_RATE_6M_PLCP, etc. */
	u8 plcp_siso;	/* uCode API:  IWL_RATE_SISO_6M_PLCP, etc. */
	u8 plcp_mimo2;	/* uCode API:  IWL_RATE_MIMO2_6M_PLCP, etc. */
	u8 plcp_mimo3;  /* uCode API:  IWL_RATE_MIMO3_6M_PLCP, etc. */
	u8 ieee;	/* MAC header:  IWL_RATE_6M_IEEE, etc. */
	u8 prev_ieee;    /* previous rate in IEEE speeds */
	u8 next_ieee;    /* next rate in IEEE speeds */
	u8 prev_rs;      /* previous rate used in rs algo */
	u8 next_rs;      /* next rate used in rs algo */
	u8 prev_rs_tgg;  /* previous rate used in TGG rs algo */
	u8 next_rs_tgg;  /* next rate used in TGG rs algo */
};

/*
 * These serve as indexes into
 * struct iwl_rate_info iwl_rates[IWL_RATE_COUNT];
 */
enum {
	IWL_RATE_1M_INDEX = 0,
	IWL_RATE_2M_INDEX,
	IWL_RATE_5M_INDEX,
	IWL_RATE_11M_INDEX,
	IWL_RATE_6M_INDEX,
	IWL_RATE_9M_INDEX,
	IWL_RATE_12M_INDEX,
	IWL_RATE_18M_INDEX,
	IWL_RATE_24M_INDEX,
	IWL_RATE_36M_INDEX,
	IWL_RATE_48M_INDEX,
	IWL_RATE_54M_INDEX,
	IWL_RATE_60M_INDEX,
	IWL_RATE_COUNT, /*FIXME:RS:change to IWL_RATE_INDEX_COUNT,*/
	IWL_RATE_COUNT_LEGACY = IWL_RATE_COUNT - 1,	/* Excluding 60M */
	IWL_RATE_INVM_INDEX = IWL_RATE_COUNT,
	IWL_RATE_INVALID = IWL_RATE_COUNT,
};

enum {
	IWL_RATE_6M_INDEX_TABLE = 0,
	IWL_RATE_9M_INDEX_TABLE,
	IWL_RATE_12M_INDEX_TABLE,
	IWL_RATE_18M_INDEX_TABLE,
	IWL_RATE_24M_INDEX_TABLE,
	IWL_RATE_36M_INDEX_TABLE,
	IWL_RATE_48M_INDEX_TABLE,
	IWL_RATE_54M_INDEX_TABLE,
	IWL_RATE_1M_INDEX_TABLE,
	IWL_RATE_2M_INDEX_TABLE,
	IWL_RATE_5M_INDEX_TABLE,
	IWL_RATE_11M_INDEX_TABLE,
	IWL_RATE_INVM_INDEX_TABLE = IWL_RATE_INVM_INDEX - 1,
};

enum {
	IWL_FIRST_OFDM_RATE = IWL_RATE_6M_INDEX,
	IWL_LAST_OFDM_RATE = IWL_RATE_60M_INDEX,
	IWL_FIRST_CCK_RATE = IWL_RATE_1M_INDEX,
	IWL_LAST_CCK_RATE = IWL_RATE_11M_INDEX,
};

/* #define vs. enum to keep from defaulting to 'large integer' */
#define	IWL_RATE_6M_MASK   (1 << IWL_RATE_6M_INDEX)
#define	IWL_RATE_9M_MASK   (1 << IWL_RATE_9M_INDEX)
#define	IWL_RATE_12M_MASK  (1 << IWL_RATE_12M_INDEX)
#define	IWL_RATE_18M_MASK  (1 << IWL_RATE_18M_INDEX)
#define	IWL_RATE_24M_MASK  (1 << IWL_RATE_24M_INDEX)
#define	IWL_RATE_36M_MASK  (1 << IWL_RATE_36M_INDEX)
#define	IWL_RATE_48M_MASK  (1 << IWL_RATE_48M_INDEX)
#define	IWL_RATE_54M_MASK  (1 << IWL_RATE_54M_INDEX)
#define IWL_RATE_60M_MASK  (1 << IWL_RATE_60M_INDEX)
#define	IWL_RATE_1M_MASK   (1 << IWL_RATE_1M_INDEX)
#define	IWL_RATE_2M_MASK   (1 << IWL_RATE_2M_INDEX)
#define	IWL_RATE_5M_MASK   (1 << IWL_RATE_5M_INDEX)
#define	IWL_RATE_11M_MASK  (1 << IWL_RATE_11M_INDEX)

/* uCode API values for legacy bit rates, both OFDM and CCK */
enum {
	IWL_RATE_6M_PLCP  = 13,
	IWL_RATE_9M_PLCP  = 15,
	IWL_RATE_12M_PLCP = 5,
	IWL_RATE_18M_PLCP = 7,
	IWL_RATE_24M_PLCP = 9,
	IWL_RATE_36M_PLCP = 11,
	IWL_RATE_48M_PLCP = 1,
	IWL_RATE_54M_PLCP = 3,
	IWL_RATE_60M_PLCP = 3,/*FIXME:RS:should be removed*/
	IWL_RATE_1M_PLCP  = 10,
	IWL_RATE_2M_PLCP  = 20,
	IWL_RATE_5M_PLCP  = 55,
	IWL_RATE_11M_PLCP = 110,
	/*FIXME:RS:change to IWL_RATE_LEGACY_??M_PLCP */
	/*FIXME:RS:add IWL_RATE_LEGACY_INVM_PLCP = 0,*/
};

/* uCode API values for OFDM high-throughput (HT) bit rates */
enum {
	IWL_RATE_SISO_6M_PLCP = 0,
	IWL_RATE_SISO_12M_PLCP = 1,
	IWL_RATE_SISO_18M_PLCP = 2,
	IWL_RATE_SISO_24M_PLCP = 3,
	IWL_RATE_SISO_36M_PLCP = 4,
	IWL_RATE_SISO_48M_PLCP = 5,
	IWL_RATE_SISO_54M_PLCP = 6,
	IWL_RATE_SISO_60M_PLCP = 7,
	IWL_RATE_MIMO2_6M_PLCP  = 0x8,
	IWL_RATE_MIMO2_12M_PLCP = 0x9,
	IWL_RATE_MIMO2_18M_PLCP = 0xa,
	IWL_RATE_MIMO2_24M_PLCP = 0xb,
	IWL_RATE_MIMO2_36M_PLCP = 0xc,
	IWL_RATE_MIMO2_48M_PLCP = 0xd,
	IWL_RATE_MIMO2_54M_PLCP = 0xe,
	IWL_RATE_MIMO2_60M_PLCP = 0xf,
	IWL_RATE_MIMO3_6M_PLCP  = 0x10,
	IWL_RATE_MIMO3_12M_PLCP = 0x11,
	IWL_RATE_MIMO3_18M_PLCP = 0x12,
	IWL_RATE_MIMO3_24M_PLCP = 0x13,
	IWL_RATE_MIMO3_36M_PLCP = 0x14,
	IWL_RATE_MIMO3_48M_PLCP = 0x15,
	IWL_RATE_MIMO3_54M_PLCP = 0x16,
	IWL_RATE_MIMO3_60M_PLCP = 0x17,
	IWL_RATE_SISO_INVM_PLCP,
	IWL_RATE_MIMO2_INVM_PLCP = IWL_RATE_SISO_INVM_PLCP,
	IWL_RATE_MIMO3_INVM_PLCP = IWL_RATE_SISO_INVM_PLCP,
};

/* MAC header values for bit rates */
enum {
	IWL_RATE_6M_IEEE  = 12,
	IWL_RATE_9M_IEEE  = 18,
	IWL_RATE_12M_IEEE = 24,
	IWL_RATE_18M_IEEE = 36,
	IWL_RATE_24M_IEEE = 48,
	IWL_RATE_36M_IEEE = 72,
	IWL_RATE_48M_IEEE = 96,
	IWL_RATE_54M_IEEE = 108,
	IWL_RATE_60M_IEEE = 120,
	IWL_RATE_1M_IEEE  = 2,
	IWL_RATE_2M_IEEE  = 4,
	IWL_RATE_5M_IEEE  = 11,
	IWL_RATE_11M_IEEE = 22,
};

#define IWL_CCK_BASIC_RATES_MASK    \
       (IWL_RATE_1M_MASK          | \
	IWL_RATE_2M_MASK)

#define IWL_CCK_RATES_MASK          \
       (IWL_CCK_BASIC_RATES_MASK  | \
	IWL_RATE_5M_MASK          | \
	IWL_RATE_11M_MASK)

#define IWL_OFDM_BASIC_RATES_MASK   \
	(IWL_RATE_6M_MASK         | \
	IWL_RATE_12M_MASK         | \
	IWL_RATE_24M_MASK)

#define IWL_OFDM_RATES_MASK         \
       (IWL_OFDM_BASIC_RATES_MASK | \
	IWL_RATE_9M_MASK          | \
	IWL_RATE_18M_MASK         | \
	IWL_RATE_36M_MASK         | \
	IWL_RATE_48M_MASK         | \
	IWL_RATE_54M_MASK)

#define IWL_BASIC_RATES_MASK         \
	(IWL_OFDM_BASIC_RATES_MASK | \
	 IWL_CCK_BASIC_RATES_MASK)

#define IWL_RATES_MASK ((1 << IWL_RATE_COUNT) - 1)

#define IWL_INVALID_VALUE    -1

#define IWL_MIN_RSSI_VAL                 -100
#define IWL_MAX_RSSI_VAL                    0

/* These values specify how many Tx frame attempts before
 * searching for a new modulation mode */
#define IWL_LEGACY_FAILURE_LIMIT	160
#define IWL_LEGACY_SUCCESS_LIMIT	480
#define IWL_LEGACY_TABLE_COUNT		160

#define IWL_NONE_LEGACY_FAILURE_LIMIT	400
#define IWL_NONE_LEGACY_SUCCESS_LIMIT	4500
#define IWL_NONE_LEGACY_TABLE_COUNT	1500

/* Success ratio (ACKed / attempted tx frames) values (perfect is 128 * 100) */
#define IWL_RS_GOOD_RATIO		12800	/* 100% */
#define IWL_RATE_SCALE_SWITCH		10880	/*  85% */
#define IWL_RATE_HIGH_TH		10880	/*  85% */
#define IWL_RATE_INCREASE_TH		6400	/*  50% */
#define IWL_RATE_DECREASE_TH		1920	/*  15% */

/* possible actions when in legacy mode */
#define IWL_LEGACY_SWITCH_ANTENNA1      0
#define IWL_LEGACY_SWITCH_ANTENNA2      1
#define IWL_LEGACY_SWITCH_SISO          2
#define IWL_LEGACY_SWITCH_MIMO2_AB      3
#define IWL_LEGACY_SWITCH_MIMO2_AC      4
#define IWL_LEGACY_SWITCH_MIMO2_BC      5
#define IWL_LEGACY_SWITCH_MIMO3_ABC     6

/* possible actions when in siso mode */
#define IWL_SISO_SWITCH_ANTENNA1        0
#define IWL_SISO_SWITCH_ANTENNA2        1
#define IWL_SISO_SWITCH_MIMO2_AB        2
#define IWL_SISO_SWITCH_MIMO2_AC        3
#define IWL_SISO_SWITCH_MIMO2_BC        4
#define IWL_SISO_SWITCH_GI              5
#define IWL_SISO_SWITCH_MIMO3_ABC       6


/* possible actions when in mimo mode */
#define IWL_MIMO2_SWITCH_ANTENNA1       0
#define IWL_MIMO2_SWITCH_ANTENNA2       1
#define IWL_MIMO2_SWITCH_SISO_A         2
#define IWL_MIMO2_SWITCH_SISO_B         3
#define IWL_MIMO2_SWITCH_SISO_C         4
#define IWL_MIMO2_SWITCH_GI             5
#define IWL_MIMO2_SWITCH_MIMO3_ABC      6


/* possible actions when in mimo3 mode */
#define IWL_MIMO3_SWITCH_ANTENNA1       0
#define IWL_MIMO3_SWITCH_ANTENNA2       1
#define IWL_MIMO3_SWITCH_SISO_A         2
#define IWL_MIMO3_SWITCH_SISO_B         3
#define IWL_MIMO3_SWITCH_SISO_C         4
#define IWL_MIMO3_SWITCH_MIMO2_AB       5
#define IWL_MIMO3_SWITCH_MIMO2_AC       6
#define IWL_MIMO3_SWITCH_MIMO2_BC       7
#define IWL_MIMO3_SWITCH_GI             8


#define IWL_MAX_11N_MIMO3_SEARCH IWL_MIMO3_SWITCH_GI
#define IWL_MAX_SEARCH IWL_MIMO2_SWITCH_MIMO3_ABC

/*FIXME:RS:add possible actions for MIMO3*/

#define IWL_ACTION_LIMIT		3	/* # possible actions */

#define LQ_SIZE		2	/* 2 mode tables:  "Active" and "Search" */

/* load per tid defines for A-MPDU activation */
#define IWL_AGG_TPT_THREHOLD	0
#define IWL_AGG_LOAD_THRESHOLD	10
#define IWL_AGG_ALL_TID		0xff
#define TID_QUEUE_CELL_SPACING	50	/*mS */
#define TID_QUEUE_MAX_SIZE	20
#define TID_ROUND_VALUE		5	/* mS */
#define TID_MAX_LOAD_COUNT	8

#define TID_MAX_TIME_DIFF ((TID_QUEUE_MAX_SIZE - 1) * TID_QUEUE_CELL_SPACING)
#define TIME_WRAP_AROUND(x, y) (((y) > (x)) ? (y) - (x) : (0-(x)) + (y))

extern const struct iwl_rate_info iwl_rates[IWL_RATE_COUNT];

enum iwl_table_type {
	LQ_NONE,
	LQ_G,		/* legacy types */
	LQ_A,
	LQ_SISO,	/* high-throughput types */
	LQ_MIMO2,
	LQ_MIMO3,
	LQ_MAX,
};

#define is_legacy(tbl) (((tbl) == LQ_G) || ((tbl) == LQ_A))
#define is_siso(tbl) ((tbl) == LQ_SISO)
#define is_mimo2(tbl) ((tbl) == LQ_MIMO2)
#define is_mimo3(tbl) ((tbl) == LQ_MIMO3)
#define is_mimo(tbl) (is_mimo2(tbl) || is_mimo3(tbl))
#define is_Ht(tbl) (is_siso(tbl) || is_mimo(tbl))
#define is_a_band(tbl) ((tbl) == LQ_A)
#define is_g_and(tbl) ((tbl) == LQ_G)

#define	ANT_NONE	0x0
#define	ANT_A		BIT(0)
#define	ANT_B		BIT(1)
#define	ANT_AB		(ANT_A | ANT_B)
#define ANT_C		BIT(2)
#define	ANT_AC		(ANT_A | ANT_C)
#define ANT_BC		(ANT_B | ANT_C)
#define ANT_ABC		(ANT_AB | ANT_C)

#define IWL_MAX_MCS_DISPLAY_SIZE	12

struct iwl_rate_mcs_info {
	char	mbps[IWL_MAX_MCS_DISPLAY_SIZE];
	char	mcs[IWL_MAX_MCS_DISPLAY_SIZE];
};

/**
 * struct iwl_rate_scale_data -- tx success history for one rate
 */
struct iwl_rate_scale_data {
	u64 data;		/* bitmap of successful frames */
	s32 success_counter;	/* number of frames successful */
	s32 success_ratio;	/* per-cent * 128  */
	s32 counter;		/* number of frames attempted */
	s32 average_tpt;	/* success ratio * expected throughput */
	unsigned long stamp;
};

/**
 * struct iwl_scale_tbl_info -- tx params and success history for all rates
 *
 * There are two of these in struct iwl_lq_sta,
 * one for "active", and one for "search".
 */
struct iwl_scale_tbl_info {
	enum iwl_table_type lq_type;
	u8 ant_type;
	u8 is_SGI;	/* 1 = short guard interval */
	u8 is_ht40;	/* 1 = 40 MHz channel width */
	u8 is_dup;	/* 1 = duplicated data streams */
	u8 action;	/* change modulation; IWL_[LEGACY/SISO/MIMO]_SWITCH_* */
	u8 max_search;	/* maximun number of tables we can search */
	s32 *expected_tpt;	/* throughput metrics; expected_tpt_G, etc. */
	u32 current_rate;  /* rate_n_flags, uCode API format */
	struct iwl_rate_scale_data win[IWL_RATE_COUNT]; /* rate histories */
};

struct iwl_traffic_load {
	unsigned long time_stamp;	/* age of the oldest statistics */
	u32 packet_count[TID_QUEUE_MAX_SIZE];   /* packet count in this time
						 * slice */
	u32 total;			/* total num of packets during the
					 * last TID_MAX_TIME_DIFF */
	u8 queue_count;			/* number of queues that has
					 * been used since the last cleanup */
	u8 head;			/* start of the circular buffer */
};

/**
 * struct iwl_lq_sta -- driver's rate scaling private structure
 *
 * Pointer to this gets passed back and forth between driver and mac80211.
 */
struct iwl_lq_sta {
	u8 active_tbl;		/* index of active table, range 0-1 */
	u8 enable_counter;	/* indicates HT mode */
	u8 stay_in_tbl;		/* 1: disallow, 0: allow search for new mode */
	u8 search_better_tbl;	/* 1: currently trying alternate mode */
	s32 last_tpt;

	/* The following determine when to search for a new mode */
	u32 table_count_limit;
	u32 max_failure_limit;	/* # failed frames before new search */
	u32 max_success_limit;	/* # successful frames before new search */
	u32 table_count;
	u32 total_failed;	/* total failed frames, any/all rates */
	u32 total_success;	/* total successful frames, any/all rates */
	u64 flush_timer;	/* time staying in mode before new search */

	u8 action_counter;	/* # mode-switch actions tried */
	u8 is_green;
	u8 is_dup;
	enum ieee80211_band band;

	/* The following are bitmaps of rates; IWL_RATE_6M_MASK, etc. */
	u32 supp_rates;
	u16 active_legacy_rate;
	u16 active_siso_rate;
	u16 active_mimo2_rate;
	u16 active_mimo3_rate;
	s8 max_rate_idx;     /* Max rate set by user */
	u8 missed_rate_counter;

	struct iwl_link_quality_cmd lq;
	struct iwl_scale_tbl_info lq_info[LQ_SIZE]; /* "active", "search" */
	struct iwl_traffic_load load[TID_MAX_LOAD_COUNT];
	u8 tx_agg_tid_en;
#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *rs_sta_dbgfs_scale_table_file;
	struct dentry *rs_sta_dbgfs_stats_table_file;
	struct dentry *rs_sta_dbgfs_rate_scale_data_file;
	struct dentry *rs_sta_dbgfs_tx_agg_tid_en_file;
	u32 dbg_fixed_rate;
#endif
	struct iwl_priv *drv;

	/* used to be in sta_info */
	int last_txrate_idx;
	/* last tx rate_n_flags */
	u32 last_rate_n_flags;
	/* packets destined for this STA are aggregated */
	u8 is_agg;
	/* BT traffic this sta was last updated in */
	u8 last_bt_traffic;
};

static inline u8 num_of_ant(u8 mask)
{
	return  !!((mask) & ANT_A) +
		!!((mask) & ANT_B) +
		!!((mask) & ANT_C);
}

static inline u8 first_antenna(u8 mask)
{
	if (mask & ANT_A)
		return ANT_A;
	if (mask & ANT_B)
		return ANT_B;
	return ANT_C;
}


/* Initialize station's rate scaling information after adding station */
extern void iwl_rs_rate_init(struct iwl_priv *priv,
			     struct ieee80211_sta *sta, u8 sta_id);

/**
 * iwl_rate_control_register - Register the rate control algorithm callbacks
 *
 * Since the rate control algorithm is hardware specific, there is no need
 * or reason to place it as a stand alone module.  The driver can call
 * iwl_rate_control_register in order to register the rate control callbacks
 * with the mac80211 subsystem.  This should be performed prior to calling
 * ieee80211_register_hw
 *
 */
extern int iwlagn_rate_control_register(void);

/**
 * iwl_rate_control_unregister - Unregister the rate control callbacks
 *
 * This should be called after calling ieee80211_unregister_hw, but before
 * the driver is unloaded.
 */
extern void iwlagn_rate_control_unregister(void);

#endif /* __iwl_agn__rs__ */
