/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2005 by Rob Landley <rob@landley.net>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

/* For 2.6, use the cleaned up header to get the 64 bit API. */
// Commented out per Rob's request
//# include "fix_u32.h" /* some old toolchains need __u64 for linux/loop.h */
# include <linux/loop.h>
typedef struct loop_info64 bb_loop_info;
# define BB_LOOP_SET_STATUS LOOP_SET_STATUS64
# define BB_LOOP_GET_STATUS LOOP_GET_STATUS64

#else

/* For 2.4 and earlier, use the 32 bit API (and don't trust the headers) */
/* Stuff stolen from linux/loop.h for 2.4 and earlier kernels */
# include <linux/posix_types.h>
# define LO_NAME_SIZE        64
# define LO_KEY_SIZE         32
# define LOOP_SET_FD         0x4C00
# define LOOP_CLR_FD         0x4C01
# define BB_LOOP_SET_STATUS  0x4C02
# define BB_LOOP_GET_STATUS  0x4C03
typedef struct {
	int                lo_number;
	__kernel_dev_t     lo_device;
	unsigned long      lo_inode;
	__kernel_dev_t     lo_rdevice;
	int                lo_offset;
	int                lo_encrypt_type;
	int                lo_encrypt_key_size;
	int                lo_flags;
	char               lo_file_name[LO_NAME_SIZE];
	unsigned char      lo_encrypt_key[LO_KEY_SIZE];
	unsigned long      lo_init[2];
	char               reserved[4];
} bb_loop_info;
#endif

char* FAST_FUNC query_loop(const char *device)
{
	int fd;
	bb_loop_info loopinfo;
	char *dev = NULL;

	fd = open(device, O_RDONLY);
	if (fd >= 0) {
		if (ioctl(fd, BB_LOOP_GET_STATUS, &loopinfo) == 0) {
			dev = xasprintf("%"OFF_FMT"u %s", (off_t) loopinfo.lo_offset,
					(char *)loopinfo.lo_file_name);
		}
		close(fd);
	}

	return dev;
}

int FAST_FUNC del_loop(const char *device)
{
	int fd, rc;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return 1;
	rc = ioctl(fd, LOOP_CLR_FD, 0);
	close(fd);

	return rc;
}

/* Returns opened fd to the loop device, <0 on error.
 * *device is loop device to use, or if *device==NULL finds a loop device to
 * mount it on and sets *device to a strdup of that loop device name.  This
 * search will re-use an existing loop device already bound to that
 * file/offset if it finds one.
 */
int FAST_FUNC set_loop(char **device, const char *file, unsigned long long offset, unsigned flags)
{
	char dev[LOOP_NAMESIZE];
	char *try;
	bb_loop_info loopinfo;
	struct stat statbuf;
	int i, dfd, ffd, mode, rc;

	rc = dfd = -1;

	/* Open the file.  Barf if this doesn't work.  */
	mode = (flags & BB_LO_FLAGS_READ_ONLY) ? O_RDONLY : O_RDWR;
 open_ffd:
	ffd = open(file, mode);
	if (ffd < 0) {
		if (mode != O_RDONLY) {
			mode = O_RDONLY;
			goto open_ffd;
		}
		return -errno;
	}

//TODO: use LOOP_CTL_GET_FREE instead of trying every loopN in sequence? a-la:
// fd = open("/dev/loop-control", O_RDWR);
// loopN = ioctl(fd, LOOP_CTL_GET_FREE);
//
	/* Find a loop device.  */
	try = *device ? *device : dev;
	/* 1048575 (0xfffff) is a max possible minor number in Linux circa 2010 */
	for (i = 0; rc && i < 1048576; i++) {
		sprintf(dev, LOOP_FORMAT, i);

		IF_FEATURE_MOUNT_LOOP_CREATE(errno = 0;)
		if (stat(try, &statbuf) != 0 || !S_ISBLK(statbuf.st_mode)) {
			if (ENABLE_FEATURE_MOUNT_LOOP_CREATE
			 && errno == ENOENT
			 && try == dev
			) {
				/* Node doesn't exist, try to create it.  */
				if (mknod(dev, S_IFBLK|0644, makedev(7, i)) == 0)
					goto try_to_open;
			}
			/* Ran out of block devices, return failure.  */
			rc = -1;
			break;
		}
 try_to_open:
		/* Open the sucker and check its loopiness.  */
		dfd = open(try, mode);
		if (dfd < 0 && errno == EROFS) {
			mode = O_RDONLY;
			dfd = open(try, mode);
		}
		if (dfd < 0) {
			if (errno == ENXIO) {
				/* Happens if loop module is not loaded */
				rc = -1;
				break;
			}
			goto try_again;
		}

		rc = ioctl(dfd, BB_LOOP_GET_STATUS, &loopinfo);

		/* If device is free, claim it.  */
		if (rc && errno == ENXIO) {
			/* Associate free loop device with file.  */
			if (ioctl(dfd, LOOP_SET_FD, ffd) == 0) {
				memset(&loopinfo, 0, sizeof(loopinfo));
				safe_strncpy((char *)loopinfo.lo_file_name, file, LO_NAME_SIZE);
				loopinfo.lo_offset = offset;
				/*
				 * Used by mount to set LO_FLAGS_AUTOCLEAR.
				 * LO_FLAGS_READ_ONLY is not set because RO is controlled by open type of the file.
				 * Note that closing LO_FLAGS_AUTOCLEARed dfd before mount
				 * is wrong (would free the loop device!)
				 */
				loopinfo.lo_flags = (flags & ~BB_LO_FLAGS_READ_ONLY);
				rc = ioctl(dfd, BB_LOOP_SET_STATUS, &loopinfo);
				if (rc != 0 && (loopinfo.lo_flags & BB_LO_FLAGS_AUTOCLEAR)) {
					/* Old kernel, does not support LO_FLAGS_AUTOCLEAR? */
					/* (this code path is not tested) */
					loopinfo.lo_flags -= BB_LO_FLAGS_AUTOCLEAR;
					rc = ioctl(dfd, BB_LOOP_SET_STATUS, &loopinfo);
				}
				if (rc != 0) {
					ioctl(dfd, LOOP_CLR_FD, 0);
				}
			}
		} else {
			rc = -1;
		}
		if (rc != 0) {
			close(dfd);
		}
 try_again:
		if (*device) break;
	}
	close(ffd);
	if (rc == 0) {
		if (!*device)
			*device = xstrdup(dev);
		return dfd;
	}
	return rc;
}
