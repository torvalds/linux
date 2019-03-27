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
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 */

/*
 * zhack is a debugging tool that can write changes to ZFS pool using libzpool
 * for testing purposes. Altering pools with zhack is unsupported and may
 * result in corrupted pools.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/zfs_znode.h>
#include <sys/dsl_synctask.h>
#include <sys/vdev.h>
#include <sys/fs/zfs.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/zfeature.h>
#include <sys/dmu_tx.h>
#undef verify
#include <libzfs.h>

extern boolean_t zfeature_checks_disable;

const char cmdname[] = "zhack";
libzfs_handle_t *g_zfs;
static importargs_t g_importargs;
static char *g_pool;
static boolean_t g_readonly;

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: %s [-c cachefile] [-d dir] <subcommand> <args> ...\n"
	    "where <subcommand> <args> is one of the following:\n"
	    "\n", cmdname);

	(void) fprintf(stderr,
	    "    feature stat <pool>\n"
	    "        print information about enabled features\n"
	    "    feature enable [-d desc] <pool> <feature>\n"
	    "        add a new enabled feature to the pool\n"
	    "        -d <desc> sets the feature's description\n"
	    "    feature ref [-md] <pool> <feature>\n"
	    "        change the refcount on the given feature\n"
	    "        -d decrease instead of increase the refcount\n"
	    "        -m add the feature to the label if increasing refcount\n"
	    "\n"
	    "    <feature> : should be a feature guid\n");
	exit(1);
}


static void
fatal(spa_t *spa, void *tag, const char *fmt, ...)
{
	va_list ap;

	if (spa != NULL) {
		spa_close(spa, tag);
		(void) spa_export(g_pool, NULL, B_TRUE, B_FALSE);
	}

	va_start(ap, fmt);
	(void) fprintf(stderr, "%s: ", cmdname);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void) fprintf(stderr, "\n");

	exit(1);
}

/* ARGSUSED */
static int
space_delta_cb(dmu_object_type_t bonustype, void *data,
    uint64_t *userp, uint64_t *groupp)
{
	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (ENOENT);
	(void) fprintf(stderr, "modifying object that needs user accounting");
	abort();
	/* NOTREACHED */
}

/*
 * Target is the dataset whose pool we want to open.
 */
static void
import_pool(const char *target, boolean_t readonly)
{
	nvlist_t *config;
	nvlist_t *pools;
	int error;
	char *sepp;
	spa_t *spa;
	nvpair_t *elem;
	nvlist_t *props;
	const char *name;

	kernel_init(readonly ? FREAD : (FREAD | FWRITE));
	g_zfs = libzfs_init();
	ASSERT(g_zfs != NULL);

	dmu_objset_register_type(DMU_OST_ZFS, space_delta_cb);

	g_readonly = readonly;

	/*
	 * If we only want readonly access, it's OK if we find
	 * a potentially-active (ie, imported into the kernel) pool from the
	 * default cachefile.
	 */
	if (readonly && spa_open(target, &spa, FTAG) == 0) {
		spa_close(spa, FTAG);
		return;
	}

	g_importargs.unique = B_TRUE;
	g_importargs.can_be_active = readonly;
	g_pool = strdup(target);
	if ((sepp = strpbrk(g_pool, "/@")) != NULL)
		*sepp = '\0';
	g_importargs.poolname = g_pool;
	pools = zpool_search_import(g_zfs, &g_importargs);

	if (nvlist_empty(pools)) {
		if (!g_importargs.can_be_active) {
			g_importargs.can_be_active = B_TRUE;
			if (zpool_search_import(g_zfs, &g_importargs) != NULL ||
			    spa_open(target, &spa, FTAG) == 0) {
				fatal(spa, FTAG, "cannot import '%s': pool is "
				    "active; run " "\"zpool export %s\" "
				    "first\n", g_pool, g_pool);
			}
		}

		fatal(NULL, FTAG, "cannot import '%s': no such pool "
		    "available\n", g_pool);
	}

	elem = nvlist_next_nvpair(pools, NULL);
	name = nvpair_name(elem);
	verify(nvpair_value_nvlist(elem, &config) == 0);

	props = NULL;
	if (readonly) {
		verify(nvlist_alloc(&props, NV_UNIQUE_NAME, 0) == 0);
		verify(nvlist_add_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_READONLY), 1) == 0);
	}

	zfeature_checks_disable = B_TRUE;
	error = spa_import(name, config, props, ZFS_IMPORT_NORMAL);
	zfeature_checks_disable = B_FALSE;
	if (error == EEXIST)
		error = 0;

	if (error)
		fatal(NULL, FTAG, "can't import '%s': %s", name,
		    strerror(error));
}

static void
zhack_spa_open(const char *target, boolean_t readonly, void *tag, spa_t **spa)
{
	int err;

	import_pool(target, readonly);

	zfeature_checks_disable = B_TRUE;
	err = spa_open(target, spa, tag);
	zfeature_checks_disable = B_FALSE;

	if (err != 0)
		fatal(*spa, FTAG, "cannot open '%s': %s", target,
		    strerror(err));
	if (spa_version(*spa) < SPA_VERSION_FEATURES) {
		fatal(*spa, FTAG, "'%s' has version %d, features not enabled",
		    target, (int)spa_version(*spa));
	}
}

static void
dump_obj(objset_t *os, uint64_t obj, const char *name)
{
	zap_cursor_t zc;
	zap_attribute_t za;

	(void) printf("%s_obj:\n", name);

	for (zap_cursor_init(&zc, os, obj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		if (za.za_integer_length == 8) {
			ASSERT(za.za_num_integers == 1);
			(void) printf("\t%s = %llu\n",
			    za.za_name, (u_longlong_t)za.za_first_integer);
		} else {
			ASSERT(za.za_integer_length == 1);
			char val[1024];
			VERIFY(zap_lookup(os, obj, za.za_name,
			    1, sizeof (val), val) == 0);
			(void) printf("\t%s = %s\n", za.za_name, val);
		}
	}
	zap_cursor_fini(&zc);
}

static void
dump_mos(spa_t *spa)
{
	nvlist_t *nv = spa->spa_label_features;

	(void) printf("label config:\n");
	for (nvpair_t *pair = nvlist_next_nvpair(nv, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(nv, pair)) {
		(void) printf("\t%s\n", nvpair_name(pair));
	}
}

static void
zhack_do_feature_stat(int argc, char **argv)
{
	spa_t *spa;
	objset_t *os;
	char *target;

	argc--;
	argv++;

	if (argc < 1) {
		(void) fprintf(stderr, "error: missing pool name\n");
		usage();
	}
	target = argv[0];

	zhack_spa_open(target, B_TRUE, FTAG, &spa);
	os = spa->spa_meta_objset;

	dump_obj(os, spa->spa_feat_for_read_obj, "for_read");
	dump_obj(os, spa->spa_feat_for_write_obj, "for_write");
	dump_obj(os, spa->spa_feat_desc_obj, "descriptions");
	if (spa_feature_is_active(spa, SPA_FEATURE_ENABLED_TXG)) {
		dump_obj(os, spa->spa_feat_enabled_txg_obj, "enabled_txg");
	}
	dump_mos(spa);

	spa_close(spa, FTAG);
}

static void
zhack_feature_enable_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	zfeature_info_t *feature = arg;

	feature_enable_sync(spa, feature, tx);

	spa_history_log_internal(spa, "zhack enable feature", tx,
	    "guid=%s flags=%x",
	    feature->fi_guid, feature->fi_flags);
}

static void
zhack_do_feature_enable(int argc, char **argv)
{
	char c;
	char *desc, *target;
	spa_t *spa;
	objset_t *mos;
	zfeature_info_t feature;
	spa_feature_t nodeps[] = { SPA_FEATURE_NONE };

	/*
	 * Features are not added to the pool's label until their refcounts
	 * are incremented, so fi_mos can just be left as false for now.
	 */
	desc = NULL;
	feature.fi_uname = "zhack";
	feature.fi_flags = 0;
	feature.fi_depends = nodeps;
	feature.fi_feature = SPA_FEATURE_NONE;

	optind = 1;
	while ((c = getopt(argc, argv, "rmd:")) != -1) {
		switch (c) {
		case 'r':
			feature.fi_flags |= ZFEATURE_FLAG_READONLY_COMPAT;
			break;
		case 'd':
			desc = strdup(optarg);
			break;
		default:
			usage();
			break;
		}
	}

	if (desc == NULL)
		desc = strdup("zhack injected");
	feature.fi_desc = desc;

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		(void) fprintf(stderr, "error: missing feature or pool name\n");
		usage();
	}
	target = argv[0];
	feature.fi_guid = argv[1];

	if (!zfeature_is_valid_guid(feature.fi_guid))
		fatal(NULL, FTAG, "invalid feature guid: %s", feature.fi_guid);

	zhack_spa_open(target, B_FALSE, FTAG, &spa);
	mos = spa->spa_meta_objset;

	if (zfeature_is_supported(feature.fi_guid))
		fatal(spa, FTAG, "'%s' is a real feature, will not enable");
	if (0 == zap_contains(mos, spa->spa_feat_desc_obj, feature.fi_guid))
		fatal(spa, FTAG, "feature already enabled: %s",
		    feature.fi_guid);

	VERIFY0(dsl_sync_task(spa_name(spa), NULL,
	    zhack_feature_enable_sync, &feature, 5, ZFS_SPACE_CHECK_NORMAL));

	spa_close(spa, FTAG);

	free(desc);
}

static void
feature_incr_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	zfeature_info_t *feature = arg;
	uint64_t refcount;

	VERIFY0(feature_get_refcount_from_disk(spa, feature, &refcount));
	feature_sync(spa, feature, refcount + 1, tx);
	spa_history_log_internal(spa, "zhack feature incr", tx,
	    "name=%s", feature->fi_guid);
}

static void
feature_decr_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	zfeature_info_t *feature = arg;
	uint64_t refcount;

	VERIFY0(feature_get_refcount_from_disk(spa, feature, &refcount));
	feature_sync(spa, feature, refcount - 1, tx);
	spa_history_log_internal(spa, "zhack feature decr", tx,
	    "name=%s", feature->fi_guid);
}

static void
zhack_do_feature_ref(int argc, char **argv)
{
	char c;
	char *target;
	boolean_t decr = B_FALSE;
	spa_t *spa;
	objset_t *mos;
	zfeature_info_t feature;
	spa_feature_t nodeps[] = { SPA_FEATURE_NONE };

	/*
	 * fi_desc does not matter here because it was written to disk
	 * when the feature was enabled, but we need to properly set the
	 * feature for read or write based on the information we read off
	 * disk later.
	 */
	feature.fi_uname = "zhack";
	feature.fi_flags = 0;
	feature.fi_desc = NULL;
	feature.fi_depends = nodeps;
	feature.fi_feature = SPA_FEATURE_NONE;

	optind = 1;
	while ((c = getopt(argc, argv, "md")) != -1) {
		switch (c) {
		case 'm':
			feature.fi_flags |= ZFEATURE_FLAG_MOS;
			break;
		case 'd':
			decr = B_TRUE;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2) {
		(void) fprintf(stderr, "error: missing feature or pool name\n");
		usage();
	}
	target = argv[0];
	feature.fi_guid = argv[1];

	if (!zfeature_is_valid_guid(feature.fi_guid))
		fatal(NULL, FTAG, "invalid feature guid: %s", feature.fi_guid);

	zhack_spa_open(target, B_FALSE, FTAG, &spa);
	mos = spa->spa_meta_objset;

	if (zfeature_is_supported(feature.fi_guid)) {
		fatal(spa, FTAG,
		    "'%s' is a real feature, will not change refcount");
	}

	if (0 == zap_contains(mos, spa->spa_feat_for_read_obj,
	    feature.fi_guid)) {
		feature.fi_flags &= ~ZFEATURE_FLAG_READONLY_COMPAT;
	} else if (0 == zap_contains(mos, spa->spa_feat_for_write_obj,
	    feature.fi_guid)) {
		feature.fi_flags |= ZFEATURE_FLAG_READONLY_COMPAT;
	} else {
		fatal(spa, FTAG, "feature is not enabled: %s", feature.fi_guid);
	}

	if (decr) {
		uint64_t count;
		if (feature_get_refcount_from_disk(spa, &feature,
		    &count) == 0 && count != 0) {
			fatal(spa, FTAG, "feature refcount already 0: %s",
			    feature.fi_guid);
		}
	}

	VERIFY0(dsl_sync_task(spa_name(spa), NULL,
	    decr ? feature_decr_sync : feature_incr_sync, &feature,
	    5, ZFS_SPACE_CHECK_NORMAL));

	spa_close(spa, FTAG);
}

static int
zhack_do_feature(int argc, char **argv)
{
	char *subcommand;

	argc--;
	argv++;
	if (argc == 0) {
		(void) fprintf(stderr,
		    "error: no feature operation specified\n");
		usage();
	}

	subcommand = argv[0];
	if (strcmp(subcommand, "stat") == 0) {
		zhack_do_feature_stat(argc, argv);
	} else if (strcmp(subcommand, "enable") == 0) {
		zhack_do_feature_enable(argc, argv);
	} else if (strcmp(subcommand, "ref") == 0) {
		zhack_do_feature_ref(argc, argv);
	} else {
		(void) fprintf(stderr, "error: unknown subcommand: %s\n",
		    subcommand);
		usage();
	}

	return (0);
}

#define	MAX_NUM_PATHS 1024

int
main(int argc, char **argv)
{
	extern void zfs_prop_init(void);

	char *path[MAX_NUM_PATHS];
	const char *subcommand;
	int rv = 0;
	char c;

	g_importargs.path = path;

	dprintf_setup(&argc, argv);
	zfs_prop_init();

	while ((c = getopt(argc, argv, "c:d:")) != -1) {
		switch (c) {
		case 'c':
			g_importargs.cachefile = optarg;
			break;
		case 'd':
			assert(g_importargs.paths < MAX_NUM_PATHS);
			g_importargs.path[g_importargs.paths++] = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	argc -= optind;
	argv += optind;
	optind = 1;

	if (argc == 0) {
		(void) fprintf(stderr, "error: no command specified\n");
		usage();
	}

	subcommand = argv[0];

	if (strcmp(subcommand, "feature") == 0) {
		rv = zhack_do_feature(argc, argv);
	} else {
		(void) fprintf(stderr, "error: unknown subcommand: %s\n",
		    subcommand);
		usage();
	}

	if (!g_readonly && spa_export(g_pool, NULL, B_TRUE, B_FALSE) != 0) {
		fatal(NULL, FTAG, "pool export failed; "
		    "changes may not be committed to disk\n");
	}

	libzfs_fini(g_zfs);
	kernel_fini();

	return (rv);
}
