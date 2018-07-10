/* vi: set sw=4 ts=4: */
/*
 * hexdump implementation for busybox
 * Based on code from util-linux v 2.11l
 *
 * Copyright (c) 1989
 * The Regents of the University of California.  All rights reserved.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config HEXDUMP
//config:	bool "hexdump (8.8 kb)"
//config:	default y
//config:	help
//config:	The hexdump utility is used to display binary data in a readable
//config:	way that is comparable to the output from most hex editors.
//config:
//config:config FEATURE_HEXDUMP_REVERSE
//config:	bool "Support -R, reverse of 'hexdump -Cv'"
//config:	default y
//config:	depends on HEXDUMP
//config:	help
//config:	The hexdump utility is used to display binary data in an ascii
//config:	readable way. This option creates binary data from an ascii input.
//config:	NB: this option is non-standard. It's unwise to use it in scripts
//config:	aimed to be portable.
//config:
//config:config HD
//config:	bool "hd (8 kb)"
//config:	default y
//config:	help
//config:	hd is an alias to hexdump -C.

//applet:IF_HEXDUMP(APPLET_NOEXEC(hexdump, hexdump, BB_DIR_USR_BIN, BB_SUID_DROP, hexdump))
//applet:IF_HD(APPLET_NOEXEC(hd, hexdump, BB_DIR_USR_BIN, BB_SUID_DROP, hd))

//kbuild:lib-$(CONFIG_HEXDUMP) += hexdump.o
//kbuild:lib-$(CONFIG_HD) += hexdump.o

//usage:#define hexdump_trivial_usage
//usage:       "[-bcCdefnosvx" IF_FEATURE_HEXDUMP_REVERSE("R") "] [FILE]..."
//usage:#define hexdump_full_usage "\n\n"
//usage:       "Display FILEs (or stdin) in a user specified format\n"
//usage:     "\n	-b		1-byte octal display"
//usage:     "\n	-c		1-byte character display"
//usage:     "\n	-d		2-byte decimal display"
//usage:     "\n	-o		2-byte octal display"
//usage:     "\n	-x		2-byte hex display"
//usage:     "\n	-C		hex+ASCII 16 bytes per line"
//usage:     "\n	-v		Show all (no dup folding)"
//usage:     "\n	-e FORMAT_STR	Example: '16/1 \"%02x|\"\"\\n\"'"
//usage:     "\n	-f FORMAT_FILE"
// exactly the same help text lines in hexdump and xxd:
//usage:     "\n	-n LENGTH	Show only first LENGTH bytes"
//usage:     "\n	-s OFFSET	Skip OFFSET bytes"
//usage:	IF_FEATURE_HEXDUMP_REVERSE(
//usage:     "\n	-R		Reverse of 'hexdump -Cv'")
// TODO: NONCOMPAT!!! move -R to xxd -r
//usage:
//usage:#define hd_trivial_usage
//usage:       "FILE..."
//usage:#define hd_full_usage "\n\n"
//usage:       "hd is an alias for hexdump -C"

#include "libbb.h"
#include "dump.h"

/* This is a NOEXEC applet. Be very careful! */

static void bb_dump_addfile(dumper_t *dumper, char *name)
{
	char *p;
	FILE *fp;
	char *buf;

	fp = xfopen_for_read(name);
	while ((buf = xmalloc_fgetline(fp)) != NULL) {
		p = skip_whitespace(buf);
		if (*p && (*p != '#')) {
			bb_dump_add(dumper, p);
		}
		free(buf);
	}
	fclose(fp);
}

static const char *const add_strings[] = {
	"\"%07.7_ax \"16/1 \"%03o \"\"\n\"",   /* b */
	"\"%07.7_ax \"16/1 \"%3_c \"\"\n\"",   /* c */
	"\"%07.7_ax \"8/2 \"  %05u \"\"\n\"",  /* d */
	"\"%07.7_ax \"8/2 \" %06o \"\"\n\"",   /* o */
	"\"%07.7_ax \"8/2 \"   %04x \"\"\n\"", /* x */
};

static const char add_first[] ALIGN1 = "\"%07.7_Ax\n\"";

static const char hexdump_opts[] ALIGN1 = "bcdoxCe:f:n:s:v" IF_FEATURE_HEXDUMP_REVERSE("R");

int hexdump_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int hexdump_main(int argc, char **argv)
{
	dumper_t *dumper = alloc_dumper();
	const char *p;
	int ch;
#if ENABLE_FEATURE_HEXDUMP_REVERSE
	FILE *fp;
	smallint rdump = 0;
#endif

	if (ENABLE_HD
	 && (!ENABLE_HEXDUMP || !applet_name[2])
	) { /* we are "hd" */
		ch = 'C';
		goto hd_applet;
	}

	/* We cannot use getopt32: in hexdump options are cumulative.
	 * E.g. "hexdump -C -C file" should dump each line twice */
	while ((ch = getopt(argc, argv, hexdump_opts)) > 0) {
		p = strchr(hexdump_opts, ch);
		if (!p)
			bb_show_usage();
		if ((p - hexdump_opts) < 5) {
			bb_dump_add(dumper, add_first);
			bb_dump_add(dumper, add_strings[(int)(p - hexdump_opts)]);
		}
		/* Save a little bit of space below by omitting the 'else's. */
		if (ch == 'C') {
 hd_applet:
			bb_dump_add(dumper, "\"%08.8_Ax\n\""); // final address line after dump
			//------------------- "address  "   8 * "xx "    "  "  8 * "xx "
			bb_dump_add(dumper, "\"%08.8_ax  \"8/1 \"%02x \"\"  \"8/1 \"%02x \"");
			//------------------- "  |ASCII...........|\n"
			bb_dump_add(dumper, "\"  |\"16/1 \"%_p\"\"|\n\"");
		}
		if (ch == 'e') {
			bb_dump_add(dumper, optarg);
		} /* else */
		if (ch == 'f') {
			bb_dump_addfile(dumper, optarg);
		} /* else */
		if (ch == 'n') {
			dumper->dump_length = xatoi_positive(optarg);
		} /* else */
		if (ch == 's') { /* compat: -s accepts hex numbers too */
			dumper->dump_skip = xstrtoull_range_sfx(
				optarg,
				/*base:*/ 0,
				/*lo:*/ 0, /*hi:*/ OFF_T_MAX,
				bkm_suffixes
			);
		} /* else */
		if (ch == 'v') {
			dumper->dump_vflag = ALL;
		}
#if ENABLE_FEATURE_HEXDUMP_REVERSE
		if (ch == 'R') {
			rdump = 1;
		}
#endif
	}

	if (!dumper->fshead) {
		bb_dump_add(dumper, add_first);
		bb_dump_add(dumper, "\"%07.7_ax \"8/2 \"%04x \"\"\n\"");
	}

	argv += optind;

#if !ENABLE_FEATURE_HEXDUMP_REVERSE
	return bb_dump_dump(dumper, argv);
#else
	if (!rdump) {
		return bb_dump_dump(dumper, argv);
	}

	/* -R: reverse of 'hexdump -Cv' */
	fp = stdin;
	if (!*argv) {
		argv--;
		goto jump_in;
	}

	do {
		char *buf;
		fp = xfopen_for_read(*argv);
 jump_in:
		while ((buf = xmalloc_fgetline(fp)) != NULL) {
			p = buf;
			while (1) {
				/* skip address or previous byte */
				while (isxdigit(*p)) p++;
				while (*p == ' ') p++;
				/* '|' char will break the line */
				if (!isxdigit(*p) || sscanf(p, "%x ", &ch) != 1)
					break;
				putchar(ch);
			}
			free(buf);
		}
		fclose(fp);
	} while (*++argv);

	fflush_stdout_and_exit(EXIT_SUCCESS);
#endif
}
