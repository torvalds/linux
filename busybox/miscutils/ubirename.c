/* ubirename - port of the ubirename from the mtd-utils package
 *
 * A utility to rename one UBI volume.
 *
 * 2016-03-01 Sven Eisenberg <sven.eisenberg@novero.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config UBIRENAME
//config:	bool "ubirename (2.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Utility to rename UBI volumes

//applet:IF_UBIRENAME(APPLET(ubirename, BB_DIR_USR_SBIN, BB_SUID_DROP))
/* not NOEXEC: if flash operation stalls, use less memory in "hung" process */

//kbuild:lib-$(CONFIG_UBIRENAME) += ubirename.o

//usage:#define ubirename_trivial_usage
//usage:	"UBI_DEVICE OLD_VOLNAME NEW_VOLNAME [OLD2 NEW2]..."
//usage:#define ubirename_full_usage "\n\n"
//usage:	"Rename UBI volumes on UBI_DEVICE"

#include "libbb.h"
#include <mtd/mtd-user.h>

#ifndef __packed
# define __packed __attribute__((packed))
#endif

// from ubi-media.h
#define UBI_MAX_VOLUME_NAME 127
#define UBI_MAX_VOLUMES     128
// end ubi-media.h

// from ubi-user.h
/* ioctl commands of UBI character devices */
#define UBI_IOC_MAGIC 'o'

/* Re-name volumes */
#define UBI_IOCRNVOL _IOW(UBI_IOC_MAGIC, 3, struct ubi_rnvol_req)

/* Maximum amount of UBI volumes that can be re-named at one go */
#define UBI_MAX_RNVOL 32

struct ubi_rnvol_req {
	int32_t count;
	int8_t  padding1[12];
	struct {
		int32_t vol_id;
		int16_t name_len;
		int8_t  padding2[2];
		char    name[UBI_MAX_VOLUME_NAME + 1];
	} ents[UBI_MAX_RNVOL];
} __packed;
// end ubi-user.h

int ubirename_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ubirename_main(int argc, char **argv)
{
	struct ubi_rnvol_req *rnvol;
	const char *ubi_devname;
	unsigned ubi_devnum;
	unsigned n;

	/* argc can be 4, 6, 8, ... */
	if ((argc & 1) || (argc >>= 1) < 2)
		bb_show_usage();

	rnvol = xzalloc(sizeof(*rnvol));
	rnvol->count = --argc;
	if (argc > ARRAY_SIZE(rnvol->ents))
		bb_error_msg_and_die("too many renames requested");

	ubi_devname = argv[1];
	ubi_devnum = ubi_devnum_from_devname(ubi_devname);

	n = 0;
	argv += 2;
	while (argv[0]) {
		rnvol->ents[n].vol_id = ubi_get_volid_by_name(ubi_devnum, argv[0]);

		/* strnlen avoids overflow of 16-bit field (paranoia) */
		rnvol->ents[n].name_len = strnlen(argv[1], sizeof(rnvol->ents[n].name));
		if (rnvol->ents[n].name_len >= sizeof(rnvol->ents[n].name))
			bb_error_msg_and_die("new name '%s' is too long", argv[1]);

		strcpy(rnvol->ents[n].name, argv[1]);
		n++;
		argv += 2;
	}

	xioctl(xopen(ubi_devname, O_RDONLY), UBI_IOCRNVOL, rnvol);

	return 0;
}
