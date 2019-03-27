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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is a test program that uses ioctls to the ZFS Unit Test driver
 * to perform readdirs or lookups using flags not normally available
 * to user-land programs.  This allows testing of the flags'
 * behavior outside of a complicated consumer, such as the SMB driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stropts.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/dirent.h>
#include <sys/attr.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#define	_KERNEL

#include <sys/fs/zut.h>
#include <sys/extdirent.h>

#undef	_KERNEL

#define	MAXBUF (64 * 1024)
#define	BIGBUF 4096
#define	LILBUF (sizeof (dirent_t))

#define	DIRENT_NAMELEN(reclen)	\
	((reclen) - (offsetof(dirent_t, d_name[0])))

static void
usage(char *pnam)
{
	(void) fprintf(stderr, "Usage:\n    %s -l [-is] dir-to-look-in "
	    "file-in-dir [xfile-on-file]\n", pnam);
	(void) fprintf(stderr, "    %s -i [-ls] dir-to-look-in "
	    "file-in-dir [xfile-on-file]\n", pnam);
	(void) fprintf(stderr, "    %s -s [-il] dir-to-look-in "
	    "file-in-dir [xfile-on-file]\n", pnam);
	(void) fprintf(stderr, "\t    Perform a lookup\n");
	(void) fprintf(stderr, "\t    -l == lookup\n");
	(void) fprintf(stderr, "\t    -i == request FIGNORECASE\n");
	(void) fprintf(stderr, "\t    -s == request stat(2) and xvattr info\n");
	(void) fprintf(stderr, "    %s -r [-ea] [-b buffer-size-in-bytes] "
	    "dir-to-look-in [file-in-dir]\n", pnam);
	(void) fprintf(stderr, "    %s -e [-ra] [-b buffer-size-in-bytes] "
	    "dir-to-look-in [file-in-dir]\n", pnam);
	(void) fprintf(stderr, "    %s -a [-re] [-b buffer-size-in-bytes] "
	    "dir-to-look-in [file-in-dir]\n", pnam);
	(void) fprintf(stderr, "\t    Perform a readdir\n");
	(void) fprintf(stderr, "\t    -r == readdir\n");
	(void) fprintf(stderr, "\t    -e == request extended entries\n");
	(void) fprintf(stderr, "\t    -a == request access filtering\n");
	(void) fprintf(stderr, "\t    -b == buffer size (default 4K)\n");
	(void) fprintf(stderr, "    %s -A path\n", pnam);
	(void) fprintf(stderr, "\t    Look up _PC_ACCESS_FILTERING "
	    "for path with pathconf(2)\n");
	(void) fprintf(stderr, "    %s -E path\n", pnam);
	(void) fprintf(stderr, "\t    Look up _PC_SATTR_EXISTS "
	    "for path with pathconf(2)\n");
	(void) fprintf(stderr, "    %s -S path\n", pnam);
	(void) fprintf(stderr, "\t    Look up _PC_SATTR_EXISTS "
	    "for path with pathconf(2)\n");
	exit(EINVAL);
}

static void
print_extd_entries(zut_readdir_t *r)
{
	struct edirent *eodp;
	char *bufstart;

	eodp = (edirent_t *)(uintptr_t)r->zr_buf;
	bufstart = (char *)eodp;
	while ((char *)eodp < bufstart + r->zr_bytes) {
		char *blanks = "                ";
		int i = 0;
		while (i < EDIRENT_NAMELEN(eodp->ed_reclen)) {
			if (!eodp->ed_name[i])
				break;
			(void) printf("%c", eodp->ed_name[i++]);
		}
		if (i < 16)
			(void) printf("%.*s", 16 - i, blanks);
		(void) printf("\t%x\n", eodp->ed_eflags);
		eodp = (edirent_t *)((intptr_t)eodp + eodp->ed_reclen);
	}
}

static void
print_entries(zut_readdir_t *r)
{
	dirent64_t *dp;
	char *bufstart;

	dp = (dirent64_t *)(intptr_t)r->zr_buf;
	bufstart = (char *)dp;
	while ((char *)dp < bufstart + r->zr_bytes) {
		int i = 0;
		while (i < DIRENT_NAMELEN(dp->d_reclen)) {
			if (!dp->d_name[i])
				break;
			(void) printf("%c", dp->d_name[i++]);
		}
		(void) printf("\n");
		dp = (dirent64_t *)((intptr_t)dp + dp->d_reclen);
	}
}

static void
print_stats(struct stat64 *sb)
{
	char timebuf[512];

	(void) printf("st_mode\t\t\t%04lo\n", (unsigned long)sb->st_mode);
	(void) printf("st_ino\t\t\t%llu\n", (unsigned long long)sb->st_ino);
	(void) printf("st_nlink\t\t%lu\n", (unsigned long)sb->st_nlink);
	(void) printf("st_uid\t\t\t%d\n", sb->st_uid);
	(void) printf("st_gid\t\t\t%d\n", sb->st_gid);
	(void) printf("st_size\t\t\t%lld\n", (long long)sb->st_size);
	(void) printf("st_blksize\t\t%ld\n", (long)sb->st_blksize);
	(void) printf("st_blocks\t\t%lld\n", (long long)sb->st_blocks);

	timebuf[0] = 0;
	if (ctime_r(&sb->st_atime, timebuf, 512)) {
		(void) printf("st_atime\t\t");
		(void) printf("%s", timebuf);
	}
	timebuf[0] = 0;
	if (ctime_r(&sb->st_mtime, timebuf, 512)) {
		(void) printf("st_mtime\t\t");
		(void) printf("%s", timebuf);
	}
	timebuf[0] = 0;
	if (ctime_r(&sb->st_ctime, timebuf, 512)) {
		(void) printf("st_ctime\t\t");
		(void) printf("%s", timebuf);
	}
}

static void
print_xvs(uint64_t xvs)
{
	uint_t bits;
	int idx = 0;

	if (xvs == 0)
		return;

	(void) printf("-------------------\n");
	(void) printf("Attribute bit(s) set:\n");
	(void) printf("-------------------\n");

	bits = xvs & ((1 << F_ATTR_ALL) - 1);
	while (bits) {
		uint_t rest = bits >> 1;
		if (bits & 1) {
			(void) printf("%s", attr_to_name((f_attr_t)idx));
			if (rest)
				(void) printf(", ");
		}
		idx++;
		bits = rest;
	}
	(void) printf("\n");
}

int
main(int argc, char **argv)
{
	zut_lookup_t lk = {0};
	zut_readdir_t rd = {0};
	boolean_t checking = B_FALSE;
	boolean_t looking = B_FALSE;
	boolean_t reading = B_FALSE;
	boolean_t bflag = B_FALSE;
	long rddir_bufsize = BIGBUF;
	int error = 0;
	int check;
	int fd;
	int c;

	while ((c = getopt(argc, argv, "lisaerb:ASE")) != -1) {
		switch (c) {
		case 'l':
			looking = B_TRUE;
			break;
		case 'i':
			lk.zl_reqflags |= ZUT_IGNORECASE;
			looking = B_TRUE;
			break;
		case 's':
			lk.zl_reqflags |= ZUT_GETSTAT;
			looking = B_TRUE;
			break;
		case 'a':
			rd.zr_reqflags |= ZUT_ACCFILTER;
			reading = B_TRUE;
			break;
		case 'e':
			rd.zr_reqflags |= ZUT_EXTRDDIR;
			reading = B_TRUE;
			break;
		case 'r':
			reading = B_TRUE;
			break;
		case 'b':
			reading = B_TRUE;
			bflag = B_TRUE;
			rddir_bufsize = strtol(optarg, NULL, 0);
			break;
		case 'A':
			checking = B_TRUE;
			check = _PC_ACCESS_FILTERING;
			break;
		case 'S':
			checking = B_TRUE;
			check = _PC_SATTR_ENABLED;
			break;
		case 'E':
			checking = B_TRUE;
			check = _PC_SATTR_EXISTS;
			break;
		case '?':
		default:
			usage(argv[0]);		/* no return */
		}
	}

	if ((checking && looking) || (checking && reading) ||
	    (looking && reading) || (!reading && bflag) ||
	    (!checking && !reading && !looking))
		usage(argv[0]);		/* no return */

	if (rddir_bufsize < LILBUF || rddir_bufsize > MAXBUF) {
		(void) fprintf(stderr, "Sorry, buffer size "
		    "must be >= %d and less than or equal to %d bytes.\n",
		    (int)LILBUF, MAXBUF);
		exit(EINVAL);
	}

	if (checking) {
		char pathbuf[MAXPATHLEN];
		long result;

		if (argc - optind < 1)
			usage(argv[0]);		/* no return */
		(void) strlcpy(pathbuf, argv[optind], MAXPATHLEN);
		result = pathconf(pathbuf, check);
		(void) printf("pathconf(2) check for %s\n", pathbuf);
		switch (check) {
		case _PC_SATTR_ENABLED:
			(void) printf("System attributes ");
			if (result != 0)
				(void) printf("Enabled\n");
			else
				(void) printf("Not enabled\n");
			break;
		case _PC_SATTR_EXISTS:
			(void) printf("System attributes ");
			if (result != 0)
				(void) printf("Exist\n");
			else
				(void) printf("Do not exist\n");
			break;
		case _PC_ACCESS_FILTERING:
			(void) printf("Access filtering ");
			if (result != 0)
				(void) printf("Available\n");
			else
				(void) printf("Not available\n");
			break;
		}
		return (result);
	}

	if ((fd = open(ZUT_DEV, O_RDONLY)) < 0) {
		perror(ZUT_DEV);
		return (ENXIO);
	}

	if (reading) {
		char *buf;

		if (argc - optind < 1)
			usage(argv[0]);		/* no return */

		(void) strlcpy(rd.zr_dir, argv[optind], MAXPATHLEN);
		if (argc - optind > 1) {
			(void) strlcpy(rd.zr_file, argv[optind + 1],
			    MAXNAMELEN);
			rd.zr_reqflags |= ZUT_XATTR;
		}

		if ((buf = malloc(rddir_bufsize)) == NULL) {
			error = errno;
			perror("malloc");
			(void) close(fd);
			return (error);
		}

		rd.zr_buf = (uint64_t)(uintptr_t)buf;
		rd.zr_buflen = rddir_bufsize;

		while (!rd.zr_eof) {
			int ierr;

			if ((ierr = ioctl(fd, ZUT_IOC_READDIR, &rd)) != 0) {
				(void) fprintf(stderr,
				    "IOCTL error: %s (%d)\n",
				    strerror(ierr), ierr);
				free(buf);
				(void) close(fd);
				return (ierr);
			}
			if (rd.zr_retcode) {
				(void) fprintf(stderr,
				    "readdir result: %s (%d)\n",
				    strerror(rd.zr_retcode), rd.zr_retcode);
				free(buf);
				(void) close(fd);
				return (rd.zr_retcode);
			}
			if (rd.zr_reqflags & ZUT_EXTRDDIR)
				print_extd_entries(&rd);
			else
				print_entries(&rd);
		}
		free(buf);
	} else {
		int ierr;

		if (argc - optind < 2)
			usage(argv[0]);		/* no return */

		(void) strlcpy(lk.zl_dir, argv[optind], MAXPATHLEN);
		(void) strlcpy(lk.zl_file, argv[optind + 1], MAXNAMELEN);
		if (argc - optind > 2) {
			(void) strlcpy(lk.zl_xfile,
			    argv[optind + 2], MAXNAMELEN);
			lk.zl_reqflags |= ZUT_XATTR;
		}

		if ((ierr = ioctl(fd, ZUT_IOC_LOOKUP, &lk)) != 0) {
			(void) fprintf(stderr,
			    "IOCTL error: %s (%d)\n",
			    strerror(ierr), ierr);
			(void) close(fd);
			return (ierr);
		}

		(void) printf("\nLookup of ");
		if (lk.zl_reqflags & ZUT_XATTR) {
			(void) printf("extended attribute \"%s\" of ",
			    lk.zl_xfile);
		}
		(void) printf("file \"%s\" ", lk.zl_file);
		(void) printf("in directory \"%s\" ", lk.zl_dir);
		if (lk.zl_retcode) {
			(void) printf("failed: %s (%d)\n",
			    strerror(lk.zl_retcode), lk.zl_retcode);
			(void) close(fd);
			return (lk.zl_retcode);
		}

		(void) printf("succeeded.\n");
		if (lk.zl_reqflags & ZUT_IGNORECASE) {
			(void) printf("----------------------------\n");
			(void) printf("dirent flags: 0x%0x\n", lk.zl_deflags);
			(void) printf("real name: %s\n", lk.zl_real);
		}
		if (lk.zl_reqflags & ZUT_GETSTAT) {
			(void) printf("----------------------------\n");
			print_stats(&lk.zl_statbuf);
			print_xvs(lk.zl_xvattrs);
		}
	}

	(void) close(fd);
	return (0);
}
