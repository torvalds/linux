/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 */

/*
 * This file contains the functions which analyze the status of a pool.  This
 * include both the status of an active pool, as well as the status exported
 * pools.  Returns one of the ZPOOL_STATUS_* defines describing the status of
 * the pool.  This status is independent (to a certain degree) from the state of
 * the pool.  A pool's state describes only whether or not it is capable of
 * providing the necessary fault tolerance for data.  The status describes the
 * overall status of devices.  A pool that is online can still have a device
 * that is experiencing errors.
 *
 * Only a subset of the possible faults can be detected using 'zpool status',
 * and not all possible errors correspond to a FMA message ID.  The explanation
 * is left up to the caller, depending on whether it is a live pool or an
 * import.
 */

#include <libzfs.h>
#include <string.h>
#include <unistd.h>
#include "libzfs_impl.h"
#include "zfeature_common.h"

/*
 * Message ID table.  This must be kept in sync with the ZPOOL_STATUS_* defines
 * in libzfs.h.  Note that there are some status results which go past the end
 * of this table, and hence have no associated message ID.
 */
static char *zfs_msgid_table[] = {
	"ZFS-8000-14",
	"ZFS-8000-2Q",
	"ZFS-8000-3C",
	"ZFS-8000-4J",
	"ZFS-8000-5E",
	"ZFS-8000-6X",
	"ZFS-8000-72",
	"ZFS-8000-8A",
	"ZFS-8000-9P",
	"ZFS-8000-A5",
	"ZFS-8000-EY",
	"ZFS-8000-HC",
	"ZFS-8000-JQ",
	"ZFS-8000-K4",
};

#define	NMSGID	(sizeof (zfs_msgid_table) / sizeof (zfs_msgid_table[0]))

/* ARGSUSED */
static int
vdev_missing(vdev_stat_t *vs, uint_t vsc)
{
	return (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_OPEN_FAILED);
}

/* ARGSUSED */
static int
vdev_faulted(vdev_stat_t *vs, uint_t vsc)
{
	return (vs->vs_state == VDEV_STATE_FAULTED);
}

/* ARGSUSED */
static int
vdev_errors(vdev_stat_t *vs, uint_t vsc)
{
	return (vs->vs_state == VDEV_STATE_DEGRADED ||
	    vs->vs_read_errors != 0 || vs->vs_write_errors != 0 ||
	    vs->vs_checksum_errors != 0);
}

/* ARGSUSED */
static int
vdev_broken(vdev_stat_t *vs, uint_t vsc)
{
	return (vs->vs_state == VDEV_STATE_CANT_OPEN);
}

/* ARGSUSED */
static int
vdev_offlined(vdev_stat_t *vs, uint_t vsc)
{
	return (vs->vs_state == VDEV_STATE_OFFLINE);
}

/* ARGSUSED */
static int
vdev_removed(vdev_stat_t *vs, uint_t vsc)
{
	return (vs->vs_state == VDEV_STATE_REMOVED);
}

static int
vdev_non_native_ashift(vdev_stat_t *vs, uint_t vsc)
{
	return (VDEV_STAT_VALID(vs_physical_ashift, vsc) &&
	    vs->vs_configured_ashift < vs->vs_physical_ashift);
}

/*
 * Detect if any leaf devices that have seen errors or could not be opened.
 */
static boolean_t
find_vdev_problem(nvlist_t *vdev, int (*func)(vdev_stat_t *, uint_t),
    boolean_t ignore_replacing)
{
	nvlist_t **child;
	vdev_stat_t *vs;
	uint_t c, vsc, children;

	/*
	 * Ignore problems within a 'replacing' vdev, since we're presumably in
	 * the process of repairing any such errors, and don't want to call them
	 * out again.  We'll pick up the fact that a resilver is happening
	 * later.
	 */
	if (ignore_replacing == B_TRUE) {
		char *type;

		verify(nvlist_lookup_string(vdev, ZPOOL_CONFIG_TYPE,
		    &type) == 0);
		if (strcmp(type, VDEV_TYPE_REPLACING) == 0)
			return (B_FALSE);
	}

	if (nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_vdev_problem(child[c], func, ignore_replacing))
				return (B_TRUE);
	} else {
		verify(nvlist_lookup_uint64_array(vdev, ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t **)&vs, &vsc) == 0);

		if (func(vs, vsc) != 0)
			return (B_TRUE);
	}

	/*
	 * Check any L2 cache devs
	 */
	if (nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_L2CACHE, &child,
	    &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_vdev_problem(child[c], func, ignore_replacing))
				return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Active pool health status.
 *
 * To determine the status for a pool, we make several passes over the config,
 * picking the most egregious error we find.  In order of importance, we do the
 * following:
 *
 *	- Check for a complete and valid configuration
 *	- Look for any faulted or missing devices in a non-replicated config
 *	- Check for any data errors
 *	- Check for any faulted or missing devices in a replicated config
 *	- Look for any devices showing errors
 *	- Check for any resilvering devices
 *
 * There can obviously be multiple errors within a single pool, so this routine
 * only picks the most damaging of all the current errors to report.
 */
static zpool_status_t
check_status(nvlist_t *config, boolean_t isimport)
{
	nvlist_t *nvroot;
	vdev_stat_t *vs;
	pool_scan_stat_t *ps = NULL;
	uint_t vsc, psc;
	uint64_t nerr;
	uint64_t version;
	uint64_t stateval;
	uint64_t suspended;
	uint64_t hostid = 0;

	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &stateval) == 0);

	/*
	 * Currently resilvering a vdev
	 */
	(void) nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &psc);
	if (ps != NULL && ps->pss_func == POOL_SCAN_RESILVER &&
	    ps->pss_state == DSS_SCANNING)
		return (ZPOOL_STATUS_RESILVERING);

	/*
	 * Pool last accessed by another system.
	 */
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID, &hostid);
	if (hostid != 0 && (unsigned long)hostid != gethostid() &&
	    stateval == POOL_STATE_ACTIVE)
		return (ZPOOL_STATUS_HOSTID_MISMATCH);

	/*
	 * Newer on-disk version.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_VERSION_NEWER)
		return (ZPOOL_STATUS_VERSION_NEWER);

	/*
	 * Unsupported feature(s).
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_UNSUP_FEAT) {
		nvlist_t *nvinfo;

		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO,
		    &nvinfo) == 0);
		if (nvlist_exists(nvinfo, ZPOOL_CONFIG_CAN_RDONLY))
			return (ZPOOL_STATUS_UNSUP_FEAT_WRITE);
		return (ZPOOL_STATUS_UNSUP_FEAT_READ);
	}

	/*
	 * Check that the config is complete.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_BAD_GUID_SUM)
		return (ZPOOL_STATUS_BAD_GUID_SUM);

	/*
	 * Check whether the pool has suspended due to failed I/O.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_SUSPENDED,
	    &suspended) == 0) {
		if (suspended == ZIO_FAILURE_MODE_CONTINUE)
			return (ZPOOL_STATUS_IO_FAILURE_CONTINUE);
		return (ZPOOL_STATUS_IO_FAILURE_WAIT);
	}

	/*
	 * Could not read a log.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_BAD_LOG) {
		return (ZPOOL_STATUS_BAD_LOG);
	}

	/*
	 * Bad devices in non-replicated config.
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    find_vdev_problem(nvroot, vdev_faulted, B_TRUE))
		return (ZPOOL_STATUS_FAULTED_DEV_NR);

	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    find_vdev_problem(nvroot, vdev_missing, B_TRUE))
		return (ZPOOL_STATUS_MISSING_DEV_NR);

	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    find_vdev_problem(nvroot, vdev_broken, B_TRUE))
		return (ZPOOL_STATUS_CORRUPT_LABEL_NR);

	/*
	 * Corrupted pool metadata
	 */
	if (vs->vs_state == VDEV_STATE_CANT_OPEN &&
	    vs->vs_aux == VDEV_AUX_CORRUPT_DATA)
		return (ZPOOL_STATUS_CORRUPT_POOL);

	/*
	 * Persistent data errors.
	 */
	if (!isimport) {
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRCOUNT,
		    &nerr) == 0 && nerr != 0)
			return (ZPOOL_STATUS_CORRUPT_DATA);
	}

	/*
	 * Missing devices in a replicated config.
	 */
	if (find_vdev_problem(nvroot, vdev_faulted, B_TRUE))
		return (ZPOOL_STATUS_FAULTED_DEV_R);
	if (find_vdev_problem(nvroot, vdev_missing, B_TRUE))
		return (ZPOOL_STATUS_MISSING_DEV_R);
	if (find_vdev_problem(nvroot, vdev_broken, B_TRUE))
		return (ZPOOL_STATUS_CORRUPT_LABEL_R);

	/*
	 * Devices with errors
	 */
	if (!isimport && find_vdev_problem(nvroot, vdev_errors, B_TRUE))
		return (ZPOOL_STATUS_FAILING_DEV);

	/*
	 * Offlined devices
	 */
	if (find_vdev_problem(nvroot, vdev_offlined, B_TRUE))
		return (ZPOOL_STATUS_OFFLINE_DEV);

	/*
	 * Removed device
	 */
	if (find_vdev_problem(nvroot, vdev_removed, B_TRUE))
		return (ZPOOL_STATUS_REMOVED_DEV);

	/*
	 * Suboptimal, but usable, ashift configuration.
	 */
	if (find_vdev_problem(nvroot, vdev_non_native_ashift, B_FALSE))
		return (ZPOOL_STATUS_NON_NATIVE_ASHIFT);

	/*
	 * Outdated, but usable, version
	 */
	if (SPA_VERSION_IS_SUPPORTED(version) && version != SPA_VERSION)
		return (ZPOOL_STATUS_VERSION_OLDER);

	/*
	 * Usable pool with disabled features
	 */
	if (version >= SPA_VERSION_FEATURES) {
		int i;
		nvlist_t *feat;

		if (isimport) {
			feat = fnvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_LOAD_INFO);
			feat = fnvlist_lookup_nvlist(feat,
			    ZPOOL_CONFIG_ENABLED_FEAT);
		} else {
			feat = fnvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_FEATURE_STATS);
		}

		for (i = 0; i < SPA_FEATURES; i++) {
			zfeature_info_t *fi = &spa_feature_table[i];
			if (!nvlist_exists(feat, fi->fi_guid))
				return (ZPOOL_STATUS_FEAT_DISABLED);
		}
	}

	return (ZPOOL_STATUS_OK);
}

zpool_status_t
zpool_get_status(zpool_handle_t *zhp, char **msgid)
{
	zpool_status_t ret = check_status(zhp->zpool_config, B_FALSE);

	if (ret >= NMSGID)
		*msgid = NULL;
	else
		*msgid = zfs_msgid_table[ret];

	return (ret);
}

zpool_status_t
zpool_import_status(nvlist_t *config, char **msgid)
{
	zpool_status_t ret = check_status(config, B_TRUE);

	if (ret >= NMSGID)
		*msgid = NULL;
	else
		*msgid = zfs_msgid_table[ret];

	return (ret);
}

static void
dump_ddt_stat(const ddt_stat_t *dds, int h)
{
	char refcnt[6];
	char blocks[6], lsize[6], psize[6], dsize[6];
	char ref_blocks[6], ref_lsize[6], ref_psize[6], ref_dsize[6];

	if (dds == NULL || dds->dds_blocks == 0)
		return;

	if (h == -1)
		(void) strcpy(refcnt, "Total");
	else
		zfs_nicenum(1ULL << h, refcnt, sizeof (refcnt));

	zfs_nicenum(dds->dds_blocks, blocks, sizeof (blocks));
	zfs_nicenum(dds->dds_lsize, lsize, sizeof (lsize));
	zfs_nicenum(dds->dds_psize, psize, sizeof (psize));
	zfs_nicenum(dds->dds_dsize, dsize, sizeof (dsize));
	zfs_nicenum(dds->dds_ref_blocks, ref_blocks, sizeof (ref_blocks));
	zfs_nicenum(dds->dds_ref_lsize, ref_lsize, sizeof (ref_lsize));
	zfs_nicenum(dds->dds_ref_psize, ref_psize, sizeof (ref_psize));
	zfs_nicenum(dds->dds_ref_dsize, ref_dsize, sizeof (ref_dsize));

	(void) printf("%6s   %6s   %5s   %5s   %5s   %6s   %5s   %5s   %5s\n",
	    refcnt,
	    blocks, lsize, psize, dsize,
	    ref_blocks, ref_lsize, ref_psize, ref_dsize);
}

/*
 * Print the DDT histogram and the column totals.
 */
void
zpool_dump_ddt(const ddt_stat_t *dds_total, const ddt_histogram_t *ddh)
{
	int h;

	(void) printf("\n");

	(void) printf("bucket   "
	    "           allocated             "
	    "          referenced          \n");
	(void) printf("______   "
	    "______________________________   "
	    "______________________________\n");

	(void) printf("%6s   %6s   %5s   %5s   %5s   %6s   %5s   %5s   %5s\n",
	    "refcnt",
	    "blocks", "LSIZE", "PSIZE", "DSIZE",
	    "blocks", "LSIZE", "PSIZE", "DSIZE");

	(void) printf("%6s   %6s   %5s   %5s   %5s   %6s   %5s   %5s   %5s\n",
	    "------",
	    "------", "-----", "-----", "-----",
	    "------", "-----", "-----", "-----");

	for (h = 0; h < 64; h++)
		dump_ddt_stat(&ddh->ddh_stat[h], h);

	dump_ddt_stat(dds_total, -1);

	(void) printf("\n");
}
