/* vi: set sw=4 ts=4: */
/*
 * Mini loadkmap implementation for busybox
 *
 * Copyright (C) 1998 Enrique Zanardi <ezanardi@ull.es>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config LOADKMAP
//config:	bool "loadkmap (1.5 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	This program loads a keyboard translation table from
//config:	standard input.

//applet:IF_LOADKMAP(APPLET_NOEXEC(loadkmap, loadkmap, BB_DIR_SBIN, BB_SUID_DROP, loadkmap))

//kbuild:lib-$(CONFIG_LOADKMAP) += loadkmap.o

//usage:#define loadkmap_trivial_usage
//usage:       "< keymap"
//usage:#define loadkmap_full_usage "\n\n"
//usage:       "Load a binary keyboard translation table from stdin"
////usage:       "\n"
////usage:       "\n	-C TTY	Affect TTY instead of /dev/tty"
//usage:
//usage:#define loadkmap_example_usage
//usage:       "$ loadkmap < /etc/i18n/lang-keymap\n"

#include "libbb.h"

#define BINARY_KEYMAP_MAGIC "bkeymap"

/* From <linux/kd.h> */
struct kbentry {
	unsigned char kb_table;
	unsigned char kb_index;
	unsigned short kb_value;
};
/* sets one entry in translation table */
#define KDSKBENT        0x4B47

/* From <linux/keyboard.h> */
#define NR_KEYS         128
#define MAX_NR_KEYMAPS  256

int loadkmap_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int loadkmap_main(int argc UNUSED_PARAM, char **argv)
{
	struct kbentry ke;
	int i, j, fd;
	uint16_t ibuff[NR_KEYS];
/*	const char *tty_name = CURRENT_TTY; */
	RESERVE_CONFIG_BUFFER(flags, MAX_NR_KEYMAPS);

	/* When user accidentally runs "loadkmap FILE"
	 * instead of "loadkmap <FILE", we end up waiting for input from tty.
	 * Let's prevent it: */
	if (argv[1])
		bb_show_usage();
/* bb_warn_ignoring_args(argv[1]); */

	fd = get_console_fd_or_die();
/* or maybe:
	opt = getopt32(argv, "C:", &tty_name);
	fd = xopen_nonblocking(tty_name);
*/

	xread(STDIN_FILENO, flags, 7);
	if (!is_prefixed_with(flags, BINARY_KEYMAP_MAGIC))
		bb_error_msg_and_die("not a valid binary keymap");

	xread(STDIN_FILENO, flags, MAX_NR_KEYMAPS);

	for (i = 0; i < MAX_NR_KEYMAPS; i++) {
		if (flags[i] != 1)
			continue;
		xread(STDIN_FILENO, ibuff, NR_KEYS * sizeof(uint16_t));
		for (j = 0; j < NR_KEYS; j++) {
			ke.kb_index = j;
			ke.kb_table = i;
			ke.kb_value = ibuff[j];
			/*
			 * Note: table[idx:0] can contain special value
			 * K_ALLOCATED (marks allocated tables in kernel).
			 * dumpkmap saves the value as-is; but attempts
			 * to load it here fail, since it isn't a valid
			 * key value: it is K(KT_SPEC,126) == 2<<8 + 126,
			 * whereas last valid KT_SPEC is
			 * K_BARENUMLOCK == K(KT_SPEC,19).
			 * So far we just ignore these errors:
			 */
			ioctl(fd, KDSKBENT, &ke);
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		close(fd);
		RELEASE_CONFIG_BUFFER(flags);
	}
	return EXIT_SUCCESS;
}
