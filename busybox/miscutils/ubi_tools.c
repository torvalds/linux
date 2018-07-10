/*
 * Ported to busybox from mtd-utils.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config UBIATTACH
//config:	bool "ubiattach (4.7 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Attach MTD device to an UBI device.
//config:
//config:config UBIDETACH
//config:	bool "ubidetach (4.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Detach MTD device from an UBI device.
//config:
//config:config UBIMKVOL
//config:	bool "ubimkvol (5.8 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Create a UBI volume.
//config:
//config:config UBIRMVOL
//config:	bool "ubirmvol (5.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Delete a UBI volume.
//config:
//config:config UBIRSVOL
//config:	bool "ubirsvol (4.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Resize a UBI volume.
//config:
//config:config UBIUPDATEVOL
//config:	bool "ubiupdatevol (5.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Update a UBI volume.

//                       APPLET_ODDNAME:name       main       location         suid_type     help
//applet:IF_UBIATTACH(   APPLET_ODDNAME(ubiattach, ubi_tools, BB_DIR_USR_SBIN, BB_SUID_DROP, ubiattach))
//applet:IF_UBIDETACH(   APPLET_ODDNAME(ubidetach, ubi_tools, BB_DIR_USR_SBIN, BB_SUID_DROP, ubidetach))
//applet:IF_UBIMKVOL(    APPLET_ODDNAME(ubimkvol,  ubi_tools, BB_DIR_USR_SBIN, BB_SUID_DROP, ubimkvol))
//applet:IF_UBIRMVOL(    APPLET_ODDNAME(ubirmvol,  ubi_tools, BB_DIR_USR_SBIN, BB_SUID_DROP, ubirmvol))
//applet:IF_UBIRSVOL(    APPLET_ODDNAME(ubirsvol,  ubi_tools, BB_DIR_USR_SBIN, BB_SUID_DROP, ubirsvol))
//applet:IF_UBIUPDATEVOL(APPLET_ODDNAME(ubiupdatevol, ubi_tools, BB_DIR_USR_SBIN, BB_SUID_DROP, ubiupdatevol))
/* not NOEXEC: if flash operation stalls, use less memory in "hung" process */

//kbuild:lib-$(CONFIG_UBIATTACH) += ubi_tools.o
//kbuild:lib-$(CONFIG_UBIDETACH) += ubi_tools.o
//kbuild:lib-$(CONFIG_UBIMKVOL)  += ubi_tools.o
//kbuild:lib-$(CONFIG_UBIRMVOL)  += ubi_tools.o
//kbuild:lib-$(CONFIG_UBIRSVOL)  += ubi_tools.o
//kbuild:lib-$(CONFIG_UBIUPDATEVOL) += ubi_tools.o

#include "libbb.h"
/* Some versions of kernel have broken headers, need this hack */
#ifndef __packed
# define __packed __attribute__((packed))
#endif
#include <mtd/ubi-user.h>

#define UBI_APPLET_CNT (0 \
	+ ENABLE_UBIATTACH \
	+ ENABLE_UBIDETACH \
	+ ENABLE_UBIMKVOL \
	+ ENABLE_UBIRMVOL \
	+ ENABLE_UBIRSVOL \
	+ ENABLE_UBIUPDATEVOL \
	)

#define do_attach (ENABLE_UBIATTACH    && (UBI_APPLET_CNT == 1 || applet_name[4] == 't'))
#define do_detach (ENABLE_UBIDETACH    && (UBI_APPLET_CNT == 1 || applet_name[4] == 'e'))
#define do_mkvol  (ENABLE_UBIMKVOL     && (UBI_APPLET_CNT == 1 || applet_name[4] == 'k'))
#define do_rmvol  (ENABLE_UBIRMVOL     && (UBI_APPLET_CNT == 1 || applet_name[4] == 'm'))
#define do_rsvol  (ENABLE_UBIRSVOL     && (UBI_APPLET_CNT == 1 || applet_name[4] == 's'))
#define do_update (ENABLE_UBIUPDATEVOL && (UBI_APPLET_CNT == 1 || applet_name[4] == 'p'))

static unsigned get_num_from_file(const char *path, unsigned max)
{
	char buf[sizeof(long long)*3];
	unsigned long long num;

	if (open_read_close(path, buf, sizeof(buf)) < 0)
		bb_perror_msg_and_die("can't open '%s'", path);
	/* It can be \n terminated, xatoull won't work well */
	if (sscanf(buf, "%llu", &num) != 1 || num > max)
		bb_error_msg_and_die("number in '%s' is malformed or too large", path);
	return num;
}

/* To prevent malloc(1G) accidents */
#define MAX_SANE_ERASEBLOCK (16*1024*1024)

int ubi_tools_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ubi_tools_main(int argc UNUSED_PARAM, char **argv)
{
	static const struct suffix_mult size_suffixes[] = {
		{ "KiB", 1024 },
		{ "MiB", 1024*1024 },
		{ "GiB", 1024*1024*1024 },
		{ "", 0 }
	};

	unsigned opts;
	char *ubi_ctrl;
	int fd;
	int mtd_num;
	int dev_num = UBI_DEV_NUM_AUTO;
	int vol_id = UBI_VOL_NUM_AUTO;
	int vid_hdr_offset = 0;
	char *vol_name;
	unsigned long long size_bytes = size_bytes; /* for compiler */
	char *size_bytes_str;
	int alignment = 1;
	char *type;
	union {
		struct ubi_attach_req attach_req;
		struct ubi_mkvol_req  mkvol_req;
		struct ubi_rsvol_req  rsvol_req;
	} req_structs;
#define attach_req req_structs.attach_req
#define mkvol_req  req_structs.mkvol_req
#define rsvol_req  req_structs.rsvol_req
	char path[sizeof("/sys/class/ubi/ubi%d_%d/usable_eb_size")
				+ 2 * sizeof(int)*3 + /*just in case:*/ 16];
#define path_sys_class_ubi_ubi (path + sizeof("/sys/class/ubi/ubi")-1)

	strcpy(path, "/sys/class/ubi/ubi");
	memset(&req_structs, 0, sizeof(req_structs));

#define OPTION_m  (1 << 0)
#define OPTION_d  (1 << 1)
#define OPTION_n  (1 << 2)
#define OPTION_N  (1 << 3)
#define OPTION_s  (1 << 4)
#define OPTION_a  (1 << 5)
#define OPTION_t  (1 << 6)
	if (do_mkvol) {
		opts = getopt32(argv, "^" "md:+n:+N:s:a:+t:O:+" "\0" "-1",
				&dev_num, &vol_id,
				&vol_name, &size_bytes_str, &alignment, &type,
				&vid_hdr_offset
			);
	} else
	if (do_update) {
		opts = getopt32(argv, "^" "s:at" "\0" "-1", &size_bytes_str);
		opts *= OPTION_s;
	} else {
		opts = getopt32(argv, "^" "m:+d:+n:+N:s:a:+t:" "\0" "-1",
				&mtd_num, &dev_num, &vol_id,
				&vol_name, &size_bytes_str, &alignment, &type
		);
	}

	if (opts & OPTION_s)
		size_bytes = xatoull_sfx(size_bytes_str, size_suffixes);
	argv += optind;
	ubi_ctrl = *argv++;

	fd = xopen(ubi_ctrl, O_RDWR);
	//xfstat(fd, &st, ubi_ctrl);
	//if (!S_ISCHR(st.st_mode))
	//	bb_error_msg_and_die("%s: not a char device", ubi_ctrl);

//usage:#define ubiattach_trivial_usage
//usage:       "-m MTD_NUM [-d UBI_NUM] [-O VID_HDR_OFF] UBI_CTRL_DEV"
//usage:#define ubiattach_full_usage "\n\n"
//usage:       "Attach MTD device to UBI\n"
//usage:     "\n	-m MTD_NUM	MTD device number to attach"
//usage:     "\n	-d UBI_NUM	UBI device number to assign"
//usage:     "\n	-O VID_HDR_OFF	VID header offset"
	if (do_attach) {
		if (!(opts & OPTION_m))
			bb_error_msg_and_die("%s device not specified", "MTD");

		attach_req.mtd_num = mtd_num;
		attach_req.ubi_num = dev_num;
		attach_req.vid_hdr_offset = vid_hdr_offset;

		xioctl(fd, UBI_IOCATT, &attach_req);
	} else

//usage:#define ubidetach_trivial_usage
//usage:       "-d UBI_NUM UBI_CTRL_DEV"
//usage:#define ubidetach_full_usage "\n\n"
//usage:       "Detach MTD device from UBI\n"
//usage:     "\n	-d UBI_NUM	UBI device number"
	if (do_detach) {
		if (!(opts & OPTION_d))
			bb_error_msg_and_die("%s device not specified", "UBI");

		/* FIXME? kernel expects int32_t* here: */
		xioctl(fd, UBI_IOCDET, &dev_num);
	} else

//usage:#define ubimkvol_trivial_usage
//usage:       "-N NAME [-s SIZE | -m] UBI_DEVICE"
//usage:#define ubimkvol_full_usage "\n\n"
//usage:       "Create UBI volume\n"
//usage:     "\n	-a ALIGNMENT	Volume alignment (default 1)"
//usage:     "\n	-m		Set volume size to maximum available"
//usage:     "\n	-n VOLID	Volume ID. If not specified,"
//usage:     "\n			assigned automatically"
//usage:     "\n	-N NAME		Volume name"
//usage:     "\n	-s SIZE		Size in bytes"
//usage:     "\n	-t TYPE		Volume type (static|dynamic)"
	if (do_mkvol) {
		if (opts & OPTION_m) {
			unsigned leb_avail;
			unsigned leb_size;
			unsigned num;
			char *p;

			num = ubi_devnum_from_devname(ubi_ctrl);
			p = path_sys_class_ubi_ubi + sprintf(path_sys_class_ubi_ubi, "%u/", num);

			strcpy(p, "avail_eraseblocks");
			leb_avail = get_num_from_file(path, UINT_MAX);

			strcpy(p, "eraseblock_size");
			leb_size = get_num_from_file(path, MAX_SANE_ERASEBLOCK);

			size_bytes = leb_avail * (unsigned long long)leb_size;
			//if (size_bytes <= 0)
			//	bb_error_msg_and_die("%s invalid maximum size calculated", "UBI");
		} else
		if (!(opts & OPTION_s))
			bb_error_msg_and_die("size not specified");

		if (!(opts & OPTION_N))
			bb_error_msg_and_die("name not specified");

		/* the structure is memset(0) above */
		mkvol_req.vol_id = vol_id;
		mkvol_req.vol_type = UBI_DYNAMIC_VOLUME;
		if ((opts & OPTION_t) && type[0] == 's')
			mkvol_req.vol_type = UBI_STATIC_VOLUME;
		mkvol_req.alignment = alignment;
		mkvol_req.bytes = size_bytes; /* signed int64_t */
		/* strnlen avoids overflow of 16-bit field (paranoia) */
		mkvol_req.name_len = strnlen(vol_name, UBI_MAX_VOLUME_NAME+1);
		if (mkvol_req.name_len > UBI_MAX_VOLUME_NAME)
			bb_error_msg_and_die("volume name too long: '%s'", vol_name);
		/* this is safe: .name[] is UBI_MAX_VOLUME_NAME+1 bytes */
		strcpy(mkvol_req.name, vol_name);

		xioctl(fd, UBI_IOCMKVOL, &mkvol_req);
	} else

//usage:#define ubirmvol_trivial_usage
//usage:       "-n VOLID / -N VOLNAME UBI_DEVICE"
//usage:#define ubirmvol_full_usage "\n\n"
//usage:       "Remove UBI volume\n"
//usage:     "\n	-n VOLID	Volume ID"
//usage:     "\n	-N VOLNAME	Volume name"
	if (do_rmvol) {
		if (!(opts & (OPTION_n|OPTION_N)))
			bb_error_msg_and_die("volume id not specified");

		if (opts & OPTION_N) {
			unsigned num = ubi_devnum_from_devname(ubi_ctrl);
			vol_id = ubi_get_volid_by_name(num, vol_name);
		}

		if (sizeof(vol_id) != 4) {
			/* kernel expects int32_t* in this ioctl */
			int32_t t = vol_id;
			xioctl(fd, UBI_IOCRMVOL, &t);
		} else {
			xioctl(fd, UBI_IOCRMVOL, &vol_id);
		}
	} else

//usage:#define ubirsvol_trivial_usage
//usage:       "-n VOLID -s SIZE UBI_DEVICE"
//usage:#define ubirsvol_full_usage "\n\n"
//usage:       "Resize UBI volume\n"
//usage:     "\n	-n VOLID	Volume ID"
//usage:     "\n	-s SIZE		Size in bytes"
	if (do_rsvol) {
		if (!(opts & OPTION_s))
			bb_error_msg_and_die("size not specified");
		if (!(opts & OPTION_n))
			bb_error_msg_and_die("volume id not specified");

		rsvol_req.bytes = size_bytes; /* signed int64_t */
		rsvol_req.vol_id = vol_id;

		xioctl(fd, UBI_IOCRSVOL, &rsvol_req);
	} else

//usage:#define ubiupdatevol_trivial_usage
//usage:       "-t UBI_DEVICE | [-s SIZE] UBI_DEVICE IMG_FILE"
//usage:#define ubiupdatevol_full_usage "\n\n"
//usage:       "Update UBI volume\n"
//usage:     "\n	-t	Truncate to zero size"
//usage:     "\n	-s SIZE	Size in bytes to resize to"
	if (do_update) {
		int64_t bytes64;

		if (opts & OPTION_t) {
			/* truncate the volume by starting an update for size 0 */
			bytes64 = 0;
			/* this ioctl expects int64_t* parameter */
			xioctl(fd, UBI_IOCVOLUP, &bytes64);
		}
		else {
			unsigned ubinum, volnum;
			unsigned leb_size;
			char *buf;

			/* Assume that device is in normal format. */
			/* Removes need for scanning sysfs tree as full libubi does. */
			if (sscanf(ubi_ctrl, "/dev/ubi%u_%u", &ubinum, &volnum) != 2)
				bb_error_msg_and_die("UBI device name '%s' is not /dev/ubiN_M", ubi_ctrl);

			sprintf(path_sys_class_ubi_ubi, "%u_%u/usable_eb_size", ubinum, volnum);
			leb_size = get_num_from_file(path, MAX_SANE_ERASEBLOCK);

			if (!*argv)
				bb_show_usage();
			if (NOT_LONE_DASH(*argv)) /* mtd-utils supports "-" as stdin */
				xmove_fd(xopen(*argv, O_RDONLY), STDIN_FILENO);

			if (!(opts & OPTION_s)) {
				struct stat st;
				xfstat(STDIN_FILENO, &st, *argv);
				size_bytes = st.st_size;
			}

			bytes64 = size_bytes;
			/* this ioctl expects signed int64_t* parameter */
			xioctl(fd, UBI_IOCVOLUP, &bytes64);

			/* can't use bb_copyfd_exact_size(): copy in blocks of exactly leb_size */
			buf = xmalloc(leb_size);
			while (size_bytes != 0) {
				int len = full_read(STDIN_FILENO, buf, leb_size);
				if (len <= 0) {
					if (len < 0)
						bb_perror_msg_and_die("read error from '%s'", *argv);
					break;
				}
				if ((unsigned)len > size_bytes) {
					/* for this case: "ubiupdatevol -s 1024000 $UBIDEV /dev/urandom" */
					len = size_bytes;
				}
				xwrite(fd, buf, len);
				size_bytes -= len;
			}
			if (ENABLE_FEATURE_CLEAN_UP)
				free(buf);
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		close(fd);

	return EXIT_SUCCESS;
}
