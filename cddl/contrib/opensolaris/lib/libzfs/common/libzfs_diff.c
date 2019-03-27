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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2015, 2017 by Delphix. All rights reserved.
 * Copyright 2016 Joyent, Inc.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 */

/*
 * zfs diff support
 */
#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/zfs_ioctl.h>
#include <libzfs.h>
#include "libzfs_impl.h"

#define	ZDIFF_SNAPDIR		"/.zfs/snapshot/"
#define	ZDIFF_SHARESDIR 	"/.zfs/shares/"
#define	ZDIFF_PREFIX		"zfs-diff-%d"

#define	ZDIFF_ADDED	'+'
#define	ZDIFF_MODIFIED	'M'
#define	ZDIFF_REMOVED	'-'
#define	ZDIFF_RENAMED	'R'

typedef struct differ_info {
	zfs_handle_t *zhp;
	char *fromsnap;
	char *frommnt;
	char *tosnap;
	char *tomnt;
	char *ds;
	char *dsmnt;
	char *tmpsnap;
	char errbuf[1024];
	boolean_t isclone;
	boolean_t scripted;
	boolean_t classify;
	boolean_t timestamped;
	uint64_t shares;
	int zerr;
	int cleanupfd;
	int outputfd;
	int datafd;
} differ_info_t;

/*
 * Given a {dsname, object id}, get the object path
 */
static int
get_stats_for_obj(differ_info_t *di, const char *dsname, uint64_t obj,
    char *pn, int maxlen, zfs_stat_t *sb)
{
	zfs_cmd_t zc = { 0 };
	int error;

	(void) strlcpy(zc.zc_name, dsname, sizeof (zc.zc_name));
	zc.zc_obj = obj;

	errno = 0;
	error = ioctl(di->zhp->zfs_hdl->libzfs_fd, ZFS_IOC_OBJ_TO_STATS, &zc);
	di->zerr = errno;

	/* we can get stats even if we failed to get a path */
	(void) memcpy(sb, &zc.zc_stat, sizeof (zfs_stat_t));
	if (error == 0) {
		ASSERT(di->zerr == 0);
		(void) strlcpy(pn, zc.zc_value, maxlen);
		return (0);
	}

	if (di->zerr == ESTALE) {
		(void) snprintf(pn, maxlen, "(on_delete_queue)");
		return (0);
	} else if (di->zerr == EPERM) {
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "The sys_config privilege or diff delegated permission "
		    "is needed\nto discover path names"));
		return (-1);
	} else {
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Unable to determine path or stats for "
		    "object %lld in %s"), obj, dsname);
		return (-1);
	}
}

/*
 * stream_bytes
 *
 * Prints a file name out a character at a time.  If the character is
 * not in the range of what we consider "printable" ASCII, display it
 * as an escaped 3-digit octal value.  ASCII values less than a space
 * are all control characters and we declare the upper end as the
 * DELete character.  This also is the last 7-bit ASCII character.
 * We choose to treat all 8-bit ASCII as not printable for this
 * application.
 */
static void
stream_bytes(FILE *fp, const char *string)
{
	char c;

	while ((c = *string++) != '\0') {
		if (c > ' ' && c != '\\' && c < '\177') {
			(void) fprintf(fp, "%c", c);
		} else {
			(void) fprintf(fp, "\\%03o", (uint8_t)c);
		}
	}
}

static void
print_what(FILE *fp, mode_t what)
{
	char symbol;

	switch (what & S_IFMT) {
	case S_IFBLK:
		symbol = 'B';
		break;
	case S_IFCHR:
		symbol = 'C';
		break;
	case S_IFDIR:
		symbol = '/';
		break;
#ifdef S_IFDOOR
	case S_IFDOOR:
		symbol = '>';
		break;
#endif
	case S_IFIFO:
		symbol = '|';
		break;
	case S_IFLNK:
		symbol = '@';
		break;
#ifdef S_IFPORT
	case S_IFPORT:
		symbol = 'P';
		break;
#endif
	case S_IFSOCK:
		symbol = '=';
		break;
	case S_IFREG:
		symbol = 'F';
		break;
	default:
		symbol = '?';
		break;
	}
	(void) fprintf(fp, "%c", symbol);
}

static void
print_cmn(FILE *fp, differ_info_t *di, const char *file)
{
	stream_bytes(fp, di->dsmnt);
	stream_bytes(fp, file);
}

static void
print_rename(FILE *fp, differ_info_t *di, const char *old, const char *new,
    zfs_stat_t *isb)
{
	if (di->timestamped)
		(void) fprintf(fp, "%10lld.%09lld\t",
		    (longlong_t)isb->zs_ctime[0],
		    (longlong_t)isb->zs_ctime[1]);
	(void) fprintf(fp, "%c\t", ZDIFF_RENAMED);
	if (di->classify) {
		print_what(fp, isb->zs_mode);
		(void) fprintf(fp, "\t");
	}
	print_cmn(fp, di, old);
	if (di->scripted)
		(void) fprintf(fp, "\t");
	else
		(void) fprintf(fp, " -> ");
	print_cmn(fp, di, new);
	(void) fprintf(fp, "\n");
}

static void
print_link_change(FILE *fp, differ_info_t *di, int delta, const char *file,
    zfs_stat_t *isb)
{
	if (di->timestamped)
		(void) fprintf(fp, "%10lld.%09lld\t",
		    (longlong_t)isb->zs_ctime[0],
		    (longlong_t)isb->zs_ctime[1]);
	(void) fprintf(fp, "%c\t", ZDIFF_MODIFIED);
	if (di->classify) {
		print_what(fp, isb->zs_mode);
		(void) fprintf(fp, "\t");
	}
	print_cmn(fp, di, file);
	(void) fprintf(fp, "\t(%+d)", delta);
	(void) fprintf(fp, "\n");
}

static void
print_file(FILE *fp, differ_info_t *di, char type, const char *file,
    zfs_stat_t *isb)
{
	if (di->timestamped)
		(void) fprintf(fp, "%10lld.%09lld\t",
		    (longlong_t)isb->zs_ctime[0],
		    (longlong_t)isb->zs_ctime[1]);
	(void) fprintf(fp, "%c\t", type);
	if (di->classify) {
		print_what(fp, isb->zs_mode);
		(void) fprintf(fp, "\t");
	}
	print_cmn(fp, di, file);
	(void) fprintf(fp, "\n");
}

static int
write_inuse_diffs_one(FILE *fp, differ_info_t *di, uint64_t dobj)
{
	struct zfs_stat fsb, tsb;
	mode_t fmode, tmode;
	char fobjname[MAXPATHLEN], tobjname[MAXPATHLEN];
	int fobjerr, tobjerr;
	int change;

	if (dobj == di->shares)
		return (0);

	/*
	 * Check the from and to snapshots for info on the object. If
	 * we get ENOENT, then the object just didn't exist in that
	 * snapshot.  If we get ENOTSUP, then we tried to get
	 * info on a non-ZPL object, which we don't care about anyway.
	 */
	fobjerr = get_stats_for_obj(di, di->fromsnap, dobj, fobjname,
	    MAXPATHLEN, &fsb);
	if (fobjerr && di->zerr != ENOENT && di->zerr != ENOTSUP)
		return (-1);

	tobjerr = get_stats_for_obj(di, di->tosnap, dobj, tobjname,
	    MAXPATHLEN, &tsb);
	if (tobjerr && di->zerr != ENOENT && di->zerr != ENOTSUP)
		return (-1);

	/*
	 * Unallocated object sharing the same meta dnode block
	 */
	if (fobjerr && tobjerr) {
		ASSERT(di->zerr == ENOENT || di->zerr == ENOTSUP);
		di->zerr = 0;
		return (0);
	}

	di->zerr = 0; /* negate get_stats_for_obj() from side that failed */
	fmode = fsb.zs_mode & S_IFMT;
	tmode = tsb.zs_mode & S_IFMT;
	if (fmode == S_IFDIR || tmode == S_IFDIR || fsb.zs_links == 0 ||
	    tsb.zs_links == 0)
		change = 0;
	else
		change = tsb.zs_links - fsb.zs_links;

	if (fobjerr) {
		if (change) {
			print_link_change(fp, di, change, tobjname, &tsb);
			return (0);
		}
		print_file(fp, di, ZDIFF_ADDED, tobjname, &tsb);
		return (0);
	} else if (tobjerr) {
		if (change) {
			print_link_change(fp, di, change, fobjname, &fsb);
			return (0);
		}
		print_file(fp, di, ZDIFF_REMOVED, fobjname, &fsb);
		return (0);
	}

	if (fmode != tmode && fsb.zs_gen == tsb.zs_gen)
		tsb.zs_gen++;	/* Force a generational difference */

	/* Simple modification or no change */
	if (fsb.zs_gen == tsb.zs_gen) {
		/* No apparent changes.  Could we assert !this?  */
		if (fsb.zs_ctime[0] == tsb.zs_ctime[0] &&
		    fsb.zs_ctime[1] == tsb.zs_ctime[1])
			return (0);
		if (change) {
			print_link_change(fp, di, change,
			    change > 0 ? fobjname : tobjname, &tsb);
		} else if (strcmp(fobjname, tobjname) == 0) {
			print_file(fp, di, ZDIFF_MODIFIED, fobjname, &tsb);
		} else {
			print_rename(fp, di, fobjname, tobjname, &tsb);
		}
		return (0);
	} else {
		/* file re-created or object re-used */
		print_file(fp, di, ZDIFF_REMOVED, fobjname, &fsb);
		print_file(fp, di, ZDIFF_ADDED, tobjname, &tsb);
		return (0);
	}
}

static int
write_inuse_diffs(FILE *fp, differ_info_t *di, dmu_diff_record_t *dr)
{
	uint64_t o;
	int err;

	for (o = dr->ddr_first; o <= dr->ddr_last; o++) {
		if ((err = write_inuse_diffs_one(fp, di, o)) != 0)
			return (err);
	}
	return (0);
}

static int
describe_free(FILE *fp, differ_info_t *di, uint64_t object, char *namebuf,
    int maxlen)
{
	struct zfs_stat sb;

	if (get_stats_for_obj(di, di->fromsnap, object, namebuf,
	    maxlen, &sb) != 0) {
		/* Let it slide, if in the delete queue on from side */
		if (di->zerr == ENOENT && sb.zs_links == 0) {
			di->zerr = 0;
			return (0);
		}
		return (-1);
	}

	print_file(fp, di, ZDIFF_REMOVED, namebuf, &sb);
	return (0);
}

static int
write_free_diffs(FILE *fp, differ_info_t *di, dmu_diff_record_t *dr)
{
	zfs_cmd_t zc = { 0 };
	libzfs_handle_t *lhdl = di->zhp->zfs_hdl;
	char fobjname[MAXPATHLEN];

	(void) strlcpy(zc.zc_name, di->fromsnap, sizeof (zc.zc_name));
	zc.zc_obj = dr->ddr_first - 1;

	ASSERT(di->zerr == 0);

	while (zc.zc_obj < dr->ddr_last) {
		int err;

		err = ioctl(lhdl->libzfs_fd, ZFS_IOC_NEXT_OBJ, &zc);
		if (err == 0) {
			if (zc.zc_obj == di->shares) {
				zc.zc_obj++;
				continue;
			}
			if (zc.zc_obj > dr->ddr_last) {
				break;
			}
			err = describe_free(fp, di, zc.zc_obj, fobjname,
			    MAXPATHLEN);
			if (err)
				break;
		} else if (errno == ESRCH) {
			break;
		} else {
			(void) snprintf(di->errbuf, sizeof (di->errbuf),
			    dgettext(TEXT_DOMAIN,
			    "next allocated object (> %lld) find failure"),
			    zc.zc_obj);
			di->zerr = errno;
			break;
		}
	}
	if (di->zerr)
		return (-1);
	return (0);
}

static void *
differ(void *arg)
{
	differ_info_t *di = arg;
	dmu_diff_record_t dr;
	FILE *ofp;
	int err = 0;

	if ((ofp = fdopen(di->outputfd, "w")) == NULL) {
		di->zerr = errno;
		(void) strerror_r(errno, di->errbuf, sizeof (di->errbuf));
		(void) close(di->datafd);
		return ((void *)-1);
	}

	for (;;) {
		char *cp = (char *)&dr;
		int len = sizeof (dr);
		int rv;

		do {
			rv = read(di->datafd, cp, len);
			cp += rv;
			len -= rv;
		} while (len > 0 && rv > 0);

		if (rv < 0 || (rv == 0 && len != sizeof (dr))) {
			di->zerr = EPIPE;
			break;
		} else if (rv == 0) {
			/* end of file at a natural breaking point */
			break;
		}

		switch (dr.ddr_type) {
		case DDR_FREE:
			err = write_free_diffs(ofp, di, &dr);
			break;
		case DDR_INUSE:
			err = write_inuse_diffs(ofp, di, &dr);
			break;
		default:
			di->zerr = EPIPE;
			break;
		}

		if (err || di->zerr)
			break;
	}

	(void) fclose(ofp);
	(void) close(di->datafd);
	if (err)
		return ((void *)-1);
	if (di->zerr) {
		ASSERT(di->zerr == EINVAL);
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Internal error: bad data from diff IOCTL"));
		return ((void *)-1);
	}
	return ((void *)0);
}

static int
find_shares_object(differ_info_t *di)
{
	char fullpath[MAXPATHLEN];
	struct stat64 sb = { 0 };

	(void) strlcpy(fullpath, di->dsmnt, MAXPATHLEN);
	(void) strlcat(fullpath, ZDIFF_SHARESDIR, MAXPATHLEN);

	if (stat64(fullpath, &sb) != 0) {
#ifdef illumos
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN, "Cannot stat %s"), fullpath);
		return (zfs_error(di->zhp->zfs_hdl, EZFS_DIFF, di->errbuf));
#else
		return (0);
#endif
	}

	di->shares = (uint64_t)sb.st_ino;
	return (0);
}

static int
make_temp_snapshot(differ_info_t *di)
{
	libzfs_handle_t *hdl = di->zhp->zfs_hdl;
	zfs_cmd_t zc = { 0 };

	(void) snprintf(zc.zc_value, sizeof (zc.zc_value),
	    ZDIFF_PREFIX, getpid());
	(void) strlcpy(zc.zc_name, di->ds, sizeof (zc.zc_name));
	zc.zc_cleanup_fd = di->cleanupfd;

	if (ioctl(hdl->libzfs_fd, ZFS_IOC_TMP_SNAPSHOT, &zc) != 0) {
		int err = errno;
		if (err == EPERM) {
			(void) snprintf(di->errbuf, sizeof (di->errbuf),
			    dgettext(TEXT_DOMAIN, "The diff delegated "
			    "permission is needed in order\nto create a "
			    "just-in-time snapshot for diffing\n"));
			return (zfs_error(hdl, EZFS_DIFF, di->errbuf));
		} else {
			(void) snprintf(di->errbuf, sizeof (di->errbuf),
			    dgettext(TEXT_DOMAIN, "Cannot create just-in-time "
			    "snapshot of '%s'"), zc.zc_name);
			return (zfs_standard_error(hdl, err, di->errbuf));
		}
	}

	di->tmpsnap = zfs_strdup(hdl, zc.zc_value);
	di->tosnap = zfs_asprintf(hdl, "%s@%s", di->ds, di->tmpsnap);
	return (0);
}

static void
teardown_differ_info(differ_info_t *di)
{
	free(di->ds);
	free(di->dsmnt);
	free(di->fromsnap);
	free(di->frommnt);
	free(di->tosnap);
	free(di->tmpsnap);
	free(di->tomnt);
	(void) close(di->cleanupfd);
}

static int
get_snapshot_names(differ_info_t *di, const char *fromsnap,
    const char *tosnap)
{
	libzfs_handle_t *hdl = di->zhp->zfs_hdl;
	char *atptrf = NULL;
	char *atptrt = NULL;
	int fdslen, fsnlen;
	int tdslen, tsnlen;

	/*
	 * Can accept
	 *    dataset@snap1
	 *    dataset@snap1 dataset@snap2
	 *    dataset@snap1 @snap2
	 *    dataset@snap1 dataset
	 *    @snap1 dataset@snap2
	 */
	if (tosnap == NULL) {
		/* only a from snapshot given, must be valid */
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Badly formed snapshot name %s"), fromsnap);

		if (!zfs_validate_name(hdl, fromsnap, ZFS_TYPE_SNAPSHOT,
		    B_FALSE)) {
			return (zfs_error(hdl, EZFS_INVALIDNAME,
			    di->errbuf));
		}

		atptrf = strchr(fromsnap, '@');
		ASSERT(atptrf != NULL);
		fdslen = atptrf - fromsnap;

		di->fromsnap = zfs_strdup(hdl, fromsnap);
		di->ds = zfs_strdup(hdl, fromsnap);
		di->ds[fdslen] = '\0';

		/* the to snap will be a just-in-time snap of the head */
		return (make_temp_snapshot(di));
	}

	(void) snprintf(di->errbuf, sizeof (di->errbuf),
	    dgettext(TEXT_DOMAIN,
	    "Unable to determine which snapshots to compare"));

	atptrf = strchr(fromsnap, '@');
	atptrt = strchr(tosnap, '@');
	fdslen = atptrf ? atptrf - fromsnap : strlen(fromsnap);
	tdslen = atptrt ? atptrt - tosnap : strlen(tosnap);
	fsnlen = strlen(fromsnap) - fdslen;	/* includes @ sign */
	tsnlen = strlen(tosnap) - tdslen;	/* includes @ sign */

	if (fsnlen <= 1 || tsnlen == 1 || (fdslen == 0 && tdslen == 0) ||
	    (fsnlen == 0 && tsnlen == 0)) {
		return (zfs_error(hdl, EZFS_INVALIDNAME, di->errbuf));
	} else if ((fdslen > 0 && tdslen > 0) &&
	    ((tdslen != fdslen || strncmp(fromsnap, tosnap, fdslen) != 0))) {
		/*
		 * not the same dataset name, might be okay if
		 * tosnap is a clone of a fromsnap descendant.
		 */
		char origin[ZFS_MAX_DATASET_NAME_LEN];
		zprop_source_t src;
		zfs_handle_t *zhp;

		di->ds = zfs_alloc(di->zhp->zfs_hdl, tdslen + 1);
		(void) strncpy(di->ds, tosnap, tdslen);
		di->ds[tdslen] = '\0';

		zhp = zfs_open(hdl, di->ds, ZFS_TYPE_FILESYSTEM);
		while (zhp != NULL) {
			if (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin,
			    sizeof (origin), &src, NULL, 0, B_FALSE) != 0) {
				(void) zfs_close(zhp);
				zhp = NULL;
				break;
			}
			if (strncmp(origin, fromsnap, fsnlen) == 0)
				break;

			(void) zfs_close(zhp);
			zhp = zfs_open(hdl, origin, ZFS_TYPE_FILESYSTEM);
		}

		if (zhp == NULL) {
			(void) snprintf(di->errbuf, sizeof (di->errbuf),
			    dgettext(TEXT_DOMAIN,
			    "Not an earlier snapshot from the same fs"));
			return (zfs_error(hdl, EZFS_INVALIDNAME, di->errbuf));
		} else {
			(void) zfs_close(zhp);
		}

		di->isclone = B_TRUE;
		di->fromsnap = zfs_strdup(hdl, fromsnap);
		if (tsnlen) {
			di->tosnap = zfs_strdup(hdl, tosnap);
		} else {
			return (make_temp_snapshot(di));
		}
	} else {
		int dslen = fdslen ? fdslen : tdslen;

		di->ds = zfs_alloc(hdl, dslen + 1);
		(void) strncpy(di->ds, fdslen ? fromsnap : tosnap, dslen);
		di->ds[dslen] = '\0';

		di->fromsnap = zfs_asprintf(hdl, "%s%s", di->ds, atptrf);
		if (tsnlen) {
			di->tosnap = zfs_asprintf(hdl, "%s%s", di->ds, atptrt);
		} else {
			return (make_temp_snapshot(di));
		}
	}
	return (0);
}

static int
get_mountpoint(differ_info_t *di, char *dsnm, char **mntpt)
{
	boolean_t mounted;

	mounted = is_mounted(di->zhp->zfs_hdl, dsnm, mntpt);
	if (mounted == B_FALSE) {
		(void) snprintf(di->errbuf, sizeof (di->errbuf),
		    dgettext(TEXT_DOMAIN,
		    "Cannot diff an unmounted snapshot"));
		return (zfs_error(di->zhp->zfs_hdl, EZFS_BADTYPE, di->errbuf));
	}

	/* Avoid a double slash at the beginning of root-mounted datasets */
	if (**mntpt == '/' && *(*mntpt + 1) == '\0')
		**mntpt = '\0';
	return (0);
}

static int
get_mountpoints(differ_info_t *di)
{
	char *strptr;
	char *frommntpt;

	/*
	 * first get the mountpoint for the parent dataset
	 */
	if (get_mountpoint(di, di->ds, &di->dsmnt) != 0)
		return (-1);

	strptr = strchr(di->tosnap, '@');
	ASSERT3P(strptr, !=, NULL);
	di->tomnt = zfs_asprintf(di->zhp->zfs_hdl, "%s%s%s", di->dsmnt,
	    ZDIFF_SNAPDIR, ++strptr);

	strptr = strchr(di->fromsnap, '@');
	ASSERT3P(strptr, !=, NULL);

	frommntpt = di->dsmnt;
	if (di->isclone) {
		char *mntpt;
		int err;

		*strptr = '\0';
		err = get_mountpoint(di, di->fromsnap, &mntpt);
		*strptr = '@';
		if (err != 0)
			return (-1);
		frommntpt = mntpt;
	}

	di->frommnt = zfs_asprintf(di->zhp->zfs_hdl, "%s%s%s", frommntpt,
	    ZDIFF_SNAPDIR, ++strptr);

	if (di->isclone)
		free(frommntpt);

	return (0);
}

static int
setup_differ_info(zfs_handle_t *zhp, const char *fromsnap,
    const char *tosnap, differ_info_t *di)
{
	di->zhp = zhp;

	di->cleanupfd = open(ZFS_DEV, O_RDWR|O_EXCL);
	VERIFY(di->cleanupfd >= 0);

	if (get_snapshot_names(di, fromsnap, tosnap) != 0)
		return (-1);

	if (get_mountpoints(di) != 0)
		return (-1);

	if (find_shares_object(di) != 0)
		return (-1);

	return (0);
}

int
zfs_show_diffs(zfs_handle_t *zhp, int outfd, const char *fromsnap,
    const char *tosnap, int flags)
{
	zfs_cmd_t zc = { 0 };
	char errbuf[1024];
	differ_info_t di = { 0 };
	pthread_t tid;
	int pipefd[2];
	int iocerr;

	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "zfs diff failed"));

	if (setup_differ_info(zhp, fromsnap, tosnap, &di)) {
		teardown_differ_info(&di);
		return (-1);
	}

	if (pipe(pipefd)) {
		zfs_error_aux(zhp->zfs_hdl, strerror(errno));
		teardown_differ_info(&di);
		return (zfs_error(zhp->zfs_hdl, EZFS_PIPEFAILED, errbuf));
	}

	di.scripted = (flags & ZFS_DIFF_PARSEABLE);
	di.classify = (flags & ZFS_DIFF_CLASSIFY);
	di.timestamped = (flags & ZFS_DIFF_TIMESTAMP);

	di.outputfd = outfd;
	di.datafd = pipefd[0];

	if (pthread_create(&tid, NULL, differ, &di)) {
		zfs_error_aux(zhp->zfs_hdl, strerror(errno));
		(void) close(pipefd[0]);
		(void) close(pipefd[1]);
		teardown_differ_info(&di);
		return (zfs_error(zhp->zfs_hdl,
		    EZFS_THREADCREATEFAILED, errbuf));
	}

	/* do the ioctl() */
	(void) strlcpy(zc.zc_value, di.fromsnap, strlen(di.fromsnap) + 1);
	(void) strlcpy(zc.zc_name, di.tosnap, strlen(di.tosnap) + 1);
	zc.zc_cookie = pipefd[1];

	iocerr = ioctl(zhp->zfs_hdl->libzfs_fd, ZFS_IOC_DIFF, &zc);
	if (iocerr != 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN, "Unable to obtain diffs"));
		if (errno == EPERM) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "\n   The sys_mount privilege or diff delegated "
			    "permission is needed\n   to execute the "
			    "diff ioctl"));
		} else if (errno == EXDEV) {
			zfs_error_aux(zhp->zfs_hdl, dgettext(TEXT_DOMAIN,
			    "\n   Not an earlier snapshot from the same fs"));
		} else if (errno != EPIPE || di.zerr == 0) {
			zfs_error_aux(zhp->zfs_hdl, strerror(errno));
		}
		(void) close(pipefd[1]);
		(void) pthread_cancel(tid);
		(void) pthread_join(tid, NULL);
		teardown_differ_info(&di);
		if (di.zerr != 0 && di.zerr != EPIPE) {
			zfs_error_aux(zhp->zfs_hdl, strerror(di.zerr));
			return (zfs_error(zhp->zfs_hdl, EZFS_DIFF, di.errbuf));
		} else {
			return (zfs_error(zhp->zfs_hdl, EZFS_DIFFDATA, errbuf));
		}
	}

	(void) close(pipefd[1]);
	(void) pthread_join(tid, NULL);

	if (di.zerr != 0) {
		zfs_error_aux(zhp->zfs_hdl, strerror(di.zerr));
		return (zfs_error(zhp->zfs_hdl, EZFS_DIFF, di.errbuf));
	}
	teardown_differ_info(&di);
	return (0);
}
