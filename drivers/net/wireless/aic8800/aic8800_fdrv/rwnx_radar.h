/**
 ******************************************************************************
 *
 * @file rwnx_radar.h
 *
 * @brief Functions to handle radar detection
 *
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */
#ifndef _RWNX_RADAR_H_
#define _RWNX_RADAR_H_

#include <linux/nl80211.h>

struct rwnx_vif;
struct rwnx_hw;

enum rwnx_radar_chain {
    RWNX_RADAR_RIU = 0,
    RWNX_RADAR_FCU,
    RWNX_RADAR_LAST
};

enum rwnx_radar_detector {
    RWNX_RADAR_DETECT_DISABLE = 0, /* Ignore radar pulses */
    RWNX_RADAR_DETECT_ENABLE  = 1, /* Process pattern detection but do not
                                      report radar to upper layer (for test) */
    RWNX_RADAR_DETECT_REPORT  = 2  /* Process pattern detection and report
                                      radar to upper layer. */
};

#ifdef CONFIG_RWNX_RADAR
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#define RWNX_RADAR_PULSE_MAX  32

/**
 * struct rwnx_radar_pulses - List of pulses reported by HW
 * @index: write index
 * @count: number of valid pulses
 * @buffer: buffer of pulses
 */
struct rwnx_radar_pulses {
    /* Last radar pulses received */
    int index;
    int count;
    u32 buffer[RWNX_RADAR_PULSE_MAX];
};

/**
 * struct dfs_pattern_detector - DFS pattern detector
 * @region: active DFS region, NL80211_DFS_UNSET until set
 * @num_radar_types: number of different radar types
 * @last_pulse_ts: time stamp of last valid pulse in usecs
 * @prev_jiffies:
 * @radar_detector_specs: array of radar detection specs
 * @channel_detectors: list connecting channel_detector elements
 */
struct dfs_pattern_detector {
    u8 enabled;
    enum nl80211_dfs_regions region;
    u8 num_radar_types;
    u64 last_pulse_ts;
    u32 prev_jiffies;
    const struct radar_detector_specs *radar_spec;
    struct list_head detectors[];
};

#define NX_NB_RADAR_DETECTED 4

/**
 * struct rwnx_radar_detected - List of radar detected
 */
struct rwnx_radar_detected {
    u16 index;
    u16 count;
    s64 time[NX_NB_RADAR_DETECTED];
    s16 freq[NX_NB_RADAR_DETECTED];
};


struct rwnx_radar {
    struct rwnx_radar_pulses pulses[RWNX_RADAR_LAST];
    struct dfs_pattern_detector *dpd[RWNX_RADAR_LAST];
    struct rwnx_radar_detected detected[RWNX_RADAR_LAST];
    struct work_struct detection_work;  /* Work used to process radar pulses */
    spinlock_t lock;                    /* lock for pulses processing */

    /* In softmac cac is handled by mac80211 */
#ifdef CONFIG_RWNX_FULLMAC
    struct delayed_work cac_work;       /* Work used to handle CAC */
    struct rwnx_vif *cac_vif;           /* vif on which we started CAC */
#endif
};

bool rwnx_radar_detection_init(struct rwnx_radar *radar);
void rwnx_radar_detection_deinit(struct rwnx_radar *radar);
bool rwnx_radar_set_domain(struct rwnx_radar *radar,
                           enum nl80211_dfs_regions region);
void rwnx_radar_detection_enable(struct rwnx_radar *radar, u8 enable, u8 chain);
bool rwnx_radar_detection_is_enable(struct rwnx_radar *radar, u8 chain);
void rwnx_radar_start_cac(struct rwnx_radar *radar, u32 cac_time_ms,
                          struct rwnx_vif *vif);
void rwnx_radar_cancel_cac(struct rwnx_radar *radar);
void rwnx_radar_detection_enable_on_cur_channel(struct rwnx_hw *rwnx_hw);
int  rwnx_radar_dump_pattern_detector(char *buf, size_t len,
                                      struct rwnx_radar *radar, u8 chain);
int  rwnx_radar_dump_radar_detected(char *buf, size_t len,
                                    struct rwnx_radar *radar, u8 chain);

#else

struct rwnx_radar {
};

static inline bool rwnx_radar_detection_init(struct rwnx_radar *radar)
{return true;}

static inline void rwnx_radar_detection_deinit(struct rwnx_radar *radar)
{}

static inline bool rwnx_radar_set_domain(struct rwnx_radar *radar,
                                         enum nl80211_dfs_regions region)
{return true;}

static inline void rwnx_radar_detection_enable(struct rwnx_radar *radar,
                                               u8 enable, u8 chain)
{}

static inline bool rwnx_radar_detection_is_enable(struct rwnx_radar *radar,
                                                 u8 chain)
{return false;}

static inline void rwnx_radar_start_cac(struct rwnx_radar *radar,
                                        u32 cac_time_ms, struct rwnx_vif *vif)
{}

static inline void rwnx_radar_cancel_cac(struct rwnx_radar *radar)
{}

static inline void rwnx_radar_detection_enable_on_cur_channel(struct rwnx_hw *rwnx_hw)
{}

static inline int rwnx_radar_dump_pattern_detector(char *buf, size_t len,
                                                   struct rwnx_radar *radar,
                                                   u8 chain)
{return 0;}

static inline int rwnx_radar_dump_radar_detected(char *buf, size_t len,
                                                 struct rwnx_radar *radar,
                                                 u8 chain)
{return 0;}

#endif /* CONFIG_RWNX_RADAR */

#endif // _RWNX_RADAR_H_
