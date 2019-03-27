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
 * Copyright (c) 2011, 2017 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012 Martin Matuska <mm@FreeBSD.org>.  All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Joyent, Inc.
 * Copyright 2017 RackTop Systems.
 */

/*
 * The objective of this program is to provide a DMU/ZAP/SPA stress test
 * that runs entirely in userland, is easy to use, and easy to extend.
 *
 * The overall design of the ztest program is as follows:
 *
 * (1) For each major functional area (e.g. adding vdevs to a pool,
 *     creating and destroying datasets, reading and writing objects, etc)
 *     we have a simple routine to test that functionality.  These
 *     individual routines do not have to do anything "stressful".
 *
 * (2) We turn these simple functionality tests into a stress test by
 *     running them all in parallel, with as many threads as desired,
 *     and spread across as many datasets, objects, and vdevs as desired.
 *
 * (3) While all this is happening, we inject faults into the pool to
 *     verify that self-healing data really works.
 *
 * (4) Every time we open a dataset, we change its checksum and compression
 *     functions.  Thus even individual objects vary from block to block
 *     in which checksum they use and whether they're compressed.
 *
 * (5) To verify that we never lose on-disk consistency after a crash,
 *     we run the entire test in a child of the main process.
 *     At random times, the child self-immolates with a SIGKILL.
 *     This is the software equivalent of pulling the power cord.
 *     The parent then runs the test again, using the existing
 *     storage pool, as many times as desired. If backwards compatibility
 *     testing is enabled ztest will sometimes run the "older" version
 *     of ztest after a SIGKILL.
 *
 * (6) To verify that we don't have future leaks or temporal incursions,
 *     many of the functional tests record the transaction group number
 *     as part of their data.  When reading old data, they verify that
 *     the transaction group number is less than the current, open txg.
 *     If you add a new test, please do this if applicable.
 *
 * When run with no arguments, ztest runs for about five minutes and
 * produces no output if successful.  To get a little bit of information,
 * specify -V.  To get more information, specify -VV, and so on.
 *
 * To turn this into an overnight stress test, use -T to specify run time.
 *
 * You can ask more more vdevs [-v], datasets [-d], or threads [-t]
 * to increase the pool capacity, fanout, and overall stress level.
 *
 * Use the -k option to set the desired frequency of kills.
 *
 * When ztest invokes itself it passes all relevant information through a
 * temporary file which is mmap-ed in the child process. This allows shared
 * memory to survive the exec syscall. The ztest_shared_hdr_t struct is always
 * stored at offset 0 of this file and contains information on the size and
 * number of shared structures in the file. The information stored in this file
 * must remain backwards compatible with older versions of ztest so that
 * ztest can invoke them during backwards compatibility testing (-B).
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/vdev_initialize.h>
#include <sys/spa_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_scan.h>
#include <sys/zio_checksum.h>
#include <sys/refcount.h>
#include <sys/zfeature.h>
#include <sys/dsl_userhold.h>
#include <sys/abd.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <umem.h>
#include <dlfcn.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <sys/fs/zfs.h>
#include <libnvpair.h>
#include <libcmdutils.h>

static int ztest_fd_data = -1;
static int ztest_fd_rand = -1;

typedef struct ztest_shared_hdr {
	uint64_t	zh_hdr_size;
	uint64_t	zh_opts_size;
	uint64_t	zh_size;
	uint64_t	zh_stats_size;
	uint64_t	zh_stats_count;
	uint64_t	zh_ds_size;
	uint64_t	zh_ds_count;
} ztest_shared_hdr_t;

static ztest_shared_hdr_t *ztest_shared_hdr;

typedef struct ztest_shared_opts {
	char zo_pool[ZFS_MAX_DATASET_NAME_LEN];
	char zo_dir[ZFS_MAX_DATASET_NAME_LEN];
	char zo_alt_ztest[MAXNAMELEN];
	char zo_alt_libpath[MAXNAMELEN];
	uint64_t zo_vdevs;
	uint64_t zo_vdevtime;
	size_t zo_vdev_size;
	int zo_ashift;
	int zo_mirrors;
	int zo_raidz;
	int zo_raidz_parity;
	int zo_datasets;
	int zo_threads;
	uint64_t zo_passtime;
	uint64_t zo_killrate;
	int zo_verbose;
	int zo_init;
	uint64_t zo_time;
	uint64_t zo_maxloops;
	uint64_t zo_metaslab_force_ganging;
} ztest_shared_opts_t;

static const ztest_shared_opts_t ztest_opts_defaults = {
	.zo_pool = { 'z', 't', 'e', 's', 't', '\0' },
	.zo_dir = { '/', 't', 'm', 'p', '\0' },
	.zo_alt_ztest = { '\0' },
	.zo_alt_libpath = { '\0' },
	.zo_vdevs = 5,
	.zo_ashift = SPA_MINBLOCKSHIFT,
	.zo_mirrors = 2,
	.zo_raidz = 4,
	.zo_raidz_parity = 1,
	.zo_vdev_size = SPA_MINDEVSIZE * 4,	/* 256m default size */
	.zo_datasets = 7,
	.zo_threads = 23,
	.zo_passtime = 60,		/* 60 seconds */
	.zo_killrate = 70,		/* 70% kill rate */
	.zo_verbose = 0,
	.zo_init = 1,
	.zo_time = 300,			/* 5 minutes */
	.zo_maxloops = 50,		/* max loops during spa_freeze() */
	.zo_metaslab_force_ganging = 32 << 10
};

extern uint64_t metaslab_force_ganging;
extern uint64_t metaslab_df_alloc_threshold;
extern uint64_t zfs_deadman_synctime_ms;
extern int metaslab_preload_limit;
extern boolean_t zfs_compressed_arc_enabled;
extern boolean_t zfs_abd_scatter_enabled;
extern boolean_t zfs_force_some_double_word_sm_entries;

static ztest_shared_opts_t *ztest_shared_opts;
static ztest_shared_opts_t ztest_opts;

typedef struct ztest_shared_ds {
	uint64_t	zd_seq;
} ztest_shared_ds_t;

static ztest_shared_ds_t *ztest_shared_ds;
#define	ZTEST_GET_SHARED_DS(d) (&ztest_shared_ds[d])

#define	BT_MAGIC	0x123456789abcdefULL
#define	MAXFAULTS() \
	(MAX(zs->zs_mirrors, 1) * (ztest_opts.zo_raidz_parity + 1) - 1)

enum ztest_io_type {
	ZTEST_IO_WRITE_TAG,
	ZTEST_IO_WRITE_PATTERN,
	ZTEST_IO_WRITE_ZEROES,
	ZTEST_IO_TRUNCATE,
	ZTEST_IO_SETATTR,
	ZTEST_IO_REWRITE,
	ZTEST_IO_TYPES
};

typedef struct ztest_block_tag {
	uint64_t	bt_magic;
	uint64_t	bt_objset;
	uint64_t	bt_object;
	uint64_t	bt_dnodesize;
	uint64_t	bt_offset;
	uint64_t	bt_gen;
	uint64_t	bt_txg;
	uint64_t	bt_crtxg;
} ztest_block_tag_t;

typedef struct bufwad {
	uint64_t	bw_index;
	uint64_t	bw_txg;
	uint64_t	bw_data;
} bufwad_t;

/*
 * XXX -- fix zfs range locks to be generic so we can use them here.
 */
typedef enum {
	RL_READER,
	RL_WRITER,
	RL_APPEND
} rl_type_t;

typedef struct rll {
	void		*rll_writer;
	int		rll_readers;
	kmutex_t	rll_lock;
	kcondvar_t	rll_cv;
} rll_t;

typedef struct rl {
	uint64_t	rl_object;
	uint64_t	rl_offset;
	uint64_t	rl_size;
	rll_t		*rl_lock;
} rl_t;

#define	ZTEST_RANGE_LOCKS	64
#define	ZTEST_OBJECT_LOCKS	64

/*
 * Object descriptor.  Used as a template for object lookup/create/remove.
 */
typedef struct ztest_od {
	uint64_t	od_dir;
	uint64_t	od_object;
	dmu_object_type_t od_type;
	dmu_object_type_t od_crtype;
	uint64_t	od_blocksize;
	uint64_t	od_crblocksize;
	uint64_t	od_crdnodesize;
	uint64_t	od_gen;
	uint64_t	od_crgen;
	char		od_name[ZFS_MAX_DATASET_NAME_LEN];
} ztest_od_t;

/*
 * Per-dataset state.
 */
typedef struct ztest_ds {
	ztest_shared_ds_t *zd_shared;
	objset_t	*zd_os;
	krwlock_t	zd_zilog_lock;
	zilog_t		*zd_zilog;
	ztest_od_t	*zd_od;		/* debugging aid */
	char		zd_name[ZFS_MAX_DATASET_NAME_LEN];
	kmutex_t	zd_dirobj_lock;
	rll_t		zd_object_lock[ZTEST_OBJECT_LOCKS];
	rll_t		zd_range_lock[ZTEST_RANGE_LOCKS];
} ztest_ds_t;

/*
 * Per-iteration state.
 */
typedef void ztest_func_t(ztest_ds_t *zd, uint64_t id);

typedef struct ztest_info {
	ztest_func_t	*zi_func;	/* test function */
	uint64_t	zi_iters;	/* iterations per execution */
	uint64_t	*zi_interval;	/* execute every <interval> seconds */
} ztest_info_t;

typedef struct ztest_shared_callstate {
	uint64_t	zc_count;	/* per-pass count */
	uint64_t	zc_time;	/* per-pass time */
	uint64_t	zc_next;	/* next time to call this function */
} ztest_shared_callstate_t;

static ztest_shared_callstate_t *ztest_shared_callstate;
#define	ZTEST_GET_SHARED_CALLSTATE(c) (&ztest_shared_callstate[c])

/*
 * Note: these aren't static because we want dladdr() to work.
 */
ztest_func_t ztest_dmu_read_write;
ztest_func_t ztest_dmu_write_parallel;
ztest_func_t ztest_dmu_object_alloc_free;
ztest_func_t ztest_dmu_commit_callbacks;
ztest_func_t ztest_zap;
ztest_func_t ztest_zap_parallel;
ztest_func_t ztest_zil_commit;
ztest_func_t ztest_zil_remount;
ztest_func_t ztest_dmu_read_write_zcopy;
ztest_func_t ztest_dmu_objset_create_destroy;
ztest_func_t ztest_dmu_prealloc;
ztest_func_t ztest_fzap;
ztest_func_t ztest_dmu_snapshot_create_destroy;
ztest_func_t ztest_dsl_prop_get_set;
ztest_func_t ztest_spa_prop_get_set;
ztest_func_t ztest_spa_create_destroy;
ztest_func_t ztest_fault_inject;
ztest_func_t ztest_ddt_repair;
ztest_func_t ztest_dmu_snapshot_hold;
ztest_func_t ztest_spa_rename;
ztest_func_t ztest_scrub;
ztest_func_t ztest_dsl_dataset_promote_busy;
ztest_func_t ztest_vdev_attach_detach;
ztest_func_t ztest_vdev_LUN_growth;
ztest_func_t ztest_vdev_add_remove;
ztest_func_t ztest_vdev_aux_add_remove;
ztest_func_t ztest_split_pool;
ztest_func_t ztest_reguid;
ztest_func_t ztest_spa_upgrade;
ztest_func_t ztest_device_removal;
ztest_func_t ztest_remap_blocks;
ztest_func_t ztest_spa_checkpoint_create_discard;
ztest_func_t ztest_initialize;
ztest_func_t ztest_verify_dnode_bt;

uint64_t zopt_always = 0ULL * NANOSEC;		/* all the time */
uint64_t zopt_incessant = 1ULL * NANOSEC / 10;	/* every 1/10 second */
uint64_t zopt_often = 1ULL * NANOSEC;		/* every second */
uint64_t zopt_sometimes = 10ULL * NANOSEC;	/* every 10 seconds */
uint64_t zopt_rarely = 60ULL * NANOSEC;		/* every 60 seconds */

ztest_info_t ztest_info[] = {
	{ ztest_dmu_read_write,			1,	&zopt_always	},
	{ ztest_dmu_write_parallel,		10,	&zopt_always	},
	{ ztest_dmu_object_alloc_free,		1,	&zopt_always	},
	{ ztest_dmu_commit_callbacks,		1,	&zopt_always	},
	{ ztest_zap,				30,	&zopt_always	},
	{ ztest_zap_parallel,			100,	&zopt_always	},
	{ ztest_split_pool,			1,	&zopt_always	},
	{ ztest_zil_commit,			1,	&zopt_incessant	},
	{ ztest_zil_remount,			1,	&zopt_sometimes	},
	{ ztest_dmu_read_write_zcopy,		1,	&zopt_often	},
	{ ztest_dmu_objset_create_destroy,	1,	&zopt_often	},
	{ ztest_dsl_prop_get_set,		1,	&zopt_often	},
	{ ztest_spa_prop_get_set,		1,	&zopt_sometimes	},
#if 0
	{ ztest_dmu_prealloc,			1,	&zopt_sometimes	},
#endif
	{ ztest_fzap,				1,	&zopt_sometimes	},
	{ ztest_dmu_snapshot_create_destroy,	1,	&zopt_sometimes	},
	{ ztest_spa_create_destroy,		1,	&zopt_sometimes	},
	{ ztest_fault_inject,			1,	&zopt_incessant	},
	{ ztest_ddt_repair,			1,	&zopt_sometimes	},
	{ ztest_dmu_snapshot_hold,		1,	&zopt_sometimes	},
	{ ztest_reguid,				1,	&zopt_rarely	},
	{ ztest_spa_rename,			1,	&zopt_rarely	},
	{ ztest_scrub,				1,	&zopt_often	},
	{ ztest_spa_upgrade,			1,	&zopt_rarely	},
	{ ztest_dsl_dataset_promote_busy,	1,	&zopt_rarely	},
	{ ztest_vdev_attach_detach,		1,	&zopt_incessant	},
	{ ztest_vdev_LUN_growth,		1,	&zopt_rarely	},
	{ ztest_vdev_add_remove,		1,
	    &ztest_opts.zo_vdevtime				},
	{ ztest_vdev_aux_add_remove,		1,
	    &ztest_opts.zo_vdevtime				},
	{ ztest_device_removal,			1,	&zopt_sometimes	},
	{ ztest_remap_blocks,			1,	&zopt_sometimes },
	{ ztest_spa_checkpoint_create_discard,	1,	&zopt_rarely	},
	{ ztest_initialize,			1,	&zopt_sometimes },
	{ ztest_verify_dnode_bt,		1,	&zopt_sometimes }
};

#define	ZTEST_FUNCS	(sizeof (ztest_info) / sizeof (ztest_info_t))

/*
 * The following struct is used to hold a list of uncalled commit callbacks.
 * The callbacks are ordered by txg number.
 */
typedef struct ztest_cb_list {
	kmutex_t zcl_callbacks_lock;
	list_t	zcl_callbacks;
} ztest_cb_list_t;

/*
 * Stuff we need to share writably between parent and child.
 */
typedef struct ztest_shared {
	boolean_t	zs_do_init;
	hrtime_t	zs_proc_start;
	hrtime_t	zs_proc_stop;
	hrtime_t	zs_thread_start;
	hrtime_t	zs_thread_stop;
	hrtime_t	zs_thread_kill;
	uint64_t	zs_enospc_count;
	uint64_t	zs_vdev_next_leaf;
	uint64_t	zs_vdev_aux;
	uint64_t	zs_alloc;
	uint64_t	zs_space;
	uint64_t	zs_splits;
	uint64_t	zs_mirrors;
	uint64_t	zs_metaslab_sz;
	uint64_t	zs_metaslab_df_alloc_threshold;
	uint64_t	zs_guid;
} ztest_shared_t;

#define	ID_PARALLEL	-1ULL

static char ztest_dev_template[] = "%s/%s.%llua";
static char ztest_aux_template[] = "%s/%s.%s.%llu";
ztest_shared_t *ztest_shared;

static spa_t *ztest_spa = NULL;
static ztest_ds_t *ztest_ds;

static kmutex_t ztest_vdev_lock;
static boolean_t ztest_device_removal_active = B_FALSE;
static kmutex_t ztest_checkpoint_lock;

/*
 * The ztest_name_lock protects the pool and dataset namespace used by
 * the individual tests. To modify the namespace, consumers must grab
 * this lock as writer. Grabbing the lock as reader will ensure that the
 * namespace does not change while the lock is held.
 */
static krwlock_t ztest_name_lock;

static boolean_t ztest_dump_core = B_TRUE;
static boolean_t ztest_exiting;

/* Global commit callback list */
static ztest_cb_list_t zcl;

enum ztest_object {
	ZTEST_META_DNODE = 0,
	ZTEST_DIROBJ,
	ZTEST_OBJECTS
};

static void usage(boolean_t) __NORETURN;

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */
const char *
_umem_debug_init()
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}

#define	FATAL_MSG_SZ	1024

char *fatal_msg;

static void
fatal(int do_perror, char *message, ...)
{
	va_list args;
	int save_errno = errno;
	char buf[FATAL_MSG_SZ];

	(void) fflush(stdout);

	va_start(args, message);
	(void) sprintf(buf, "ztest: ");
	/* LINTED */
	(void) vsprintf(buf + strlen(buf), message, args);
	va_end(args);
	if (do_perror) {
		(void) snprintf(buf + strlen(buf), FATAL_MSG_SZ - strlen(buf),
		    ": %s", strerror(save_errno));
	}
	(void) fprintf(stderr, "%s\n", buf);
	fatal_msg = buf;			/* to ease debugging */
	if (ztest_dump_core)
		abort();
	exit(3);
}

static int
str2shift(const char *buf)
{
	const char *ends = "BKMGTPEZ";
	int i;

	if (buf[0] == '\0')
		return (0);
	for (i = 0; i < strlen(ends); i++) {
		if (toupper(buf[0]) == ends[i])
			break;
	}
	if (i == strlen(ends)) {
		(void) fprintf(stderr, "ztest: invalid bytes suffix: %s\n",
		    buf);
		usage(B_FALSE);
	}
	if (buf[1] == '\0' || (toupper(buf[1]) == 'B' && buf[2] == '\0')) {
		return (10*i);
	}
	(void) fprintf(stderr, "ztest: invalid bytes suffix: %s\n", buf);
	usage(B_FALSE);
	/* NOTREACHED */
}

static uint64_t
nicenumtoull(const char *buf)
{
	char *end;
	uint64_t val;

	val = strtoull(buf, &end, 0);
	if (end == buf) {
		(void) fprintf(stderr, "ztest: bad numeric value: %s\n", buf);
		usage(B_FALSE);
	} else if (end[0] == '.') {
		double fval = strtod(buf, &end);
		fval *= pow(2, str2shift(end));
		if (fval > UINT64_MAX) {
			(void) fprintf(stderr, "ztest: value too large: %s\n",
			    buf);
			usage(B_FALSE);
		}
		val = (uint64_t)fval;
	} else {
		int shift = str2shift(end);
		if (shift >= 64 || (val << shift) >> shift != val) {
			(void) fprintf(stderr, "ztest: value too large: %s\n",
			    buf);
			usage(B_FALSE);
		}
		val <<= shift;
	}
	return (val);
}

static void
usage(boolean_t requested)
{
	const ztest_shared_opts_t *zo = &ztest_opts_defaults;

	char nice_vdev_size[NN_NUMBUF_SZ];
	char nice_force_ganging[NN_NUMBUF_SZ];
	FILE *fp = requested ? stdout : stderr;

	nicenum(zo->zo_vdev_size, nice_vdev_size, sizeof (nice_vdev_size));
	nicenum(zo->zo_metaslab_force_ganging, nice_force_ganging,
	    sizeof (nice_force_ganging));

	(void) fprintf(fp, "Usage: %s\n"
	    "\t[-v vdevs (default: %llu)]\n"
	    "\t[-s size_of_each_vdev (default: %s)]\n"
	    "\t[-a alignment_shift (default: %d)] use 0 for random\n"
	    "\t[-m mirror_copies (default: %d)]\n"
	    "\t[-r raidz_disks (default: %d)]\n"
	    "\t[-R raidz_parity (default: %d)]\n"
	    "\t[-d datasets (default: %d)]\n"
	    "\t[-t threads (default: %d)]\n"
	    "\t[-g gang_block_threshold (default: %s)]\n"
	    "\t[-i init_count (default: %d)] initialize pool i times\n"
	    "\t[-k kill_percentage (default: %llu%%)]\n"
	    "\t[-p pool_name (default: %s)]\n"
	    "\t[-f dir (default: %s)] file directory for vdev files\n"
	    "\t[-V] verbose (use multiple times for ever more blather)\n"
	    "\t[-E] use existing pool instead of creating new one\n"
	    "\t[-T time (default: %llu sec)] total run time\n"
	    "\t[-F freezeloops (default: %llu)] max loops in spa_freeze()\n"
	    "\t[-P passtime (default: %llu sec)] time per pass\n"
	    "\t[-B alt_ztest (default: <none>)] alternate ztest path\n"
	    "\t[-o variable=value] ... set global variable to an unsigned\n"
	    "\t    32-bit integer value\n"
	    "\t[-h] (print help)\n"
	    "",
	    zo->zo_pool,
	    (u_longlong_t)zo->zo_vdevs,			/* -v */
	    nice_vdev_size,				/* -s */
	    zo->zo_ashift,				/* -a */
	    zo->zo_mirrors,				/* -m */
	    zo->zo_raidz,				/* -r */
	    zo->zo_raidz_parity,			/* -R */
	    zo->zo_datasets,				/* -d */
	    zo->zo_threads,				/* -t */
	    nice_force_ganging,				/* -g */
	    zo->zo_init,				/* -i */
	    (u_longlong_t)zo->zo_killrate,		/* -k */
	    zo->zo_pool,				/* -p */
	    zo->zo_dir,					/* -f */
	    (u_longlong_t)zo->zo_time,			/* -T */
	    (u_longlong_t)zo->zo_maxloops,		/* -F */
	    (u_longlong_t)zo->zo_passtime);
	exit(requested ? 0 : 1);
}

static void
process_options(int argc, char **argv)
{
	char *path;
	ztest_shared_opts_t *zo = &ztest_opts;

	int opt;
	uint64_t value;
	char altdir[MAXNAMELEN] = { 0 };

	bcopy(&ztest_opts_defaults, zo, sizeof (*zo));

	while ((opt = getopt(argc, argv,
	    "v:s:a:m:r:R:d:t:g:i:k:p:f:VET:P:hF:B:o:")) != EOF) {
		value = 0;
		switch (opt) {
		case 'v':
		case 's':
		case 'a':
		case 'm':
		case 'r':
		case 'R':
		case 'd':
		case 't':
		case 'g':
		case 'i':
		case 'k':
		case 'T':
		case 'P':
		case 'F':
			value = nicenumtoull(optarg);
		}
		switch (opt) {
		case 'v':
			zo->zo_vdevs = value;
			break;
		case 's':
			zo->zo_vdev_size = MAX(SPA_MINDEVSIZE, value);
			break;
		case 'a':
			zo->zo_ashift = value;
			break;
		case 'm':
			zo->zo_mirrors = value;
			break;
		case 'r':
			zo->zo_raidz = MAX(1, value);
			break;
		case 'R':
			zo->zo_raidz_parity = MIN(MAX(value, 1), 3);
			break;
		case 'd':
			zo->zo_datasets = MAX(1, value);
			break;
		case 't':
			zo->zo_threads = MAX(1, value);
			break;
		case 'g':
			zo->zo_metaslab_force_ganging =
			    MAX(SPA_MINBLOCKSIZE << 1, value);
			break;
		case 'i':
			zo->zo_init = value;
			break;
		case 'k':
			zo->zo_killrate = value;
			break;
		case 'p':
			(void) strlcpy(zo->zo_pool, optarg,
			    sizeof (zo->zo_pool));
			break;
		case 'f':
			path = realpath(optarg, NULL);
			if (path == NULL) {
				(void) fprintf(stderr, "error: %s: %s\n",
				    optarg, strerror(errno));
				usage(B_FALSE);
			} else {
				(void) strlcpy(zo->zo_dir, path,
				    sizeof (zo->zo_dir));
			}
			break;
		case 'V':
			zo->zo_verbose++;
			break;
		case 'E':
			zo->zo_init = 0;
			break;
		case 'T':
			zo->zo_time = value;
			break;
		case 'P':
			zo->zo_passtime = MAX(1, value);
			break;
		case 'F':
			zo->zo_maxloops = MAX(1, value);
			break;
		case 'B':
			(void) strlcpy(altdir, optarg, sizeof (altdir));
			break;
		case 'o':
			if (set_global_var(optarg) != 0)
				usage(B_FALSE);
			break;
		case 'h':
			usage(B_TRUE);
			break;
		case '?':
		default:
			usage(B_FALSE);
			break;
		}
	}

	zo->zo_raidz_parity = MIN(zo->zo_raidz_parity, zo->zo_raidz - 1);

	zo->zo_vdevtime =
	    (zo->zo_vdevs > 0 ? zo->zo_time * NANOSEC / zo->zo_vdevs :
	    UINT64_MAX >> 2);

	if (strlen(altdir) > 0) {
		char *cmd;
		char *realaltdir;
		char *bin;
		char *ztest;
		char *isa;
		int isalen;

		cmd = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
		realaltdir = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);

		VERIFY(NULL != realpath(getexecname(), cmd));
		if (0 != access(altdir, F_OK)) {
			ztest_dump_core = B_FALSE;
			fatal(B_TRUE, "invalid alternate ztest path: %s",
			    altdir);
		}
		VERIFY(NULL != realpath(altdir, realaltdir));

		/*
		 * 'cmd' should be of the form "<anything>/usr/bin/<isa>/ztest".
		 * We want to extract <isa> to determine if we should use
		 * 32 or 64 bit binaries.
		 */
		bin = strstr(cmd, "/usr/bin/");
		ztest = strstr(bin, "/ztest");
		isa = bin + 9;
		isalen = ztest - isa;
		(void) snprintf(zo->zo_alt_ztest, sizeof (zo->zo_alt_ztest),
		    "%s/usr/bin/%.*s/ztest", realaltdir, isalen, isa);
		(void) snprintf(zo->zo_alt_libpath, sizeof (zo->zo_alt_libpath),
		    "%s/usr/lib/%.*s", realaltdir, isalen, isa);

		if (0 != access(zo->zo_alt_ztest, X_OK)) {
			ztest_dump_core = B_FALSE;
			fatal(B_TRUE, "invalid alternate ztest: %s",
			    zo->zo_alt_ztest);
		} else if (0 != access(zo->zo_alt_libpath, X_OK)) {
			ztest_dump_core = B_FALSE;
			fatal(B_TRUE, "invalid alternate lib directory %s",
			    zo->zo_alt_libpath);
		}

		umem_free(cmd, MAXPATHLEN);
		umem_free(realaltdir, MAXPATHLEN);
	}
}

static void
ztest_kill(ztest_shared_t *zs)
{
	zs->zs_alloc = metaslab_class_get_alloc(spa_normal_class(ztest_spa));
	zs->zs_space = metaslab_class_get_space(spa_normal_class(ztest_spa));

	/*
	 * Before we kill off ztest, make sure that the config is updated.
	 * See comment above spa_write_cachefile().
	 */
	mutex_enter(&spa_namespace_lock);
	spa_write_cachefile(ztest_spa, B_FALSE, B_FALSE);
	mutex_exit(&spa_namespace_lock);

	zfs_dbgmsg_print(FTAG);
	(void) kill(getpid(), SIGKILL);
}

static uint64_t
ztest_random(uint64_t range)
{
	uint64_t r;

	ASSERT3S(ztest_fd_rand, >=, 0);

	if (range == 0)
		return (0);

	if (read(ztest_fd_rand, &r, sizeof (r)) != sizeof (r))
		fatal(1, "short read from /dev/urandom");

	return (r % range);
}

/* ARGSUSED */
static void
ztest_record_enospc(const char *s)
{
	ztest_shared->zs_enospc_count++;
}

static uint64_t
ztest_get_ashift(void)
{
	if (ztest_opts.zo_ashift == 0)
		return (SPA_MINBLOCKSHIFT + ztest_random(5));
	return (ztest_opts.zo_ashift);
}

static nvlist_t *
make_vdev_file(char *path, char *aux, char *pool, size_t size, uint64_t ashift)
{
	char pathbuf[MAXPATHLEN];
	uint64_t vdev;
	nvlist_t *file;

	if (ashift == 0)
		ashift = ztest_get_ashift();

	if (path == NULL) {
		path = pathbuf;

		if (aux != NULL) {
			vdev = ztest_shared->zs_vdev_aux;
			(void) snprintf(path, sizeof (pathbuf),
			    ztest_aux_template, ztest_opts.zo_dir,
			    pool == NULL ? ztest_opts.zo_pool : pool,
			    aux, vdev);
		} else {
			vdev = ztest_shared->zs_vdev_next_leaf++;
			(void) snprintf(path, sizeof (pathbuf),
			    ztest_dev_template, ztest_opts.zo_dir,
			    pool == NULL ? ztest_opts.zo_pool : pool, vdev);
		}
	}

	if (size != 0) {
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd == -1)
			fatal(1, "can't open %s", path);
		if (ftruncate(fd, size) != 0)
			fatal(1, "can't ftruncate %s", path);
		(void) close(fd);
	}

	VERIFY(nvlist_alloc(&file, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(file, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE) == 0);
	VERIFY(nvlist_add_string(file, ZPOOL_CONFIG_PATH, path) == 0);
	VERIFY(nvlist_add_uint64(file, ZPOOL_CONFIG_ASHIFT, ashift) == 0);

	return (file);
}

static nvlist_t *
make_vdev_raidz(char *path, char *aux, char *pool, size_t size,
    uint64_t ashift, int r)
{
	nvlist_t *raidz, **child;
	int c;

	if (r < 2)
		return (make_vdev_file(path, aux, pool, size, ashift));
	child = umem_alloc(r * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < r; c++)
		child[c] = make_vdev_file(path, aux, pool, size, ashift);

	VERIFY(nvlist_alloc(&raidz, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(raidz, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_RAIDZ) == 0);
	VERIFY(nvlist_add_uint64(raidz, ZPOOL_CONFIG_NPARITY,
	    ztest_opts.zo_raidz_parity) == 0);
	VERIFY(nvlist_add_nvlist_array(raidz, ZPOOL_CONFIG_CHILDREN,
	    child, r) == 0);

	for (c = 0; c < r; c++)
		nvlist_free(child[c]);

	umem_free(child, r * sizeof (nvlist_t *));

	return (raidz);
}

static nvlist_t *
make_vdev_mirror(char *path, char *aux, char *pool, size_t size,
    uint64_t ashift, int r, int m)
{
	nvlist_t *mirror, **child;
	int c;

	if (m < 1)
		return (make_vdev_raidz(path, aux, pool, size, ashift, r));

	child = umem_alloc(m * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < m; c++)
		child[c] = make_vdev_raidz(path, aux, pool, size, ashift, r);

	VERIFY(nvlist_alloc(&mirror, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(mirror, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_MIRROR) == 0);
	VERIFY(nvlist_add_nvlist_array(mirror, ZPOOL_CONFIG_CHILDREN,
	    child, m) == 0);

	for (c = 0; c < m; c++)
		nvlist_free(child[c]);

	umem_free(child, m * sizeof (nvlist_t *));

	return (mirror);
}

static nvlist_t *
make_vdev_root(char *path, char *aux, char *pool, size_t size, uint64_t ashift,
    int log, int r, int m, int t)
{
	nvlist_t *root, **child;
	int c;

	ASSERT(t > 0);

	child = umem_alloc(t * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < t; c++) {
		child[c] = make_vdev_mirror(path, aux, pool, size, ashift,
		    r, m);
		VERIFY(nvlist_add_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    log) == 0);
	}

	VERIFY(nvlist_alloc(&root, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_nvlist_array(root, aux ? aux : ZPOOL_CONFIG_CHILDREN,
	    child, t) == 0);

	for (c = 0; c < t; c++)
		nvlist_free(child[c]);

	umem_free(child, t * sizeof (nvlist_t *));

	return (root);
}

/*
 * Find a random spa version. Returns back a random spa version in the
 * range [initial_version, SPA_VERSION_FEATURES].
 */
static uint64_t
ztest_random_spa_version(uint64_t initial_version)
{
	uint64_t version = initial_version;

	if (version <= SPA_VERSION_BEFORE_FEATURES) {
		version = version +
		    ztest_random(SPA_VERSION_BEFORE_FEATURES - version + 1);
	}

	if (version > SPA_VERSION_BEFORE_FEATURES)
		version = SPA_VERSION_FEATURES;

	ASSERT(SPA_VERSION_IS_SUPPORTED(version));
	return (version);
}

static int
ztest_random_blocksize(void)
{
	uint64_t block_shift;
	/*
	 * Choose a block size >= the ashift.
	 * If the SPA supports new MAXBLOCKSIZE, test up to 1MB blocks.
	 */
	int maxbs = SPA_OLD_MAXBLOCKSHIFT;
	if (spa_maxblocksize(ztest_spa) == SPA_MAXBLOCKSIZE)
		maxbs = 20;
	block_shift = ztest_random(maxbs - ztest_spa->spa_max_ashift + 1);
	return (1 << (SPA_MINBLOCKSHIFT + block_shift));
}

static int
ztest_random_dnodesize(void)
{
	int slots;
	int max_slots = spa_maxdnodesize(ztest_spa) >> DNODE_SHIFT;

	if (max_slots == DNODE_MIN_SLOTS)
		return (DNODE_MIN_SIZE);

	/*
	 * Weight the random distribution more heavily toward smaller
	 * dnode sizes since that is more likely to reflect real-world
	 * usage.
	 */
	ASSERT3U(max_slots, >, 4);
	switch (ztest_random(10)) {
	case 0:
		slots = 5 + ztest_random(max_slots - 4);
		break;
	case 1 ... 4:
		slots = 2 + ztest_random(3);
		break;
	default:
		slots = 1;
		break;
	}

	return (slots << DNODE_SHIFT);
}

static int
ztest_random_ibshift(void)
{
	return (DN_MIN_INDBLKSHIFT +
	    ztest_random(DN_MAX_INDBLKSHIFT - DN_MIN_INDBLKSHIFT + 1));
}

static uint64_t
ztest_random_vdev_top(spa_t *spa, boolean_t log_ok)
{
	uint64_t top;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *tvd;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);

	do {
		top = ztest_random(rvd->vdev_children);
		tvd = rvd->vdev_child[top];
	} while (!vdev_is_concrete(tvd) || (tvd->vdev_islog && !log_ok) ||
	    tvd->vdev_mg == NULL || tvd->vdev_mg->mg_class == NULL);

	return (top);
}

static uint64_t
ztest_random_dsl_prop(zfs_prop_t prop)
{
	uint64_t value;

	do {
		value = zfs_prop_random_value(prop, ztest_random(-1ULL));
	} while (prop == ZFS_PROP_CHECKSUM && value == ZIO_CHECKSUM_OFF);

	return (value);
}

static int
ztest_dsl_prop_set_uint64(char *osname, zfs_prop_t prop, uint64_t value,
    boolean_t inherit)
{
	const char *propname = zfs_prop_to_name(prop);
	const char *valname;
	char setpoint[MAXPATHLEN];
	uint64_t curval;
	int error;

	error = dsl_prop_set_int(osname, propname,
	    (inherit ? ZPROP_SRC_NONE : ZPROP_SRC_LOCAL), value);

	if (error == ENOSPC) {
		ztest_record_enospc(FTAG);
		return (error);
	}
	ASSERT0(error);

	VERIFY0(dsl_prop_get_integer(osname, propname, &curval, setpoint));

	if (ztest_opts.zo_verbose >= 6) {
		VERIFY(zfs_prop_index_to_string(prop, curval, &valname) == 0);
		(void) printf("%s %s = %s at '%s'\n",
		    osname, propname, valname, setpoint);
	}

	return (error);
}

static int
ztest_spa_prop_set_uint64(zpool_prop_t prop, uint64_t value)
{
	spa_t *spa = ztest_spa;
	nvlist_t *props = NULL;
	int error;

	VERIFY(nvlist_alloc(&props, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_uint64(props, zpool_prop_to_name(prop), value) == 0);

	error = spa_prop_set(spa, props);

	nvlist_free(props);

	if (error == ENOSPC) {
		ztest_record_enospc(FTAG);
		return (error);
	}
	ASSERT0(error);

	return (error);
}

static void
ztest_rll_init(rll_t *rll)
{
	rll->rll_writer = NULL;
	rll->rll_readers = 0;
	mutex_init(&rll->rll_lock, NULL, USYNC_THREAD, NULL);
	cv_init(&rll->rll_cv, NULL, USYNC_THREAD, NULL);
}

static void
ztest_rll_destroy(rll_t *rll)
{
	ASSERT(rll->rll_writer == NULL);
	ASSERT(rll->rll_readers == 0);
	mutex_destroy(&rll->rll_lock);
	cv_destroy(&rll->rll_cv);
}

static void
ztest_rll_lock(rll_t *rll, rl_type_t type)
{
	mutex_enter(&rll->rll_lock);

	if (type == RL_READER) {
		while (rll->rll_writer != NULL)
			cv_wait(&rll->rll_cv, &rll->rll_lock);
		rll->rll_readers++;
	} else {
		while (rll->rll_writer != NULL || rll->rll_readers)
			cv_wait(&rll->rll_cv, &rll->rll_lock);
		rll->rll_writer = curthread;
	}

	mutex_exit(&rll->rll_lock);
}

static void
ztest_rll_unlock(rll_t *rll)
{
	mutex_enter(&rll->rll_lock);

	if (rll->rll_writer) {
		ASSERT(rll->rll_readers == 0);
		rll->rll_writer = NULL;
	} else {
		ASSERT(rll->rll_readers != 0);
		ASSERT(rll->rll_writer == NULL);
		rll->rll_readers--;
	}

	if (rll->rll_writer == NULL && rll->rll_readers == 0)
		cv_broadcast(&rll->rll_cv);

	mutex_exit(&rll->rll_lock);
}

static void
ztest_object_lock(ztest_ds_t *zd, uint64_t object, rl_type_t type)
{
	rll_t *rll = &zd->zd_object_lock[object & (ZTEST_OBJECT_LOCKS - 1)];

	ztest_rll_lock(rll, type);
}

static void
ztest_object_unlock(ztest_ds_t *zd, uint64_t object)
{
	rll_t *rll = &zd->zd_object_lock[object & (ZTEST_OBJECT_LOCKS - 1)];

	ztest_rll_unlock(rll);
}

static rl_t *
ztest_range_lock(ztest_ds_t *zd, uint64_t object, uint64_t offset,
    uint64_t size, rl_type_t type)
{
	uint64_t hash = object ^ (offset % (ZTEST_RANGE_LOCKS + 1));
	rll_t *rll = &zd->zd_range_lock[hash & (ZTEST_RANGE_LOCKS - 1)];
	rl_t *rl;

	rl = umem_alloc(sizeof (*rl), UMEM_NOFAIL);
	rl->rl_object = object;
	rl->rl_offset = offset;
	rl->rl_size = size;
	rl->rl_lock = rll;

	ztest_rll_lock(rll, type);

	return (rl);
}

static void
ztest_range_unlock(rl_t *rl)
{
	rll_t *rll = rl->rl_lock;

	ztest_rll_unlock(rll);

	umem_free(rl, sizeof (*rl));
}

static void
ztest_zd_init(ztest_ds_t *zd, ztest_shared_ds_t *szd, objset_t *os)
{
	zd->zd_os = os;
	zd->zd_zilog = dmu_objset_zil(os);
	zd->zd_shared = szd;
	dmu_objset_name(os, zd->zd_name);

	if (zd->zd_shared != NULL)
		zd->zd_shared->zd_seq = 0;

	rw_init(&zd->zd_zilog_lock, NULL, USYNC_THREAD, NULL);
	mutex_init(&zd->zd_dirobj_lock, NULL, USYNC_THREAD, NULL);

	for (int l = 0; l < ZTEST_OBJECT_LOCKS; l++)
		ztest_rll_init(&zd->zd_object_lock[l]);

	for (int l = 0; l < ZTEST_RANGE_LOCKS; l++)
		ztest_rll_init(&zd->zd_range_lock[l]);
}

static void
ztest_zd_fini(ztest_ds_t *zd)
{
	mutex_destroy(&zd->zd_dirobj_lock);

	for (int l = 0; l < ZTEST_OBJECT_LOCKS; l++)
		ztest_rll_destroy(&zd->zd_object_lock[l]);

	for (int l = 0; l < ZTEST_RANGE_LOCKS; l++)
		ztest_rll_destroy(&zd->zd_range_lock[l]);
}

#define	TXG_MIGHTWAIT	(ztest_random(10) == 0 ? TXG_NOWAIT : TXG_WAIT)

static uint64_t
ztest_tx_assign(dmu_tx_t *tx, uint64_t txg_how, const char *tag)
{
	uint64_t txg;
	int error;

	/*
	 * Attempt to assign tx to some transaction group.
	 */
	error = dmu_tx_assign(tx, txg_how);
	if (error) {
		if (error == ERESTART) {
			ASSERT(txg_how == TXG_NOWAIT);
			dmu_tx_wait(tx);
		} else {
			ASSERT3U(error, ==, ENOSPC);
			ztest_record_enospc(tag);
		}
		dmu_tx_abort(tx);
		return (0);
	}
	txg = dmu_tx_get_txg(tx);
	ASSERT(txg != 0);
	return (txg);
}

static void
ztest_pattern_set(void *buf, uint64_t size, uint64_t value)
{
	uint64_t *ip = buf;
	uint64_t *ip_end = (uint64_t *)((uintptr_t)buf + (uintptr_t)size);

	while (ip < ip_end)
		*ip++ = value;
}

static boolean_t
ztest_pattern_match(void *buf, uint64_t size, uint64_t value)
{
	uint64_t *ip = buf;
	uint64_t *ip_end = (uint64_t *)((uintptr_t)buf + (uintptr_t)size);
	uint64_t diff = 0;

	while (ip < ip_end)
		diff |= (value - *ip++);

	return (diff == 0);
}

static void
ztest_bt_generate(ztest_block_tag_t *bt, objset_t *os, uint64_t object,
    uint64_t dnodesize, uint64_t offset, uint64_t gen, uint64_t txg,
    uint64_t crtxg)
{
	bt->bt_magic = BT_MAGIC;
	bt->bt_objset = dmu_objset_id(os);
	bt->bt_object = object;
	bt->bt_dnodesize = dnodesize;
	bt->bt_offset = offset;
	bt->bt_gen = gen;
	bt->bt_txg = txg;
	bt->bt_crtxg = crtxg;
}

static void
ztest_bt_verify(ztest_block_tag_t *bt, objset_t *os, uint64_t object,
    uint64_t dnodesize, uint64_t offset, uint64_t gen, uint64_t txg,
    uint64_t crtxg)
{
	ASSERT3U(bt->bt_magic, ==, BT_MAGIC);
	ASSERT3U(bt->bt_objset, ==, dmu_objset_id(os));
	ASSERT3U(bt->bt_object, ==, object);
	ASSERT3U(bt->bt_dnodesize, ==, dnodesize);
	ASSERT3U(bt->bt_offset, ==, offset);
	ASSERT3U(bt->bt_gen, <=, gen);
	ASSERT3U(bt->bt_txg, <=, txg);
	ASSERT3U(bt->bt_crtxg, ==, crtxg);
}

static ztest_block_tag_t *
ztest_bt_bonus(dmu_buf_t *db)
{
	dmu_object_info_t doi;
	ztest_block_tag_t *bt;

	dmu_object_info_from_db(db, &doi);
	ASSERT3U(doi.doi_bonus_size, <=, db->db_size);
	ASSERT3U(doi.doi_bonus_size, >=, sizeof (*bt));
	bt = (void *)((char *)db->db_data + doi.doi_bonus_size - sizeof (*bt));

	return (bt);
}

/*
 * Generate a token to fill up unused bonus buffer space.  Try to make
 * it unique to the object, generation, and offset to verify that data
 * is not getting overwritten by data from other dnodes.
 */
#define	ZTEST_BONUS_FILL_TOKEN(obj, ds, gen, offset) \
	(((ds) << 48) | ((gen) << 32) | ((obj) << 8) | (offset))

/*
 * Fill up the unused bonus buffer region before the block tag with a
 * verifiable pattern. Filling the whole bonus area with non-zero data
 * helps ensure that all dnode traversal code properly skips the
 * interior regions of large dnodes.
 */
void
ztest_fill_unused_bonus(dmu_buf_t *db, void *end, uint64_t obj,
    objset_t *os, uint64_t gen)
{
	uint64_t *bonusp;

	ASSERT(IS_P2ALIGNED((char *)end - (char *)db->db_data, 8));

	for (bonusp = db->db_data; bonusp < (uint64_t *)end; bonusp++) {
		uint64_t token = ZTEST_BONUS_FILL_TOKEN(obj, dmu_objset_id(os),
		    gen, bonusp - (uint64_t *)db->db_data);
		*bonusp = token;
	}
}

/*
 * Verify that the unused area of a bonus buffer is filled with the
 * expected tokens.
 */
void
ztest_verify_unused_bonus(dmu_buf_t *db, void *end, uint64_t obj,
    objset_t *os, uint64_t gen)
{
	uint64_t *bonusp;

	for (bonusp = db->db_data; bonusp < (uint64_t *)end; bonusp++) {
		uint64_t token = ZTEST_BONUS_FILL_TOKEN(obj, dmu_objset_id(os),
		    gen, bonusp - (uint64_t *)db->db_data);
		VERIFY3U(*bonusp, ==, token);
	}
}

/*
 * ZIL logging ops
 */

#define	lrz_type	lr_mode
#define	lrz_blocksize	lr_uid
#define	lrz_ibshift	lr_gid
#define	lrz_bonustype	lr_rdev
#define	lrz_dnodesize	lr_crtime[1]

static void
ztest_log_create(ztest_ds_t *zd, dmu_tx_t *tx, lr_create_t *lr)
{
	char *name = (void *)(lr + 1);		/* name follows lr */
	size_t namesize = strlen(name) + 1;
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_CREATE, sizeof (*lr) + namesize);
	bcopy(&lr->lr_common + 1, &itx->itx_lr + 1,
	    sizeof (*lr) + namesize - sizeof (lr_t));

	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_remove(ztest_ds_t *zd, dmu_tx_t *tx, lr_remove_t *lr, uint64_t object)
{
	char *name = (void *)(lr + 1);		/* name follows lr */
	size_t namesize = strlen(name) + 1;
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_REMOVE, sizeof (*lr) + namesize);
	bcopy(&lr->lr_common + 1, &itx->itx_lr + 1,
	    sizeof (*lr) + namesize - sizeof (lr_t));

	itx->itx_oid = object;
	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_write(ztest_ds_t *zd, dmu_tx_t *tx, lr_write_t *lr)
{
	itx_t *itx;
	itx_wr_state_t write_state = ztest_random(WR_NUM_STATES);

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	if (lr->lr_length > ZIL_MAX_LOG_DATA)
		write_state = WR_INDIRECT;

	itx = zil_itx_create(TX_WRITE,
	    sizeof (*lr) + (write_state == WR_COPIED ? lr->lr_length : 0));

	if (write_state == WR_COPIED &&
	    dmu_read(zd->zd_os, lr->lr_foid, lr->lr_offset, lr->lr_length,
	    ((lr_write_t *)&itx->itx_lr) + 1, DMU_READ_NO_PREFETCH) != 0) {
		zil_itx_destroy(itx);
		itx = zil_itx_create(TX_WRITE, sizeof (*lr));
		write_state = WR_NEED_COPY;
	}
	itx->itx_private = zd;
	itx->itx_wr_state = write_state;
	itx->itx_sync = (ztest_random(8) == 0);

	bcopy(&lr->lr_common + 1, &itx->itx_lr + 1,
	    sizeof (*lr) - sizeof (lr_t));

	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_truncate(ztest_ds_t *zd, dmu_tx_t *tx, lr_truncate_t *lr)
{
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_TRUNCATE, sizeof (*lr));
	bcopy(&lr->lr_common + 1, &itx->itx_lr + 1,
	    sizeof (*lr) - sizeof (lr_t));

	itx->itx_sync = B_FALSE;
	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_setattr(ztest_ds_t *zd, dmu_tx_t *tx, lr_setattr_t *lr)
{
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_SETATTR, sizeof (*lr));
	bcopy(&lr->lr_common + 1, &itx->itx_lr + 1,
	    sizeof (*lr) - sizeof (lr_t));

	itx->itx_sync = B_FALSE;
	zil_itx_assign(zd->zd_zilog, itx, tx);
}

/*
 * ZIL replay ops
 */
static int
ztest_replay_create(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_create_t *lr = arg2;
	char *name = (void *)(lr + 1);		/* name follows lr */
	objset_t *os = zd->zd_os;
	ztest_block_tag_t *bbt;
	dmu_buf_t *db;
	dmu_tx_t *tx;
	uint64_t txg;
	int error = 0;
	int bonuslen;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ASSERT(lr->lr_doid == ZTEST_DIROBJ);
	ASSERT(name[0] != '\0');

	tx = dmu_tx_create(os);

	dmu_tx_hold_zap(tx, lr->lr_doid, B_TRUE, name);

	if (lr->lrz_type == DMU_OT_ZAP_OTHER) {
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
	} else {
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	}

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0)
		return (ENOSPC);

	ASSERT(dmu_objset_zil(os)->zl_replay == !!lr->lr_foid);
	bonuslen = DN_BONUS_SIZE(lr->lrz_dnodesize);

	if (lr->lrz_type == DMU_OT_ZAP_OTHER) {
		if (lr->lr_foid == 0) {
			lr->lr_foid = zap_create_dnsize(os,
			    lr->lrz_type, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		} else {
			error = zap_create_claim_dnsize(os, lr->lr_foid,
			    lr->lrz_type, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		}
	} else {
		if (lr->lr_foid == 0) {
			lr->lr_foid = dmu_object_alloc_dnsize(os,
			    lr->lrz_type, 0, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		} else {
			error = dmu_object_claim_dnsize(os, lr->lr_foid,
			    lr->lrz_type, 0, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		}
	}

	if (error) {
		ASSERT3U(error, ==, EEXIST);
		ASSERT(zd->zd_zilog->zl_replay);
		dmu_tx_commit(tx);
		return (error);
	}

	ASSERT(lr->lr_foid != 0);

	if (lr->lrz_type != DMU_OT_ZAP_OTHER)
		VERIFY3U(0, ==, dmu_object_set_blocksize(os, lr->lr_foid,
		    lr->lrz_blocksize, lr->lrz_ibshift, tx));

	VERIFY3U(0, ==, dmu_bonus_hold(os, lr->lr_foid, FTAG, &db));
	bbt = ztest_bt_bonus(db);
	dmu_buf_will_dirty(db, tx);
	ztest_bt_generate(bbt, os, lr->lr_foid, lr->lrz_dnodesize, -1ULL,
	    lr->lr_gen, txg, txg);
	ztest_fill_unused_bonus(db, bbt, lr->lr_foid, os, lr->lr_gen);
	dmu_buf_rele(db, FTAG);

	VERIFY3U(0, ==, zap_add(os, lr->lr_doid, name, sizeof (uint64_t), 1,
	    &lr->lr_foid, tx));

	(void) ztest_log_create(zd, tx, lr);

	dmu_tx_commit(tx);

	return (0);
}

static int
ztest_replay_remove(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_remove_t *lr = arg2;
	char *name = (void *)(lr + 1);		/* name follows lr */
	objset_t *os = zd->zd_os;
	dmu_object_info_t doi;
	dmu_tx_t *tx;
	uint64_t object, txg;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ASSERT(lr->lr_doid == ZTEST_DIROBJ);
	ASSERT(name[0] != '\0');

	VERIFY3U(0, ==,
	    zap_lookup(os, lr->lr_doid, name, sizeof (object), 1, &object));
	ASSERT(object != 0);

	ztest_object_lock(zd, object, RL_WRITER);

	VERIFY3U(0, ==, dmu_object_info(os, object, &doi));

	tx = dmu_tx_create(os);

	dmu_tx_hold_zap(tx, lr->lr_doid, B_FALSE, name);
	dmu_tx_hold_free(tx, object, 0, DMU_OBJECT_END);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		ztest_object_unlock(zd, object);
		return (ENOSPC);
	}

	if (doi.doi_type == DMU_OT_ZAP_OTHER) {
		VERIFY3U(0, ==, zap_destroy(os, object, tx));
	} else {
		VERIFY3U(0, ==, dmu_object_free(os, object, tx));
	}

	VERIFY3U(0, ==, zap_remove(os, lr->lr_doid, name, tx));

	(void) ztest_log_remove(zd, tx, lr, object);

	dmu_tx_commit(tx);

	ztest_object_unlock(zd, object);

	return (0);
}

static int
ztest_replay_write(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_write_t *lr = arg2;
	objset_t *os = zd->zd_os;
	void *data = lr + 1;			/* data follows lr */
	uint64_t offset, length;
	ztest_block_tag_t *bt = data;
	ztest_block_tag_t *bbt;
	uint64_t gen, txg, lrtxg, crtxg;
	dmu_object_info_t doi;
	dmu_tx_t *tx;
	dmu_buf_t *db;
	arc_buf_t *abuf = NULL;
	rl_t *rl;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	/* If it's a dmu_sync() block, write the whole block */
	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		uint64_t blocksize = BP_GET_LSIZE(&lr->lr_blkptr);
		if (length < blocksize) {
			offset -= offset % blocksize;
			length = blocksize;
		}
	}

	if (bt->bt_magic == BSWAP_64(BT_MAGIC))
		byteswap_uint64_array(bt, sizeof (*bt));

	if (bt->bt_magic != BT_MAGIC)
		bt = NULL;

	ztest_object_lock(zd, lr->lr_foid, RL_READER);
	rl = ztest_range_lock(zd, lr->lr_foid, offset, length, RL_WRITER);

	VERIFY3U(0, ==, dmu_bonus_hold(os, lr->lr_foid, FTAG, &db));

	dmu_object_info_from_db(db, &doi);

	bbt = ztest_bt_bonus(db);
	ASSERT3U(bbt->bt_magic, ==, BT_MAGIC);
	gen = bbt->bt_gen;
	crtxg = bbt->bt_crtxg;
	lrtxg = lr->lr_common.lrc_txg;

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, lr->lr_foid, offset, length);

	if (ztest_random(8) == 0 && length == doi.doi_data_block_size &&
	    P2PHASE(offset, length) == 0)
		abuf = dmu_request_arcbuf(db, length);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		if (abuf != NULL)
			dmu_return_arcbuf(abuf);
		dmu_buf_rele(db, FTAG);
		ztest_range_unlock(rl);
		ztest_object_unlock(zd, lr->lr_foid);
		return (ENOSPC);
	}

	if (bt != NULL) {
		/*
		 * Usually, verify the old data before writing new data --
		 * but not always, because we also want to verify correct
		 * behavior when the data was not recently read into cache.
		 */
		ASSERT(offset % doi.doi_data_block_size == 0);
		if (ztest_random(4) != 0) {
			int prefetch = ztest_random(2) ?
			    DMU_READ_PREFETCH : DMU_READ_NO_PREFETCH;
			ztest_block_tag_t rbt;

			VERIFY(dmu_read(os, lr->lr_foid, offset,
			    sizeof (rbt), &rbt, prefetch) == 0);
			if (rbt.bt_magic == BT_MAGIC) {
				ztest_bt_verify(&rbt, os, lr->lr_foid, 0,
				    offset, gen, txg, crtxg);
			}
		}

		/*
		 * Writes can appear to be newer than the bonus buffer because
		 * the ztest_get_data() callback does a dmu_read() of the
		 * open-context data, which may be different than the data
		 * as it was when the write was generated.
		 */
		if (zd->zd_zilog->zl_replay) {
			ztest_bt_verify(bt, os, lr->lr_foid, 0, offset,
			    MAX(gen, bt->bt_gen), MAX(txg, lrtxg),
			    bt->bt_crtxg);
		}

		/*
		 * Set the bt's gen/txg to the bonus buffer's gen/txg
		 * so that all of the usual ASSERTs will work.
		 */
		ztest_bt_generate(bt, os, lr->lr_foid, 0, offset, gen, txg,
		    crtxg);
	}

	if (abuf == NULL) {
		dmu_write(os, lr->lr_foid, offset, length, data, tx);
	} else {
		bcopy(data, abuf->b_data, length);
		dmu_assign_arcbuf(db, offset, abuf, tx);
	}

	(void) ztest_log_write(zd, tx, lr);

	dmu_buf_rele(db, FTAG);

	dmu_tx_commit(tx);

	ztest_range_unlock(rl);
	ztest_object_unlock(zd, lr->lr_foid);

	return (0);
}

static int
ztest_replay_truncate(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_truncate_t *lr = arg2;
	objset_t *os = zd->zd_os;
	dmu_tx_t *tx;
	uint64_t txg;
	rl_t *rl;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ztest_object_lock(zd, lr->lr_foid, RL_READER);
	rl = ztest_range_lock(zd, lr->lr_foid, lr->lr_offset, lr->lr_length,
	    RL_WRITER);

	tx = dmu_tx_create(os);

	dmu_tx_hold_free(tx, lr->lr_foid, lr->lr_offset, lr->lr_length);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		ztest_range_unlock(rl);
		ztest_object_unlock(zd, lr->lr_foid);
		return (ENOSPC);
	}

	VERIFY(dmu_free_range(os, lr->lr_foid, lr->lr_offset,
	    lr->lr_length, tx) == 0);

	(void) ztest_log_truncate(zd, tx, lr);

	dmu_tx_commit(tx);

	ztest_range_unlock(rl);
	ztest_object_unlock(zd, lr->lr_foid);

	return (0);
}

static int
ztest_replay_setattr(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_setattr_t *lr = arg2;
	objset_t *os = zd->zd_os;
	dmu_tx_t *tx;
	dmu_buf_t *db;
	ztest_block_tag_t *bbt;
	uint64_t txg, lrtxg, crtxg, dnodesize;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ztest_object_lock(zd, lr->lr_foid, RL_WRITER);

	VERIFY3U(0, ==, dmu_bonus_hold(os, lr->lr_foid, FTAG, &db));

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, lr->lr_foid);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		dmu_buf_rele(db, FTAG);
		ztest_object_unlock(zd, lr->lr_foid);
		return (ENOSPC);
	}

	bbt = ztest_bt_bonus(db);
	ASSERT3U(bbt->bt_magic, ==, BT_MAGIC);
	crtxg = bbt->bt_crtxg;
	lrtxg = lr->lr_common.lrc_txg;
	dnodesize = bbt->bt_dnodesize;

	if (zd->zd_zilog->zl_replay) {
		ASSERT(lr->lr_size != 0);
		ASSERT(lr->lr_mode != 0);
		ASSERT(lrtxg != 0);
	} else {
		/*
		 * Randomly change the size and increment the generation.
		 */
		lr->lr_size = (ztest_random(db->db_size / sizeof (*bbt)) + 1) *
		    sizeof (*bbt);
		lr->lr_mode = bbt->bt_gen + 1;
		ASSERT(lrtxg == 0);
	}

	/*
	 * Verify that the current bonus buffer is not newer than our txg.
	 */
	ztest_bt_verify(bbt, os, lr->lr_foid, dnodesize, -1ULL, lr->lr_mode,
	    MAX(txg, lrtxg), crtxg);

	dmu_buf_will_dirty(db, tx);

	ASSERT3U(lr->lr_size, >=, sizeof (*bbt));
	ASSERT3U(lr->lr_size, <=, db->db_size);
	VERIFY0(dmu_set_bonus(db, lr->lr_size, tx));
	bbt = ztest_bt_bonus(db);

	ztest_bt_generate(bbt, os, lr->lr_foid, dnodesize, -1ULL, lr->lr_mode,
	    txg, crtxg);
	ztest_fill_unused_bonus(db, bbt, lr->lr_foid, os, bbt->bt_gen);
	dmu_buf_rele(db, FTAG);

	(void) ztest_log_setattr(zd, tx, lr);

	dmu_tx_commit(tx);

	ztest_object_unlock(zd, lr->lr_foid);

	return (0);
}

zil_replay_func_t *ztest_replay_vector[TX_MAX_TYPE] = {
	NULL,			/* 0 no such transaction type */
	ztest_replay_create,	/* TX_CREATE */
	NULL,			/* TX_MKDIR */
	NULL,			/* TX_MKXATTR */
	NULL,			/* TX_SYMLINK */
	ztest_replay_remove,	/* TX_REMOVE */
	NULL,			/* TX_RMDIR */
	NULL,			/* TX_LINK */
	NULL,			/* TX_RENAME */
	ztest_replay_write,	/* TX_WRITE */
	ztest_replay_truncate,	/* TX_TRUNCATE */
	ztest_replay_setattr,	/* TX_SETATTR */
	NULL,			/* TX_ACL */
	NULL,			/* TX_CREATE_ACL */
	NULL,			/* TX_CREATE_ATTR */
	NULL,			/* TX_CREATE_ACL_ATTR */
	NULL,			/* TX_MKDIR_ACL */
	NULL,			/* TX_MKDIR_ATTR */
	NULL,			/* TX_MKDIR_ACL_ATTR */
	NULL,			/* TX_WRITE2 */
};

/*
 * ZIL get_data callbacks
 */

static void
ztest_get_done(zgd_t *zgd, int error)
{
	ztest_ds_t *zd = zgd->zgd_private;
	uint64_t object = zgd->zgd_rl->rl_object;

	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	ztest_range_unlock(zgd->zgd_rl);
	ztest_object_unlock(zd, object);

	if (error == 0 && zgd->zgd_bp)
		zil_lwb_add_block(zgd->zgd_lwb, zgd->zgd_bp);

	umem_free(zgd, sizeof (*zgd));
}

static int
ztest_get_data(void *arg, lr_write_t *lr, char *buf, struct lwb *lwb,
    zio_t *zio)
{
	ztest_ds_t *zd = arg;
	objset_t *os = zd->zd_os;
	uint64_t object = lr->lr_foid;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	uint64_t txg = lr->lr_common.lrc_txg;
	uint64_t crtxg;
	dmu_object_info_t doi;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT3P(lwb, !=, NULL);
	ASSERT3P(zio, !=, NULL);
	ASSERT3U(size, !=, 0);

	ztest_object_lock(zd, object, RL_READER);
	error = dmu_bonus_hold(os, object, FTAG, &db);
	if (error) {
		ztest_object_unlock(zd, object);
		return (error);
	}

	crtxg = ztest_bt_bonus(db)->bt_crtxg;

	if (crtxg == 0 || crtxg > txg) {
		dmu_buf_rele(db, FTAG);
		ztest_object_unlock(zd, object);
		return (ENOENT);
	}

	dmu_object_info_from_db(db, &doi);
	dmu_buf_rele(db, FTAG);
	db = NULL;

	zgd = umem_zalloc(sizeof (*zgd), UMEM_NOFAIL);
	zgd->zgd_lwb = lwb;
	zgd->zgd_private = zd;

	if (buf != NULL) {	/* immediate write */
		zgd->zgd_rl = ztest_range_lock(zd, object, offset, size,
		    RL_READER);

		error = dmu_read(os, object, offset, size, buf,
		    DMU_READ_NO_PREFETCH);
		ASSERT(error == 0);
	} else {
		size = doi.doi_data_block_size;
		if (ISP2(size)) {
			offset = P2ALIGN(offset, size);
		} else {
			ASSERT(offset < size);
			offset = 0;
		}

		zgd->zgd_rl = ztest_range_lock(zd, object, offset, size,
		    RL_READER);

		error = dmu_buf_hold(os, object, offset, zgd, &db,
		    DMU_READ_NO_PREFETCH);

		if (error == 0) {
			blkptr_t *bp = &lr->lr_blkptr;

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    ztest_get_done, zgd);

			if (error == 0)
				return (0);
		}
	}

	ztest_get_done(zgd, error);

	return (error);
}

static void *
ztest_lr_alloc(size_t lrsize, char *name)
{
	char *lr;
	size_t namesize = name ? strlen(name) + 1 : 0;

	lr = umem_zalloc(lrsize + namesize, UMEM_NOFAIL);

	if (name)
		bcopy(name, lr + lrsize, namesize);

	return (lr);
}

void
ztest_lr_free(void *lr, size_t lrsize, char *name)
{
	size_t namesize = name ? strlen(name) + 1 : 0;

	umem_free(lr, lrsize + namesize);
}

/*
 * Lookup a bunch of objects.  Returns the number of objects not found.
 */
static int
ztest_lookup(ztest_ds_t *zd, ztest_od_t *od, int count)
{
	int missing = 0;
	int error;

	ASSERT(MUTEX_HELD(&zd->zd_dirobj_lock));

	for (int i = 0; i < count; i++, od++) {
		od->od_object = 0;
		error = zap_lookup(zd->zd_os, od->od_dir, od->od_name,
		    sizeof (uint64_t), 1, &od->od_object);
		if (error) {
			ASSERT(error == ENOENT);
			ASSERT(od->od_object == 0);
			missing++;
		} else {
			dmu_buf_t *db;
			ztest_block_tag_t *bbt;
			dmu_object_info_t doi;

			ASSERT(od->od_object != 0);
			ASSERT(missing == 0);	/* there should be no gaps */

			ztest_object_lock(zd, od->od_object, RL_READER);
			VERIFY3U(0, ==, dmu_bonus_hold(zd->zd_os,
			    od->od_object, FTAG, &db));
			dmu_object_info_from_db(db, &doi);
			bbt = ztest_bt_bonus(db);
			ASSERT3U(bbt->bt_magic, ==, BT_MAGIC);
			od->od_type = doi.doi_type;
			od->od_blocksize = doi.doi_data_block_size;
			od->od_gen = bbt->bt_gen;
			dmu_buf_rele(db, FTAG);
			ztest_object_unlock(zd, od->od_object);
		}
	}

	return (missing);
}

static int
ztest_create(ztest_ds_t *zd, ztest_od_t *od, int count)
{
	int missing = 0;

	ASSERT(MUTEX_HELD(&zd->zd_dirobj_lock));

	for (int i = 0; i < count; i++, od++) {
		if (missing) {
			od->od_object = 0;
			missing++;
			continue;
		}

		lr_create_t *lr = ztest_lr_alloc(sizeof (*lr), od->od_name);

		lr->lr_doid = od->od_dir;
		lr->lr_foid = 0;	/* 0 to allocate, > 0 to claim */
		lr->lrz_type = od->od_crtype;
		lr->lrz_blocksize = od->od_crblocksize;
		lr->lrz_ibshift = ztest_random_ibshift();
		lr->lrz_bonustype = DMU_OT_UINT64_OTHER;
		lr->lrz_dnodesize = od->od_crdnodesize;
		lr->lr_gen = od->od_crgen;
		lr->lr_crtime[0] = time(NULL);

		if (ztest_replay_create(zd, lr, B_FALSE) != 0) {
			ASSERT(missing == 0);
			od->od_object = 0;
			missing++;
		} else {
			od->od_object = lr->lr_foid;
			od->od_type = od->od_crtype;
			od->od_blocksize = od->od_crblocksize;
			od->od_gen = od->od_crgen;
			ASSERT(od->od_object != 0);
		}

		ztest_lr_free(lr, sizeof (*lr), od->od_name);
	}

	return (missing);
}

static int
ztest_remove(ztest_ds_t *zd, ztest_od_t *od, int count)
{
	int missing = 0;
	int error;

	ASSERT(MUTEX_HELD(&zd->zd_dirobj_lock));

	od += count - 1;

	for (int i = count - 1; i >= 0; i--, od--) {
		if (missing) {
			missing++;
			continue;
		}

		/*
		 * No object was found.
		 */
		if (od->od_object == 0)
			continue;

		lr_remove_t *lr = ztest_lr_alloc(sizeof (*lr), od->od_name);

		lr->lr_doid = od->od_dir;

		if ((error = ztest_replay_remove(zd, lr, B_FALSE)) != 0) {
			ASSERT3U(error, ==, ENOSPC);
			missing++;
		} else {
			od->od_object = 0;
		}
		ztest_lr_free(lr, sizeof (*lr), od->od_name);
	}

	return (missing);
}

static int
ztest_write(ztest_ds_t *zd, uint64_t object, uint64_t offset, uint64_t size,
    void *data)
{
	lr_write_t *lr;
	int error;

	lr = ztest_lr_alloc(sizeof (*lr) + size, NULL);

	lr->lr_foid = object;
	lr->lr_offset = offset;
	lr->lr_length = size;
	lr->lr_blkoff = 0;
	BP_ZERO(&lr->lr_blkptr);

	bcopy(data, lr + 1, size);

	error = ztest_replay_write(zd, lr, B_FALSE);

	ztest_lr_free(lr, sizeof (*lr) + size, NULL);

	return (error);
}

static int
ztest_truncate(ztest_ds_t *zd, uint64_t object, uint64_t offset, uint64_t size)
{
	lr_truncate_t *lr;
	int error;

	lr = ztest_lr_alloc(sizeof (*lr), NULL);

	lr->lr_foid = object;
	lr->lr_offset = offset;
	lr->lr_length = size;

	error = ztest_replay_truncate(zd, lr, B_FALSE);

	ztest_lr_free(lr, sizeof (*lr), NULL);

	return (error);
}

static int
ztest_setattr(ztest_ds_t *zd, uint64_t object)
{
	lr_setattr_t *lr;
	int error;

	lr = ztest_lr_alloc(sizeof (*lr), NULL);

	lr->lr_foid = object;
	lr->lr_size = 0;
	lr->lr_mode = 0;

	error = ztest_replay_setattr(zd, lr, B_FALSE);

	ztest_lr_free(lr, sizeof (*lr), NULL);

	return (error);
}

static void
ztest_prealloc(ztest_ds_t *zd, uint64_t object, uint64_t offset, uint64_t size)
{
	objset_t *os = zd->zd_os;
	dmu_tx_t *tx;
	uint64_t txg;
	rl_t *rl;

	txg_wait_synced(dmu_objset_pool(os), 0);

	ztest_object_lock(zd, object, RL_READER);
	rl = ztest_range_lock(zd, object, offset, size, RL_WRITER);

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, object, offset, size);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);

	if (txg != 0) {
		dmu_prealloc(os, object, offset, size, tx);
		dmu_tx_commit(tx);
		txg_wait_synced(dmu_objset_pool(os), txg);
	} else {
		(void) dmu_free_long_range(os, object, offset, size);
	}

	ztest_range_unlock(rl);
	ztest_object_unlock(zd, object);
}

static void
ztest_io(ztest_ds_t *zd, uint64_t object, uint64_t offset)
{
	int err;
	ztest_block_tag_t wbt;
	dmu_object_info_t doi;
	enum ztest_io_type io_type;
	uint64_t blocksize;
	void *data;

	VERIFY(dmu_object_info(zd->zd_os, object, &doi) == 0);
	blocksize = doi.doi_data_block_size;
	data = umem_alloc(blocksize, UMEM_NOFAIL);

	/*
	 * Pick an i/o type at random, biased toward writing block tags.
	 */
	io_type = ztest_random(ZTEST_IO_TYPES);
	if (ztest_random(2) == 0)
		io_type = ZTEST_IO_WRITE_TAG;

	rw_enter(&zd->zd_zilog_lock, RW_READER);

	switch (io_type) {

	case ZTEST_IO_WRITE_TAG:
		ztest_bt_generate(&wbt, zd->zd_os, object, doi.doi_dnodesize,
		    offset, 0, 0, 0);
		(void) ztest_write(zd, object, offset, sizeof (wbt), &wbt);
		break;

	case ZTEST_IO_WRITE_PATTERN:
		(void) memset(data, 'a' + (object + offset) % 5, blocksize);
		if (ztest_random(2) == 0) {
			/*
			 * Induce fletcher2 collisions to ensure that
			 * zio_ddt_collision() detects and resolves them
			 * when using fletcher2-verify for deduplication.
			 */
			((uint64_t *)data)[0] ^= 1ULL << 63;
			((uint64_t *)data)[4] ^= 1ULL << 63;
		}
		(void) ztest_write(zd, object, offset, blocksize, data);
		break;

	case ZTEST_IO_WRITE_ZEROES:
		bzero(data, blocksize);
		(void) ztest_write(zd, object, offset, blocksize, data);
		break;

	case ZTEST_IO_TRUNCATE:
		(void) ztest_truncate(zd, object, offset, blocksize);
		break;

	case ZTEST_IO_SETATTR:
		(void) ztest_setattr(zd, object);
		break;

	case ZTEST_IO_REWRITE:
		rw_enter(&ztest_name_lock, RW_READER);
		err = ztest_dsl_prop_set_uint64(zd->zd_name,
		    ZFS_PROP_CHECKSUM, spa_dedup_checksum(ztest_spa),
		    B_FALSE);
		VERIFY(err == 0 || err == ENOSPC);
		err = ztest_dsl_prop_set_uint64(zd->zd_name,
		    ZFS_PROP_COMPRESSION,
		    ztest_random_dsl_prop(ZFS_PROP_COMPRESSION),
		    B_FALSE);
		VERIFY(err == 0 || err == ENOSPC);
		rw_exit(&ztest_name_lock);

		VERIFY0(dmu_read(zd->zd_os, object, offset, blocksize, data,
		    DMU_READ_NO_PREFETCH));

		(void) ztest_write(zd, object, offset, blocksize, data);
		break;
	}

	rw_exit(&zd->zd_zilog_lock);

	umem_free(data, blocksize);
}

/*
 * Initialize an object description template.
 */
static void
ztest_od_init(ztest_od_t *od, uint64_t id, char *tag, uint64_t index,
    dmu_object_type_t type, uint64_t blocksize, uint64_t dnodesize,
    uint64_t gen)
{
	od->od_dir = ZTEST_DIROBJ;
	od->od_object = 0;

	od->od_crtype = type;
	od->od_crblocksize = blocksize ? blocksize : ztest_random_blocksize();
	od->od_crdnodesize = dnodesize ? dnodesize : ztest_random_dnodesize();
	od->od_crgen = gen;

	od->od_type = DMU_OT_NONE;
	od->od_blocksize = 0;
	od->od_gen = 0;

	(void) snprintf(od->od_name, sizeof (od->od_name), "%s(%lld)[%llu]",
	    tag, (int64_t)id, index);
}

/*
 * Lookup or create the objects for a test using the od template.
 * If the objects do not all exist, or if 'remove' is specified,
 * remove any existing objects and create new ones.  Otherwise,
 * use the existing objects.
 */
static int
ztest_object_init(ztest_ds_t *zd, ztest_od_t *od, size_t size, boolean_t remove)
{
	int count = size / sizeof (*od);
	int rv = 0;

	mutex_enter(&zd->zd_dirobj_lock);
	if ((ztest_lookup(zd, od, count) != 0 || remove) &&
	    (ztest_remove(zd, od, count) != 0 ||
	    ztest_create(zd, od, count) != 0))
		rv = -1;
	zd->zd_od = od;
	mutex_exit(&zd->zd_dirobj_lock);

	return (rv);
}

/* ARGSUSED */
void
ztest_zil_commit(ztest_ds_t *zd, uint64_t id)
{
	zilog_t *zilog = zd->zd_zilog;

	rw_enter(&zd->zd_zilog_lock, RW_READER);

	zil_commit(zilog, ztest_random(ZTEST_OBJECTS));

	/*
	 * Remember the committed values in zd, which is in parent/child
	 * shared memory.  If we die, the next iteration of ztest_run()
	 * will verify that the log really does contain this record.
	 */
	mutex_enter(&zilog->zl_lock);
	ASSERT(zd->zd_shared != NULL);
	ASSERT3U(zd->zd_shared->zd_seq, <=, zilog->zl_commit_lr_seq);
	zd->zd_shared->zd_seq = zilog->zl_commit_lr_seq;
	mutex_exit(&zilog->zl_lock);

	rw_exit(&zd->zd_zilog_lock);
}

/*
 * This function is designed to simulate the operations that occur during a
 * mount/unmount operation.  We hold the dataset across these operations in an
 * attempt to expose any implicit assumptions about ZIL management.
 */
/* ARGSUSED */
void
ztest_zil_remount(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;

	/*
	 * We grab the zd_dirobj_lock to ensure that no other thread is
	 * updating the zil (i.e. adding in-memory log records) and the
	 * zd_zilog_lock to block any I/O.
	 */
	mutex_enter(&zd->zd_dirobj_lock);
	rw_enter(&zd->zd_zilog_lock, RW_WRITER);

	/* zfsvfs_teardown() */
	zil_close(zd->zd_zilog);

	/* zfsvfs_setup() */
	VERIFY(zil_open(os, ztest_get_data) == zd->zd_zilog);
	zil_replay(os, zd, ztest_replay_vector);

	rw_exit(&zd->zd_zilog_lock);
	mutex_exit(&zd->zd_dirobj_lock);
}

/*
 * Verify that we can't destroy an active pool, create an existing pool,
 * or create a pool with a bad vdev spec.
 */
/* ARGSUSED */
void
ztest_spa_create_destroy(ztest_ds_t *zd, uint64_t id)
{
	ztest_shared_opts_t *zo = &ztest_opts;
	spa_t *spa;
	nvlist_t *nvroot;

	/*
	 * Attempt to create using a bad file.
	 */
	nvroot = make_vdev_root("/dev/bogus", NULL, NULL, 0, 0, 0, 0, 0, 1);
	VERIFY3U(ENOENT, ==,
	    spa_create("ztest_bad_file", nvroot, NULL, NULL));
	nvlist_free(nvroot);

	/*
	 * Attempt to create using a bad mirror.
	 */
	nvroot = make_vdev_root("/dev/bogus", NULL, NULL, 0, 0, 0, 0, 2, 1);
	VERIFY3U(ENOENT, ==,
	    spa_create("ztest_bad_mirror", nvroot, NULL, NULL));
	nvlist_free(nvroot);

	/*
	 * Attempt to create an existing pool.  It shouldn't matter
	 * what's in the nvroot; we should fail with EEXIST.
	 */
	rw_enter(&ztest_name_lock, RW_READER);
	nvroot = make_vdev_root("/dev/bogus", NULL, NULL, 0, 0, 0, 0, 0, 1);
	VERIFY3U(EEXIST, ==, spa_create(zo->zo_pool, nvroot, NULL, NULL));
	nvlist_free(nvroot);
	VERIFY3U(0, ==, spa_open(zo->zo_pool, &spa, FTAG));
	VERIFY3U(EBUSY, ==, spa_destroy(zo->zo_pool));
	spa_close(spa, FTAG);

	rw_exit(&ztest_name_lock);
}

/* ARGSUSED */
void
ztest_spa_upgrade(ztest_ds_t *zd, uint64_t id)
{
	spa_t *spa;
	uint64_t initial_version = SPA_VERSION_INITIAL;
	uint64_t version, newversion;
	nvlist_t *nvroot, *props;
	char *name;

	mutex_enter(&ztest_vdev_lock);
	name = kmem_asprintf("%s_upgrade", ztest_opts.zo_pool);

	/*
	 * Clean up from previous runs.
	 */
	(void) spa_destroy(name);

	nvroot = make_vdev_root(NULL, NULL, name, ztest_opts.zo_vdev_size, 0,
	    0, ztest_opts.zo_raidz, ztest_opts.zo_mirrors, 1);

	/*
	 * If we're configuring a RAIDZ device then make sure that the
	 * the initial version is capable of supporting that feature.
	 */
	switch (ztest_opts.zo_raidz_parity) {
	case 0:
	case 1:
		initial_version = SPA_VERSION_INITIAL;
		break;
	case 2:
		initial_version = SPA_VERSION_RAIDZ2;
		break;
	case 3:
		initial_version = SPA_VERSION_RAIDZ3;
		break;
	}

	/*
	 * Create a pool with a spa version that can be upgraded. Pick
	 * a value between initial_version and SPA_VERSION_BEFORE_FEATURES.
	 */
	do {
		version = ztest_random_spa_version(initial_version);
	} while (version > SPA_VERSION_BEFORE_FEATURES);

	props = fnvlist_alloc();
	fnvlist_add_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_VERSION), version);
	VERIFY0(spa_create(name, nvroot, props, NULL));
	fnvlist_free(nvroot);
	fnvlist_free(props);

	VERIFY0(spa_open(name, &spa, FTAG));
	VERIFY3U(spa_version(spa), ==, version);
	newversion = ztest_random_spa_version(version + 1);

	if (ztest_opts.zo_verbose >= 4) {
		(void) printf("upgrading spa version from %llu to %llu\n",
		    (u_longlong_t)version, (u_longlong_t)newversion);
	}

	spa_upgrade(spa, newversion);
	VERIFY3U(spa_version(spa), >, version);
	VERIFY3U(spa_version(spa), ==, fnvlist_lookup_uint64(spa->spa_config,
	    zpool_prop_to_name(ZPOOL_PROP_VERSION)));
	spa_close(spa, FTAG);

	strfree(name);
	mutex_exit(&ztest_vdev_lock);
}

static void
ztest_spa_checkpoint(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&ztest_checkpoint_lock));

	int error = spa_checkpoint(spa->spa_name);

	switch (error) {
	case 0:
	case ZFS_ERR_DEVRM_IN_PROGRESS:
	case ZFS_ERR_DISCARDING_CHECKPOINT:
	case ZFS_ERR_CHECKPOINT_EXISTS:
		break;
	case ENOSPC:
		ztest_record_enospc(FTAG);
		break;
	default:
		fatal(0, "spa_checkpoint(%s) = %d", spa->spa_name, error);
	}
}

static void
ztest_spa_discard_checkpoint(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&ztest_checkpoint_lock));

	int error = spa_checkpoint_discard(spa->spa_name);

	switch (error) {
	case 0:
	case ZFS_ERR_DISCARDING_CHECKPOINT:
	case ZFS_ERR_NO_CHECKPOINT:
		break;
	default:
		fatal(0, "spa_discard_checkpoint(%s) = %d",
		    spa->spa_name, error);
	}

}

/* ARGSUSED */
void
ztest_spa_checkpoint_create_discard(ztest_ds_t *zd, uint64_t id)
{
	spa_t *spa = ztest_spa;

	mutex_enter(&ztest_checkpoint_lock);
	if (ztest_random(2) == 0) {
		ztest_spa_checkpoint(spa);
	} else {
		ztest_spa_discard_checkpoint(spa);
	}
	mutex_exit(&ztest_checkpoint_lock);
}


static vdev_t *
vdev_lookup_by_path(vdev_t *vd, const char *path)
{
	vdev_t *mvd;

	if (vd->vdev_path != NULL && strcmp(path, vd->vdev_path) == 0)
		return (vd);

	for (int c = 0; c < vd->vdev_children; c++)
		if ((mvd = vdev_lookup_by_path(vd->vdev_child[c], path)) !=
		    NULL)
			return (mvd);

	return (NULL);
}

/*
 * Find the first available hole which can be used as a top-level.
 */
int
find_vdev_hole(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	int c;

	ASSERT(spa_config_held(spa, SCL_VDEV, RW_READER) == SCL_VDEV);

	for (c = 0; c < rvd->vdev_children; c++) {
		vdev_t *cvd = rvd->vdev_child[c];

		if (cvd->vdev_ishole)
			break;
	}
	return (c);
}

/*
 * Verify that vdev_add() works as expected.
 */
/* ARGSUSED */
void
ztest_vdev_add_remove(ztest_ds_t *zd, uint64_t id)
{
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	uint64_t leaves;
	uint64_t guid;
	nvlist_t *nvroot;
	int error;

	mutex_enter(&ztest_vdev_lock);
	leaves = MAX(zs->zs_mirrors + zs->zs_splits, 1) * ztest_opts.zo_raidz;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	ztest_shared->zs_vdev_next_leaf = find_vdev_hole(spa) * leaves;

	/*
	 * If we have slogs then remove them 1/4 of the time.
	 */
	if (spa_has_slogs(spa) && ztest_random(4) == 0) {
		/*
		 * Grab the guid from the head of the log class rotor.
		 */
		guid = spa_log_class(spa)->mc_rotor->mg_vd->vdev_guid;

		spa_config_exit(spa, SCL_VDEV, FTAG);

		/*
		 * We have to grab the zs_name_lock as writer to
		 * prevent a race between removing a slog (dmu_objset_find)
		 * and destroying a dataset. Removing the slog will
		 * grab a reference on the dataset which may cause
		 * dmu_objset_destroy() to fail with EBUSY thus
		 * leaving the dataset in an inconsistent state.
		 */
		rw_enter(&ztest_name_lock, RW_WRITER);
		error = spa_vdev_remove(spa, guid, B_FALSE);
		rw_exit(&ztest_name_lock);

		switch (error) {
		case 0:
		case EEXIST:
		case ZFS_ERR_CHECKPOINT_EXISTS:
		case ZFS_ERR_DISCARDING_CHECKPOINT:
			break;
		default:
			fatal(0, "spa_vdev_remove() = %d", error);
		}
	} else {
		spa_config_exit(spa, SCL_VDEV, FTAG);

		/*
		 * Make 1/4 of the devices be log devices.
		 */
		nvroot = make_vdev_root(NULL, NULL, NULL,
		    ztest_opts.zo_vdev_size, 0,
		    ztest_random(4) == 0, ztest_opts.zo_raidz,
		    zs->zs_mirrors, 1);

		error = spa_vdev_add(spa, nvroot);
		nvlist_free(nvroot);

		switch (error) {
		case 0:
			break;
		case ENOSPC:
			ztest_record_enospc("spa_vdev_add");
			break;
		default:
			fatal(0, "spa_vdev_add() = %d", error);
		}
	}

	mutex_exit(&ztest_vdev_lock);
}

/*
 * Verify that adding/removing aux devices (l2arc, hot spare) works as expected.
 */
/* ARGSUSED */
void
ztest_vdev_aux_add_remove(ztest_ds_t *zd, uint64_t id)
{
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	spa_aux_vdev_t *sav;
	char *aux;
	uint64_t guid = 0;
	int error;

	if (ztest_random(2) == 0) {
		sav = &spa->spa_spares;
		aux = ZPOOL_CONFIG_SPARES;
	} else {
		sav = &spa->spa_l2cache;
		aux = ZPOOL_CONFIG_L2CACHE;
	}

	mutex_enter(&ztest_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	if (sav->sav_count != 0 && ztest_random(4) == 0) {
		/*
		 * Pick a random device to remove.
		 */
		guid = sav->sav_vdevs[ztest_random(sav->sav_count)]->vdev_guid;
	} else {
		/*
		 * Find an unused device we can add.
		 */
		zs->zs_vdev_aux = 0;
		for (;;) {
			char path[MAXPATHLEN];
			int c;
			(void) snprintf(path, sizeof (path), ztest_aux_template,
			    ztest_opts.zo_dir, ztest_opts.zo_pool, aux,
			    zs->zs_vdev_aux);
			for (c = 0; c < sav->sav_count; c++)
				if (strcmp(sav->sav_vdevs[c]->vdev_path,
				    path) == 0)
					break;
			if (c == sav->sav_count &&
			    vdev_lookup_by_path(rvd, path) == NULL)
				break;
			zs->zs_vdev_aux++;
		}
	}

	spa_config_exit(spa, SCL_VDEV, FTAG);

	if (guid == 0) {
		/*
		 * Add a new device.
		 */
		nvlist_t *nvroot = make_vdev_root(NULL, aux, NULL,
		    (ztest_opts.zo_vdev_size * 5) / 4, 0, 0, 0, 0, 1);
		error = spa_vdev_add(spa, nvroot);

		switch (error) {
		case 0:
			break;
		default:
			fatal(0, "spa_vdev_add(%p) = %d", nvroot, error);
		}
		nvlist_free(nvroot);
	} else {
		/*
		 * Remove an existing device.  Sometimes, dirty its
		 * vdev state first to make sure we handle removal
		 * of devices that have pending state changes.
		 */
		if (ztest_random(2) == 0)
			(void) vdev_online(spa, guid, 0, NULL);

		error = spa_vdev_remove(spa, guid, B_FALSE);

		switch (error) {
		case 0:
		case EBUSY:
		case ZFS_ERR_CHECKPOINT_EXISTS:
		case ZFS_ERR_DISCARDING_CHECKPOINT:
			break;
		default:
			fatal(0, "spa_vdev_remove(%llu) = %d", guid, error);
		}
	}

	mutex_exit(&ztest_vdev_lock);
}

/*
 * split a pool if it has mirror tlvdevs
 */
/* ARGSUSED */
void
ztest_split_pool(ztest_ds_t *zd, uint64_t id)
{
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	nvlist_t *tree, **child, *config, *split, **schild;
	uint_t c, children, schildren = 0, lastlogid = 0;
	int error = 0;

	mutex_enter(&ztest_vdev_lock);

	/* ensure we have a useable config; mirrors of raidz aren't supported */
	if (zs->zs_mirrors < 3 || ztest_opts.zo_raidz > 1) {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/* clean up the old pool, if any */
	(void) spa_destroy("splitp");

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	/* generate a config from the existing config */
	mutex_enter(&spa->spa_props_lock);
	VERIFY(nvlist_lookup_nvlist(spa->spa_config, ZPOOL_CONFIG_VDEV_TREE,
	    &tree) == 0);
	mutex_exit(&spa->spa_props_lock);

	VERIFY(nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) == 0);

	schild = malloc(rvd->vdev_children * sizeof (nvlist_t *));
	for (c = 0; c < children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		nvlist_t **mchild;
		uint_t mchildren;

		if (tvd->vdev_islog || tvd->vdev_ops == &vdev_hole_ops) {
			VERIFY(nvlist_alloc(&schild[schildren], NV_UNIQUE_NAME,
			    0) == 0);
			VERIFY(nvlist_add_string(schild[schildren],
			    ZPOOL_CONFIG_TYPE, VDEV_TYPE_HOLE) == 0);
			VERIFY(nvlist_add_uint64(schild[schildren],
			    ZPOOL_CONFIG_IS_HOLE, 1) == 0);
			if (lastlogid == 0)
				lastlogid = schildren;
			++schildren;
			continue;
		}
		lastlogid = 0;
		VERIFY(nvlist_lookup_nvlist_array(child[c],
		    ZPOOL_CONFIG_CHILDREN, &mchild, &mchildren) == 0);
		VERIFY(nvlist_dup(mchild[0], &schild[schildren++], 0) == 0);
	}

	/* OK, create a config that can be used to split */
	VERIFY(nvlist_alloc(&split, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(split, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_nvlist_array(split, ZPOOL_CONFIG_CHILDREN, schild,
	    lastlogid != 0 ? lastlogid : schildren) == 0);

	VERIFY(nvlist_alloc(&config, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, split) == 0);

	for (c = 0; c < schildren; c++)
		nvlist_free(schild[c]);
	free(schild);
	nvlist_free(split);

	spa_config_exit(spa, SCL_VDEV, FTAG);

	rw_enter(&ztest_name_lock, RW_WRITER);
	error = spa_vdev_split_mirror(spa, "splitp", config, NULL, B_FALSE);
	rw_exit(&ztest_name_lock);

	nvlist_free(config);

	if (error == 0) {
		(void) printf("successful split - results:\n");
		mutex_enter(&spa_namespace_lock);
		show_pool_stats(spa);
		show_pool_stats(spa_lookup("splitp"));
		mutex_exit(&spa_namespace_lock);
		++zs->zs_splits;
		--zs->zs_mirrors;
	}
	mutex_exit(&ztest_vdev_lock);
}

/*
 * Verify that we can attach and detach devices.
 */
/* ARGSUSED */
void
ztest_vdev_attach_detach(ztest_ds_t *zd, uint64_t id)
{
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	spa_aux_vdev_t *sav = &spa->spa_spares;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *oldvd, *newvd, *pvd;
	nvlist_t *root;
	uint64_t leaves;
	uint64_t leaf, top;
	uint64_t ashift = ztest_get_ashift();
	uint64_t oldguid, pguid;
	uint64_t oldsize, newsize;
	char oldpath[MAXPATHLEN], newpath[MAXPATHLEN];
	int replacing;
	int oldvd_has_siblings = B_FALSE;
	int newvd_is_spare = B_FALSE;
	int oldvd_is_log;
	int error, expected_error;

	mutex_enter(&ztest_vdev_lock);
	leaves = MAX(zs->zs_mirrors, 1) * ztest_opts.zo_raidz;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * If a vdev is in the process of being removed, its removal may
	 * finish while we are in progress, leading to an unexpected error
	 * value.  Don't bother trying to attach while we are in the middle
	 * of removal.
	 */
	if (ztest_device_removal_active) {
		spa_config_exit(spa, SCL_ALL, FTAG);
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * Decide whether to do an attach or a replace.
	 */
	replacing = ztest_random(2);

	/*
	 * Pick a random top-level vdev.
	 */
	top = ztest_random_vdev_top(spa, B_TRUE);

	/*
	 * Pick a random leaf within it.
	 */
	leaf = ztest_random(leaves);

	/*
	 * Locate this vdev.
	 */
	oldvd = rvd->vdev_child[top];
	if (zs->zs_mirrors >= 1) {
		ASSERT(oldvd->vdev_ops == &vdev_mirror_ops);
		ASSERT(oldvd->vdev_children >= zs->zs_mirrors);
		oldvd = oldvd->vdev_child[leaf / ztest_opts.zo_raidz];
	}
	if (ztest_opts.zo_raidz > 1) {
		ASSERT(oldvd->vdev_ops == &vdev_raidz_ops);
		ASSERT(oldvd->vdev_children == ztest_opts.zo_raidz);
		oldvd = oldvd->vdev_child[leaf % ztest_opts.zo_raidz];
	}

	/*
	 * If we're already doing an attach or replace, oldvd may be a
	 * mirror vdev -- in which case, pick a random child.
	 */
	while (oldvd->vdev_children != 0) {
		oldvd_has_siblings = B_TRUE;
		ASSERT(oldvd->vdev_children >= 2);
		oldvd = oldvd->vdev_child[ztest_random(oldvd->vdev_children)];
	}

	oldguid = oldvd->vdev_guid;
	oldsize = vdev_get_min_asize(oldvd);
	oldvd_is_log = oldvd->vdev_top->vdev_islog;
	(void) strcpy(oldpath, oldvd->vdev_path);
	pvd = oldvd->vdev_parent;
	pguid = pvd->vdev_guid;

	/*
	 * If oldvd has siblings, then half of the time, detach it.
	 */
	if (oldvd_has_siblings && ztest_random(2) == 0) {
		spa_config_exit(spa, SCL_ALL, FTAG);
		error = spa_vdev_detach(spa, oldguid, pguid, B_FALSE);
		if (error != 0 && error != ENODEV && error != EBUSY &&
		    error != ENOTSUP && error != ZFS_ERR_CHECKPOINT_EXISTS &&
		    error != ZFS_ERR_DISCARDING_CHECKPOINT)
			fatal(0, "detach (%s) returned %d", oldpath, error);
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * For the new vdev, choose with equal probability between the two
	 * standard paths (ending in either 'a' or 'b') or a random hot spare.
	 */
	if (sav->sav_count != 0 && ztest_random(3) == 0) {
		newvd = sav->sav_vdevs[ztest_random(sav->sav_count)];
		newvd_is_spare = B_TRUE;
		(void) strcpy(newpath, newvd->vdev_path);
	} else {
		(void) snprintf(newpath, sizeof (newpath), ztest_dev_template,
		    ztest_opts.zo_dir, ztest_opts.zo_pool,
		    top * leaves + leaf);
		if (ztest_random(2) == 0)
			newpath[strlen(newpath) - 1] = 'b';
		newvd = vdev_lookup_by_path(rvd, newpath);
	}

	if (newvd) {
		/*
		 * Reopen to ensure the vdev's asize field isn't stale.
		 */
		vdev_reopen(newvd);
		newsize = vdev_get_min_asize(newvd);
	} else {
		/*
		 * Make newsize a little bigger or smaller than oldsize.
		 * If it's smaller, the attach should fail.
		 * If it's larger, and we're doing a replace,
		 * we should get dynamic LUN growth when we're done.
		 */
		newsize = 10 * oldsize / (9 + ztest_random(3));
	}

	/*
	 * If pvd is not a mirror or root, the attach should fail with ENOTSUP,
	 * unless it's a replace; in that case any non-replacing parent is OK.
	 *
	 * If newvd is already part of the pool, it should fail with EBUSY.
	 *
	 * If newvd is too small, it should fail with EOVERFLOW.
	 */
	if (pvd->vdev_ops != &vdev_mirror_ops &&
	    pvd->vdev_ops != &vdev_root_ops && (!replacing ||
	    pvd->vdev_ops == &vdev_replacing_ops ||
	    pvd->vdev_ops == &vdev_spare_ops))
		expected_error = ENOTSUP;
	else if (newvd_is_spare && (!replacing || oldvd_is_log))
		expected_error = ENOTSUP;
	else if (newvd == oldvd)
		expected_error = replacing ? 0 : EBUSY;
	else if (vdev_lookup_by_path(rvd, newpath) != NULL)
		expected_error = EBUSY;
	else if (newsize < oldsize)
		expected_error = EOVERFLOW;
	else if (ashift > oldvd->vdev_top->vdev_ashift)
		expected_error = EDOM;
	else
		expected_error = 0;

	spa_config_exit(spa, SCL_ALL, FTAG);

	/*
	 * Build the nvlist describing newpath.
	 */
	root = make_vdev_root(newpath, NULL, NULL, newvd == NULL ? newsize : 0,
	    ashift, 0, 0, 0, 1);

	error = spa_vdev_attach(spa, oldguid, root, replacing);

	nvlist_free(root);

	/*
	 * If our parent was the replacing vdev, but the replace completed,
	 * then instead of failing with ENOTSUP we may either succeed,
	 * fail with ENODEV, or fail with EOVERFLOW.
	 */
	if (expected_error == ENOTSUP &&
	    (error == 0 || error == ENODEV || error == EOVERFLOW))
		expected_error = error;

	/*
	 * If someone grew the LUN, the replacement may be too small.
	 */
	if (error == EOVERFLOW || error == EBUSY)
		expected_error = error;

	if (error == ZFS_ERR_CHECKPOINT_EXISTS ||
	    error == ZFS_ERR_DISCARDING_CHECKPOINT)
		expected_error = error;

	/* XXX workaround 6690467 */
	if (error != expected_error && expected_error != EBUSY) {
		fatal(0, "attach (%s %llu, %s %llu, %d) "
		    "returned %d, expected %d",
		    oldpath, oldsize, newpath,
		    newsize, replacing, error, expected_error);
	}

	mutex_exit(&ztest_vdev_lock);
}

/* ARGSUSED */
void
ztest_device_removal(ztest_ds_t *zd, uint64_t id)
{
	spa_t *spa = ztest_spa;
	vdev_t *vd;
	uint64_t guid;
	int error;

	mutex_enter(&ztest_vdev_lock);

	if (ztest_device_removal_active) {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * Remove a random top-level vdev and wait for removal to finish.
	 */
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	vd = vdev_lookup_top(spa, ztest_random_vdev_top(spa, B_FALSE));
	guid = vd->vdev_guid;
	spa_config_exit(spa, SCL_VDEV, FTAG);

	error = spa_vdev_remove(spa, guid, B_FALSE);
	if (error == 0) {
		ztest_device_removal_active = B_TRUE;
		mutex_exit(&ztest_vdev_lock);

		while (spa->spa_vdev_removal != NULL)
			txg_wait_synced(spa_get_dsl(spa), 0);
	} else {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * The pool needs to be scrubbed after completing device removal.
	 * Failure to do so may result in checksum errors due to the
	 * strategy employed by ztest_fault_inject() when selecting which
	 * offset are redundant and can be damaged.
	 */
	error = spa_scan(spa, POOL_SCAN_SCRUB);
	if (error == 0) {
		while (dsl_scan_scrubbing(spa_get_dsl(spa)))
			txg_wait_synced(spa_get_dsl(spa), 0);
	}

	mutex_enter(&ztest_vdev_lock);
	ztest_device_removal_active = B_FALSE;
	mutex_exit(&ztest_vdev_lock);
}

/*
 * Callback function which expands the physical size of the vdev.
 */
vdev_t *
grow_vdev(vdev_t *vd, void *arg)
{
	spa_t *spa = vd->vdev_spa;
	size_t *newsize = arg;
	size_t fsize;
	int fd;

	ASSERT(spa_config_held(spa, SCL_STATE, RW_READER) == SCL_STATE);
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if ((fd = open(vd->vdev_path, O_RDWR)) == -1)
		return (vd);

	fsize = lseek(fd, 0, SEEK_END);
	(void) ftruncate(fd, *newsize);

	if (ztest_opts.zo_verbose >= 6) {
		(void) printf("%s grew from %lu to %lu bytes\n",
		    vd->vdev_path, (ulong_t)fsize, (ulong_t)*newsize);
	}
	(void) close(fd);
	return (NULL);
}

/*
 * Callback function which expands a given vdev by calling vdev_online().
 */
/* ARGSUSED */
vdev_t *
online_vdev(vdev_t *vd, void *arg)
{
	spa_t *spa = vd->vdev_spa;
	vdev_t *tvd = vd->vdev_top;
	uint64_t guid = vd->vdev_guid;
	uint64_t generation = spa->spa_config_generation + 1;
	vdev_state_t newstate = VDEV_STATE_UNKNOWN;
	int error;

	ASSERT(spa_config_held(spa, SCL_STATE, RW_READER) == SCL_STATE);
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	/* Calling vdev_online will initialize the new metaslabs */
	spa_config_exit(spa, SCL_STATE, spa);
	error = vdev_online(spa, guid, ZFS_ONLINE_EXPAND, &newstate);
	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	/*
	 * If vdev_online returned an error or the underlying vdev_open
	 * failed then we abort the expand. The only way to know that
	 * vdev_open fails is by checking the returned newstate.
	 */
	if (error || newstate != VDEV_STATE_HEALTHY) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("Unable to expand vdev, state %llu, "
			    "error %d\n", (u_longlong_t)newstate, error);
		}
		return (vd);
	}
	ASSERT3U(newstate, ==, VDEV_STATE_HEALTHY);

	/*
	 * Since we dropped the lock we need to ensure that we're
	 * still talking to the original vdev. It's possible this
	 * vdev may have been detached/replaced while we were
	 * trying to online it.
	 */
	if (generation != spa->spa_config_generation) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("vdev configuration has changed, "
			    "guid %llu, state %llu, expected gen %llu, "
			    "got gen %llu\n",
			    (u_longlong_t)guid,
			    (u_longlong_t)tvd->vdev_state,
			    (u_longlong_t)generation,
			    (u_longlong_t)spa->spa_config_generation);
		}
		return (vd);
	}
	return (NULL);
}

/*
 * Traverse the vdev tree calling the supplied function.
 * We continue to walk the tree until we either have walked all
 * children or we receive a non-NULL return from the callback.
 * If a NULL callback is passed, then we just return back the first
 * leaf vdev we encounter.
 */
vdev_t *
vdev_walk_tree(vdev_t *vd, vdev_t *(*func)(vdev_t *, void *), void *arg)
{
	if (vd->vdev_ops->vdev_op_leaf) {
		if (func == NULL)
			return (vd);
		else
			return (func(vd, arg));
	}

	for (uint_t c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		if ((cvd = vdev_walk_tree(cvd, func, arg)) != NULL)
			return (cvd);
	}
	return (NULL);
}

/*
 * Verify that dynamic LUN growth works as expected.
 */
/* ARGSUSED */
void
ztest_vdev_LUN_growth(ztest_ds_t *zd, uint64_t id)
{
	spa_t *spa = ztest_spa;
	vdev_t *vd, *tvd;
	metaslab_class_t *mc;
	metaslab_group_t *mg;
	size_t psize, newsize;
	uint64_t top;
	uint64_t old_class_space, new_class_space, old_ms_count, new_ms_count;

	mutex_enter(&ztest_checkpoint_lock);
	mutex_enter(&ztest_vdev_lock);
	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	/*
	 * If there is a vdev removal in progress, it could complete while
	 * we are running, in which case we would not be able to verify
	 * that the metaslab_class space increased (because it decreases
	 * when the device removal completes).
	 */
	if (ztest_device_removal_active) {
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}

	top = ztest_random_vdev_top(spa, B_TRUE);

	tvd = spa->spa_root_vdev->vdev_child[top];
	mg = tvd->vdev_mg;
	mc = mg->mg_class;
	old_ms_count = tvd->vdev_ms_count;
	old_class_space = metaslab_class_get_space(mc);

	/*
	 * Determine the size of the first leaf vdev associated with
	 * our top-level device.
	 */
	vd = vdev_walk_tree(tvd, NULL, NULL);
	ASSERT3P(vd, !=, NULL);
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	psize = vd->vdev_psize;

	/*
	 * We only try to expand the vdev if it's healthy, less than 4x its
	 * original size, and it has a valid psize.
	 */
	if (tvd->vdev_state != VDEV_STATE_HEALTHY ||
	    psize == 0 || psize >= 4 * ztest_opts.zo_vdev_size) {
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}
	ASSERT(psize > 0);
	newsize = psize + psize / 8;
	ASSERT3U(newsize, >, psize);

	if (ztest_opts.zo_verbose >= 6) {
		(void) printf("Expanding LUN %s from %lu to %lu\n",
		    vd->vdev_path, (ulong_t)psize, (ulong_t)newsize);
	}

	/*
	 * Growing the vdev is a two step process:
	 *	1). expand the physical size (i.e. relabel)
	 *	2). online the vdev to create the new metaslabs
	 */
	if (vdev_walk_tree(tvd, grow_vdev, &newsize) != NULL ||
	    vdev_walk_tree(tvd, online_vdev, NULL) != NULL ||
	    tvd->vdev_state != VDEV_STATE_HEALTHY) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("Could not expand LUN because "
			    "the vdev configuration changed.\n");
		}
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}

	spa_config_exit(spa, SCL_STATE, spa);

	/*
	 * Expanding the LUN will update the config asynchronously,
	 * thus we must wait for the async thread to complete any
	 * pending tasks before proceeding.
	 */
	for (;;) {
		boolean_t done;
		mutex_enter(&spa->spa_async_lock);
		done = (spa->spa_async_thread == NULL && !spa->spa_async_tasks);
		mutex_exit(&spa->spa_async_lock);
		if (done)
			break;
		txg_wait_synced(spa_get_dsl(spa), 0);
		(void) poll(NULL, 0, 100);
	}

	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	tvd = spa->spa_root_vdev->vdev_child[top];
	new_ms_count = tvd->vdev_ms_count;
	new_class_space = metaslab_class_get_space(mc);

	if (tvd->vdev_mg != mg || mg->mg_class != mc) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("Could not verify LUN expansion due to "
			    "intervening vdev offline or remove.\n");
		}
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}

	/*
	 * Make sure we were able to grow the vdev.
	 */
	if (new_ms_count <= old_ms_count) {
		fatal(0, "LUN expansion failed: ms_count %llu < %llu\n",
		    old_ms_count, new_ms_count);
	}

	/*
	 * Make sure we were able to grow the pool.
	 */
	if (new_class_space <= old_class_space) {
		fatal(0, "LUN expansion failed: class_space %llu < %llu\n",
		    old_class_space, new_class_space);
	}

	if (ztest_opts.zo_verbose >= 5) {
		char oldnumbuf[NN_NUMBUF_SZ], newnumbuf[NN_NUMBUF_SZ];

		nicenum(old_class_space, oldnumbuf, sizeof (oldnumbuf));
		nicenum(new_class_space, newnumbuf, sizeof (newnumbuf));
		(void) printf("%s grew from %s to %s\n",
		    spa->spa_name, oldnumbuf, newnumbuf);
	}

	spa_config_exit(spa, SCL_STATE, spa);
	mutex_exit(&ztest_vdev_lock);
	mutex_exit(&ztest_checkpoint_lock);
}

/*
 * Verify that dmu_objset_{create,destroy,open,close} work as expected.
 */
/* ARGSUSED */
static void
ztest_objset_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	/*
	 * Create the objects common to all ztest datasets.
	 */
	VERIFY(zap_create_claim(os, ZTEST_DIROBJ,
	    DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx) == 0);
}

static int
ztest_dataset_create(char *dsname)
{
	uint64_t zilset = ztest_random(100);
	int err = dmu_objset_create(dsname, DMU_OST_OTHER, 0,
	    ztest_objset_create_cb, NULL);

	if (err || zilset < 80)
		return (err);

	if (ztest_opts.zo_verbose >= 6)
		(void) printf("Setting dataset %s to sync always\n", dsname);
	return (ztest_dsl_prop_set_uint64(dsname, ZFS_PROP_SYNC,
	    ZFS_SYNC_ALWAYS, B_FALSE));
}

/* ARGSUSED */
static int
ztest_objset_destroy_cb(const char *name, void *arg)
{
	objset_t *os;
	dmu_object_info_t doi;
	int error;

	/*
	 * Verify that the dataset contains a directory object.
	 */
	VERIFY0(dmu_objset_own(name, DMU_OST_OTHER, B_TRUE, FTAG, &os));
	error = dmu_object_info(os, ZTEST_DIROBJ, &doi);
	if (error != ENOENT) {
		/* We could have crashed in the middle of destroying it */
		ASSERT0(error);
		ASSERT3U(doi.doi_type, ==, DMU_OT_ZAP_OTHER);
		ASSERT3S(doi.doi_physical_blocks_512, >=, 0);
	}
	dmu_objset_disown(os, FTAG);

	/*
	 * Destroy the dataset.
	 */
	if (strchr(name, '@') != NULL) {
		VERIFY0(dsl_destroy_snapshot(name, B_FALSE));
	} else {
		VERIFY0(dsl_destroy_head(name));
	}
	return (0);
}

static boolean_t
ztest_snapshot_create(char *osname, uint64_t id)
{
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	(void) snprintf(snapname, sizeof (snapname), "%llu", (u_longlong_t)id);

	error = dmu_objset_snapshot_one(osname, snapname);
	if (error == ENOSPC) {
		ztest_record_enospc(FTAG);
		return (B_FALSE);
	}
	if (error != 0 && error != EEXIST) {
		fatal(0, "ztest_snapshot_create(%s@%s) = %d", osname,
		    snapname, error);
	}
	return (B_TRUE);
}

static boolean_t
ztest_snapshot_destroy(char *osname, uint64_t id)
{
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	(void) snprintf(snapname, sizeof (snapname), "%s@%llu", osname,
	    (u_longlong_t)id);

	error = dsl_destroy_snapshot(snapname, B_FALSE);
	if (error != 0 && error != ENOENT)
		fatal(0, "ztest_snapshot_destroy(%s) = %d", snapname, error);
	return (B_TRUE);
}

/* ARGSUSED */
void
ztest_dmu_objset_create_destroy(ztest_ds_t *zd, uint64_t id)
{
	ztest_ds_t zdtmp;
	int iters;
	int error;
	objset_t *os, *os2;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	zilog_t *zilog;

	rw_enter(&ztest_name_lock, RW_READER);

	(void) snprintf(name, sizeof (name), "%s/temp_%llu",
	    ztest_opts.zo_pool, (u_longlong_t)id);

	/*
	 * If this dataset exists from a previous run, process its replay log
	 * half of the time.  If we don't replay it, then dmu_objset_destroy()
	 * (invoked from ztest_objset_destroy_cb()) should just throw it away.
	 */
	if (ztest_random(2) == 0 &&
	    dmu_objset_own(name, DMU_OST_OTHER, B_FALSE, FTAG, &os) == 0) {
		ztest_zd_init(&zdtmp, NULL, os);
		zil_replay(os, &zdtmp, ztest_replay_vector);
		ztest_zd_fini(&zdtmp);
		dmu_objset_disown(os, FTAG);
	}

	/*
	 * There may be an old instance of the dataset we're about to
	 * create lying around from a previous run.  If so, destroy it
	 * and all of its snapshots.
	 */
	(void) dmu_objset_find(name, ztest_objset_destroy_cb, NULL,
	    DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);

	/*
	 * Verify that the destroyed dataset is no longer in the namespace.
	 */
	VERIFY3U(ENOENT, ==, dmu_objset_own(name, DMU_OST_OTHER, B_TRUE,
	    FTAG, &os));

	/*
	 * Verify that we can create a new dataset.
	 */
	error = ztest_dataset_create(name);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			rw_exit(&ztest_name_lock);
			return;
		}
		fatal(0, "dmu_objset_create(%s) = %d", name, error);
	}

	VERIFY0(dmu_objset_own(name, DMU_OST_OTHER, B_FALSE, FTAG, &os));

	ztest_zd_init(&zdtmp, NULL, os);

	/*
	 * Open the intent log for it.
	 */
	zilog = zil_open(os, ztest_get_data);

	/*
	 * Put some objects in there, do a little I/O to them,
	 * and randomly take a couple of snapshots along the way.
	 */
	iters = ztest_random(5);
	for (int i = 0; i < iters; i++) {
		ztest_dmu_object_alloc_free(&zdtmp, id);
		if (ztest_random(iters) == 0)
			(void) ztest_snapshot_create(name, i);
	}

	/*
	 * Verify that we cannot create an existing dataset.
	 */
	VERIFY3U(EEXIST, ==,
	    dmu_objset_create(name, DMU_OST_OTHER, 0, NULL, NULL));

	/*
	 * Verify that we can hold an objset that is also owned.
	 */
	VERIFY3U(0, ==, dmu_objset_hold(name, FTAG, &os2));
	dmu_objset_rele(os2, FTAG);

	/*
	 * Verify that we cannot own an objset that is already owned.
	 */
	VERIFY3U(EBUSY, ==,
	    dmu_objset_own(name, DMU_OST_OTHER, B_FALSE, FTAG, &os2));

	zil_close(zilog);
	dmu_objset_disown(os, FTAG);
	ztest_zd_fini(&zdtmp);

	rw_exit(&ztest_name_lock);
}

/*
 * Verify that dmu_snapshot_{create,destroy,open,close} work as expected.
 */
void
ztest_dmu_snapshot_create_destroy(ztest_ds_t *zd, uint64_t id)
{
	rw_enter(&ztest_name_lock, RW_READER);
	(void) ztest_snapshot_destroy(zd->zd_name, id);
	(void) ztest_snapshot_create(zd->zd_name, id);
	rw_exit(&ztest_name_lock);
}

/*
 * Cleanup non-standard snapshots and clones.
 */
void
ztest_dsl_dataset_cleanup(char *osname, uint64_t id)
{
	char snap1name[ZFS_MAX_DATASET_NAME_LEN];
	char clone1name[ZFS_MAX_DATASET_NAME_LEN];
	char snap2name[ZFS_MAX_DATASET_NAME_LEN];
	char clone2name[ZFS_MAX_DATASET_NAME_LEN];
	char snap3name[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	(void) snprintf(snap1name, sizeof (snap1name),
	    "%s@s1_%llu", osname, id);
	(void) snprintf(clone1name, sizeof (clone1name),
	    "%s/c1_%llu", osname, id);
	(void) snprintf(snap2name, sizeof (snap2name),
	    "%s@s2_%llu", clone1name, id);
	(void) snprintf(clone2name, sizeof (clone2name),
	    "%s/c2_%llu", osname, id);
	(void) snprintf(snap3name, sizeof (snap3name),
	    "%s@s3_%llu", clone1name, id);

	error = dsl_destroy_head(clone2name);
	if (error && error != ENOENT)
		fatal(0, "dsl_destroy_head(%s) = %d", clone2name, error);
	error = dsl_destroy_snapshot(snap3name, B_FALSE);
	if (error && error != ENOENT)
		fatal(0, "dsl_destroy_snapshot(%s) = %d", snap3name, error);
	error = dsl_destroy_snapshot(snap2name, B_FALSE);
	if (error && error != ENOENT)
		fatal(0, "dsl_destroy_snapshot(%s) = %d", snap2name, error);
	error = dsl_destroy_head(clone1name);
	if (error && error != ENOENT)
		fatal(0, "dsl_destroy_head(%s) = %d", clone1name, error);
	error = dsl_destroy_snapshot(snap1name, B_FALSE);
	if (error && error != ENOENT)
		fatal(0, "dsl_destroy_snapshot(%s) = %d", snap1name, error);
}

/*
 * Verify dsl_dataset_promote handles EBUSY
 */
void
ztest_dsl_dataset_promote_busy(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os;
	char snap1name[ZFS_MAX_DATASET_NAME_LEN];
	char clone1name[ZFS_MAX_DATASET_NAME_LEN];
	char snap2name[ZFS_MAX_DATASET_NAME_LEN];
	char clone2name[ZFS_MAX_DATASET_NAME_LEN];
	char snap3name[ZFS_MAX_DATASET_NAME_LEN];
	char *osname = zd->zd_name;
	int error;

	rw_enter(&ztest_name_lock, RW_READER);

	ztest_dsl_dataset_cleanup(osname, id);

	(void) snprintf(snap1name, sizeof (snap1name),
	    "%s@s1_%llu", osname, id);
	(void) snprintf(clone1name, sizeof (clone1name),
	    "%s/c1_%llu", osname, id);
	(void) snprintf(snap2name, sizeof (snap2name),
	    "%s@s2_%llu", clone1name, id);
	(void) snprintf(clone2name, sizeof (clone2name),
	    "%s/c2_%llu", osname, id);
	(void) snprintf(snap3name, sizeof (snap3name),
	    "%s@s3_%llu", clone1name, id);

	error = dmu_objset_snapshot_one(osname, strchr(snap1name, '@') + 1);
	if (error && error != EEXIST) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(0, "dmu_take_snapshot(%s) = %d", snap1name, error);
	}

	error = dmu_objset_clone(clone1name, snap1name);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(0, "dmu_objset_create(%s) = %d", clone1name, error);
	}

	error = dmu_objset_snapshot_one(clone1name, strchr(snap2name, '@') + 1);
	if (error && error != EEXIST) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(0, "dmu_open_snapshot(%s) = %d", snap2name, error);
	}

	error = dmu_objset_snapshot_one(clone1name, strchr(snap3name, '@') + 1);
	if (error && error != EEXIST) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(0, "dmu_open_snapshot(%s) = %d", snap3name, error);
	}

	error = dmu_objset_clone(clone2name, snap3name);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(0, "dmu_objset_create(%s) = %d", clone2name, error);
	}

	error = dmu_objset_own(snap2name, DMU_OST_ANY, B_TRUE, FTAG, &os);
	if (error)
		fatal(0, "dmu_objset_own(%s) = %d", snap2name, error);
	error = dsl_dataset_promote(clone2name, NULL);
	if (error == ENOSPC) {
		dmu_objset_disown(os, FTAG);
		ztest_record_enospc(FTAG);
		goto out;
	}
	if (error != EBUSY)
		fatal(0, "dsl_dataset_promote(%s), %d, not EBUSY", clone2name,
		    error);
	dmu_objset_disown(os, FTAG);

out:
	ztest_dsl_dataset_cleanup(osname, id);

	rw_exit(&ztest_name_lock);
}

/*
 * Verify that dmu_object_{alloc,free} work as expected.
 */
void
ztest_dmu_object_alloc_free(ztest_ds_t *zd, uint64_t id)
{
	ztest_od_t od[4];
	int batchsize = sizeof (od) / sizeof (od[0]);

	for (int b = 0; b < batchsize; b++)
		ztest_od_init(&od[b], id, FTAG, b, DMU_OT_UINT64_OTHER, 0, 0, 0);

	/*
	 * Destroy the previous batch of objects, create a new batch,
	 * and do some I/O on the new objects.
	 */
	if (ztest_object_init(zd, od, sizeof (od), B_TRUE) != 0)
		return;

	while (ztest_random(4 * batchsize) != 0)
		ztest_io(zd, od[ztest_random(batchsize)].od_object,
		    ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);
}

/*
 * Verify that dmu_{read,write} work as expected.
 */
void
ztest_dmu_read_write(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t od[2];
	dmu_tx_t *tx;
	int i, freeit, error;
	uint64_t n, s, txg;
	bufwad_t *packbuf, *bigbuf, *pack, *bigH, *bigT;
	uint64_t packobj, packoff, packsize, bigobj, bigoff, bigsize;
	uint64_t chunksize = (1000 + ztest_random(1000)) * sizeof (uint64_t);
	uint64_t regions = 997;
	uint64_t stride = 123456789ULL;
	uint64_t width = 40;
	int free_percent = 5;

	/*
	 * This test uses two objects, packobj and bigobj, that are always
	 * updated together (i.e. in the same tx) so that their contents are
	 * in sync and can be compared.  Their contents relate to each other
	 * in a simple way: packobj is a dense array of 'bufwad' structures,
	 * while bigobj is a sparse array of the same bufwads.  Specifically,
	 * for any index n, there are three bufwads that should be identical:
	 *
	 *	packobj, at offset n * sizeof (bufwad_t)
	 *	bigobj, at the head of the nth chunk
	 *	bigobj, at the tail of the nth chunk
	 *
	 * The chunk size is arbitrary. It doesn't have to be a power of two,
	 * and it doesn't have any relation to the object blocksize.
	 * The only requirement is that it can hold at least two bufwads.
	 *
	 * Normally, we write the bufwad to each of these locations.
	 * However, free_percent of the time we instead write zeroes to
	 * packobj and perform a dmu_free_range() on bigobj.  By comparing
	 * bigobj to packobj, we can verify that the DMU is correctly
	 * tracking which parts of an object are allocated and free,
	 * and that the contents of the allocated blocks are correct.
	 */

	/*
	 * Read the directory info.  If it's the first time, set things up.
	 */
	ztest_od_init(&od[0], id, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, chunksize);
	ztest_od_init(&od[1], id, FTAG, 1, DMU_OT_UINT64_OTHER, 0, 0, chunksize);

	if (ztest_object_init(zd, od, sizeof (od), B_FALSE) != 0)
		return;

	bigobj = od[0].od_object;
	packobj = od[1].od_object;
	chunksize = od[0].od_gen;
	ASSERT(chunksize == od[1].od_gen);

	/*
	 * Prefetch a random chunk of the big object.
	 * Our aim here is to get some async reads in flight
	 * for blocks that we may free below; the DMU should
	 * handle this race correctly.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(2 * width - 1);
	dmu_prefetch(os, bigobj, 0, n * chunksize, s * chunksize,
	    ZIO_PRIORITY_SYNC_READ);

	/*
	 * Pick a random index and compute the offsets into packobj and bigobj.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(width - 1);

	packoff = n * sizeof (bufwad_t);
	packsize = s * sizeof (bufwad_t);

	bigoff = n * chunksize;
	bigsize = s * chunksize;

	packbuf = umem_alloc(packsize, UMEM_NOFAIL);
	bigbuf = umem_alloc(bigsize, UMEM_NOFAIL);

	/*
	 * free_percent of the time, free a range of bigobj rather than
	 * overwriting it.
	 */
	freeit = (ztest_random(100) < free_percent);

	/*
	 * Read the current contents of our objects.
	 */
	error = dmu_read(os, packobj, packoff, packsize, packbuf,
	    DMU_READ_PREFETCH);
	ASSERT0(error);
	error = dmu_read(os, bigobj, bigoff, bigsize, bigbuf,
	    DMU_READ_PREFETCH);
	ASSERT0(error);

	/*
	 * Get a tx for the mods to both packobj and bigobj.
	 */
	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, packobj, packoff, packsize);

	if (freeit)
		dmu_tx_hold_free(tx, bigobj, bigoff, bigsize);
	else
		dmu_tx_hold_write(tx, bigobj, bigoff, bigsize);

	/* This accounts for setting the checksum/compression. */
	dmu_tx_hold_bonus(tx, bigobj);

	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0) {
		umem_free(packbuf, packsize);
		umem_free(bigbuf, bigsize);
		return;
	}

	enum zio_checksum cksum;
	do {
		cksum = (enum zio_checksum)
		    ztest_random_dsl_prop(ZFS_PROP_CHECKSUM);
	} while (cksum >= ZIO_CHECKSUM_LEGACY_FUNCTIONS);
	dmu_object_set_checksum(os, bigobj, cksum, tx);

	enum zio_compress comp;
	do {
		comp = (enum zio_compress)
		    ztest_random_dsl_prop(ZFS_PROP_COMPRESSION);
	} while (comp >= ZIO_COMPRESS_LEGACY_FUNCTIONS);
	dmu_object_set_compress(os, bigobj, comp, tx);

	/*
	 * For each index from n to n + s, verify that the existing bufwad
	 * in packobj matches the bufwads at the head and tail of the
	 * corresponding chunk in bigobj.  Then update all three bufwads
	 * with the new values we want to write out.
	 */
	for (i = 0; i < s; i++) {
		/* LINTED */
		pack = (bufwad_t *)((char *)packbuf + i * sizeof (bufwad_t));
		/* LINTED */
		bigH = (bufwad_t *)((char *)bigbuf + i * chunksize);
		/* LINTED */
		bigT = (bufwad_t *)((char *)bigH + chunksize) - 1;

		ASSERT((uintptr_t)bigH - (uintptr_t)bigbuf < bigsize);
		ASSERT((uintptr_t)bigT - (uintptr_t)bigbuf < bigsize);

		if (pack->bw_txg > txg)
			fatal(0, "future leak: got %llx, open txg is %llx",
			    pack->bw_txg, txg);

		if (pack->bw_data != 0 && pack->bw_index != n + i)
			fatal(0, "wrong index: got %llx, wanted %llx+%llx",
			    pack->bw_index, n, i);

		if (bcmp(pack, bigH, sizeof (bufwad_t)) != 0)
			fatal(0, "pack/bigH mismatch in %p/%p", pack, bigH);

		if (bcmp(pack, bigT, sizeof (bufwad_t)) != 0)
			fatal(0, "pack/bigT mismatch in %p/%p", pack, bigT);

		if (freeit) {
			bzero(pack, sizeof (bufwad_t));
		} else {
			pack->bw_index = n + i;
			pack->bw_txg = txg;
			pack->bw_data = 1 + ztest_random(-2ULL);
		}
		*bigH = *pack;
		*bigT = *pack;
	}

	/*
	 * We've verified all the old bufwads, and made new ones.
	 * Now write them out.
	 */
	dmu_write(os, packobj, packoff, packsize, packbuf, tx);

	if (freeit) {
		if (ztest_opts.zo_verbose >= 7) {
			(void) printf("freeing offset %llx size %llx"
			    " txg %llx\n",
			    (u_longlong_t)bigoff,
			    (u_longlong_t)bigsize,
			    (u_longlong_t)txg);
		}
		VERIFY(0 == dmu_free_range(os, bigobj, bigoff, bigsize, tx));
	} else {
		if (ztest_opts.zo_verbose >= 7) {
			(void) printf("writing offset %llx size %llx"
			    " txg %llx\n",
			    (u_longlong_t)bigoff,
			    (u_longlong_t)bigsize,
			    (u_longlong_t)txg);
		}
		dmu_write(os, bigobj, bigoff, bigsize, bigbuf, tx);
	}

	dmu_tx_commit(tx);

	/*
	 * Sanity check the stuff we just wrote.
	 */
	{
		void *packcheck = umem_alloc(packsize, UMEM_NOFAIL);
		void *bigcheck = umem_alloc(bigsize, UMEM_NOFAIL);

		VERIFY(0 == dmu_read(os, packobj, packoff,
		    packsize, packcheck, DMU_READ_PREFETCH));
		VERIFY(0 == dmu_read(os, bigobj, bigoff,
		    bigsize, bigcheck, DMU_READ_PREFETCH));

		ASSERT(bcmp(packbuf, packcheck, packsize) == 0);
		ASSERT(bcmp(bigbuf, bigcheck, bigsize) == 0);

		umem_free(packcheck, packsize);
		umem_free(bigcheck, bigsize);
	}

	umem_free(packbuf, packsize);
	umem_free(bigbuf, bigsize);
}

void
compare_and_update_pbbufs(uint64_t s, bufwad_t *packbuf, bufwad_t *bigbuf,
    uint64_t bigsize, uint64_t n, uint64_t chunksize, uint64_t txg)
{
	uint64_t i;
	bufwad_t *pack;
	bufwad_t *bigH;
	bufwad_t *bigT;

	/*
	 * For each index from n to n + s, verify that the existing bufwad
	 * in packobj matches the bufwads at the head and tail of the
	 * corresponding chunk in bigobj.  Then update all three bufwads
	 * with the new values we want to write out.
	 */
	for (i = 0; i < s; i++) {
		/* LINTED */
		pack = (bufwad_t *)((char *)packbuf + i * sizeof (bufwad_t));
		/* LINTED */
		bigH = (bufwad_t *)((char *)bigbuf + i * chunksize);
		/* LINTED */
		bigT = (bufwad_t *)((char *)bigH + chunksize) - 1;

		ASSERT((uintptr_t)bigH - (uintptr_t)bigbuf < bigsize);
		ASSERT((uintptr_t)bigT - (uintptr_t)bigbuf < bigsize);

		if (pack->bw_txg > txg)
			fatal(0, "future leak: got %llx, open txg is %llx",
			    pack->bw_txg, txg);

		if (pack->bw_data != 0 && pack->bw_index != n + i)
			fatal(0, "wrong index: got %llx, wanted %llx+%llx",
			    pack->bw_index, n, i);

		if (bcmp(pack, bigH, sizeof (bufwad_t)) != 0)
			fatal(0, "pack/bigH mismatch in %p/%p", pack, bigH);

		if (bcmp(pack, bigT, sizeof (bufwad_t)) != 0)
			fatal(0, "pack/bigT mismatch in %p/%p", pack, bigT);

		pack->bw_index = n + i;
		pack->bw_txg = txg;
		pack->bw_data = 1 + ztest_random(-2ULL);

		*bigH = *pack;
		*bigT = *pack;
	}
}

void
ztest_dmu_read_write_zcopy(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t od[2];
	dmu_tx_t *tx;
	uint64_t i;
	int error;
	uint64_t n, s, txg;
	bufwad_t *packbuf, *bigbuf;
	uint64_t packobj, packoff, packsize, bigobj, bigoff, bigsize;
	uint64_t blocksize = ztest_random_blocksize();
	uint64_t chunksize = blocksize;
	uint64_t regions = 997;
	uint64_t stride = 123456789ULL;
	uint64_t width = 9;
	dmu_buf_t *bonus_db;
	arc_buf_t **bigbuf_arcbufs;
	dmu_object_info_t doi;

	/*
	 * This test uses two objects, packobj and bigobj, that are always
	 * updated together (i.e. in the same tx) so that their contents are
	 * in sync and can be compared.  Their contents relate to each other
	 * in a simple way: packobj is a dense array of 'bufwad' structures,
	 * while bigobj is a sparse array of the same bufwads.  Specifically,
	 * for any index n, there are three bufwads that should be identical:
	 *
	 *	packobj, at offset n * sizeof (bufwad_t)
	 *	bigobj, at the head of the nth chunk
	 *	bigobj, at the tail of the nth chunk
	 *
	 * The chunk size is set equal to bigobj block size so that
	 * dmu_assign_arcbuf() can be tested for object updates.
	 */

	/*
	 * Read the directory info.  If it's the first time, set things up.
	 */
	ztest_od_init(&od[0], id, FTAG, 0, DMU_OT_UINT64_OTHER, blocksize, 0, 0);
	ztest_od_init(&od[1], id, FTAG, 1, DMU_OT_UINT64_OTHER, 0, 0, chunksize);

	if (ztest_object_init(zd, od, sizeof (od), B_FALSE) != 0)
		return;

	bigobj = od[0].od_object;
	packobj = od[1].od_object;
	blocksize = od[0].od_blocksize;
	chunksize = blocksize;
	ASSERT(chunksize == od[1].od_gen);

	VERIFY(dmu_object_info(os, bigobj, &doi) == 0);
	VERIFY(ISP2(doi.doi_data_block_size));
	VERIFY(chunksize == doi.doi_data_block_size);
	VERIFY(chunksize >= 2 * sizeof (bufwad_t));

	/*
	 * Pick a random index and compute the offsets into packobj and bigobj.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(width - 1);

	packoff = n * sizeof (bufwad_t);
	packsize = s * sizeof (bufwad_t);

	bigoff = n * chunksize;
	bigsize = s * chunksize;

	packbuf = umem_zalloc(packsize, UMEM_NOFAIL);
	bigbuf = umem_zalloc(bigsize, UMEM_NOFAIL);

	VERIFY3U(0, ==, dmu_bonus_hold(os, bigobj, FTAG, &bonus_db));

	bigbuf_arcbufs = umem_zalloc(2 * s * sizeof (arc_buf_t *), UMEM_NOFAIL);

	/*
	 * Iteration 0 test zcopy for DB_UNCACHED dbufs.
	 * Iteration 1 test zcopy to already referenced dbufs.
	 * Iteration 2 test zcopy to dirty dbuf in the same txg.
	 * Iteration 3 test zcopy to dbuf dirty in previous txg.
	 * Iteration 4 test zcopy when dbuf is no longer dirty.
	 * Iteration 5 test zcopy when it can't be done.
	 * Iteration 6 one more zcopy write.
	 */
	for (i = 0; i < 7; i++) {
		uint64_t j;
		uint64_t off;

		/*
		 * In iteration 5 (i == 5) use arcbufs
		 * that don't match bigobj blksz to test
		 * dmu_assign_arcbuf() when it can't directly
		 * assign an arcbuf to a dbuf.
		 */
		for (j = 0; j < s; j++) {
			if (i != 5) {
				bigbuf_arcbufs[j] =
				    dmu_request_arcbuf(bonus_db, chunksize);
			} else {
				bigbuf_arcbufs[2 * j] =
				    dmu_request_arcbuf(bonus_db, chunksize / 2);
				bigbuf_arcbufs[2 * j + 1] =
				    dmu_request_arcbuf(bonus_db, chunksize / 2);
			}
		}

		/*
		 * Get a tx for the mods to both packobj and bigobj.
		 */
		tx = dmu_tx_create(os);

		dmu_tx_hold_write(tx, packobj, packoff, packsize);
		dmu_tx_hold_write(tx, bigobj, bigoff, bigsize);

		txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
		if (txg == 0) {
			umem_free(packbuf, packsize);
			umem_free(bigbuf, bigsize);
			for (j = 0; j < s; j++) {
				if (i != 5) {
					dmu_return_arcbuf(bigbuf_arcbufs[j]);
				} else {
					dmu_return_arcbuf(
					    bigbuf_arcbufs[2 * j]);
					dmu_return_arcbuf(
					    bigbuf_arcbufs[2 * j + 1]);
				}
			}
			umem_free(bigbuf_arcbufs, 2 * s * sizeof (arc_buf_t *));
			dmu_buf_rele(bonus_db, FTAG);
			return;
		}

		/*
		 * 50% of the time don't read objects in the 1st iteration to
		 * test dmu_assign_arcbuf() for the case when there're no
		 * existing dbufs for the specified offsets.
		 */
		if (i != 0 || ztest_random(2) != 0) {
			error = dmu_read(os, packobj, packoff,
			    packsize, packbuf, DMU_READ_PREFETCH);
			ASSERT0(error);
			error = dmu_read(os, bigobj, bigoff, bigsize,
			    bigbuf, DMU_READ_PREFETCH);
			ASSERT0(error);
		}
		compare_and_update_pbbufs(s, packbuf, bigbuf, bigsize,
		    n, chunksize, txg);

		/*
		 * We've verified all the old bufwads, and made new ones.
		 * Now write them out.
		 */
		dmu_write(os, packobj, packoff, packsize, packbuf, tx);
		if (ztest_opts.zo_verbose >= 7) {
			(void) printf("writing offset %llx size %llx"
			    " txg %llx\n",
			    (u_longlong_t)bigoff,
			    (u_longlong_t)bigsize,
			    (u_longlong_t)txg);
		}
		for (off = bigoff, j = 0; j < s; j++, off += chunksize) {
			dmu_buf_t *dbt;
			if (i != 5) {
				bcopy((caddr_t)bigbuf + (off - bigoff),
				    bigbuf_arcbufs[j]->b_data, chunksize);
			} else {
				bcopy((caddr_t)bigbuf + (off - bigoff),
				    bigbuf_arcbufs[2 * j]->b_data,
				    chunksize / 2);
				bcopy((caddr_t)bigbuf + (off - bigoff) +
				    chunksize / 2,
				    bigbuf_arcbufs[2 * j + 1]->b_data,
				    chunksize / 2);
			}

			if (i == 1) {
				VERIFY(dmu_buf_hold(os, bigobj, off,
				    FTAG, &dbt, DMU_READ_NO_PREFETCH) == 0);
			}
			if (i != 5) {
				dmu_assign_arcbuf(bonus_db, off,
				    bigbuf_arcbufs[j], tx);
			} else {
				dmu_assign_arcbuf(bonus_db, off,
				    bigbuf_arcbufs[2 * j], tx);
				dmu_assign_arcbuf(bonus_db,
				    off + chunksize / 2,
				    bigbuf_arcbufs[2 * j + 1], tx);
			}
			if (i == 1) {
				dmu_buf_rele(dbt, FTAG);
			}
		}
		dmu_tx_commit(tx);

		/*
		 * Sanity check the stuff we just wrote.
		 */
		{
			void *packcheck = umem_alloc(packsize, UMEM_NOFAIL);
			void *bigcheck = umem_alloc(bigsize, UMEM_NOFAIL);

			VERIFY(0 == dmu_read(os, packobj, packoff,
			    packsize, packcheck, DMU_READ_PREFETCH));
			VERIFY(0 == dmu_read(os, bigobj, bigoff,
			    bigsize, bigcheck, DMU_READ_PREFETCH));

			ASSERT(bcmp(packbuf, packcheck, packsize) == 0);
			ASSERT(bcmp(bigbuf, bigcheck, bigsize) == 0);

			umem_free(packcheck, packsize);
			umem_free(bigcheck, bigsize);
		}
		if (i == 2) {
			txg_wait_open(dmu_objset_pool(os), 0);
		} else if (i == 3) {
			txg_wait_synced(dmu_objset_pool(os), 0);
		}
	}

	dmu_buf_rele(bonus_db, FTAG);
	umem_free(packbuf, packsize);
	umem_free(bigbuf, bigsize);
	umem_free(bigbuf_arcbufs, 2 * s * sizeof (arc_buf_t *));
}

/* ARGSUSED */
void
ztest_dmu_write_parallel(ztest_ds_t *zd, uint64_t id)
{
	ztest_od_t od[1];
	uint64_t offset = (1ULL << (ztest_random(20) + 43)) +
	    (ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);

	/*
	 * Have multiple threads write to large offsets in an object
	 * to verify that parallel writes to an object -- even to the
	 * same blocks within the object -- doesn't cause any trouble.
	 */
	ztest_od_init(&od[0], ID_PARALLEL, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (od), B_FALSE) != 0)
		return;

	while (ztest_random(10) != 0)
		ztest_io(zd, od[0].od_object, offset);
}

void
ztest_dmu_prealloc(ztest_ds_t *zd, uint64_t id)
{
	ztest_od_t od[1];
	uint64_t offset = (1ULL << (ztest_random(4) + SPA_MAXBLOCKSHIFT)) +
	    (ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);
	uint64_t count = ztest_random(20) + 1;
	uint64_t blocksize = ztest_random_blocksize();
	void *data;

	ztest_od_init(&od[0], id, FTAG, 0, DMU_OT_UINT64_OTHER, blocksize, 0, 0);

	if (ztest_object_init(zd, od, sizeof (od), !ztest_random(2)) != 0)
		return;

	if (ztest_truncate(zd, od[0].od_object, offset, count * blocksize) != 0)
		return;

	ztest_prealloc(zd, od[0].od_object, offset, count * blocksize);

	data = umem_zalloc(blocksize, UMEM_NOFAIL);

	while (ztest_random(count) != 0) {
		uint64_t randoff = offset + (ztest_random(count) * blocksize);
		if (ztest_write(zd, od[0].od_object, randoff, blocksize,
		    data) != 0)
			break;
		while (ztest_random(4) != 0)
			ztest_io(zd, od[0].od_object, randoff);
	}

	umem_free(data, blocksize);
}

/*
 * Verify that zap_{create,destroy,add,remove,update} work as expected.
 */
#define	ZTEST_ZAP_MIN_INTS	1
#define	ZTEST_ZAP_MAX_INTS	4
#define	ZTEST_ZAP_MAX_PROPS	1000

void
ztest_zap(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t od[1];
	uint64_t object;
	uint64_t txg, last_txg;
	uint64_t value[ZTEST_ZAP_MAX_INTS];
	uint64_t zl_ints, zl_intsize, prop;
	int i, ints;
	dmu_tx_t *tx;
	char propname[100], txgname[100];
	int error;
	char *hc[2] = { "s.acl.h", ".s.open.h.hyLZlg" };

	ztest_od_init(&od[0], id, FTAG, 0, DMU_OT_ZAP_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (od), !ztest_random(2)) != 0)
		return;

	object = od[0].od_object;

	/*
	 * Generate a known hash collision, and verify that
	 * we can lookup and remove both entries.
	 */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0)
		return;
	for (i = 0; i < 2; i++) {
		value[i] = i;
		VERIFY3U(0, ==, zap_add(os, object, hc[i], sizeof (uint64_t),
		    1, &value[i], tx));
	}
	for (i = 0; i < 2; i++) {
		VERIFY3U(EEXIST, ==, zap_add(os, object, hc[i],
		    sizeof (uint64_t), 1, &value[i], tx));
		VERIFY3U(0, ==,
		    zap_length(os, object, hc[i], &zl_intsize, &zl_ints));
		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, 1);
	}
	for (i = 0; i < 2; i++) {
		VERIFY3U(0, ==, zap_remove(os, object, hc[i], tx));
	}
	dmu_tx_commit(tx);

	/*
	 * Generate a buch of random entries.
	 */
	ints = MAX(ZTEST_ZAP_MIN_INTS, object % ZTEST_ZAP_MAX_INTS);

	prop = ztest_random(ZTEST_ZAP_MAX_PROPS);
	(void) sprintf(propname, "prop_%llu", (u_longlong_t)prop);
	(void) sprintf(txgname, "txg_%llu", (u_longlong_t)prop);
	bzero(value, sizeof (value));
	last_txg = 0;

	/*
	 * If these zap entries already exist, validate their contents.
	 */
	error = zap_length(os, object, txgname, &zl_intsize, &zl_ints);
	if (error == 0) {
		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, 1);

		VERIFY(zap_lookup(os, object, txgname, zl_intsize,
		    zl_ints, &last_txg) == 0);

		VERIFY(zap_length(os, object, propname, &zl_intsize,
		    &zl_ints) == 0);

		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, ints);

		VERIFY(zap_lookup(os, object, propname, zl_intsize,
		    zl_ints, value) == 0);

		for (i = 0; i < ints; i++) {
			ASSERT3U(value[i], ==, last_txg + object + i);
		}
	} else {
		ASSERT3U(error, ==, ENOENT);
	}

	/*
	 * Atomically update two entries in our zap object.
	 * The first is named txg_%llu, and contains the txg
	 * in which the property was last updated.  The second
	 * is named prop_%llu, and the nth element of its value
	 * should be txg + object + n.
	 */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0)
		return;

	if (last_txg > txg)
		fatal(0, "zap future leak: old %llu new %llu", last_txg, txg);

	for (i = 0; i < ints; i++)
		value[i] = txg + object + i;

	VERIFY3U(0, ==, zap_update(os, object, txgname, sizeof (uint64_t),
	    1, &txg, tx));
	VERIFY3U(0, ==, zap_update(os, object, propname, sizeof (uint64_t),
	    ints, value, tx));

	dmu_tx_commit(tx);

	/*
	 * Remove a random pair of entries.
	 */
	prop = ztest_random(ZTEST_ZAP_MAX_PROPS);
	(void) sprintf(propname, "prop_%llu", (u_longlong_t)prop);
	(void) sprintf(txgname, "txg_%llu", (u_longlong_t)prop);

	error = zap_length(os, object, txgname, &zl_intsize, &zl_ints);

	if (error == ENOENT)
		return;

	ASSERT0(error);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0)
		return;
	VERIFY3U(0, ==, zap_remove(os, object, txgname, tx));
	VERIFY3U(0, ==, zap_remove(os, object, propname, tx));
	dmu_tx_commit(tx);
}

/*
 * Testcase to test the upgrading of a microzap to fatzap.
 */
void
ztest_fzap(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t od[1];
	uint64_t object, txg;

	ztest_od_init(&od[0], id, FTAG, 0, DMU_OT_ZAP_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (od), !ztest_random(2)) != 0)
		return;

	object = od[0].od_object;

	/*
	 * Add entries to this ZAP and make sure it spills over
	 * and gets upgraded to a fatzap. Also, since we are adding
	 * 2050 entries we should see ptrtbl growth and leaf-block split.
	 */
	for (int i = 0; i < 2050; i++) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		uint64_t value = i;
		dmu_tx_t *tx;
		int error;

		(void) snprintf(name, sizeof (name), "fzap-%llu-%llu",
		    id, value);

		tx = dmu_tx_create(os);
		dmu_tx_hold_zap(tx, object, B_TRUE, name);
		txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
		if (txg == 0)
			return;
		error = zap_add(os, object, name, sizeof (uint64_t), 1,
		    &value, tx);
		ASSERT(error == 0 || error == EEXIST);
		dmu_tx_commit(tx);
	}
}

/* ARGSUSED */
void
ztest_zap_parallel(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t od[1];
	uint64_t txg, object, count, wsize, wc, zl_wsize, zl_wc;
	dmu_tx_t *tx;
	int i, namelen, error;
	int micro = ztest_random(2);
	char name[20], string_value[20];
	void *data;

	ztest_od_init(&od[0], ID_PARALLEL, FTAG, micro, DMU_OT_ZAP_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (od), B_FALSE) != 0)
		return;

	object = od[0].od_object;

	/*
	 * Generate a random name of the form 'xxx.....' where each
	 * x is a random printable character and the dots are dots.
	 * There are 94 such characters, and the name length goes from
	 * 6 to 20, so there are 94^3 * 15 = 12,458,760 possible names.
	 */
	namelen = ztest_random(sizeof (name) - 5) + 5 + 1;

	for (i = 0; i < 3; i++)
		name[i] = '!' + ztest_random('~' - '!' + 1);
	for (; i < namelen - 1; i++)
		name[i] = '.';
	name[i] = '\0';

	if ((namelen & 1) || micro) {
		wsize = sizeof (txg);
		wc = 1;
		data = &txg;
	} else {
		wsize = 1;
		wc = namelen;
		data = string_value;
	}

	count = -1ULL;
	VERIFY0(zap_count(os, object, &count));
	ASSERT(count != -1ULL);

	/*
	 * Select an operation: length, lookup, add, update, remove.
	 */
	i = ztest_random(5);

	if (i >= 2) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
		txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
		if (txg == 0)
			return;
		bcopy(name, string_value, namelen);
	} else {
		tx = NULL;
		txg = 0;
		bzero(string_value, namelen);
	}

	switch (i) {

	case 0:
		error = zap_length(os, object, name, &zl_wsize, &zl_wc);
		if (error == 0) {
			ASSERT3U(wsize, ==, zl_wsize);
			ASSERT3U(wc, ==, zl_wc);
		} else {
			ASSERT3U(error, ==, ENOENT);
		}
		break;

	case 1:
		error = zap_lookup(os, object, name, wsize, wc, data);
		if (error == 0) {
			if (data == string_value &&
			    bcmp(name, data, namelen) != 0)
				fatal(0, "name '%s' != val '%s' len %d",
				    name, data, namelen);
		} else {
			ASSERT3U(error, ==, ENOENT);
		}
		break;

	case 2:
		error = zap_add(os, object, name, wsize, wc, data, tx);
		ASSERT(error == 0 || error == EEXIST);
		break;

	case 3:
		VERIFY(zap_update(os, object, name, wsize, wc, data, tx) == 0);
		break;

	case 4:
		error = zap_remove(os, object, name, tx);
		ASSERT(error == 0 || error == ENOENT);
		break;
	}

	if (tx != NULL)
		dmu_tx_commit(tx);
}

/*
 * Commit callback data.
 */
typedef struct ztest_cb_data {
	list_node_t		zcd_node;
	uint64_t		zcd_txg;
	int			zcd_expected_err;
	boolean_t		zcd_added;
	boolean_t		zcd_called;
	spa_t			*zcd_spa;
} ztest_cb_data_t;

/* This is the actual commit callback function */
static void
ztest_commit_callback(void *arg, int error)
{
	ztest_cb_data_t *data = arg;
	uint64_t synced_txg;

	VERIFY(data != NULL);
	VERIFY3S(data->zcd_expected_err, ==, error);
	VERIFY(!data->zcd_called);

	synced_txg = spa_last_synced_txg(data->zcd_spa);
	if (data->zcd_txg > synced_txg)
		fatal(0, "commit callback of txg %" PRIu64 " called prematurely"
		    ", last synced txg = %" PRIu64 "\n", data->zcd_txg,
		    synced_txg);

	data->zcd_called = B_TRUE;

	if (error == ECANCELED) {
		ASSERT0(data->zcd_txg);
		ASSERT(!data->zcd_added);

		/*
		 * The private callback data should be destroyed here, but
		 * since we are going to check the zcd_called field after
		 * dmu_tx_abort(), we will destroy it there.
		 */
		return;
	}

	/* Was this callback added to the global callback list? */
	if (!data->zcd_added)
		goto out;

	ASSERT3U(data->zcd_txg, !=, 0);

	/* Remove our callback from the list */
	mutex_enter(&zcl.zcl_callbacks_lock);
	list_remove(&zcl.zcl_callbacks, data);
	mutex_exit(&zcl.zcl_callbacks_lock);

out:
	umem_free(data, sizeof (ztest_cb_data_t));
}

/* Allocate and initialize callback data structure */
static ztest_cb_data_t *
ztest_create_cb_data(objset_t *os, uint64_t txg)
{
	ztest_cb_data_t *cb_data;

	cb_data = umem_zalloc(sizeof (ztest_cb_data_t), UMEM_NOFAIL);

	cb_data->zcd_txg = txg;
	cb_data->zcd_spa = dmu_objset_spa(os);

	return (cb_data);
}

/*
 * If a number of txgs equal to this threshold have been created after a commit
 * callback has been registered but not called, then we assume there is an
 * implementation bug.
 */
#define	ZTEST_COMMIT_CALLBACK_THRESH	(TXG_CONCURRENT_STATES + 2)

/*
 * Commit callback test.
 */
void
ztest_dmu_commit_callbacks(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t od[1];
	dmu_tx_t *tx;
	ztest_cb_data_t *cb_data[3], *tmp_cb;
	uint64_t old_txg, txg;
	int i, error;

	ztest_od_init(&od[0], id, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (od), B_FALSE) != 0)
		return;

	tx = dmu_tx_create(os);

	cb_data[0] = ztest_create_cb_data(os, 0);
	dmu_tx_callback_register(tx, ztest_commit_callback, cb_data[0]);

	dmu_tx_hold_write(tx, od[0].od_object, 0, sizeof (uint64_t));

	/* Every once in a while, abort the transaction on purpose */
	if (ztest_random(100) == 0)
		error = -1;

	if (!error)
		error = dmu_tx_assign(tx, TXG_NOWAIT);

	txg = error ? 0 : dmu_tx_get_txg(tx);

	cb_data[0]->zcd_txg = txg;
	cb_data[1] = ztest_create_cb_data(os, txg);
	dmu_tx_callback_register(tx, ztest_commit_callback, cb_data[1]);

	if (error) {
		/*
		 * It's not a strict requirement to call the registered
		 * callbacks from inside dmu_tx_abort(), but that's what
		 * it's supposed to happen in the current implementation
		 * so we will check for that.
		 */
		for (i = 0; i < 2; i++) {
			cb_data[i]->zcd_expected_err = ECANCELED;
			VERIFY(!cb_data[i]->zcd_called);
		}

		dmu_tx_abort(tx);

		for (i = 0; i < 2; i++) {
			VERIFY(cb_data[i]->zcd_called);
			umem_free(cb_data[i], sizeof (ztest_cb_data_t));
		}

		return;
	}

	cb_data[2] = ztest_create_cb_data(os, txg);
	dmu_tx_callback_register(tx, ztest_commit_callback, cb_data[2]);

	/*
	 * Read existing data to make sure there isn't a future leak.
	 */
	VERIFY(0 == dmu_read(os, od[0].od_object, 0, sizeof (uint64_t),
	    &old_txg, DMU_READ_PREFETCH));

	if (old_txg > txg)
		fatal(0, "future leak: got %" PRIu64 ", open txg is %" PRIu64,
		    old_txg, txg);

	dmu_write(os, od[0].od_object, 0, sizeof (uint64_t), &txg, tx);

	mutex_enter(&zcl.zcl_callbacks_lock);

	/*
	 * Since commit callbacks don't have any ordering requirement and since
	 * it is theoretically possible for a commit callback to be called
	 * after an arbitrary amount of time has elapsed since its txg has been
	 * synced, it is difficult to reliably determine whether a commit
	 * callback hasn't been called due to high load or due to a flawed
	 * implementation.
	 *
	 * In practice, we will assume that if after a certain number of txgs a
	 * commit callback hasn't been called, then most likely there's an
	 * implementation bug..
	 */
	tmp_cb = list_head(&zcl.zcl_callbacks);
	if (tmp_cb != NULL &&
	    (txg - ZTEST_COMMIT_CALLBACK_THRESH) > tmp_cb->zcd_txg) {
		fatal(0, "Commit callback threshold exceeded, oldest txg: %"
		    PRIu64 ", open txg: %" PRIu64 "\n", tmp_cb->zcd_txg, txg);
	}

	/*
	 * Let's find the place to insert our callbacks.
	 *
	 * Even though the list is ordered by txg, it is possible for the
	 * insertion point to not be the end because our txg may already be
	 * quiescing at this point and other callbacks in the open txg
	 * (from other objsets) may have sneaked in.
	 */
	tmp_cb = list_tail(&zcl.zcl_callbacks);
	while (tmp_cb != NULL && tmp_cb->zcd_txg > txg)
		tmp_cb = list_prev(&zcl.zcl_callbacks, tmp_cb);

	/* Add the 3 callbacks to the list */
	for (i = 0; i < 3; i++) {
		if (tmp_cb == NULL)
			list_insert_head(&zcl.zcl_callbacks, cb_data[i]);
		else
			list_insert_after(&zcl.zcl_callbacks, tmp_cb,
			    cb_data[i]);

		cb_data[i]->zcd_added = B_TRUE;
		VERIFY(!cb_data[i]->zcd_called);

		tmp_cb = cb_data[i];
	}

	mutex_exit(&zcl.zcl_callbacks_lock);

	dmu_tx_commit(tx);
}

/*
 * Visit each object in the dataset. Verify that its properties
 * are consistent what was stored in the block tag when it was created,
 * and that its unused bonus buffer space has not been overwritten.
 */
void
ztest_verify_dnode_bt(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	uint64_t obj;
	int err = 0;

	for (obj = 0; err == 0; err = dmu_object_next(os, &obj, FALSE, 0)) {
		ztest_block_tag_t *bt = NULL;
		dmu_object_info_t doi;
		dmu_buf_t *db;

		if (dmu_bonus_hold(os, obj, FTAG, &db) != 0)
			continue;

		dmu_object_info_from_db(db, &doi);
		if (doi.doi_bonus_size >= sizeof (*bt))
			bt = ztest_bt_bonus(db);

		if (bt && bt->bt_magic == BT_MAGIC) {
			ztest_bt_verify(bt, os, obj, doi.doi_dnodesize,
			    bt->bt_offset, bt->bt_gen, bt->bt_txg,
			    bt->bt_crtxg);
			ztest_verify_unused_bonus(db, bt, obj, os, bt->bt_gen);
		}

		dmu_buf_rele(db, FTAG);
	}
}

/* ARGSUSED */
void
ztest_dsl_prop_get_set(ztest_ds_t *zd, uint64_t id)
{
	zfs_prop_t proplist[] = {
		ZFS_PROP_CHECKSUM,
		ZFS_PROP_COMPRESSION,
		ZFS_PROP_COPIES,
		ZFS_PROP_DEDUP
	};

	rw_enter(&ztest_name_lock, RW_READER);

	for (int p = 0; p < sizeof (proplist) / sizeof (proplist[0]); p++)
		(void) ztest_dsl_prop_set_uint64(zd->zd_name, proplist[p],
		    ztest_random_dsl_prop(proplist[p]), (int)ztest_random(2));

	rw_exit(&ztest_name_lock);
}

/* ARGSUSED */
void
ztest_remap_blocks(ztest_ds_t *zd, uint64_t id)
{
	rw_enter(&ztest_name_lock, RW_READER);

	int error = dmu_objset_remap_indirects(zd->zd_name);
	if (error == ENOSPC)
		error = 0;
	ASSERT0(error);

	rw_exit(&ztest_name_lock);
}

/* ARGSUSED */
void
ztest_spa_prop_get_set(ztest_ds_t *zd, uint64_t id)
{
	nvlist_t *props = NULL;

	rw_enter(&ztest_name_lock, RW_READER);

	(void) ztest_spa_prop_set_uint64(ZPOOL_PROP_DEDUPDITTO,
	    ZIO_DEDUPDITTO_MIN + ztest_random(ZIO_DEDUPDITTO_MIN));

	VERIFY0(spa_prop_get(ztest_spa, &props));

	if (ztest_opts.zo_verbose >= 6)
		dump_nvlist(props, 4);

	nvlist_free(props);

	rw_exit(&ztest_name_lock);
}

static int
user_release_one(const char *snapname, const char *holdname)
{
	nvlist_t *snaps, *holds;
	int error;

	snaps = fnvlist_alloc();
	holds = fnvlist_alloc();
	fnvlist_add_boolean(holds, holdname);
	fnvlist_add_nvlist(snaps, snapname, holds);
	fnvlist_free(holds);
	error = dsl_dataset_user_release(snaps, NULL);
	fnvlist_free(snaps);
	return (error);
}

/*
 * Test snapshot hold/release and deferred destroy.
 */
void
ztest_dmu_snapshot_hold(ztest_ds_t *zd, uint64_t id)
{
	int error;
	objset_t *os = zd->zd_os;
	objset_t *origin;
	char snapname[100];
	char fullname[100];
	char clonename[100];
	char tag[100];
	char osname[ZFS_MAX_DATASET_NAME_LEN];
	nvlist_t *holds;

	rw_enter(&ztest_name_lock, RW_READER);

	dmu_objset_name(os, osname);

	(void) snprintf(snapname, sizeof (snapname), "sh1_%llu", id);
	(void) snprintf(fullname, sizeof (fullname), "%s@%s", osname, snapname);
	(void) snprintf(clonename, sizeof (clonename),
	    "%s/ch1_%llu", osname, id);
	(void) snprintf(tag, sizeof (tag), "tag_%llu", id);

	/*
	 * Clean up from any previous run.
	 */
	error = dsl_destroy_head(clonename);
	if (error != ENOENT)
		ASSERT0(error);
	error = user_release_one(fullname, tag);
	if (error != ESRCH && error != ENOENT)
		ASSERT0(error);
	error = dsl_destroy_snapshot(fullname, B_FALSE);
	if (error != ENOENT)
		ASSERT0(error);

	/*
	 * Create snapshot, clone it, mark snap for deferred destroy,
	 * destroy clone, verify snap was also destroyed.
	 */
	error = dmu_objset_snapshot_one(osname, snapname);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc("dmu_objset_snapshot");
			goto out;
		}
		fatal(0, "dmu_objset_snapshot(%s) = %d", fullname, error);
	}

	error = dmu_objset_clone(clonename, fullname);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc("dmu_objset_clone");
			goto out;
		}
		fatal(0, "dmu_objset_clone(%s) = %d", clonename, error);
	}

	error = dsl_destroy_snapshot(fullname, B_TRUE);
	if (error) {
		fatal(0, "dsl_destroy_snapshot(%s, B_TRUE) = %d",
		    fullname, error);
	}

	error = dsl_destroy_head(clonename);
	if (error)
		fatal(0, "dsl_destroy_head(%s) = %d", clonename, error);

	error = dmu_objset_hold(fullname, FTAG, &origin);
	if (error != ENOENT)
		fatal(0, "dmu_objset_hold(%s) = %d", fullname, error);

	/*
	 * Create snapshot, add temporary hold, verify that we can't
	 * destroy a held snapshot, mark for deferred destroy,
	 * release hold, verify snapshot was destroyed.
	 */
	error = dmu_objset_snapshot_one(osname, snapname);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc("dmu_objset_snapshot");
			goto out;
		}
		fatal(0, "dmu_objset_snapshot(%s) = %d", fullname, error);
	}

	holds = fnvlist_alloc();
	fnvlist_add_string(holds, fullname, tag);
	error = dsl_dataset_user_hold(holds, 0, NULL);
	fnvlist_free(holds);

	if (error == ENOSPC) {
		ztest_record_enospc("dsl_dataset_user_hold");
		goto out;
	} else if (error) {
		fatal(0, "dsl_dataset_user_hold(%s, %s) = %u",
		    fullname, tag, error);
	}

	error = dsl_destroy_snapshot(fullname, B_FALSE);
	if (error != EBUSY) {
		fatal(0, "dsl_destroy_snapshot(%s, B_FALSE) = %d",
		    fullname, error);
	}

	error = dsl_destroy_snapshot(fullname, B_TRUE);
	if (error) {
		fatal(0, "dsl_destroy_snapshot(%s, B_TRUE) = %d",
		    fullname, error);
	}

	error = user_release_one(fullname, tag);
	if (error)
		fatal(0, "user_release_one(%s, %s) = %d", fullname, tag, error);

	VERIFY3U(dmu_objset_hold(fullname, FTAG, &origin), ==, ENOENT);

out:
	rw_exit(&ztest_name_lock);
}

/*
 * Inject random faults into the on-disk data.
 */
/* ARGSUSED */
void
ztest_fault_inject(ztest_ds_t *zd, uint64_t id)
{
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	int fd;
	uint64_t offset;
	uint64_t leaves;
	uint64_t bad = 0x1990c0ffeedecadeULL;
	uint64_t top, leaf;
	char path0[MAXPATHLEN];
	char pathrand[MAXPATHLEN];
	size_t fsize;
	int bshift = SPA_MAXBLOCKSHIFT + 2;
	int iters = 1000;
	int maxfaults;
	int mirror_save;
	vdev_t *vd0 = NULL;
	uint64_t guid0 = 0;
	boolean_t islog = B_FALSE;

	mutex_enter(&ztest_vdev_lock);

	/*
	 * Device removal is in progress, fault injection must be disabled
	 * until it completes and the pool is scrubbed.  The fault injection
	 * strategy for damaging blocks does not take in to account evacuated
	 * blocks which may have already been damaged.
	 */
	if (ztest_device_removal_active) {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	maxfaults = MAXFAULTS();
	leaves = MAX(zs->zs_mirrors, 1) * ztest_opts.zo_raidz;
	mirror_save = zs->zs_mirrors;
	mutex_exit(&ztest_vdev_lock);

	ASSERT(leaves >= 1);

	/*
	 * Grab the name lock as reader. There are some operations
	 * which don't like to have their vdevs changed while
	 * they are in progress (i.e. spa_change_guid). Those
	 * operations will have grabbed the name lock as writer.
	 */
	rw_enter(&ztest_name_lock, RW_READER);

	/*
	 * We need SCL_STATE here because we're going to look at vd0->vdev_tsd.
	 */
	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	if (ztest_random(2) == 0) {
		/*
		 * Inject errors on a normal data device or slog device.
		 */
		top = ztest_random_vdev_top(spa, B_TRUE);
		leaf = ztest_random(leaves) + zs->zs_splits;

		/*
		 * Generate paths to the first leaf in this top-level vdev,
		 * and to the random leaf we selected.  We'll induce transient
		 * write failures and random online/offline activity on leaf 0,
		 * and we'll write random garbage to the randomly chosen leaf.
		 */
		(void) snprintf(path0, sizeof (path0), ztest_dev_template,
		    ztest_opts.zo_dir, ztest_opts.zo_pool,
		    top * leaves + zs->zs_splits);
		(void) snprintf(pathrand, sizeof (pathrand), ztest_dev_template,
		    ztest_opts.zo_dir, ztest_opts.zo_pool,
		    top * leaves + leaf);

		vd0 = vdev_lookup_by_path(spa->spa_root_vdev, path0);
		if (vd0 != NULL && vd0->vdev_top->vdev_islog)
			islog = B_TRUE;

		/*
		 * If the top-level vdev needs to be resilvered
		 * then we only allow faults on the device that is
		 * resilvering.
		 */
		if (vd0 != NULL && maxfaults != 1 &&
		    (!vdev_resilver_needed(vd0->vdev_top, NULL, NULL) ||
		    vd0->vdev_resilver_txg != 0)) {
			/*
			 * Make vd0 explicitly claim to be unreadable,
			 * or unwriteable, or reach behind its back
			 * and close the underlying fd.  We can do this if
			 * maxfaults == 0 because we'll fail and reexecute,
			 * and we can do it if maxfaults >= 2 because we'll
			 * have enough redundancy.  If maxfaults == 1, the
			 * combination of this with injection of random data
			 * corruption below exceeds the pool's fault tolerance.
			 */
			vdev_file_t *vf = vd0->vdev_tsd;

			zfs_dbgmsg("injecting fault to vdev %llu; maxfaults=%d",
			    (long long)vd0->vdev_id, (int)maxfaults);

			if (vf != NULL && ztest_random(3) == 0) {
				(void) close(vf->vf_vnode->v_fd);
				vf->vf_vnode->v_fd = -1;
			} else if (ztest_random(2) == 0) {
				vd0->vdev_cant_read = B_TRUE;
			} else {
				vd0->vdev_cant_write = B_TRUE;
			}
			guid0 = vd0->vdev_guid;
		}
	} else {
		/*
		 * Inject errors on an l2cache device.
		 */
		spa_aux_vdev_t *sav = &spa->spa_l2cache;

		if (sav->sav_count == 0) {
			spa_config_exit(spa, SCL_STATE, FTAG);
			rw_exit(&ztest_name_lock);
			return;
		}
		vd0 = sav->sav_vdevs[ztest_random(sav->sav_count)];
		guid0 = vd0->vdev_guid;
		(void) strcpy(path0, vd0->vdev_path);
		(void) strcpy(pathrand, vd0->vdev_path);

		leaf = 0;
		leaves = 1;
		maxfaults = INT_MAX;	/* no limit on cache devices */
	}

	spa_config_exit(spa, SCL_STATE, FTAG);
	rw_exit(&ztest_name_lock);

	/*
	 * If we can tolerate two or more faults, or we're dealing
	 * with a slog, randomly online/offline vd0.
	 */
	if ((maxfaults >= 2 || islog) && guid0 != 0) {
		if (ztest_random(10) < 6) {
			int flags = (ztest_random(2) == 0 ?
			    ZFS_OFFLINE_TEMPORARY : 0);

			/*
			 * We have to grab the zs_name_lock as writer to
			 * prevent a race between offlining a slog and
			 * destroying a dataset. Offlining the slog will
			 * grab a reference on the dataset which may cause
			 * dmu_objset_destroy() to fail with EBUSY thus
			 * leaving the dataset in an inconsistent state.
			 */
			if (islog)
				rw_enter(&ztest_name_lock, RW_WRITER);

			VERIFY(vdev_offline(spa, guid0, flags) != EBUSY);

			if (islog)
				rw_exit(&ztest_name_lock);
		} else {
			/*
			 * Ideally we would like to be able to randomly
			 * call vdev_[on|off]line without holding locks
			 * to force unpredictable failures but the side
			 * effects of vdev_[on|off]line prevent us from
			 * doing so. We grab the ztest_vdev_lock here to
			 * prevent a race between injection testing and
			 * aux_vdev removal.
			 */
			mutex_enter(&ztest_vdev_lock);
			(void) vdev_online(spa, guid0, 0, NULL);
			mutex_exit(&ztest_vdev_lock);
		}
	}

	if (maxfaults == 0)
		return;

	/*
	 * We have at least single-fault tolerance, so inject data corruption.
	 */
	fd = open(pathrand, O_RDWR);

	if (fd == -1) /* we hit a gap in the device namespace */
		return;

	fsize = lseek(fd, 0, SEEK_END);

	while (--iters != 0) {
		/*
		 * The offset must be chosen carefully to ensure that
		 * we do not inject a given logical block with errors
		 * on two different leaf devices, because ZFS can not
		 * tolerate that (if maxfaults==1).
		 *
		 * We divide each leaf into chunks of size
		 * (# leaves * SPA_MAXBLOCKSIZE * 4).  Within each chunk
		 * there is a series of ranges to which we can inject errors.
		 * Each range can accept errors on only a single leaf vdev.
		 * The error injection ranges are separated by ranges
		 * which we will not inject errors on any device (DMZs).
		 * Each DMZ must be large enough such that a single block
		 * can not straddle it, so that a single block can not be
		 * a target in two different injection ranges (on different
		 * leaf vdevs).
		 *
		 * For example, with 3 leaves, each chunk looks like:
		 *    0 to  32M: injection range for leaf 0
		 *  32M to  64M: DMZ - no injection allowed
		 *  64M to  96M: injection range for leaf 1
		 *  96M to 128M: DMZ - no injection allowed
		 * 128M to 160M: injection range for leaf 2
		 * 160M to 192M: DMZ - no injection allowed
		 */
		offset = ztest_random(fsize / (leaves << bshift)) *
		    (leaves << bshift) + (leaf << bshift) +
		    (ztest_random(1ULL << (bshift - 1)) & -8ULL);

		/*
		 * Only allow damage to the labels at one end of the vdev.
		 *
		 * If all labels are damaged, the device will be totally
		 * inaccessible, which will result in loss of data,
		 * because we also damage (parts of) the other side of
		 * the mirror/raidz.
		 *
		 * Additionally, we will always have both an even and an
		 * odd label, so that we can handle crashes in the
		 * middle of vdev_config_sync().
		 */
		if ((leaf & 1) == 0 && offset < VDEV_LABEL_START_SIZE)
			continue;

		/*
		 * The two end labels are stored at the "end" of the disk, but
		 * the end of the disk (vdev_psize) is aligned to
		 * sizeof (vdev_label_t).
		 */
		uint64_t psize = P2ALIGN(fsize, sizeof (vdev_label_t));
		if ((leaf & 1) == 1 &&
		    offset + sizeof (bad) > psize - VDEV_LABEL_END_SIZE)
			continue;

		mutex_enter(&ztest_vdev_lock);
		if (mirror_save != zs->zs_mirrors) {
			mutex_exit(&ztest_vdev_lock);
			(void) close(fd);
			return;
		}

		if (pwrite(fd, &bad, sizeof (bad), offset) != sizeof (bad))
			fatal(1, "can't inject bad word at 0x%llx in %s",
			    offset, pathrand);

		mutex_exit(&ztest_vdev_lock);

		if (ztest_opts.zo_verbose >= 7)
			(void) printf("injected bad word into %s,"
			    " offset 0x%llx\n", pathrand, (u_longlong_t)offset);
	}

	(void) close(fd);
}

/*
 * Verify that DDT repair works as expected.
 */
void
ztest_ddt_repair(ztest_ds_t *zd, uint64_t id)
{
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	objset_t *os = zd->zd_os;
	ztest_od_t od[1];
	uint64_t object, blocksize, txg, pattern, psize;
	enum zio_checksum checksum = spa_dedup_checksum(spa);
	dmu_buf_t *db;
	dmu_tx_t *tx;
	abd_t *abd;
	blkptr_t blk;
	int copies = 2 * ZIO_DEDUPDITTO_MIN;

	blocksize = ztest_random_blocksize();
	blocksize = MIN(blocksize, 2048);	/* because we write so many */

	ztest_od_init(&od[0], id, FTAG, 0, DMU_OT_UINT64_OTHER, blocksize, 0, 0);

	if (ztest_object_init(zd, od, sizeof (od), B_FALSE) != 0)
		return;

	/*
	 * Take the name lock as writer to prevent anyone else from changing
	 * the pool and dataset properies we need to maintain during this test.
	 */
	rw_enter(&ztest_name_lock, RW_WRITER);

	if (ztest_dsl_prop_set_uint64(zd->zd_name, ZFS_PROP_DEDUP, checksum,
	    B_FALSE) != 0 ||
	    ztest_dsl_prop_set_uint64(zd->zd_name, ZFS_PROP_COPIES, 1,
	    B_FALSE) != 0) {
		rw_exit(&ztest_name_lock);
		return;
	}

	dmu_objset_stats_t dds;
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	dmu_objset_fast_stat(os, &dds);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);

	object = od[0].od_object;
	blocksize = od[0].od_blocksize;
	pattern = zs->zs_guid ^ dds.dds_guid;

	ASSERT(object != 0);

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, object, 0, copies * blocksize);
	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		rw_exit(&ztest_name_lock);
		return;
	}

	/*
	 * Write all the copies of our block.
	 */
	for (int i = 0; i < copies; i++) {
		uint64_t offset = i * blocksize;
		int error = dmu_buf_hold(os, object, offset, FTAG, &db,
		    DMU_READ_NO_PREFETCH);
		if (error != 0) {
			fatal(B_FALSE, "dmu_buf_hold(%p, %llu, %llu) = %u",
			    os, (long long)object, (long long) offset, error);
		}
		ASSERT(db->db_offset == offset);
		ASSERT(db->db_size == blocksize);
		ASSERT(ztest_pattern_match(db->db_data, db->db_size, pattern) ||
		    ztest_pattern_match(db->db_data, db->db_size, 0ULL));
		dmu_buf_will_fill(db, tx);
		ztest_pattern_set(db->db_data, db->db_size, pattern);
		dmu_buf_rele(db, FTAG);
	}

	dmu_tx_commit(tx);
	txg_wait_synced(spa_get_dsl(spa), txg);

	/*
	 * Find out what block we got.
	 */
	VERIFY0(dmu_buf_hold(os, object, 0, FTAG, &db,
	    DMU_READ_NO_PREFETCH));
	blk = *((dmu_buf_impl_t *)db)->db_blkptr;
	dmu_buf_rele(db, FTAG);

	/*
	 * Damage the block.  Dedup-ditto will save us when we read it later.
	 */
	psize = BP_GET_PSIZE(&blk);
	abd = abd_alloc_linear(psize, B_TRUE);
	ztest_pattern_set(abd_to_buf(abd), psize, ~pattern);

	(void) zio_wait(zio_rewrite(NULL, spa, 0, &blk,
	    abd, psize, NULL, NULL, ZIO_PRIORITY_SYNC_WRITE,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_INDUCE_DAMAGE, NULL));

	abd_free(abd);

	rw_exit(&ztest_name_lock);
}

/*
 * Scrub the pool.
 */
/* ARGSUSED */
void
ztest_scrub(ztest_ds_t *zd, uint64_t id)
{
	spa_t *spa = ztest_spa;

	/*
	 * Scrub in progress by device removal.
	 */
	if (ztest_device_removal_active)
		return;

	(void) spa_scan(spa, POOL_SCAN_SCRUB);
	(void) poll(NULL, 0, 100); /* wait a moment, then force a restart */
	(void) spa_scan(spa, POOL_SCAN_SCRUB);
}

/*
 * Change the guid for the pool.
 */
/* ARGSUSED */
void
ztest_reguid(ztest_ds_t *zd, uint64_t id)
{
	spa_t *spa = ztest_spa;
	uint64_t orig, load;
	int error;

	orig = spa_guid(spa);
	load = spa_load_guid(spa);

	rw_enter(&ztest_name_lock, RW_WRITER);
	error = spa_change_guid(spa);
	rw_exit(&ztest_name_lock);

	if (error != 0)
		return;

	if (ztest_opts.zo_verbose >= 4) {
		(void) printf("Changed guid old %llu -> %llu\n",
		    (u_longlong_t)orig, (u_longlong_t)spa_guid(spa));
	}

	VERIFY3U(orig, !=, spa_guid(spa));
	VERIFY3U(load, ==, spa_load_guid(spa));
}

/*
 * Rename the pool to a different name and then rename it back.
 */
/* ARGSUSED */
void
ztest_spa_rename(ztest_ds_t *zd, uint64_t id)
{
	char *oldname, *newname;
	spa_t *spa;

	rw_enter(&ztest_name_lock, RW_WRITER);

	oldname = ztest_opts.zo_pool;
	newname = umem_alloc(strlen(oldname) + 5, UMEM_NOFAIL);
	(void) strcpy(newname, oldname);
	(void) strcat(newname, "_tmp");

	/*
	 * Do the rename
	 */
	VERIFY3U(0, ==, spa_rename(oldname, newname));

	/*
	 * Try to open it under the old name, which shouldn't exist
	 */
	VERIFY3U(ENOENT, ==, spa_open(oldname, &spa, FTAG));

	/*
	 * Open it under the new name and make sure it's still the same spa_t.
	 */
	VERIFY3U(0, ==, spa_open(newname, &spa, FTAG));

	ASSERT(spa == ztest_spa);
	spa_close(spa, FTAG);

	/*
	 * Rename it back to the original
	 */
	VERIFY3U(0, ==, spa_rename(newname, oldname));

	/*
	 * Make sure it can still be opened
	 */
	VERIFY3U(0, ==, spa_open(oldname, &spa, FTAG));

	ASSERT(spa == ztest_spa);
	spa_close(spa, FTAG);

	umem_free(newname, strlen(newname) + 1);

	rw_exit(&ztest_name_lock);
}

static vdev_t *
ztest_random_concrete_vdev_leaf(vdev_t *vd)
{
	if (vd == NULL)
		return (NULL);

	if (vd->vdev_children == 0)
		return (vd);

	vdev_t *eligible[vd->vdev_children];
	int eligible_idx = 0, i;
	for (i = 0; i < vd->vdev_children; i++) {
		vdev_t *cvd = vd->vdev_child[i];
		if (cvd->vdev_top->vdev_removing)
			continue;
		if (cvd->vdev_children > 0 ||
		    (vdev_is_concrete(cvd) && !cvd->vdev_detached)) {
			eligible[eligible_idx++] = cvd;
		}
	}
	VERIFY(eligible_idx > 0);

	uint64_t child_no = ztest_random(eligible_idx);
	return (ztest_random_concrete_vdev_leaf(eligible[child_no]));
}

/* ARGSUSED */
void
ztest_initialize(ztest_ds_t *zd, uint64_t id)
{
	spa_t *spa = ztest_spa;
	int error = 0;

	mutex_enter(&ztest_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	/* Random leaf vdev */
	vdev_t *rand_vd = ztest_random_concrete_vdev_leaf(spa->spa_root_vdev);
	if (rand_vd == NULL) {
		spa_config_exit(spa, SCL_VDEV, FTAG);
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * The random vdev we've selected may change as soon as we
	 * drop the spa_config_lock. We create local copies of things
	 * we're interested in.
	 */
	uint64_t guid = rand_vd->vdev_guid;
	char *path = strdup(rand_vd->vdev_path);
	boolean_t active = rand_vd->vdev_initialize_thread != NULL;

	zfs_dbgmsg("vd %p, guid %llu", rand_vd, guid);
	spa_config_exit(spa, SCL_VDEV, FTAG);

	uint64_t cmd = ztest_random(POOL_INITIALIZE_FUNCS);
	error = spa_vdev_initialize(spa, guid, cmd);
	switch (cmd) {
	case POOL_INITIALIZE_CANCEL:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Cancel initialize %s", path);
			if (!active)
				(void) printf(" failed (no initialize active)");
			(void) printf("\n");
		}
		break;
	case POOL_INITIALIZE_DO:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Start initialize %s", path);
			if (active && error == 0)
				(void) printf(" failed (already active)");
			else if (error != 0)
				(void) printf(" failed (error %d)", error);
			(void) printf("\n");
		}
		break;
	case POOL_INITIALIZE_SUSPEND:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Suspend initialize %s", path);
			if (!active)
				(void) printf(" failed (no initialize active)");
			(void) printf("\n");
		}
		break;
	}
	free(path);
	mutex_exit(&ztest_vdev_lock);
}

/*
 * Verify pool integrity by running zdb.
 */
static void
ztest_run_zdb(char *pool)
{
	int status;
	char zdb[MAXPATHLEN + MAXNAMELEN + 20];
	char zbuf[1024];
	char *bin;
	char *ztest;
	char *isa;
	int isalen;
	FILE *fp;

	strlcpy(zdb, "/usr/bin/ztest", sizeof(zdb));

	/* zdb lives in /usr/sbin, while ztest lives in /usr/bin */
	bin = strstr(zdb, "/usr/bin/");
	ztest = strstr(bin, "/ztest");
	isa = bin + 8;
	isalen = ztest - isa;
	isa = strdup(isa);
	/* LINTED */
	(void) sprintf(bin,
	    "/usr/sbin%.*s/zdb -bcc%s%s -G -d -U %s %s",
	    isalen,
	    isa,
	    ztest_opts.zo_verbose >= 3 ? "s" : "",
	    ztest_opts.zo_verbose >= 4 ? "v" : "",
	    spa_config_path,
	    pool);
	free(isa);

	if (ztest_opts.zo_verbose >= 5)
		(void) printf("Executing %s\n", strstr(zdb, "zdb "));

	fp = popen(zdb, "r");
	assert(fp != NULL);

	while (fgets(zbuf, sizeof (zbuf), fp) != NULL)
		if (ztest_opts.zo_verbose >= 3)
			(void) printf("%s", zbuf);

	status = pclose(fp);

	if (status == 0)
		return;

	ztest_dump_core = 0;
	if (WIFEXITED(status))
		fatal(0, "'%s' exit code %d", zdb, WEXITSTATUS(status));
	else
		fatal(0, "'%s' died with signal %d", zdb, WTERMSIG(status));
}

static void
ztest_walk_pool_directory(char *header)
{
	spa_t *spa = NULL;

	if (ztest_opts.zo_verbose >= 6)
		(void) printf("%s\n", header);

	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL)
		if (ztest_opts.zo_verbose >= 6)
			(void) printf("\t%s\n", spa_name(spa));
	mutex_exit(&spa_namespace_lock);
}

static void
ztest_spa_import_export(char *oldname, char *newname)
{
	nvlist_t *config, *newconfig;
	uint64_t pool_guid;
	spa_t *spa;
	int error;

	if (ztest_opts.zo_verbose >= 4) {
		(void) printf("import/export: old = %s, new = %s\n",
		    oldname, newname);
	}

	/*
	 * Clean up from previous runs.
	 */
	(void) spa_destroy(newname);

	/*
	 * Get the pool's configuration and guid.
	 */
	VERIFY3U(0, ==, spa_open(oldname, &spa, FTAG));

	/*
	 * Kick off a scrub to tickle scrub/export races.
	 */
	if (ztest_random(2) == 0)
		(void) spa_scan(spa, POOL_SCAN_SCRUB);

	pool_guid = spa_guid(spa);
	spa_close(spa, FTAG);

	ztest_walk_pool_directory("pools before export");

	/*
	 * Export it.
	 */
	VERIFY3U(0, ==, spa_export(oldname, &config, B_FALSE, B_FALSE));

	ztest_walk_pool_directory("pools after export");

	/*
	 * Try to import it.
	 */
	newconfig = spa_tryimport(config);
	ASSERT(newconfig != NULL);
	nvlist_free(newconfig);

	/*
	 * Import it under the new name.
	 */
	error = spa_import(newname, config, NULL, 0);
	if (error != 0) {
		dump_nvlist(config, 0);
		fatal(B_FALSE, "couldn't import pool %s as %s: error %u",
		    oldname, newname, error);
	}

	ztest_walk_pool_directory("pools after import");

	/*
	 * Try to import it again -- should fail with EEXIST.
	 */
	VERIFY3U(EEXIST, ==, spa_import(newname, config, NULL, 0));

	/*
	 * Try to import it under a different name -- should fail with EEXIST.
	 */
	VERIFY3U(EEXIST, ==, spa_import(oldname, config, NULL, 0));

	/*
	 * Verify that the pool is no longer visible under the old name.
	 */
	VERIFY3U(ENOENT, ==, spa_open(oldname, &spa, FTAG));

	/*
	 * Verify that we can open and close the pool using the new name.
	 */
	VERIFY3U(0, ==, spa_open(newname, &spa, FTAG));
	ASSERT(pool_guid == spa_guid(spa));
	spa_close(spa, FTAG);

	nvlist_free(config);
}

static void
ztest_resume(spa_t *spa)
{
	if (spa_suspended(spa) && ztest_opts.zo_verbose >= 6)
		(void) printf("resuming from suspended state\n");
	spa_vdev_state_enter(spa, SCL_NONE);
	vdev_clear(spa, NULL);
	(void) spa_vdev_state_exit(spa, NULL, 0);
	(void) zio_resume(spa);
}

static void *
ztest_resume_thread(void *arg)
{
	spa_t *spa = arg;

	while (!ztest_exiting) {
		if (spa_suspended(spa))
			ztest_resume(spa);
		(void) poll(NULL, 0, 100);

		/*
		 * Periodically change the zfs_compressed_arc_enabled setting.
		 */
		if (ztest_random(10) == 0)
			zfs_compressed_arc_enabled = ztest_random(2);

		/*
		 * Periodically change the zfs_abd_scatter_enabled setting.
		 */
		if (ztest_random(10) == 0)
			zfs_abd_scatter_enabled = ztest_random(2);
	}
	return (NULL);
}

static void *
ztest_deadman_thread(void *arg)
{
	ztest_shared_t *zs = arg;
	spa_t *spa = ztest_spa;
	hrtime_t delta, total = 0;

	for (;;) {
		delta = zs->zs_thread_stop - zs->zs_thread_start +
		    MSEC2NSEC(zfs_deadman_synctime_ms);

		(void) poll(NULL, 0, (int)NSEC2MSEC(delta));

		/*
		 * If the pool is suspended then fail immediately. Otherwise,
		 * check to see if the pool is making any progress. If
		 * vdev_deadman() discovers that there hasn't been any recent
		 * I/Os then it will end up aborting the tests.
		 */
		if (spa_suspended(spa) || spa->spa_root_vdev == NULL) {
			fatal(0, "aborting test after %llu seconds because "
			    "pool has transitioned to a suspended state.",
			    zfs_deadman_synctime_ms / 1000);
			return (NULL);
		}
		vdev_deadman(spa->spa_root_vdev);

		total += zfs_deadman_synctime_ms/1000;
		(void) printf("ztest has been running for %lld seconds\n",
		    total);
	}
}

static void
ztest_execute(int test, ztest_info_t *zi, uint64_t id)
{
	ztest_ds_t *zd = &ztest_ds[id % ztest_opts.zo_datasets];
	ztest_shared_callstate_t *zc = ZTEST_GET_SHARED_CALLSTATE(test);
	hrtime_t functime = gethrtime();

	for (int i = 0; i < zi->zi_iters; i++)
		zi->zi_func(zd, id);

	functime = gethrtime() - functime;

	atomic_add_64(&zc->zc_count, 1);
	atomic_add_64(&zc->zc_time, functime);

	if (ztest_opts.zo_verbose >= 4) {
		Dl_info dli;
		(void) dladdr((void *)zi->zi_func, &dli);
		(void) printf("%6.2f sec in %s\n",
		    (double)functime / NANOSEC, dli.dli_sname);
	}
}

static void *
ztest_thread(void *arg)
{
	int rand;
	uint64_t id = (uintptr_t)arg;
	ztest_shared_t *zs = ztest_shared;
	uint64_t call_next;
	hrtime_t now;
	ztest_info_t *zi;
	ztest_shared_callstate_t *zc;

	while ((now = gethrtime()) < zs->zs_thread_stop) {
		/*
		 * See if it's time to force a crash.
		 */
		if (now > zs->zs_thread_kill)
			ztest_kill(zs);

		/*
		 * If we're getting ENOSPC with some regularity, stop.
		 */
		if (zs->zs_enospc_count > 10)
			break;

		/*
		 * Pick a random function to execute.
		 */
		rand = ztest_random(ZTEST_FUNCS);
		zi = &ztest_info[rand];
		zc = ZTEST_GET_SHARED_CALLSTATE(rand);
		call_next = zc->zc_next;

		if (now >= call_next &&
		    atomic_cas_64(&zc->zc_next, call_next, call_next +
		    ztest_random(2 * zi->zi_interval[0] + 1)) == call_next) {
			ztest_execute(rand, zi, id);
		}
	}

	return (NULL);
}

static void
ztest_dataset_name(char *dsname, char *pool, int d)
{
	(void) snprintf(dsname, ZFS_MAX_DATASET_NAME_LEN, "%s/ds_%d", pool, d);
}

static void
ztest_dataset_destroy(int d)
{
	char name[ZFS_MAX_DATASET_NAME_LEN];

	ztest_dataset_name(name, ztest_opts.zo_pool, d);

	if (ztest_opts.zo_verbose >= 3)
		(void) printf("Destroying %s to free up space\n", name);

	/*
	 * Cleanup any non-standard clones and snapshots.  In general,
	 * ztest thread t operates on dataset (t % zopt_datasets),
	 * so there may be more than one thing to clean up.
	 */
	for (int t = d; t < ztest_opts.zo_threads;
	    t += ztest_opts.zo_datasets) {
		ztest_dsl_dataset_cleanup(name, t);
	}

	(void) dmu_objset_find(name, ztest_objset_destroy_cb, NULL,
	    DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN);
}

static void
ztest_dataset_dirobj_verify(ztest_ds_t *zd)
{
	uint64_t usedobjs, dirobjs, scratch;

	/*
	 * ZTEST_DIROBJ is the object directory for the entire dataset.
	 * Therefore, the number of objects in use should equal the
	 * number of ZTEST_DIROBJ entries, +1 for ZTEST_DIROBJ itself.
	 * If not, we have an object leak.
	 *
	 * Note that we can only check this in ztest_dataset_open(),
	 * when the open-context and syncing-context values agree.
	 * That's because zap_count() returns the open-context value,
	 * while dmu_objset_space() returns the rootbp fill count.
	 */
	VERIFY3U(0, ==, zap_count(zd->zd_os, ZTEST_DIROBJ, &dirobjs));
	dmu_objset_space(zd->zd_os, &scratch, &scratch, &usedobjs, &scratch);
	ASSERT3U(dirobjs + 1, ==, usedobjs);
}

static int
ztest_dataset_open(int d)
{
	ztest_ds_t *zd = &ztest_ds[d];
	uint64_t committed_seq = ZTEST_GET_SHARED_DS(d)->zd_seq;
	objset_t *os;
	zilog_t *zilog;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	ztest_dataset_name(name, ztest_opts.zo_pool, d);

	rw_enter(&ztest_name_lock, RW_READER);

	error = ztest_dataset_create(name);
	if (error == ENOSPC) {
		rw_exit(&ztest_name_lock);
		ztest_record_enospc(FTAG);
		return (error);
	}
	ASSERT(error == 0 || error == EEXIST);

	VERIFY0(dmu_objset_own(name, DMU_OST_OTHER, B_FALSE, zd, &os));
	rw_exit(&ztest_name_lock);

	ztest_zd_init(zd, ZTEST_GET_SHARED_DS(d), os);

	zilog = zd->zd_zilog;

	if (zilog->zl_header->zh_claim_lr_seq != 0 &&
	    zilog->zl_header->zh_claim_lr_seq < committed_seq)
		fatal(0, "missing log records: claimed %llu < committed %llu",
		    zilog->zl_header->zh_claim_lr_seq, committed_seq);

	ztest_dataset_dirobj_verify(zd);

	zil_replay(os, zd, ztest_replay_vector);

	ztest_dataset_dirobj_verify(zd);

	if (ztest_opts.zo_verbose >= 6)
		(void) printf("%s replay %llu blocks, %llu records, seq %llu\n",
		    zd->zd_name,
		    (u_longlong_t)zilog->zl_parse_blk_count,
		    (u_longlong_t)zilog->zl_parse_lr_count,
		    (u_longlong_t)zilog->zl_replaying_seq);

	zilog = zil_open(os, ztest_get_data);

	if (zilog->zl_replaying_seq != 0 &&
	    zilog->zl_replaying_seq < committed_seq)
		fatal(0, "missing log records: replayed %llu < committed %llu",
		    zilog->zl_replaying_seq, committed_seq);

	return (0);
}

static void
ztest_dataset_close(int d)
{
	ztest_ds_t *zd = &ztest_ds[d];

	zil_close(zd->zd_zilog);
	dmu_objset_disown(zd->zd_os, zd);

	ztest_zd_fini(zd);
}

/*
 * Kick off threads to run tests on all datasets in parallel.
 */
static void
ztest_run(ztest_shared_t *zs)
{
	thread_t *tid;
	spa_t *spa;
	objset_t *os;
	thread_t resume_tid;
	int error;

	ztest_exiting = B_FALSE;

	/*
	 * Initialize parent/child shared state.
	 */
	mutex_init(&ztest_checkpoint_lock, NULL, USYNC_THREAD, NULL);
	mutex_init(&ztest_vdev_lock, NULL, USYNC_THREAD, NULL);
	rw_init(&ztest_name_lock, NULL, USYNC_THREAD, NULL);

	zs->zs_thread_start = gethrtime();
	zs->zs_thread_stop =
	    zs->zs_thread_start + ztest_opts.zo_passtime * NANOSEC;
	zs->zs_thread_stop = MIN(zs->zs_thread_stop, zs->zs_proc_stop);
	zs->zs_thread_kill = zs->zs_thread_stop;
	if (ztest_random(100) < ztest_opts.zo_killrate) {
		zs->zs_thread_kill -=
		    ztest_random(ztest_opts.zo_passtime * NANOSEC);
	}

	mutex_init(&zcl.zcl_callbacks_lock, NULL, USYNC_THREAD, NULL);

	list_create(&zcl.zcl_callbacks, sizeof (ztest_cb_data_t),
	    offsetof(ztest_cb_data_t, zcd_node));

	/*
	 * Open our pool.
	 */
	kernel_init(FREAD | FWRITE);
	VERIFY0(spa_open(ztest_opts.zo_pool, &spa, FTAG));
	metaslab_preload_limit = ztest_random(20) + 1;
	ztest_spa = spa;

	dmu_objset_stats_t dds;
	VERIFY0(dmu_objset_own(ztest_opts.zo_pool,
	    DMU_OST_ANY, B_TRUE, FTAG, &os));
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	dmu_objset_fast_stat(os, &dds);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
	zs->zs_guid = dds.dds_guid;
	dmu_objset_disown(os, FTAG);

	spa->spa_dedup_ditto = 2 * ZIO_DEDUPDITTO_MIN;

	/*
	 * We don't expect the pool to suspend unless maxfaults == 0,
	 * in which case ztest_fault_inject() temporarily takes away
	 * the only valid replica.
	 */
	if (MAXFAULTS() == 0)
		spa->spa_failmode = ZIO_FAILURE_MODE_WAIT;
	else
		spa->spa_failmode = ZIO_FAILURE_MODE_PANIC;

	/*
	 * Create a thread to periodically resume suspended I/O.
	 */
	VERIFY(thr_create(0, 0, ztest_resume_thread, spa, THR_BOUND,
	    &resume_tid) == 0);

	/*
	 * Create a deadman thread to abort() if we hang.
	 */
	VERIFY(thr_create(0, 0, ztest_deadman_thread, zs, THR_BOUND,
	    NULL) == 0);

	/*
	 * Verify that we can safely inquire about any object,
	 * whether it's allocated or not.  To make it interesting,
	 * we probe a 5-wide window around each power of two.
	 * This hits all edge cases, including zero and the max.
	 */
	for (int t = 0; t < 64; t++) {
		for (int d = -5; d <= 5; d++) {
			error = dmu_object_info(spa->spa_meta_objset,
			    (1ULL << t) + d, NULL);
			ASSERT(error == 0 || error == ENOENT ||
			    error == EINVAL);
		}
	}

	/*
	 * If we got any ENOSPC errors on the previous run, destroy something.
	 */
	if (zs->zs_enospc_count != 0) {
		int d = ztest_random(ztest_opts.zo_datasets);
		ztest_dataset_destroy(d);
	}
	zs->zs_enospc_count = 0;

	tid = umem_zalloc(ztest_opts.zo_threads * sizeof (thread_t),
	    UMEM_NOFAIL);

	if (ztest_opts.zo_verbose >= 4)
		(void) printf("starting main threads...\n");

	/*
	 * Kick off all the tests that run in parallel.
	 */
	for (int t = 0; t < ztest_opts.zo_threads; t++) {
		if (t < ztest_opts.zo_datasets &&
		    ztest_dataset_open(t) != 0)
			return;
		VERIFY(thr_create(0, 0, ztest_thread, (void *)(uintptr_t)t,
		    THR_BOUND, &tid[t]) == 0);
	}

	/*
	 * Wait for all of the tests to complete.  We go in reverse order
	 * so we don't close datasets while threads are still using them.
	 */
	for (int t = ztest_opts.zo_threads - 1; t >= 0; t--) {
		VERIFY(thr_join(tid[t], NULL, NULL) == 0);
		if (t < ztest_opts.zo_datasets)
			ztest_dataset_close(t);
	}

	txg_wait_synced(spa_get_dsl(spa), 0);

	zs->zs_alloc = metaslab_class_get_alloc(spa_normal_class(spa));
	zs->zs_space = metaslab_class_get_space(spa_normal_class(spa));
	zfs_dbgmsg_print(FTAG);

	umem_free(tid, ztest_opts.zo_threads * sizeof (thread_t));

	/* Kill the resume thread */
	ztest_exiting = B_TRUE;
	VERIFY(thr_join(resume_tid, NULL, NULL) == 0);
	ztest_resume(spa);

	/*
	 * Right before closing the pool, kick off a bunch of async I/O;
	 * spa_close() should wait for it to complete.
	 */
	for (uint64_t object = 1; object < 50; object++) {
		dmu_prefetch(spa->spa_meta_objset, object, 0, 0, 1ULL << 20,
		    ZIO_PRIORITY_SYNC_READ);
	}

	spa_close(spa, FTAG);

	/*
	 * Verify that we can loop over all pools.
	 */
	mutex_enter(&spa_namespace_lock);
	for (spa = spa_next(NULL); spa != NULL; spa = spa_next(spa))
		if (ztest_opts.zo_verbose > 3)
			(void) printf("spa_next: found %s\n", spa_name(spa));
	mutex_exit(&spa_namespace_lock);

	/*
	 * Verify that we can export the pool and reimport it under a
	 * different name.
	 */
	if (ztest_random(2) == 0) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		(void) snprintf(name, sizeof (name), "%s_import",
		    ztest_opts.zo_pool);
		ztest_spa_import_export(ztest_opts.zo_pool, name);
		ztest_spa_import_export(name, ztest_opts.zo_pool);
	}

	kernel_fini();

	list_destroy(&zcl.zcl_callbacks);

	mutex_destroy(&zcl.zcl_callbacks_lock);

	rw_destroy(&ztest_name_lock);
	mutex_destroy(&ztest_vdev_lock);
	mutex_destroy(&ztest_checkpoint_lock);
}

static void
ztest_freeze(void)
{
	ztest_ds_t *zd = &ztest_ds[0];
	spa_t *spa;
	int numloops = 0;

	if (ztest_opts.zo_verbose >= 3)
		(void) printf("testing spa_freeze()...\n");

	kernel_init(FREAD | FWRITE);
	VERIFY3U(0, ==, spa_open(ztest_opts.zo_pool, &spa, FTAG));
	VERIFY3U(0, ==, ztest_dataset_open(0));
	ztest_spa = spa;

	/*
	 * Force the first log block to be transactionally allocated.
	 * We have to do this before we freeze the pool -- otherwise
	 * the log chain won't be anchored.
	 */
	while (BP_IS_HOLE(&zd->zd_zilog->zl_header->zh_log)) {
		ztest_dmu_object_alloc_free(zd, 0);
		zil_commit(zd->zd_zilog, 0);
	}

	txg_wait_synced(spa_get_dsl(spa), 0);

	/*
	 * Freeze the pool.  This stops spa_sync() from doing anything,
	 * so that the only way to record changes from now on is the ZIL.
	 */
	spa_freeze(spa);

	/*
	 * Because it is hard to predict how much space a write will actually
	 * require beforehand, we leave ourselves some fudge space to write over
	 * capacity.
	 */
	uint64_t capacity = metaslab_class_get_space(spa_normal_class(spa)) / 2;

	/*
	 * Run tests that generate log records but don't alter the pool config
	 * or depend on DSL sync tasks (snapshots, objset create/destroy, etc).
	 * We do a txg_wait_synced() after each iteration to force the txg
	 * to increase well beyond the last synced value in the uberblock.
	 * The ZIL should be OK with that.
	 *
	 * Run a random number of times less than zo_maxloops and ensure we do
	 * not run out of space on the pool.
	 */
	while (ztest_random(10) != 0 &&
	    numloops++ < ztest_opts.zo_maxloops &&
	    metaslab_class_get_alloc(spa_normal_class(spa)) < capacity) {
		ztest_od_t od;
		ztest_od_init(&od, 0, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, 0);
		VERIFY0(ztest_object_init(zd, &od, sizeof (od), B_FALSE));
		ztest_io(zd, od.od_object,
		    ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);
		txg_wait_synced(spa_get_dsl(spa), 0);
	}

	/*
	 * Commit all of the changes we just generated.
	 */
	zil_commit(zd->zd_zilog, 0);
	txg_wait_synced(spa_get_dsl(spa), 0);

	/*
	 * Close our dataset and close the pool.
	 */
	ztest_dataset_close(0);
	spa_close(spa, FTAG);
	kernel_fini();

	/*
	 * Open and close the pool and dataset to induce log replay.
	 */
	kernel_init(FREAD | FWRITE);
	VERIFY3U(0, ==, spa_open(ztest_opts.zo_pool, &spa, FTAG));
	ASSERT(spa_freeze_txg(spa) == UINT64_MAX);
	VERIFY3U(0, ==, ztest_dataset_open(0));
	ztest_dataset_close(0);

	ztest_spa = spa;
	txg_wait_synced(spa_get_dsl(spa), 0);
	ztest_reguid(NULL, 0);

	spa_close(spa, FTAG);
	kernel_fini();
}

void
print_time(hrtime_t t, char *timebuf)
{
	hrtime_t s = t / NANOSEC;
	hrtime_t m = s / 60;
	hrtime_t h = m / 60;
	hrtime_t d = h / 24;

	s -= m * 60;
	m -= h * 60;
	h -= d * 24;

	timebuf[0] = '\0';

	if (d)
		(void) sprintf(timebuf,
		    "%llud%02lluh%02llum%02llus", d, h, m, s);
	else if (h)
		(void) sprintf(timebuf, "%lluh%02llum%02llus", h, m, s);
	else if (m)
		(void) sprintf(timebuf, "%llum%02llus", m, s);
	else
		(void) sprintf(timebuf, "%llus", s);
}

static nvlist_t *
make_random_props()
{
	nvlist_t *props;

	VERIFY(nvlist_alloc(&props, NV_UNIQUE_NAME, 0) == 0);
	if (ztest_random(2) == 0)
		return (props);
	VERIFY(nvlist_add_uint64(props, "autoreplace", 1) == 0);

	return (props);
}

/*
 * Create a storage pool with the given name and initial vdev size.
 * Then test spa_freeze() functionality.
 */
static void
ztest_init(ztest_shared_t *zs)
{
	spa_t *spa;
	nvlist_t *nvroot, *props;

	mutex_init(&ztest_vdev_lock, NULL, USYNC_THREAD, NULL);
	mutex_init(&ztest_checkpoint_lock, NULL, USYNC_THREAD, NULL);
	rw_init(&ztest_name_lock, NULL, USYNC_THREAD, NULL);

	kernel_init(FREAD | FWRITE);

	/*
	 * Create the storage pool.
	 */
	(void) spa_destroy(ztest_opts.zo_pool);
	ztest_shared->zs_vdev_next_leaf = 0;
	zs->zs_splits = 0;
	zs->zs_mirrors = ztest_opts.zo_mirrors;
	nvroot = make_vdev_root(NULL, NULL, NULL, ztest_opts.zo_vdev_size, 0,
	    0, ztest_opts.zo_raidz, zs->zs_mirrors, 1);
	props = make_random_props();
	for (int i = 0; i < SPA_FEATURES; i++) {
		char buf[1024];
		(void) snprintf(buf, sizeof (buf), "feature@%s",
		    spa_feature_table[i].fi_uname);
		VERIFY3U(0, ==, nvlist_add_uint64(props, buf, 0));
	}
	VERIFY3U(0, ==, spa_create(ztest_opts.zo_pool, nvroot, props, NULL));
	nvlist_free(nvroot);
	nvlist_free(props);

	VERIFY3U(0, ==, spa_open(ztest_opts.zo_pool, &spa, FTAG));
	zs->zs_metaslab_sz =
	    1ULL << spa->spa_root_vdev->vdev_child[0]->vdev_ms_shift;

	spa_close(spa, FTAG);

	kernel_fini();

	ztest_run_zdb(ztest_opts.zo_pool);

	ztest_freeze();

	ztest_run_zdb(ztest_opts.zo_pool);

	rw_destroy(&ztest_name_lock);
	mutex_destroy(&ztest_vdev_lock);
	mutex_destroy(&ztest_checkpoint_lock);
}

static void
setup_data_fd(void)
{
	static char ztest_name_data[] = "/tmp/ztest.data.XXXXXX";

	ztest_fd_data = mkstemp(ztest_name_data);
	ASSERT3S(ztest_fd_data, >=, 0);
	(void) unlink(ztest_name_data);
}


static int
shared_data_size(ztest_shared_hdr_t *hdr)
{
	int size;

	size = hdr->zh_hdr_size;
	size += hdr->zh_opts_size;
	size += hdr->zh_size;
	size += hdr->zh_stats_size * hdr->zh_stats_count;
	size += hdr->zh_ds_size * hdr->zh_ds_count;

	return (size);
}

static void
setup_hdr(void)
{
	int size;
	ztest_shared_hdr_t *hdr;

	hdr = (void *)mmap(0, P2ROUNDUP(sizeof (*hdr), getpagesize()),
	    PROT_READ | PROT_WRITE, MAP_SHARED, ztest_fd_data, 0);
	ASSERT(hdr != MAP_FAILED);

	VERIFY3U(0, ==, ftruncate(ztest_fd_data, sizeof (ztest_shared_hdr_t)));

	hdr->zh_hdr_size = sizeof (ztest_shared_hdr_t);
	hdr->zh_opts_size = sizeof (ztest_shared_opts_t);
	hdr->zh_size = sizeof (ztest_shared_t);
	hdr->zh_stats_size = sizeof (ztest_shared_callstate_t);
	hdr->zh_stats_count = ZTEST_FUNCS;
	hdr->zh_ds_size = sizeof (ztest_shared_ds_t);
	hdr->zh_ds_count = ztest_opts.zo_datasets;

	size = shared_data_size(hdr);
	VERIFY3U(0, ==, ftruncate(ztest_fd_data, size));

	(void) munmap((caddr_t)hdr, P2ROUNDUP(sizeof (*hdr), getpagesize()));
}

static void
setup_data(void)
{
	int size, offset;
	ztest_shared_hdr_t *hdr;
	uint8_t *buf;

	hdr = (void *)mmap(0, P2ROUNDUP(sizeof (*hdr), getpagesize()),
	    PROT_READ, MAP_SHARED, ztest_fd_data, 0);
	ASSERT(hdr != MAP_FAILED);

	size = shared_data_size(hdr);

	(void) munmap((caddr_t)hdr, P2ROUNDUP(sizeof (*hdr), getpagesize()));
	hdr = ztest_shared_hdr = (void *)mmap(0, P2ROUNDUP(size, getpagesize()),
	    PROT_READ | PROT_WRITE, MAP_SHARED, ztest_fd_data, 0);
	ASSERT(hdr != MAP_FAILED);
	buf = (uint8_t *)hdr;

	offset = hdr->zh_hdr_size;
	ztest_shared_opts = (void *)&buf[offset];
	offset += hdr->zh_opts_size;
	ztest_shared = (void *)&buf[offset];
	offset += hdr->zh_size;
	ztest_shared_callstate = (void *)&buf[offset];
	offset += hdr->zh_stats_size * hdr->zh_stats_count;
	ztest_shared_ds = (void *)&buf[offset];
}

static boolean_t
exec_child(char *cmd, char *libpath, boolean_t ignorekill, int *statusp)
{
	pid_t pid;
	int status;
	char *cmdbuf = NULL;

	pid = fork();

	if (cmd == NULL) {
		cmdbuf = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
		(void) strlcpy(cmdbuf, getexecname(), MAXPATHLEN);
		cmd = cmdbuf;
	}

	if (pid == -1)
		fatal(1, "fork failed");

	if (pid == 0) {	/* child */
		char *emptyargv[2] = { cmd, NULL };
		char fd_data_str[12];

		struct rlimit rl = { 1024, 1024 };
		(void) setrlimit(RLIMIT_NOFILE, &rl);

		(void) close(ztest_fd_rand);
		VERIFY3U(11, >=,
		    snprintf(fd_data_str, 12, "%d", ztest_fd_data));
		VERIFY0(setenv("ZTEST_FD_DATA", fd_data_str, 1));

		(void) enable_extended_FILE_stdio(-1, -1);
		if (libpath != NULL)
			VERIFY(0 == setenv("LD_LIBRARY_PATH", libpath, 1));
#ifdef illumos
		(void) execv(cmd, emptyargv);
#else
		(void) execvp(cmd, emptyargv);
#endif
		ztest_dump_core = B_FALSE;
		fatal(B_TRUE, "exec failed: %s", cmd);
	}

	if (cmdbuf != NULL) {
		umem_free(cmdbuf, MAXPATHLEN);
		cmd = NULL;
	}

	while (waitpid(pid, &status, 0) != pid)
		continue;
	if (statusp != NULL)
		*statusp = status;

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			(void) fprintf(stderr, "child exited with code %d\n",
			    WEXITSTATUS(status));
			exit(2);
		}
		return (B_FALSE);
	} else if (WIFSIGNALED(status)) {
		if (!ignorekill || WTERMSIG(status) != SIGKILL) {
			(void) fprintf(stderr, "child died with signal %d\n",
			    WTERMSIG(status));
			exit(3);
		}
		return (B_TRUE);
	} else {
		(void) fprintf(stderr, "something strange happened to child\n");
		exit(4);
		/* NOTREACHED */
	}
}

static void
ztest_run_init(void)
{
	ztest_shared_t *zs = ztest_shared;

	ASSERT(ztest_opts.zo_init != 0);

	/*
	 * Blow away any existing copy of zpool.cache
	 */
	(void) remove(spa_config_path);

	/*
	 * Create and initialize our storage pool.
	 */
	for (int i = 1; i <= ztest_opts.zo_init; i++) {
		bzero(zs, sizeof (ztest_shared_t));
		if (ztest_opts.zo_verbose >= 3 &&
		    ztest_opts.zo_init != 1) {
			(void) printf("ztest_init(), pass %d\n", i);
		}
		ztest_init(zs);
	}
}

int
main(int argc, char **argv)
{
	int kills = 0;
	int iters = 0;
	int older = 0;
	int newer = 0;
	ztest_shared_t *zs;
	ztest_info_t *zi;
	ztest_shared_callstate_t *zc;
	char timebuf[100];
	char numbuf[NN_NUMBUF_SZ];
	spa_t *spa;
	char *cmd;
	boolean_t hasalt;
	char *fd_data_str = getenv("ZTEST_FD_DATA");

	(void) setvbuf(stdout, NULL, _IOLBF, 0);

	dprintf_setup(&argc, argv);
	zfs_deadman_synctime_ms = 300000;
	/*
	 * As two-word space map entries may not come up often (especially
	 * if pool and vdev sizes are small) we want to force at least some
	 * of them so the feature get tested.
	 */
	zfs_force_some_double_word_sm_entries = B_TRUE;

	ztest_fd_rand = open("/dev/urandom", O_RDONLY);
	ASSERT3S(ztest_fd_rand, >=, 0);

	if (!fd_data_str) {
		process_options(argc, argv);

		setup_data_fd();
		setup_hdr();
		setup_data();
		bcopy(&ztest_opts, ztest_shared_opts,
		    sizeof (*ztest_shared_opts));
	} else {
		ztest_fd_data = atoi(fd_data_str);
		setup_data();
		bcopy(ztest_shared_opts, &ztest_opts, sizeof (ztest_opts));
	}
	ASSERT3U(ztest_opts.zo_datasets, ==, ztest_shared_hdr->zh_ds_count);

	/* Override location of zpool.cache */
	VERIFY3U(asprintf((char **)&spa_config_path, "%s/zpool.cache",
	    ztest_opts.zo_dir), !=, -1);

	ztest_ds = umem_alloc(ztest_opts.zo_datasets * sizeof (ztest_ds_t),
	    UMEM_NOFAIL);
	zs = ztest_shared;

	if (fd_data_str) {
		metaslab_force_ganging = ztest_opts.zo_metaslab_force_ganging;
		metaslab_df_alloc_threshold =
		    zs->zs_metaslab_df_alloc_threshold;

		if (zs->zs_do_init)
			ztest_run_init();
		else
			ztest_run(zs);
		exit(0);
	}

	hasalt = (strlen(ztest_opts.zo_alt_ztest) != 0);

	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("%llu vdevs, %d datasets, %d threads,"
		    " %llu seconds...\n",
		    (u_longlong_t)ztest_opts.zo_vdevs,
		    ztest_opts.zo_datasets,
		    ztest_opts.zo_threads,
		    (u_longlong_t)ztest_opts.zo_time);
	}

	cmd = umem_alloc(MAXNAMELEN, UMEM_NOFAIL);
	(void) strlcpy(cmd, getexecname(), MAXNAMELEN);

	zs->zs_do_init = B_TRUE;
	if (strlen(ztest_opts.zo_alt_ztest) != 0) {
		if (ztest_opts.zo_verbose >= 1) {
			(void) printf("Executing older ztest for "
			    "initialization: %s\n", ztest_opts.zo_alt_ztest);
		}
		VERIFY(!exec_child(ztest_opts.zo_alt_ztest,
		    ztest_opts.zo_alt_libpath, B_FALSE, NULL));
	} else {
		VERIFY(!exec_child(NULL, NULL, B_FALSE, NULL));
	}
	zs->zs_do_init = B_FALSE;

	zs->zs_proc_start = gethrtime();
	zs->zs_proc_stop = zs->zs_proc_start + ztest_opts.zo_time * NANOSEC;

	for (int f = 0; f < ZTEST_FUNCS; f++) {
		zi = &ztest_info[f];
		zc = ZTEST_GET_SHARED_CALLSTATE(f);
		if (zs->zs_proc_start + zi->zi_interval[0] > zs->zs_proc_stop)
			zc->zc_next = UINT64_MAX;
		else
			zc->zc_next = zs->zs_proc_start +
			    ztest_random(2 * zi->zi_interval[0] + 1);
	}

	/*
	 * Run the tests in a loop.  These tests include fault injection
	 * to verify that self-healing data works, and forced crashes
	 * to verify that we never lose on-disk consistency.
	 */
	while (gethrtime() < zs->zs_proc_stop) {
		int status;
		boolean_t killed;

		/*
		 * Initialize the workload counters for each function.
		 */
		for (int f = 0; f < ZTEST_FUNCS; f++) {
			zc = ZTEST_GET_SHARED_CALLSTATE(f);
			zc->zc_count = 0;
			zc->zc_time = 0;
		}

		/* Set the allocation switch size */
		zs->zs_metaslab_df_alloc_threshold =
		    ztest_random(zs->zs_metaslab_sz / 4) + 1;

		if (!hasalt || ztest_random(2) == 0) {
			if (hasalt && ztest_opts.zo_verbose >= 1) {
				(void) printf("Executing newer ztest: %s\n",
				    cmd);
			}
			newer++;
			killed = exec_child(cmd, NULL, B_TRUE, &status);
		} else {
			if (hasalt && ztest_opts.zo_verbose >= 1) {
				(void) printf("Executing older ztest: %s\n",
				    ztest_opts.zo_alt_ztest);
			}
			older++;
			killed = exec_child(ztest_opts.zo_alt_ztest,
			    ztest_opts.zo_alt_libpath, B_TRUE, &status);
		}

		if (killed)
			kills++;
		iters++;

		if (ztest_opts.zo_verbose >= 1) {
			hrtime_t now = gethrtime();

			now = MIN(now, zs->zs_proc_stop);
			print_time(zs->zs_proc_stop - now, timebuf);
			nicenum(zs->zs_space, numbuf, sizeof (numbuf));

			(void) printf("Pass %3d, %8s, %3llu ENOSPC, "
			    "%4.1f%% of %5s used, %3.0f%% done, %8s to go\n",
			    iters,
			    WIFEXITED(status) ? "Complete" : "SIGKILL",
			    (u_longlong_t)zs->zs_enospc_count,
			    100.0 * zs->zs_alloc / zs->zs_space,
			    numbuf,
			    100.0 * (now - zs->zs_proc_start) /
			    (ztest_opts.zo_time * NANOSEC), timebuf);
		}

		if (ztest_opts.zo_verbose >= 2) {
			(void) printf("\nWorkload summary:\n\n");
			(void) printf("%7s %9s   %s\n",
			    "Calls", "Time", "Function");
			(void) printf("%7s %9s   %s\n",
			    "-----", "----", "--------");
			for (int f = 0; f < ZTEST_FUNCS; f++) {
				Dl_info dli;

				zi = &ztest_info[f];
				zc = ZTEST_GET_SHARED_CALLSTATE(f);
				print_time(zc->zc_time, timebuf);
				(void) dladdr((void *)zi->zi_func, &dli);
				(void) printf("%7llu %9s   %s\n",
				    (u_longlong_t)zc->zc_count, timebuf,
				    dli.dli_sname);
			}
			(void) printf("\n");
		}

		/*
		 * It's possible that we killed a child during a rename test,
		 * in which case we'll have a 'ztest_tmp' pool lying around
		 * instead of 'ztest'.  Do a blind rename in case this happened.
		 */
		kernel_init(FREAD);
		if (spa_open(ztest_opts.zo_pool, &spa, FTAG) == 0) {
			spa_close(spa, FTAG);
		} else {
			char tmpname[ZFS_MAX_DATASET_NAME_LEN];
			kernel_fini();
			kernel_init(FREAD | FWRITE);
			(void) snprintf(tmpname, sizeof (tmpname), "%s_tmp",
			    ztest_opts.zo_pool);
			(void) spa_rename(tmpname, ztest_opts.zo_pool);
		}
		kernel_fini();

		ztest_run_zdb(ztest_opts.zo_pool);
	}

	if (ztest_opts.zo_verbose >= 1) {
		if (hasalt) {
			(void) printf("%d runs of older ztest: %s\n", older,
			    ztest_opts.zo_alt_ztest);
			(void) printf("%d runs of newer ztest: %s\n", newer,
			    cmd);
		}
		(void) printf("%d killed, %d completed, %.0f%% kill rate\n",
		    kills, iters - kills, (100.0 * kills) / MAX(1, iters));
	}

	umem_free(cmd, MAXNAMELEN);

	return (0);
}
