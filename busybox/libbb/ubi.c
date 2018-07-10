/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2016 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//kbuild:lib-y += ubi.o

#include "libbb.h"

// from ubi-media.h
#define UBI_MAX_VOLUME_NAME 127
#define UBI_MAX_VOLUMES     128

unsigned FAST_FUNC ubi_devnum_from_devname(const char *str)
{
	unsigned ubi_devnum;

	if (sscanf(str, "/dev/ubi%u", &ubi_devnum) != 1)
		bb_error_msg_and_die("not an UBI device: '%s'", str);
	return ubi_devnum;
}

int FAST_FUNC ubi_get_volid_by_name(unsigned ubi_devnum, const char *vol_name)
{
	unsigned i;

	for (i = 0; i < UBI_MAX_VOLUMES; i++) {
		char buf[UBI_MAX_VOLUME_NAME + 1];
		char fname[sizeof("/sys/class/ubi/ubi%u_%u/name") + 2 * sizeof(int)*3];

		sprintf(fname, "/sys/class/ubi/ubi%u_%u/name", ubi_devnum, i);
		if (open_read_close(fname, buf, sizeof(buf)) <= 0)
			continue;

		buf[UBI_MAX_VOLUME_NAME] = '\0';
		strchrnul(buf, '\n')[0] = '\0';
		if (strcmp(vol_name, buf) == 0)
			return i;
	}
	bb_error_msg_and_die("volume '%s' not found", vol_name);
}
