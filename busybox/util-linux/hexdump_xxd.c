/* vi: set sw=4 ts=4: */
/*
 * xxd implementation for busybox
 *
 * Copyright (c) 2017 Denys Vlasenko <vda.linux@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config XXD
//config:	bool "xxd (8.9 kb)"
//config:	default y
//config:	help
//config:	The xxd utility is used to display binary data in a readable
//config:	way that is comparable to the output from most hex editors.

//applet:IF_XXD(APPLET_NOEXEC(xxd, xxd, BB_DIR_USR_BIN, BB_SUID_DROP, xxd))

//kbuild:lib-$(CONFIG_XXD) += hexdump_xxd.o

// $ xxd --version
// xxd V1.10 27oct98 by Juergen Weigert
// $ xxd --help
// Usage:
//       xxd [options] [infile [outfile]]
//    or
//       xxd -r [-s [-]offset] [-c cols] [-ps] [infile [outfile]]
// Options:
//    -a          toggle autoskip: A single '*' replaces nul-lines. Default off.
//    -b          binary digit dump (incompatible with -ps,-i,-r). Default hex.
//    -c cols     format <cols> octets per line. Default 16 (-i: 12, -ps: 30).
//    -E          show characters in EBCDIC. Default ASCII.
//    -e          little-endian dump (incompatible with -ps,-i,-r).
//    -g          number of octets per group in normal output. Default 2 (-e: 4).
//    -i          output in C include file style.
//    -l len      stop after <len> octets.
//    -o off      add <off> to the displayed file position.
//    -ps         output in postscript plain hexdump style.
//    -r          reverse operation: convert (or patch) hexdump into binary.
//    -r -s off   revert with <off> added to file positions found in hexdump.
//    -s [+][-]seek  start at <seek> bytes abs. (or +: rel.) infile offset.
//    -u          use upper case hex letters.

//usage:#define xxd_trivial_usage
//usage:       "[OPTIONS] [FILE]"
//usage:#define xxd_full_usage "\n\n"
//usage:       "Hex dump FILE (or stdin)\n"
//usage:     "\n	-g N		Bytes per group"
//usage:     "\n	-c N		Bytes per line"
//usage:     "\n	-p		Show only hex bytes, assumes -c30"
// exactly the same help text lines in hexdump and xxd:
//usage:     "\n	-l LENGTH	Show only first LENGTH bytes"
//usage:     "\n	-s OFFSET	Skip OFFSET bytes"
// TODO: implement -r (see hexdump -R)

#include "libbb.h"
#include "dump.h"

/* This is a NOEXEC applet. Be very careful! */

int xxd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int xxd_main(int argc UNUSED_PARAM, char **argv)
{
	char buf[80];
	dumper_t *dumper;
	char *opt_l, *opt_s;
	unsigned bytes = 2;
	unsigned cols = 0;
	unsigned opt;

	dumper = alloc_dumper();

#define OPT_l (1 << 0)
#define OPT_s (1 << 1)
#define OPT_a (1 << 2)
#define OPT_p (1 << 3)
	opt = getopt32(argv, "^" "l:s:apg:+c:+" "\0" "?1" /* 1 argument max */,
			&opt_l, &opt_s, &bytes, &cols
	);
	argv += optind;

	dumper->dump_vflag = ALL;
//	if (opt & OPT_a)
//		dumper->dump_vflag = SKIPNUL; ..does not exist
	if (opt & OPT_l) {
		dumper->dump_length = xstrtou_range(
				opt_l,
				/*base:*/ 0,
				/*lo:*/ 0, /*hi:*/ INT_MAX
		);
	}
	if (opt & OPT_s) {
		dumper->dump_skip = xstrtoull_range(
				opt_s,
				/*base:*/ 0,
				/*lo:*/ 0, /*hi:*/ OFF_T_MAX
		);
		//BUGGY for /proc/version (unseekable?)
	}

	if (opt & OPT_p) {
		if (cols == 0)
			cols = 30;
		bytes = cols; /* -p ignores -gN */
	} else {
		if (cols == 0)
			cols = 16;
		bb_dump_add(dumper, "\"%08.8_ax: \""); // "address: "
	}

	if (bytes < 1 || bytes >= cols) {
		sprintf(buf, "%u/1 \"%%02x\"", cols); // cols * "xx"
		bb_dump_add(dumper, buf);
	}
	else if (bytes == 1) {
		sprintf(buf, "%u/1 \"%%02x \"", cols); // cols * "xx "
		bb_dump_add(dumper, buf);
	}
	else {
/* Format "print byte" with and without trailing space */
#define BS "/1 \"%02x \""
#define B  "/1 \"%02x\""
		unsigned i;
		char *bigbuf = xmalloc(cols * (sizeof(BS)-1));
		char *p = bigbuf;
		for (i = 1; i <= cols; i++) {
			if (i == cols || i % bytes)
				p = stpcpy(p, B);
			else
				p = stpcpy(p, BS);
		}
		// for -g3, this results in B B BS B B BS... B = "xxxxxx xxxxxx .....xx"
		// todo: can be more clever and use
		// one 'bytes-1/1 "%02x"' format instead of many "B B B..." formats
		//bb_error_msg("ADDED:'%s'", bigbuf);
		bb_dump_add(dumper, bigbuf);
		free(bigbuf);
	}

	if (!(opt & OPT_p)) {
		sprintf(buf, "\"  \"%u/1 \"%%_p\"\"\n\"", cols); // "  ASCII\n"
		bb_dump_add(dumper, buf);
	} else {
		bb_dump_add(dumper, "\"\n\"");
	}

	return bb_dump_dump(dumper, argv);
}
