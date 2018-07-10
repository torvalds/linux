/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FALLOCATE
//config:	bool "fallocate (5 kb)"
//config:	default y
//config:	help
//config:	Preallocate space for files.

//applet:IF_FALLOCATE(APPLET(fallocate, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FALLOCATE) += fallocate.o

//usage:#define fallocate_trivial_usage
//usage:       "[-o OFS] -l LEN FILE"
//		fallocate [-c|-p|-z] [-n] [-o OFS] -l LEN FILE
//		fallocate -d [-o OFS] [-l LEN] FILE
//usage:#define fallocate_full_usage "\n\n"
//usage:	"Preallocate space for FILE\n"
//           "\n	-c	Remove range"
//           "\n	-p	Make hole"
//           "\n	-z	Zero and allocate range"
//           "\n	-d	Convert zeros to holes"
//           "\n	-n	Keep size"
//usage:     "\n	-o OFS	Offset of range"
//usage:     "\n	-l LEN	Length of range"

//Upstream options:
//The options --collapse-range, --dig-holes, --punch-hole and --zero-range
//are mutually exclusive.
//-c, --collapse-range
//    Removes a byte range from a file, without leaving a hole. The byte range
//    to be collapsed starts at offset and continues for length bytes.
//    At the completion of the operation, the contents of the file starting
//    at the location offset+length will be appended at the location offset,
//    and the file will be length bytes smaller. The option --keep-size may
//    not be specified for the collapse-range operation.
//-d, --dig-holes
//    Detect and dig holes. This makes the file sparse in-place, without using
//    extra disk space. The minimum size of the hole depends on filesystem I/O
//    block size (usually 4096 bytes). Also,
//-l, --length length
//    Specifies the length of the range, in bytes.
//-n, --keep-size
//    Do not modify the apparent length of the file. This may effectively
//    allocate blocks past EOF, which can be removed with a truncate.
//-o, --offset offset
//    Specifies the beginning offset of the range, in bytes.
//-p, --punch-hole
//    Deallocates space (i.e., creates a hole) in the byte range starting
//    at offset and continuing for length bytes. Within the specified range,
//    partial filesystem blocks are zeroed, and whole
//    filesystem blocks are removed from the file. After a successful call,
//    subsequent reads from this range will return zeroes. This option may not
//    be specified at the same time as the
//    --zero-range option. Also, when using this option, --keep-size is implied.
//-z, --zero-range
//    Zeroes space in the byte range starting at offset and continuing for
//    length bytes. Within the specified range, blocks are preallocated for
//    the regions that span the holes in the file. After
//    a successful call, subsequent reads from this range will return zeroes.
//    Zeroing is done within the filesystem preferably by converting the range
//    into unwritten extents. This approach means that the specified range
//    will not be physically zeroed out on the device (except for partial
//    blocks at the either end of the range), and I/O is (otherwise) required
//    only to update metadata.
//    Option --keep-size can be specified to prevent file length modification.

#include "libbb.h"

int fallocate_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fallocate_main(int argc UNUSED_PARAM, char **argv)
{
	const char *str_l;
	const char *str_o = "0";
	off_t ofs, len;
	unsigned opts;
	int fd;

	/* exactly one non-option arg */
	opts = getopt32(argv, "^" "l:o:" "\0" "=1", &str_l, &str_o);
	if (!(opts & 1))
		bb_show_usage();

	ofs = xatoull_sfx(str_o, kmg_i_suffixes);
	len = xatoull_sfx(str_l, kmg_i_suffixes);

	argv += optind;
	fd = xopen3(*argv, O_RDWR | O_CREAT, 0666);

	/* posix_fallocate has unusual method of returning error */
	/* maybe use Linux-specific fallocate(int fd, int mode, off_t offset, off_t len) instead? */
	if ((errno = posix_fallocate(fd, ofs, len)) != 0)
		bb_perror_msg_and_die("fallocate '%s'", *argv);

	/* util-linux also performs fsync(fd); */

	return EXIT_SUCCESS;
}
