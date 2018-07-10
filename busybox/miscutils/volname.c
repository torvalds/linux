/*
 * Reads and displays CD-ROM volume name
 *
 * Several people have asked how to read CD volume names so I wrote this
 * small program to do it.
 *
 * usage: volname [<device-file>]
 *
 * Copyright (C) 2000-2001 Jeff Tranter (tranter@pobox.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 * mods from distributed source (eject-2.0.13) are by
 * Matthew Stoltenberg <d3matt@gmail.com>
 */
//config:config VOLNAME
//config:	bool "volname (1.7 kb)"
//config:	default y
//config:	help
//config:	Prints a CD-ROM volume name.

//applet:IF_VOLNAME(APPLET(volname, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_VOLNAME) += volname.o

//usage:#define volname_trivial_usage
//usage:       "[DEVICE]"
//usage:#define volname_full_usage "\n\n"
//usage:       "Show CD volume name of the DEVICE (default /dev/cdrom)"

#include "libbb.h"

int volname_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int volname_main(int argc UNUSED_PARAM, char **argv)
{
	int fd;
	char buffer[32];
	const char *device;

	device = "/dev/cdrom";
	if (argv[1]) {
		device = argv[1];
		if (argv[2])
			bb_show_usage();
	}

	fd = xopen(device, O_RDONLY);
	xlseek(fd, 32808, SEEK_SET);
	xread(fd, buffer, 32);
	printf("%32.32s\n", buffer);
	if (ENABLE_FEATURE_CLEAN_UP) {
		close(fd);
	}
	return 0;
}
