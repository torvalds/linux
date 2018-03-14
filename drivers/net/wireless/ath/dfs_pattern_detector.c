/*
 * Copyright (c) 2012 Neratec Solutions AG
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

#include <linux/slab.h>
#include <linux/export.h>

#include "dfs_pattern_detector.h"
#include "dfs_pri_detector.h"
#include "ath.h"

/**
 * struct radar_types - contains array of patterns defined for one DFS domain
 * @domain: DFS regulatory domain
 * @num_radar_types: number of radar types to follow
 * @radar_types: radar types array
 */
struct radar_types {
	enum nl80211_dfs_regions region;
	u32 num_radar_types;
	const struct radar_detector_specs *radar_types;
};

/* percentage on ppb threshold to trigger detection */
#define MIN_PPB_THRESH	50
#define PPB_THRESH_RATE(PPB, RATE) ((PPB * RATE + 100 - RATE) / 100)
#define PPB_THRESH(PPB) PPB_THRESH_RATE(PPB, MIN_PPB_THRESH)
#define PRF2PRI(PRF) ((1000000 + PRF / 2) / PRF)
/* percentage of pulse width tolerance */
#define WIDTH_TOLERANCE 5
#define WIDTH_LOWER(X) ((X*(100-WIDTH_TOLERANCE)+50)/100)
#define WIDTH_UPPER(X) ((X*(100+WIDTH_TOLERANCE)+50)/100)

#define ETSI_PATTERN(ID, WMIN, WMAX, PMIN, PMAX, PRF, PPB, CHIRP)	\
{								\
	ID, WIDTH_LOWER(WMIN), WIDTH_UPPER(WMAX),		\
	(PRF2PRI(PMAX) - PRI_TOLERANCE),			\
	(PRF2PRI(PMIN) * PRF + PRI_TOLERANCE), PRF, PPB * PRF,	\
	PPB_THRESH(PPB), PRI_TOLERANCE,	CHIRP			\
}

/* radar types as defined by ETSI EN-301-893 v1.5.1 */
static const struct radar_detector_specs etsi_radar_ref_types_v15[] = {
	ETSI_PATTERN(0,  0,  1,  700,  700, 1, 18, false),
	ETSI_PATTERN(1,  0,  5,  200, 1000, 1, 10, false),
	ETSI_PATTERN(2,  0, 15,  200, 1600, 1, 15, false),
	ETSI_PATTERN(3,  0, 15, 2300, 4000, 1, 25, false),
	ETSI_PATTERN(4, 20, 30, 2000, 4000, 1, 20, false),
	ETSI_PATTERN(5,  0,  2,  300,  400, 3, 10, false),
	ETSI_PATTERN(6,  0,  2,  400, 1200, 3, 15, false),
};

static const struct radar_types etsi_radar_types_v15 = {
	.region			= NL80211_DFS_ETSI,
	.num_radar_types	= ARRAY_SIZE(etsi_radar_ref_types_v15),
	.radar_types		= etsi_radar_ref_types_v15,
};

#define FCC_PATTERN(ID, WMIN, WMAX, PMIN, PMAX, PRF, PPB, CHIRP)	\
{								\
	ID, WIDTH_LOWER(WMIN), WIDTH_UPPER(WMAX),		\
	PMIN - PRI_TOLERANCE,					\
	PMAX * PRF + PRI_TOLERANCE, PRF, PPB * PRF,		\
	PPB_THRESH(PPB), PRI_TOLERANCE,	CHIRP			\
}

/* radar types released on August 14, 2014
 * type 1 PRI values randomly selected within the range of 518 and 3066.
 * divide it to 3 groups is good enough for both of radar detection and
 * avoiding false detection based on practical test results
 * collected for more than a year.
 */
static const struct radar_detector_specs fcc_radar_ref_types[] = {
	FCC_PATTERN(0, 0, 1, 1428, 1428, 1, 18, false),
	FCC_PATTERN(101, 0, 1, 518, 938, 1, 57, false),
	FCC_PATTERN(102, 0, 1, 938, 2000, 1, 27, false),
	FCC_PATTERN(103, 0, 1, 2000, 3066, 1, 18, false),
	FCC_PATTERN(2, 0, 5, 150, 230, 1, 23, false),
	FCC_PATTERN(3, 6, 10, 200, 500, 1, 16, false),
	FCC_PATTERN(4, 11, 20, 200, 500, 1, 12, false),
	FCC_PATTERN(5, 50, 100, 1000, 2000, 1, 1, true),
	FCC_PATTERN(6, 0, 1, 333, 333, 1, 9, false),
};

static const struct radar_types fcc_radar_types = {
	.region			= NL80211_DFS_FCC,
	.num_radar_types	= ARRAY_SIZE(fcc_radar_ref_types),
	.radar_types		= fcc_radar_ref_types,
};

#define JP_PATTERN(ID, WMIN, WMAX, PMIN, PMAX, PRF, PPB, RATE, CHIRP)	\
{								\
	ID, WIDTH_LOWER(WMIN), WIDTH_UPPER(WMAX),		\
	PMIN - PRI_TOLERANCE,					\
	PMAX * PRF + PRI_TOLERANCE, PRF, PPB * PRF,		\
	PPB_THRESH_RATE(PPB, RATE), PRI_TOLERANCE, CHIRP	\
}
static const struct radar_detector_specs jp_radar_ref_types[] = {
	JP_PATTERN(0, 0, 1, 1428, 1428, 1, 18, 29, false),
	JP_PATTERN(1, 2, 3, 3846, 3846, 1, 18, 29, false),
	JP_PATTERN(2, 0, 1, 1388, 1388, 1, 18, 50, false),
	JP_PATTERN(3, 1, 2, 4000, 4000, 1, 18, 50, false),
	JP_PATTERN(4, 0, 5, 150, 230, 1, 23, 50, false),
	JP_PATTERN(5, 6, 10, 200, 500, 1, 16, 50, false),
	JP_PATTERN(6, 11, 20, 200, 500, 1, 12, 50, false),
	JP_PATTERN(7, 50, 100, 1000, 2000, 1, 3, 50, true),
	JP_PATTERN(5, 0, 1, 333, 333, 1, 9, 50, false),
};

static const struct radar_types jp_radar_types = {
	.region			= NL80211_DFS_JP,
	.num_radar_types	= ARRAY_SIZE(jp_radar_ref_types),
	.radar_types		= jp_radar_ref_types,
};

static const struct radar_types *dfs_domains[] = {
	&etsi_radar_types_v15,
	&fcc_radar_types,
	&jp_radar_types,
};

/**
 * get_dfs_domain_radar_types() - get radar types for a given DFS domain
 * @param domain DFS domain
 * @return radar_types ptr on success, NULL if DFS domain is not supported
 */
static const struct radar_types *
get_dfs_domain_radar_types(enum nl80211_dfs_regions region)
{
	u32 i;
	for (i = 0; i < ARRAY_SIZE(dfs_domains); i++) {
		if (dfs_domains[i]->region == region)
			return dfs_domains[i];
	}
	return NULL;
}

/**
 * struct channel_detector - detector elements for a DFS channel
 * @head: list_head
 * @freq: frequency for this channel detector in MHz
 * @detectors: array of dynamically created detector elements for this freq
 *
 * Channel detectors are required to provide multi-channel DFS detection, e.g.
 * to support off-channel scanning. A pattern detector has a list of channels
 * radar pulses have been reported for in the past.
 */
struct channel_detector {
	struct list_head head;
	u16 freq;
	struct pri_detector **detectors;
};

/* channel_detector_reset() - reset detector lines for a given channel */
static void channel_detector_reset(struct dfs_pattern_detector *dpd,
				   struct channel_detector *cd)
{
	u32 i;
	if (cd == NULL)
		return;
	for (i = 0; i < dpd->num_radar_types; i++)
		cd->detectors[i]->reset(cd->detectors[i], dpd->last_pulse_ts);
}

/* channel_detector_exit() - destructor */
static void channel_detector_exit(struct dfs_pattern_detector *dpd,
				  struct channel_detector *cd)
{
	u32 i;
	if (cd == NULL)
		return;
	list_del(&cd->head);
	for (i = 0; i < dpd->num_radar_types; i++) {
		struct pri_detector *de = cd->detectors[i];
		if (de != NULL)
			de->exit(de);
	}
	kfree(cd->detectors);
	kfree(cd);
}

static struct channel_detector *
channel_detector_create(struct dfs_pattern_detector *dpd, u16 freq)
{
	u32 sz, i;
	struct channel_detector *cd;

	cd = kmalloc(sizeof(*cd), GFP_ATOMIC);
	if (cd == NULL)
		goto fail;

	INIT_LIST_HEAD(&cd->head);
	cd->freq = freq;
	sz = sizeof(cd->detectors) * dpd->num_radar_types;
	cd->detectors = kzalloc(sz, GFP_ATOMIC);
	if (cd->detectors == NULL)
		goto fail;

	for (i = 0; i < dpd->num_radar_types; i++) {
		const struct radar_detector_specs *rs = &dpd->radar_spec[i];
		struct pri_detector *de = pri_detector_init(rs);
		if (de == NULL)
			goto fail;
		cd->detectors[i] = de;
	}
	list_add(&cd->head, &dpd->channel_detectors);
	return cd;

fail:
	ath_dbg(dpd->common, DFS,
		"failed to allocate channel_detector for freq=%d\n", freq);
	channel_detector_exit(dpd, cd);
	return NULL;
}

/**
 * channel_detector_get() - get channel detector for given frequency
 * @param dpd instance pointer
 * @param freq frequency in MHz
 * @return pointer to channel detector on success, NULL otherwise
 *
 * Return existing channel detector for the given frequency or return a
 * newly create one.
 */
static struct channel_detector *
channel_detector_get(struct dfs_pattern_detector *dpd, u16 freq)
{
	struct channel_detector *cd;
	list_for_each_entry(cd, &dpd->channel_detectors, head) {
		if (cd->freq == freq)
			return cd;
	}
	return channel_detector_create(dpd, freq);
}

/*
 * DFS Pattern Detector
 */

/* dpd_reset(): reset all channel detectors */
static void dpd_reset(struct dfs_pattern_detector *dpd)
{
	struct channel_detector *cd;
	if (!list_empty(&dpd->channel_detectors))
		list_for_each_entry(cd, &dpd->channel_detectors, head)
			channel_detector_reset(dpd, cd);

}
static void dpd_exit(struct dfs_pattern_detector *dpd)
{
	struct channel_detector *cd, *cd0;
	if (!list_empty(&dpd->channel_detectors))
		list_for_each_entry_safe(cd, cd0, &dpd->channel_detectors, head)
			channel_detector_exit(dpd, cd);
	kfree(dpd);
}

static bool
dpd_add_pulse(struct dfs_pattern_detector *dpd, struct pulse_event *event)
{
	u32 i;
	struct channel_detector *cd;

	/*
	 * pulses received for a non-supported or un-initialized
	 * domain are treated as detected radars for fail-safety
	 */
	if (dpd->region == NL80211_DFS_UNSET)
		return true;

	cd = channel_detector_get(dpd, event->freq);
	if (cd == NULL)
		return false;

	/* reset detector on time stamp wraparound, caused by TSF reset */
	if (event->ts < dpd->last_pulse_ts)
		dpd_reset(dpd);
	dpd->last_pulse_ts = event->ts;

	/* do type individual pattern matching */
	for (i = 0; i < dpd->num_radar_types; i++) {
		struct pri_detector *pd = cd->detectors[i];
		struct pri_sequence *ps = pd->add_pulse(pd, event);
		if (ps != NULL) {
			ath_dbg(dpd->common, DFS,
				"DFS: radar found on freq=%d: id=%d, pri=%d, "
				"count=%d, count_false=%d\n",
				event->freq, pd->rs->type_id,
				ps->pri, ps->count, ps->count_falses);
			pd->reset(pd, dpd->last_pulse_ts);
			return true;
		}
	}
	return false;
}

static struct ath_dfs_pool_stats
dpd_get_stats(struct dfs_pattern_detector *dpd)
{
	return global_dfs_pool_stats;
}

static bool dpd_set_domain(struct dfs_pattern_detector *dpd,
			   enum nl80211_dfs_regions region)
{
	const struct radar_types *rt;
	struct channel_detector *cd, *cd0;

	if (dpd->region == region)
		return true;

	dpd->region = NL80211_DFS_UNSET;

	rt = get_dfs_domain_radar_types(region);
	if (rt == NULL)
		return false;

	/* delete all channel detectors for previous DFS domain */
	if (!list_empty(&dpd->channel_detectors))
		list_for_each_entry_safe(cd, cd0, &dpd->channel_detectors, head)
			channel_detector_exit(dpd, cd);
	dpd->radar_spec = rt->radar_types;
	dpd->num_radar_types = rt->num_radar_types;

	dpd->region = region;
	return true;
}

static const struct dfs_pattern_detector default_dpd = {
	.exit		= dpd_exit,
	.set_dfs_domain	= dpd_set_domain,
	.add_pulse	= dpd_add_pulse,
	.get_stats	= dpd_get_stats,
	.region		= NL80211_DFS_UNSET,
};

struct dfs_pattern_detector *
dfs_pattern_detector_init(struct ath_common *common,
			  enum nl80211_dfs_regions region)
{
	struct dfs_pattern_detector *dpd;

	if (!IS_ENABLED(CONFIG_CFG80211_CERTIFICATION_ONUS))
		return NULL;

	dpd = kmalloc(sizeof(*dpd), GFP_KERNEL);
	if (dpd == NULL)
		return NULL;

	*dpd = default_dpd;
	INIT_LIST_HEAD(&dpd->channel_detectors);

	dpd->common = common;
	if (dpd->set_dfs_domain(dpd, region))
		return dpd;

	ath_dbg(common, DFS,"Could not set DFS domain to %d", region);
	kfree(dpd);
	return NULL;
}
EXPORT_SYMBOL(dfs_pattern_detector_init);
