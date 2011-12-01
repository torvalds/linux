/*
 * Copyright (c) 2005-2006 Atheros Communications, Inc.
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


#ifndef _DFS_HOST_H_
#define _DFS_HOST_H_

#ifdef ATH_SUPPORT_DFS 

#include "dfs_host_project.h"

#define DFS_MIN(a,b) ((a)<(b)?(a):(b))
#define DFS_MAX(a,b) ((a)>(b)?(a):(b))
#define DFS_DIFF(a,b) (DFS_MAX(a,b) - DFS_MIN(a,b))
/*
 * Maximum number of radar events to be processed in a single iteration.
 * Allows soft watchdog to run.
 */
#define MAX_EVENTS 100


#define DFS_MARGIN_EQUAL(a, b, margin) ((DFS_DIFF(a,b)) <= margin)
#define DFS_MAX_STAGGERED_BURSTS 3

/* All filter thresholds in the radar filter tables are effective at a 50% channel loading */
#define DFS_CHAN_LOADING_THRESH         50
#define DFS_EXT_CHAN_LOADING_THRESH     30
#define DFS_DEFAULT_PRI_MARGIN          10
#define DFS_DEFAULT_FIXEDPATTERN_PRI_MARGIN       6


#define	ATH_DFSQ_LOCK(_dfs)         spin_lock(&(_dfs)->dfs_radarqlock)
#define	ATH_DFSQ_UNLOCK(_dfs)       spin_unlock(&(_dfs)->dfs_radarqlock)
#define	ATH_DFSQ_LOCK_INIT(_dfs)    spin_lock_init(&(_dfs)->dfs_radarqlock)

#define	ATH_ARQ_LOCK(_dfs)          spin_lock(&(_dfs)->dfs_arqlock)
#define	ATH_ARQ_UNLOCK(_dfs)        spin_unlock(&(_dfs)->dfs_arqlock)
#define	ATH_ARQ_LOCK_INIT(_dfs)     spin_lock_init(&(_dfs)->dfs_arqlock)

#define	ATH_DFSEVENTQ_LOCK(_dfs)    spin_lock(&(_dfs)->dfs_eventqlock)
#define	ATH_DFSEVENTQ_UNLOCK(_dfs)  spin_unlock(&(_dfs)->dfs_eventqlock)
#define	ATH_DFSEVENTQ_LOCK_INIT(_dfs)   spin_lock_init(&(_dfs)->dfs_eventqlock)


#define DFS_TSMASK              0xFFFFFFFF      /* Mask for time stamp from descriptor */
#define DFS_TSSHIFT             32              /* Shift for time stamp from descriptor */
#define	DFS_TSF_WRAP		0xFFFFFFFFFFFFFFFFULL	/* 64 bit TSF wrap value */
#define	DFS_64BIT_TSFMASK	0x0000000000007FFFULL	/* TS mask for 64 bit value */


#define	DFS_AR_RADAR_RSSI_THR		5	/* in dB */
#define	DFS_AR_RADAR_RESET_INT		1	/* in secs */
#define	DFS_AR_RADAR_MAX_HISTORY	500
#define	DFS_AR_REGION_WIDTH		128
#define	DFS_AR_RSSI_THRESH_STRONG_PKTS	17	/* in dB */
#define	DFS_AR_RSSI_DOUBLE_THRESHOLD	15	/* in dB */
#define	DFS_AR_MAX_NUM_ACK_REGIONS	9
#define	DFS_AR_ACK_DETECT_PAR_THRESH	20
#define	DFS_AR_PKT_COUNT_THRESH		20

#define	DFS_MAX_DL_MASK			0x3F

#define DFS_NOL_TIME			30*60*1000000	/* 30 minutes in usecs */

#define DFS_WAIT_TIME			60*1000000	/* 1 minute in usecs */

#define	DFS_DISABLE_TIME		3*60*1000000	/* 3 minutes in usecs */

#define	DFS_MAX_B5_SIZE			128
#define	DFS_MAX_B5_MASK			0x0000007F	/* 128 */

#define	DFS_MAX_RADAR_OVERLAP		16		/* Max number of overlapping filters */

#define	DFS_MAX_EVENTS			1024		/* Max number of dfs events which can be q'd */

#define DFS_RADAR_EN		0x80000000	/* Radar detect is capable */
#define DFS_AR_EN		0x40000000	/* AR detect is capable */
#define	DFS_MAX_RSSI_VALUE	0x7fffffff	/* Max rssi value */

#define DFS_BIN_MAX_PULSES              60      /* max num of pulses in a burst */
#define DFS_BIN5_PRI_LOWER_LIMIT	990	/* us */
#define DFS_BIN5_PRI_HIGHER_LIMIT	2010	/* us */
#define DFS_BIN5_WIDTH_MARGIN	    	4	/* us */
#define DFS_BIN5_RSSI_MARGIN	    	5	/* dBm */
/*Following threshold is not specified but should be okay statistically*/
#define DFS_BIN5_BRI_LOWER_LIMIT	300000  /* us */

#define DFS_MAX_PULSE_BUFFER_SIZE 1024          /* Max number of pulses kept in buffer */
#define DFS_MAX_PULSE_BUFFER_MASK 0x3ff
        
#define DFS_FAST_CLOCK_MULTIPLIER       (800/11)
#define DFS_NO_FAST_CLOCK_MULTIPLIER    (80)

typedef	spinlock_t dfsq_lock_t;

struct  dfs_pulse {
    A_UINT32    rp_numpulses;    /* Num of pulses in radar burst */
    A_UINT32    rp_pulsedur;    /* Duration of each pulse in usecs */
    A_UINT32    rp_pulsefreq;    /* Frequency of pulses in burst */
    A_UINT32    rp_max_pulsefreq;    /* Frequency of pulses in burst */
    A_UINT32       rp_patterntype;  /*fixed or variable pattern type*/
    A_UINT32    rp_pulsevar;    /* Time variation of pulse duration for
                                    matched filter (single-sided) in usecs */
    A_UINT32    rp_threshold;    /* Thershold for MF output to indicate
                                     radar match */
    A_UINT32    rp_mindur;    /* Min pulse duration to be considered for
                                  this pulse type */
    A_UINT32    rp_maxdur;    /* Max pusle duration to be considered for
                                  this pulse type */
    A_UINT32    rp_rssithresh;    /* Minimum rssi to be considered a radar pulse */
    A_UINT32    rp_meanoffset;    /* Offset for timing adjustment */
    A_INT32        rp_rssimargin;  /* rssi threshold margin. In Turbo Mode HW reports rssi 3dBm 
                                    * lower than in non TURBO mode.  This will be used to offset
                                    * that diff.*/
    A_UINT32    rp_pulseid;    /* Unique ID for identifying filter */

};

struct dfs_bin5pulse {
    A_UINT32       b5_threshold;          /* Number of bin5 pulses to indicate detection */
    A_UINT32       b5_mindur;             /* Min duration for a bin5 pulse */
    A_UINT32       b5_maxdur;             /* Max duration for a bin5 pulse */
    A_UINT32       b5_timewindow;         /* Window over which to count bin5 pulses */
    A_UINT32       b5_rssithresh;         /* Min rssi to be considered a pulse */
    A_UINT32       b5_rssimargin;         /* rssi threshold margin. In Turbo Mode HW reports rssi 3dB */
};


#define	DFS_MAX_DL_SIZE			64
#include "athstartpack.h"
PREPACK struct dfs_delayelem {
    u_int32_t  de_time;  /* Current "filter" time for start of pulse in usecs*/
    u_int8_t   de_dur;   /* Duration of pulse in usecs*/
    u_int8_t   de_rssi;  /* rssi of pulse in dB*/
} POSTPACK adf_os_packed;

/* NB: The first element in the circular buffer is the oldest element */

PREPACK struct dfs_delayline {
	struct dfs_delayelem dl_elems[DFS_MAX_DL_SIZE];	/* Array of pulses in delay line */
	u_int64_t	dl_last_ts;		/* Last timestamp the delay line was used (in usecs) */
	u_int32_t	dl_firstelem;		/* Index of the first element */
	u_int32_t	dl_lastelem;		/* Index of the last element */
	u_int32_t	dl_numelems;		/* Number of elements in the delay line */
} POSTPACK adf_os_packed;


PREPACK struct dfs_filter {
        struct dfs_delayline rf_dl;     /* Delay line of pulses for this filter */
        u_int32_t       rf_numpulses;   /* Number of pulses in the filter */
        u_int32_t       rf_minpri;      /* min pri to be considered for this filter*/
        u_int32_t       rf_maxpri;      /* max pri to be considered for this filter*/
        u_int32_t       rf_threshold;   /* match filter output threshold for radar detect */
        u_int32_t       rf_filterlen;   /* Length (in usecs) of the filter */
        u_int32_t       rf_patterntype; /* fixed or variable pattern type */
        u_int32_t       rf_mindur;      /* Min duration for this radar filter */
        u_int32_t       rf_maxdur;      /* Max duration for this radar filter */
        u_int32_t       rf_pulseid;     /* Unique ID corresponding to the original filter ID */
} POSTPACK adf_os_packed;


                                   
PREPACK struct dfs_pulseparams {
    u_int64_t  p_time;  /* time for start of pulse in usecs*/
    u_int8_t   p_dur;   /* Duration of pulse in usecs*/
    u_int8_t   p_rssi;  /* Duration of pulse in usecs*/
} POSTPACK adf_os_packed;

PREPACK struct dfs_pulseline {
    /* pl_elems - array of pulses in delay line */
    struct dfs_pulseparams pl_elems[DFS_MAX_PULSE_BUFFER_SIZE];
    u_int32_t  pl_firstelem;  /* Index of the first element */
    u_int32_t  pl_lastelem;   /* Index of the last element */
    u_int32_t  pl_numelems;   /* Number of elements in the delay line */
} POSTPACK adf_os_packed;

PREPACK struct dfs_event {
	u_int64_t  re_full_ts;    /* 64-bit full timestamp from interrupt time */
	u_int32_t  re_ts;         /* Original 15 bit recv timestamp */
	u_int32_t  re_ext_chan_busy;  /* Ext channel busy % */
	u_int8_t   re_rssi;       /* rssi of radar event */
	u_int8_t   re_dur;        /* duration of radar pulse */
	u_int8_t   re_chanindex;  /* Channel of event */
	u_int8_t   re_chanindextype;  /* Primary channel or extension channel */
	STAILQ_ENTRY(dfs_event)	re_list; /* List of radar events */
} POSTPACK adf_os_packed;
#include "athendpack.h"

#define DFS_AR_MAX_ACK_RADAR_DUR	511
#define DFS_AR_MAX_NUM_PEAKS		3
#define DFS_AR_ARQ_SIZE			2048	/* 8K AR events for buffer size */
#define DFS_AR_ARQ_SEQSIZE		2049	/* Sequence counter wrap for AR */

#define DFS_RADARQ_SIZE		512		/* 1K radar events for buffer size */
#define DFS_RADARQ_SEQSIZE	513		/* Sequence counter wrap for radar */
#define DFS_NUM_RADAR_STATES	64		/* Number of radar channels we keep state for */
#define DFS_MAX_NUM_RADAR_FILTERS 10		/* Max number radar filters for each type */ 
#define DFS_MAX_RADAR_TYPES	32		/* Number of different radar types */

struct dfs_ar_state {
	u_int32_t	ar_prevwidth;
	u_int32_t	ar_phyerrcount[DFS_AR_MAX_ACK_RADAR_DUR];
	u_int32_t	ar_acksum;
	u_int32_t	ar_packetthreshold;	/* Thresh to determine traffic load */
	u_int32_t	ar_parthreshold;	/* Thresh to determine peak */
	u_int32_t	ar_radarrssi;		/* Rssi threshold for AR event */
	u_int16_t	ar_prevtimestamp;
	u_int16_t	ar_peaklist[DFS_AR_MAX_NUM_PEAKS];
};

struct dfs_filtertype {
    struct dfs_filter ft_filters[DFS_MAX_NUM_RADAR_FILTERS];
    u_int32_t ft_filterdur;   /* Duration of pulse which specifies filter type*/
    u_int32_t ft_numfilters;  /* Num filters of this type */
    u_int64_t ft_last_ts;     /* Last timestamp this filtertype was used
                               * (in usecs) */
    u_int32_t ft_mindur;      /* min pulse duration to be considered
                               * for this filter type */
    u_int32_t ft_maxdur;	  /* max pulse duration to be considered
                               * for this filter type */
    u_int32_t ft_rssithresh;  /* min rssi to be considered
                               * for this filter type */
    u_int32_t ft_numpulses;   /* Num pulses in each filter of this type */
    u_int32_t ft_patterntype; /* fixed or variable pattern type */
    u_int32_t ft_minpri;      /* min pri to be considered for this type */
    u_int32_t ft_rssimargin;  /* rssi threshold margin. In Turbo Mode HW
                               * reports rssi 3dB lower than in non TURBO
                               * mode. This will offset that diff. */
};


#define DFS_NOL_TIMEOUT_S  (30*60)    /* 30 minutes in seconds */
#define DFS_NOL_TIMEOUT_MS (DFS_NOL_TIMEOUT_S * 1000)
#define DFS_NOL_TIMEOUT_US (DFS_NOL_TIMEOUT_MS * 1000)

#include "athstartpack.h"
PREPACK struct dfs_info_host {
    u_int32_t	rn_numradars;		/* Number of different types of radars  */
    u_int64_t	rn_lastfull_ts;		/* Last 64 bit timstamp from recv interrupt */
    u_int16_t	rn_last_ts;		/* last 15 bit ts from recv descriptor  */
    u_int32_t   rn_last_unique_ts;      /* last unique 32 bit ts from recv descriptor  */
    u_int64_t	rn_ts_prefix;		/* Prefix to prepend to 15 bit recv ts  */
    u_int32_t	rn_numbin5radars;	/* Number of bin5 radar pulses to search for  */
    u_int64_t       dfs_bin5_chirp_ts;  
    u_int8_t        dfs_last_bin5_dur; 
} POSTPACK adf_os_packed;
#include "athendpack.h"

struct dfs_bin5elem {
	u_int64_t	be_ts;			/* Timestamp for the bin5 element */
	u_int32_t	be_rssi;		/* Rssi for the bin5 element */
	u_int32_t	be_dur;			/* Duration of bin5 element */
};

struct dfs_bin5radars {
	struct dfs_bin5elem br_elems[DFS_MAX_B5_SIZE];	/* List of bin5 elems that fall
							 * within the time window */
	u_int32_t	br_firstelem;		/* Index of the first element */
	u_int32_t	br_lastelem;		/* Index of the last element */
	u_int32_t	br_numelems;		/* Number of elements in the delay line */
	struct dfs_bin5pulse br_pulse;		/* Original info about bin5 pulse */
};


#define ATH_DFS_RESET_TIME_S 7
#define ATH_DFS_WAIT (60 + ATH_DFS_RESET_TIME_S) /* 60 seconds */
#define ATH_DFS_WAIT_MS ((ATH_DFS_WAIT) * 1000)	/*in MS*/

#define ATH_DFS_WEATHER_CHANNEL_WAIT_MIN 10 /*10 minutes*/
#define ATH_DFS_WEATHER_CHANNEL_WAIT_S (ATH_DFS_WEATHER_CHANNEL_WAIT_MIN * 60)
#define ATH_DFS_WEATHER_CHANNEL_WAIT_MS ((ATH_DFS_WEATHER_CHANNEL_WAIT_S) * 1000)	/*in MS*/

#define ATH_DFS_WAIT_POLL_PERIOD 2	/* 2 seconds */
#define ATH_DFS_WAIT_POLL_PERIOD_MS ((ATH_DFS_WAIT_POLL_PERIOD) * 1000)	/*in MS*/
#define	ATH_DFS_TEST_RETURN_PERIOD 2	/* 2 seconds */
#define	ATH_DFS_TEST_RETURN_PERIOD_MS ((ATH_DFS_TEST_RETURN_PERIOD) * 1000)/* n MS*/

#define IS_CHANNEL_WEATHER_RADAR(chan) ((chan->channel >= 5600) && (chan->channel <= 5650))

#define DFS_DEBUG_TIMEOUT_S     30 // debug timeout is 30 seconds
#define DFS_DEBUG_TIMEOUT_MS    (DFS_DEBUG_TIMEOUT_S * 1000)
struct ath_dfs_host {
    DEV_HDL dev_hdl;
    u_int32_t dfs_debug_level;
    OS_HDL os_hdl;
    STAILQ_HEAD(,dfs_event)	dfs_eventq; /* Q of free dfs event objects */
    dfsq_lock_t dfs_eventqlock;         /* Lock for free dfs event list */
    STAILQ_HEAD(,dfs_event)	dfs_radarq; /* Q of radar events */
    dfsq_lock_t dfs_radarqlock;         /* Lock for dfs q */
    STAILQ_HEAD(,dfs_event) dfs_arq;    /* Q of AR events */
    dfsq_lock_t dfs_arqlock;            /* Lock for AR q */

    struct dfs_pulseline *pulses;       /* pulse history */
    struct dfs_event     *events;       /* Events structure */
    /* dfs_radarf - One filter for each radar pulse type */
    struct dfs_filtertype *dfs_radarf[DFS_MAX_RADAR_TYPES];

    int8_t **dfs_radartable;            /* map of radar durs to filter types */
    struct dfs_bin5radars *dfs_b5radars;/* array of bin5 radar events */
    struct dfs_ar_state dfs_ar_state; /* AR state */
    struct dfs_info_host dfs_rinfo;          /* State vars for radar processing */
    u_int8_t   dfs_bangradar;
    u_int32_t  dur_multiplier;
    A_TIMER dfs_radar_task_timer;
};


#define	HAL_CAP_RADAR	0
#define	HAL_CAP_AR	1
#define HAL_CAP_STRONG_DIV 2


/* Attach, detach, handle ioctl prototypes */
struct ath_dfs_host *dfs_attach_host(DEV_HDL dev, OS_HDL os, ATH_DFS_CAPINFO *cap_info);
void        dfs_detach_host(struct ath_dfs_host *sc);


/* PHY error and radar event handling */
void        dfs_process_phyerr_host(struct ath_dfs_host *dfs, WMI_DFS_PHYERR_EVENT *ev);
int         dfs_process_radarevent_host(struct ath_dfs_host *dfs, int16_t *chan_index, u_int8_t *bangradar);


/* FCC Bin5 detection prototypes */
int         dfs_bin5_addpulse(struct ath_dfs_host *sc, struct dfs_bin5radars *br,
                               struct dfs_event *re, u_int64_t thists);
int         dfs_bin5_check(struct ath_dfs_host *dfs);
u_int8_t    dfs_retain_bin5_burst_pattern(struct ath_dfs_host *dfs, u_int32_t diff_ts, u_int8_t old_dur);

/* Debug prototypes */
void         dfs_print_delayline(struct ath_dfs_host *dfs, struct dfs_delayline *dl);
void         dfs_print_filters(struct ath_dfs_host *dfs);
void dfs_print_filter(struct ath_dfs_host *dfs, struct dfs_filter *rf);

/* Misc prototypes */
u_int32_t  dfs_round(int32_t val);

/* Reset and init data structures */

int           dfs_init_radar_filters_host(struct ath_dfs_host *dfs,  struct ath_dfs_info *dfs_info);
void          dfs_reset_alldelaylines(struct ath_dfs_host *dfs);
void          dfs_reset_delayline(struct dfs_delayline *dl);
void          dfs_reset_filter_delaylines(struct dfs_filtertype *dft);
void          dfs_reset_radarq(struct ath_dfs_host *dfs);

/* Detection algorithm prototypes */
void          dfs_add_pulse(struct ath_dfs_host *dfs, struct dfs_filter *rf,
                           struct dfs_event *re, u_int32_t deltaT);

int           dfs_bin_fixedpattern_check(struct ath_dfs_host *dfs, struct dfs_filter *rf, u_int32_t dur, int ext_chan_flag, u_int32_t ext_chan_busy);
int           dfs_bin_check(struct ath_dfs_host *dfs, struct dfs_filter *rf,
                            u_int32_t deltaT, u_int32_t dur, int ext_chan_flag, u_int32_t ext_chan_busy);


int     dfs_bin_pri_check(struct ath_dfs_host *dfs, struct dfs_filter *rf,
                             struct dfs_delayline *dl, u_int32_t score,
                             u_int32_t refpri, u_int32_t refdur, int ext_chan_flag, u_int32_t ext_chan_busy);
int    dfs_staggered_check(struct ath_dfs_host *dfs, struct dfs_filter *rf,
                             u_int32_t deltaT, u_int32_t width, u_int32_t ext_chan_busy);

/* AR related prototypes */
u_int32_t        dfs_process_ar_event(struct ath_dfs_host *dfs);
void        dfs_reset_ar(struct ath_dfs_host *dfs);
void        dfs_reset_arq(struct ath_dfs_host *dfs);

void dfs_bangradar_enable(struct ath_dfs_host *dfs, u_int8_t enable);
void dfs_set_dur_multiplier(struct ath_dfs_host *dfs, u_int32_t dur_multiplier);
void dfs_set_debug_level_host(struct ath_dfs_host *dfs, u_int32_t level);

/* False detection reduction */
int dfs_get_pri_margin(int is_extchan_detect, int is_fixed_pattern, u_int64_t lastfull_ts, u_int32_t ext_chan_busy);
int dfs_get_filter_threshold(struct dfs_filter *rf, int is_extchan_detect, u_int64_t lastfull_ts, u_int32_t ext_chan_busy);

#endif /* ATH_SUPPORT_DFS */
#endif  /* _DFS_H_ */
