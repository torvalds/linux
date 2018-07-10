/* vi: set sw=4 ts=4: */
/*
 * Ported to busybox from mtd-utils.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FLASH_LOCK
//config:	bool "flash_lock (2.1 kb)"
//config:	default n  # doesn't build on Ubuntu 8.04
//config:	help
//config:	The flash_lock binary from mtd-utils as of git head 5ec0c10d0. This
//config:	utility locks part or all of the flash device.
//config:
//config:config FLASH_UNLOCK
//config:	bool "flash_unlock (1.3 kb)"
//config:	default n  # doesn't build on Ubuntu 8.04
//config:	help
//config:	The flash_unlock binary from mtd-utils as of git head 5ec0c10d0. This
//config:	utility unlocks part or all of the flash device.

//                       APPLET_ODDNAME:name          main               location         suid_type     help
//applet:IF_FLASH_LOCK(  APPLET_ODDNAME(flash_lock,   flash_lock_unlock, BB_DIR_USR_SBIN, BB_SUID_DROP, flash_lock))
//applet:IF_FLASH_UNLOCK(APPLET_ODDNAME(flash_unlock, flash_lock_unlock, BB_DIR_USR_SBIN, BB_SUID_DROP, flash_unlock))
/* not NOEXEC: if flash operation stalls, use less memory in "hung" process */

//kbuild:lib-$(CONFIG_FLASH_LOCK) += flash_lock_unlock.o
//kbuild:lib-$(CONFIG_FLASH_UNLOCK) += flash_lock_unlock.o

//usage:#define flash_lock_trivial_usage
//usage:       "MTD_DEVICE OFFSET SECTORS"
//usage:#define flash_lock_full_usage "\n\n"
//usage:       "Lock part or all of an MTD device. If SECTORS is -1, then all sectors\n"
//usage:       "will be locked, regardless of the value of OFFSET"
//usage:
//usage:#define flash_unlock_trivial_usage
//usage:       "MTD_DEVICE"
//usage:#define flash_unlock_full_usage "\n\n"
//usage:       "Unlock an MTD device"

#include "libbb.h"
#include <mtd/mtd-user.h>

int flash_lock_unlock_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int flash_lock_unlock_main(int argc UNUSED_PARAM, char **argv)
{
	/* note: fields in these structs are 32-bits.
	 * apparently we can't win anything by using off_t
	 * or long long's for offset and/or sectors vars. */
	struct mtd_info_user info;
	struct erase_info_user lock;
	unsigned long offset;
	long sectors;
	int fd;

#define do_lock (ENABLE_FLASH_LOCK && (!ENABLE_FLASH_UNLOCK || (applet_name[6] == 'l')))

	if (!argv[1])
		bb_show_usage();

	/* parse offset and number of sectors to lock */
	offset = 0;
	sectors = -1;
	if (do_lock) {
		if (!argv[2] || !argv[3])
			bb_show_usage();
		offset = xstrtoul(argv[2], 0);
		sectors = xstrtol(argv[3], 0);
	}

	fd = xopen(argv[1], O_RDWR);

	xioctl(fd, MEMGETINFO, &info);

	lock.start = 0;
	lock.length = info.size;
	if (do_lock) {
		unsigned long size = info.size - info.erasesize;
		if (offset > size) {
			bb_error_msg_and_die("%lx is beyond device size %lx\n",
					offset, size);
		}

		if (sectors == -1) {
			sectors = info.size / info.erasesize;
		} else {
// isn't this useless?
			unsigned long num = info.size / info.erasesize;
			if (sectors > num) {
				bb_error_msg_and_die("%ld are too many "
						"sectors, device only has "
						"%ld\n", sectors, num);
			}
		}

		lock.start = offset;
		lock.length = sectors * info.erasesize;
		xioctl(fd, MEMLOCK, &lock);
	} else {
		xioctl(fd, MEMUNLOCK, &lock);
	}

	return EXIT_SUCCESS;
}
