/*
 * Copyright (C) 2010-2011 Neil Brown
 * Copyright (C) 2010-2018 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/slab.h>
#include <linux/module.h>

#include "md.h"
#include "raid1.h"
#include "raid5.h"
#include "raid10.h"
#include "md-bitmap.h"

#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "raid"
#define	MAX_RAID_DEVICES	253 /* md-raid kernel limit */

/*
 * Minimum sectors of free reshape space per raid device
 */
#define	MIN_FREE_RESHAPE_SPACE to_sector(4*4096)

/*
 * Minimum journal space 4 MiB in sectors.
 */
#define	MIN_RAID456_JOURNAL_SPACE (4*2048)

static bool devices_handle_discard_safely = false;

/*
 * The following flags are used by dm-raid.c to set up the array state.
 * They must be cleared before md_run is called.
 */
#define FirstUse 10		/* rdev flag */

struct raid_dev {
	/*
	 * Two DM devices, one to hold metadata and one to hold the
	 * actual data/parity.	The reason for this is to not confuse
	 * ti->len and give more flexibility in altering size and
	 * characteristics.
	 *
	 * While it is possible for this device to be associated
	 * with a different physical device than the data_dev, it
	 * is intended for it to be the same.
	 *    |--------- Physical Device ---------|
	 *    |- meta_dev -|------ data_dev ------|
	 */
	struct dm_dev *meta_dev;
	struct dm_dev *data_dev;
	struct md_rdev rdev;
};

/*
 * Bits for establishing rs->ctr_flags
 *
 * 1 = no flag value
 * 2 = flag with value
 */
#define __CTR_FLAG_SYNC			0  /* 1 */ /* Not with raid0! */
#define __CTR_FLAG_NOSYNC		1  /* 1 */ /* Not with raid0! */
#define __CTR_FLAG_REBUILD		2  /* 2 */ /* Not with raid0! */
#define __CTR_FLAG_DAEMON_SLEEP		3  /* 2 */ /* Not with raid0! */
#define __CTR_FLAG_MIN_RECOVERY_RATE	4  /* 2 */ /* Not with raid0! */
#define __CTR_FLAG_MAX_RECOVERY_RATE	5  /* 2 */ /* Not with raid0! */
#define __CTR_FLAG_MAX_WRITE_BEHIND	6  /* 2 */ /* Only with raid1! */
#define __CTR_FLAG_WRITE_MOSTLY		7  /* 2 */ /* Only with raid1! */
#define __CTR_FLAG_STRIPE_CACHE		8  /* 2 */ /* Only with raid4/5/6! */
#define __CTR_FLAG_REGION_SIZE		9  /* 2 */ /* Not with raid0! */
#define __CTR_FLAG_RAID10_COPIES	10 /* 2 */ /* Only with raid10 */
#define __CTR_FLAG_RAID10_FORMAT	11 /* 2 */ /* Only with raid10 */
/* New for v1.9.0 */
#define __CTR_FLAG_DELTA_DISKS		12 /* 2 */ /* Only with reshapable raid1/4/5/6/10! */
#define __CTR_FLAG_DATA_OFFSET		13 /* 2 */ /* Only with reshapable raid4/5/6/10! */
#define __CTR_FLAG_RAID10_USE_NEAR_SETS 14 /* 2 */ /* Only with raid10! */

/* New for v1.10.0 */
#define __CTR_FLAG_JOURNAL_DEV		15 /* 2 */ /* Only with raid4/5/6 (journal device)! */

/* New for v1.11.1 */
#define __CTR_FLAG_JOURNAL_MODE		16 /* 2 */ /* Only with raid4/5/6 (journal mode)! */

/*
 * Flags for rs->ctr_flags field.
 */
#define CTR_FLAG_SYNC			(1 << __CTR_FLAG_SYNC)
#define CTR_FLAG_NOSYNC			(1 << __CTR_FLAG_NOSYNC)
#define CTR_FLAG_REBUILD		(1 << __CTR_FLAG_REBUILD)
#define CTR_FLAG_DAEMON_SLEEP		(1 << __CTR_FLAG_DAEMON_SLEEP)
#define CTR_FLAG_MIN_RECOVERY_RATE	(1 << __CTR_FLAG_MIN_RECOVERY_RATE)
#define CTR_FLAG_MAX_RECOVERY_RATE	(1 << __CTR_FLAG_MAX_RECOVERY_RATE)
#define CTR_FLAG_MAX_WRITE_BEHIND	(1 << __CTR_FLAG_MAX_WRITE_BEHIND)
#define CTR_FLAG_WRITE_MOSTLY		(1 << __CTR_FLAG_WRITE_MOSTLY)
#define CTR_FLAG_STRIPE_CACHE		(1 << __CTR_FLAG_STRIPE_CACHE)
#define CTR_FLAG_REGION_SIZE		(1 << __CTR_FLAG_REGION_SIZE)
#define CTR_FLAG_RAID10_COPIES		(1 << __CTR_FLAG_RAID10_COPIES)
#define CTR_FLAG_RAID10_FORMAT		(1 << __CTR_FLAG_RAID10_FORMAT)
#define CTR_FLAG_DELTA_DISKS		(1 << __CTR_FLAG_DELTA_DISKS)
#define CTR_FLAG_DATA_OFFSET		(1 << __CTR_FLAG_DATA_OFFSET)
#define CTR_FLAG_RAID10_USE_NEAR_SETS	(1 << __CTR_FLAG_RAID10_USE_NEAR_SETS)
#define CTR_FLAG_JOURNAL_DEV		(1 << __CTR_FLAG_JOURNAL_DEV)
#define CTR_FLAG_JOURNAL_MODE		(1 << __CTR_FLAG_JOURNAL_MODE)

/*
 * Definitions of various constructor flags to
 * be used in checks of valid / invalid flags
 * per raid level.
 */
/* Define all any sync flags */
#define	CTR_FLAGS_ANY_SYNC		(CTR_FLAG_SYNC | CTR_FLAG_NOSYNC)

/* Define flags for options without argument (e.g. 'nosync') */
#define	CTR_FLAG_OPTIONS_NO_ARGS	(CTR_FLAGS_ANY_SYNC | \
					 CTR_FLAG_RAID10_USE_NEAR_SETS)

/* Define flags for options with one argument (e.g. 'delta_disks +2') */
#define CTR_FLAG_OPTIONS_ONE_ARG (CTR_FLAG_REBUILD | \
				  CTR_FLAG_WRITE_MOSTLY | \
				  CTR_FLAG_DAEMON_SLEEP | \
				  CTR_FLAG_MIN_RECOVERY_RATE | \
				  CTR_FLAG_MAX_RECOVERY_RATE | \
				  CTR_FLAG_MAX_WRITE_BEHIND | \
				  CTR_FLAG_STRIPE_CACHE | \
				  CTR_FLAG_REGION_SIZE | \
				  CTR_FLAG_RAID10_COPIES | \
				  CTR_FLAG_RAID10_FORMAT | \
				  CTR_FLAG_DELTA_DISKS | \
				  CTR_FLAG_DATA_OFFSET | \
				  CTR_FLAG_JOURNAL_DEV | \
				  CTR_FLAG_JOURNAL_MODE)

/* Valid options definitions per raid level... */

/* "raid0" does only accept data offset */
#define RAID0_VALID_FLAGS	(CTR_FLAG_DATA_OFFSET)

/* "raid1" does not accept stripe cache, data offset, delta_disks or any raid10 options */
#define RAID1_VALID_FLAGS	(CTR_FLAGS_ANY_SYNC | \
				 CTR_FLAG_REBUILD | \
				 CTR_FLAG_WRITE_MOSTLY | \
				 CTR_FLAG_DAEMON_SLEEP | \
				 CTR_FLAG_MIN_RECOVERY_RATE | \
				 CTR_FLAG_MAX_RECOVERY_RATE | \
				 CTR_FLAG_MAX_WRITE_BEHIND | \
				 CTR_FLAG_REGION_SIZE | \
				 CTR_FLAG_DELTA_DISKS | \
				 CTR_FLAG_DATA_OFFSET)

/* "raid10" does not accept any raid1 or stripe cache options */
#define RAID10_VALID_FLAGS	(CTR_FLAGS_ANY_SYNC | \
				 CTR_FLAG_REBUILD | \
				 CTR_FLAG_DAEMON_SLEEP | \
				 CTR_FLAG_MIN_RECOVERY_RATE | \
				 CTR_FLAG_MAX_RECOVERY_RATE | \
				 CTR_FLAG_REGION_SIZE | \
				 CTR_FLAG_RAID10_COPIES | \
				 CTR_FLAG_RAID10_FORMAT | \
				 CTR_FLAG_DELTA_DISKS | \
				 CTR_FLAG_DATA_OFFSET | \
				 CTR_FLAG_RAID10_USE_NEAR_SETS)

/*
 * "raid4/5/6" do not accept any raid1 or raid10 specific options
 *
 * "raid6" does not accept "nosync", because it is not guaranteed
 * that both parity and q-syndrome are being written properly with
 * any writes
 */
#define RAID45_VALID_FLAGS	(CTR_FLAGS_ANY_SYNC | \
				 CTR_FLAG_REBUILD | \
				 CTR_FLAG_DAEMON_SLEEP | \
				 CTR_FLAG_MIN_RECOVERY_RATE | \
				 CTR_FLAG_MAX_RECOVERY_RATE | \
				 CTR_FLAG_STRIPE_CACHE | \
				 CTR_FLAG_REGION_SIZE | \
				 CTR_FLAG_DELTA_DISKS | \
				 CTR_FLAG_DATA_OFFSET | \
				 CTR_FLAG_JOURNAL_DEV | \
				 CTR_FLAG_JOURNAL_MODE)

#define RAID6_VALID_FLAGS	(CTR_FLAG_SYNC | \
				 CTR_FLAG_REBUILD | \
				 CTR_FLAG_DAEMON_SLEEP | \
				 CTR_FLAG_MIN_RECOVERY_RATE | \
				 CTR_FLAG_MAX_RECOVERY_RATE | \
				 CTR_FLAG_STRIPE_CACHE | \
				 CTR_FLAG_REGION_SIZE | \
				 CTR_FLAG_DELTA_DISKS | \
				 CTR_FLAG_DATA_OFFSET | \
				 CTR_FLAG_JOURNAL_DEV | \
				 CTR_FLAG_JOURNAL_MODE)
/* ...valid options definitions per raid level */

/*
 * Flags for rs->runtime_flags field
 * (RT_FLAG prefix meaning "runtime flag")
 *
 * These are all internal and used to define runtime state,
 * e.g. to prevent another resume from preresume processing
 * the raid set all over again.
 */
#define RT_FLAG_RS_PRERESUMED		0
#define RT_FLAG_RS_RESUMED		1
#define RT_FLAG_RS_BITMAP_LOADED	2
#define RT_FLAG_UPDATE_SBS		3
#define RT_FLAG_RESHAPE_RS		4
#define RT_FLAG_RS_SUSPENDED		5
#define RT_FLAG_RS_IN_SYNC		6
#define RT_FLAG_RS_RESYNCING		7
#define RT_FLAG_RS_GROW			8

/* Array elements of 64 bit needed for rebuild/failed disk bits */
#define DISKS_ARRAY_ELEMS ((MAX_RAID_DEVICES + (sizeof(uint64_t) * 8 - 1)) / sizeof(uint64_t) / 8)

/*
 * raid set level, layout and chunk sectors backup/restore
 */
struct rs_layout {
	int new_level;
	int new_layout;
	int new_chunk_sectors;
};

struct raid_set {
	struct dm_target *ti;

	uint32_t stripe_cache_entries;
	unsigned long ctr_flags;
	unsigned long runtime_flags;

	uint64_t rebuild_disks[DISKS_ARRAY_ELEMS];

	int raid_disks;
	int delta_disks;
	int data_offset;
	int raid10_copies;
	int requested_bitmap_chunk_sectors;

	struct mddev md;
	struct raid_type *raid_type;

	sector_t array_sectors;
	sector_t dev_sectors;

	/* Optional raid4/5/6 journal device */
	struct journal_dev {
		struct dm_dev *dev;
		struct md_rdev rdev;
		int mode;
	} journal_dev;

	struct raid_dev dev[];
};

static void rs_config_backup(struct raid_set *rs, struct rs_layout *l)
{
	struct mddev *mddev = &rs->md;

	l->new_level = mddev->new_level;
	l->new_layout = mddev->new_layout;
	l->new_chunk_sectors = mddev->new_chunk_sectors;
}

static void rs_config_restore(struct raid_set *rs, struct rs_layout *l)
{
	struct mddev *mddev = &rs->md;

	mddev->new_level = l->new_level;
	mddev->new_layout = l->new_layout;
	mddev->new_chunk_sectors = l->new_chunk_sectors;
}

/* raid10 algorithms (i.e. formats) */
#define	ALGORITHM_RAID10_DEFAULT	0
#define	ALGORITHM_RAID10_NEAR		1
#define	ALGORITHM_RAID10_OFFSET		2
#define	ALGORITHM_RAID10_FAR		3

/* Supported raid types and properties. */
static struct raid_type {
	const char *name;		/* RAID algorithm. */
	const char *descr;		/* Descriptor text for logging. */
	const unsigned int parity_devs;	/* # of parity devices. */
	const unsigned int minimal_devs;/* minimal # of devices in set. */
	const unsigned int level;	/* RAID level. */
	const unsigned int algorithm;	/* RAID algorithm. */
} raid_types[] = {
	{"raid0",	  "raid0 (striping)",			    0, 2, 0,  0 /* NONE */},
	{"raid1",	  "raid1 (mirroring)",			    0, 2, 1,  0 /* NONE */},
	{"raid10_far",	  "raid10 far (striped mirrors)",	    0, 2, 10, ALGORITHM_RAID10_FAR},
	{"raid10_offset", "raid10 offset (striped mirrors)",	    0, 2, 10, ALGORITHM_RAID10_OFFSET},
	{"raid10_near",	  "raid10 near (striped mirrors)",	    0, 2, 10, ALGORITHM_RAID10_NEAR},
	{"raid10",	  "raid10 (striped mirrors)",		    0, 2, 10, ALGORITHM_RAID10_DEFAULT},
	{"raid4",	  "raid4 (dedicated first parity disk)",    1, 2, 5,  ALGORITHM_PARITY_0}, /* raid4 layout = raid5_0 */
	{"raid5_n",	  "raid5 (dedicated last parity disk)",	    1, 2, 5,  ALGORITHM_PARITY_N},
	{"raid5_ls",	  "raid5 (left symmetric)",		    1, 2, 5,  ALGORITHM_LEFT_SYMMETRIC},
	{"raid5_rs",	  "raid5 (right symmetric)",		    1, 2, 5,  ALGORITHM_RIGHT_SYMMETRIC},
	{"raid5_la",	  "raid5 (left asymmetric)",		    1, 2, 5,  ALGORITHM_LEFT_ASYMMETRIC},
	{"raid5_ra",	  "raid5 (right asymmetric)",		    1, 2, 5,  ALGORITHM_RIGHT_ASYMMETRIC},
	{"raid6_zr",	  "raid6 (zero restart)",		    2, 4, 6,  ALGORITHM_ROTATING_ZERO_RESTART},
	{"raid6_nr",	  "raid6 (N restart)",			    2, 4, 6,  ALGORITHM_ROTATING_N_RESTART},
	{"raid6_nc",	  "raid6 (N continue)",			    2, 4, 6,  ALGORITHM_ROTATING_N_CONTINUE},
	{"raid6_n_6",	  "raid6 (dedicated parity/Q n/6)",	    2, 4, 6,  ALGORITHM_PARITY_N_6},
	{"raid6_ls_6",	  "raid6 (left symmetric dedicated Q 6)",   2, 4, 6,  ALGORITHM_LEFT_SYMMETRIC_6},
	{"raid6_rs_6",	  "raid6 (right symmetric dedicated Q 6)",  2, 4, 6,  ALGORITHM_RIGHT_SYMMETRIC_6},
	{"raid6_la_6",	  "raid6 (left asymmetric dedicated Q 6)",  2, 4, 6,  ALGORITHM_LEFT_ASYMMETRIC_6},
	{"raid6_ra_6",	  "raid6 (right asymmetric dedicated Q 6)", 2, 4, 6,  ALGORITHM_RIGHT_ASYMMETRIC_6}
};

/* True, if @v is in inclusive range [@min, @max] */
static bool __within_range(long v, long min, long max)
{
	return v >= min && v <= max;
}

/* All table line arguments are defined here */
static struct arg_name_flag {
	const unsigned long flag;
	const char *name;
} __arg_name_flags[] = {
	{ CTR_FLAG_SYNC, "sync"},
	{ CTR_FLAG_NOSYNC, "nosync"},
	{ CTR_FLAG_REBUILD, "rebuild"},
	{ CTR_FLAG_DAEMON_SLEEP, "daemon_sleep"},
	{ CTR_FLAG_MIN_RECOVERY_RATE, "min_recovery_rate"},
	{ CTR_FLAG_MAX_RECOVERY_RATE, "max_recovery_rate"},
	{ CTR_FLAG_MAX_WRITE_BEHIND, "max_write_behind"},
	{ CTR_FLAG_WRITE_MOSTLY, "write_mostly"},
	{ CTR_FLAG_STRIPE_CACHE, "stripe_cache"},
	{ CTR_FLAG_REGION_SIZE, "region_size"},
	{ CTR_FLAG_RAID10_COPIES, "raid10_copies"},
	{ CTR_FLAG_RAID10_FORMAT, "raid10_format"},
	{ CTR_FLAG_DATA_OFFSET, "data_offset"},
	{ CTR_FLAG_DELTA_DISKS, "delta_disks"},
	{ CTR_FLAG_RAID10_USE_NEAR_SETS, "raid10_use_near_sets"},
	{ CTR_FLAG_JOURNAL_DEV, "journal_dev" },
	{ CTR_FLAG_JOURNAL_MODE, "journal_mode" },
};

/* Return argument name string for given @flag */
static const char *dm_raid_arg_name_by_flag(const uint32_t flag)
{
	if (hweight32(flag) == 1) {
		struct arg_name_flag *anf = __arg_name_flags + ARRAY_SIZE(__arg_name_flags);

		while (anf-- > __arg_name_flags)
			if (flag & anf->flag)
				return anf->name;

	} else
		DMERR("%s called with more than one flag!", __func__);

	return NULL;
}

/* Define correlation of raid456 journal cache modes and dm-raid target line parameters */
static struct {
	const int mode;
	const char *param;
} _raid456_journal_mode[] = {
	{ R5C_JOURNAL_MODE_WRITE_THROUGH , "writethrough" },
	{ R5C_JOURNAL_MODE_WRITE_BACK    , "writeback" }
};

/* Return MD raid4/5/6 journal mode for dm @journal_mode one */
static int dm_raid_journal_mode_to_md(const char *mode)
{
	int m = ARRAY_SIZE(_raid456_journal_mode);

	while (m--)
		if (!strcasecmp(mode, _raid456_journal_mode[m].param))
			return _raid456_journal_mode[m].mode;

	return -EINVAL;
}

/* Return dm-raid raid4/5/6 journal mode string for @mode */
static const char *md_journal_mode_to_dm_raid(const int mode)
{
	int m = ARRAY_SIZE(_raid456_journal_mode);

	while (m--)
		if (mode == _raid456_journal_mode[m].mode)
			return _raid456_journal_mode[m].param;

	return "unknown";
}

/*
 * Bool helpers to test for various raid levels of a raid set.
 * It's level as reported by the superblock rather than
 * the requested raid_type passed to the constructor.
 */
/* Return true, if raid set in @rs is raid0 */
static bool rs_is_raid0(struct raid_set *rs)
{
	return !rs->md.level;
}

/* Return true, if raid set in @rs is raid1 */
static bool rs_is_raid1(struct raid_set *rs)
{
	return rs->md.level == 1;
}

/* Return true, if raid set in @rs is raid10 */
static bool rs_is_raid10(struct raid_set *rs)
{
	return rs->md.level == 10;
}

/* Return true, if raid set in @rs is level 6 */
static bool rs_is_raid6(struct raid_set *rs)
{
	return rs->md.level == 6;
}

/* Return true, if raid set in @rs is level 4, 5 or 6 */
static bool rs_is_raid456(struct raid_set *rs)
{
	return __within_range(rs->md.level, 4, 6);
}

/* Return true, if raid set in @rs is reshapable */
static bool __is_raid10_far(int layout);
static bool rs_is_reshapable(struct raid_set *rs)
{
	return rs_is_raid456(rs) ||
	       (rs_is_raid10(rs) && !__is_raid10_far(rs->md.new_layout));
}

/* Return true, if raid set in @rs is recovering */
static bool rs_is_recovering(struct raid_set *rs)
{
	return rs->md.recovery_cp < rs->md.dev_sectors;
}

/* Return true, if raid set in @rs is reshaping */
static bool rs_is_reshaping(struct raid_set *rs)
{
	return rs->md.reshape_position != MaxSector;
}

/*
 * bool helpers to test for various raid levels of a raid type @rt
 */

/* Return true, if raid type in @rt is raid0 */
static bool rt_is_raid0(struct raid_type *rt)
{
	return !rt->level;
}

/* Return true, if raid type in @rt is raid1 */
static bool rt_is_raid1(struct raid_type *rt)
{
	return rt->level == 1;
}

/* Return true, if raid type in @rt is raid10 */
static bool rt_is_raid10(struct raid_type *rt)
{
	return rt->level == 10;
}

/* Return true, if raid type in @rt is raid4/5 */
static bool rt_is_raid45(struct raid_type *rt)
{
	return __within_range(rt->level, 4, 5);
}

/* Return true, if raid type in @rt is raid6 */
static bool rt_is_raid6(struct raid_type *rt)
{
	return rt->level == 6;
}

/* Return true, if raid type in @rt is raid4/5/6 */
static bool rt_is_raid456(struct raid_type *rt)
{
	return __within_range(rt->level, 4, 6);
}
/* END: raid level bools */

/* Return valid ctr flags for the raid level of @rs */
static unsigned long __valid_flags(struct raid_set *rs)
{
	if (rt_is_raid0(rs->raid_type))
		return RAID0_VALID_FLAGS;
	else if (rt_is_raid1(rs->raid_type))
		return RAID1_VALID_FLAGS;
	else if (rt_is_raid10(rs->raid_type))
		return RAID10_VALID_FLAGS;
	else if (rt_is_raid45(rs->raid_type))
		return RAID45_VALID_FLAGS;
	else if (rt_is_raid6(rs->raid_type))
		return RAID6_VALID_FLAGS;

	return 0;
}

/*
 * Check for valid flags set on @rs
 *
 * Has to be called after parsing of the ctr flags!
 */
static int rs_check_for_valid_flags(struct raid_set *rs)
{
	if (rs->ctr_flags & ~__valid_flags(rs)) {
		rs->ti->error = "Invalid flags combination";
		return -EINVAL;
	}

	return 0;
}

/* MD raid10 bit definitions and helpers */
#define RAID10_OFFSET			(1 << 16) /* stripes with data copies area adjacent on devices */
#define RAID10_BROCKEN_USE_FAR_SETS	(1 << 17) /* Broken in raid10.c: use sets instead of whole stripe rotation */
#define RAID10_USE_FAR_SETS		(1 << 18) /* Use sets instead of whole stripe rotation */
#define RAID10_FAR_COPIES_SHIFT		8	  /* raid10 # far copies shift (2nd byte of layout) */

/* Return md raid10 near copies for @layout */
static unsigned int __raid10_near_copies(int layout)
{
	return layout & 0xFF;
}

/* Return md raid10 far copies for @layout */
static unsigned int __raid10_far_copies(int layout)
{
	return __raid10_near_copies(layout >> RAID10_FAR_COPIES_SHIFT);
}

/* Return true if md raid10 offset for @layout */
static bool __is_raid10_offset(int layout)
{
	return !!(layout & RAID10_OFFSET);
}

/* Return true if md raid10 near for @layout */
static bool __is_raid10_near(int layout)
{
	return !__is_raid10_offset(layout) && __raid10_near_copies(layout) > 1;
}

/* Return true if md raid10 far for @layout */
static bool __is_raid10_far(int layout)
{
	return !__is_raid10_offset(layout) && __raid10_far_copies(layout) > 1;
}

/* Return md raid10 layout string for @layout */
static const char *raid10_md_layout_to_format(int layout)
{
	/*
	 * Bit 16 stands for "offset"
	 * (i.e. adjacent stripes hold copies)
	 *
	 * Refer to MD's raid10.c for details
	 */
	if (__is_raid10_offset(layout))
		return "offset";

	if (__raid10_near_copies(layout) > 1)
		return "near";

	if (__raid10_far_copies(layout) > 1)
		return "far";

	return "unknown";
}

/* Return md raid10 algorithm for @name */
static int raid10_name_to_format(const char *name)
{
	if (!strcasecmp(name, "near"))
		return ALGORITHM_RAID10_NEAR;
	else if (!strcasecmp(name, "offset"))
		return ALGORITHM_RAID10_OFFSET;
	else if (!strcasecmp(name, "far"))
		return ALGORITHM_RAID10_FAR;

	return -EINVAL;
}

/* Return md raid10 copies for @layout */
static unsigned int raid10_md_layout_to_copies(int layout)
{
	return max(__raid10_near_copies(layout), __raid10_far_copies(layout));
}

/* Return md raid10 format id for @format string */
static int raid10_format_to_md_layout(struct raid_set *rs,
				      unsigned int algorithm,
				      unsigned int copies)
{
	unsigned int n = 1, f = 1, r = 0;

	/*
	 * MD resilienece flaw:
	 *
	 * enabling use_far_sets for far/offset formats causes copies
	 * to be colocated on the same devs together with their origins!
	 *
	 * -> disable it for now in the definition above
	 */
	if (algorithm == ALGORITHM_RAID10_DEFAULT ||
	    algorithm == ALGORITHM_RAID10_NEAR)
		n = copies;

	else if (algorithm == ALGORITHM_RAID10_OFFSET) {
		f = copies;
		r = RAID10_OFFSET;
		if (!test_bit(__CTR_FLAG_RAID10_USE_NEAR_SETS, &rs->ctr_flags))
			r |= RAID10_USE_FAR_SETS;

	} else if (algorithm == ALGORITHM_RAID10_FAR) {
		f = copies;
		if (!test_bit(__CTR_FLAG_RAID10_USE_NEAR_SETS, &rs->ctr_flags))
			r |= RAID10_USE_FAR_SETS;

	} else
		return -EINVAL;

	return r | (f << RAID10_FAR_COPIES_SHIFT) | n;
}
/* END: MD raid10 bit definitions and helpers */

/* Check for any of the raid10 algorithms */
static bool __got_raid10(struct raid_type *rtp, const int layout)
{
	if (rtp->level == 10) {
		switch (rtp->algorithm) {
		case ALGORITHM_RAID10_DEFAULT:
		case ALGORITHM_RAID10_NEAR:
			return __is_raid10_near(layout);
		case ALGORITHM_RAID10_OFFSET:
			return __is_raid10_offset(layout);
		case ALGORITHM_RAID10_FAR:
			return __is_raid10_far(layout);
		default:
			break;
		}
	}

	return false;
}

/* Return raid_type for @name */
static struct raid_type *get_raid_type(const char *name)
{
	struct raid_type *rtp = raid_types + ARRAY_SIZE(raid_types);

	while (rtp-- > raid_types)
		if (!strcasecmp(rtp->name, name))
			return rtp;

	return NULL;
}

/* Return raid_type for @name based derived from @level and @layout */
static struct raid_type *get_raid_type_by_ll(const int level, const int layout)
{
	struct raid_type *rtp = raid_types + ARRAY_SIZE(raid_types);

	while (rtp-- > raid_types) {
		/* RAID10 special checks based on @layout flags/properties */
		if (rtp->level == level &&
		    (__got_raid10(rtp, layout) || rtp->algorithm == layout))
			return rtp;
	}

	return NULL;
}

/* Adjust rdev sectors */
static void rs_set_rdev_sectors(struct raid_set *rs)
{
	struct mddev *mddev = &rs->md;
	struct md_rdev *rdev;

	/*
	 * raid10 sets rdev->sector to the device size, which
	 * is unintended in case of out-of-place reshaping
	 */
	rdev_for_each(rdev, mddev)
		if (!test_bit(Journal, &rdev->flags))
			rdev->sectors = mddev->dev_sectors;
}

/*
 * Change bdev capacity of @rs in case of a disk add/remove reshape
 */
static void rs_set_capacity(struct raid_set *rs)
{
	struct gendisk *gendisk = dm_disk(dm_table_get_md(rs->ti->table));

	set_capacity_and_notify(gendisk, rs->md.array_sectors);
}

/*
 * Set the mddev properties in @rs to the current
 * ones retrieved from the freshest superblock
 */
static void rs_set_cur(struct raid_set *rs)
{
	struct mddev *mddev = &rs->md;

	mddev->new_level = mddev->level;
	mddev->new_layout = mddev->layout;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
}

/*
 * Set the mddev properties in @rs to the new
 * ones requested by the ctr
 */
static void rs_set_new(struct raid_set *rs)
{
	struct mddev *mddev = &rs->md;

	mddev->level = mddev->new_level;
	mddev->layout = mddev->new_layout;
	mddev->chunk_sectors = mddev->new_chunk_sectors;
	mddev->raid_disks = rs->raid_disks;
	mddev->delta_disks = 0;
}

static struct raid_set *raid_set_alloc(struct dm_target *ti, struct raid_type *raid_type,
				       unsigned int raid_devs)
{
	unsigned int i;
	struct raid_set *rs;

	if (raid_devs <= raid_type->parity_devs) {
		ti->error = "Insufficient number of devices";
		return ERR_PTR(-EINVAL);
	}

	rs = kzalloc(struct_size(rs, dev, raid_devs), GFP_KERNEL);
	if (!rs) {
		ti->error = "Cannot allocate raid context";
		return ERR_PTR(-ENOMEM);
	}

	mddev_init(&rs->md);

	rs->raid_disks = raid_devs;
	rs->delta_disks = 0;

	rs->ti = ti;
	rs->raid_type = raid_type;
	rs->stripe_cache_entries = 256;
	rs->md.raid_disks = raid_devs;
	rs->md.level = raid_type->level;
	rs->md.new_level = rs->md.level;
	rs->md.layout = raid_type->algorithm;
	rs->md.new_layout = rs->md.layout;
	rs->md.delta_disks = 0;
	rs->md.recovery_cp = MaxSector;

	for (i = 0; i < raid_devs; i++)
		md_rdev_init(&rs->dev[i].rdev);

	/*
	 * Remaining items to be initialized by further RAID params:
	 *  rs->md.persistent
	 *  rs->md.external
	 *  rs->md.chunk_sectors
	 *  rs->md.new_chunk_sectors
	 *  rs->md.dev_sectors
	 */

	return rs;
}

/* Free all @rs allocations */
static void raid_set_free(struct raid_set *rs)
{
	int i;

	if (rs->journal_dev.dev) {
		md_rdev_clear(&rs->journal_dev.rdev);
		dm_put_device(rs->ti, rs->journal_dev.dev);
	}

	for (i = 0; i < rs->raid_disks; i++) {
		if (rs->dev[i].meta_dev)
			dm_put_device(rs->ti, rs->dev[i].meta_dev);
		md_rdev_clear(&rs->dev[i].rdev);
		if (rs->dev[i].data_dev)
			dm_put_device(rs->ti, rs->dev[i].data_dev);
	}

	kfree(rs);
}

/*
 * For every device we have two words
 *  <meta_dev>: meta device name or '-' if missing
 *  <data_dev>: data device name or '-' if missing
 *
 * The following are permitted:
 *    - -
 *    - <data_dev>
 *    <meta_dev> <data_dev>
 *
 * The following is not allowed:
 *    <meta_dev> -
 *
 * This code parses those words.  If there is a failure,
 * the caller must use raid_set_free() to unwind the operations.
 */
static int parse_dev_params(struct raid_set *rs, struct dm_arg_set *as)
{
	int i;
	int rebuild = 0;
	int metadata_available = 0;
	int r = 0;
	const char *arg;

	/* Put off the number of raid devices argument to get to dev pairs */
	arg = dm_shift_arg(as);
	if (!arg)
		return -EINVAL;

	for (i = 0; i < rs->raid_disks; i++) {
		rs->dev[i].rdev.raid_disk = i;

		rs->dev[i].meta_dev = NULL;
		rs->dev[i].data_dev = NULL;

		/*
		 * There are no offsets initially.
		 * Out of place reshape will set them accordingly.
		 */
		rs->dev[i].rdev.data_offset = 0;
		rs->dev[i].rdev.new_data_offset = 0;
		rs->dev[i].rdev.mddev = &rs->md;

		arg = dm_shift_arg(as);
		if (!arg)
			return -EINVAL;

		if (strcmp(arg, "-")) {
			r = dm_get_device(rs->ti, arg, dm_table_get_mode(rs->ti->table),
					  &rs->dev[i].meta_dev);
			if (r) {
				rs->ti->error = "RAID metadata device lookup failure";
				return r;
			}

			rs->dev[i].rdev.sb_page = alloc_page(GFP_KERNEL);
			if (!rs->dev[i].rdev.sb_page) {
				rs->ti->error = "Failed to allocate superblock page";
				return -ENOMEM;
			}
		}

		arg = dm_shift_arg(as);
		if (!arg)
			return -EINVAL;

		if (!strcmp(arg, "-")) {
			if (!test_bit(In_sync, &rs->dev[i].rdev.flags) &&
			    (!rs->dev[i].rdev.recovery_offset)) {
				rs->ti->error = "Drive designated for rebuild not specified";
				return -EINVAL;
			}

			if (rs->dev[i].meta_dev) {
				rs->ti->error = "No data device supplied with metadata device";
				return -EINVAL;
			}

			continue;
		}

		r = dm_get_device(rs->ti, arg, dm_table_get_mode(rs->ti->table),
				  &rs->dev[i].data_dev);
		if (r) {
			rs->ti->error = "RAID device lookup failure";
			return r;
		}

		if (rs->dev[i].meta_dev) {
			metadata_available = 1;
			rs->dev[i].rdev.meta_bdev = rs->dev[i].meta_dev->bdev;
		}
		rs->dev[i].rdev.bdev = rs->dev[i].data_dev->bdev;
		list_add_tail(&rs->dev[i].rdev.same_set, &rs->md.disks);
		if (!test_bit(In_sync, &rs->dev[i].rdev.flags))
			rebuild++;
	}

	if (rs->journal_dev.dev)
		list_add_tail(&rs->journal_dev.rdev.same_set, &rs->md.disks);

	if (metadata_available) {
		rs->md.external = 0;
		rs->md.persistent = 1;
		rs->md.major_version = 2;
	} else if (rebuild && !rs->md.recovery_cp) {
		/*
		 * Without metadata, we will not be able to tell if the array
		 * is in-sync or not - we must assume it is not.  Therefore,
		 * it is impossible to rebuild a drive.
		 *
		 * Even if there is metadata, the on-disk information may
		 * indicate that the array is not in-sync and it will then
		 * fail at that time.
		 *
		 * User could specify 'nosync' option if desperate.
		 */
		rs->ti->error = "Unable to rebuild drive while array is not in-sync";
		return -EINVAL;
	}

	return 0;
}

/*
 * validate_region_size
 * @rs
 * @region_size:  region size in sectors.  If 0, pick a size (4MiB default).
 *
 * Set rs->md.bitmap_info.chunksize (which really refers to 'region size').
 * Ensure that (ti->len/region_size < 2^21) - required by MD bitmap.
 *
 * Returns: 0 on success, -EINVAL on failure.
 */
static int validate_region_size(struct raid_set *rs, unsigned long region_size)
{
	unsigned long min_region_size = rs->ti->len / (1 << 21);

	if (rs_is_raid0(rs))
		return 0;

	if (!region_size) {
		/*
		 * Choose a reasonable default.	 All figures in sectors.
		 */
		if (min_region_size > (1 << 13)) {
			/* If not a power of 2, make it the next power of 2 */
			region_size = roundup_pow_of_two(min_region_size);
			DMINFO("Choosing default region size of %lu sectors",
			       region_size);
		} else {
			DMINFO("Choosing default region size of 4MiB");
			region_size = 1 << 13; /* sectors */
		}
	} else {
		/*
		 * Validate user-supplied value.
		 */
		if (region_size > rs->ti->len) {
			rs->ti->error = "Supplied region size is too large";
			return -EINVAL;
		}

		if (region_size < min_region_size) {
			DMERR("Supplied region_size (%lu sectors) below minimum (%lu)",
			      region_size, min_region_size);
			rs->ti->error = "Supplied region size is too small";
			return -EINVAL;
		}

		if (!is_power_of_2(region_size)) {
			rs->ti->error = "Region size is not a power of 2";
			return -EINVAL;
		}

		if (region_size < rs->md.chunk_sectors) {
			rs->ti->error = "Region size is smaller than the chunk size";
			return -EINVAL;
		}
	}

	/*
	 * Convert sectors to bytes.
	 */
	rs->md.bitmap_info.chunksize = to_bytes(region_size);

	return 0;
}

/*
 * validate_raid_redundancy
 * @rs
 *
 * Determine if there are enough devices in the array that haven't
 * failed (or are being rebuilt) to form a usable array.
 *
 * Returns: 0 on success, -EINVAL on failure.
 */
static int validate_raid_redundancy(struct raid_set *rs)
{
	unsigned int i, rebuild_cnt = 0;
	unsigned int rebuilds_per_group = 0, copies, raid_disks;
	unsigned int group_size, last_group_start;

	for (i = 0; i < rs->raid_disks; i++)
		if (!test_bit(FirstUse, &rs->dev[i].rdev.flags) &&
		    ((!test_bit(In_sync, &rs->dev[i].rdev.flags) ||
		      !rs->dev[i].rdev.sb_page)))
			rebuild_cnt++;

	switch (rs->md.level) {
	case 0:
		break;
	case 1:
		if (rebuild_cnt >= rs->md.raid_disks)
			goto too_many;
		break;
	case 4:
	case 5:
	case 6:
		if (rebuild_cnt > rs->raid_type->parity_devs)
			goto too_many;
		break;
	case 10:
		copies = raid10_md_layout_to_copies(rs->md.new_layout);
		if (copies < 2) {
			DMERR("Bogus raid10 data copies < 2!");
			return -EINVAL;
		}

		if (rebuild_cnt < copies)
			break;

		/*
		 * It is possible to have a higher rebuild count for RAID10,
		 * as long as the failed devices occur in different mirror
		 * groups (i.e. different stripes).
		 *
		 * When checking "near" format, make sure no adjacent devices
		 * have failed beyond what can be handled.  In addition to the
		 * simple case where the number of devices is a multiple of the
		 * number of copies, we must also handle cases where the number
		 * of devices is not a multiple of the number of copies.
		 * E.g.	   dev1 dev2 dev3 dev4 dev5
		 *	    A	 A    B	   B	C
		 *	    C	 D    D	   E	E
		 */
		raid_disks = min(rs->raid_disks, rs->md.raid_disks);
		if (__is_raid10_near(rs->md.new_layout)) {
			for (i = 0; i < raid_disks; i++) {
				if (!(i % copies))
					rebuilds_per_group = 0;
				if ((!rs->dev[i].rdev.sb_page ||
				    !test_bit(In_sync, &rs->dev[i].rdev.flags)) &&
				    (++rebuilds_per_group >= copies))
					goto too_many;
			}
			break;
		}

		/*
		 * When checking "far" and "offset" formats, we need to ensure
		 * that the device that holds its copy is not also dead or
		 * being rebuilt.  (Note that "far" and "offset" formats only
		 * support two copies right now.  These formats also only ever
		 * use the 'use_far_sets' variant.)
		 *
		 * This check is somewhat complicated by the need to account
		 * for arrays that are not a multiple of (far) copies.	This
		 * results in the need to treat the last (potentially larger)
		 * set differently.
		 */
		group_size = (raid_disks / copies);
		last_group_start = (raid_disks / group_size) - 1;
		last_group_start *= group_size;
		for (i = 0; i < raid_disks; i++) {
			if (!(i % copies) && !(i > last_group_start))
				rebuilds_per_group = 0;
			if ((!rs->dev[i].rdev.sb_page ||
			     !test_bit(In_sync, &rs->dev[i].rdev.flags)) &&
			    (++rebuilds_per_group >= copies))
					goto too_many;
		}
		break;
	default:
		if (rebuild_cnt)
			return -EINVAL;
	}

	return 0;

too_many:
	return -EINVAL;
}

/*
 * Possible arguments are...
 *	<chunk_size> [optional_args]
 *
 * Argument definitions
 *    <chunk_size>			The number of sectors per disk that
 *					will form the "stripe"
 *    [[no]sync]			Force or prevent recovery of the
 *					entire array
 *    [rebuild <idx>]			Rebuild the drive indicated by the index
 *    [daemon_sleep <ms>]		Time between bitmap daemon work to
 *					clear bits
 *    [min_recovery_rate <kB/sec/disk>]	Throttle RAID initialization
 *    [max_recovery_rate <kB/sec/disk>]	Throttle RAID initialization
 *    [write_mostly <idx>]		Indicate a write mostly drive via index
 *    [max_write_behind <sectors>]	See '-write-behind=' (man mdadm)
 *    [stripe_cache <sectors>]		Stripe cache size for higher RAIDs
 *    [region_size <sectors>]		Defines granularity of bitmap
 *    [journal_dev <dev>]		raid4/5/6 journaling deviice
 *    					(i.e. write hole closing log)
 *
 * RAID10-only options:
 *    [raid10_copies <# copies>]	Number of copies.  (Default: 2)
 *    [raid10_format <near|far|offset>] Layout algorithm.  (Default: near)
 */
static int parse_raid_params(struct raid_set *rs, struct dm_arg_set *as,
			     unsigned int num_raid_params)
{
	int value, raid10_format = ALGORITHM_RAID10_DEFAULT;
	unsigned int raid10_copies = 2;
	unsigned int i, write_mostly = 0;
	unsigned int region_size = 0;
	sector_t max_io_len;
	const char *arg, *key;
	struct raid_dev *rd;
	struct raid_type *rt = rs->raid_type;

	arg = dm_shift_arg(as);
	num_raid_params--; /* Account for chunk_size argument */

	if (kstrtoint(arg, 10, &value) < 0) {
		rs->ti->error = "Bad numerical argument given for chunk_size";
		return -EINVAL;
	}

	/*
	 * First, parse the in-order required arguments
	 * "chunk_size" is the only argument of this type.
	 */
	if (rt_is_raid1(rt)) {
		if (value)
			DMERR("Ignoring chunk size parameter for RAID 1");
		value = 0;
	} else if (!is_power_of_2(value)) {
		rs->ti->error = "Chunk size must be a power of 2";
		return -EINVAL;
	} else if (value < 8) {
		rs->ti->error = "Chunk size value is too small";
		return -EINVAL;
	}

	rs->md.new_chunk_sectors = rs->md.chunk_sectors = value;

	/*
	 * We set each individual device as In_sync with a completed
	 * 'recovery_offset'.  If there has been a device failure or
	 * replacement then one of the following cases applies:
	 *
	 *   1) User specifies 'rebuild'.
	 *	- Device is reset when param is read.
	 *   2) A new device is supplied.
	 *	- No matching superblock found, resets device.
	 *   3) Device failure was transient and returns on reload.
	 *	- Failure noticed, resets device for bitmap replay.
	 *   4) Device hadn't completed recovery after previous failure.
	 *	- Superblock is read and overrides recovery_offset.
	 *
	 * What is found in the superblocks of the devices is always
	 * authoritative, unless 'rebuild' or '[no]sync' was specified.
	 */
	for (i = 0; i < rs->raid_disks; i++) {
		set_bit(In_sync, &rs->dev[i].rdev.flags);
		rs->dev[i].rdev.recovery_offset = MaxSector;
	}

	/*
	 * Second, parse the unordered optional arguments
	 */
	for (i = 0; i < num_raid_params; i++) {
		key = dm_shift_arg(as);
		if (!key) {
			rs->ti->error = "Not enough raid parameters given";
			return -EINVAL;
		}

		if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_NOSYNC))) {
			if (test_and_set_bit(__CTR_FLAG_NOSYNC, &rs->ctr_flags)) {
				rs->ti->error = "Only one 'nosync' argument allowed";
				return -EINVAL;
			}
			continue;
		}
		if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_SYNC))) {
			if (test_and_set_bit(__CTR_FLAG_SYNC, &rs->ctr_flags)) {
				rs->ti->error = "Only one 'sync' argument allowed";
				return -EINVAL;
			}
			continue;
		}
		if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_RAID10_USE_NEAR_SETS))) {
			if (test_and_set_bit(__CTR_FLAG_RAID10_USE_NEAR_SETS, &rs->ctr_flags)) {
				rs->ti->error = "Only one 'raid10_use_new_sets' argument allowed";
				return -EINVAL;
			}
			continue;
		}

		arg = dm_shift_arg(as);
		i++; /* Account for the argument pairs */
		if (!arg) {
			rs->ti->error = "Wrong number of raid parameters given";
			return -EINVAL;
		}

		/*
		 * Parameters that take a string value are checked here.
		 */
		/* "raid10_format {near|offset|far} */
		if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_RAID10_FORMAT))) {
			if (test_and_set_bit(__CTR_FLAG_RAID10_FORMAT, &rs->ctr_flags)) {
				rs->ti->error = "Only one 'raid10_format' argument pair allowed";
				return -EINVAL;
			}
			if (!rt_is_raid10(rt)) {
				rs->ti->error = "'raid10_format' is an invalid parameter for this RAID type";
				return -EINVAL;
			}
			raid10_format = raid10_name_to_format(arg);
			if (raid10_format < 0) {
				rs->ti->error = "Invalid 'raid10_format' value given";
				return raid10_format;
			}
			continue;
		}

		/* "journal_dev <dev>" */
		if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_JOURNAL_DEV))) {
			int r;
			struct md_rdev *jdev;

			if (test_and_set_bit(__CTR_FLAG_JOURNAL_DEV, &rs->ctr_flags)) {
				rs->ti->error = "Only one raid4/5/6 set journaling device allowed";
				return -EINVAL;
			}
			if (!rt_is_raid456(rt)) {
				rs->ti->error = "'journal_dev' is an invalid parameter for this RAID type";
				return -EINVAL;
			}
			r = dm_get_device(rs->ti, arg, dm_table_get_mode(rs->ti->table),
					  &rs->journal_dev.dev);
			if (r) {
				rs->ti->error = "raid4/5/6 journal device lookup failure";
				return r;
			}
			jdev = &rs->journal_dev.rdev;
			md_rdev_init(jdev);
			jdev->mddev = &rs->md;
			jdev->bdev = rs->journal_dev.dev->bdev;
			jdev->sectors = to_sector(i_size_read(jdev->bdev->bd_inode));
			if (jdev->sectors < MIN_RAID456_JOURNAL_SPACE) {
				rs->ti->error = "No space for raid4/5/6 journal";
				return -ENOSPC;
			}
			rs->journal_dev.mode = R5C_JOURNAL_MODE_WRITE_THROUGH;
			set_bit(Journal, &jdev->flags);
			continue;
		}

		/* "journal_mode <mode>" ("journal_dev" mandatory!) */
		if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_JOURNAL_MODE))) {
			int r;

			if (!test_bit(__CTR_FLAG_JOURNAL_DEV, &rs->ctr_flags)) {
				rs->ti->error = "raid4/5/6 'journal_mode' is invalid without 'journal_dev'";
				return -EINVAL;
			}
			if (test_and_set_bit(__CTR_FLAG_JOURNAL_MODE, &rs->ctr_flags)) {
				rs->ti->error = "Only one raid4/5/6 'journal_mode' argument allowed";
				return -EINVAL;
			}
			r = dm_raid_journal_mode_to_md(arg);
			if (r < 0) {
				rs->ti->error = "Invalid 'journal_mode' argument";
				return r;
			}
			rs->journal_dev.mode = r;
			continue;
		}

		/*
		 * Parameters with number values from here on.
		 */
		if (kstrtoint(arg, 10, &value) < 0) {
			rs->ti->error = "Bad numerical argument given in raid params";
			return -EINVAL;
		}

		if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_REBUILD))) {
			/*
			 * "rebuild" is being passed in by userspace to provide
			 * indexes of replaced devices and to set up additional
			 * devices on raid level takeover.
			 */
			if (!__within_range(value, 0, rs->raid_disks - 1)) {
				rs->ti->error = "Invalid rebuild index given";
				return -EINVAL;
			}

			if (test_and_set_bit(value, (void *) rs->rebuild_disks)) {
				rs->ti->error = "rebuild for this index already given";
				return -EINVAL;
			}

			rd = rs->dev + value;
			clear_bit(In_sync, &rd->rdev.flags);
			clear_bit(Faulty, &rd->rdev.flags);
			rd->rdev.recovery_offset = 0;
			set_bit(__CTR_FLAG_REBUILD, &rs->ctr_flags);
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_WRITE_MOSTLY))) {
			if (!rt_is_raid1(rt)) {
				rs->ti->error = "write_mostly option is only valid for RAID1";
				return -EINVAL;
			}

			if (!__within_range(value, 0, rs->md.raid_disks - 1)) {
				rs->ti->error = "Invalid write_mostly index given";
				return -EINVAL;
			}

			write_mostly++;
			set_bit(WriteMostly, &rs->dev[value].rdev.flags);
			set_bit(__CTR_FLAG_WRITE_MOSTLY, &rs->ctr_flags);
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_MAX_WRITE_BEHIND))) {
			if (!rt_is_raid1(rt)) {
				rs->ti->error = "max_write_behind option is only valid for RAID1";
				return -EINVAL;
			}

			if (test_and_set_bit(__CTR_FLAG_MAX_WRITE_BEHIND, &rs->ctr_flags)) {
				rs->ti->error = "Only one max_write_behind argument pair allowed";
				return -EINVAL;
			}

			/*
			 * In device-mapper, we specify things in sectors, but
			 * MD records this value in kB
			 */
			if (value < 0 || value / 2 > COUNTER_MAX) {
				rs->ti->error = "Max write-behind limit out of range";
				return -EINVAL;
			}

			rs->md.bitmap_info.max_write_behind = value / 2;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_DAEMON_SLEEP))) {
			if (test_and_set_bit(__CTR_FLAG_DAEMON_SLEEP, &rs->ctr_flags)) {
				rs->ti->error = "Only one daemon_sleep argument pair allowed";
				return -EINVAL;
			}
			if (value < 0) {
				rs->ti->error = "daemon sleep period out of range";
				return -EINVAL;
			}
			rs->md.bitmap_info.daemon_sleep = value;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_DATA_OFFSET))) {
			/* Userspace passes new data_offset after having extended the the data image LV */
			if (test_and_set_bit(__CTR_FLAG_DATA_OFFSET, &rs->ctr_flags)) {
				rs->ti->error = "Only one data_offset argument pair allowed";
				return -EINVAL;
			}
			/* Ensure sensible data offset */
			if (value < 0 ||
			    (value && (value < MIN_FREE_RESHAPE_SPACE || value % to_sector(PAGE_SIZE)))) {
				rs->ti->error = "Bogus data_offset value";
				return -EINVAL;
			}
			rs->data_offset = value;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_DELTA_DISKS))) {
			/* Define the +/-# of disks to add to/remove from the given raid set */
			if (test_and_set_bit(__CTR_FLAG_DELTA_DISKS, &rs->ctr_flags)) {
				rs->ti->error = "Only one delta_disks argument pair allowed";
				return -EINVAL;
			}
			/* Ensure MAX_RAID_DEVICES and raid type minimal_devs! */
			if (!__within_range(abs(value), 1, MAX_RAID_DEVICES - rt->minimal_devs)) {
				rs->ti->error = "Too many delta_disk requested";
				return -EINVAL;
			}

			rs->delta_disks = value;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_STRIPE_CACHE))) {
			if (test_and_set_bit(__CTR_FLAG_STRIPE_CACHE, &rs->ctr_flags)) {
				rs->ti->error = "Only one stripe_cache argument pair allowed";
				return -EINVAL;
			}

			if (!rt_is_raid456(rt)) {
				rs->ti->error = "Inappropriate argument: stripe_cache";
				return -EINVAL;
			}

			if (value < 0) {
				rs->ti->error = "Bogus stripe cache entries value";
				return -EINVAL;
			}
			rs->stripe_cache_entries = value;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_MIN_RECOVERY_RATE))) {
			if (test_and_set_bit(__CTR_FLAG_MIN_RECOVERY_RATE, &rs->ctr_flags)) {
				rs->ti->error = "Only one min_recovery_rate argument pair allowed";
				return -EINVAL;
			}

			if (value < 0) {
				rs->ti->error = "min_recovery_rate out of range";
				return -EINVAL;
			}
			rs->md.sync_speed_min = value;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_MAX_RECOVERY_RATE))) {
			if (test_and_set_bit(__CTR_FLAG_MAX_RECOVERY_RATE, &rs->ctr_flags)) {
				rs->ti->error = "Only one max_recovery_rate argument pair allowed";
				return -EINVAL;
			}

			if (value < 0) {
				rs->ti->error = "max_recovery_rate out of range";
				return -EINVAL;
			}
			rs->md.sync_speed_max = value;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_REGION_SIZE))) {
			if (test_and_set_bit(__CTR_FLAG_REGION_SIZE, &rs->ctr_flags)) {
				rs->ti->error = "Only one region_size argument pair allowed";
				return -EINVAL;
			}

			region_size = value;
			rs->requested_bitmap_chunk_sectors = value;
		} else if (!strcasecmp(key, dm_raid_arg_name_by_flag(CTR_FLAG_RAID10_COPIES))) {
			if (test_and_set_bit(__CTR_FLAG_RAID10_COPIES, &rs->ctr_flags)) {
				rs->ti->error = "Only one raid10_copies argument pair allowed";
				return -EINVAL;
			}

			if (!__within_range(value, 2, rs->md.raid_disks)) {
				rs->ti->error = "Bad value for 'raid10_copies'";
				return -EINVAL;
			}

			raid10_copies = value;
		} else {
			DMERR("Unable to parse RAID parameter: %s", key);
			rs->ti->error = "Unable to parse RAID parameter";
			return -EINVAL;
		}
	}

	if (test_bit(__CTR_FLAG_SYNC, &rs->ctr_flags) &&
	    test_bit(__CTR_FLAG_NOSYNC, &rs->ctr_flags)) {
		rs->ti->error = "sync and nosync are mutually exclusive";
		return -EINVAL;
	}

	if (test_bit(__CTR_FLAG_REBUILD, &rs->ctr_flags) &&
	    (test_bit(__CTR_FLAG_SYNC, &rs->ctr_flags) ||
	     test_bit(__CTR_FLAG_NOSYNC, &rs->ctr_flags))) {
		rs->ti->error = "sync/nosync and rebuild are mutually exclusive";
		return -EINVAL;
	}

	if (write_mostly >= rs->md.raid_disks) {
		rs->ti->error = "Can't set all raid1 devices to write_mostly";
		return -EINVAL;
	}

	if (rs->md.sync_speed_max &&
	    rs->md.sync_speed_min > rs->md.sync_speed_max) {
		rs->ti->error = "Bogus recovery rates";
		return -EINVAL;
	}

	if (validate_region_size(rs, region_size))
		return -EINVAL;

	if (rs->md.chunk_sectors)
		max_io_len = rs->md.chunk_sectors;
	else
		max_io_len = region_size;

	if (dm_set_target_max_io_len(rs->ti, max_io_len))
		return -EINVAL;

	if (rt_is_raid10(rt)) {
		if (raid10_copies > rs->md.raid_disks) {
			rs->ti->error = "Not enough devices to satisfy specification";
			return -EINVAL;
		}

		rs->md.new_layout = raid10_format_to_md_layout(rs, raid10_format, raid10_copies);
		if (rs->md.new_layout < 0) {
			rs->ti->error = "Error getting raid10 format";
			return rs->md.new_layout;
		}

		rt = get_raid_type_by_ll(10, rs->md.new_layout);
		if (!rt) {
			rs->ti->error = "Failed to recognize new raid10 layout";
			return -EINVAL;
		}

		if ((rt->algorithm == ALGORITHM_RAID10_DEFAULT ||
		     rt->algorithm == ALGORITHM_RAID10_NEAR) &&
		    test_bit(__CTR_FLAG_RAID10_USE_NEAR_SETS, &rs->ctr_flags)) {
			rs->ti->error = "RAID10 format 'near' and 'raid10_use_near_sets' are incompatible";
			return -EINVAL;
		}
	}

	rs->raid10_copies = raid10_copies;

	/* Assume there are no metadata devices until the drives are parsed */
	rs->md.persistent = 0;
	rs->md.external = 1;

	/* Check, if any invalid ctr arguments have been passed in for the raid level */
	return rs_check_for_valid_flags(rs);
}

/* Set raid4/5/6 cache size */
static int rs_set_raid456_stripe_cache(struct raid_set *rs)
{
	int r;
	struct r5conf *conf;
	struct mddev *mddev = &rs->md;
	uint32_t min_stripes = max(mddev->chunk_sectors, mddev->new_chunk_sectors) / 2;
	uint32_t nr_stripes = rs->stripe_cache_entries;

	if (!rt_is_raid456(rs->raid_type)) {
		rs->ti->error = "Inappropriate raid level; cannot change stripe_cache size";
		return -EINVAL;
	}

	if (nr_stripes < min_stripes) {
		DMINFO("Adjusting requested %u stripe cache entries to %u to suit stripe size",
		       nr_stripes, min_stripes);
		nr_stripes = min_stripes;
	}

	conf = mddev->private;
	if (!conf) {
		rs->ti->error = "Cannot change stripe_cache size on inactive RAID set";
		return -EINVAL;
	}

	/* Try setting number of stripes in raid456 stripe cache */
	if (conf->min_nr_stripes != nr_stripes) {
		r = raid5_set_cache_size(mddev, nr_stripes);
		if (r) {
			rs->ti->error = "Failed to set raid4/5/6 stripe cache size";
			return r;
		}

		DMINFO("%u stripe cache entries", nr_stripes);
	}

	return 0;
}

/* Return # of data stripes as kept in mddev as of @rs (i.e. as of superblock) */
static unsigned int mddev_data_stripes(struct raid_set *rs)
{
	return rs->md.raid_disks - rs->raid_type->parity_devs;
}

/* Return # of data stripes of @rs (i.e. as of ctr) */
static unsigned int rs_data_stripes(struct raid_set *rs)
{
	return rs->raid_disks - rs->raid_type->parity_devs;
}

/*
 * Retrieve rdev->sectors from any valid raid device of @rs
 * to allow userpace to pass in arbitray "- -" device tupples.
 */
static sector_t __rdev_sectors(struct raid_set *rs)
{
	int i;

	for (i = 0; i < rs->raid_disks; i++) {
		struct md_rdev *rdev = &rs->dev[i].rdev;

		if (!test_bit(Journal, &rdev->flags) &&
		    rdev->bdev && rdev->sectors)
			return rdev->sectors;
	}

	return 0;
}

/* Check that calculated dev_sectors fits all component devices. */
static int _check_data_dev_sectors(struct raid_set *rs)
{
	sector_t ds = ~0;
	struct md_rdev *rdev;

	rdev_for_each(rdev, &rs->md)
		if (!test_bit(Journal, &rdev->flags) && rdev->bdev) {
			ds = min(ds, to_sector(i_size_read(rdev->bdev->bd_inode)));
			if (ds < rs->md.dev_sectors) {
				rs->ti->error = "Component device(s) too small";
				return -EINVAL;
			}
		}

	return 0;
}

/* Calculate the sectors per device and per array used for @rs */
static int rs_set_dev_and_array_sectors(struct raid_set *rs, sector_t sectors, bool use_mddev)
{
	int delta_disks;
	unsigned int data_stripes;
	sector_t array_sectors = sectors, dev_sectors = sectors;
	struct mddev *mddev = &rs->md;

	if (use_mddev) {
		delta_disks = mddev->delta_disks;
		data_stripes = mddev_data_stripes(rs);
	} else {
		delta_disks = rs->delta_disks;
		data_stripes = rs_data_stripes(rs);
	}

	/* Special raid1 case w/o delta_disks support (yet) */
	if (rt_is_raid1(rs->raid_type))
		;
	else if (rt_is_raid10(rs->raid_type)) {
		if (rs->raid10_copies < 2 ||
		    delta_disks < 0) {
			rs->ti->error = "Bogus raid10 data copies or delta disks";
			return -EINVAL;
		}

		dev_sectors *= rs->raid10_copies;
		if (sector_div(dev_sectors, data_stripes))
			goto bad;

		array_sectors = (data_stripes + delta_disks) * dev_sectors;
		if (sector_div(array_sectors, rs->raid10_copies))
			goto bad;

	} else if (sector_div(dev_sectors, data_stripes))
		goto bad;

	else
		/* Striped layouts */
		array_sectors = (data_stripes + delta_disks) * dev_sectors;

	mddev->array_sectors = array_sectors;
	mddev->dev_sectors = dev_sectors;
	rs_set_rdev_sectors(rs);

	return _check_data_dev_sectors(rs);
bad:
	rs->ti->error = "Target length not divisible by number of data devices";
	return -EINVAL;
}

/* Setup recovery on @rs */
static void rs_setup_recovery(struct raid_set *rs, sector_t dev_sectors)
{
	/* raid0 does not recover */
	if (rs_is_raid0(rs))
		rs->md.recovery_cp = MaxSector;
	/*
	 * A raid6 set has to be recovered either
	 * completely or for the grown part to
	 * ensure proper parity and Q-Syndrome
	 */
	else if (rs_is_raid6(rs))
		rs->md.recovery_cp = dev_sectors;
	/*
	 * Other raid set types may skip recovery
	 * depending on the 'nosync' flag.
	 */
	else
		rs->md.recovery_cp = test_bit(__CTR_FLAG_NOSYNC, &rs->ctr_flags)
				     ? MaxSector : dev_sectors;
}

static void do_table_event(struct work_struct *ws)
{
	struct raid_set *rs = container_of(ws, struct raid_set, md.event_work);

	smp_rmb(); /* Make sure we access most actual mddev properties */
	if (!rs_is_reshaping(rs)) {
		if (rs_is_raid10(rs))
			rs_set_rdev_sectors(rs);
		rs_set_capacity(rs);
	}
	dm_table_event(rs->ti->table);
}

/*
 * Make sure a valid takover (level switch) is being requested on @rs
 *
 * Conversions of raid sets from one MD personality to another
 * have to conform to restrictions which are enforced here.
 */
static int rs_check_takeover(struct raid_set *rs)
{
	struct mddev *mddev = &rs->md;
	unsigned int near_copies;

	if (rs->md.degraded) {
		rs->ti->error = "Can't takeover degraded raid set";
		return -EPERM;
	}

	if (rs_is_reshaping(rs)) {
		rs->ti->error = "Can't takeover reshaping raid set";
		return -EPERM;
	}

	switch (mddev->level) {
	case 0:
		/* raid0 -> raid1/5 with one disk */
		if ((mddev->new_level == 1 || mddev->new_level == 5) &&
		    mddev->raid_disks == 1)
			return 0;

		/* raid0 -> raid10 */
		if (mddev->new_level == 10 &&
		    !(rs->raid_disks % mddev->raid_disks))
			return 0;

		/* raid0 with multiple disks -> raid4/5/6 */
		if (__within_range(mddev->new_level, 4, 6) &&
		    mddev->new_layout == ALGORITHM_PARITY_N &&
		    mddev->raid_disks > 1)
			return 0;

		break;

	case 10:
		/* Can't takeover raid10_offset! */
		if (__is_raid10_offset(mddev->layout))
			break;

		near_copies = __raid10_near_copies(mddev->layout);

		/* raid10* -> raid0 */
		if (mddev->new_level == 0) {
			/* Can takeover raid10_near with raid disks divisable by data copies! */
			if (near_copies > 1 &&
			    !(mddev->raid_disks % near_copies)) {
				mddev->raid_disks /= near_copies;
				mddev->delta_disks = mddev->raid_disks;
				return 0;
			}

			/* Can takeover raid10_far */
			if (near_copies == 1 &&
			    __raid10_far_copies(mddev->layout) > 1)
				return 0;

			break;
		}

		/* raid10_{near,far} -> raid1 */
		if (mddev->new_level == 1 &&
		    max(near_copies, __raid10_far_copies(mddev->layout)) == mddev->raid_disks)
			return 0;

		/* raid10_{near,far} with 2 disks -> raid4/5 */
		if (__within_range(mddev->new_level, 4, 5) &&
		    mddev->raid_disks == 2)
			return 0;
		break;

	case 1:
		/* raid1 with 2 disks -> raid4/5 */
		if (__within_range(mddev->new_level, 4, 5) &&
		    mddev->raid_disks == 2) {
			mddev->degraded = 1;
			return 0;
		}

		/* raid1 -> raid0 */
		if (mddev->new_level == 0 &&
		    mddev->raid_disks == 1)
			return 0;

		/* raid1 -> raid10 */
		if (mddev->new_level == 10)
			return 0;
		break;

	case 4:
		/* raid4 -> raid0 */
		if (mddev->new_level == 0)
			return 0;

		/* raid4 -> raid1/5 with 2 disks */
		if ((mddev->new_level == 1 || mddev->new_level == 5) &&
		    mddev->raid_disks == 2)
			return 0;

		/* raid4 -> raid5/6 with parity N */
		if (__within_range(mddev->new_level, 5, 6) &&
		    mddev->layout == ALGORITHM_PARITY_N)
			return 0;
		break;

	case 5:
		/* raid5 with parity N -> raid0 */
		if (mddev->new_level == 0 &&
		    mddev->layout == ALGORITHM_PARITY_N)
			return 0;

		/* raid5 with parity N -> raid4 */
		if (mddev->new_level == 4 &&
		    mddev->layout == ALGORITHM_PARITY_N)
			return 0;

		/* raid5 with 2 disks -> raid1/4/10 */
		if ((mddev->new_level == 1 || mddev->new_level == 4 || mddev->new_level == 10) &&
		    mddev->raid_disks == 2)
			return 0;

		/* raid5_* ->  raid6_*_6 with Q-Syndrome N (e.g. raid5_ra -> raid6_ra_6 */
		if (mddev->new_level == 6 &&
		    ((mddev->layout == ALGORITHM_PARITY_N && mddev->new_layout == ALGORITHM_PARITY_N) ||
		      __within_range(mddev->new_layout, ALGORITHM_LEFT_ASYMMETRIC_6, ALGORITHM_RIGHT_SYMMETRIC_6)))
			return 0;
		break;

	case 6:
		/* raid6 with parity N -> raid0 */
		if (mddev->new_level == 0 &&
		    mddev->layout == ALGORITHM_PARITY_N)
			return 0;

		/* raid6 with parity N -> raid4 */
		if (mddev->new_level == 4 &&
		    mddev->layout == ALGORITHM_PARITY_N)
			return 0;

		/* raid6_*_n with Q-Syndrome N -> raid5_* */
		if (mddev->new_level == 5 &&
		    ((mddev->layout == ALGORITHM_PARITY_N && mddev->new_layout == ALGORITHM_PARITY_N) ||
		     __within_range(mddev->new_layout, ALGORITHM_LEFT_ASYMMETRIC, ALGORITHM_RIGHT_SYMMETRIC)))
			return 0;
		break;

	default:
		break;
	}

	rs->ti->error = "takeover not possible";
	return -EINVAL;
}

/* True if @rs requested to be taken over */
static bool rs_takeover_requested(struct raid_set *rs)
{
	return rs->md.new_level != rs->md.level;
}

/* True if layout is set to reshape. */
static bool rs_is_layout_change(struct raid_set *rs, bool use_mddev)
{
	return (use_mddev ? rs->md.delta_disks : rs->delta_disks) ||
	       rs->md.new_layout != rs->md.layout ||
	       rs->md.new_chunk_sectors != rs->md.chunk_sectors;
}

/* True if @rs is requested to reshape by ctr */
static bool rs_reshape_requested(struct raid_set *rs)
{
	bool change;
	struct mddev *mddev = &rs->md;

	if (rs_takeover_requested(rs))
		return false;

	if (rs_is_raid0(rs))
		return false;

	change = rs_is_layout_change(rs, false);

	/* Historical case to support raid1 reshape without delta disks */
	if (rs_is_raid1(rs)) {
		if (rs->delta_disks)
			return !!rs->delta_disks;

		return !change &&
		       mddev->raid_disks != rs->raid_disks;
	}

	if (rs_is_raid10(rs))
		return change &&
		       !__is_raid10_far(mddev->new_layout) &&
		       rs->delta_disks >= 0;

	return change;
}

/*  Features */
#define	FEATURE_FLAG_SUPPORTS_V190	0x1 /* Supports extended superblock */

/* State flags for sb->flags */
#define	SB_FLAG_RESHAPE_ACTIVE		0x1
#define	SB_FLAG_RESHAPE_BACKWARDS	0x2

/*
 * This structure is never routinely used by userspace, unlike md superblocks.
 * Devices with this superblock should only ever be accessed via device-mapper.
 */
#define DM_RAID_MAGIC 0x64526D44
struct dm_raid_superblock {
	__le32 magic;		/* "DmRd" */
	__le32 compat_features;	/* Used to indicate compatible features (like 1.9.0 ondisk metadata extension) */

	__le32 num_devices;	/* Number of devices in this raid set. (Max 64) */
	__le32 array_position;	/* The position of this drive in the raid set */

	__le64 events;		/* Incremented by md when superblock updated */
	__le64 failed_devices;	/* Pre 1.9.0 part of bit field of devices to */
				/* indicate failures (see extension below) */

	/*
	 * This offset tracks the progress of the repair or replacement of
	 * an individual drive.
	 */
	__le64 disk_recovery_offset;

	/*
	 * This offset tracks the progress of the initial raid set
	 * synchronisation/parity calculation.
	 */
	__le64 array_resync_offset;

	/*
	 * raid characteristics
	 */
	__le32 level;
	__le32 layout;
	__le32 stripe_sectors;

	/********************************************************************
	 * BELOW FOLLOW V1.9.0 EXTENSIONS TO THE PRISTINE SUPERBLOCK FORMAT!!!
	 *
	 * FEATURE_FLAG_SUPPORTS_V190 in the compat_features member indicates that those exist
	 */

	__le32 flags; /* Flags defining array states for reshaping */

	/*
	 * This offset tracks the progress of a raid
	 * set reshape in order to be able to restart it
	 */
	__le64 reshape_position;

	/*
	 * These define the properties of the array in case of an interrupted reshape
	 */
	__le32 new_level;
	__le32 new_layout;
	__le32 new_stripe_sectors;
	__le32 delta_disks;

	__le64 array_sectors; /* Array size in sectors */

	/*
	 * Sector offsets to data on devices (reshaping).
	 * Needed to support out of place reshaping, thus
	 * not writing over any stripes whilst converting
	 * them from old to new layout
	 */
	__le64 data_offset;
	__le64 new_data_offset;

	__le64 sectors; /* Used device size in sectors */

	/*
	 * Additonal Bit field of devices indicating failures to support
	 * up to 256 devices with the 1.9.0 on-disk metadata format
	 */
	__le64 extended_failed_devices[DISKS_ARRAY_ELEMS - 1];

	__le32 incompat_features;	/* Used to indicate any incompatible features */

	/* Always set rest up to logical block size to 0 when writing (see get_metadata_device() below). */
} __packed;

/*
 * Check for reshape constraints on raid set @rs:
 *
 * - reshape function non-existent
 * - degraded set
 * - ongoing recovery
 * - ongoing reshape
 *
 * Returns 0 if none or -EPERM if given constraint
 * and error message reference in @errmsg
 */
static int rs_check_reshape(struct raid_set *rs)
{
	struct mddev *mddev = &rs->md;

	if (!mddev->pers || !mddev->pers->check_reshape)
		rs->ti->error = "Reshape not supported";
	else if (mddev->degraded)
		rs->ti->error = "Can't reshape degraded raid set";
	else if (rs_is_recovering(rs))
		rs->ti->error = "Convert request on recovering raid set prohibited";
	else if (rs_is_reshaping(rs))
		rs->ti->error = "raid set already reshaping!";
	else if (!(rs_is_raid1(rs) || rs_is_raid10(rs) || rs_is_raid456(rs)))
		rs->ti->error = "Reshaping only supported for raid1/4/5/6/10";
	else
		return 0;

	return -EPERM;
}

static int read_disk_sb(struct md_rdev *rdev, int size, bool force_reload)
{
	BUG_ON(!rdev->sb_page);

	if (rdev->sb_loaded && !force_reload)
		return 0;

	rdev->sb_loaded = 0;

	if (!sync_page_io(rdev, 0, size, rdev->sb_page, REQ_OP_READ, 0, true)) {
		DMERR("Failed to read superblock of device at position %d",
		      rdev->raid_disk);
		md_error(rdev->mddev, rdev);
		set_bit(Faulty, &rdev->flags);
		return -EIO;
	}

	rdev->sb_loaded = 1;

	return 0;
}

static void sb_retrieve_failed_devices(struct dm_raid_superblock *sb, uint64_t *failed_devices)
{
	failed_devices[0] = le64_to_cpu(sb->failed_devices);
	memset(failed_devices + 1, 0, sizeof(sb->extended_failed_devices));

	if (le32_to_cpu(sb->compat_features) & FEATURE_FLAG_SUPPORTS_V190) {
		int i = ARRAY_SIZE(sb->extended_failed_devices);

		while (i--)
			failed_devices[i+1] = le64_to_cpu(sb->extended_failed_devices[i]);
	}
}

static void sb_update_failed_devices(struct dm_raid_superblock *sb, uint64_t *failed_devices)
{
	int i = ARRAY_SIZE(sb->extended_failed_devices);

	sb->failed_devices = cpu_to_le64(failed_devices[0]);
	while (i--)
		sb->extended_failed_devices[i] = cpu_to_le64(failed_devices[i+1]);
}

/*
 * Synchronize the superblock members with the raid set properties
 *
 * All superblock data is little endian.
 */
static void super_sync(struct mddev *mddev, struct md_rdev *rdev)
{
	bool update_failed_devices = false;
	unsigned int i;
	uint64_t failed_devices[DISKS_ARRAY_ELEMS];
	struct dm_raid_superblock *sb;
	struct raid_set *rs = container_of(mddev, struct raid_set, md);

	/* No metadata device, no superblock */
	if (!rdev->meta_bdev)
		return;

	BUG_ON(!rdev->sb_page);

	sb = page_address(rdev->sb_page);

	sb_retrieve_failed_devices(sb, failed_devices);

	for (i = 0; i < rs->raid_disks; i++)
		if (!rs->dev[i].data_dev || test_bit(Faulty, &rs->dev[i].rdev.flags)) {
			update_failed_devices = true;
			set_bit(i, (void *) failed_devices);
		}

	if (update_failed_devices)
		sb_update_failed_devices(sb, failed_devices);

	sb->magic = cpu_to_le32(DM_RAID_MAGIC);
	sb->compat_features = cpu_to_le32(FEATURE_FLAG_SUPPORTS_V190);

	sb->num_devices = cpu_to_le32(mddev->raid_disks);
	sb->array_position = cpu_to_le32(rdev->raid_disk);

	sb->events = cpu_to_le64(mddev->events);

	sb->disk_recovery_offset = cpu_to_le64(rdev->recovery_offset);
	sb->array_resync_offset = cpu_to_le64(mddev->recovery_cp);

	sb->level = cpu_to_le32(mddev->level);
	sb->layout = cpu_to_le32(mddev->layout);
	sb->stripe_sectors = cpu_to_le32(mddev->chunk_sectors);

	/********************************************************************
	 * BELOW FOLLOW V1.9.0 EXTENSIONS TO THE PRISTINE SUPERBLOCK FORMAT!!!
	 *
	 * FEATURE_FLAG_SUPPORTS_V190 in the compat_features member indicates that those exist
	 */
	sb->new_level = cpu_to_le32(mddev->new_level);
	sb->new_layout = cpu_to_le32(mddev->new_layout);
	sb->new_stripe_sectors = cpu_to_le32(mddev->new_chunk_sectors);

	sb->delta_disks = cpu_to_le32(mddev->delta_disks);

	smp_rmb(); /* Make sure we access most recent reshape position */
	sb->reshape_position = cpu_to_le64(mddev->reshape_position);
	if (le64_to_cpu(sb->reshape_position) != MaxSector) {
		/* Flag ongoing reshape */
		sb->flags |= cpu_to_le32(SB_FLAG_RESHAPE_ACTIVE);

		if (mddev->delta_disks < 0 || mddev->reshape_backwards)
			sb->flags |= cpu_to_le32(SB_FLAG_RESHAPE_BACKWARDS);
	} else {
		/* Clear reshape flags */
		sb->flags &= ~(cpu_to_le32(SB_FLAG_RESHAPE_ACTIVE|SB_FLAG_RESHAPE_BACKWARDS));
	}

	sb->array_sectors = cpu_to_le64(mddev->array_sectors);
	sb->data_offset = cpu_to_le64(rdev->data_offset);
	sb->new_data_offset = cpu_to_le64(rdev->new_data_offset);
	sb->sectors = cpu_to_le64(rdev->sectors);
	sb->incompat_features = cpu_to_le32(0);

	/* Zero out the rest of the payload after the size of the superblock */
	memset(sb + 1, 0, rdev->sb_size - sizeof(*sb));
}

/*
 * super_load
 *
 * This function creates a superblock if one is not found on the device
 * and will decide which superblock to use if there's a choice.
 *
 * Return: 1 if use rdev, 0 if use refdev, -Exxx otherwise
 */
static int super_load(struct md_rdev *rdev, struct md_rdev *refdev)
{
	int r;
	struct dm_raid_superblock *sb;
	struct dm_raid_superblock *refsb;
	uint64_t events_sb, events_refsb;

	r = read_disk_sb(rdev, rdev->sb_size, false);
	if (r)
		return r;

	sb = page_address(rdev->sb_page);

	/*
	 * Two cases that we want to write new superblocks and rebuild:
	 * 1) New device (no matching magic number)
	 * 2) Device specified for rebuild (!In_sync w/ offset == 0)
	 */
	if ((sb->magic != cpu_to_le32(DM_RAID_MAGIC)) ||
	    (!test_bit(In_sync, &rdev->flags) && !rdev->recovery_offset)) {
		super_sync(rdev->mddev, rdev);

		set_bit(FirstUse, &rdev->flags);
		sb->compat_features = cpu_to_le32(FEATURE_FLAG_SUPPORTS_V190);

		/* Force writing of superblocks to disk */
		set_bit(MD_SB_CHANGE_DEVS, &rdev->mddev->sb_flags);

		/* Any superblock is better than none, choose that if given */
		return refdev ? 0 : 1;
	}

	if (!refdev)
		return 1;

	events_sb = le64_to_cpu(sb->events);

	refsb = page_address(refdev->sb_page);
	events_refsb = le64_to_cpu(refsb->events);

	return (events_sb > events_refsb) ? 1 : 0;
}

static int super_init_validation(struct raid_set *rs, struct md_rdev *rdev)
{
	int role;
	unsigned int d;
	struct mddev *mddev = &rs->md;
	uint64_t events_sb;
	uint64_t failed_devices[DISKS_ARRAY_ELEMS];
	struct dm_raid_superblock *sb;
	uint32_t new_devs = 0, rebuild_and_new = 0, rebuilds = 0;
	struct md_rdev *r;
	struct dm_raid_superblock *sb2;

	sb = page_address(rdev->sb_page);
	events_sb = le64_to_cpu(sb->events);

	/*
	 * Initialise to 1 if this is a new superblock.
	 */
	mddev->events = events_sb ? : 1;

	mddev->reshape_position = MaxSector;

	mddev->raid_disks = le32_to_cpu(sb->num_devices);
	mddev->level = le32_to_cpu(sb->level);
	mddev->layout = le32_to_cpu(sb->layout);
	mddev->chunk_sectors = le32_to_cpu(sb->stripe_sectors);

	/*
	 * Reshaping is supported, e.g. reshape_position is valid
	 * in superblock and superblock content is authoritative.
	 */
	if (le32_to_cpu(sb->compat_features) & FEATURE_FLAG_SUPPORTS_V190) {
		/* Superblock is authoritative wrt given raid set layout! */
		mddev->new_level = le32_to_cpu(sb->new_level);
		mddev->new_layout = le32_to_cpu(sb->new_layout);
		mddev->new_chunk_sectors = le32_to_cpu(sb->new_stripe_sectors);
		mddev->delta_disks = le32_to_cpu(sb->delta_disks);
		mddev->array_sectors = le64_to_cpu(sb->array_sectors);

		/* raid was reshaping and got interrupted */
		if (le32_to_cpu(sb->flags) & SB_FLAG_RESHAPE_ACTIVE) {
			if (test_bit(__CTR_FLAG_DELTA_DISKS, &rs->ctr_flags)) {
				DMERR("Reshape requested but raid set is still reshaping");
				return -EINVAL;
			}

			if (mddev->delta_disks < 0 ||
			    (!mddev->delta_disks && (le32_to_cpu(sb->flags) & SB_FLAG_RESHAPE_BACKWARDS)))
				mddev->reshape_backwards = 1;
			else
				mddev->reshape_backwards = 0;

			mddev->reshape_position = le64_to_cpu(sb->reshape_position);
			rs->raid_type = get_raid_type_by_ll(mddev->level, mddev->layout);
		}

	} else {
		/*
		 * No takeover/reshaping, because we don't have the extended v1.9.0 metadata
		 */
		struct raid_type *rt_cur = get_raid_type_by_ll(mddev->level, mddev->layout);
		struct raid_type *rt_new = get_raid_type_by_ll(mddev->new_level, mddev->new_layout);

		if (rs_takeover_requested(rs)) {
			if (rt_cur && rt_new)
				DMERR("Takeover raid sets from %s to %s not yet supported by metadata. (raid level change)",
				      rt_cur->name, rt_new->name);
			else
				DMERR("Takeover raid sets not yet supported by metadata. (raid level change)");
			return -EINVAL;
		} else if (rs_reshape_requested(rs)) {
			DMERR("Reshaping raid sets not yet supported by metadata. (raid layout change keeping level)");
			if (mddev->layout != mddev->new_layout) {
				if (rt_cur && rt_new)
					DMERR("	 current layout %s vs new layout %s",
					      rt_cur->name, rt_new->name);
				else
					DMERR("	 current layout 0x%X vs new layout 0x%X",
					      le32_to_cpu(sb->layout), mddev->new_layout);
			}
			if (mddev->chunk_sectors != mddev->new_chunk_sectors)
				DMERR("	 current stripe sectors %u vs new stripe sectors %u",
				      mddev->chunk_sectors, mddev->new_chunk_sectors);
			if (rs->delta_disks)
				DMERR("	 current %u disks vs new %u disks",
				      mddev->raid_disks, mddev->raid_disks + rs->delta_disks);
			if (rs_is_raid10(rs)) {
				DMERR("	 Old layout: %s w/ %u copies",
				      raid10_md_layout_to_format(mddev->layout),
				      raid10_md_layout_to_copies(mddev->layout));
				DMERR("	 New layout: %s w/ %u copies",
				      raid10_md_layout_to_format(mddev->new_layout),
				      raid10_md_layout_to_copies(mddev->new_layout));
			}
			return -EINVAL;
		}

		DMINFO("Discovered old metadata format; upgrading to extended metadata format");
	}

	if (!test_bit(__CTR_FLAG_NOSYNC, &rs->ctr_flags))
		mddev->recovery_cp = le64_to_cpu(sb->array_resync_offset);

	/*
	 * During load, we set FirstUse if a new superblock was written.
	 * There are two reasons we might not have a superblock:
	 * 1) The raid set is brand new - in which case, all of the
	 *    devices must have their In_sync bit set.	Also,
	 *    recovery_cp must be 0, unless forced.
	 * 2) This is a new device being added to an old raid set
	 *    and the new device needs to be rebuilt - in which
	 *    case the In_sync bit will /not/ be set and
	 *    recovery_cp must be MaxSector.
	 * 3) This is/are a new device(s) being added to an old
	 *    raid set during takeover to a higher raid level
	 *    to provide capacity for redundancy or during reshape
	 *    to add capacity to grow the raid set.
	 */
	d = 0;
	rdev_for_each(r, mddev) {
		if (test_bit(Journal, &rdev->flags))
			continue;

		if (test_bit(FirstUse, &r->flags))
			new_devs++;

		if (!test_bit(In_sync, &r->flags)) {
			DMINFO("Device %d specified for rebuild; clearing superblock",
				r->raid_disk);
			rebuilds++;

			if (test_bit(FirstUse, &r->flags))
				rebuild_and_new++;
		}

		d++;
	}

	if (new_devs == rs->raid_disks || !rebuilds) {
		/* Replace a broken device */
		if (new_devs == rs->raid_disks) {
			DMINFO("Superblocks created for new raid set");
			set_bit(MD_ARRAY_FIRST_USE, &mddev->flags);
		} else if (new_devs != rebuilds &&
			   new_devs != rs->delta_disks) {
			DMERR("New device injected into existing raid set without "
			      "'delta_disks' or 'rebuild' parameter specified");
			return -EINVAL;
		}
	} else if (new_devs && new_devs != rebuilds) {
		DMERR("%u 'rebuild' devices cannot be injected into"
		      " a raid set with %u other first-time devices",
		      rebuilds, new_devs);
		return -EINVAL;
	} else if (rebuilds) {
		if (rebuild_and_new && rebuilds != rebuild_and_new) {
			DMERR("new device%s provided without 'rebuild'",
			      new_devs > 1 ? "s" : "");
			return -EINVAL;
		} else if (!test_bit(__CTR_FLAG_REBUILD, &rs->ctr_flags) && rs_is_recovering(rs)) {
			DMERR("'rebuild' specified while raid set is not in-sync (recovery_cp=%llu)",
			      (unsigned long long) mddev->recovery_cp);
			return -EINVAL;
		} else if (rs_is_reshaping(rs)) {
			DMERR("'rebuild' specified while raid set is being reshaped (reshape_position=%llu)",
			      (unsigned long long) mddev->reshape_position);
			return -EINVAL;
		}
	}

	/*
	 * Now we set the Faulty bit for those devices that are
	 * recorded in the superblock as failed.
	 */
	sb_retrieve_failed_devices(sb, failed_devices);
	rdev_for_each(r, mddev) {
		if (test_bit(Journal, &rdev->flags) ||
		    !r->sb_page)
			continue;
		sb2 = page_address(r->sb_page);
		sb2->failed_devices = 0;
		memset(sb2->extended_failed_devices, 0, sizeof(sb2->extended_failed_devices));

		/*
		 * Check for any device re-ordering.
		 */
		if (!test_bit(FirstUse, &r->flags) && (r->raid_disk >= 0)) {
			role = le32_to_cpu(sb2->array_position);
			if (role < 0)
				continue;

			if (role != r->raid_disk) {
				if (rs_is_raid10(rs) && __is_raid10_near(mddev->layout)) {
					if (mddev->raid_disks % __raid10_near_copies(mddev->layout) ||
					    rs->raid_disks % rs->raid10_copies) {
						rs->ti->error =
							"Cannot change raid10 near set to odd # of devices!";
						return -EINVAL;
					}

					sb2->array_position = cpu_to_le32(r->raid_disk);

				} else if (!(rs_is_raid10(rs) && rt_is_raid0(rs->raid_type)) &&
					   !(rs_is_raid0(rs) && rt_is_raid10(rs->raid_type)) &&
					   !rt_is_raid1(rs->raid_type)) {
					rs->ti->error = "Cannot change device positions in raid set";
					return -EINVAL;
				}

				DMINFO("raid device #%d now at position #%d", role, r->raid_disk);
			}

			/*
			 * Partial recovery is performed on
			 * returning failed devices.
			 */
			if (test_bit(role, (void *) failed_devices))
				set_bit(Faulty, &r->flags);
		}
	}

	return 0;
}

static int super_validate(struct raid_set *rs, struct md_rdev *rdev)
{
	struct mddev *mddev = &rs->md;
	struct dm_raid_superblock *sb;

	if (rs_is_raid0(rs) || !rdev->sb_page || rdev->raid_disk < 0)
		return 0;

	sb = page_address(rdev->sb_page);

	/*
	 * If mddev->events is not set, we know we have not yet initialized
	 * the array.
	 */
	if (!mddev->events && super_init_validation(rs, rdev))
		return -EINVAL;

	if (le32_to_cpu(sb->compat_features) &&
	    le32_to_cpu(sb->compat_features) != FEATURE_FLAG_SUPPORTS_V190) {
		rs->ti->error = "Unable to assemble array: Unknown flag(s) in compatible feature flags";
		return -EINVAL;
	}

	if (sb->incompat_features) {
		rs->ti->error = "Unable to assemble array: No incompatible feature flags supported yet";
		return -EINVAL;
	}

	/* Enable bitmap creation on @rs unless no metadevs or raid0 or journaled raid4/5/6 set. */
	mddev->bitmap_info.offset = (rt_is_raid0(rs->raid_type) || rs->journal_dev.dev) ? 0 : to_sector(4096);
	mddev->bitmap_info.default_offset = mddev->bitmap_info.offset;

	if (!test_and_clear_bit(FirstUse, &rdev->flags)) {
		/*
		 * Retrieve rdev size stored in superblock to be prepared for shrink.
		 * Check extended superblock members are present otherwise the size
		 * will not be set!
		 */
		if (le32_to_cpu(sb->compat_features) & FEATURE_FLAG_SUPPORTS_V190)
			rdev->sectors = le64_to_cpu(sb->sectors);

		rdev->recovery_offset = le64_to_cpu(sb->disk_recovery_offset);
		if (rdev->recovery_offset == MaxSector)
			set_bit(In_sync, &rdev->flags);
		/*
		 * If no reshape in progress -> we're recovering single
		 * disk(s) and have to set the device(s) to out-of-sync
		 */
		else if (!rs_is_reshaping(rs))
			clear_bit(In_sync, &rdev->flags); /* Mandatory for recovery */
	}

	/*
	 * If a device comes back, set it as not In_sync and no longer faulty.
	 */
	if (test_and_clear_bit(Faulty, &rdev->flags)) {
		rdev->recovery_offset = 0;
		clear_bit(In_sync, &rdev->flags);
		rdev->saved_raid_disk = rdev->raid_disk;
	}

	/* Reshape support -> restore repective data offsets */
	rdev->data_offset = le64_to_cpu(sb->data_offset);
	rdev->new_data_offset = le64_to_cpu(sb->new_data_offset);

	return 0;
}

/*
 * Analyse superblocks and select the freshest.
 */
static int analyse_superblocks(struct dm_target *ti, struct raid_set *rs)
{
	int r;
	struct md_rdev *rdev, *freshest;
	struct mddev *mddev = &rs->md;

	freshest = NULL;
	rdev_for_each(rdev, mddev) {
		if (test_bit(Journal, &rdev->flags))
			continue;

		if (!rdev->meta_bdev)
			continue;

		/* Set superblock offset/size for metadata device. */
		rdev->sb_start = 0;
		rdev->sb_size = bdev_logical_block_size(rdev->meta_bdev);
		if (rdev->sb_size < sizeof(struct dm_raid_superblock) || rdev->sb_size > PAGE_SIZE) {
			DMERR("superblock size of a logical block is no longer valid");
			return -EINVAL;
		}

		/*
		 * Skipping super_load due to CTR_FLAG_SYNC will cause
		 * the array to undergo initialization again as
		 * though it were new.	This is the intended effect
		 * of the "sync" directive.
		 *
		 * With reshaping capability added, we must ensure that
		 * that the "sync" directive is disallowed during the reshape.
		 */
		if (test_bit(__CTR_FLAG_SYNC, &rs->ctr_flags))
			continue;

		r = super_load(rdev, freshest);

		switch (r) {
		case 1:
			freshest = rdev;
			break;
		case 0:
			break;
		default:
			/* This is a failure to read the superblock from the metadata device. */
			/*
			 * We have to keep any raid0 data/metadata device pairs or
			 * the MD raid0 personality will fail to start the array.
			 */
			if (rs_is_raid0(rs))
				continue;

			/*
			 * We keep the dm_devs to be able to emit the device tuple
			 * properly on the table line in raid_status() (rather than
			 * mistakenly acting as if '- -' got passed into the constructor).
			 *
			 * The rdev has to stay on the same_set list to allow for
			 * the attempt to restore faulty devices on second resume.
			 */
			rdev->raid_disk = rdev->saved_raid_disk = -1;
			break;
		}
	}

	if (!freshest)
		return 0;

	/*
	 * Validation of the freshest device provides the source of
	 * validation for the remaining devices.
	 */
	rs->ti->error = "Unable to assemble array: Invalid superblocks";
	if (super_validate(rs, freshest))
		return -EINVAL;

	if (validate_raid_redundancy(rs)) {
		rs->ti->error = "Insufficient redundancy to activate array";
		return -EINVAL;
	}

	rdev_for_each(rdev, mddev)
		if (!test_bit(Journal, &rdev->flags) &&
		    rdev != freshest &&
		    super_validate(rs, rdev))
			return -EINVAL;
	return 0;
}

/*
 * Adjust data_offset and new_data_offset on all disk members of @rs
 * for out of place reshaping if requested by contructor
 *
 * We need free space at the beginning of each raid disk for forward
 * and at the end for backward reshapes which userspace has to provide
 * via remapping/reordering of space.
 */
static int rs_adjust_data_offsets(struct raid_set *rs)
{
	sector_t data_offset = 0, new_data_offset = 0;
	struct md_rdev *rdev;

	/* Constructor did not request data offset change */
	if (!test_bit(__CTR_FLAG_DATA_OFFSET, &rs->ctr_flags)) {
		if (!rs_is_reshapable(rs))
			goto out;

		return 0;
	}

	/* HM FIXME: get In_Sync raid_dev? */
	rdev = &rs->dev[0].rdev;

	if (rs->delta_disks < 0) {
		/*
		 * Removing disks (reshaping backwards):
		 *
		 * - before reshape: data is at offset 0 and free space
		 *		     is at end of each component LV
		 *
		 * - after reshape: data is at offset rs->data_offset != 0 on each component LV
		 */
		data_offset = 0;
		new_data_offset = rs->data_offset;

	} else if (rs->delta_disks > 0) {
		/*
		 * Adding disks (reshaping forwards):
		 *
		 * - before reshape: data is at offset rs->data_offset != 0 and
		 *		     free space is at begin of each component LV
		 *
		 * - after reshape: data is at offset 0 on each component LV
		 */
		data_offset = rs->data_offset;
		new_data_offset = 0;

	} else {
		/*
		 * User space passes in 0 for data offset after having removed reshape space
		 *
		 * - or - (data offset != 0)
		 *
		 * Changing RAID layout or chunk size -> toggle offsets
		 *
		 * - before reshape: data is at offset rs->data_offset 0 and
		 *		     free space is at end of each component LV
		 *		     -or-
		 *                   data is at offset rs->data_offset != 0 and
		 *		     free space is at begin of each component LV
		 *
		 * - after reshape: data is at offset 0 if it was at offset != 0
		 *                  or at offset != 0 if it was at offset 0
		 *                  on each component LV
		 *
		 */
		data_offset = rs->data_offset ? rdev->data_offset : 0;
		new_data_offset = data_offset ? 0 : rs->data_offset;
		set_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags);
	}

	/*
	 * Make sure we got a minimum amount of free sectors per device
	 */
	if (rs->data_offset &&
	    to_sector(i_size_read(rdev->bdev->bd_inode)) - rs->md.dev_sectors < MIN_FREE_RESHAPE_SPACE) {
		rs->ti->error = data_offset ? "No space for forward reshape" :
					      "No space for backward reshape";
		return -ENOSPC;
	}
out:
	/*
	 * Raise recovery_cp in case data_offset != 0 to
	 * avoid false recovery positives in the constructor.
	 */
	if (rs->md.recovery_cp < rs->md.dev_sectors)
		rs->md.recovery_cp += rs->dev[0].rdev.data_offset;

	/* Adjust data offsets on all rdevs but on any raid4/5/6 journal device */
	rdev_for_each(rdev, &rs->md) {
		if (!test_bit(Journal, &rdev->flags)) {
			rdev->data_offset = data_offset;
			rdev->new_data_offset = new_data_offset;
		}
	}

	return 0;
}

/* Userpace reordered disks -> adjust raid_disk indexes in @rs */
static void __reorder_raid_disk_indexes(struct raid_set *rs)
{
	int i = 0;
	struct md_rdev *rdev;

	rdev_for_each(rdev, &rs->md) {
		if (!test_bit(Journal, &rdev->flags)) {
			rdev->raid_disk = i++;
			rdev->saved_raid_disk = rdev->new_raid_disk = -1;
		}
	}
}

/*
 * Setup @rs for takeover by a different raid level
 */
static int rs_setup_takeover(struct raid_set *rs)
{
	struct mddev *mddev = &rs->md;
	struct md_rdev *rdev;
	unsigned int d = mddev->raid_disks = rs->raid_disks;
	sector_t new_data_offset = rs->dev[0].rdev.data_offset ? 0 : rs->data_offset;

	if (rt_is_raid10(rs->raid_type)) {
		if (rs_is_raid0(rs)) {
			/* Userpace reordered disks -> adjust raid_disk indexes */
			__reorder_raid_disk_indexes(rs);

			/* raid0 -> raid10_far layout */
			mddev->layout = raid10_format_to_md_layout(rs, ALGORITHM_RAID10_FAR,
								   rs->raid10_copies);
		} else if (rs_is_raid1(rs))
			/* raid1 -> raid10_near layout */
			mddev->layout = raid10_format_to_md_layout(rs, ALGORITHM_RAID10_NEAR,
								   rs->raid_disks);
		else
			return -EINVAL;

	}

	clear_bit(MD_ARRAY_FIRST_USE, &mddev->flags);
	mddev->recovery_cp = MaxSector;

	while (d--) {
		rdev = &rs->dev[d].rdev;

		if (test_bit(d, (void *) rs->rebuild_disks)) {
			clear_bit(In_sync, &rdev->flags);
			clear_bit(Faulty, &rdev->flags);
			mddev->recovery_cp = rdev->recovery_offset = 0;
			/* Bitmap has to be created when we do an "up" takeover */
			set_bit(MD_ARRAY_FIRST_USE, &mddev->flags);
		}

		rdev->new_data_offset = new_data_offset;
	}

	return 0;
}

/* Prepare @rs for reshape */
static int rs_prepare_reshape(struct raid_set *rs)
{
	bool reshape;
	struct mddev *mddev = &rs->md;

	if (rs_is_raid10(rs)) {
		if (rs->raid_disks != mddev->raid_disks &&
		    __is_raid10_near(mddev->layout) &&
		    rs->raid10_copies &&
		    rs->raid10_copies != __raid10_near_copies(mddev->layout)) {
			/*
			 * raid disk have to be multiple of data copies to allow this conversion,
			 *
			 * This is actually not a reshape it is a
			 * rebuild of any additional mirrors per group
			 */
			if (rs->raid_disks % rs->raid10_copies) {
				rs->ti->error = "Can't reshape raid10 mirror groups";
				return -EINVAL;
			}

			/* Userpace reordered disks to add/remove mirrors -> adjust raid_disk indexes */
			__reorder_raid_disk_indexes(rs);
			mddev->layout = raid10_format_to_md_layout(rs, ALGORITHM_RAID10_NEAR,
								   rs->raid10_copies);
			mddev->new_layout = mddev->layout;
			reshape = false;
		} else
			reshape = true;

	} else if (rs_is_raid456(rs))
		reshape = true;

	else if (rs_is_raid1(rs)) {
		if (rs->delta_disks) {
			/* Process raid1 via delta_disks */
			mddev->degraded = rs->delta_disks < 0 ? -rs->delta_disks : rs->delta_disks;
			reshape = true;
		} else {
			/* Process raid1 without delta_disks */
			mddev->raid_disks = rs->raid_disks;
			reshape = false;
		}
	} else {
		rs->ti->error = "Called with bogus raid type";
		return -EINVAL;
	}

	if (reshape) {
		set_bit(RT_FLAG_RESHAPE_RS, &rs->runtime_flags);
		set_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags);
	} else if (mddev->raid_disks < rs->raid_disks)
		/* Create new superblocks and bitmaps, if any new disks */
		set_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags);

	return 0;
}

/* Get reshape sectors from data_offsets or raid set */
static sector_t _get_reshape_sectors(struct raid_set *rs)
{
	struct md_rdev *rdev;
	sector_t reshape_sectors = 0;

	rdev_for_each(rdev, &rs->md)
		if (!test_bit(Journal, &rdev->flags)) {
			reshape_sectors = (rdev->data_offset > rdev->new_data_offset) ?
					rdev->data_offset - rdev->new_data_offset :
					rdev->new_data_offset - rdev->data_offset;
			break;
		}

	return max(reshape_sectors, (sector_t) rs->data_offset);
}

/*
 * Reshape:
 * - change raid layout
 * - change chunk size
 * - add disks
 * - remove disks
 */
static int rs_setup_reshape(struct raid_set *rs)
{
	int r = 0;
	unsigned int cur_raid_devs, d;
	sector_t reshape_sectors = _get_reshape_sectors(rs);
	struct mddev *mddev = &rs->md;
	struct md_rdev *rdev;

	mddev->delta_disks = rs->delta_disks;
	cur_raid_devs = mddev->raid_disks;

	/* Ignore impossible layout change whilst adding/removing disks */
	if (mddev->delta_disks &&
	    mddev->layout != mddev->new_layout) {
		DMINFO("Ignoring invalid layout change with delta_disks=%d", rs->delta_disks);
		mddev->new_layout = mddev->layout;
	}

	/*
	 * Adjust array size:
	 *
	 * - in case of adding disk(s), array size has
	 *   to grow after the disk adding reshape,
	 *   which'll hapen in the event handler;
	 *   reshape will happen forward, so space has to
	 *   be available at the beginning of each disk
	 *
	 * - in case of removing disk(s), array size
	 *   has to shrink before starting the reshape,
	 *   which'll happen here;
	 *   reshape will happen backward, so space has to
	 *   be available at the end of each disk
	 *
	 * - data_offset and new_data_offset are
	 *   adjusted for aforementioned out of place
	 *   reshaping based on userspace passing in
	 *   the "data_offset <sectors>" key/value
	 *   pair via the constructor
	 */

	/* Add disk(s) */
	if (rs->delta_disks > 0) {
		/* Prepare disks for check in raid4/5/6/10 {check|start}_reshape */
		for (d = cur_raid_devs; d < rs->raid_disks; d++) {
			rdev = &rs->dev[d].rdev;
			clear_bit(In_sync, &rdev->flags);

			/*
			 * save_raid_disk needs to be -1, or recovery_offset will be set to 0
			 * by md, which'll store that erroneously in the superblock on reshape
			 */
			rdev->saved_raid_disk = -1;
			rdev->raid_disk = d;

			rdev->sectors = mddev->dev_sectors;
			rdev->recovery_offset = rs_is_raid1(rs) ? 0 : MaxSector;
		}

		mddev->reshape_backwards = 0; /* adding disk(s) -> forward reshape */

	/* Remove disk(s) */
	} else if (rs->delta_disks < 0) {
		r = rs_set_dev_and_array_sectors(rs, rs->ti->len, true);
		mddev->reshape_backwards = 1; /* removing disk(s) -> backward reshape */

	/* Change layout and/or chunk size */
	} else {
		/*
		 * Reshape layout (e.g. raid5_ls -> raid5_n) and/or chunk size:
		 *
		 * keeping number of disks and do layout change ->
		 *
		 * toggle reshape_backward depending on data_offset:
		 *
		 * - free space upfront -> reshape forward
		 *
		 * - free space at the end -> reshape backward
		 *
		 *
		 * This utilizes free reshape space avoiding the need
		 * for userspace to move (parts of) LV segments in
		 * case of layout/chunksize change  (for disk
		 * adding/removing reshape space has to be at
		 * the proper address (see above with delta_disks):
		 *
		 * add disk(s)   -> begin
		 * remove disk(s)-> end
		 */
		mddev->reshape_backwards = rs->dev[0].rdev.data_offset ? 0 : 1;
	}

	/*
	 * Adjust device size for forward reshape
	 * because md_finish_reshape() reduces it.
	 */
	if (!mddev->reshape_backwards)
		rdev_for_each(rdev, &rs->md)
			if (!test_bit(Journal, &rdev->flags))
				rdev->sectors += reshape_sectors;

	return r;
}

/*
 * If the md resync thread has updated superblock with max reshape position
 * at the end of a reshape but not (yet) reset the layout configuration
 * changes -> reset the latter.
 */
static void rs_reset_inconclusive_reshape(struct raid_set *rs)
{
	if (!rs_is_reshaping(rs) && rs_is_layout_change(rs, true)) {
		rs_set_cur(rs);
		rs->md.delta_disks = 0;
		rs->md.reshape_backwards = 0;
	}
}

/*
 * Enable/disable discard support on RAID set depending on
 * RAID level and discard properties of underlying RAID members.
 */
static void configure_discard_support(struct raid_set *rs)
{
	int i;
	bool raid456;
	struct dm_target *ti = rs->ti;

	/*
	 * XXX: RAID level 4,5,6 require zeroing for safety.
	 */
	raid456 = rs_is_raid456(rs);

	for (i = 0; i < rs->raid_disks; i++) {
		struct request_queue *q;

		if (!rs->dev[i].rdev.bdev)
			continue;

		q = bdev_get_queue(rs->dev[i].rdev.bdev);
		if (!q || !blk_queue_discard(q))
			return;

		if (raid456) {
			if (!devices_handle_discard_safely) {
				DMERR("raid456 discard support disabled due to discard_zeroes_data uncertainty.");
				DMERR("Set dm-raid.devices_handle_discard_safely=Y to override.");
				return;
			}
		}
	}

	ti->num_discard_bios = 1;
}

/*
 * Construct a RAID0/1/10/4/5/6 mapping:
 * Args:
 *	<raid_type> <#raid_params> <raid_params>{0,}	\
 *	<#raid_devs> [<meta_dev1> <dev1>]{1,}
 *
 * <raid_params> varies by <raid_type>.	 See 'parse_raid_params' for
 * details on possible <raid_params>.
 *
 * Userspace is free to initialize the metadata devices, hence the superblocks to
 * enforce recreation based on the passed in table parameters.
 *
 */
static int raid_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r;
	bool resize = false;
	struct raid_type *rt;
	unsigned int num_raid_params, num_raid_devs;
	sector_t sb_array_sectors, rdev_sectors, reshape_sectors;
	struct raid_set *rs = NULL;
	const char *arg;
	struct rs_layout rs_layout;
	struct dm_arg_set as = { argc, argv }, as_nrd;
	struct dm_arg _args[] = {
		{ 0, as.argc, "Cannot understand number of raid parameters" },
		{ 1, 254, "Cannot understand number of raid devices parameters" }
	};

	arg = dm_shift_arg(&as);
	if (!arg) {
		ti->error = "No arguments";
		return -EINVAL;
	}

	rt = get_raid_type(arg);
	if (!rt) {
		ti->error = "Unrecognised raid_type";
		return -EINVAL;
	}

	/* Must have <#raid_params> */
	if (dm_read_arg_group(_args, &as, &num_raid_params, &ti->error))
		return -EINVAL;

	/* number of raid device tupples <meta_dev data_dev> */
	as_nrd = as;
	dm_consume_args(&as_nrd, num_raid_params);
	_args[1].max = (as_nrd.argc - 1) / 2;
	if (dm_read_arg(_args + 1, &as_nrd, &num_raid_devs, &ti->error))
		return -EINVAL;

	if (!__within_range(num_raid_devs, 1, MAX_RAID_DEVICES)) {
		ti->error = "Invalid number of supplied raid devices";
		return -EINVAL;
	}

	rs = raid_set_alloc(ti, rt, num_raid_devs);
	if (IS_ERR(rs))
		return PTR_ERR(rs);

	r = parse_raid_params(rs, &as, num_raid_params);
	if (r)
		goto bad;

	r = parse_dev_params(rs, &as);
	if (r)
		goto bad;

	rs->md.sync_super = super_sync;

	/*
	 * Calculate ctr requested array and device sizes to allow
	 * for superblock analysis needing device sizes defined.
	 *
	 * Any existing superblock will overwrite the array and device sizes
	 */
	r = rs_set_dev_and_array_sectors(rs, rs->ti->len, false);
	if (r)
		goto bad;

	/* Memorize just calculated, potentially larger sizes to grow the raid set in preresume */
	rs->array_sectors = rs->md.array_sectors;
	rs->dev_sectors = rs->md.dev_sectors;

	/*
	 * Backup any new raid set level, layout, ...
	 * requested to be able to compare to superblock
	 * members for conversion decisions.
	 */
	rs_config_backup(rs, &rs_layout);

	r = analyse_superblocks(ti, rs);
	if (r)
		goto bad;

	/* All in-core metadata now as of current superblocks after calling analyse_superblocks() */
	sb_array_sectors = rs->md.array_sectors;
	rdev_sectors = __rdev_sectors(rs);
	if (!rdev_sectors) {
		ti->error = "Invalid rdev size";
		r = -EINVAL;
		goto bad;
	}


	reshape_sectors = _get_reshape_sectors(rs);
	if (rs->dev_sectors != rdev_sectors) {
		resize = (rs->dev_sectors != rdev_sectors - reshape_sectors);
		if (rs->dev_sectors > rdev_sectors - reshape_sectors)
			set_bit(RT_FLAG_RS_GROW, &rs->runtime_flags);
	}

	INIT_WORK(&rs->md.event_work, do_table_event);
	ti->private = rs;
	ti->num_flush_bios = 1;

	/* Restore any requested new layout for conversion decision */
	rs_config_restore(rs, &rs_layout);

	/*
	 * Now that we have any superblock metadata available,
	 * check for new, recovering, reshaping, to be taken over,
	 * to be reshaped or an existing, unchanged raid set to
	 * run in sequence.
	 */
	if (test_bit(MD_ARRAY_FIRST_USE, &rs->md.flags)) {
		/* A new raid6 set has to be recovered to ensure proper parity and Q-Syndrome */
		if (rs_is_raid6(rs) &&
		    test_bit(__CTR_FLAG_NOSYNC, &rs->ctr_flags)) {
			ti->error = "'nosync' not allowed for new raid6 set";
			r = -EINVAL;
			goto bad;
		}
		rs_setup_recovery(rs, 0);
		set_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags);
		rs_set_new(rs);
	} else if (rs_is_recovering(rs)) {
		/* A recovering raid set may be resized */
		goto size_check;
	} else if (rs_is_reshaping(rs)) {
		/* Have to reject size change request during reshape */
		if (resize) {
			ti->error = "Can't resize a reshaping raid set";
			r = -EPERM;
			goto bad;
		}
		/* skip setup rs */
	} else if (rs_takeover_requested(rs)) {
		if (rs_is_reshaping(rs)) {
			ti->error = "Can't takeover a reshaping raid set";
			r = -EPERM;
			goto bad;
		}

		/* We can't takeover a journaled raid4/5/6 */
		if (test_bit(__CTR_FLAG_JOURNAL_DEV, &rs->ctr_flags)) {
			ti->error = "Can't takeover a journaled raid4/5/6 set";
			r = -EPERM;
			goto bad;
		}

		/*
		 * If a takeover is needed, userspace sets any additional
		 * devices to rebuild and we can check for a valid request here.
		 *
		 * If acceptible, set the level to the new requested
		 * one, prohibit requesting recovery, allow the raid
		 * set to run and store superblocks during resume.
		 */
		r = rs_check_takeover(rs);
		if (r)
			goto bad;

		r = rs_setup_takeover(rs);
		if (r)
			goto bad;

		set_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags);
		/* Takeover ain't recovery, so disable recovery */
		rs_setup_recovery(rs, MaxSector);
		rs_set_new(rs);
	} else if (rs_reshape_requested(rs)) {
		/* Only request grow on raid set size extensions, not on reshapes. */
		clear_bit(RT_FLAG_RS_GROW, &rs->runtime_flags);

		/*
		 * No need to check for 'ongoing' takeover here, because takeover
		 * is an instant operation as oposed to an ongoing reshape.
		 */

		/* We can't reshape a journaled raid4/5/6 */
		if (test_bit(__CTR_FLAG_JOURNAL_DEV, &rs->ctr_flags)) {
			ti->error = "Can't reshape a journaled raid4/5/6 set";
			r = -EPERM;
			goto bad;
		}

		/* Out-of-place space has to be available to allow for a reshape unless raid1! */
		if (reshape_sectors || rs_is_raid1(rs)) {
			/*
			  * We can only prepare for a reshape here, because the
			  * raid set needs to run to provide the repective reshape
			  * check functions via its MD personality instance.
			  *
			  * So do the reshape check after md_run() succeeded.
			  */
			r = rs_prepare_reshape(rs);
			if (r)
				goto bad;

			/* Reshaping ain't recovery, so disable recovery */
			rs_setup_recovery(rs, MaxSector);
		}
		rs_set_cur(rs);
	} else {
size_check:
		/* May not set recovery when a device rebuild is requested */
		if (test_bit(__CTR_FLAG_REBUILD, &rs->ctr_flags)) {
			clear_bit(RT_FLAG_RS_GROW, &rs->runtime_flags);
			set_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags);
			rs_setup_recovery(rs, MaxSector);
		} else if (test_bit(RT_FLAG_RS_GROW, &rs->runtime_flags)) {
			/*
			 * Set raid set to current size, i.e. size as of
			 * superblocks to grow to larger size in preresume.
			 */
			r = rs_set_dev_and_array_sectors(rs, sb_array_sectors, false);
			if (r)
				goto bad;

			rs_setup_recovery(rs, rs->md.recovery_cp < rs->md.dev_sectors ? rs->md.recovery_cp : rs->md.dev_sectors);
		} else {
			/* This is no size change or it is shrinking, update size and record in superblocks */
			r = rs_set_dev_and_array_sectors(rs, rs->ti->len, false);
			if (r)
				goto bad;

			if (sb_array_sectors > rs->array_sectors)
				set_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags);
		}
		rs_set_cur(rs);
	}

	/* If constructor requested it, change data and new_data offsets */
	r = rs_adjust_data_offsets(rs);
	if (r)
		goto bad;

	/* Catch any inconclusive reshape superblock content. */
	rs_reset_inconclusive_reshape(rs);

	/* Start raid set read-only and assumed clean to change in raid_resume() */
	rs->md.ro = 1;
	rs->md.in_sync = 1;

	/* Keep array frozen until resume. */
	set_bit(MD_RECOVERY_FROZEN, &rs->md.recovery);

	/* Has to be held on running the array */
	mddev_lock_nointr(&rs->md);
	r = md_run(&rs->md);
	rs->md.in_sync = 0; /* Assume already marked dirty */
	if (r) {
		ti->error = "Failed to run raid array";
		mddev_unlock(&rs->md);
		goto bad;
	}

	r = md_start(&rs->md);
	if (r) {
		ti->error = "Failed to start raid array";
		mddev_unlock(&rs->md);
		goto bad_md_start;
	}

	/* If raid4/5/6 journal mode explicitly requested (only possible with journal dev) -> set it */
	if (test_bit(__CTR_FLAG_JOURNAL_MODE, &rs->ctr_flags)) {
		r = r5c_journal_mode_set(&rs->md, rs->journal_dev.mode);
		if (r) {
			ti->error = "Failed to set raid4/5/6 journal mode";
			mddev_unlock(&rs->md);
			goto bad_journal_mode_set;
		}
	}

	mddev_suspend(&rs->md);
	set_bit(RT_FLAG_RS_SUSPENDED, &rs->runtime_flags);

	/* Try to adjust the raid4/5/6 stripe cache size to the stripe size */
	if (rs_is_raid456(rs)) {
		r = rs_set_raid456_stripe_cache(rs);
		if (r)
			goto bad_stripe_cache;
	}

	/* Now do an early reshape check */
	if (test_bit(RT_FLAG_RESHAPE_RS, &rs->runtime_flags)) {
		r = rs_check_reshape(rs);
		if (r)
			goto bad_check_reshape;

		/* Restore new, ctr requested layout to perform check */
		rs_config_restore(rs, &rs_layout);

		if (rs->md.pers->start_reshape) {
			r = rs->md.pers->check_reshape(&rs->md);
			if (r) {
				ti->error = "Reshape check failed";
				goto bad_check_reshape;
			}
		}
	}

	/* Disable/enable discard support on raid set. */
	configure_discard_support(rs);

	mddev_unlock(&rs->md);
	return 0;

bad_md_start:
bad_journal_mode_set:
bad_stripe_cache:
bad_check_reshape:
	md_stop(&rs->md);
bad:
	raid_set_free(rs);

	return r;
}

static void raid_dtr(struct dm_target *ti)
{
	struct raid_set *rs = ti->private;

	md_stop(&rs->md);
	raid_set_free(rs);
}

static int raid_map(struct dm_target *ti, struct bio *bio)
{
	struct raid_set *rs = ti->private;
	struct mddev *mddev = &rs->md;

	/*
	 * If we're reshaping to add disk(s)), ti->len and
	 * mddev->array_sectors will differ during the process
	 * (ti->len > mddev->array_sectors), so we have to requeue
	 * bios with addresses > mddev->array_sectors here or
	 * there will occur accesses past EOD of the component
	 * data images thus erroring the raid set.
	 */
	if (unlikely(bio_end_sector(bio) > mddev->array_sectors))
		return DM_MAPIO_REQUEUE;

	md_handle_request(mddev, bio);

	return DM_MAPIO_SUBMITTED;
}

/* Return sync state string for @state */
enum sync_state { st_frozen, st_reshape, st_resync, st_check, st_repair, st_recover, st_idle };
static const char *sync_str(enum sync_state state)
{
	/* Has to be in above sync_state order! */
	static const char *sync_strs[] = {
		"frozen",
		"reshape",
		"resync",
		"check",
		"repair",
		"recover",
		"idle"
	};

	return __within_range(state, 0, ARRAY_SIZE(sync_strs) - 1) ? sync_strs[state] : "undef";
};

/* Return enum sync_state for @mddev derived from @recovery flags */
static enum sync_state decipher_sync_action(struct mddev *mddev, unsigned long recovery)
{
	if (test_bit(MD_RECOVERY_FROZEN, &recovery))
		return st_frozen;

	/* The MD sync thread can be done with io or be interrupted but still be running */
	if (!test_bit(MD_RECOVERY_DONE, &recovery) &&
	    (test_bit(MD_RECOVERY_RUNNING, &recovery) ||
	     (!mddev->ro && test_bit(MD_RECOVERY_NEEDED, &recovery)))) {
		if (test_bit(MD_RECOVERY_RESHAPE, &recovery))
			return st_reshape;

		if (test_bit(MD_RECOVERY_SYNC, &recovery)) {
			if (!test_bit(MD_RECOVERY_REQUESTED, &recovery))
				return st_resync;
			if (test_bit(MD_RECOVERY_CHECK, &recovery))
				return st_check;
			return st_repair;
		}

		if (test_bit(MD_RECOVERY_RECOVER, &recovery))
			return st_recover;

		if (mddev->reshape_position != MaxSector)
			return st_reshape;
	}

	return st_idle;
}

/*
 * Return status string for @rdev
 *
 * Status characters:
 *
 *  'D' = Dead/Failed raid set component or raid4/5/6 journal device
 *  'a' = Alive but not in-sync raid set component _or_ alive raid4/5/6 'write_back' journal device
 *  'A' = Alive and in-sync raid set component _or_ alive raid4/5/6 'write_through' journal device
 *  '-' = Non-existing device (i.e. uspace passed '- -' into the ctr)
 */
static const char *__raid_dev_status(struct raid_set *rs, struct md_rdev *rdev)
{
	if (!rdev->bdev)
		return "-";
	else if (test_bit(Faulty, &rdev->flags))
		return "D";
	else if (test_bit(Journal, &rdev->flags))
		return (rs->journal_dev.mode == R5C_JOURNAL_MODE_WRITE_THROUGH) ? "A" : "a";
	else if (test_bit(RT_FLAG_RS_RESYNCING, &rs->runtime_flags) ||
		 (!test_bit(RT_FLAG_RS_IN_SYNC, &rs->runtime_flags) &&
		  !test_bit(In_sync, &rdev->flags)))
		return "a";
	else
		return "A";
}

/* Helper to return resync/reshape progress for @rs and runtime flags for raid set in sync / resynching */
static sector_t rs_get_progress(struct raid_set *rs, unsigned long recovery,
				enum sync_state state, sector_t resync_max_sectors)
{
	sector_t r;
	struct mddev *mddev = &rs->md;

	clear_bit(RT_FLAG_RS_IN_SYNC, &rs->runtime_flags);
	clear_bit(RT_FLAG_RS_RESYNCING, &rs->runtime_flags);

	if (rs_is_raid0(rs)) {
		r = resync_max_sectors;
		set_bit(RT_FLAG_RS_IN_SYNC, &rs->runtime_flags);

	} else {
		if (state == st_idle && !test_bit(MD_RECOVERY_INTR, &recovery))
			r = mddev->recovery_cp;
		else
			r = mddev->curr_resync_completed;

		if (state == st_idle && r >= resync_max_sectors) {
			/*
			 * Sync complete.
			 */
			/* In case we have finished recovering, the array is in sync. */
			if (test_bit(MD_RECOVERY_RECOVER, &recovery))
				set_bit(RT_FLAG_RS_IN_SYNC, &rs->runtime_flags);

		} else if (state == st_recover)
			/*
			 * In case we are recovering, the array is not in sync
			 * and health chars should show the recovering legs.
			 *
			 * Already retrieved recovery offset from curr_resync_completed above.
			 */
			;

		else if (state == st_resync || state == st_reshape)
			/*
			 * If "resync/reshape" is occurring, the raid set
			 * is or may be out of sync hence the health
			 * characters shall be 'a'.
			 */
			set_bit(RT_FLAG_RS_RESYNCING, &rs->runtime_flags);

		else if (state == st_check || state == st_repair)
			/*
			 * If "check" or "repair" is occurring, the raid set has
			 * undergone an initial sync and the health characters
			 * should not be 'a' anymore.
			 */
			set_bit(RT_FLAG_RS_IN_SYNC, &rs->runtime_flags);

		else if (test_bit(MD_RECOVERY_NEEDED, &recovery))
			/*
			 * We are idle and recovery is needed, prevent 'A' chars race
			 * caused by components still set to in-sync by constructor.
			 */
			set_bit(RT_FLAG_RS_RESYNCING, &rs->runtime_flags);

		else {
			/*
			 * We are idle and the raid set may be doing an initial
			 * sync, or it may be rebuilding individual components.
			 * If all the devices are In_sync, then it is the raid set
			 * that is being initialized.
			 */
			struct md_rdev *rdev;

			set_bit(RT_FLAG_RS_IN_SYNC, &rs->runtime_flags);
			rdev_for_each(rdev, mddev)
				if (!test_bit(Journal, &rdev->flags) &&
				    !test_bit(In_sync, &rdev->flags)) {
					clear_bit(RT_FLAG_RS_IN_SYNC, &rs->runtime_flags);
					break;
				}
		}
	}

	return min(r, resync_max_sectors);
}

/* Helper to return @dev name or "-" if !@dev */
static const char *__get_dev_name(struct dm_dev *dev)
{
	return dev ? dev->name : "-";
}

static void raid_status(struct dm_target *ti, status_type_t type,
			unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct raid_set *rs = ti->private;
	struct mddev *mddev = &rs->md;
	struct r5conf *conf = rs_is_raid456(rs) ? mddev->private : NULL;
	int i, max_nr_stripes = conf ? conf->max_nr_stripes : 0;
	unsigned long recovery;
	unsigned int raid_param_cnt = 1; /* at least 1 for chunksize */
	unsigned int sz = 0;
	unsigned int rebuild_writemostly_count = 0;
	sector_t progress, resync_max_sectors, resync_mismatches;
	enum sync_state state;
	struct raid_type *rt;

	switch (type) {
	case STATUSTYPE_INFO:
		/* *Should* always succeed */
		rt = get_raid_type_by_ll(mddev->new_level, mddev->new_layout);
		if (!rt)
			return;

		DMEMIT("%s %d ", rt->name, mddev->raid_disks);

		/* Access most recent mddev properties for status output */
		smp_rmb();
		/* Get sensible max sectors even if raid set not yet started */
		resync_max_sectors = test_bit(RT_FLAG_RS_PRERESUMED, &rs->runtime_flags) ?
				      mddev->resync_max_sectors : mddev->dev_sectors;
		recovery = rs->md.recovery;
		state = decipher_sync_action(mddev, recovery);
		progress = rs_get_progress(rs, recovery, state, resync_max_sectors);
		resync_mismatches = (mddev->last_sync_action && !strcasecmp(mddev->last_sync_action, "check")) ?
				    atomic64_read(&mddev->resync_mismatches) : 0;

		/* HM FIXME: do we want another state char for raid0? It shows 'D'/'A'/'-' now */
		for (i = 0; i < rs->raid_disks; i++)
			DMEMIT(__raid_dev_status(rs, &rs->dev[i].rdev));

		/*
		 * In-sync/Reshape ratio:
		 *  The in-sync ratio shows the progress of:
		 *   - Initializing the raid set
		 *   - Rebuilding a subset of devices of the raid set
		 *  The user can distinguish between the two by referring
		 *  to the status characters.
		 *
		 *  The reshape ratio shows the progress of
		 *  changing the raid layout or the number of
		 *  disks of a raid set
		 */
		DMEMIT(" %llu/%llu", (unsigned long long) progress,
				     (unsigned long long) resync_max_sectors);

		/*
		 * v1.5.0+:
		 *
		 * Sync action:
		 *   See Documentation/admin-guide/device-mapper/dm-raid.rst for
		 *   information on each of these states.
		 */
		DMEMIT(" %s", sync_str(state));

		/*
		 * v1.5.0+:
		 *
		 * resync_mismatches/mismatch_cnt
		 *   This field shows the number of discrepancies found when
		 *   performing a "check" of the raid set.
		 */
		DMEMIT(" %llu", (unsigned long long) resync_mismatches);

		/*
		 * v1.9.0+:
		 *
		 * data_offset (needed for out of space reshaping)
		 *   This field shows the data offset into the data
		 *   image LV where the first stripes data starts.
		 *
		 * We keep data_offset equal on all raid disks of the set,
		 * so retrieving it from the first raid disk is sufficient.
		 */
		DMEMIT(" %llu", (unsigned long long) rs->dev[0].rdev.data_offset);

		/*
		 * v1.10.0+:
		 */
		DMEMIT(" %s", test_bit(__CTR_FLAG_JOURNAL_DEV, &rs->ctr_flags) ?
			      __raid_dev_status(rs, &rs->journal_dev.rdev) : "-");
		break;

	case STATUSTYPE_TABLE:
		/* Report the table line string you would use to construct this raid set */

		/*
		 * Count any rebuild or writemostly argument pairs and subtract the
		 * hweight count being added below of any rebuild and writemostly ctr flags.
		 */
		for (i = 0; i < rs->raid_disks; i++) {
			rebuild_writemostly_count += (test_bit(i, (void *) rs->rebuild_disks) ? 2 : 0) +
						     (test_bit(WriteMostly, &rs->dev[i].rdev.flags) ? 2 : 0);
		}
		rebuild_writemostly_count -= (test_bit(__CTR_FLAG_REBUILD, &rs->ctr_flags) ? 2 : 0) +
					     (test_bit(__CTR_FLAG_WRITE_MOSTLY, &rs->ctr_flags) ? 2 : 0);
		/* Calculate raid parameter count based on ^ rebuild/writemostly argument counts and ctr flags set. */
		raid_param_cnt += rebuild_writemostly_count +
				  hweight32(rs->ctr_flags & CTR_FLAG_OPTIONS_NO_ARGS) +
				  hweight32(rs->ctr_flags & CTR_FLAG_OPTIONS_ONE_ARG) * 2;
		/* Emit table line */
		/* This has to be in the documented order for userspace! */
		DMEMIT("%s %u %u", rs->raid_type->name, raid_param_cnt, mddev->new_chunk_sectors);
		if (test_bit(__CTR_FLAG_SYNC, &rs->ctr_flags))
			DMEMIT(" %s", dm_raid_arg_name_by_flag(CTR_FLAG_SYNC));
		if (test_bit(__CTR_FLAG_NOSYNC, &rs->ctr_flags))
			DMEMIT(" %s", dm_raid_arg_name_by_flag(CTR_FLAG_NOSYNC));
		if (test_bit(__CTR_FLAG_REBUILD, &rs->ctr_flags))
			for (i = 0; i < rs->raid_disks; i++)
				if (test_bit(i, (void *) rs->rebuild_disks))
					DMEMIT(" %s %u", dm_raid_arg_name_by_flag(CTR_FLAG_REBUILD), i);
		if (test_bit(__CTR_FLAG_DAEMON_SLEEP, &rs->ctr_flags))
			DMEMIT(" %s %lu", dm_raid_arg_name_by_flag(CTR_FLAG_DAEMON_SLEEP),
					  mddev->bitmap_info.daemon_sleep);
		if (test_bit(__CTR_FLAG_MIN_RECOVERY_RATE, &rs->ctr_flags))
			DMEMIT(" %s %d", dm_raid_arg_name_by_flag(CTR_FLAG_MIN_RECOVERY_RATE),
					 mddev->sync_speed_min);
		if (test_bit(__CTR_FLAG_MAX_RECOVERY_RATE, &rs->ctr_flags))
			DMEMIT(" %s %d", dm_raid_arg_name_by_flag(CTR_FLAG_MAX_RECOVERY_RATE),
					 mddev->sync_speed_max);
		if (test_bit(__CTR_FLAG_WRITE_MOSTLY, &rs->ctr_flags))
			for (i = 0; i < rs->raid_disks; i++)
				if (test_bit(WriteMostly, &rs->dev[i].rdev.flags))
					DMEMIT(" %s %d", dm_raid_arg_name_by_flag(CTR_FLAG_WRITE_MOSTLY),
					       rs->dev[i].rdev.raid_disk);
		if (test_bit(__CTR_FLAG_MAX_WRITE_BEHIND, &rs->ctr_flags))
			DMEMIT(" %s %lu", dm_raid_arg_name_by_flag(CTR_FLAG_MAX_WRITE_BEHIND),
					  mddev->bitmap_info.max_write_behind);
		if (test_bit(__CTR_FLAG_STRIPE_CACHE, &rs->ctr_flags))
			DMEMIT(" %s %d", dm_raid_arg_name_by_flag(CTR_FLAG_STRIPE_CACHE),
					 max_nr_stripes);
		if (test_bit(__CTR_FLAG_REGION_SIZE, &rs->ctr_flags))
			DMEMIT(" %s %llu", dm_raid_arg_name_by_flag(CTR_FLAG_REGION_SIZE),
					   (unsigned long long) to_sector(mddev->bitmap_info.chunksize));
		if (test_bit(__CTR_FLAG_RAID10_COPIES, &rs->ctr_flags))
			DMEMIT(" %s %d", dm_raid_arg_name_by_flag(CTR_FLAG_RAID10_COPIES),
					 raid10_md_layout_to_copies(mddev->layout));
		if (test_bit(__CTR_FLAG_RAID10_FORMAT, &rs->ctr_flags))
			DMEMIT(" %s %s", dm_raid_arg_name_by_flag(CTR_FLAG_RAID10_FORMAT),
					 raid10_md_layout_to_format(mddev->layout));
		if (test_bit(__CTR_FLAG_DELTA_DISKS, &rs->ctr_flags))
			DMEMIT(" %s %d", dm_raid_arg_name_by_flag(CTR_FLAG_DELTA_DISKS),
					 max(rs->delta_disks, mddev->delta_disks));
		if (test_bit(__CTR_FLAG_DATA_OFFSET, &rs->ctr_flags))
			DMEMIT(" %s %llu", dm_raid_arg_name_by_flag(CTR_FLAG_DATA_OFFSET),
					   (unsigned long long) rs->data_offset);
		if (test_bit(__CTR_FLAG_JOURNAL_DEV, &rs->ctr_flags))
			DMEMIT(" %s %s", dm_raid_arg_name_by_flag(CTR_FLAG_JOURNAL_DEV),
					__get_dev_name(rs->journal_dev.dev));
		if (test_bit(__CTR_FLAG_JOURNAL_MODE, &rs->ctr_flags))
			DMEMIT(" %s %s", dm_raid_arg_name_by_flag(CTR_FLAG_JOURNAL_MODE),
					 md_journal_mode_to_dm_raid(rs->journal_dev.mode));
		DMEMIT(" %d", rs->raid_disks);
		for (i = 0; i < rs->raid_disks; i++)
			DMEMIT(" %s %s", __get_dev_name(rs->dev[i].meta_dev),
					 __get_dev_name(rs->dev[i].data_dev));
		break;

	case STATUSTYPE_IMA:
		rt = get_raid_type_by_ll(mddev->new_level, mddev->new_layout);
		if (!rt)
			return;

		DMEMIT_TARGET_NAME_VERSION(ti->type);
		DMEMIT(",raid_type=%s,raid_disks=%d", rt->name, mddev->raid_disks);

		/* Access most recent mddev properties for status output */
		smp_rmb();
		recovery = rs->md.recovery;
		state = decipher_sync_action(mddev, recovery);
		DMEMIT(",raid_state=%s", sync_str(state));

		for (i = 0; i < rs->raid_disks; i++) {
			DMEMIT(",raid_device_%d_status=", i);
			DMEMIT(__raid_dev_status(rs, &rs->dev[i].rdev));
		}

		if (rt_is_raid456(rt)) {
			DMEMIT(",journal_dev_mode=");
			switch (rs->journal_dev.mode) {
			case R5C_JOURNAL_MODE_WRITE_THROUGH:
				DMEMIT("%s",
				       _raid456_journal_mode[R5C_JOURNAL_MODE_WRITE_THROUGH].param);
				break;
			case R5C_JOURNAL_MODE_WRITE_BACK:
				DMEMIT("%s",
				       _raid456_journal_mode[R5C_JOURNAL_MODE_WRITE_BACK].param);
				break;
			default:
				DMEMIT("invalid");
				break;
			}
		}
		DMEMIT(";");
		break;
	}
}

static int raid_message(struct dm_target *ti, unsigned int argc, char **argv,
			char *result, unsigned maxlen)
{
	struct raid_set *rs = ti->private;
	struct mddev *mddev = &rs->md;

	if (!mddev->pers || !mddev->pers->sync_request)
		return -EINVAL;

	if (!strcasecmp(argv[0], "frozen"))
		set_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
	else
		clear_bit(MD_RECOVERY_FROZEN, &mddev->recovery);

	if (!strcasecmp(argv[0], "idle") || !strcasecmp(argv[0], "frozen")) {
		if (mddev->sync_thread) {
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			md_reap_sync_thread(mddev);
		}
	} else if (decipher_sync_action(mddev, mddev->recovery) != st_idle)
		return -EBUSY;
	else if (!strcasecmp(argv[0], "resync"))
		; /* MD_RECOVERY_NEEDED set below */
	else if (!strcasecmp(argv[0], "recover"))
		set_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
	else {
		if (!strcasecmp(argv[0], "check")) {
			set_bit(MD_RECOVERY_CHECK, &mddev->recovery);
			set_bit(MD_RECOVERY_REQUESTED, &mddev->recovery);
			set_bit(MD_RECOVERY_SYNC, &mddev->recovery);
		} else if (!strcasecmp(argv[0], "repair")) {
			set_bit(MD_RECOVERY_REQUESTED, &mddev->recovery);
			set_bit(MD_RECOVERY_SYNC, &mddev->recovery);
		} else
			return -EINVAL;
	}
	if (mddev->ro == 2) {
		/* A write to sync_action is enough to justify
		 * canceling read-auto mode
		 */
		mddev->ro = 0;
		if (!mddev->suspended && mddev->sync_thread)
			md_wakeup_thread(mddev->sync_thread);
	}
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	if (!mddev->suspended && mddev->thread)
		md_wakeup_thread(mddev->thread);

	return 0;
}

static int raid_iterate_devices(struct dm_target *ti,
				iterate_devices_callout_fn fn, void *data)
{
	struct raid_set *rs = ti->private;
	unsigned int i;
	int r = 0;

	for (i = 0; !r && i < rs->raid_disks; i++) {
		if (rs->dev[i].data_dev) {
			r = fn(ti, rs->dev[i].data_dev,
			       0, /* No offset on data devs */
			       rs->md.dev_sectors, data);
		}
	}

	return r;
}

static void raid_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct raid_set *rs = ti->private;
	unsigned int chunk_size_bytes = to_bytes(rs->md.chunk_sectors);

	blk_limits_io_min(limits, chunk_size_bytes);
	blk_limits_io_opt(limits, chunk_size_bytes * mddev_data_stripes(rs));
}

static void raid_postsuspend(struct dm_target *ti)
{
	struct raid_set *rs = ti->private;

	if (!test_and_set_bit(RT_FLAG_RS_SUSPENDED, &rs->runtime_flags)) {
		/* Writes have to be stopped before suspending to avoid deadlocks. */
		if (!test_bit(MD_RECOVERY_FROZEN, &rs->md.recovery))
			md_stop_writes(&rs->md);

		mddev_lock_nointr(&rs->md);
		mddev_suspend(&rs->md);
		mddev_unlock(&rs->md);
	}
}

static void attempt_restore_of_faulty_devices(struct raid_set *rs)
{
	int i;
	uint64_t cleared_failed_devices[DISKS_ARRAY_ELEMS];
	unsigned long flags;
	bool cleared = false;
	struct dm_raid_superblock *sb;
	struct mddev *mddev = &rs->md;
	struct md_rdev *r;

	/* RAID personalities have to provide hot add/remove methods or we need to bail out. */
	if (!mddev->pers || !mddev->pers->hot_add_disk || !mddev->pers->hot_remove_disk)
		return;

	memset(cleared_failed_devices, 0, sizeof(cleared_failed_devices));

	for (i = 0; i < rs->raid_disks; i++) {
		r = &rs->dev[i].rdev;
		/* HM FIXME: enhance journal device recovery processing */
		if (test_bit(Journal, &r->flags))
			continue;

		if (test_bit(Faulty, &r->flags) &&
		    r->meta_bdev && !read_disk_sb(r, r->sb_size, true)) {
			DMINFO("Faulty %s device #%d has readable super block."
			       "  Attempting to revive it.",
			       rs->raid_type->name, i);

			/*
			 * Faulty bit may be set, but sometimes the array can
			 * be suspended before the personalities can respond
			 * by removing the device from the array (i.e. calling
			 * 'hot_remove_disk').	If they haven't yet removed
			 * the failed device, its 'raid_disk' number will be
			 * '>= 0' - meaning we must call this function
			 * ourselves.
			 */
			flags = r->flags;
			clear_bit(In_sync, &r->flags); /* Mandatory for hot remove. */
			if (r->raid_disk >= 0) {
				if (mddev->pers->hot_remove_disk(mddev, r)) {
					/* Failed to revive this device, try next */
					r->flags = flags;
					continue;
				}
			} else
				r->raid_disk = r->saved_raid_disk = i;

			clear_bit(Faulty, &r->flags);
			clear_bit(WriteErrorSeen, &r->flags);

			if (mddev->pers->hot_add_disk(mddev, r)) {
				/* Failed to revive this device, try next */
				r->raid_disk = r->saved_raid_disk = -1;
				r->flags = flags;
			} else {
				clear_bit(In_sync, &r->flags);
				r->recovery_offset = 0;
				set_bit(i, (void *) cleared_failed_devices);
				cleared = true;
			}
		}
	}

	/* If any failed devices could be cleared, update all sbs failed_devices bits */
	if (cleared) {
		uint64_t failed_devices[DISKS_ARRAY_ELEMS];

		rdev_for_each(r, &rs->md) {
			if (test_bit(Journal, &r->flags))
				continue;

			sb = page_address(r->sb_page);
			sb_retrieve_failed_devices(sb, failed_devices);

			for (i = 0; i < DISKS_ARRAY_ELEMS; i++)
				failed_devices[i] &= ~cleared_failed_devices[i];

			sb_update_failed_devices(sb, failed_devices);
		}
	}
}

static int __load_dirty_region_bitmap(struct raid_set *rs)
{
	int r = 0;

	/* Try loading the bitmap unless "raid0", which does not have one */
	if (!rs_is_raid0(rs) &&
	    !test_and_set_bit(RT_FLAG_RS_BITMAP_LOADED, &rs->runtime_flags)) {
		r = md_bitmap_load(&rs->md);
		if (r)
			DMERR("Failed to load bitmap");
	}

	return r;
}

/* Enforce updating all superblocks */
static void rs_update_sbs(struct raid_set *rs)
{
	struct mddev *mddev = &rs->md;
	int ro = mddev->ro;

	set_bit(MD_SB_CHANGE_DEVS, &mddev->sb_flags);
	mddev->ro = 0;
	md_update_sb(mddev, 1);
	mddev->ro = ro;
}

/*
 * Reshape changes raid algorithm of @rs to new one within personality
 * (e.g. raid6_zr -> raid6_nc), changes stripe size, adds/removes
 * disks from a raid set thus growing/shrinking it or resizes the set
 *
 * Call mddev_lock_nointr() before!
 */
static int rs_start_reshape(struct raid_set *rs)
{
	int r;
	struct mddev *mddev = &rs->md;
	struct md_personality *pers = mddev->pers;

	/* Don't allow the sync thread to work until the table gets reloaded. */
	set_bit(MD_RECOVERY_WAIT, &mddev->recovery);

	r = rs_setup_reshape(rs);
	if (r)
		return r;

	/*
	 * Check any reshape constraints enforced by the personalility
	 *
	 * May as well already kick the reshape off so that * pers->start_reshape() becomes optional.
	 */
	r = pers->check_reshape(mddev);
	if (r) {
		rs->ti->error = "pers->check_reshape() failed";
		return r;
	}

	/*
	 * Personality may not provide start reshape method in which
	 * case check_reshape above has already covered everything
	 */
	if (pers->start_reshape) {
		r = pers->start_reshape(mddev);
		if (r) {
			rs->ti->error = "pers->start_reshape() failed";
			return r;
		}
	}

	/*
	 * Now reshape got set up, update superblocks to
	 * reflect the fact so that a table reload will
	 * access proper superblock content in the ctr.
	 */
	rs_update_sbs(rs);

	return 0;
}

static int raid_preresume(struct dm_target *ti)
{
	int r;
	struct raid_set *rs = ti->private;
	struct mddev *mddev = &rs->md;

	/* This is a resume after a suspend of the set -> it's already started. */
	if (test_and_set_bit(RT_FLAG_RS_PRERESUMED, &rs->runtime_flags))
		return 0;

	/*
	 * The superblocks need to be updated on disk if the
	 * array is new or new devices got added (thus zeroed
	 * out by userspace) or __load_dirty_region_bitmap
	 * will overwrite them in core with old data or fail.
	 */
	if (test_bit(RT_FLAG_UPDATE_SBS, &rs->runtime_flags))
		rs_update_sbs(rs);

	/* Load the bitmap from disk unless raid0 */
	r = __load_dirty_region_bitmap(rs);
	if (r)
		return r;

	/* We are extending the raid set size, adjust mddev/md_rdev sizes and set capacity. */
	if (test_bit(RT_FLAG_RS_GROW, &rs->runtime_flags)) {
		mddev->array_sectors = rs->array_sectors;
		mddev->dev_sectors = rs->dev_sectors;
		rs_set_rdev_sectors(rs);
		rs_set_capacity(rs);
	}

	/* Resize bitmap to adjust to changed region size (aka MD bitmap chunksize) or grown device size */
        if (test_bit(RT_FLAG_RS_BITMAP_LOADED, &rs->runtime_flags) && mddev->bitmap &&
	    (test_bit(RT_FLAG_RS_GROW, &rs->runtime_flags) ||
	     (rs->requested_bitmap_chunk_sectors &&
	       mddev->bitmap_info.chunksize != to_bytes(rs->requested_bitmap_chunk_sectors)))) {
		int chunksize = to_bytes(rs->requested_bitmap_chunk_sectors) ?: mddev->bitmap_info.chunksize;

		r = md_bitmap_resize(mddev->bitmap, mddev->dev_sectors, chunksize, 0);
		if (r)
			DMERR("Failed to resize bitmap");
	}

	/* Check for any resize/reshape on @rs and adjust/initiate */
	/* Be prepared for mddev_resume() in raid_resume() */
	set_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
	if (mddev->recovery_cp && mddev->recovery_cp < MaxSector) {
		set_bit(MD_RECOVERY_REQUESTED, &mddev->recovery);
		mddev->resync_min = mddev->recovery_cp;
		if (test_bit(RT_FLAG_RS_GROW, &rs->runtime_flags))
			mddev->resync_max_sectors = mddev->dev_sectors;
	}

	/* Check for any reshape request unless new raid set */
	if (test_bit(RT_FLAG_RESHAPE_RS, &rs->runtime_flags)) {
		/* Initiate a reshape. */
		rs_set_rdev_sectors(rs);
		mddev_lock_nointr(mddev);
		r = rs_start_reshape(rs);
		mddev_unlock(mddev);
		if (r)
			DMWARN("Failed to check/start reshape, continuing without change");
		r = 0;
	}

	return r;
}

static void raid_resume(struct dm_target *ti)
{
	struct raid_set *rs = ti->private;
	struct mddev *mddev = &rs->md;

	if (test_and_set_bit(RT_FLAG_RS_RESUMED, &rs->runtime_flags)) {
		/*
		 * A secondary resume while the device is active.
		 * Take this opportunity to check whether any failed
		 * devices are reachable again.
		 */
		attempt_restore_of_faulty_devices(rs);
	}

	if (test_and_clear_bit(RT_FLAG_RS_SUSPENDED, &rs->runtime_flags)) {
		/* Only reduce raid set size before running a disk removing reshape. */
		if (mddev->delta_disks < 0)
			rs_set_capacity(rs);

		mddev_lock_nointr(mddev);
		clear_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
		mddev->ro = 0;
		mddev->in_sync = 0;
		mddev_resume(mddev);
		mddev_unlock(mddev);
	}
}

static struct target_type raid_target = {
	.name = "raid",
	.version = {1, 15, 1},
	.module = THIS_MODULE,
	.ctr = raid_ctr,
	.dtr = raid_dtr,
	.map = raid_map,
	.status = raid_status,
	.message = raid_message,
	.iterate_devices = raid_iterate_devices,
	.io_hints = raid_io_hints,
	.postsuspend = raid_postsuspend,
	.preresume = raid_preresume,
	.resume = raid_resume,
};

static int __init dm_raid_init(void)
{
	DMINFO("Loading target version %u.%u.%u",
	       raid_target.version[0],
	       raid_target.version[1],
	       raid_target.version[2]);
	return dm_register_target(&raid_target);
}

static void __exit dm_raid_exit(void)
{
	dm_unregister_target(&raid_target);
}

module_init(dm_raid_init);
module_exit(dm_raid_exit);

module_param(devices_handle_discard_safely, bool, 0644);
MODULE_PARM_DESC(devices_handle_discard_safely,
		 "Set to Y if all devices in each array reliably return zeroes on reads from discarded regions");

MODULE_DESCRIPTION(DM_NAME " raid0/1/10/4/5/6 target");
MODULE_ALIAS("dm-raid0");
MODULE_ALIAS("dm-raid1");
MODULE_ALIAS("dm-raid10");
MODULE_ALIAS("dm-raid4");
MODULE_ALIAS("dm-raid5");
MODULE_ALIAS("dm-raid6");
MODULE_AUTHOR("Neil Brown <dm-devel@redhat.com>");
MODULE_AUTHOR("Heinz Mauelshagen <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
