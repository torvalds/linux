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

/*
 * tolerated deviation of radar time stamp in usecs on both sides
 * TODO: this might need to be HW-dependent
 */
#define PRI_TOLERANCE	16

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
#define PPB_THRESH(PPB) ((PPB * MIN_PPB_THRESH + 50) / 100)
#define PRF2PRI(PRF) ((1000000 + PRF / 2) / PRF)

#define ETSI_PATTERN(ID, WMIN, WMAX, PMIN, PMAX, PRF, PPB)	\
{								\
	ID, WMIN, WMAX, (PRF2PRI(PMAX) - PRI_TOLERANCE),	\
	(PRF2PRI(PMIN) * PRF + PRI_TOLERANCE), PRF, PPB * PRF,	\
	PPB_THRESH(PPB), PRI_TOLERANCE,				\
}

/* radar types as defined by ETSI EN-301-893 v1.5.1 */
static const struct radar_detector_specs etsi_radar_ref_types_v15[] = {
	ETSI_PATTERN(0,  0,  1,  700,  700, 1, 18),
	ETSI_PATTERN(1,  0,  5,  200, 1000, 1, 10),
	ETSI_PATTERN(2,  0, 15,  200, 1600, 1, 15),
	ETSI_PATTERN(3,  0, 15, 2300, 4000, 1, 25),
	ETSI_PATTERN(4, 20, 30, 2000, 4000, 1, 20),
	ETSI_PATTERN(5,  0,  2,  300,  400, 3, 10),
	ETSI_PATTERN(6,  0,  2,  400, 1200, 3, 15),
};

static const struct radar_types etsi_radar_types_v15 = {
	.region			= NL80211_DFS_ETSI,
	.num_radar_types	= ARRAY_SIZE(etsi_radar_ref_types_v15),
	.radar_types		= etsi_radar_ref_types_v15,
};

/* for now, we support ETSI radar types, FCC and JP are TODO */
static const struct radar_types *dfs_domains[] = {
	&etsi_radar_types_v15,
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

	cd = kmalloc(sizeof(*cd), GFP_KERNEL);
	if (cd == NULL)
		goto fail;

	INIT_LIST_HEAD(&cd->head);
	cd->freq = freq;
	sz = sizeof(cd->detectors) * dpd->num_radar_types;
	cd->detectors = kzalloc(sz, GFP_KERNEL);
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
	pr_err("failed to allocate channel_detector for freq=%d\n", freq);
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
	bool ts_wraparound;
	struct channel_detector *cd;

	if (dpd->region == NL80211_DFS_UNSET) {
		/*
		 * pulses received for a non-supported or un-initialized
		 * domain are treated as detected radars
		 */
		return true;
	}

	cd = channel_detector_get(dpd, event->freq);
	if (cd == NULL)
		return false;

	ts_wraparound = (event->ts < dpd->last_pulse_ts);
	dpd->last_pulse_ts = event->ts;
	if (ts_wraparound) {
		/*
		 * reset detector on time stamp wraparound
		 * with monotonic time stamps, this should never happen
		 */
		pr_warn("DFS: time stamp wraparound detected, resetting\n");
		dpd_reset(dpd);
	}
	/* do type individual pattern matching */
	for (i = 0; i < dpd->num_radar_types; i++) {
		if (cd->detectors[i]->add_pulse(cd->detectors[i], event) != 0) {
			channel_detector_reset(dpd, cd);
			return true;
		}
	}
	return false;
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

static struct dfs_pattern_detector default_dpd = {
	.exit		= dpd_exit,
	.set_dfs_domain	= dpd_set_domain,
	.add_pulse	= dpd_add_pulse,
	.region		= NL80211_DFS_UNSET,
};

struct dfs_pattern_detector *
dfs_pattern_detector_init(enum nl80211_dfs_regions region)
{
	struct dfs_pattern_detector *dpd;
	dpd = kmalloc(sizeof(*dpd), GFP_KERNEL);
	if (dpd == NULL) {
		pr_err("allocation of dfs_pattern_detector failed\n");
		return NULL;
	}
	*dpd = default_dpd;
	INIT_LIST_HEAD(&dpd->channel_detectors);

	if (dpd->set_dfs_domain(dpd, region))
		return dpd;

	pr_err("Could not set DFS domain to %d. ", region);
	kfree(dpd);
	return NULL;
}
EXPORT_SYMBOL(dfs_pattern_detector_init);
