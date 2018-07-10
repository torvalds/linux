/* vi: set sw=4 ts=4: */
/*
 * Display or change file attributes on a fat file system
 *
 * Copyright 2005 H. Peter Anvin
 * Busybox'ed (2014) by Pascal Bellard <pascal.bellard@ads-lu.com>
 *
 * This file can be redistributed under the terms of the GNU General
 * Public License
 */
//config:config FATATTR
//config:	bool "fatattr (1.9 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	fatattr lists or changes the file attributes on a fat file system.

//applet:IF_FATATTR(APPLET_NOEXEC(fatattr, fatattr, BB_DIR_BIN, BB_SUID_DROP, fatattr))

//kbuild:lib-$(CONFIG_FATATTR) += fatattr.o

//usage:#define fatattr_trivial_usage
//usage:       "[-+rhsvda] FILE..."
//usage:#define fatattr_full_usage "\n\n"
//usage:       "Change file attributes on FAT filesystem\n"
//usage:     "\n	-	Clear attributes"
//usage:     "\n	+	Set attributes"
//usage:     "\n	r	Read only"
//usage:     "\n	h	Hidden"
//usage:     "\n	s	System"
//usage:     "\n	v	Volume label"
//usage:     "\n	d	Directory"
//usage:     "\n	a	Archive"

#include "libbb.h"
/* linux/msdos_fs.h says: */
#ifndef FAT_IOCTL_GET_ATTRIBUTES
# define FAT_IOCTL_GET_ATTRIBUTES        _IOR('r', 0x10, uint32_t)
# define FAT_IOCTL_SET_ATTRIBUTES        _IOW('r', 0x11, uint32_t)
#endif

/* Currently supports only the FAT flags, not the NTFS ones.
 * Extra space at the end is a hack to print space separator in file listing.
 * Let's hope no one ever passes space as an option char :)
 */
static const char bit_to_char[] ALIGN1 = "rhsvda67 ";

static inline unsigned long get_flag(char c)
{
	const char *fp = strchr(bit_to_char, c);
	if (!fp)
		bb_error_msg_and_die("invalid character '%c'", c);
	return 1 << (fp - bit_to_char);
}

static unsigned decode_arg(const char *arg)
{
	unsigned fl = 0;
	while (*++arg)
		fl |= get_flag(*arg);
	return fl;
}

int fatattr_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fatattr_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned set_mask = 0;
	unsigned clear_mask = 0;

	for (;;) {
		unsigned fl;
		char *arg = *++argv;

		if (!arg)
			bb_show_usage();
		if (arg[0] != '-' && arg[0] != '+')
			break;
		fl = decode_arg(arg);
		if (arg[0] == '+')
			set_mask |= fl;
		else
			clear_mask |= fl;
	}

	do {
		int fd, i;
		uint32_t attr;

		fd = xopen(*argv, O_RDONLY);
		xioctl(fd, FAT_IOCTL_GET_ATTRIBUTES, &attr);
		attr = (attr | set_mask) & ~clear_mask;
		if (set_mask | clear_mask)
			xioctl(fd, FAT_IOCTL_SET_ATTRIBUTES, &attr);
		else {
			for (i = 0; bit_to_char[i]; i++) {
				bb_putchar((attr & 1) ? bit_to_char[i] : ' ');
				attr >>= 1;
			}
			puts(*argv);
		}
		close(fd);
	} while (*++argv);

	return EXIT_SUCCESS;
}
