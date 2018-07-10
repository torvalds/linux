/* vi: set sw=4 ts=4: */
/*
 * dpkg-deb packs, unpacks and provides information about Debian archives.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config DPKG_DEB
//config:	bool "dpkg_deb"
//config:	default y
//config:	select FEATURE_SEAMLESS_GZ
//config:	help
//config:	dpkg-deb unpacks and provides information about Debian archives.
//config:
//config:	This implementation of dpkg-deb cannot pack archives.
//config:
//config:	Unless you have a specific application which requires dpkg-deb,
//config:	say N here.

//applet:IF_DPKG_DEB(APPLET_ODDNAME(dpkg-deb, dpkg_deb, BB_DIR_USR_BIN, BB_SUID_DROP, dpkg_deb))

//kbuild:lib-$(CONFIG_DPKG_DEB) += dpkg_deb.o

//usage:#define dpkg_deb_trivial_usage
//usage:       "[-cefxX] FILE [DIR]"
//usage:#define dpkg_deb_full_usage "\n\n"
//usage:       "Perform actions on Debian packages (.deb)\n"
//usage:     "\n	-c	List files"
//usage:     "\n	-f	Print control fields"
//usage:     "\n	-e	Extract control files to DIR (default: ./DEBIAN)"
//usage:     "\n	-x	Extract files to DIR (no default)"
//usage:     "\n	-X	Verbose -x"
//usage:
//usage:#define dpkg_deb_example_usage
//usage:       "$ dpkg-deb -X ./busybox_0.48-1_i386.deb /tmp\n"

#include "libbb.h"
#include "bb_archive.h"

#define DPKG_DEB_OPT_CONTENTS         1
#define DPKG_DEB_OPT_CONTROL          2
#define DPKG_DEB_OPT_FIELD            4
#define DPKG_DEB_OPT_EXTRACT_VERBOSE  8
#define DPKG_DEB_OPT_EXTRACT         16

int dpkg_deb_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int dpkg_deb_main(int argc UNUSED_PARAM, char **argv)
{
	archive_handle_t *ar_archive;
	archive_handle_t *tar_archive;
	llist_t *control_tar_llist = NULL;
	unsigned opt;
	const char *extract_dir;

	/* Setup the tar archive handle */
	tar_archive = init_handle();

	/* Setup an ar archive handle that refers to the gzip sub archive */
	ar_archive = init_handle();
	ar_archive->dpkg__sub_archive = tar_archive;
	ar_archive->filter = filter_accept_list_reassign;

	llist_add_to(&ar_archive->accept, (char*)"data.tar");
	llist_add_to(&control_tar_llist, (char*)"control.tar");
#if ENABLE_FEATURE_SEAMLESS_GZ
	llist_add_to(&ar_archive->accept, (char*)"data.tar.gz");
	llist_add_to(&control_tar_llist, (char*)"control.tar.gz");
#endif
#if ENABLE_FEATURE_SEAMLESS_BZ2
	llist_add_to(&ar_archive->accept, (char*)"data.tar.bz2");
	llist_add_to(&control_tar_llist, (char*)"control.tar.bz2");
#endif
#if ENABLE_FEATURE_SEAMLESS_LZMA
	llist_add_to(&ar_archive->accept, (char*)"data.tar.lzma");
	llist_add_to(&control_tar_llist, (char*)"control.tar.lzma");
#endif
#if ENABLE_FEATURE_SEAMLESS_XZ
	llist_add_to(&ar_archive->accept, (char*)"data.tar.xz");
	llist_add_to(&control_tar_llist, (char*)"control.tar.xz");
#endif

	/* Must have 1 or 2 args */
	opt = getopt32(argv, "^" "cefXx"
			"\0" "-1:?2:c--efXx:e--cfXx:f--ceXx:X--cefx:x--cefX"
	);
	argv += optind;
	//argc -= optind;

	extract_dir = argv[1];
	if (opt & DPKG_DEB_OPT_CONTENTS) { // -c
		tar_archive->action_header = header_verbose_list;
		if (extract_dir)
			bb_show_usage();
	}
	if (opt & DPKG_DEB_OPT_FIELD) { // -f
		/* Print the entire control file */
//TODO: standard tool accepts an optional list of fields to print
		ar_archive->accept = control_tar_llist;
		llist_add_to(&(tar_archive->accept), (char*)"./control");
		tar_archive->filter = filter_accept_list;
		tar_archive->action_data = data_extract_to_stdout;
		if (extract_dir)
			bb_show_usage();
	}
	if (opt & DPKG_DEB_OPT_CONTROL) { // -e
		ar_archive->accept = control_tar_llist;
		tar_archive->action_data = data_extract_all;
		if (!extract_dir)
			extract_dir = "./DEBIAN";
	}
	if (opt & (DPKG_DEB_OPT_EXTRACT_VERBOSE | DPKG_DEB_OPT_EXTRACT)) { // -Xx
		if (opt & DPKG_DEB_OPT_EXTRACT_VERBOSE)
			tar_archive->action_header = header_list;
		tar_archive->action_data = data_extract_all;
		if (!extract_dir)
			bb_show_usage();
	}

	/* Standard tool supports "-" */
	tar_archive->src_fd = ar_archive->src_fd = xopen_stdin(argv[0]);

	if (extract_dir) {
		mkdir(extract_dir, 0777); /* bb_make_directory(extract_dir, 0777, 0) */
		xchdir(extract_dir);
	}

	/* Do it */
	unpack_ar_archive(ar_archive);

	/* Cleanup */
	if (ENABLE_FEATURE_CLEAN_UP)
		close(ar_archive->src_fd);

	return EXIT_SUCCESS;
}
