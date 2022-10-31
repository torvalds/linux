/**
 ******************************************************************************
 *
 * @file ecrnx_radar.h
 *
 * @brief Functions to handle radar detection
 *
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
#ifndef _ECRNX_RADAR_H_
#define _ECRNX_RADAR_H_

#include <linux/nl80211.h>

struct ecrnx_vif;
struct ecrnx_hw;

enum ecrnx_radar_chain {
    ECRNX_RADAR_RIU = 0,
    ECRNX_RADAR_FCU,
    ECRNX_RADAR_LAST
};

enum ecrnx_radar_detector {
    ECRNX_RADAR_DETECT_DISABLE = 0, /* Ignore radar pulses */
    ECRNX_RADAR_DETECT_ENABLE  = 1, /* Process pattern detection but do not
                                      report radar to upper layer (for test) */
    ECRNX_RADAR_DETECT_REPORT  = 2  /* Process pattern detection and report
                                      radar to upper layer. */
};

#ifdef CONFIG_ECRNX_RADAR
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#define ECRNX_RADAR_PULSE_MAX  32

/**
 * struct ecrnx_radar_pulses - List of pulses reported by HW
 * @index: write index
 * @count: number of valid pulses
 * @buffer: buffer of pulses
 */
struct ecrnx_radar_pulses {
    /* Last radar pulses received */
    int index;
    int count;
    u32 buffer[ECRNX_RADAR_PULSE_MAX];
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
 * struct ecrnx_radar_detected - List of radar detected
 */
struct ecrnx_radar_detected {
    u16 index;
    u16 count;
    s64 time[NX_NB_RADAR_DETECTED];
    s16 freq[NX_NB_RADAR_DETECTED];
};


struct ecrnx_radar {
    struct ecrnx_radar_pulses pulses[ECRNX_RADAR_LAST];
    struct dfs_pattern_detector *dpd[ECRNX_RADAR_LAST];
    struct ecrnx_radar_detected detected[ECRNX_RADAR_LAST];
    struct work_struct detection_work;  /* Work used to process radar pulses */
    spinlock_t lock;                    /* lock for pulses processing */

    /* In softmac cac is handled by mac80211 */
#ifdef CONFIG_ECRNX_FULLMAC
    struct delayed_work cac_work;       /* Work used to handle CAC */
    struct ecrnx_vif *cac_vif;           /* vif on which we started CAC */
#endif
};

bool ecrnx_radar_detection_init(struct ecrnx_radar *radar);
void ecrnx_radar_detection_deinit(struct ecrnx_radar *radar);
bool ecrnx_radar_set_domain(struct ecrnx_radar *radar,
                           enum nl80211_dfs_regions region);
void ecrnx_radar_detection_enable(struct ecrnx_radar *radar, u8 enable, u8 chain);
bool ecrnx_radar_detection_is_enable(struct ecrnx_radar *radar, u8 chain);
void ecrnx_radar_start_cac(struct ecrnx_radar *radar, u32 cac_time_ms,
                          struct ecrnx_vif *vif);
void ecrnx_radar_cancel_cac(struct ecrnx_radar *radar);
void ecrnx_radar_detection_enable_on_cur_channel(struct ecrnx_hw *ecrnx_hw);
int  ecrnx_radar_dump_pattern_detector(char *buf, size_t len,
                                      struct ecrnx_radar *radar, u8 chain);
int  ecrnx_radar_dump_radar_detected(char *buf, size_t len,
                                    struct ecrnx_radar *radar, u8 chain);

#else

struct ecrnx_radar {
};

static inline bool ecrnx_radar_detection_init(struct ecrnx_radar *radar)
{return true;}

static inline void ecrnx_radar_detection_deinit(struct ecrnx_radar *radar)
{}

static inline bool ecrnx_radar_set_domain(struct ecrnx_radar *radar,
                                         enum nl80211_dfs_regions region)
{return true;}

static inline void ecrnx_radar_detection_enable(struct ecrnx_radar *radar,
                                               u8 enable, u8 chain)
{}

static inline bool ecrnx_radar_detection_is_enable(struct ecrnx_radar *radar,
                                                 u8 chain)
{return false;}

static inline void ecrnx_radar_start_cac(struct ecrnx_radar *radar,
                                        u32 cac_time_ms, struct ecrnx_vif *vif)
{}

static inline void ecrnx_radar_cancel_cac(struct ecrnx_radar *radar)
{}

static inline void ecrnx_radar_detection_enable_on_cur_channel(struct ecrnx_hw *ecrnx_hw)
{}

static inline int ecrnx_radar_dump_pattern_detector(char *buf, size_t len,
                                                   struct ecrnx_radar *radar,
                                                   u8 chain)
{return 0;}

static inline int ecrnx_radar_dump_radar_detected(char *buf, size_t len,
                                                 struct ecrnx_radar *radar,
                                                 u8 chain)
{return 0;}

#endif /* CONFIG_ECRNX_RADAR */

#endif // _ECRNX_RADAR_H_
