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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012 by Frederik Wessels. All rights reserved.
 * Copyright (c) 2012 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 * Copyright (c) 2013 by Prasad Joshi (sTec). All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 * Copyright 2016 Nexenta Systems, Inc.
 * Copyright (c) 2017 Datto Inc.
 */

#include <solaris.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <priv.h>
#include <pwd.h>
#include <zone.h>
#include <sys/time.h>
#include <zfs_prop.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>

#include <libzfs.h>

#include "zpool_util.h"
#include "zfs_comutil.h"
#include "zfeature_common.h"

#include "statcommon.h"

static int zpool_do_create(int, char **);
static int zpool_do_destroy(int, char **);

static int zpool_do_add(int, char **);
static int zpool_do_remove(int, char **);
static int zpool_do_labelclear(int, char **);

static int zpool_do_checkpoint(int, char **);

static int zpool_do_list(int, char **);
static int zpool_do_iostat(int, char **);
static int zpool_do_status(int, char **);

static int zpool_do_online(int, char **);
static int zpool_do_offline(int, char **);
static int zpool_do_clear(int, char **);
static int zpool_do_reopen(int, char **);

static int zpool_do_reguid(int, char **);

static int zpool_do_attach(int, char **);
static int zpool_do_detach(int, char **);
static int zpool_do_replace(int, char **);
static int zpool_do_split(int, char **);

static int zpool_do_initialize(int, char **);
static int zpool_do_scrub(int, char **);

static int zpool_do_import(int, char **);
static int zpool_do_export(int, char **);

static int zpool_do_upgrade(int, char **);

static int zpool_do_history(int, char **);

static int zpool_do_get(int, char **);
static int zpool_do_set(int, char **);

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */

#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif

typedef enum {
	HELP_ADD,
	HELP_ATTACH,
	HELP_CLEAR,
	HELP_CREATE,
	HELP_CHECKPOINT,
	HELP_DESTROY,
	HELP_DETACH,
	HELP_EXPORT,
	HELP_HISTORY,
	HELP_IMPORT,
	HELP_IOSTAT,
	HELP_LABELCLEAR,
	HELP_LIST,
	HELP_OFFLINE,
	HELP_ONLINE,
	HELP_REPLACE,
	HELP_REMOVE,
	HELP_INITIALIZE,
	HELP_SCRUB,
	HELP_STATUS,
	HELP_UPGRADE,
	HELP_GET,
	HELP_SET,
	HELP_SPLIT,
	HELP_REGUID,
	HELP_REOPEN
} zpool_help_t;


typedef struct zpool_command {
	const char	*name;
	int		(*func)(int, char **);
	zpool_help_t	usage;
} zpool_command_t;

/*
 * Master command table.  Each ZFS command has a name, associated function, and
 * usage message.  The usage messages need to be internationalized, so we have
 * to have a function to return the usage message based on a command index.
 *
 * These commands are organized according to how they are displayed in the usage
 * message.  An empty command (one with a NULL name) indicates an empty line in
 * the generic usage message.
 */
static zpool_command_t command_table[] = {
	{ "create",	zpool_do_create,	HELP_CREATE		},
	{ "destroy",	zpool_do_destroy,	HELP_DESTROY		},
	{ NULL },
	{ "add",	zpool_do_add,		HELP_ADD		},
	{ "remove",	zpool_do_remove,	HELP_REMOVE		},
	{ NULL },
	{ "labelclear",	zpool_do_labelclear,	HELP_LABELCLEAR		},
	{ NULL },
	{ "checkpoint",	zpool_do_checkpoint,	HELP_CHECKPOINT		},
	{ NULL },
	{ "list",	zpool_do_list,		HELP_LIST		},
	{ "iostat",	zpool_do_iostat,	HELP_IOSTAT		},
	{ "status",	zpool_do_status,	HELP_STATUS		},
	{ NULL },
	{ "online",	zpool_do_online,	HELP_ONLINE		},
	{ "offline",	zpool_do_offline,	HELP_OFFLINE		},
	{ "clear",	zpool_do_clear,		HELP_CLEAR		},
	{ "reopen",	zpool_do_reopen,	HELP_REOPEN		},
	{ NULL },
	{ "attach",	zpool_do_attach,	HELP_ATTACH		},
	{ "detach",	zpool_do_detach,	HELP_DETACH		},
	{ "replace",	zpool_do_replace,	HELP_REPLACE		},
	{ "split",	zpool_do_split,		HELP_SPLIT		},
	{ NULL },
	{ "initialize",	zpool_do_initialize,	HELP_INITIALIZE		},
	{ "scrub",	zpool_do_scrub,		HELP_SCRUB		},
	{ NULL },
	{ "import",	zpool_do_import,	HELP_IMPORT		},
	{ "export",	zpool_do_export,	HELP_EXPORT		},
	{ "upgrade",	zpool_do_upgrade,	HELP_UPGRADE		},
	{ "reguid",	zpool_do_reguid,	HELP_REGUID		},
	{ NULL },
	{ "history",	zpool_do_history,	HELP_HISTORY		},
	{ "get",	zpool_do_get,		HELP_GET		},
	{ "set",	zpool_do_set,		HELP_SET		},
};

#define	NCOMMAND	(sizeof (command_table) / sizeof (command_table[0]))

static zpool_command_t *current_command;
static char history_str[HIS_MAX_RECORD_LEN];
static boolean_t log_history = B_TRUE;
static uint_t timestamp_fmt = NODATE;

static const char *
get_usage(zpool_help_t idx)
{
	switch (idx) {
	case HELP_ADD:
		return (gettext("\tadd [-fn] <pool> <vdev> ...\n"));
	case HELP_ATTACH:
		return (gettext("\tattach [-f] <pool> <device> "
		    "<new-device>\n"));
	case HELP_CLEAR:
		return (gettext("\tclear [-nF] <pool> [device]\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-fnd] [-B] "
		    "[-o property=value] ... \n"
		    "\t    [-O file-system-property=value] ...\n"
		    "\t    [-m mountpoint] [-R root] [-t tempname] "
		    "<pool> <vdev> ...\n"));
	case HELP_CHECKPOINT:
		return (gettext("\tcheckpoint [--discard] <pool> ...\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-f] <pool>\n"));
	case HELP_DETACH:
		return (gettext("\tdetach <pool> <device>\n"));
	case HELP_EXPORT:
		return (gettext("\texport [-f] <pool> ...\n"));
	case HELP_HISTORY:
		return (gettext("\thistory [-il] [<pool>] ...\n"));
	case HELP_IMPORT:
		return (gettext("\timport [-d dir] [-D]\n"
		    "\timport [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]] -a\n"
		    "\timport [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]] [-t]\n"
		    "\t    [--rewind-to-checkpoint] <pool | id> [newpool]\n"));
	case HELP_IOSTAT:
		return (gettext("\tiostat [-v] [-T d|u] [pool] ... [interval "
		    "[count]]\n"));
	case HELP_LABELCLEAR:
		return (gettext("\tlabelclear [-f] <vdev>\n"));
	case HELP_LIST:
		return (gettext("\tlist [-Hpv] [-o property[,...]] "
		    "[-T d|u] [pool] ... [interval [count]]\n"));
	case HELP_OFFLINE:
		return (gettext("\toffline [-t] <pool> <device> ...\n"));
	case HELP_ONLINE:
		return (gettext("\tonline [-e] <pool> <device> ...\n"));
	case HELP_REPLACE:
		return (gettext("\treplace [-f] <pool> <device> "
		    "[new-device]\n"));
	case HELP_REMOVE:
		return (gettext("\tremove [-nps] <pool> <device> ...\n"));
	case HELP_REOPEN:
		return (gettext("\treopen <pool>\n"));
	case HELP_INITIALIZE:
		return (gettext("\tinitialize [-cs] <pool> [<device> ...]\n"));
	case HELP_SCRUB:
		return (gettext("\tscrub [-s | -p] <pool> ...\n"));
	case HELP_STATUS:
		return (gettext("\tstatus [-vx] [-T d|u] [pool] ... [interval "
		    "[count]]\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade [-v]\n"
		    "\tupgrade [-V version] <-a | pool ...>\n"));
	case HELP_GET:
		return (gettext("\tget [-Hp] [-o \"all\" | field[,...]] "
		    "<\"all\" | property[,...]> <pool> ...\n"));
	case HELP_SET:
		return (gettext("\tset <property=value> <pool> \n"));
	case HELP_SPLIT:
		return (gettext("\tsplit [-n] [-R altroot] [-o mntopts]\n"
		    "\t    [-o property=value] <pool> <newpool> "
		    "[<device> ...]\n"));
	case HELP_REGUID:
		return (gettext("\treguid <pool>\n"));
	}

	abort();
	/* NOTREACHED */
}


/*
 * Callback routine that will print out a pool property value.
 */
static int
print_prop_cb(int prop, void *cb)
{
	FILE *fp = cb;

	(void) fprintf(fp, "\t%-15s  ", zpool_prop_to_name(prop));

	if (zpool_prop_readonly(prop))
		(void) fprintf(fp, "  NO   ");
	else
		(void) fprintf(fp, " YES   ");

	if (zpool_prop_values(prop) == NULL)
		(void) fprintf(fp, "-\n");
	else
		(void) fprintf(fp, "%s\n", zpool_prop_values(prop));

	return (ZPROP_CONT);
}

/*
 * Display usage message.  If we're inside a command, display only the usage for
 * that command.  Otherwise, iterate over the entire command table and display
 * a complete usage message.
 */
void
usage(boolean_t requested)
{
	FILE *fp = requested ? stdout : stderr;

	if (current_command == NULL) {
		int i;

		(void) fprintf(fp, gettext("usage: zpool command args ...\n"));
		(void) fprintf(fp,
		    gettext("where 'command' is one of the following:\n\n"));

		for (i = 0; i < NCOMMAND; i++) {
			if (command_table[i].name == NULL)
				(void) fprintf(fp, "\n");
			else
				(void) fprintf(fp, "%s",
				    get_usage(command_table[i].usage));
		}
	} else {
		(void) fprintf(fp, gettext("usage:\n"));
		(void) fprintf(fp, "%s", get_usage(current_command->usage));
	}

	if (current_command != NULL &&
	    ((strcmp(current_command->name, "set") == 0) ||
	    (strcmp(current_command->name, "get") == 0) ||
	    (strcmp(current_command->name, "list") == 0))) {

		(void) fprintf(fp,
		    gettext("\nthe following properties are supported:\n"));

		(void) fprintf(fp, "\n\t%-15s  %s   %s\n\n",
		    "PROPERTY", "EDIT", "VALUES");

		/* Iterate over all properties */
		(void) zprop_iter(print_prop_cb, fp, B_FALSE, B_TRUE,
		    ZFS_TYPE_POOL);

		(void) fprintf(fp, "\t%-15s   ", "feature@...");
		(void) fprintf(fp, "YES   disabled | enabled | active\n");

		(void) fprintf(fp, gettext("\nThe feature@ properties must be "
		    "appended with a feature name.\nSee zpool-features(7).\n"));
	}

	/*
	 * See comments at end of main().
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	exit(requested ? 0 : 2);
}

void
print_vdev_tree(zpool_handle_t *zhp, const char *name, nvlist_t *nv, int indent,
    boolean_t print_logs)
{
	nvlist_t **child;
	uint_t c, children;
	char *vname;

	if (name != NULL)
		(void) printf("\t%*s%s\n", indent, "", name);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if ((is_log && !print_logs) || (!is_log && print_logs))
			continue;

		vname = zpool_vdev_name(g_zfs, zhp, child[c], B_FALSE);
		print_vdev_tree(zhp, vname, child[c], indent + 2,
		    B_FALSE);
		free(vname);
	}
}

static boolean_t
prop_list_contains_feature(nvlist_t *proplist)
{
	nvpair_t *nvp;
	for (nvp = nvlist_next_nvpair(proplist, NULL); NULL != nvp;
	    nvp = nvlist_next_nvpair(proplist, nvp)) {
		if (zpool_prop_feature(nvpair_name(nvp)))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Add a property pair (name, string-value) into a property nvlist.
 */
static int
add_prop_list(const char *propname, char *propval, nvlist_t **props,
    boolean_t poolprop)
{
	zpool_prop_t prop = ZPROP_INVAL;
	zfs_prop_t fprop;
	nvlist_t *proplist;
	const char *normnm;
	char *strval;

	if (*props == NULL &&
	    nvlist_alloc(props, NV_UNIQUE_NAME, 0) != 0) {
		(void) fprintf(stderr,
		    gettext("internal error: out of memory\n"));
		return (1);
	}

	proplist = *props;

	if (poolprop) {
		const char *vname = zpool_prop_to_name(ZPOOL_PROP_VERSION);

		if ((prop = zpool_name_to_prop(propname)) == ZPROP_INVAL &&
		    !zpool_prop_feature(propname)) {
			(void) fprintf(stderr, gettext("property '%s' is "
			    "not a valid pool property\n"), propname);
			return (2);
		}

		/*
		 * feature@ properties and version should not be specified
		 * at the same time.
		 */
		if ((prop == ZPOOL_PROP_INVAL && zpool_prop_feature(propname) &&
		    nvlist_exists(proplist, vname)) ||
		    (prop == ZPOOL_PROP_VERSION &&
		    prop_list_contains_feature(proplist))) {
			(void) fprintf(stderr, gettext("'feature@' and "
			    "'version' properties cannot be specified "
			    "together\n"));
			return (2);
		}


		if (zpool_prop_feature(propname))
			normnm = propname;
		else
			normnm = zpool_prop_to_name(prop);
	} else {
		if ((fprop = zfs_name_to_prop(propname)) != ZPROP_INVAL) {
			normnm = zfs_prop_to_name(fprop);
		} else {
			normnm = propname;
		}
	}

	if (nvlist_lookup_string(proplist, normnm, &strval) == 0 &&
	    prop != ZPOOL_PROP_CACHEFILE) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (2);
	}

	if (nvlist_add_string(proplist, normnm, propval) != 0) {
		(void) fprintf(stderr, gettext("internal "
		    "error: out of memory\n"));
		return (1);
	}

	return (0);
}

/*
 * Set a default property pair (name, string-value) in a property nvlist
 */
static int
add_prop_list_default(const char *propname, char *propval, nvlist_t **props,
    boolean_t poolprop)
{
	char *pval;

	if (nvlist_lookup_string(*props, propname, &pval) == 0)
		return (0);

	return (add_prop_list(propname, propval, props, poolprop));
}

/*
 * zpool add [-fn] <pool> <vdev> ...
 *
 *	-f	Force addition of devices, even if they appear in use
 *	-n	Do not add the devices, but display the resulting layout if
 *		they were to be added.
 *
 * Adds the given vdevs to 'pool'.  As with create, the bulk of this work is
 * handled by get_vdev_spec(), which constructs the nvlist needed to pass to
 * libzfs.
 */
int
zpool_do_add(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	boolean_t dryrun = B_FALSE;
	int c;
	nvlist_t *nvroot;
	char *poolname;
	zpool_boot_label_t boot_type;
	uint64_t boot_size;
	int ret;
	zpool_handle_t *zhp;
	nvlist_t *config;

	/* check options */
	while ((c = getopt(argc, argv, "fn")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing vdev specification\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	argc--;
	argv++;

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	if ((config = zpool_get_config(zhp, NULL)) == NULL) {
		(void) fprintf(stderr, gettext("pool '%s' is unavailable\n"),
		    poolname);
		zpool_close(zhp);
		return (1);
	}

	if (zpool_is_bootable(zhp))
		boot_type = ZPOOL_COPY_BOOT_LABEL;
	else
		boot_type = ZPOOL_NO_BOOT_LABEL;

	/* pass off to get_vdev_spec for processing */
	boot_size = zpool_get_prop_int(zhp, ZPOOL_PROP_BOOTSIZE, NULL);
	nvroot = make_root_vdev(zhp, force, !force, B_FALSE, dryrun,
	    boot_type, boot_size, argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		return (1);
	}

	if (dryrun) {
		nvlist_t *poolnvroot;

		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &poolnvroot) == 0);

		(void) printf(gettext("would update '%s' to the following "
		    "configuration:\n"), zpool_get_name(zhp));

		/* print original main pool and new tree */
		print_vdev_tree(zhp, poolname, poolnvroot, 0, B_FALSE);
		print_vdev_tree(zhp, NULL, nvroot, 0, B_FALSE);

		/* Do the same for the logs */
		if (num_logs(poolnvroot) > 0) {
			print_vdev_tree(zhp, "logs", poolnvroot, 0, B_TRUE);
			print_vdev_tree(zhp, NULL, nvroot, 0, B_TRUE);
		} else if (num_logs(nvroot) > 0) {
			print_vdev_tree(zhp, "logs", nvroot, 0, B_TRUE);
		}

		ret = 0;
	} else {
		ret = (zpool_add(zhp, nvroot) != 0);
	}

	nvlist_free(nvroot);
	zpool_close(zhp);

	return (ret);
}

/*
 * zpool remove  <pool> <vdev> ...
 *
 * Removes the given vdev from the pool.
 */
int
zpool_do_remove(int argc, char **argv)
{
	char *poolname;
	int i, ret = 0;
	zpool_handle_t *zhp;
	boolean_t stop = B_FALSE;
	boolean_t noop = B_FALSE;
	boolean_t parsable = B_FALSE;
	char c;

	/* check options */
	while ((c = getopt(argc, argv, "nps")) != -1) {
		switch (c) {
		case 'n':
			noop = B_TRUE;
			break;
		case 'p':
			parsable = B_TRUE;
			break;
		case 's':
			stop = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	if (stop && noop) {
		(void) fprintf(stderr, gettext("stop request ignored\n"));
		return (0);
	}

	if (stop) {
		if (argc > 1) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}
		if (zpool_vdev_remove_cancel(zhp) != 0)
			ret = 1;
	} else {
		if (argc < 2) {
			(void) fprintf(stderr, gettext("missing device\n"));
			usage(B_FALSE);
		}

		for (i = 1; i < argc; i++) {
			if (noop) {
				uint64_t size;

				if (zpool_vdev_indirect_size(zhp, argv[i],
				    &size) != 0) {
					ret = 1;
					break;
				}
				if (parsable) {
					(void) printf("%s %llu\n",
					    argv[i], size);
				} else {
					char valstr[32];
					zfs_nicenum(size, valstr,
					    sizeof (valstr));
					(void) printf("Memory that will be "
					    "used after removing %s: %s\n",
					    argv[i], valstr);
				}
			} else {
				if (zpool_vdev_remove(zhp, argv[i]) != 0)
					ret = 1;
			}
		}
	}

	return (ret);
}

/*
 * zpool labelclear [-f] <vdev>
 *
 *	-f	Force clearing the label for the vdevs which are members of
 *		the exported or foreign pools.
 *
 * Verifies that the vdev is not active and zeros out the label information
 * on the device.
 */
int
zpool_do_labelclear(int argc, char **argv)
{
	char vdev[MAXPATHLEN];
	char *name = NULL;
	struct stat st;
	int c, fd, ret = 0;
	nvlist_t *config;
	pool_state_t state;
	boolean_t inuse = B_FALSE;
	boolean_t force = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get vdev name */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing vdev name\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	/*
	 * Check if we were given absolute path and use it as is.
	 * Otherwise if the provided vdev name doesn't point to a file,
	 * try prepending dsk path and appending s0.
	 */
	(void) strlcpy(vdev, argv[0], sizeof (vdev));
	if (vdev[0] != '/' && stat(vdev, &st) != 0) {
		char *s;

		(void) snprintf(vdev, sizeof (vdev), "%s/%s",
#ifdef illumos
		    ZFS_DISK_ROOT, argv[0]);
		if ((s = strrchr(argv[0], 's')) == NULL ||
		    !isdigit(*(s + 1)))
			(void) strlcat(vdev, "s0", sizeof (vdev));
#else
		    "/dev", argv[0]);
#endif
		if (stat(vdev, &st) != 0) {
			(void) fprintf(stderr, gettext(
			    "failed to find device %s, try specifying absolute "
			    "path instead\n"), argv[0]);
			return (1);
		}
	}

	if ((fd = open(vdev, O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("failed to open %s: %s\n"),
		    vdev, strerror(errno));
		return (1);
	}

	if (zpool_read_label(fd, &config) != 0) {
		(void) fprintf(stderr,
		    gettext("failed to read label from %s\n"), vdev);
		return (1);
	}
	nvlist_free(config);

	ret = zpool_in_use(g_zfs, fd, &state, &name, &inuse);
	if (ret != 0) {
		(void) fprintf(stderr,
		    gettext("failed to check state for %s\n"), vdev);
		return (1);
	}

	if (!inuse)
		goto wipe_label;

	switch (state) {
	default:
	case POOL_STATE_ACTIVE:
	case POOL_STATE_SPARE:
	case POOL_STATE_L2CACHE:
		(void) fprintf(stderr, gettext(
		    "%s is a member (%s) of pool \"%s\"\n"),
		    vdev, zpool_pool_state_to_name(state), name);
		ret = 1;
		goto errout;

	case POOL_STATE_EXPORTED:
		if (force)
			break;
		(void) fprintf(stderr, gettext(
		    "use '-f' to override the following error:\n"
		    "%s is a member of exported pool \"%s\"\n"),
		    vdev, name);
		ret = 1;
		goto errout;

	case POOL_STATE_POTENTIALLY_ACTIVE:
		if (force)
			break;
		(void) fprintf(stderr, gettext(
		    "use '-f' to override the following error:\n"
		    "%s is a member of potentially active pool \"%s\"\n"),
		    vdev, name);
		ret = 1;
		goto errout;

	case POOL_STATE_DESTROYED:
		/* inuse should never be set for a destroyed pool */
		assert(0);
		break;
	}

wipe_label:
	ret = zpool_clear_label(fd);
	if (ret != 0) {
		(void) fprintf(stderr,
		    gettext("failed to clear label for %s\n"), vdev);
	}

errout:
	free(name);
	(void) close(fd);

	return (ret);
}

/*
 * zpool create [-fnd] [-B] [-o property=value] ...
 *		[-O file-system-property=value] ...
 *		[-R root] [-m mountpoint] [-t tempname] <pool> <dev> ...
 *
 *	-B	Create boot partition.
 *	-f	Force creation, even if devices appear in use
 *	-n	Do not create the pool, but display the resulting layout if it
 *		were to be created.
 *	-R	Create a pool under an alternate root
 *	-m	Set default mountpoint for the root dataset.  By default it's
 *		'/<pool>'
 *	-t	Use the temporary name until the pool is exported.
 *	-o	Set property=value.
 *	-d	Don't automatically enable all supported pool features
 *		(individual features can be enabled with -o).
 *	-O	Set fsproperty=value in the pool's root file system
 *
 * Creates the named pool according to the given vdev specification.  The
 * bulk of the vdev processing is done in get_vdev_spec() in zpool_vdev.c.  Once
 * we get the nvlist back from get_vdev_spec(), we either print out the contents
 * (if '-n' was specified), or pass it to libzfs to do the creation.
 */

#define	SYSTEM256	(256 * 1024 * 1024)
int
zpool_do_create(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	boolean_t dryrun = B_FALSE;
	boolean_t enable_all_pool_feat = B_TRUE;
	zpool_boot_label_t boot_type = ZPOOL_NO_BOOT_LABEL;
	uint64_t boot_size = 0;
	int c;
	nvlist_t *nvroot = NULL;
	char *poolname;
	char *tname = NULL;
	int ret = 1;
	char *altroot = NULL;
	char *mountpoint = NULL;
	nvlist_t *fsprops = NULL;
	nvlist_t *props = NULL;
	char *propval;

	/* check options */
	while ((c = getopt(argc, argv, ":fndBR:m:o:O:t:")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'd':
			enable_all_pool_feat = B_FALSE;
			break;
		case 'B':
#ifdef illumos
			/*
			 * We should create the system partition.
			 * Also make sure the size is set.
			 */
			boot_type = ZPOOL_CREATE_BOOT_LABEL;
			if (boot_size == 0)
				boot_size = SYSTEM256;
			break;
#else
			(void) fprintf(stderr,
			    gettext("option '%c' is not supported\n"),
			    optopt);
			goto badusage;
#endif
		case 'R':
			altroot = optarg;
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE))
				goto errout;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto errout;
			break;
		case 'm':
			/* Equivalent to -O mountpoint=optarg */
			mountpoint = optarg;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -o option\n"));
				goto errout;
			}
			*propval = '\0';
			propval++;

			if (add_prop_list(optarg, propval, &props, B_TRUE))
				goto errout;

			/*
			 * Get bootsize value for make_root_vdev().
			 */
			if (zpool_name_to_prop(optarg) == ZPOOL_PROP_BOOTSIZE) {
				if (zfs_nicestrtonum(g_zfs, propval,
				    &boot_size) < 0 || boot_size == 0) {
					(void) fprintf(stderr,
					    gettext("bad boot partition size "
					    "'%s': %s\n"),  propval,
					    libzfs_error_description(g_zfs));
					goto errout;
				}
			}

			/*
			 * If the user is creating a pool that doesn't support
			 * feature flags, don't enable any features.
			 */
			if (zpool_name_to_prop(optarg) == ZPOOL_PROP_VERSION) {
				char *end;
				u_longlong_t ver;

				ver = strtoull(propval, &end, 10);
				if (*end == '\0' &&
				    ver < SPA_VERSION_FEATURES) {
					enable_all_pool_feat = B_FALSE;
				}
			}
			if (zpool_name_to_prop(optarg) == ZPOOL_PROP_ALTROOT)
				altroot = propval;
			break;
		case 'O':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -O option\n"));
				goto errout;
			}
			*propval = '\0';
			propval++;

			/*
			 * Mountpoints are checked and then added later.
			 * Uniquely among properties, they can be specified
			 * more than once, to avoid conflict with -m.
			 */
			if (0 == strcmp(optarg,
			    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT))) {
				mountpoint = propval;
			} else if (add_prop_list(optarg, propval, &fsprops,
			    B_FALSE)) {
				goto errout;
			}
			break;
		case 't':
			/*
			 * Sanity check temporary pool name.
			 */
			if (strchr(optarg, '/') != NULL) {
				(void) fprintf(stderr, gettext("cannot create "
				    "'%s': invalid character '/' in temporary "
				    "name\n"), optarg);
				(void) fprintf(stderr, gettext("use 'zfs "
				    "create' to create a dataset\n"));
				goto errout;
			}

			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_TNAME), optarg, &props, B_TRUE))
				goto errout;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto errout;
			tname = optarg;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			goto badusage;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto badusage;
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		goto badusage;
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing vdev specification\n"));
		goto badusage;
	}

	poolname = argv[0];

	/*
	 * As a special case, check for use of '/' in the name, and direct the
	 * user to use 'zfs create' instead.
	 */
	if (strchr(poolname, '/') != NULL) {
		(void) fprintf(stderr, gettext("cannot create '%s': invalid "
		    "character '/' in pool name\n"), poolname);
		(void) fprintf(stderr, gettext("use 'zfs create' to "
		    "create a dataset\n"));
		goto errout;
	}

	/*
	 * Make sure the bootsize is set when ZPOOL_CREATE_BOOT_LABEL is used,
	 * and not set otherwise.
	 */
	if (boot_type == ZPOOL_CREATE_BOOT_LABEL) {
		const char *propname;
		char *strptr, *buf = NULL;
		int rv;

		propname = zpool_prop_to_name(ZPOOL_PROP_BOOTSIZE);
		if (nvlist_lookup_string(props, propname, &strptr) != 0) {
			(void) asprintf(&buf, "%" PRIu64, boot_size);
			if (buf == NULL) {
				(void) fprintf(stderr,
				    gettext("internal error: out of memory\n"));
				goto errout;
			}
			rv = add_prop_list(propname, buf, &props, B_TRUE);
			free(buf);
			if (rv != 0)
				goto errout;
		}
	} else {
		const char *propname;
		char *strptr;

		propname = zpool_prop_to_name(ZPOOL_PROP_BOOTSIZE);
		if (nvlist_lookup_string(props, propname, &strptr) == 0) {
			(void) fprintf(stderr, gettext("error: setting boot "
			    "partition size requires option '-B'\n"));
			goto errout;
		}
	}

	/* pass off to get_vdev_spec for bulk processing */
	nvroot = make_root_vdev(NULL, force, !force, B_FALSE, dryrun,
	    boot_type, boot_size, argc - 1, argv + 1);
	if (nvroot == NULL)
		goto errout;

	/* make_root_vdev() allows 0 toplevel children if there are spares */
	if (!zfs_allocatable_devs(nvroot)) {
		(void) fprintf(stderr, gettext("invalid vdev "
		    "specification: at least one toplevel vdev must be "
		    "specified\n"));
		goto errout;
	}

	if (altroot != NULL && altroot[0] != '/') {
		(void) fprintf(stderr, gettext("invalid alternate root '%s': "
		    "must be an absolute path\n"), altroot);
		goto errout;
	}

	/*
	 * Check the validity of the mountpoint and direct the user to use the
	 * '-m' mountpoint option if it looks like its in use.
	 * Ignore the checks if the '-f' option is given.
	 */
	if (!force && (mountpoint == NULL ||
	    (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) != 0 &&
	    strcmp(mountpoint, ZFS_MOUNTPOINT_NONE) != 0))) {
		char buf[MAXPATHLEN];
		DIR *dirp;

		if (mountpoint && mountpoint[0] != '/') {
			(void) fprintf(stderr, gettext("invalid mountpoint "
			    "'%s': must be an absolute path, 'legacy', or "
			    "'none'\n"), mountpoint);
			goto errout;
		}

		if (mountpoint == NULL) {
			if (altroot != NULL)
				(void) snprintf(buf, sizeof (buf), "%s/%s",
				    altroot, poolname);
			else
				(void) snprintf(buf, sizeof (buf), "/%s",
				    poolname);
		} else {
			if (altroot != NULL)
				(void) snprintf(buf, sizeof (buf), "%s%s",
				    altroot, mountpoint);
			else
				(void) snprintf(buf, sizeof (buf), "%s",
				    mountpoint);
		}

		if ((dirp = opendir(buf)) == NULL && errno != ENOENT) {
			(void) fprintf(stderr, gettext("mountpoint '%s' : "
			    "%s\n"), buf, strerror(errno));
			(void) fprintf(stderr, gettext("use '-m' "
			    "option to provide a different default\n"));
			goto errout;
		} else if (dirp) {
			int count = 0;

			while (count < 3 && readdir(dirp) != NULL)
				count++;
			(void) closedir(dirp);

			if (count > 2) {
				(void) fprintf(stderr, gettext("mountpoint "
				    "'%s' exists and is not empty\n"), buf);
				(void) fprintf(stderr, gettext("use '-m' "
				    "option to provide a "
				    "different default\n"));
				goto errout;
			}
		}
	}

	/*
	 * Now that the mountpoint's validity has been checked, ensure that
	 * the property is set appropriately prior to creating the pool.
	 */
	if (mountpoint != NULL) {
		ret = add_prop_list(zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    mountpoint, &fsprops, B_FALSE);
		if (ret != 0)
			goto errout;
	}

	ret = 1;
	if (dryrun) {
		/*
		 * For a dry run invocation, print out a basic message and run
		 * through all the vdevs in the list and print out in an
		 * appropriate hierarchy.
		 */
		(void) printf(gettext("would create '%s' with the "
		    "following layout:\n\n"), poolname);

		print_vdev_tree(NULL, poolname, nvroot, 0, B_FALSE);
		if (num_logs(nvroot) > 0)
			print_vdev_tree(NULL, "logs", nvroot, 0, B_TRUE);

		ret = 0;
	} else {
		/*
		 * Hand off to libzfs.
		 */
		if (enable_all_pool_feat) {
			spa_feature_t i;
			for (i = 0; i < SPA_FEATURES; i++) {
				char propname[MAXPATHLEN];
				zfeature_info_t *feat = &spa_feature_table[i];

				(void) snprintf(propname, sizeof (propname),
				    "feature@%s", feat->fi_uname);

				/*
				 * Skip feature if user specified it manually
				 * on the command line.
				 */
				if (nvlist_exists(props, propname))
					continue;

				ret = add_prop_list(propname,
				    ZFS_FEATURE_ENABLED, &props, B_TRUE);
				if (ret != 0)
					goto errout;
			}
		}

		ret = 1;
		if (zpool_create(g_zfs, poolname,
		    nvroot, props, fsprops) == 0) {
			zfs_handle_t *pool = zfs_open(g_zfs,
			    tname ? tname : poolname, ZFS_TYPE_FILESYSTEM);
			if (pool != NULL) {
				if (zfs_mount(pool, NULL, 0) == 0)
					ret = zfs_shareall(pool);
				zfs_close(pool);
			}
		} else if (libzfs_errno(g_zfs) == EZFS_INVALIDNAME) {
			(void) fprintf(stderr, gettext("pool name may have "
			    "been omitted\n"));
		}
	}

errout:
	nvlist_free(nvroot);
	nvlist_free(fsprops);
	nvlist_free(props);
	return (ret);
badusage:
	nvlist_free(fsprops);
	nvlist_free(props);
	usage(B_FALSE);
	return (2);
}

/*
 * zpool destroy <pool>
 *
 *	-f	Forcefully unmount any datasets
 *
 * Destroy the given pool.  Automatically unmounts any datasets in the pool.
 */
int
zpool_do_destroy(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	int c;
	char *pool;
	zpool_handle_t *zhp;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	pool = argv[0];

	if ((zhp = zpool_open_canfail(g_zfs, pool)) == NULL) {
		/*
		 * As a special case, check for use of '/' in the name, and
		 * direct the user to use 'zfs destroy' instead.
		 */
		if (strchr(pool, '/') != NULL)
			(void) fprintf(stderr, gettext("use 'zfs destroy' to "
			    "destroy a dataset\n"));
		return (1);
	}

	if (zpool_disable_datasets(zhp, force) != 0) {
		(void) fprintf(stderr, gettext("could not destroy '%s': "
		    "could not unmount datasets\n"), zpool_get_name(zhp));
		return (1);
	}

	/* The history must be logged as part of the export */
	log_history = B_FALSE;

	ret = (zpool_destroy(zhp, history_str) != 0);

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool export [-f] <pool> ...
 *
 *	-f	Forcefully unmount datasets
 *
 * Export the given pools.  By default, the command will attempt to cleanly
 * unmount any active datasets within the pool.  If the '-f' flag is specified,
 * then the datasets will be forcefully unmounted.
 */
int
zpool_do_export(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	boolean_t hardforce = B_FALSE;
	int c;
	zpool_handle_t *zhp;
	int ret;
	int i;

	/* check options */
	while ((c = getopt(argc, argv, "fF")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'F':
			hardforce = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool argument\n"));
		usage(B_FALSE);
	}

	ret = 0;
	for (i = 0; i < argc; i++) {
		if ((zhp = zpool_open_canfail(g_zfs, argv[i])) == NULL) {
			ret = 1;
			continue;
		}

		if (zpool_disable_datasets(zhp, force) != 0) {
			ret = 1;
			zpool_close(zhp);
			continue;
		}

		/* The history must be logged as part of the export */
		log_history = B_FALSE;

		if (hardforce) {
			if (zpool_export_force(zhp, history_str) != 0)
				ret = 1;
		} else if (zpool_export(zhp, force, history_str) != 0) {
			ret = 1;
		}

		zpool_close(zhp);
	}

	return (ret);
}

/*
 * Given a vdev configuration, determine the maximum width needed for the device
 * name column.
 */
static int
max_width(zpool_handle_t *zhp, nvlist_t *nv, int depth, int max)
{
	char *name = zpool_vdev_name(g_zfs, zhp, nv, B_TRUE);
	nvlist_t **child;
	uint_t c, children;
	int ret;

	if (strlen(name) + depth > max)
		max = strlen(name) + depth;

	free(name);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max)) > max)
				max = ret;
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max)) > max)
				max = ret;
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max)) > max)
				max = ret;
	}


	return (max);
}

typedef struct spare_cbdata {
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
} spare_cbdata_t;

static boolean_t
find_vdev(nvlist_t *nv, uint64_t search)
{
	uint64_t guid;
	nvlist_t **child;
	uint_t c, children;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0 &&
	    search == guid)
		return (B_TRUE);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_vdev(child[c], search))
				return (B_TRUE);
	}

	return (B_FALSE);
}

static int
find_spare(zpool_handle_t *zhp, void *data)
{
	spare_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	if (find_vdev(nvroot, cbp->cb_guid)) {
		cbp->cb_zhp = zhp;
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Print out configuration state as requested by status_callback.
 */
void
print_status_config(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    int namewidth, int depth, boolean_t isspare)
{
	nvlist_t **child;
	uint_t c, vsc, children;
	pool_scan_stat_t *ps = NULL;
	vdev_stat_t *vs;
	char rbuf[6], wbuf[6], cbuf[6];
	char *vname;
	uint64_t notpresent;
	uint64_t ashift;
	spare_cbdata_t cb;
	const char *state;
	char *type;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);

	if (strcmp(type, VDEV_TYPE_INDIRECT) == 0)
		return;

	state = zpool_state_to_name(vs->vs_state, vs->vs_aux);
	if (isspare) {
		/*
		 * For hot spares, we use the terms 'INUSE' and 'AVAILABLE' for
		 * online drives.
		 */
		if (vs->vs_aux == VDEV_AUX_SPARED)
			state = "INUSE";
		else if (vs->vs_state == VDEV_STATE_HEALTHY)
			state = "AVAIL";
	}

	(void) printf("\t%*s%-*s  %-8s", depth, "", namewidth - depth,
	    name, state);

	if (!isspare) {
		zfs_nicenum(vs->vs_read_errors, rbuf, sizeof (rbuf));
		zfs_nicenum(vs->vs_write_errors, wbuf, sizeof (wbuf));
		zfs_nicenum(vs->vs_checksum_errors, cbuf, sizeof (cbuf));
		(void) printf(" %5s %5s %5s", rbuf, wbuf, cbuf);
	}

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
	    &notpresent) == 0 ||
	    vs->vs_state <= VDEV_STATE_CANT_OPEN) {
		char *path;
		if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0)
			(void) printf("  was %s", path);
	} else if (vs->vs_aux != 0) {
		(void) printf("  ");

		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			(void) printf(gettext("cannot open"));
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			(void) printf(gettext("missing device"));
			break;

		case VDEV_AUX_NO_REPLICAS:
			(void) printf(gettext("insufficient replicas"));
			break;

		case VDEV_AUX_VERSION_NEWER:
			(void) printf(gettext("newer version"));
			break;

		case VDEV_AUX_UNSUP_FEAT:
			(void) printf(gettext("unsupported feature(s)"));
			break;

		case VDEV_AUX_ASHIFT_TOO_BIG:
			(void) printf(gettext("unsupported minimum blocksize"));
			break;

		case VDEV_AUX_SPARED:
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
			    &cb.cb_guid) == 0);
			if (zpool_iter(g_zfs, find_spare, &cb) == 1) {
				if (strcmp(zpool_get_name(cb.cb_zhp),
				    zpool_get_name(zhp)) == 0)
					(void) printf(gettext("currently in "
					    "use"));
				else
					(void) printf(gettext("in use by "
					    "pool '%s'"),
					    zpool_get_name(cb.cb_zhp));
				zpool_close(cb.cb_zhp);
			} else {
				(void) printf(gettext("currently in use"));
			}
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			(void) printf(gettext("too many errors"));
			break;

		case VDEV_AUX_IO_FAILURE:
			(void) printf(gettext("experienced I/O failures"));
			break;

		case VDEV_AUX_BAD_LOG:
			(void) printf(gettext("bad intent log"));
			break;

		case VDEV_AUX_EXTERNAL:
			(void) printf(gettext("external device fault"));
			break;

		case VDEV_AUX_SPLIT_POOL:
			(void) printf(gettext("split into new pool"));
			break;

		case VDEV_AUX_CHILDREN_OFFLINE:
			(void) printf(gettext("all children offline"));
			break;

		default:
			(void) printf(gettext("corrupted data"));
			break;
		}
	} else if (children == 0 && !isspare &&
	    VDEV_STAT_VALID(vs_physical_ashift, vsc) &&
	    vs->vs_configured_ashift < vs->vs_physical_ashift) {
		(void) printf(
		    gettext("  block size: %dB configured, %dB native"),
		    1 << vs->vs_configured_ashift, 1 << vs->vs_physical_ashift);
	}

	(void) nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &c);

	if (ps != NULL && ps->pss_state == DSS_SCANNING &&
	    vs->vs_scan_processed != 0 && children == 0) {
		(void) printf(gettext("  (%s)"),
		    (ps->pss_func == POOL_SCAN_RESILVER) ?
		    "resilvering" : "repairing");
	}

	if ((vs->vs_initialize_state == VDEV_INITIALIZE_ACTIVE ||
	    vs->vs_initialize_state == VDEV_INITIALIZE_SUSPENDED ||
	    vs->vs_initialize_state == VDEV_INITIALIZE_COMPLETE) &&
	    !vs->vs_scan_removing) {
		char zbuf[1024];
		char tbuf[256];
		struct tm zaction_ts;

		time_t t = vs->vs_initialize_action_time;
		int initialize_pct = 100;
		if (vs->vs_initialize_state != VDEV_INITIALIZE_COMPLETE) {
			initialize_pct = (vs->vs_initialize_bytes_done * 100 /
			    (vs->vs_initialize_bytes_est + 1));
		}

		(void) localtime_r(&t, &zaction_ts);
		(void) strftime(tbuf, sizeof (tbuf), "%c", &zaction_ts);

		switch (vs->vs_initialize_state) {
		case VDEV_INITIALIZE_SUSPENDED:
			(void) snprintf(zbuf, sizeof (zbuf),
			    ", suspended, started at %s", tbuf);
			break;
		case VDEV_INITIALIZE_ACTIVE:
			(void) snprintf(zbuf, sizeof (zbuf),
			    ", started at %s", tbuf);
			break;
		case VDEV_INITIALIZE_COMPLETE:
			(void) snprintf(zbuf, sizeof (zbuf),
			    ", completed at %s", tbuf);
			break;
		}

		(void) printf(gettext("  (%d%% initialized%s)"),
		    initialize_pct, zbuf);
	}

	(void) printf("\n");

	for (c = 0; c < children; c++) {
		uint64_t islog = B_FALSE, ishole = B_FALSE;

		/* Don't print logs or holes here */
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &islog);
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &ishole);
		if (islog || ishole)
			continue;
		vname = zpool_vdev_name(g_zfs, zhp, child[c], B_TRUE);
		print_status_config(zhp, vname, child[c],
		    namewidth, depth + 2, isspare);
		free(vname);
	}
}


/*
 * Print the configuration of an exported pool.  Iterate over all vdevs in the
 * pool, printing out the name and status for each one.
 */
void
print_import_config(const char *name, nvlist_t *nv, int namewidth, int depth)
{
	nvlist_t **child;
	uint_t c, children;
	vdev_stat_t *vs;
	char *type, *vname;

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
	if (strcmp(type, VDEV_TYPE_MISSING) == 0 ||
	    strcmp(type, VDEV_TYPE_HOLE) == 0)
		return;

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	(void) printf("\t%*s%-*s", depth, "", namewidth - depth, name);
	(void) printf("  %s", zpool_state_to_name(vs->vs_state, vs->vs_aux));

	if (vs->vs_aux != 0) {
		(void) printf("  ");

		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			(void) printf(gettext("cannot open"));
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			(void) printf(gettext("missing device"));
			break;

		case VDEV_AUX_NO_REPLICAS:
			(void) printf(gettext("insufficient replicas"));
			break;

		case VDEV_AUX_VERSION_NEWER:
			(void) printf(gettext("newer version"));
			break;

		case VDEV_AUX_UNSUP_FEAT:
			(void) printf(gettext("unsupported feature(s)"));
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			(void) printf(gettext("too many errors"));
			break;

		case VDEV_AUX_CHILDREN_OFFLINE:
			(void) printf(gettext("all children offline"));
			break;

		default:
			(void) printf(gettext("corrupted data"));
			break;
		}
	}
	(void) printf("\n");

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (is_log)
			continue;

		vname = zpool_vdev_name(g_zfs, NULL, child[c], B_TRUE);
		print_import_config(vname, child[c], namewidth, depth + 2);
		free(vname);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		(void) printf(gettext("\tcache\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c], B_FALSE);
			(void) printf("\t  %s\n", vname);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		(void) printf(gettext("\tspares\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c], B_FALSE);
			(void) printf("\t  %s\n", vname);
			free(vname);
		}
	}
}

/*
 * Print log vdevs.
 * Logs are recorded as top level vdevs in the main pool child array
 * but with "is_log" set to 1. We use either print_status_config() or
 * print_import_config() to print the top level logs then any log
 * children (eg mirrored slogs) are printed recursively - which
 * works because only the top level vdev is marked "is_log"
 */
static void
print_logs(zpool_handle_t *zhp, nvlist_t *nv, int namewidth, boolean_t verbose)
{
	uint_t c, children;
	nvlist_t **child;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0)
		return;

	(void) printf(gettext("\tlogs\n"));

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;
		char *name;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (!is_log)
			continue;
		name = zpool_vdev_name(g_zfs, zhp, child[c], B_TRUE);
		if (verbose)
			print_status_config(zhp, name, child[c], namewidth,
			    2, B_FALSE);
		else
			print_import_config(name, child[c], namewidth, 2);
		free(name);
	}
}

/*
 * Display the status for the given pool.
 */
static void
show_import(nvlist_t *config)
{
	uint64_t pool_state;
	vdev_stat_t *vs;
	char *name;
	uint64_t guid;
	char *msgid;
	nvlist_t *nvroot;
	int reason;
	const char *health;
	uint_t vsc;
	int namewidth;
	char *comment;

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &guid) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &pool_state) == 0);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);
	health = zpool_state_to_name(vs->vs_state, vs->vs_aux);

	reason = zpool_import_status(config, &msgid);

	(void) printf(gettext("   pool: %s\n"), name);
	(void) printf(gettext("     id: %llu\n"), (u_longlong_t)guid);
	(void) printf(gettext("  state: %s"), health);
	if (pool_state == POOL_STATE_DESTROYED)
		(void) printf(gettext(" (DESTROYED)"));
	(void) printf("\n");

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
	case ZPOOL_STATUS_MISSING_DEV_NR:
	case ZPOOL_STATUS_BAD_GUID_SUM:
		(void) printf(gettext(" status: One or more devices are "
		    "missing from the system.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		(void) printf(gettext(" status: One or more devices contains "
		    "corrupted data.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		(void) printf(
		    gettext(" status: The pool data is corrupted.\n"));
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		(void) printf(gettext(" status: One or more devices "
		    "are offlined.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		(void) printf(gettext(" status: The pool metadata is "
		    "corrupted.\n"));
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		(void) printf(gettext(" status: The pool is formatted using a "
		    "legacy on-disk version.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		(void) printf(gettext(" status: The pool is formatted using an "
		    "incompatible version.\n"));
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		(void) printf(gettext(" status: Some supported features are "
		    "not enabled on the pool.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		(void) printf(gettext("status: The pool uses the following "
		    "feature(s) not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		(void) printf(gettext("status: The pool can only be accessed "
		    "in read-only mode on this system. It\n\tcannot be "
		    "accessed in read-write mode because it uses the "
		    "following\n\tfeature(s) not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		(void) printf(gettext(" status: The pool was last accessed by "
		    "another system.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
	case ZPOOL_STATUS_FAULTED_DEV_NR:
		(void) printf(gettext(" status: One or more devices are "
		    "faulted.\n"));
		break;

	case ZPOOL_STATUS_BAD_LOG:
		(void) printf(gettext(" status: An intent log record cannot be "
		    "read.\n"));
		break;

	case ZPOOL_STATUS_RESILVERING:
		(void) printf(gettext(" status: One or more devices were being "
		    "resilvered.\n"));
		break;

	case ZPOOL_STATUS_NON_NATIVE_ASHIFT:
		(void) printf(gettext("status: One or more devices were "
		    "configured to use a non-native block size.\n"
		    "\tExpect reduced performance.\n"));
		break;

	default:
		/*
		 * No other status can be seen when importing pools.
		 */
		assert(reason == ZPOOL_STATUS_OK);
	}

	/*
	 * Print out an action according to the overall state of the pool.
	 */
	if (vs->vs_state == VDEV_STATE_HEALTHY) {
		if (reason == ZPOOL_STATUS_VERSION_OLDER ||
		    reason == ZPOOL_STATUS_FEAT_DISABLED) {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric identifier, "
			    "though\n\tsome features will not be available "
			    "without an explicit 'zpool upgrade'.\n"));
		} else if (reason == ZPOOL_STATUS_HOSTID_MISMATCH) {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric "
			    "identifier and\n\tthe '-f' flag.\n"));
		} else {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric "
			    "identifier.\n"));
		}
	} else if (vs->vs_state == VDEV_STATE_DEGRADED) {
		(void) printf(gettext(" action: The pool can be imported "
		    "despite missing or damaged devices.  The\n\tfault "
		    "tolerance of the pool may be compromised if imported.\n"));
	} else {
		switch (reason) {
		case ZPOOL_STATUS_VERSION_NEWER:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported.  Access the pool on a system running "
			    "newer\n\tsoftware, or recreate the pool from "
			    "backup.\n"));
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_READ:
			(void) printf(gettext("action: The pool cannot be "
			    "imported. Access the pool on a system that "
			    "supports\n\tthe required feature(s), or recreate "
			    "the pool from backup.\n"));
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
			(void) printf(gettext("action: The pool cannot be "
			    "imported in read-write mode. Import the pool "
			    "with\n"
			    "\t\"-o readonly=on\", access the pool on a system "
			    "that supports the\n\trequired feature(s), or "
			    "recreate the pool from backup.\n"));
			break;
		case ZPOOL_STATUS_MISSING_DEV_R:
		case ZPOOL_STATUS_MISSING_DEV_NR:
		case ZPOOL_STATUS_BAD_GUID_SUM:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported. Attach the missing\n\tdevices and try "
			    "again.\n"));
			break;
		default:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported due to damaged devices or data.\n"));
		}
	}

	/* Print the comment attached to the pool. */
	if (nvlist_lookup_string(config, ZPOOL_CONFIG_COMMENT, &comment) == 0)
		(void) printf(gettext("comment: %s\n"), comment);

	/*
	 * If the state is "closed" or "can't open", and the aux state
	 * is "corrupt data":
	 */
	if (((vs->vs_state == VDEV_STATE_CLOSED) ||
	    (vs->vs_state == VDEV_STATE_CANT_OPEN)) &&
	    (vs->vs_aux == VDEV_AUX_CORRUPT_DATA)) {
		if (pool_state == POOL_STATE_DESTROYED)
			(void) printf(gettext("\tThe pool was destroyed, "
			    "but can be imported using the '-Df' flags.\n"));
		else if (pool_state != POOL_STATE_EXPORTED)
			(void) printf(gettext("\tThe pool may be active on "
			    "another system, but can be imported using\n\t"
			    "the '-f' flag.\n"));
	}

	if (msgid != NULL)
		(void) printf(gettext("   see: http://illumos.org/msg/%s\n"),
		    msgid);

	(void) printf(gettext(" config:\n\n"));

	namewidth = max_width(NULL, nvroot, 0, 0);
	if (namewidth < 10)
		namewidth = 10;

	print_import_config(name, nvroot, namewidth, 0);
	if (num_logs(nvroot) > 0)
		print_logs(NULL, nvroot, namewidth, B_FALSE);

	if (reason == ZPOOL_STATUS_BAD_GUID_SUM) {
		(void) printf(gettext("\n\tAdditional devices are known to "
		    "be part of this pool, though their\n\texact "
		    "configuration cannot be determined.\n"));
	}
}

/*
 * Perform the import for the given configuration.  This passes the heavy
 * lifting off to zpool_import_props(), and then mounts the datasets contained
 * within the pool.
 */
static int
do_import(nvlist_t *config, const char *newname, const char *mntopts,
    nvlist_t *props, int flags)
{
	zpool_handle_t *zhp;
	char *name;
	uint64_t state;
	uint64_t version;

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);

	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_POOL_STATE, &state) == 0);
	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_VERSION, &version) == 0);
	if (!SPA_VERSION_IS_SUPPORTED(version)) {
		(void) fprintf(stderr, gettext("cannot import '%s': pool "
		    "is formatted using an unsupported ZFS version\n"), name);
		return (1);
	} else if (state != POOL_STATE_EXPORTED &&
	    !(flags & ZFS_IMPORT_ANY_HOST)) {
		uint64_t hostid;

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID,
		    &hostid) == 0) {
			if ((unsigned long)hostid != gethostid()) {
				char *hostname;
				uint64_t timestamp;
				time_t t;

				verify(nvlist_lookup_string(config,
				    ZPOOL_CONFIG_HOSTNAME, &hostname) == 0);
				verify(nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_TIMESTAMP, &timestamp) == 0);
				t = timestamp;
				(void) fprintf(stderr, gettext("cannot import "
				    "'%s': pool may be in use from other "
				    "system, it was last accessed by %s "
				    "(hostid: 0x%lx) on %s"), name, hostname,
				    (unsigned long)hostid,
				    asctime(localtime(&t)));
				(void) fprintf(stderr, gettext("use '-f' to "
				    "import anyway\n"));
				return (1);
			}
		} else {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "pool may be in use from other system\n"), name);
			(void) fprintf(stderr, gettext("use '-f' to import "
			    "anyway\n"));
			return (1);
		}
	}

	if (zpool_import_props(g_zfs, config, newname, props, flags) != 0)
		return (1);

	if (newname != NULL)
		name = (char *)newname;

	if ((zhp = zpool_open_canfail(g_zfs, name)) == NULL)
		return (1);

	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    !(flags & ZFS_IMPORT_ONLY) &&
	    zpool_enable_datasets(zhp, mntopts, 0) != 0) {
		zpool_close(zhp);
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

/*
 * zpool checkpoint <pool>
 *       checkpoint --discard <pool>
 *
 *	-d	Discard the checkpoint from a checkpointed
 *	--discard  pool.
 *
 * Checkpoints the specified pool, by taking a "snapshot" of its
 * current state. A pool can only have one checkpoint at a time.
 */
int
zpool_do_checkpoint(int argc, char **argv)
{
	boolean_t discard;
	char *pool;
	zpool_handle_t *zhp;
	int c, err;

	struct option long_options[] = {
		{"discard", no_argument, NULL, 'd'},
		{0, 0, 0, 0}
	};

	discard = B_FALSE;
	while ((c = getopt_long(argc, argv, ":d", long_options, NULL)) != -1) {
		switch (c) {
		case 'd':
			discard = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool argument\n"));
		usage(B_FALSE);
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	pool = argv[0];

	if ((zhp = zpool_open(g_zfs, pool)) == NULL) {
		/* As a special case, check for use of '/' in the name */
		if (strchr(pool, '/') != NULL)
			(void) fprintf(stderr, gettext("'zpool checkpoint' "
			    "doesn't work on datasets. To save the state "
			    "of a dataset from a specific point in time "
			    "please use 'zfs snapshot'\n"));
		return (1);
	}

	if (discard)
		err = (zpool_discard_checkpoint(zhp) != 0);
	else
		err = (zpool_checkpoint(zhp) != 0);

	zpool_close(zhp);

	return (err);
}

#define	CHECKPOINT_OPT	1024

/*
 * zpool import [-d dir] [-D]
 *       import [-o mntopts] [-o prop=value] ... [-R root] [-D]
 *              [-d dir | -c cachefile] [-f] -a
 *       import [-o mntopts] [-o prop=value] ... [-R root] [-D]
 *              [-d dir | -c cachefile] [-f] [-n] [-F] [-t]
 *              <pool | id> [newpool]
 *
 *	-c	Read pool information from a cachefile instead of searching
 *		devices.
 *
 *	-d	Scan in a specific directory, other than /dev/dsk.  More than
 *		one directory can be specified using multiple '-d' options.
 *
 *	-D	Scan for previously destroyed pools or import all or only
 *		specified destroyed pools.
 *
 *	-R	Temporarily import the pool, with all mountpoints relative to
 *		the given root.  The pool will remain exported when the machine
 *		is rebooted.
 *
 *	-V	Import even in the presence of faulted vdevs.  This is an
 *		intentionally undocumented option for testing purposes, and
 *		treats the pool configuration as complete, leaving any bad
 *		vdevs in the FAULTED state. In other words, it does verbatim
 *		import.
 *
 *	-f	Force import, even if it appears that the pool is active.
 *
 *	-F	Attempt rewind if necessary.
 *
 *	-n	See if rewind would work, but don't actually rewind.
 *
 *	-N	Import the pool but don't mount datasets.
 *
 *	-t	Use newpool as a temporary pool name instead of renaming
 *		the pool.
 *
 *	-T	Specify a starting txg to use for import. This option is
 *		intentionally undocumented option for testing purposes.
 *
 *	-a	Import all pools found.
 *
 *	-o	Set property=value and/or temporary mount options (without '=').
 *
 *	--rewind-to-checkpoint
 *		Import the pool and revert back to the checkpoint.
 *
 * The import command scans for pools to import, and import pools based on pool
 * name and GUID.  The pool can also be renamed as part of the import process.
 */
int
zpool_do_import(int argc, char **argv)
{
	char **searchdirs = NULL;
	int nsearch = 0;
	int c;
	int err = 0;
	nvlist_t *pools = NULL;
	boolean_t do_all = B_FALSE;
	boolean_t do_destroyed = B_FALSE;
	char *mntopts = NULL;
	nvpair_t *elem;
	nvlist_t *config;
	uint64_t searchguid = 0;
	char *searchname = NULL;
	char *propval;
	nvlist_t *found_config;
	nvlist_t *policy = NULL;
	nvlist_t *props = NULL;
	boolean_t first;
	int flags = ZFS_IMPORT_NORMAL;
	uint32_t rewind_policy = ZPOOL_NO_REWIND;
	boolean_t dryrun = B_FALSE;
	boolean_t do_rewind = B_FALSE;
	boolean_t xtreme_rewind = B_FALSE;
	uint64_t pool_state, txg = -1ULL;
	char *cachefile = NULL;
	importargs_t idata = { 0 };
	char *endptr;


	struct option long_options[] = {
		{"rewind-to-checkpoint", no_argument, NULL, CHECKPOINT_OPT},
		{0, 0, 0, 0}
	};

	/* check options */
	while ((c = getopt_long(argc, argv, ":aCc:d:DEfFmnNo:rR:tT:VX",
	    long_options, NULL)) != -1) {
		switch (c) {
		case 'a':
			do_all = B_TRUE;
			break;
		case 'c':
			cachefile = optarg;
			break;
		case 'd':
			if (searchdirs == NULL) {
				searchdirs = safe_malloc(sizeof (char *));
			} else {
				char **tmp = safe_malloc((nsearch + 1) *
				    sizeof (char *));
				bcopy(searchdirs, tmp, nsearch *
				    sizeof (char *));
				free(searchdirs);
				searchdirs = tmp;
			}
			searchdirs[nsearch++] = optarg;
			break;
		case 'D':
			do_destroyed = B_TRUE;
			break;
		case 'f':
			flags |= ZFS_IMPORT_ANY_HOST;
			break;
		case 'F':
			do_rewind = B_TRUE;
			break;
		case 'm':
			flags |= ZFS_IMPORT_MISSING_LOG;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'N':
			flags |= ZFS_IMPORT_ONLY;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) != NULL) {
				*propval = '\0';
				propval++;
				if (add_prop_list(optarg, propval,
				    &props, B_TRUE))
					goto error;
			} else {
				mntopts = optarg;
			}
			break;
		case 'R':
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE))
				goto error;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto error;
			break;
		case 't':
			flags |= ZFS_IMPORT_TEMP_NAME;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto error;
			break;
		case 'T':
			errno = 0;
			txg = strtoull(optarg, &endptr, 0);
			if (errno != 0 || *endptr != '\0') {
				(void) fprintf(stderr,
				    gettext("invalid txg value\n"));
				usage(B_FALSE);
			}
			rewind_policy = ZPOOL_DO_REWIND | ZPOOL_EXTREME_REWIND;
			break;
		case 'V':
			flags |= ZFS_IMPORT_VERBATIM;
			break;
		case 'X':
			xtreme_rewind = B_TRUE;
			break;
		case CHECKPOINT_OPT:
			flags |= ZFS_IMPORT_CHECKPOINT;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (cachefile && nsearch != 0) {
		(void) fprintf(stderr, gettext("-c is incompatible with -d\n"));
		usage(B_FALSE);
	}

	if ((dryrun || xtreme_rewind) && !do_rewind) {
		(void) fprintf(stderr,
		    gettext("-n or -X only meaningful with -F\n"));
		usage(B_FALSE);
	}
	if (dryrun)
		rewind_policy = ZPOOL_TRY_REWIND;
	else if (do_rewind)
		rewind_policy = ZPOOL_DO_REWIND;
	if (xtreme_rewind)
		rewind_policy |= ZPOOL_EXTREME_REWIND;

	/* In the future, we can capture further policy and include it here */
	if (nvlist_alloc(&policy, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint64(policy, ZPOOL_LOAD_REQUEST_TXG, txg) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY,
	    rewind_policy) != 0)
		goto error;

	if (searchdirs == NULL) {
		searchdirs = safe_malloc(sizeof (char *));
		searchdirs[0] = "/dev";
		nsearch = 1;
	}

	/* check argument count */
	if (do_all) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}
	} else {
		if (argc > 2) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		/*
		 * Check for the SYS_CONFIG privilege.  We do this explicitly
		 * here because otherwise any attempt to discover pools will
		 * silently fail.
		 */
		if (argc == 0 && !priv_ineffect(PRIV_SYS_CONFIG)) {
			(void) fprintf(stderr, gettext("cannot "
			    "discover pools: permission denied\n"));
			free(searchdirs);
			nvlist_free(policy);
			return (1);
		}
	}

	/*
	 * Depending on the arguments given, we do one of the following:
	 *
	 *	<none>	Iterate through all pools and display information about
	 *		each one.
	 *
	 *	-a	Iterate through all pools and try to import each one.
	 *
	 *	<id>	Find the pool that corresponds to the given GUID/pool
	 *		name and import that one.
	 *
	 *	-D	Above options applies only to destroyed pools.
	 */
	if (argc != 0) {
		char *endptr;

		errno = 0;
		searchguid = strtoull(argv[0], &endptr, 10);
		if (errno != 0 || *endptr != '\0') {
			searchname = argv[0];
			searchguid = 0;
		}
		found_config = NULL;

		/*
		 * User specified a name or guid.  Ensure it's unique.
		 */
		idata.unique = B_TRUE;
	}


	idata.path = searchdirs;
	idata.paths = nsearch;
	idata.poolname = searchname;
	idata.guid = searchguid;
	idata.cachefile = cachefile;
	idata.policy = policy;

	pools = zpool_search_import(g_zfs, &idata);

	if (pools != NULL && idata.exists &&
	    (argc == 1 || strcmp(argv[0], argv[1]) == 0)) {
		(void) fprintf(stderr, gettext("cannot import '%s': "
		    "a pool with that name already exists\n"),
		    argv[0]);
		(void) fprintf(stderr, gettext("use the form 'zpool import "
		    "[-t] <pool | id> <newpool>' to give it a new temporary "
		    "or permanent name\n"));
		err = 1;
	} else if (pools == NULL && idata.exists) {
		(void) fprintf(stderr, gettext("cannot import '%s': "
		    "a pool with that name is already created/imported,\n"),
		    argv[0]);
		(void) fprintf(stderr, gettext("and no additional pools "
		    "with that name were found\n"));
		err = 1;
	} else if (pools == NULL) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "no such pool available\n"), argv[0]);
		}
		err = 1;
	}

	if (err == 1) {
		free(searchdirs);
		nvlist_free(policy);
		return (1);
	}

	/*
	 * At this point we have a list of import candidate configs. Even if
	 * we were searching by pool name or guid, we still need to
	 * post-process the list to deal with pool state and possible
	 * duplicate names.
	 */
	err = 0;
	elem = NULL;
	first = B_TRUE;
	while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {

		verify(nvpair_value_nvlist(elem, &config) == 0);

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    &pool_state) == 0);
		if (!do_destroyed && pool_state == POOL_STATE_DESTROYED)
			continue;
		if (do_destroyed && pool_state != POOL_STATE_DESTROYED)
			continue;

		verify(nvlist_add_nvlist(config, ZPOOL_LOAD_POLICY,
		    policy) == 0);

		if (argc == 0) {
			if (first)
				first = B_FALSE;
			else if (!do_all)
				(void) printf("\n");

			if (do_all) {
				err |= do_import(config, NULL, mntopts,
				    props, flags);
			} else {
				show_import(config);
			}
		} else if (searchname != NULL) {
			char *name;

			/*
			 * We are searching for a pool based on name.
			 */
			verify(nvlist_lookup_string(config,
			    ZPOOL_CONFIG_POOL_NAME, &name) == 0);

			if (strcmp(name, searchname) == 0) {
				if (found_config != NULL) {
					(void) fprintf(stderr, gettext(
					    "cannot import '%s': more than "
					    "one matching pool\n"), searchname);
					(void) fprintf(stderr, gettext(
					    "import by numeric ID instead\n"));
					err = B_TRUE;
				}
				found_config = config;
			}
		} else {
			uint64_t guid;

			/*
			 * Search for a pool by guid.
			 */
			verify(nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_POOL_GUID, &guid) == 0);

			if (guid == searchguid)
				found_config = config;
		}
	}

	/*
	 * If we were searching for a specific pool, verify that we found a
	 * pool, and then do the import.
	 */
	if (argc != 0 && err == 0) {
		if (found_config == NULL) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "no such pool available\n"), argv[0]);
			err = B_TRUE;
		} else {
			err |= do_import(found_config, argc == 1 ? NULL :
			    argv[1], mntopts, props, flags);
		}
	}

	/*
	 * If we were just looking for pools, report an error if none were
	 * found.
	 */
	if (argc == 0 && first)
		(void) fprintf(stderr,
		    gettext("no pools available to import\n"));

error:
	nvlist_free(props);
	nvlist_free(pools);
	nvlist_free(policy);
	free(searchdirs);

	return (err ? 1 : 0);
}

typedef struct iostat_cbdata {
	boolean_t cb_verbose;
	int cb_namewidth;
	int cb_iteration;
	zpool_list_t *cb_list;
} iostat_cbdata_t;

static void
print_iostat_separator(iostat_cbdata_t *cb)
{
	int i = 0;

	for (i = 0; i < cb->cb_namewidth; i++)
		(void) printf("-");
	(void) printf("  -----  -----  -----  -----  -----  -----\n");
}

static void
print_iostat_header(iostat_cbdata_t *cb)
{
	(void) printf("%*s     capacity     operations    bandwidth\n",
	    cb->cb_namewidth, "");
	(void) printf("%-*s  alloc   free   read  write   read  write\n",
	    cb->cb_namewidth, "pool");
	print_iostat_separator(cb);
}

/*
 * Display a single statistic.
 */
static void
print_one_stat(uint64_t value)
{
	char buf[64];

	zfs_nicenum(value, buf, sizeof (buf));
	(void) printf("  %5s", buf);
}

/*
 * Print out all the statistics for the given vdev.  This can either be the
 * toplevel configuration, or called recursively.  If 'name' is NULL, then this
 * is a verbose output, and we don't want to display the toplevel pool stats.
 */
void
print_vdev_stats(zpool_handle_t *zhp, const char *name, nvlist_t *oldnv,
    nvlist_t *newnv, iostat_cbdata_t *cb, int depth)
{
	nvlist_t **oldchild, **newchild;
	uint_t c, children;
	vdev_stat_t *oldvs, *newvs;
	vdev_stat_t zerovs = { 0 };
	uint64_t tdelta;
	double scale;
	char *vname;

	if (strcmp(name, VDEV_TYPE_INDIRECT) == 0)
		return;

	if (oldnv != NULL) {
		verify(nvlist_lookup_uint64_array(oldnv,
		    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&oldvs, &c) == 0);
	} else {
		oldvs = &zerovs;
	}

	verify(nvlist_lookup_uint64_array(newnv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&newvs, &c) == 0);

	if (strlen(name) + depth > cb->cb_namewidth)
		(void) printf("%*s%s", depth, "", name);
	else
		(void) printf("%*s%s%*s", depth, "", name,
		    (int)(cb->cb_namewidth - strlen(name) - depth), "");

	tdelta = newvs->vs_timestamp - oldvs->vs_timestamp;

	if (tdelta == 0)
		scale = 1.0;
	else
		scale = (double)NANOSEC / tdelta;

	/* only toplevel vdevs have capacity stats */
	if (newvs->vs_space == 0) {
		(void) printf("      -      -");
	} else {
		print_one_stat(newvs->vs_alloc);
		print_one_stat(newvs->vs_space - newvs->vs_alloc);
	}

	print_one_stat((uint64_t)(scale * (newvs->vs_ops[ZIO_TYPE_READ] -
	    oldvs->vs_ops[ZIO_TYPE_READ])));

	print_one_stat((uint64_t)(scale * (newvs->vs_ops[ZIO_TYPE_WRITE] -
	    oldvs->vs_ops[ZIO_TYPE_WRITE])));

	print_one_stat((uint64_t)(scale * (newvs->vs_bytes[ZIO_TYPE_READ] -
	    oldvs->vs_bytes[ZIO_TYPE_READ])));

	print_one_stat((uint64_t)(scale * (newvs->vs_bytes[ZIO_TYPE_WRITE] -
	    oldvs->vs_bytes[ZIO_TYPE_WRITE])));

	(void) printf("\n");

	if (!cb->cb_verbose)
		return;

	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_CHILDREN,
	    &newchild, &children) != 0)
		return;

	if (oldnv && nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_CHILDREN,
	    &oldchild, &c) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t ishole = B_FALSE, islog = B_FALSE;

		(void) nvlist_lookup_uint64(newchild[c], ZPOOL_CONFIG_IS_HOLE,
		    &ishole);

		(void) nvlist_lookup_uint64(newchild[c], ZPOOL_CONFIG_IS_LOG,
		    &islog);

		if (ishole || islog)
			continue;

		vname = zpool_vdev_name(g_zfs, zhp, newchild[c], B_FALSE);
		print_vdev_stats(zhp, vname, oldnv ? oldchild[c] : NULL,
		    newchild[c], cb, depth + 2);
		free(vname);
	}

	/*
	 * Log device section
	 */

	if (num_logs(newnv) > 0) {
		(void) printf("%-*s      -      -      -      -      -      "
		    "-\n", cb->cb_namewidth, "logs");

		for (c = 0; c < children; c++) {
			uint64_t islog = B_FALSE;
			(void) nvlist_lookup_uint64(newchild[c],
			    ZPOOL_CONFIG_IS_LOG, &islog);

			if (islog) {
				vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
				    B_FALSE);
				print_vdev_stats(zhp, vname, oldnv ?
				    oldchild[c] : NULL, newchild[c],
				    cb, depth + 2);
				free(vname);
			}
		}

	}

	/*
	 * Include level 2 ARC devices in iostat output
	 */
	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_L2CACHE,
	    &newchild, &children) != 0)
		return;

	if (oldnv && nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_L2CACHE,
	    &oldchild, &c) != 0)
		return;

	if (children > 0) {
		(void) printf("%-*s      -      -      -      -      -      "
		    "-\n", cb->cb_namewidth, "cache");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
			    B_FALSE);
			print_vdev_stats(zhp, vname, oldnv ? oldchild[c] : NULL,
			    newchild[c], cb, depth + 2);
			free(vname);
		}
	}
}

static int
refresh_iostat(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	boolean_t missing;

	/*
	 * If the pool has disappeared, remove it from the list and continue.
	 */
	if (zpool_refresh_stats(zhp, &missing) != 0)
		return (-1);

	if (missing)
		pool_list_remove(cb->cb_list, zhp);

	return (0);
}

/*
 * Callback to print out the iostats for the given pool.
 */
int
print_iostat(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	nvlist_t *oldconfig, *newconfig;
	nvlist_t *oldnvroot, *newnvroot;

	newconfig = zpool_get_config(zhp, &oldconfig);

	if (cb->cb_iteration == 1)
		oldconfig = NULL;

	verify(nvlist_lookup_nvlist(newconfig, ZPOOL_CONFIG_VDEV_TREE,
	    &newnvroot) == 0);

	if (oldconfig == NULL)
		oldnvroot = NULL;
	else
		verify(nvlist_lookup_nvlist(oldconfig, ZPOOL_CONFIG_VDEV_TREE,
		    &oldnvroot) == 0);

	/*
	 * Print out the statistics for the pool.
	 */
	print_vdev_stats(zhp, zpool_get_name(zhp), oldnvroot, newnvroot, cb, 0);

	if (cb->cb_verbose)
		print_iostat_separator(cb);

	return (0);
}

int
get_namewidth(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	nvlist_t *config, *nvroot;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if (!cb->cb_verbose)
			cb->cb_namewidth = strlen(zpool_get_name(zhp));
		else
			cb->cb_namewidth = max_width(zhp, nvroot, 0,
			    cb->cb_namewidth);
	}

	/*
	 * The width must fall into the range [10,38].  The upper limit is the
	 * maximum we can have and still fit in 80 columns.
	 */
	if (cb->cb_namewidth < 10)
		cb->cb_namewidth = 10;
	if (cb->cb_namewidth > 38)
		cb->cb_namewidth = 38;

	return (0);
}

/*
 * Parse the input string, get the 'interval' and 'count' value if there is one.
 */
static void
get_interval_count(int *argcp, char **argv, unsigned long *iv,
    unsigned long *cnt)
{
	unsigned long interval = 0, count = 0;
	int argc = *argcp, errno;

	/*
	 * Determine if the last argument is an integer or a pool name
	 */
	if (argc > 0 && isdigit(argv[argc - 1][0])) {
		char *end;

		errno = 0;
		interval = strtoul(argv[argc - 1], &end, 10);

		if (*end == '\0' && errno == 0) {
			if (interval == 0) {
				(void) fprintf(stderr, gettext("interval "
				    "cannot be zero\n"));
				usage(B_FALSE);
			}
			/*
			 * Ignore the last parameter
			 */
			argc--;
		} else {
			/*
			 * If this is not a valid number, just plow on.  The
			 * user will get a more informative error message later
			 * on.
			 */
			interval = 0;
		}
	}

	/*
	 * If the last argument is also an integer, then we have both a count
	 * and an interval.
	 */
	if (argc > 0 && isdigit(argv[argc - 1][0])) {
		char *end;

		errno = 0;
		count = interval;
		interval = strtoul(argv[argc - 1], &end, 10);

		if (*end == '\0' && errno == 0) {
			if (interval == 0) {
				(void) fprintf(stderr, gettext("interval "
				    "cannot be zero\n"));
				usage(B_FALSE);
			}

			/*
			 * Ignore the last parameter
			 */
			argc--;
		} else {
			interval = 0;
		}
	}

	*iv = interval;
	*cnt = count;
	*argcp = argc;
}

static void
get_timestamp_arg(char c)
{
	if (c == 'u')
		timestamp_fmt = UDATE;
	else if (c == 'd')
		timestamp_fmt = DDATE;
	else
		usage(B_FALSE);
}

/*
 * zpool iostat [-v] [-T d|u] [pool] ... [interval [count]]
 *
 *	-v	Display statistics for individual vdevs
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * This command can be tricky because we want to be able to deal with pool
 * creation/destruction as well as vdev configuration changes.  The bulk of this
 * processing is handled by the pool_list_* routines in zpool_iter.c.  We rely
 * on pool_list_update() to detect the addition of new pools.  Configuration
 * changes are all handled within libzfs.
 */
int
zpool_do_iostat(int argc, char **argv)
{
	int c;
	int ret;
	int npools;
	unsigned long interval = 0, count = 0;
	zpool_list_t *list;
	boolean_t verbose = B_FALSE;
	iostat_cbdata_t cb;

	/* check options */
	while ((c = getopt(argc, argv, "T:v")) != -1) {
		switch (c) {
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	/*
	 * Construct the list of all interesting pools.
	 */
	ret = 0;
	if ((list = pool_list_get(argc, argv, NULL, &ret)) == NULL)
		return (1);

	if (pool_list_count(list) == 0 && argc != 0) {
		pool_list_free(list);
		return (1);
	}

	if (pool_list_count(list) == 0 && interval == 0) {
		pool_list_free(list);
		(void) fprintf(stderr, gettext("no pools available\n"));
		return (1);
	}

	/*
	 * Enter the main iostat loop.
	 */
	cb.cb_list = list;
	cb.cb_verbose = verbose;
	cb.cb_iteration = 0;
	cb.cb_namewidth = 0;

	for (;;) {
		pool_list_update(list);

		if ((npools = pool_list_count(list)) == 0)
			break;

		/*
		 * Refresh all statistics.  This is done as an explicit step
		 * before calculating the maximum name width, so that any
		 * configuration changes are properly accounted for.
		 */
		(void) pool_list_iter(list, B_FALSE, refresh_iostat, &cb);

		/*
		 * Iterate over all pools to determine the maximum width
		 * for the pool / device name column across all pools.
		 */
		cb.cb_namewidth = 0;
		(void) pool_list_iter(list, B_FALSE, get_namewidth, &cb);

		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		/*
		 * If it's the first time, or verbose mode, print the header.
		 */
		if (++cb.cb_iteration == 1 || verbose)
			print_iostat_header(&cb);

		(void) pool_list_iter(list, B_FALSE, print_iostat, &cb);

		/*
		 * If there's more than one pool, and we're not in verbose mode
		 * (which prints a separator for us), then print a separator.
		 */
		if (npools > 1 && !verbose)
			print_iostat_separator(&cb);

		if (verbose)
			(void) printf("\n");

		/*
		 * Flush the output so that redirection to a file isn't buffered
		 * indefinitely.
		 */
		(void) fflush(stdout);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) sleep(interval);
	}

	pool_list_free(list);

	return (ret);
}

typedef struct list_cbdata {
	boolean_t	cb_verbose;
	int		cb_namewidth;
	boolean_t	cb_scripted;
	zprop_list_t	*cb_proplist;
	boolean_t	cb_literal;
} list_cbdata_t;

/*
 * Given a list of columns to display, output appropriate headers for each one.
 */
static void
print_header(list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	char headerbuf[ZPOOL_MAXPROPLEN];
	const char *header;
	boolean_t first = B_TRUE;
	boolean_t right_justify;
	size_t width = 0;

	for (; pl != NULL; pl = pl->pl_next) {
		width = pl->pl_width;
		if (first && cb->cb_verbose) {
			/*
			 * Reset the width to accommodate the verbose listing
			 * of devices.
			 */
			width = cb->cb_namewidth;
		}

		if (!first)
			(void) printf("  ");
		else
			first = B_FALSE;

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_INVAL) {
			header = zpool_prop_column_name(pl->pl_prop);
			right_justify = zpool_prop_align_right(pl->pl_prop);
		} else {
			int i;

			for (i = 0; pl->pl_user_prop[i] != '\0'; i++)
				headerbuf[i] = toupper(pl->pl_user_prop[i]);
			headerbuf[i] = '\0';
			header = headerbuf;
		}

		if (pl->pl_next == NULL && !right_justify)
			(void) printf("%s", header);
		else if (right_justify)
			(void) printf("%*s", width, header);
		else
			(void) printf("%-*s", width, header);

	}

	(void) printf("\n");
}

/*
 * Given a pool and a list of properties, print out all the properties according
 * to the described layout.
 */
static void
print_pool(zpool_handle_t *zhp, list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	boolean_t first = B_TRUE;
	char property[ZPOOL_MAXPROPLEN];
	char *propstr;
	boolean_t right_justify;
	size_t width;

	for (; pl != NULL; pl = pl->pl_next) {

		width = pl->pl_width;
		if (first && cb->cb_verbose) {
			/*
			 * Reset the width to accommodate the verbose listing
			 * of devices.
			 */
			width = cb->cb_namewidth;
		}

		if (!first) {
			if (cb->cb_scripted)
				(void) printf("\t");
			else
				(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_INVAL) {
			if (zpool_get_prop(zhp, pl->pl_prop, property,
			    sizeof (property), NULL, cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;

			right_justify = zpool_prop_align_right(pl->pl_prop);
		} else if ((zpool_prop_feature(pl->pl_user_prop) ||
		    zpool_prop_unsupported(pl->pl_user_prop)) &&
		    zpool_prop_get_feature(zhp, pl->pl_user_prop, property,
		    sizeof (property)) == 0) {
			propstr = property;
		} else {
			propstr = "-";
		}


		/*
		 * If this is being called in scripted mode, or if this is the
		 * last column and it is left-justified, don't include a width
		 * format specifier.
		 */
		if (cb->cb_scripted || (pl->pl_next == NULL && !right_justify))
			(void) printf("%s", propstr);
		else if (right_justify)
			(void) printf("%*s", width, propstr);
		else
			(void) printf("%-*s", width, propstr);
	}

	(void) printf("\n");
}

static void
print_one_column(zpool_prop_t prop, uint64_t value, boolean_t scripted,
    boolean_t valid)
{
	char propval[64];
	boolean_t fixed;
	size_t width = zprop_width(prop, &fixed, ZFS_TYPE_POOL);

	switch (prop) {
	case ZPOOL_PROP_EXPANDSZ:
	case ZPOOL_PROP_CHECKPOINT:
		if (value == 0)
			(void) strlcpy(propval, "-", sizeof (propval));
		else
			zfs_nicenum(value, propval, sizeof (propval));
		break;
	case ZPOOL_PROP_FRAGMENTATION:
		if (value == ZFS_FRAG_INVALID) {
			(void) strlcpy(propval, "-", sizeof (propval));
		} else {
			(void) snprintf(propval, sizeof (propval), "%llu%%",
			    value);
		}
		break;
	case ZPOOL_PROP_CAPACITY:
		(void) snprintf(propval, sizeof (propval), "%llu%%", value);
		break;
	default:
		zfs_nicenum(value, propval, sizeof (propval));
	}

	if (!valid)
		(void) strlcpy(propval, "-", sizeof (propval));

	if (scripted)
		(void) printf("\t%s", propval);
	else
		(void) printf("  %*s", width, propval);
}

void
print_list_stats(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    list_cbdata_t *cb, int depth)
{
	nvlist_t **child;
	vdev_stat_t *vs;
	uint_t c, children;
	char *vname;
	boolean_t scripted = cb->cb_scripted;
	uint64_t islog = B_FALSE;
	boolean_t haslog = B_FALSE;
	char *dashes = "%-*s      -      -      -         -      -      -\n";

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	if (name != NULL) {
		boolean_t toplevel = (vs->vs_space != 0);
		uint64_t cap;

		if (strcmp(name, VDEV_TYPE_INDIRECT) == 0)
			return;

		if (scripted)
			(void) printf("\t%s", name);
		else if (strlen(name) + depth > cb->cb_namewidth)
			(void) printf("%*s%s", depth, "", name);
		else
			(void) printf("%*s%s%*s", depth, "", name,
			    (int)(cb->cb_namewidth - strlen(name) - depth), "");

		/*
		 * Print the properties for the individual vdevs. Some
		 * properties are only applicable to toplevel vdevs. The
		 * 'toplevel' boolean value is passed to the print_one_column()
		 * to indicate that the value is valid.
		 */
		print_one_column(ZPOOL_PROP_SIZE, vs->vs_space, scripted,
		    toplevel);
		print_one_column(ZPOOL_PROP_ALLOCATED, vs->vs_alloc, scripted,
		    toplevel);
		print_one_column(ZPOOL_PROP_FREE, vs->vs_space - vs->vs_alloc,
		    scripted, toplevel);
		print_one_column(ZPOOL_PROP_CHECKPOINT,
		    vs->vs_checkpoint_space, scripted, toplevel);
		print_one_column(ZPOOL_PROP_EXPANDSZ, vs->vs_esize, scripted,
		    B_TRUE);
		print_one_column(ZPOOL_PROP_FRAGMENTATION,
		    vs->vs_fragmentation, scripted,
		    (vs->vs_fragmentation != ZFS_FRAG_INVALID && toplevel));
		cap = (vs->vs_space == 0) ? 0 :
		    (vs->vs_alloc * 100 / vs->vs_space);
		print_one_column(ZPOOL_PROP_CAPACITY, cap, scripted, toplevel);
		(void) printf("\n");
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t ishole = B_FALSE;

		if (nvlist_lookup_uint64(child[c],
		    ZPOOL_CONFIG_IS_HOLE, &ishole) == 0 && ishole)
			continue;

		if (nvlist_lookup_uint64(child[c],
		    ZPOOL_CONFIG_IS_LOG, &islog) == 0 && islog) {
			haslog = B_TRUE;
			continue;
		}

		vname = zpool_vdev_name(g_zfs, zhp, child[c], B_FALSE);
		print_list_stats(zhp, vname, child[c], cb, depth + 2);
		free(vname);
	}

	if (haslog == B_TRUE) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "log");
		for (c = 0; c < children; c++) {
			if (nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
			    &islog) != 0 || !islog)
				continue;
			vname = zpool_vdev_name(g_zfs, zhp, child[c], B_FALSE);
			print_list_stats(zhp, vname, child[c], cb, depth + 2);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "cache");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, child[c], B_FALSE);
			print_list_stats(zhp, vname, child[c], cb, depth + 2);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES, &child,
	    &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "spare");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, child[c], B_FALSE);
			print_list_stats(zhp, vname, child[c], cb, depth + 2);
			free(vname);
		}
	}
}


/*
 * Generic callback function to list a pool.
 */
int
list_callback(zpool_handle_t *zhp, void *data)
{
	list_cbdata_t *cbp = data;
	nvlist_t *config;
	nvlist_t *nvroot;

	config = zpool_get_config(zhp, NULL);

	print_pool(zhp, cbp);
	if (!cbp->cb_verbose)
		return (0);

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	print_list_stats(zhp, NULL, nvroot, cbp, 0);

	return (0);
}

/*
 * zpool list [-Hp] [-o prop[,prop]*] [-T d|u] [pool] ... [interval [count]]
 *
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-o	List of properties to display.  Defaults to
 *		"name,size,allocated,free,expandsize,fragmentation,capacity,"
 *		"dedupratio,health,altroot"
 *	-p	Diplay values in parsable (exact) format.
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * List all pools in the system, whether or not they're healthy.  Output space
 * statistics for each one, as well as health status summary.
 */
int
zpool_do_list(int argc, char **argv)
{
	int c;
	int ret;
	list_cbdata_t cb = { 0 };
	static char default_props[] =
	    "name,size,allocated,free,checkpoint,expandsize,fragmentation,"
	    "capacity,dedupratio,health,altroot";
	char *props = default_props;
	unsigned long interval = 0, count = 0;
	zpool_list_t *list;
	boolean_t first = B_TRUE;

	/* check options */
	while ((c = getopt(argc, argv, ":Ho:pT:v")) != -1) {
		switch (c) {
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 'o':
			props = optarg;
			break;
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			cb.cb_verbose = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (zprop_get_list(g_zfs, props, &cb.cb_proplist, ZFS_TYPE_POOL) != 0)
		usage(B_FALSE);

	for (;;) {
		if ((list = pool_list_get(argc, argv, &cb.cb_proplist,
		    &ret)) == NULL)
			return (1);

		if (pool_list_count(list) == 0)
			break;

		cb.cb_namewidth = 0;
		(void) pool_list_iter(list, B_FALSE, get_namewidth, &cb);

		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		if (!cb.cb_scripted && (first || cb.cb_verbose)) {
			print_header(&cb);
			first = B_FALSE;
		}
		ret = pool_list_iter(list, B_TRUE, list_callback, &cb);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		pool_list_free(list);
		(void) sleep(interval);
	}

	if (argc == 0 && !cb.cb_scripted && pool_list_count(list) == 0) {
		(void) printf(gettext("no pools available\n"));
		ret = 0;
	}

	pool_list_free(list);
	zprop_free_list(cb.cb_proplist);
	return (ret);
}

static int
zpool_do_attach_or_replace(int argc, char **argv, int replacing)
{
	boolean_t force = B_FALSE;
	int c;
	nvlist_t *nvroot;
	char *poolname, *old_disk, *new_disk;
	zpool_handle_t *zhp;
	zpool_boot_label_t boot_type;
	uint64_t boot_size;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("missing <device> specification\n"));
		usage(B_FALSE);
	}

	old_disk = argv[1];

	if (argc < 3) {
		if (!replacing) {
			(void) fprintf(stderr,
			    gettext("missing <new_device> specification\n"));
			usage(B_FALSE);
		}
		new_disk = old_disk;
		argc -= 1;
		argv += 1;
	} else {
		new_disk = argv[2];
		argc -= 2;
		argv += 2;
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	if (zpool_get_config(zhp, NULL) == NULL) {
		(void) fprintf(stderr, gettext("pool '%s' is unavailable\n"),
		    poolname);
		zpool_close(zhp);
		return (1);
	}

	if (zpool_is_bootable(zhp))
		boot_type = ZPOOL_COPY_BOOT_LABEL;
	else
		boot_type = ZPOOL_NO_BOOT_LABEL;

	boot_size = zpool_get_prop_int(zhp, ZPOOL_PROP_BOOTSIZE, NULL);
	nvroot = make_root_vdev(zhp, force, B_FALSE, replacing, B_FALSE,
	    boot_type, boot_size, argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		return (1);
	}

	ret = zpool_vdev_attach(zhp, old_disk, new_disk, nvroot, replacing);

	nvlist_free(nvroot);
	zpool_close(zhp);

	return (ret);
}

/*
 * zpool replace [-f] <pool> <device> <new_device>
 *
 *	-f	Force attach, even if <new_device> appears to be in use.
 *
 * Replace <device> with <new_device>.
 */
/* ARGSUSED */
int
zpool_do_replace(int argc, char **argv)
{
	return (zpool_do_attach_or_replace(argc, argv, B_TRUE));
}

/*
 * zpool attach [-f] <pool> <device> <new_device>
 *
 *	-f	Force attach, even if <new_device> appears to be in use.
 *
 * Attach <new_device> to the mirror containing <device>.  If <device> is not
 * part of a mirror, then <device> will be transformed into a mirror of
 * <device> and <new_device>.  In either case, <new_device> will begin life
 * with a DTL of [0, now], and will immediately begin to resilver itself.
 */
int
zpool_do_attach(int argc, char **argv)
{
	return (zpool_do_attach_or_replace(argc, argv, B_FALSE));
}

/*
 * zpool detach [-f] <pool> <device>
 *
 *	-f	Force detach of <device>, even if DTLs argue against it
 *		(not supported yet)
 *
 * Detach a device from a mirror.  The operation will be refused if <device>
 * is the last device in the mirror, or if the DTLs indicate that this device
 * has the only valid copy of some data.
 */
/* ARGSUSED */
int
zpool_do_detach(int argc, char **argv)
{
	int c;
	char *poolname, *path;
	zpool_handle_t *zhp;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("missing <device> specification\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];
	path = argv[1];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	ret = zpool_vdev_detach(zhp, path);

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool split [-n] [-o prop=val] ...
 *		[-o mntopt] ...
 *		[-R altroot] <pool> <newpool> [<device> ...]
 *
 *	-n	Do not split the pool, but display the resulting layout if
 *		it were to be split.
 *	-o	Set property=value, or set mount options.
 *	-R	Mount the split-off pool under an alternate root.
 *
 * Splits the named pool and gives it the new pool name.  Devices to be split
 * off may be listed, provided that no more than one device is specified
 * per top-level vdev mirror.  The newly split pool is left in an exported
 * state unless -R is specified.
 *
 * Restrictions: the top-level of the pool pool must only be made up of
 * mirrors; all devices in the pool must be healthy; no device may be
 * undergoing a resilvering operation.
 */
int
zpool_do_split(int argc, char **argv)
{
	char *srcpool, *newpool, *propval;
	char *mntopts = NULL;
	splitflags_t flags;
	int c, ret = 0;
	zpool_handle_t *zhp;
	nvlist_t *config, *props = NULL;

	flags.dryrun = B_FALSE;
	flags.import = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, ":R:no:")) != -1) {
		switch (c) {
		case 'R':
			flags.import = B_TRUE;
			if (add_prop_list(
			    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), optarg,
			    &props, B_TRUE) != 0) {
				nvlist_free(props);
				usage(B_FALSE);
			}
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) != NULL) {
				*propval = '\0';
				propval++;
				if (add_prop_list(optarg, propval,
				    &props, B_TRUE) != 0) {
					nvlist_free(props);
					usage(B_FALSE);
				}
			} else {
				mntopts = optarg;
			}
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
			break;
		}
	}

	if (!flags.import && mntopts != NULL) {
		(void) fprintf(stderr, gettext("setting mntopts is only "
		    "valid when importing the pool\n"));
		usage(B_FALSE);
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("Missing pool name\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("Missing new pool name\n"));
		usage(B_FALSE);
	}

	srcpool = argv[0];
	newpool = argv[1];

	argc -= 2;
	argv += 2;

	if ((zhp = zpool_open(g_zfs, srcpool)) == NULL)
		return (1);

	config = split_mirror_vdev(zhp, newpool, props, flags, argc, argv);
	if (config == NULL) {
		ret = 1;
	} else {
		if (flags.dryrun) {
			(void) printf(gettext("would create '%s' with the "
			    "following layout:\n\n"), newpool);
			print_vdev_tree(NULL, newpool, config, 0, B_FALSE);
		}
		nvlist_free(config);
	}

	zpool_close(zhp);

	if (ret != 0 || flags.dryrun || !flags.import)
		return (ret);

	/*
	 * The split was successful. Now we need to open the new
	 * pool and import it.
	 */
	if ((zhp = zpool_open_canfail(g_zfs, newpool)) == NULL)
		return (1);
	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    zpool_enable_datasets(zhp, mntopts, 0) != 0) {
		ret = 1;
		(void) fprintf(stderr, gettext("Split was successful, but "
		    "the datasets could not all be mounted\n"));
		(void) fprintf(stderr, gettext("Try doing '%s' with a "
		    "different altroot\n"), "zpool import");
	}
	zpool_close(zhp);

	return (ret);
}



/*
 * zpool online <pool> <device> ...
 */
int
zpool_do_online(int argc, char **argv)
{
	int c, i;
	char *poolname;
	zpool_handle_t *zhp;
	int ret = 0;
	vdev_state_t newstate;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, "et")) != -1) {
		switch (c) {
		case 'e':
			flags |= ZFS_ONLINE_EXPAND;
			break;
		case 't':
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing device name\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	for (i = 1; i < argc; i++) {
		if (zpool_vdev_online(zhp, argv[i], flags, &newstate) == 0) {
			if (newstate != VDEV_STATE_HEALTHY) {
				(void) printf(gettext("warning: device '%s' "
				    "onlined, but remains in faulted state\n"),
				    argv[i]);
				if (newstate == VDEV_STATE_FAULTED)
					(void) printf(gettext("use 'zpool "
					    "clear' to restore a faulted "
					    "device\n"));
				else
					(void) printf(gettext("use 'zpool "
					    "replace' to replace devices "
					    "that are no longer present\n"));
			}
		} else {
			ret = 1;
		}
	}

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool offline [-ft] <pool> <device> ...
 *
 *	-f	Force the device into the offline state, even if doing
 *		so would appear to compromise pool availability.
 *		(not supported yet)
 *
 *	-t	Only take the device off-line temporarily.  The offline
 *		state will not be persistent across reboots.
 */
/* ARGSUSED */
int
zpool_do_offline(int argc, char **argv)
{
	int c, i;
	char *poolname;
	zpool_handle_t *zhp;
	int ret = 0;
	boolean_t istmp = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "ft")) != -1) {
		switch (c) {
		case 't':
			istmp = B_TRUE;
			break;
		case 'f':
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing device name\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	for (i = 1; i < argc; i++) {
		if (zpool_vdev_offline(zhp, argv[i], istmp) != 0)
			ret = 1;
	}

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool clear <pool> [device]
 *
 * Clear all errors associated with a pool or a particular device.
 */
int
zpool_do_clear(int argc, char **argv)
{
	int c;
	int ret = 0;
	boolean_t dryrun = B_FALSE;
	boolean_t do_rewind = B_FALSE;
	boolean_t xtreme_rewind = B_FALSE;
	uint32_t rewind_policy = ZPOOL_NO_REWIND;
	nvlist_t *policy = NULL;
	zpool_handle_t *zhp;
	char *pool, *device;

	/* check options */
	while ((c = getopt(argc, argv, "FnX")) != -1) {
		switch (c) {
		case 'F':
			do_rewind = B_TRUE;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'X':
			xtreme_rewind = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}

	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	if ((dryrun || xtreme_rewind) && !do_rewind) {
		(void) fprintf(stderr,
		    gettext("-n or -X only meaningful with -F\n"));
		usage(B_FALSE);
	}
	if (dryrun)
		rewind_policy = ZPOOL_TRY_REWIND;
	else if (do_rewind)
		rewind_policy = ZPOOL_DO_REWIND;
	if (xtreme_rewind)
		rewind_policy |= ZPOOL_EXTREME_REWIND;

	/* In future, further rewind policy choices can be passed along here */
	if (nvlist_alloc(&policy, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY,
	    rewind_policy) != 0) {
		return (1);
	}

	pool = argv[0];
	device = argc == 2 ? argv[1] : NULL;

	if ((zhp = zpool_open_canfail(g_zfs, pool)) == NULL) {
		nvlist_free(policy);
		return (1);
	}

	if (zpool_clear(zhp, device, policy) != 0)
		ret = 1;

	zpool_close(zhp);

	nvlist_free(policy);

	return (ret);
}

/*
 * zpool reguid <pool>
 */
int
zpool_do_reguid(int argc, char **argv)
{
	int c;
	char *poolname;
	zpool_handle_t *zhp;
	int ret = 0;

	/* check options */
	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];
	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	ret = zpool_reguid(zhp);

	zpool_close(zhp);
	return (ret);
}


/*
 * zpool reopen <pool>
 *
 * Reopen the pool so that the kernel can update the sizes of all vdevs.
 */
int
zpool_do_reopen(int argc, char **argv)
{
	int c;
	int ret = 0;
	zpool_handle_t *zhp;
	char *pool;

	/* check options */
	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc--;
	argv++;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	pool = argv[0];
	if ((zhp = zpool_open_canfail(g_zfs, pool)) == NULL)
		return (1);

	ret = zpool_reopen(zhp);
	zpool_close(zhp);
	return (ret);
}

typedef struct scrub_cbdata {
	int	cb_type;
	int	cb_argc;
	char	**cb_argv;
	pool_scrub_cmd_t cb_scrub_cmd;
} scrub_cbdata_t;

static boolean_t
zpool_has_checkpoint(zpool_handle_t *zhp)
{
	nvlist_t *config, *nvroot;

	config = zpool_get_config(zhp, NULL);

	if (config != NULL) {
		pool_checkpoint_stat_t *pcs = NULL;
		uint_t c;

		nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_CHECKPOINT_STATS, (uint64_t **)&pcs, &c);

		if (pcs == NULL || pcs->pcs_state == CS_NONE)
			return (B_FALSE);

		assert(pcs->pcs_state == CS_CHECKPOINT_EXISTS ||
		    pcs->pcs_state == CS_CHECKPOINT_DISCARDING);
		return (B_TRUE);
	}

	return (B_FALSE);
}

int
scrub_callback(zpool_handle_t *zhp, void *data)
{
	scrub_cbdata_t *cb = data;
	int err;

	/*
	 * Ignore faulted pools.
	 */
	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		(void) fprintf(stderr, gettext("cannot scrub '%s': pool is "
		    "currently unavailable\n"), zpool_get_name(zhp));
		return (1);
	}

	err = zpool_scan(zhp, cb->cb_type, cb->cb_scrub_cmd);

	if (err == 0 && zpool_has_checkpoint(zhp) &&
	    cb->cb_type == POOL_SCAN_SCRUB) {
		(void) printf(gettext("warning: will not scrub state that "
		    "belongs to the checkpoint of pool '%s'\n"),
		    zpool_get_name(zhp));
	}

	return (err != 0);
}

/*
 * zpool scrub [-s | -p] <pool> ...
 *
 *	-s	Stop.  Stops any in-progress scrub.
 *	-p	Pause. Pause in-progress scrub.
 */
int
zpool_do_scrub(int argc, char **argv)
{
	int c;
	scrub_cbdata_t cb;

	cb.cb_type = POOL_SCAN_SCRUB;
	cb.cb_scrub_cmd = POOL_SCRUB_NORMAL;

	/* check options */
	while ((c = getopt(argc, argv, "sp")) != -1) {
		switch (c) {
		case 's':
			cb.cb_type = POOL_SCAN_NONE;
			break;
		case 'p':
			cb.cb_scrub_cmd = POOL_SCRUB_PAUSE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	if (cb.cb_type == POOL_SCAN_NONE &&
	    cb.cb_scrub_cmd == POOL_SCRUB_PAUSE) {
		(void) fprintf(stderr, gettext("invalid option combination: "
		    "-s and -p are mutually exclusive\n"));
		usage(B_FALSE);
	}

	cb.cb_argc = argc;
	cb.cb_argv = argv;
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}

	return (for_each_pool(argc, argv, B_TRUE, NULL, scrub_callback, &cb));
}

static void
zpool_collect_leaves(zpool_handle_t *zhp, nvlist_t *nvroot, nvlist_t *res)
{
	uint_t children = 0;
	nvlist_t **child;
	uint_t i;

	(void) nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children);

	if (children == 0) {
		char *path = zpool_vdev_name(g_zfs, zhp, nvroot, B_FALSE);
		fnvlist_add_boolean(res, path);
		free(path);
		return;
	}

	for (i = 0; i < children; i++) {
		zpool_collect_leaves(zhp, child[i], res);
	}
}

/*
 * zpool initialize [-cs] <pool> [<vdev> ...]
 * Initialize all unused blocks in the specified vdevs, or all vdevs in the pool
 * if none specified.
 *
 *	-c	Cancel. Ends active initializing.
 *	-s	Suspend. Initializing can then be restarted with no flags.
 */
int
zpool_do_initialize(int argc, char **argv)
{
	int c;
	char *poolname;
	zpool_handle_t *zhp;
	nvlist_t *vdevs;
	int err = 0;

	struct option long_options[] = {
		{"cancel",	no_argument,		NULL, 'c'},
		{"suspend",	no_argument,		NULL, 's'},
		{0, 0, 0, 0}
	};

	pool_initialize_func_t cmd_type = POOL_INITIALIZE_DO;
	while ((c = getopt_long(argc, argv, "cs", long_options, NULL)) != -1) {
		switch (c) {
		case 'c':
			if (cmd_type != POOL_INITIALIZE_DO) {
				(void) fprintf(stderr, gettext("-c cannot be "
				    "combined with other options\n"));
				usage(B_FALSE);
			}
			cmd_type = POOL_INITIALIZE_CANCEL;
			break;
		case 's':
			if (cmd_type != POOL_INITIALIZE_DO) {
				(void) fprintf(stderr, gettext("-s cannot be "
				    "combined with other options\n"));
				usage(B_FALSE);
			}
			cmd_type = POOL_INITIALIZE_SUSPEND;
			break;
		case '?':
			if (optopt != 0) {
				(void) fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			} else {
				(void) fprintf(stderr,
				    gettext("invalid option '%s'\n"),
				    argv[optind - 1]);
			}
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
		return (-1);
	}

	poolname = argv[0];
	zhp = zpool_open(g_zfs, poolname);
	if (zhp == NULL)
		return (-1);

	vdevs = fnvlist_alloc();
	if (argc == 1) {
		/* no individual leaf vdevs specified, so add them all */
		nvlist_t *config = zpool_get_config(zhp, NULL);
		nvlist_t *nvroot = fnvlist_lookup_nvlist(config,
		    ZPOOL_CONFIG_VDEV_TREE);
		zpool_collect_leaves(zhp, nvroot, vdevs);
	} else {
		int i;
		for (i = 1; i < argc; i++) {
			fnvlist_add_boolean(vdevs, argv[i]);
		}
	}

	err = zpool_initialize(zhp, cmd_type, vdevs);

	fnvlist_free(vdevs);
	zpool_close(zhp);

	return (err);
}

typedef struct status_cbdata {
	int		cb_count;
	boolean_t	cb_allpools;
	boolean_t	cb_verbose;
	boolean_t	cb_explain;
	boolean_t	cb_first;
	boolean_t	cb_dedup_stats;
} status_cbdata_t;

/*
 * Print out detailed scrub status.
 */
static void
print_scan_status(pool_scan_stat_t *ps)
{
	time_t start, end, pause;
	uint64_t total_secs_left;
	uint64_t elapsed, secs_left, mins_left, hours_left, days_left;
	uint64_t pass_scanned, scanned, pass_issued, issued, total;
	uint_t scan_rate, issue_rate;
	double fraction_done;
	char processed_buf[7], scanned_buf[7], issued_buf[7], total_buf[7];
	char srate_buf[7], irate_buf[7];

	(void) printf(gettext("  scan: "));

	/* If there's never been a scan, there's not much to say. */
	if (ps == NULL || ps->pss_func == POOL_SCAN_NONE ||
	    ps->pss_func >= POOL_SCAN_FUNCS) {
		(void) printf(gettext("none requested\n"));
		return;
	}

	start = ps->pss_start_time;
	end = ps->pss_end_time;
	pause = ps->pss_pass_scrub_pause;

	zfs_nicenum(ps->pss_processed, processed_buf, sizeof (processed_buf));

	assert(ps->pss_func == POOL_SCAN_SCRUB ||
	    ps->pss_func == POOL_SCAN_RESILVER);

	/* Scan is finished or canceled. */
	if (ps->pss_state == DSS_FINISHED) {
		total_secs_left = end - start;
		days_left = total_secs_left / 60 / 60 / 24;
		hours_left = (total_secs_left / 60 / 60) % 24;
		mins_left = (total_secs_left / 60) % 60;
		secs_left = (total_secs_left % 60);
		
		if (ps->pss_func == POOL_SCAN_SCRUB) {
			(void) printf(gettext("scrub repaired %s "
                            "in %llu days %02llu:%02llu:%02llu "
			    "with %llu errors on %s"), processed_buf,
                            (u_longlong_t)days_left, (u_longlong_t)hours_left,
                            (u_longlong_t)mins_left, (u_longlong_t)secs_left,
                            (u_longlong_t)ps->pss_errors, ctime(&end));
		} else if (ps->pss_func == POOL_SCAN_RESILVER) {
                       (void) printf(gettext("resilvered %s "
                           "in %llu days %02llu:%02llu:%02llu "
                           "with %llu errors on %s"), processed_buf,
                           (u_longlong_t)days_left, (u_longlong_t)hours_left,
                           (u_longlong_t)mins_left, (u_longlong_t)secs_left,
                           (u_longlong_t)ps->pss_errors, ctime(&end));

		}

		return;
	} else if (ps->pss_state == DSS_CANCELED) {
		if (ps->pss_func == POOL_SCAN_SCRUB) {
			(void) printf(gettext("scrub canceled on %s"),
			    ctime(&end));
		} else if (ps->pss_func == POOL_SCAN_RESILVER) {
			(void) printf(gettext("resilver canceled on %s"),
			    ctime(&end));
		}
		return;
	}

	assert(ps->pss_state == DSS_SCANNING);

	/* Scan is in progress. Resilvers can't be paused. */
	if (ps->pss_func == POOL_SCAN_SCRUB) {
		if (pause == 0) {
			(void) printf(gettext("scrub in progress since %s"),
			    ctime(&start));
		} else {
			(void) printf(gettext("scrub paused since %s"),
			    ctime(&pause));
			(void) printf(gettext("\tscrub started on %s"),
			    ctime(&start));
		}
	} else if (ps->pss_func == POOL_SCAN_RESILVER) {
		(void) printf(gettext("resilver in progress since %s"),
		    ctime(&start));
	}

	scanned = ps->pss_examined;
	pass_scanned = ps->pss_pass_exam;
	issued = ps->pss_issued;
	pass_issued = ps->pss_pass_issued;
	total = ps->pss_to_examine;

	/* we are only done with a block once we have issued the IO for it */
	fraction_done = (double)issued / total;

	/* elapsed time for this pass, rounding up to 1 if it's 0 */
	elapsed = time(NULL) - ps->pss_pass_start;
	elapsed -= ps->pss_pass_scrub_spent_paused;
	elapsed = (elapsed != 0) ? elapsed : 1;

	scan_rate = pass_scanned / elapsed;
	issue_rate = pass_issued / elapsed;
	total_secs_left = (issue_rate != 0) ?
	    ((total - issued) / issue_rate) : UINT64_MAX;

	days_left = total_secs_left / 60 / 60 / 24;
	hours_left = (total_secs_left / 60 / 60) % 24;
	mins_left = (total_secs_left / 60) % 60;
	secs_left = (total_secs_left % 60);

	/* format all of the numbers we will be reporting */
	zfs_nicenum(scanned, scanned_buf, sizeof (scanned_buf));
	zfs_nicenum(issued, issued_buf, sizeof (issued_buf));
	zfs_nicenum(total, total_buf, sizeof (total_buf));
	zfs_nicenum(scan_rate, srate_buf, sizeof (srate_buf));
	zfs_nicenum(issue_rate, irate_buf, sizeof (irate_buf));

	/* doo not print estimated time if we have a paused scrub */
	if (pause == 0) {
		(void) printf(gettext("\t%s scanned at %s/s, "
		    "%s issued at %s/s, %s total\n"),
		    scanned_buf, srate_buf, issued_buf, irate_buf, total_buf);
	} else {
		(void) printf(gettext("\t%s scanned, %s issued, %s total\n"),
                    scanned_buf, issued_buf, total_buf);
	}

	if (ps->pss_func == POOL_SCAN_RESILVER) {
		(void) printf(gettext("\t%s resilvered, %.2f%% done"),
		    processed_buf, 100 * fraction_done);
	} else if (ps->pss_func == POOL_SCAN_SCRUB) {
		(void) printf(gettext("\t%s repaired, %.2f%% done"),
		    processed_buf, 100 * fraction_done);
	}

	if (pause == 0) {
		if (issue_rate >= 10 * 1024 * 1024) {
			(void) printf(gettext(", %llu days "
                            "%02llu:%02llu:%02llu to go\n"),
                            (u_longlong_t)days_left, (u_longlong_t)hours_left,
                            (u_longlong_t)mins_left, (u_longlong_t)secs_left);
		} else {
			(void) printf(gettext(", no estimated "
                            "completion time\n"));
		}
	} else {
		(void) printf(gettext("\n"));
	}
}

/*
 * As we don't scrub checkpointed blocks, we want to warn the
 * user that we skipped scanning some blocks if a checkpoint exists
 * or existed at any time during the scan.
 */
static void
print_checkpoint_scan_warning(pool_scan_stat_t *ps, pool_checkpoint_stat_t *pcs)
{
	if (ps == NULL || pcs == NULL)
		return;

	if (pcs->pcs_state == CS_NONE ||
	    pcs->pcs_state == CS_CHECKPOINT_DISCARDING)
		return;

	assert(pcs->pcs_state == CS_CHECKPOINT_EXISTS);

	if (ps->pss_state == DSS_NONE)
		return;

	if ((ps->pss_state == DSS_FINISHED || ps->pss_state == DSS_CANCELED) &&
	    ps->pss_end_time < pcs->pcs_start_time)
		return;

	if (ps->pss_state == DSS_FINISHED || ps->pss_state == DSS_CANCELED) {
		(void) printf(gettext("    scan warning: skipped blocks "
		    "that are only referenced by the checkpoint.\n"));
	} else {
		assert(ps->pss_state == DSS_SCANNING);
		(void) printf(gettext("    scan warning: skipping blocks "
		    "that are only referenced by the checkpoint.\n"));
	}
}

/*
 * Print out detailed removal status.
 */
static void
print_removal_status(zpool_handle_t *zhp, pool_removal_stat_t *prs)
{
	char copied_buf[7], examined_buf[7], total_buf[7], rate_buf[7];
	time_t start, end;
	nvlist_t *config, *nvroot;
	nvlist_t **child;
	uint_t children;
	char *vdev_name;

	if (prs == NULL || prs->prs_state == DSS_NONE)
		return;

	/*
	 * Determine name of vdev.
	 */
	config = zpool_get_config(zhp, NULL);
	nvroot = fnvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE);
	verify(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);
	assert(prs->prs_removing_vdev < children);
	vdev_name = zpool_vdev_name(g_zfs, zhp,
	    child[prs->prs_removing_vdev], B_TRUE);

	(void) printf(gettext("remove: "));

	start = prs->prs_start_time;
	end = prs->prs_end_time;
	zfs_nicenum(prs->prs_copied, copied_buf, sizeof (copied_buf));

	/*
	 * Removal is finished or canceled.
	 */
	if (prs->prs_state == DSS_FINISHED) {
		uint64_t minutes_taken = (end - start) / 60;

		(void) printf(gettext("Removal of vdev %llu copied %s "
		    "in %lluh%um, completed on %s"),
		    (longlong_t)prs->prs_removing_vdev,
		    copied_buf,
		    (u_longlong_t)(minutes_taken / 60),
		    (uint_t)(minutes_taken % 60),
		    ctime((time_t *)&end));
	} else if (prs->prs_state == DSS_CANCELED) {
		(void) printf(gettext("Removal of %s canceled on %s"),
		    vdev_name, ctime(&end));
	} else {
		uint64_t copied, total, elapsed, mins_left, hours_left;
		double fraction_done;
		uint_t rate;

		assert(prs->prs_state == DSS_SCANNING);

		/*
		 * Removal is in progress.
		 */
		(void) printf(gettext(
		    "Evacuation of %s in progress since %s"),
		    vdev_name, ctime(&start));

		copied = prs->prs_copied > 0 ? prs->prs_copied : 1;
		total = prs->prs_to_copy;
		fraction_done = (double)copied / total;

		/* elapsed time for this pass */
		elapsed = time(NULL) - prs->prs_start_time;
		elapsed = elapsed > 0 ? elapsed : 1;
		rate = copied / elapsed;
		rate = rate > 0 ? rate : 1;
		mins_left = ((total - copied) / rate) / 60;
		hours_left = mins_left / 60;

		zfs_nicenum(copied, examined_buf, sizeof (examined_buf));
		zfs_nicenum(total, total_buf, sizeof (total_buf));
		zfs_nicenum(rate, rate_buf, sizeof (rate_buf));

		/*
		 * do not print estimated time if hours_left is more than
		 * 30 days
		 */
		(void) printf(gettext("    %s copied out of %s at %s/s, "
		    "%.2f%% done"),
		    examined_buf, total_buf, rate_buf, 100 * fraction_done);
		if (hours_left < (30 * 24)) {
			(void) printf(gettext(", %lluh%um to go\n"),
			    (u_longlong_t)hours_left, (uint_t)(mins_left % 60));
		} else {
			(void) printf(gettext(
			    ", (copy is slow, no estimated time)\n"));
		}
	}

	if (prs->prs_mapping_memory > 0) {
		char mem_buf[7];
		zfs_nicenum(prs->prs_mapping_memory, mem_buf, sizeof (mem_buf));
		(void) printf(gettext("    %s memory used for "
		    "removed device mappings\n"),
		    mem_buf);
	}
}

static void
print_checkpoint_status(pool_checkpoint_stat_t *pcs)
{
	time_t start;
	char space_buf[7];

	if (pcs == NULL || pcs->pcs_state == CS_NONE)
		return;

	(void) printf(gettext("checkpoint: "));

	start = pcs->pcs_start_time;
	zfs_nicenum(pcs->pcs_space, space_buf, sizeof (space_buf));

	if (pcs->pcs_state == CS_CHECKPOINT_EXISTS) {
		char *date = ctime(&start);

		/*
		 * ctime() adds a newline at the end of the generated
		 * string, thus the weird format specifier and the
		 * strlen() call used to chop it off from the output.
		 */
		(void) printf(gettext("created %.*s, consumes %s\n"),
		    strlen(date) - 1, date, space_buf);
		return;
	}

	assert(pcs->pcs_state == CS_CHECKPOINT_DISCARDING);

	(void) printf(gettext("discarding, %s remaining.\n"),
	    space_buf);
}

static void
print_error_log(zpool_handle_t *zhp)
{
	nvlist_t *nverrlist = NULL;
	nvpair_t *elem;
	char *pathname;
	size_t len = MAXPATHLEN * 2;

	if (zpool_get_errlog(zhp, &nverrlist) != 0) {
		(void) printf("errors: List of errors unavailable "
		    "(insufficient privileges)\n");
		return;
	}

	(void) printf("errors: Permanent errors have been "
	    "detected in the following files:\n\n");

	pathname = safe_malloc(len);
	elem = NULL;
	while ((elem = nvlist_next_nvpair(nverrlist, elem)) != NULL) {
		nvlist_t *nv;
		uint64_t dsobj, obj;

		verify(nvpair_value_nvlist(elem, &nv) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_DATASET,
		    &dsobj) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_OBJECT,
		    &obj) == 0);
		zpool_obj_to_path(zhp, dsobj, obj, pathname, len);
		(void) printf("%7s %s\n", "", pathname);
	}
	free(pathname);
	nvlist_free(nverrlist);
}

static void
print_spares(zpool_handle_t *zhp, nvlist_t **spares, uint_t nspares,
    int namewidth)
{
	uint_t i;
	char *name;

	if (nspares == 0)
		return;

	(void) printf(gettext("\tspares\n"));

	for (i = 0; i < nspares; i++) {
		name = zpool_vdev_name(g_zfs, zhp, spares[i], B_FALSE);
		print_status_config(zhp, name, spares[i],
		    namewidth, 2, B_TRUE);
		free(name);
	}
}

static void
print_l2cache(zpool_handle_t *zhp, nvlist_t **l2cache, uint_t nl2cache,
    int namewidth)
{
	uint_t i;
	char *name;

	if (nl2cache == 0)
		return;

	(void) printf(gettext("\tcache\n"));

	for (i = 0; i < nl2cache; i++) {
		name = zpool_vdev_name(g_zfs, zhp, l2cache[i], B_FALSE);
		print_status_config(zhp, name, l2cache[i],
		    namewidth, 2, B_FALSE);
		free(name);
	}
}

static void
print_dedup_stats(nvlist_t *config)
{
	ddt_histogram_t *ddh;
	ddt_stat_t *dds;
	ddt_object_t *ddo;
	uint_t c;

	/*
	 * If the pool was faulted then we may not have been able to
	 * obtain the config. Otherwise, if we have anything in the dedup
	 * table continue processing the stats.
	 */
	if (nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_OBJ_STATS,
	    (uint64_t **)&ddo, &c) != 0)
		return;

	(void) printf("\n");
	(void) printf(gettext(" dedup: "));
	if (ddo->ddo_count == 0) {
		(void) printf(gettext("no DDT entries\n"));
		return;
	}

	(void) printf("DDT entries %llu, size %llu on disk, %llu in core\n",
	    (u_longlong_t)ddo->ddo_count,
	    (u_longlong_t)ddo->ddo_dspace,
	    (u_longlong_t)ddo->ddo_mspace);

	verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_STATS,
	    (uint64_t **)&dds, &c) == 0);
	verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_HISTOGRAM,
	    (uint64_t **)&ddh, &c) == 0);
	zpool_dump_ddt(dds, ddh);
}

/*
 * Display a summary of pool status.  Displays a summary such as:
 *
 *        pool: tank
 *	status: DEGRADED
 *	reason: One or more devices ...
 *         see: http://illumos.org/msg/ZFS-xxxx-01
 *	config:
 *		mirror		DEGRADED
 *                c1t0d0	OK
 *                c2t0d0	UNAVAIL
 *
 * When given the '-v' option, we print out the complete config.  If the '-e'
 * option is specified, then we print out error rate information as well.
 */
int
status_callback(zpool_handle_t *zhp, void *data)
{
	status_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;
	char *msgid;
	int reason;
	const char *health;
	uint_t c;
	vdev_stat_t *vs;

	config = zpool_get_config(zhp, NULL);
	reason = zpool_get_status(zhp, &msgid);

	cbp->cb_count++;

	/*
	 * If we were given 'zpool status -x', only report those pools with
	 * problems.
	 */
	if (cbp->cb_explain &&
	    (reason == ZPOOL_STATUS_OK ||
	    reason == ZPOOL_STATUS_VERSION_OLDER ||
	    reason == ZPOOL_STATUS_NON_NATIVE_ASHIFT ||
	    reason == ZPOOL_STATUS_FEAT_DISABLED)) {
		if (!cbp->cb_allpools) {
			(void) printf(gettext("pool '%s' is healthy\n"),
			    zpool_get_name(zhp));
			if (cbp->cb_first)
				cbp->cb_first = B_FALSE;
		}
		return (0);
	}

	if (cbp->cb_first)
		cbp->cb_first = B_FALSE;
	else
		(void) printf("\n");

	nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);
	health = zpool_state_to_name(vs->vs_state, vs->vs_aux);

	(void) printf(gettext("  pool: %s\n"), zpool_get_name(zhp));
	(void) printf(gettext(" state: %s\n"), health);

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
		(void) printf(gettext("status: One or more devices could not "
		    "be opened.  Sufficient replicas exist for\n\tthe pool to "
		    "continue functioning in a degraded state.\n"));
		(void) printf(gettext("action: Attach the missing device and "
		    "online it using 'zpool online'.\n"));
		break;

	case ZPOOL_STATUS_MISSING_DEV_NR:
		(void) printf(gettext("status: One or more devices could not "
		    "be opened.  There are insufficient\n\treplicas for the "
		    "pool to continue functioning.\n"));
		(void) printf(gettext("action: Attach the missing device and "
		    "online it using 'zpool online'.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
		(void) printf(gettext("status: One or more devices could not "
		    "be used because the label is missing or\n\tinvalid.  "
		    "Sufficient replicas exist for the pool to continue\n\t"
		    "functioning in a degraded state.\n"));
		(void) printf(gettext("action: Replace the device using "
		    "'zpool replace'.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		(void) printf(gettext("status: One or more devices could not "
		    "be used because the label is missing \n\tor invalid.  "
		    "There are insufficient replicas for the pool to "
		    "continue\n\tfunctioning.\n"));
		zpool_explain_recover(zpool_get_handle(zhp),
		    zpool_get_name(zhp), reason, config);
		break;

	case ZPOOL_STATUS_FAILING_DEV:
		(void) printf(gettext("status: One or more devices has "
		    "experienced an unrecoverable error.  An\n\tattempt was "
		    "made to correct the error.  Applications are "
		    "unaffected.\n"));
		(void) printf(gettext("action: Determine if the device needs "
		    "to be replaced, and clear the errors\n\tusing "
		    "'zpool clear' or replace the device with 'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		(void) printf(gettext("status: One or more devices has "
		    "been taken offline by the administrator.\n\tSufficient "
		    "replicas exist for the pool to continue functioning in "
		    "a\n\tdegraded state.\n"));
		(void) printf(gettext("action: Online the device using "
		    "'zpool online' or replace the device with\n\t'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_REMOVED_DEV:
		(void) printf(gettext("status: One or more devices has "
		    "been removed by the administrator.\n\tSufficient "
		    "replicas exist for the pool to continue functioning in "
		    "a\n\tdegraded state.\n"));
		(void) printf(gettext("action: Online the device using "
		    "'zpool online' or replace the device with\n\t'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_RESILVERING:
		(void) printf(gettext("status: One or more devices is "
		    "currently being resilvered.  The pool will\n\tcontinue "
		    "to function, possibly in a degraded state.\n"));
		(void) printf(gettext("action: Wait for the resilver to "
		    "complete.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		(void) printf(gettext("status: One or more devices has "
		    "experienced an error resulting in data\n\tcorruption.  "
		    "Applications may be affected.\n"));
		(void) printf(gettext("action: Restore the file in question "
		    "if possible.  Otherwise restore the\n\tentire pool from "
		    "backup.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		(void) printf(gettext("status: The pool metadata is corrupted "
		    "and the pool cannot be opened.\n"));
		zpool_explain_recover(zpool_get_handle(zhp),
		    zpool_get_name(zhp), reason, config);
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		(void) printf(gettext("status: The pool is formatted using a "
		    "legacy on-disk format.  The pool can\n\tstill be used, "
		    "but some features are unavailable.\n"));
		(void) printf(gettext("action: Upgrade the pool using 'zpool "
		    "upgrade'.  Once this is done, the\n\tpool will no longer "
		    "be accessible on software that does not support feature\n"
		    "\tflags.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		(void) printf(gettext("status: The pool has been upgraded to a "
		    "newer, incompatible on-disk version.\n\tThe pool cannot "
		    "be accessed on this system.\n"));
		(void) printf(gettext("action: Access the pool from a system "
		    "running more recent software, or\n\trestore the pool from "
		    "backup.\n"));
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		(void) printf(gettext("status: Some supported features are not "
		    "enabled on the pool. The pool can\n\tstill be used, but "
		    "some features are unavailable.\n"));
		(void) printf(gettext("action: Enable all features using "
		    "'zpool upgrade'. Once this is done,\n\tthe pool may no "
		    "longer be accessible by software that does not support\n\t"
		    "the features. See zpool-features(7) for details.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		(void) printf(gettext("status: The pool cannot be accessed on "
		    "this system because it uses the\n\tfollowing feature(s) "
		    "not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		(void) printf("\n");
		(void) printf(gettext("action: Access the pool from a system "
		    "that supports the required feature(s),\n\tor restore the "
		    "pool from backup.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		(void) printf(gettext("status: The pool can only be accessed "
		    "in read-only mode on this system. It\n\tcannot be "
		    "accessed in read-write mode because it uses the "
		    "following\n\tfeature(s) not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		(void) printf("\n");
		(void) printf(gettext("action: The pool cannot be accessed in "
		    "read-write mode. Import the pool with\n"
		    "\t\"-o readonly=on\", access the pool from a system that "
		    "supports the\n\trequired feature(s), or restore the "
		    "pool from backup.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
		(void) printf(gettext("status: One or more devices are "
		    "faulted in response to persistent errors.\n\tSufficient "
		    "replicas exist for the pool to continue functioning "
		    "in a\n\tdegraded state.\n"));
		(void) printf(gettext("action: Replace the faulted device, "
		    "or use 'zpool clear' to mark the device\n\trepaired.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_NR:
		(void) printf(gettext("status: One or more devices are "
		    "faulted in response to persistent errors.  There are "
		    "insufficient replicas for the pool to\n\tcontinue "
		    "functioning.\n"));
		(void) printf(gettext("action: Destroy and re-create the pool "
		    "from a backup source.  Manually marking the device\n"
		    "\trepaired using 'zpool clear' may allow some data "
		    "to be recovered.\n"));
		break;

	case ZPOOL_STATUS_IO_FAILURE_WAIT:
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
		(void) printf(gettext("status: One or more devices are "
		    "faulted in response to IO failures.\n"));
		(void) printf(gettext("action: Make sure the affected devices "
		    "are connected, then run 'zpool clear'.\n"));
		break;

	case ZPOOL_STATUS_BAD_LOG:
		(void) printf(gettext("status: An intent log record "
		    "could not be read.\n"
		    "\tWaiting for adminstrator intervention to fix the "
		    "faulted pool.\n"));
		(void) printf(gettext("action: Either restore the affected "
		    "device(s) and run 'zpool online',\n"
		    "\tor ignore the intent log records by running "
		    "'zpool clear'.\n"));
		break;

	case ZPOOL_STATUS_NON_NATIVE_ASHIFT:
		(void) printf(gettext("status: One or more devices are "
		    "configured to use a non-native block size.\n"
		    "\tExpect reduced performance.\n"));
		(void) printf(gettext("action: Replace affected devices with "
		    "devices that support the\n\tconfigured block size, or "
		    "migrate data to a properly configured\n\tpool.\n"));
		break;

	default:
		/*
		 * The remaining errors can't actually be generated, yet.
		 */
		assert(reason == ZPOOL_STATUS_OK);
	}

	if (msgid != NULL)
		(void) printf(gettext("   see: http://illumos.org/msg/%s\n"),
		    msgid);

	if (config != NULL) {
		int namewidth;
		uint64_t nerr;
		nvlist_t **spares, **l2cache;
		uint_t nspares, nl2cache;
		pool_checkpoint_stat_t *pcs = NULL;
		pool_scan_stat_t *ps = NULL;
		pool_removal_stat_t *prs = NULL;

		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_CHECKPOINT_STATS, (uint64_t **)&pcs, &c);
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_SCAN_STATS, (uint64_t **)&ps, &c);
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_REMOVAL_STATS, (uint64_t **)&prs, &c);

		print_scan_status(ps);
		print_checkpoint_scan_warning(ps, pcs);
		print_removal_status(zhp, prs);
		print_checkpoint_status(pcs);

		namewidth = max_width(zhp, nvroot, 0, 0);
		if (namewidth < 10)
			namewidth = 10;

		(void) printf(gettext("config:\n\n"));
		(void) printf(gettext("\t%-*s  %-8s %5s %5s %5s\n"), namewidth,
		    "NAME", "STATE", "READ", "WRITE", "CKSUM");
		print_status_config(zhp, zpool_get_name(zhp), nvroot,
		    namewidth, 0, B_FALSE);

		if (num_logs(nvroot) > 0)
			print_logs(zhp, nvroot, namewidth, B_TRUE);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0)
			print_l2cache(zhp, l2cache, nl2cache, namewidth);

		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0)
			print_spares(zhp, spares, nspares, namewidth);

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRCOUNT,
		    &nerr) == 0) {
			nvlist_t *nverrlist = NULL;

			/*
			 * If the approximate error count is small, get a
			 * precise count by fetching the entire log and
			 * uniquifying the results.
			 */
			if (nerr > 0 && nerr < 100 && !cbp->cb_verbose &&
			    zpool_get_errlog(zhp, &nverrlist) == 0) {
				nvpair_t *elem;

				elem = NULL;
				nerr = 0;
				while ((elem = nvlist_next_nvpair(nverrlist,
				    elem)) != NULL) {
					nerr++;
				}
			}
			nvlist_free(nverrlist);

			(void) printf("\n");

			if (nerr == 0)
				(void) printf(gettext("errors: No known data "
				    "errors\n"));
			else if (!cbp->cb_verbose)
				(void) printf(gettext("errors: %llu data "
				    "errors, use '-v' for a list\n"),
				    (u_longlong_t)nerr);
			else
				print_error_log(zhp);
		}

		if (cbp->cb_dedup_stats)
			print_dedup_stats(config);
	} else {
		(void) printf(gettext("config: The configuration cannot be "
		    "determined.\n"));
	}

	return (0);
}

/*
 * zpool status [-vx] [-T d|u] [pool] ... [interval [count]]
 *
 *	-v	Display complete error logs
 *	-x	Display only pools with potential problems
 *	-D	Display dedup status (undocumented)
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * Describes the health status of all pools or some subset.
 */
int
zpool_do_status(int argc, char **argv)
{
	int c;
	int ret;
	unsigned long interval = 0, count = 0;
	status_cbdata_t cb = { 0 };

	/* check options */
	while ((c = getopt(argc, argv, "vxDT:")) != -1) {
		switch (c) {
		case 'v':
			cb.cb_verbose = B_TRUE;
			break;
		case 'x':
			cb.cb_explain = B_TRUE;
			break;
		case 'D':
			cb.cb_dedup_stats = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (argc == 0)
		cb.cb_allpools = B_TRUE;

	cb.cb_first = B_TRUE;

	for (;;) {
		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		ret = for_each_pool(argc, argv, B_TRUE, NULL,
		    status_callback, &cb);

		if (argc == 0 && cb.cb_count == 0)
			(void) printf(gettext("no pools available\n"));
		else if (cb.cb_explain && cb.cb_first && cb.cb_allpools)
			(void) printf(gettext("all pools are healthy\n"));

		if (ret != 0)
			return (ret);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) sleep(interval);
	}

	return (0);
}

typedef struct upgrade_cbdata {
	boolean_t	cb_first;
	boolean_t	cb_unavail;
	char		cb_poolname[ZFS_MAX_DATASET_NAME_LEN];
	int		cb_argc;
	uint64_t	cb_version;
	char		**cb_argv;
} upgrade_cbdata_t;

#ifdef __FreeBSD__
static int
is_root_pool(zpool_handle_t *zhp)
{
	static struct statfs sfs;
	static char *poolname = NULL;
	static boolean_t stated = B_FALSE;
	char *slash;

	if (!stated) {
		stated = B_TRUE;
		if (statfs("/", &sfs) == -1) {
			(void) fprintf(stderr,
			    "Unable to stat root file system: %s.\n",
			    strerror(errno));
			return (0);
		}
		if (strcmp(sfs.f_fstypename, "zfs") != 0)
			return (0);
		poolname = sfs.f_mntfromname;
		if ((slash = strchr(poolname, '/')) != NULL)
			*slash = '\0';
	}
	return (poolname != NULL && strcmp(poolname, zpool_get_name(zhp)) == 0);
}

static void
root_pool_upgrade_check(zpool_handle_t *zhp, char *poolname, int size)
{

	if (poolname[0] == '\0' && is_root_pool(zhp))
		(void) strlcpy(poolname, zpool_get_name(zhp), size);
}
#endif	/* FreeBSD */

static int
upgrade_version(zpool_handle_t *zhp, uint64_t version)
{
	int ret;
	nvlist_t *config;
	uint64_t oldversion;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &oldversion) == 0);

	assert(SPA_VERSION_IS_SUPPORTED(oldversion));
	assert(oldversion < version);

	ret = zpool_upgrade(zhp, version);
	if (ret != 0)
		return (ret);

	if (version >= SPA_VERSION_FEATURES) {
		(void) printf(gettext("Successfully upgraded "
		    "'%s' from version %llu to feature flags.\n"),
		    zpool_get_name(zhp), oldversion);
	} else {
		(void) printf(gettext("Successfully upgraded "
		    "'%s' from version %llu to version %llu.\n"),
		    zpool_get_name(zhp), oldversion, version);
	}

	return (0);
}

static int
upgrade_enable_all(zpool_handle_t *zhp, int *countp)
{
	int i, ret, count;
	boolean_t firstff = B_TRUE;
	nvlist_t *enabled = zpool_get_features(zhp);

	count = 0;
	for (i = 0; i < SPA_FEATURES; i++) {
		const char *fname = spa_feature_table[i].fi_uname;
		const char *fguid = spa_feature_table[i].fi_guid;
		if (!nvlist_exists(enabled, fguid)) {
			char *propname;
			verify(-1 != asprintf(&propname, "feature@%s", fname));
			ret = zpool_set_prop(zhp, propname,
			    ZFS_FEATURE_ENABLED);
			if (ret != 0) {
				free(propname);
				return (ret);
			}
			count++;

			if (firstff) {
				(void) printf(gettext("Enabled the "
				    "following features on '%s':\n"),
				    zpool_get_name(zhp));
				firstff = B_FALSE;
			}
			(void) printf(gettext("  %s\n"), fname);
			free(propname);
		}
	}

	if (countp != NULL)
		*countp = count;
	return (0);
}

static int
upgrade_cb(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;
	nvlist_t *config;
	uint64_t version;
	boolean_t printnl = B_FALSE;
	int ret;

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		(void) fprintf(stderr, gettext("cannot upgrade '%s': pool is "
		    "currently unavailable.\n\n"), zpool_get_name(zhp));
		cbp->cb_unavail = B_TRUE;
		/* Allow iteration to continue. */
		return (0);
	}

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);

	assert(SPA_VERSION_IS_SUPPORTED(version));

	if (version < cbp->cb_version) {
		cbp->cb_first = B_FALSE;
		ret = upgrade_version(zhp, cbp->cb_version);
		if (ret != 0)
			return (ret);
#ifdef __FreeBSD__
		root_pool_upgrade_check(zhp, cbp->cb_poolname,
		    sizeof(cbp->cb_poolname));
#endif	/* __FreeBSD__ */
		printnl = B_TRUE;

#ifdef illumos
		/*
		 * If they did "zpool upgrade -a", then we could
		 * be doing ioctls to different pools.  We need
		 * to log this history once to each pool, and bypass
		 * the normal history logging that happens in main().
		 */
		(void) zpool_log_history(g_zfs, history_str);
		log_history = B_FALSE;
#endif
	}

	if (cbp->cb_version >= SPA_VERSION_FEATURES) {
		int count;
		ret = upgrade_enable_all(zhp, &count);
		if (ret != 0)
			return (ret);

		if (count > 0) {
			cbp->cb_first = B_FALSE;
			printnl = B_TRUE;
#ifdef __FreeBSD__
			root_pool_upgrade_check(zhp, cbp->cb_poolname,
			    sizeof(cbp->cb_poolname));
#endif	/* __FreeBSD__ */
			/*
			 * If they did "zpool upgrade -a", then we could
			 * be doing ioctls to different pools.  We need
			 * to log this history once to each pool, and bypass
			 * the normal history logging that happens in main().
			 */
			(void) zpool_log_history(g_zfs, history_str);
			log_history = B_FALSE;
		}
	}

	if (printnl) {
		(void) printf(gettext("\n"));
	}

	return (0);
}

static int
upgrade_list_unavail(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		if (cbp->cb_first) {
			(void) fprintf(stderr, gettext("The following pools "
			    "are unavailable and cannot be upgraded as this "
			    "time.\n\n"));
			(void) fprintf(stderr, gettext("POOL\n"));
			(void) fprintf(stderr, gettext("------------\n"));
			cbp->cb_first = B_FALSE;
		}
		(void) printf(gettext("%s\n"), zpool_get_name(zhp));
		cbp->cb_unavail = B_TRUE;
	}
	return (0);
}

static int
upgrade_list_older_cb(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;
	nvlist_t *config;
	uint64_t version;

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		/*
		 * This will have been reported by upgrade_list_unavail so
		 * just allow iteration to continue.
		 */
		cbp->cb_unavail = B_TRUE;
		return (0);
	}

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);

	assert(SPA_VERSION_IS_SUPPORTED(version));

	if (version < SPA_VERSION_FEATURES) {
		if (cbp->cb_first) {
			(void) printf(gettext("The following pools are "
			    "formatted with legacy version numbers and can\n"
			    "be upgraded to use feature flags.  After "
			    "being upgraded, these pools\nwill no "
			    "longer be accessible by software that does not "
			    "support feature\nflags.\n\n"));
			(void) printf(gettext("VER  POOL\n"));
			(void) printf(gettext("---  ------------\n"));
			cbp->cb_first = B_FALSE;
		}

		(void) printf("%2llu   %s\n", (u_longlong_t)version,
		    zpool_get_name(zhp));
	}

	return (0);
}

static int
upgrade_list_disabled_cb(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;
	nvlist_t *config;
	uint64_t version;

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		/*
		 * This will have been reported by upgrade_list_unavail so
		 * just allow iteration to continue.
		 */
		cbp->cb_unavail = B_TRUE;
		return (0);
	}

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);

	if (version >= SPA_VERSION_FEATURES) {
		int i;
		boolean_t poolfirst = B_TRUE;
		nvlist_t *enabled = zpool_get_features(zhp);

		for (i = 0; i < SPA_FEATURES; i++) {
			const char *fguid = spa_feature_table[i].fi_guid;
			const char *fname = spa_feature_table[i].fi_uname;
			if (!nvlist_exists(enabled, fguid)) {
				if (cbp->cb_first) {
					(void) printf(gettext("\nSome "
					    "supported features are not "
					    "enabled on the following pools. "
					    "Once a\nfeature is enabled the "
					    "pool may become incompatible with "
					    "software\nthat does not support "
					    "the feature. See "
					    "zpool-features(7) for "
					    "details.\n\n"));
					(void) printf(gettext("POOL  "
					    "FEATURE\n"));
					(void) printf(gettext("------"
					    "---------\n"));
					cbp->cb_first = B_FALSE;
				}

				if (poolfirst) {
					(void) printf(gettext("%s\n"),
					    zpool_get_name(zhp));
					poolfirst = B_FALSE;
				}

				(void) printf(gettext("      %s\n"), fname);
			}
		}
	}

	return (0);
}

/* ARGSUSED */
static int
upgrade_one(zpool_handle_t *zhp, void *data)
{
	boolean_t printnl = B_FALSE;
	upgrade_cbdata_t *cbp = data;
	uint64_t cur_version;
	int ret;

	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		(void) fprintf(stderr, gettext("cannot upgrade '%s': pool is "
		    "is currently unavailable.\n\n"), zpool_get_name(zhp));
		cbp->cb_unavail = B_TRUE;
		return (1);
	}

	if (strcmp("log", zpool_get_name(zhp)) == 0) {
		(void) printf(gettext("'log' is now a reserved word\n"
		    "Pool 'log' must be renamed using export and import"
		    " to upgrade.\n\n"));
		return (1);
	}

	cur_version = zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL);
	if (cur_version > cbp->cb_version) {
		(void) printf(gettext("Pool '%s' is already formatted "
		    "using more current version '%llu'.\n\n"),
		    zpool_get_name(zhp), cur_version);
		return (0);
	}

	if (cbp->cb_version != SPA_VERSION && cur_version == cbp->cb_version) {
		(void) printf(gettext("Pool '%s' is already formatted "
		    "using version %llu.\n\n"), zpool_get_name(zhp),
		    cbp->cb_version);
		return (0);
	}

	if (cur_version != cbp->cb_version) {
		printnl = B_TRUE;
		ret = upgrade_version(zhp, cbp->cb_version);
		if (ret != 0)
			return (ret);
#ifdef __FreeBSD__
		root_pool_upgrade_check(zhp, cbp->cb_poolname,
		    sizeof(cbp->cb_poolname));
#endif	/* __FreeBSD__ */
	}

	if (cbp->cb_version >= SPA_VERSION_FEATURES) {
		int count = 0;
		ret = upgrade_enable_all(zhp, &count);
		if (ret != 0)
			return (ret);

		if (count != 0) {
			printnl = B_TRUE;
#ifdef __FreeBSD__
			root_pool_upgrade_check(zhp, cbp->cb_poolname,
			    sizeof(cbp->cb_poolname));
#endif	/* __FreeBSD __*/
		} else if (cur_version == SPA_VERSION) {
			(void) printf(gettext("Pool '%s' already has all "
			    "supported features enabled.\n\n"),
			    zpool_get_name(zhp));
		}
	}

	if (printnl) {
		(void) printf(gettext("\n"));
	}

	return (0);
}

/*
 * zpool upgrade
 * zpool upgrade -v
 * zpool upgrade [-V version] <-a | pool ...>
 *
 * With no arguments, display downrev'd ZFS pool available for upgrade.
 * Individual pools can be upgraded by specifying the pool, and '-a' will
 * upgrade all pools.
 */
int
zpool_do_upgrade(int argc, char **argv)
{
	int c;
	upgrade_cbdata_t cb = { 0 };
	int ret = 0;
	boolean_t showversions = B_FALSE;
	boolean_t upgradeall = B_FALSE;
	char *end;


	/* check options */
	while ((c = getopt(argc, argv, ":avV:")) != -1) {
		switch (c) {
		case 'a':
			upgradeall = B_TRUE;
			break;
		case 'v':
			showversions = B_TRUE;
			break;
		case 'V':
			cb.cb_version = strtoll(optarg, &end, 10);
			if (*end != '\0' ||
			    !SPA_VERSION_IS_SUPPORTED(cb.cb_version)) {
				(void) fprintf(stderr,
				    gettext("invalid version '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	cb.cb_argc = argc;
	cb.cb_argv = argv;
	argc -= optind;
	argv += optind;

	if (cb.cb_version == 0) {
		cb.cb_version = SPA_VERSION;
	} else if (!upgradeall && argc == 0) {
		(void) fprintf(stderr, gettext("-V option is "
		    "incompatible with other arguments\n"));
		usage(B_FALSE);
	}

	if (showversions) {
		if (upgradeall || argc != 0) {
			(void) fprintf(stderr, gettext("-v option is "
			    "incompatible with other arguments\n"));
			usage(B_FALSE);
		}
	} else if (upgradeall) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("-a option should not "
			    "be used along with a pool name\n"));
			usage(B_FALSE);
		}
	}

	(void) printf(gettext("This system supports ZFS pool feature "
	    "flags.\n\n"));
	if (showversions) {
		int i;

		(void) printf(gettext("The following features are "
		    "supported:\n\n"));
		(void) printf(gettext("FEAT DESCRIPTION\n"));
		(void) printf("----------------------------------------------"
		    "---------------\n");
		for (i = 0; i < SPA_FEATURES; i++) {
			zfeature_info_t *fi = &spa_feature_table[i];
			const char *ro =
			    (fi->fi_flags & ZFEATURE_FLAG_READONLY_COMPAT) ?
			    " (read-only compatible)" : "";

			(void) printf("%-37s%s\n", fi->fi_uname, ro);
			(void) printf("     %s\n", fi->fi_desc);
		}
		(void) printf("\n");

		(void) printf(gettext("The following legacy versions are also "
		    "supported:\n\n"));
		(void) printf(gettext("VER  DESCRIPTION\n"));
		(void) printf("---  -----------------------------------------"
		    "---------------\n");
		(void) printf(gettext(" 1   Initial ZFS version\n"));
		(void) printf(gettext(" 2   Ditto blocks "
		    "(replicated metadata)\n"));
		(void) printf(gettext(" 3   Hot spares and double parity "
		    "RAID-Z\n"));
		(void) printf(gettext(" 4   zpool history\n"));
		(void) printf(gettext(" 5   Compression using the gzip "
		    "algorithm\n"));
		(void) printf(gettext(" 6   bootfs pool property\n"));
		(void) printf(gettext(" 7   Separate intent log devices\n"));
		(void) printf(gettext(" 8   Delegated administration\n"));
		(void) printf(gettext(" 9   refquota and refreservation "
		    "properties\n"));
		(void) printf(gettext(" 10  Cache devices\n"));
		(void) printf(gettext(" 11  Improved scrub performance\n"));
		(void) printf(gettext(" 12  Snapshot properties\n"));
		(void) printf(gettext(" 13  snapused property\n"));
		(void) printf(gettext(" 14  passthrough-x aclinherit\n"));
		(void) printf(gettext(" 15  user/group space accounting\n"));
		(void) printf(gettext(" 16  stmf property support\n"));
		(void) printf(gettext(" 17  Triple-parity RAID-Z\n"));
		(void) printf(gettext(" 18  Snapshot user holds\n"));
		(void) printf(gettext(" 19  Log device removal\n"));
		(void) printf(gettext(" 20  Compression using zle "
		    "(zero-length encoding)\n"));
		(void) printf(gettext(" 21  Deduplication\n"));
		(void) printf(gettext(" 22  Received properties\n"));
		(void) printf(gettext(" 23  Slim ZIL\n"));
		(void) printf(gettext(" 24  System attributes\n"));
		(void) printf(gettext(" 25  Improved scrub stats\n"));
		(void) printf(gettext(" 26  Improved snapshot deletion "
		    "performance\n"));
		(void) printf(gettext(" 27  Improved snapshot creation "
		    "performance\n"));
		(void) printf(gettext(" 28  Multiple vdev replacements\n"));
		(void) printf(gettext("\nFor more information on a particular "
		    "version, including supported releases,\n"));
		(void) printf(gettext("see the ZFS Administration Guide.\n\n"));
	} else if (argc == 0 && upgradeall) {
		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_cb, &cb);
		if (ret == 0 && cb.cb_first) {
			if (cb.cb_version == SPA_VERSION) {
				(void) printf(gettext("All %spools are already "
				    "formatted using feature flags.\n\n"),
				    cb.cb_unavail ? gettext("available ") : "");
				(void) printf(gettext("Every %sfeature flags "
				    "pool already has all supported features "
				    "enabled.\n"),
				    cb.cb_unavail ? gettext("available ") : "");
			} else {
				(void) printf(gettext("All pools are already "
				    "formatted with version %llu or higher.\n"),
				    cb.cb_version);
			}
		}
	} else if (argc == 0) {
		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_list_unavail, &cb);
		assert(ret == 0);

		if (!cb.cb_first) {
			(void) fprintf(stderr, "\n");
		}

		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_list_older_cb, &cb);
		assert(ret == 0);

		if (cb.cb_first) {
			(void) printf(gettext("All %spools are formatted using "
			    "feature flags.\n\n"), cb.cb_unavail ?
			    gettext("available ") : "");
		} else {
			(void) printf(gettext("\nUse 'zpool upgrade -v' "
			    "for a list of available legacy versions.\n"));
		}

		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_list_disabled_cb, &cb);
		assert(ret == 0);

		if (cb.cb_first) {
			(void) printf(gettext("Every %sfeature flags pool has "
			    "all supported features enabled.\n"),
			    cb.cb_unavail ? gettext("available ") : "");
		} else {
			(void) printf(gettext("\n"));
		}
	} else {
		ret = for_each_pool(argc, argv, B_TRUE, NULL,
		    upgrade_one, &cb);
	}

	if (cb.cb_poolname[0] != '\0') {
		(void) printf(
		    "If you boot from pool '%s', don't forget to update boot code.\n"
		    "Assuming you use GPT partitioning and da0 is your boot disk\n"
		    "the following command will do it:\n"
		    "\n"
		    "\tgpart bootcode -b /boot/pmbr -p /boot/gptzfsboot -i 1 da0\n\n",
		    cb.cb_poolname);
	}

	return (ret);
}

typedef struct hist_cbdata {
	boolean_t first;
	boolean_t longfmt;
	boolean_t internal;
} hist_cbdata_t;

/*
 * Print out the command history for a specific pool.
 */
static int
get_history_one(zpool_handle_t *zhp, void *data)
{
	nvlist_t *nvhis;
	nvlist_t **records;
	uint_t numrecords;
	int ret, i;
	hist_cbdata_t *cb = (hist_cbdata_t *)data;

	cb->first = B_FALSE;

	(void) printf(gettext("History for '%s':\n"), zpool_get_name(zhp));

	if ((ret = zpool_get_history(zhp, &nvhis)) != 0)
		return (ret);

	verify(nvlist_lookup_nvlist_array(nvhis, ZPOOL_HIST_RECORD,
	    &records, &numrecords) == 0);
	for (i = 0; i < numrecords; i++) {
		nvlist_t *rec = records[i];
		char tbuf[30] = "";

		if (nvlist_exists(rec, ZPOOL_HIST_TIME)) {
			time_t tsec;
			struct tm t;

			tsec = fnvlist_lookup_uint64(records[i],
			    ZPOOL_HIST_TIME);
			(void) localtime_r(&tsec, &t);
			(void) strftime(tbuf, sizeof (tbuf), "%F.%T", &t);
		}

		if (nvlist_exists(rec, ZPOOL_HIST_CMD)) {
			(void) printf("%s %s", tbuf,
			    fnvlist_lookup_string(rec, ZPOOL_HIST_CMD));
		} else if (nvlist_exists(rec, ZPOOL_HIST_INT_EVENT)) {
			int ievent =
			    fnvlist_lookup_uint64(rec, ZPOOL_HIST_INT_EVENT);
			if (!cb->internal)
				continue;
			if (ievent >= ZFS_NUM_LEGACY_HISTORY_EVENTS) {
				(void) printf("%s unrecognized record:\n",
				    tbuf);
				dump_nvlist(rec, 4);
				continue;
			}
			(void) printf("%s [internal %s txg:%lld] %s", tbuf,
			    zfs_history_event_names[ievent],
			    fnvlist_lookup_uint64(rec, ZPOOL_HIST_TXG),
			    fnvlist_lookup_string(rec, ZPOOL_HIST_INT_STR));
		} else if (nvlist_exists(rec, ZPOOL_HIST_INT_NAME)) {
			if (!cb->internal)
				continue;
			(void) printf("%s [txg:%lld] %s", tbuf,
			    fnvlist_lookup_uint64(rec, ZPOOL_HIST_TXG),
			    fnvlist_lookup_string(rec, ZPOOL_HIST_INT_NAME));
			if (nvlist_exists(rec, ZPOOL_HIST_DSNAME)) {
				(void) printf(" %s (%llu)",
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_DSNAME),
				    fnvlist_lookup_uint64(rec,
				    ZPOOL_HIST_DSID));
			}
			(void) printf(" %s", fnvlist_lookup_string(rec,
			    ZPOOL_HIST_INT_STR));
		} else if (nvlist_exists(rec, ZPOOL_HIST_IOCTL)) {
			if (!cb->internal)
				continue;
			(void) printf("%s ioctl %s\n", tbuf,
			    fnvlist_lookup_string(rec, ZPOOL_HIST_IOCTL));
			if (nvlist_exists(rec, ZPOOL_HIST_INPUT_NVL)) {
				(void) printf("    input:\n");
				dump_nvlist(fnvlist_lookup_nvlist(rec,
				    ZPOOL_HIST_INPUT_NVL), 8);
			}
			if (nvlist_exists(rec, ZPOOL_HIST_OUTPUT_NVL)) {
				(void) printf("    output:\n");
				dump_nvlist(fnvlist_lookup_nvlist(rec,
				    ZPOOL_HIST_OUTPUT_NVL), 8);
			}
			if (nvlist_exists(rec, ZPOOL_HIST_ERRNO)) {
				(void) printf("    errno: %lld\n",
				    fnvlist_lookup_int64(rec,
				    ZPOOL_HIST_ERRNO));
			}
		} else {
			if (!cb->internal)
				continue;
			(void) printf("%s unrecognized record:\n", tbuf);
			dump_nvlist(rec, 4);
		}

		if (!cb->longfmt) {
			(void) printf("\n");
			continue;
		}
		(void) printf(" [");
		if (nvlist_exists(rec, ZPOOL_HIST_WHO)) {
			uid_t who = fnvlist_lookup_uint64(rec, ZPOOL_HIST_WHO);
			struct passwd *pwd = getpwuid(who);
			(void) printf("user %d ", (int)who);
			if (pwd != NULL)
				(void) printf("(%s) ", pwd->pw_name);
		}
		if (nvlist_exists(rec, ZPOOL_HIST_HOST)) {
			(void) printf("on %s",
			    fnvlist_lookup_string(rec, ZPOOL_HIST_HOST));
		}
		if (nvlist_exists(rec, ZPOOL_HIST_ZONE)) {
			(void) printf(":%s",
			    fnvlist_lookup_string(rec, ZPOOL_HIST_ZONE));
		}
		(void) printf("]");
		(void) printf("\n");
	}
	(void) printf("\n");
	nvlist_free(nvhis);

	return (ret);
}

/*
 * zpool history <pool>
 *
 * Displays the history of commands that modified pools.
 */
int
zpool_do_history(int argc, char **argv)
{
	hist_cbdata_t cbdata = { 0 };
	int ret;
	int c;

	cbdata.first = B_TRUE;
	/* check options */
	while ((c = getopt(argc, argv, "li")) != -1) {
		switch (c) {
		case 'l':
			cbdata.longfmt = B_TRUE;
			break;
		case 'i':
			cbdata.internal = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}
	argc -= optind;
	argv += optind;

	ret = for_each_pool(argc, argv, B_FALSE,  NULL, get_history_one,
	    &cbdata);

	if (argc == 0 && cbdata.first == B_TRUE) {
		(void) printf(gettext("no pools available\n"));
		return (0);
	}

	return (ret);
}

static int
get_callback(zpool_handle_t *zhp, void *data)
{
	zprop_get_cbdata_t *cbp = (zprop_get_cbdata_t *)data;
	char value[MAXNAMELEN];
	zprop_source_t srctype;
	zprop_list_t *pl;

	for (pl = cbp->cb_proplist; pl != NULL; pl = pl->pl_next) {

		/*
		 * Skip the special fake placeholder. This will also skip
		 * over the name property when 'all' is specified.
		 */
		if (pl->pl_prop == ZPOOL_PROP_NAME &&
		    pl == cbp->cb_proplist)
			continue;

		if (pl->pl_prop == ZPROP_INVAL &&
		    (zpool_prop_feature(pl->pl_user_prop) ||
		    zpool_prop_unsupported(pl->pl_user_prop))) {
			srctype = ZPROP_SRC_LOCAL;

			if (zpool_prop_get_feature(zhp, pl->pl_user_prop,
			    value, sizeof (value)) == 0) {
				zprop_print_one_property(zpool_get_name(zhp),
				    cbp, pl->pl_user_prop, value, srctype,
				    NULL, NULL);
			}
		} else {
			if (zpool_get_prop(zhp, pl->pl_prop, value,
			    sizeof (value), &srctype, cbp->cb_literal) != 0)
				continue;

			zprop_print_one_property(zpool_get_name(zhp), cbp,
			    zpool_prop_to_name(pl->pl_prop), value, srctype,
			    NULL, NULL);
		}
	}
	return (0);
}

/*
 * zpool get [-Hp] [-o "all" | field[,...]] <"all" | property[,...]> <pool> ...
 *
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-o	List of columns to display.  Defaults to
 *		"name,property,value,source".
 *	-p	Diplay values in parsable (exact) format.
 *
 * Get properties of pools in the system. Output space statistics
 * for each one as well as other attributes.
 */
int
zpool_do_get(int argc, char **argv)
{
	zprop_get_cbdata_t cb = { 0 };
	zprop_list_t fake_name = { 0 };
	int ret;
	int c, i;
	char *value;

	cb.cb_first = B_TRUE;

	/*
	 * Set up default columns and sources.
	 */
	cb.cb_sources = ZPROP_SRC_ALL;
	cb.cb_columns[0] = GET_COL_NAME;
	cb.cb_columns[1] = GET_COL_PROPERTY;
	cb.cb_columns[2] = GET_COL_VALUE;
	cb.cb_columns[3] = GET_COL_SOURCE;
	cb.cb_type = ZFS_TYPE_POOL;

	/* check options */
	while ((c = getopt(argc, argv, ":Hpo:")) != -1) {
		switch (c) {
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 'o':
			bzero(&cb.cb_columns, sizeof (cb.cb_columns));
			i = 0;
			while (*optarg != '\0') {
				static char *col_subopts[] =
				{ "name", "property", "value", "source",
				"all", NULL };

				if (i == ZFS_GET_NCOLS) {
					(void) fprintf(stderr, gettext("too "
					"many fields given to -o "
					"option\n"));
					usage(B_FALSE);
				}

				switch (getsubopt(&optarg, col_subopts,
				    &value)) {
				case 0:
					cb.cb_columns[i++] = GET_COL_NAME;
					break;
				case 1:
					cb.cb_columns[i++] = GET_COL_PROPERTY;
					break;
				case 2:
					cb.cb_columns[i++] = GET_COL_VALUE;
					break;
				case 3:
					cb.cb_columns[i++] = GET_COL_SOURCE;
					break;
				case 4:
					if (i > 0) {
						(void) fprintf(stderr,
						    gettext("\"all\" conflicts "
						    "with specific fields "
						    "given to -o option\n"));
						usage(B_FALSE);
					}
					cb.cb_columns[0] = GET_COL_NAME;
					cb.cb_columns[1] = GET_COL_PROPERTY;
					cb.cb_columns[2] = GET_COL_VALUE;
					cb.cb_columns[3] = GET_COL_SOURCE;
					i = ZFS_GET_NCOLS;
					break;
				default:
					(void) fprintf(stderr,
					    gettext("invalid column name "
					    "'%s'\n"), suboptarg);
					usage(B_FALSE);
				}
			}
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing property "
		    "argument\n"));
		usage(B_FALSE);
	}

	if (zprop_get_list(g_zfs, argv[0], &cb.cb_proplist,
	    ZFS_TYPE_POOL) != 0)
		usage(B_FALSE);

	argc--;
	argv++;

	if (cb.cb_proplist != NULL) {
		fake_name.pl_prop = ZPOOL_PROP_NAME;
		fake_name.pl_width = strlen(gettext("NAME"));
		fake_name.pl_next = cb.cb_proplist;
		cb.cb_proplist = &fake_name;
	}

	ret = for_each_pool(argc, argv, B_TRUE, &cb.cb_proplist,
	    get_callback, &cb);

	if (cb.cb_proplist == &fake_name)
		zprop_free_list(fake_name.pl_next);
	else
		zprop_free_list(cb.cb_proplist);

	return (ret);
}

typedef struct set_cbdata {
	char *cb_propname;
	char *cb_value;
	boolean_t cb_any_successful;
} set_cbdata_t;

int
set_callback(zpool_handle_t *zhp, void *data)
{
	int error;
	set_cbdata_t *cb = (set_cbdata_t *)data;

	error = zpool_set_prop(zhp, cb->cb_propname, cb->cb_value);

	if (!error)
		cb->cb_any_successful = B_TRUE;

	return (error);
}

int
zpool_do_set(int argc, char **argv)
{
	set_cbdata_t cb = { 0 };
	int error;

	if (argc > 1 && argv[1][0] == '-') {
		(void) fprintf(stderr, gettext("invalid option '%c'\n"),
		    argv[1][1]);
		usage(B_FALSE);
	}

	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing property=value "
		    "argument\n"));
		usage(B_FALSE);
	}

	if (argc < 3) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}

	if (argc > 3) {
		(void) fprintf(stderr, gettext("too many pool names\n"));
		usage(B_FALSE);
	}

	cb.cb_propname = argv[1];
	cb.cb_value = strchr(cb.cb_propname, '=');
	if (cb.cb_value == NULL) {
		(void) fprintf(stderr, gettext("missing value in "
		    "property=value argument\n"));
		usage(B_FALSE);
	}

	*(cb.cb_value) = '\0';
	cb.cb_value++;

	error = for_each_pool(argc - 2, argv + 2, B_TRUE, NULL,
	    set_callback, &cb);

	return (error);
}

static int
find_command_idx(char *command, int *idx)
{
	int i;

	for (i = 0; i < NCOMMAND; i++) {
		if (command_table[i].name == NULL)
			continue;

		if (strcmp(command, command_table[i].name) == 0) {
			*idx = i;
			return (0);
		}
	}
	return (1);
}

int
main(int argc, char **argv)
{
	int ret = 0;
	int i;
	char *cmdname;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		return (1);
	}

	libzfs_print_on_error(g_zfs, B_TRUE);

	opterr = 0;

	/*
	 * Make sure the user has specified some command.
	 */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing command\n"));
		usage(B_FALSE);
	}

	cmdname = argv[1];

	/*
	 * Special case '-?'
	 */
	if (strcmp(cmdname, "-?") == 0)
		usage(B_TRUE);

	zfs_save_arguments(argc, argv, history_str, sizeof (history_str));

	/*
	 * Run the appropriate command.
	 */
	if (find_command_idx(cmdname, &i) == 0) {
		current_command = &command_table[i];
		ret = command_table[i].func(argc - 1, argv + 1);
	} else if (strchr(cmdname, '=')) {
		verify(find_command_idx("set", &i) == 0);
		current_command = &command_table[i];
		ret = command_table[i].func(argc, argv);
	} else if (strcmp(cmdname, "freeze") == 0 && argc == 3) {
		/*
		 * 'freeze' is a vile debugging abomination, so we treat
		 * it as such.
		 */
		zfs_cmd_t zc = { 0 };
		(void) strlcpy(zc.zc_name, argv[2], sizeof (zc.zc_name));
		return (!!zfs_ioctl(g_zfs, ZFS_IOC_POOL_FREEZE, &zc));
	} else {
		(void) fprintf(stderr, gettext("unrecognized "
		    "command '%s'\n"), cmdname);
		usage(B_FALSE);
	}

	if (ret == 0 && log_history)
		(void) zpool_log_history(g_zfs, history_str);

	libzfs_fini(g_zfs);

	/*
	 * The 'ZFS_ABORT' environment variable causes us to dump core on exit
	 * for the purposes of running ::findleaks.
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	return (ret);
}
