/* vi: set sw=4 ts=4: */
/*
 * eject implementation for busybox
 *
 * Copyright (C) 2004  Peter Willis <psyphreak@phreaker.net>
 * Copyright (C) 2005  Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/*
 * This is a simple hack of eject based on something Erik posted in #uclibc.
 * Most of the dirty work blatantly ripped off from cat.c =)
 */
//config:config EJECT
//config:	bool "eject (4.1 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Used to eject cdroms. (defaults to /dev/cdrom)
//config:
//config:config FEATURE_EJECT_SCSI
//config:	bool "SCSI support"
//config:	default y
//config:	depends on EJECT
//config:	help
//config:	Add the -s option to eject, this allows to eject SCSI-Devices and
//config:	usb-storage devices.

//applet:IF_EJECT(APPLET(eject, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_EJECT) += eject.o

//usage:#define eject_trivial_usage
//usage:       "[-t] [-T] [DEVICE]"
//usage:#define eject_full_usage "\n\n"
//usage:       "Eject DEVICE or default /dev/cdrom\n"
//usage:	IF_FEATURE_EJECT_SCSI(
//usage:     "\n	-s	SCSI device"
//usage:	)
//usage:     "\n	-t	Close tray"
//usage:     "\n	-T	Open/close tray (toggle)"

#include <sys/mount.h>
#include "libbb.h"
#if ENABLE_FEATURE_EJECT_SCSI
/* Must be after libbb.h: they need size_t */
# include "fix_u32.h"
# include <scsi/sg.h>
# include <scsi/scsi.h>
#endif

#define dev_fd 3

/* Code taken from the original eject (http://eject.sourceforge.net/),
 * refactored it a bit for busybox (ne-bb@nicoerfurth.de) */

#if ENABLE_FEATURE_EJECT_SCSI
static void eject_scsi(const char *dev)
{
	static const char sg_commands[3][6] ALIGN1 = {
		{ ALLOW_MEDIUM_REMOVAL, 0, 0, 0, 0, 0 },
		{ START_STOP, 0, 0, 0, 1, 0 },
		{ START_STOP, 0, 0, 0, 2, 0 }
	};

	unsigned i;
	unsigned char sense_buffer[32];
	unsigned char inqBuff[2];
	sg_io_hdr_t io_hdr;

	if ((ioctl(dev_fd, SG_GET_VERSION_NUM, &i) < 0) || (i < 30000))
		bb_error_msg_and_die("not a sg device or old sg driver");

	memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = 6;
	io_hdr.mx_sb_len = sizeof(sense_buffer);
	io_hdr.dxfer_direction = SG_DXFER_NONE;
	/* io_hdr.dxfer_len = 0; */
	io_hdr.dxferp = inqBuff;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = 2000;

	for (i = 0; i < 3; i++) {
		io_hdr.cmdp = (void *)sg_commands[i];
		ioctl_or_perror_and_die(dev_fd, SG_IO, (void *)&io_hdr, "%s", dev);
	}

	/* force kernel to reread partition table when new disc is inserted */
	ioctl(dev_fd, BLKRRPART);
}
#else
# define eject_scsi(dev) ((void)0)
#endif

/* various defines swiped from linux/cdrom.h */
#define CDROMCLOSETRAY            0x5319  /* pendant of CDROMEJECT  */
#define CDROMEJECT                0x5309  /* Ejects the cdrom media */
#define CDROM_DRIVE_STATUS        0x5326  /* Get tray position, etc. */
/* drive status possibilities returned by CDROM_DRIVE_STATUS ioctl */
#define CDS_TRAY_OPEN        2

#define FLAG_CLOSE  1
#define FLAG_SMART  2
#define FLAG_SCSI   4

static void eject_cdrom(unsigned flags, const char *dev)
{
	int cmd = CDROMEJECT;

	if (flags & FLAG_CLOSE
	 || ((flags & FLAG_SMART) && ioctl(dev_fd, CDROM_DRIVE_STATUS) == CDS_TRAY_OPEN)
	) {
		cmd = CDROMCLOSETRAY;
	}

	ioctl_or_perror_and_die(dev_fd, cmd, NULL, "%s", dev);
}

int eject_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int eject_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned flags;
	const char *device;

	flags = getopt32(argv, "^" "tT"IF_FEATURE_EJECT_SCSI("s")
			"\0" "?1:t--T:T--t"
	);
	device = argv[optind] ? argv[optind] : "/dev/cdrom";

	/* We used to do "umount <device>" here, but it was buggy
	   if something was mounted OVER cdrom and
	   if cdrom is mounted many times.

	   This works equally well (or better):
	   #!/bin/sh
	   umount /dev/cdrom
	   eject /dev/cdrom
	*/

	xmove_fd(xopen_nonblocking(device), dev_fd);

	if (ENABLE_FEATURE_EJECT_SCSI && (flags & FLAG_SCSI))
		eject_scsi(device);
	else
		eject_cdrom(flags, device);

	if (ENABLE_FEATURE_CLEAN_UP)
		close(dev_fd);

	return EXIT_SUCCESS;
}
