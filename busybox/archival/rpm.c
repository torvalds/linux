/* vi: set sw=4 ts=4: */
/*
 * Mini rpm applet for busybox
 *
 * Copyright (C) 2001,2002 by Laurence Anderson
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config RPM
//config:	bool "rpm (33 kb)"
//config:	default y
//config:	help
//config:	Mini RPM applet - queries and extracts RPM packages.

//applet:IF_RPM(APPLET(rpm, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RPM) += rpm.o

#include "libbb.h"
#include "common_bufsiz.h"
#include "bb_archive.h"
#include "rpm.h"

#define RPM_CHAR_TYPE           1
#define RPM_INT8_TYPE           2
#define RPM_INT16_TYPE          3
#define RPM_INT32_TYPE          4
/* #define RPM_INT64_TYPE       5   ---- These aren't supported (yet) */
#define RPM_STRING_TYPE         6
#define RPM_BIN_TYPE            7
#define RPM_STRING_ARRAY_TYPE   8
#define RPM_I18NSTRING_TYPE     9

#define TAG_NAME                1000
#define TAG_VERSION             1001
#define TAG_RELEASE             1002
#define TAG_SUMMARY             1004
#define TAG_DESCRIPTION         1005
#define TAG_BUILDTIME           1006
#define TAG_BUILDHOST           1007
#define TAG_SIZE                1009
#define TAG_VENDOR              1011
#define TAG_LICENSE             1014
#define TAG_PACKAGER            1015
#define TAG_GROUP               1016
#define TAG_URL                 1020
#define TAG_PREIN               1023
#define TAG_POSTIN              1024
#define TAG_FILEFLAGS           1037
#define TAG_FILEUSERNAME        1039
#define TAG_FILEGROUPNAME       1040
#define TAG_SOURCERPM           1044
#define TAG_PREINPROG           1085
#define TAG_POSTINPROG          1086
#define TAG_PREFIXS             1098
#define TAG_DIRINDEXES          1116
#define TAG_BASENAMES           1117
#define TAG_DIRNAMES            1118
#define TAG_PAYLOADCOMPRESSOR   1125


#define RPMFILE_CONFIG          (1 << 0)
#define RPMFILE_DOC             (1 << 1)

enum rpm_functions_e {
	rpm_query = 1,
	rpm_install = 2,
	rpm_query_info = 4,
	rpm_query_package = 8,
	rpm_query_list = 16,
	rpm_query_list_doc = 32,
	rpm_query_list_config = 64
};

typedef struct {
	uint32_t tag; /* 4 byte tag */
	uint32_t type; /* 4 byte type */
	uint32_t offset; /* 4 byte offset */
	uint32_t count; /* 4 byte count */
} rpm_index;

struct globals {
	void *map;
	rpm_index *mytags;
	int tagcount;
	unsigned mapsize, pagesize;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static int rpm_gettags(const char *filename)
{
	rpm_index *tags;
	int fd;
	unsigned pass, idx;
	unsigned storepos;

	if (!filename) { /* rpm2cpio w/o filename? */
		filename = bb_msg_standard_output;
		fd = 0;
	} else {
		fd = xopen(filename, O_RDONLY);
	}

	storepos = xlseek(fd, 96, SEEK_CUR); /* Seek past the unused lead */
	G.tagcount = 0;
	tags = NULL;
	idx = 0;
	/* 1st pass is the signature headers, 2nd is the main stuff */
	for (pass = 0; pass < 2; pass++) {
		struct rpm_header header;
		unsigned cnt;

		xread(fd, &header, sizeof(header));
		if (header.magic_and_ver != htonl(RPM_HEADER_MAGICnVER))
			bb_error_msg_and_die("invalid RPM header magic in '%s'", filename);
		header.size = ntohl(header.size);
		cnt = ntohl(header.entries);
		storepos += sizeof(header) + cnt * 16;

		G.tagcount += cnt;
		tags = xrealloc(tags, sizeof(tags[0]) * G.tagcount);
		xread(fd, &tags[idx], sizeof(tags[0]) * cnt);
		while (cnt--) {
			rpm_index *tag = &tags[idx];
			tag->tag = ntohl(tag->tag);
			tag->type = ntohl(tag->type);
			tag->count = ntohl(tag->count);
			tag->offset = storepos + ntohl(tag->offset);
			if (pass == 0)
				tag->tag -= 743;
			idx++;
		}
		/* Skip padding to 8 byte boundary after reading signature headers */
		if (pass == 0)
			while (header.size & 7)
				header.size++;
		/* Seek past store */
		storepos = xlseek(fd, header.size, SEEK_CUR);
	}
	G.mytags = tags;

	/* Map the store */
	storepos = (storepos + G.pagesize) & -(int)G.pagesize;
	/* remember size for munmap */
	G.mapsize = storepos;
	/* some NOMMU systems prefer MAP_PRIVATE over MAP_SHARED */
	G.map = mmap(0, storepos, PROT_READ, MAP_PRIVATE, fd, 0);
	if (G.map == MAP_FAILED)
		bb_perror_msg_and_die("mmap '%s'", filename);

	return fd;
}

static int bsearch_rpmtag(const void *key, const void *item)
{
	int *tag = (int *)key;
	rpm_index *tmp = (rpm_index *) item;
	return (*tag - tmp->tag);
}

static char *rpm_getstr(int tag, int itemindex)
{
	rpm_index *found;
	found = bsearch(&tag, G.mytags, G.tagcount, sizeof(G.mytags[0]), bsearch_rpmtag);
	if (!found || itemindex >= found->count)
		return NULL;
	if (found->type == RPM_STRING_TYPE
	 || found->type == RPM_I18NSTRING_TYPE
	 || found->type == RPM_STRING_ARRAY_TYPE
	) {
		int n;
		char *tmpstr = (char *) G.map + found->offset;
		for (n = 0; n < itemindex; n++)
			tmpstr = tmpstr + strlen(tmpstr) + 1;
		return tmpstr;
	}
	return NULL;
}
static char *rpm_getstr0(int tag)
{
	return rpm_getstr(tag, 0);
}

#if ENABLE_RPM

static int rpm_getint(int tag, int itemindex)
{
	rpm_index *found;
	char *tmpint;

	/* gcc throws warnings here when sizeof(void*)!=sizeof(int) ...
	 * it's ok to ignore it because tag won't be used as a pointer */
	found = bsearch(&tag, G.mytags, G.tagcount, sizeof(G.mytags[0]), bsearch_rpmtag);
	if (!found || itemindex >= found->count)
		return -1;

	tmpint = (char *) G.map + found->offset;
	if (found->type == RPM_INT32_TYPE) {
		tmpint += itemindex*4;
		return ntohl(*(int32_t*)tmpint);
	}
	if (found->type == RPM_INT16_TYPE) {
		tmpint += itemindex*2;
		return ntohs(*(int16_t*)tmpint);
	}
	if (found->type == RPM_INT8_TYPE) {
		tmpint += itemindex;
		return *(int8_t*)tmpint;
	}
	return -1;
}

static int rpm_getcount(int tag)
{
	rpm_index *found;
	found = bsearch(&tag, G.mytags, G.tagcount, sizeof(G.mytags[0]), bsearch_rpmtag);
	if (!found)
		return 0;
	return found->count;
}

static void fileaction_dobackup(char *filename, int fileref)
{
	struct stat oldfile;
	int stat_res;
	char *newname;
	if (rpm_getint(TAG_FILEFLAGS, fileref) & RPMFILE_CONFIG) {
		/* Only need to backup config files */
		stat_res = lstat(filename, &oldfile);
		if (stat_res == 0 && S_ISREG(oldfile.st_mode)) {
			/* File already exists  - really should check MD5's etc to see if different */
			newname = xasprintf("%s.rpmorig", filename);
			copy_file(filename, newname, FILEUTILS_RECUR | FILEUTILS_PRESERVE_STATUS);
			remove_file(filename, FILEUTILS_RECUR | FILEUTILS_FORCE);
			free(newname);
		}
	}
}

static void fileaction_setowngrp(char *filename, int fileref)
{
	/* real rpm warns: "user foo does not exist - using <you>" */
	struct passwd *pw = getpwnam(rpm_getstr(TAG_FILEUSERNAME, fileref));
	int uid = pw ? pw->pw_uid : getuid(); /* or euid? */
	struct group *gr = getgrnam(rpm_getstr(TAG_FILEGROUPNAME, fileref));
	int gid = gr ? gr->gr_gid : getgid();
	chown(filename, uid, gid);
}

static void loop_through_files(int filetag, void (*fileaction)(char *filename, int fileref))
{
	int count = 0;
	while (rpm_getstr(filetag, count)) {
		char* filename = xasprintf("%s%s",
			rpm_getstr(TAG_DIRNAMES, rpm_getint(TAG_DIRINDEXES, count)),
			rpm_getstr(TAG_BASENAMES, count));
		fileaction(filename, count++);
		free(filename);
	}
}

#if 0 //DEBUG
static void print_all_tags(void)
{
	unsigned i = 0;
	while (i < G.tagcount) {
		rpm_index *tag = &G.mytags[i];
		if (tag->type == RPM_STRING_TYPE
		 || tag->type == RPM_I18NSTRING_TYPE
		 || tag->type == RPM_STRING_ARRAY_TYPE
		) {
			unsigned n;
			char *str = (char *) G.map + tag->offset;

			printf("tag[%u] %08x type %08x offset %08x count %d '%s'\n",
				i, tag->tag, tag->type, tag->offset, tag->count, str
			);
			for (n = 1; n < tag->count; n++) {
				str += strlen(str) + 1;
				printf("\t'%s'\n", str);
			}
		}
		i++;
	}
}
#else
#define print_all_tags() ((void)0)
#endif

static void extract_cpio(int fd, const char *source_rpm)
{
	archive_handle_t *archive_handle;

	if (source_rpm != NULL) {
		/* Binary rpm (it was built from some SRPM), install to root */
		xchdir("/");
	} /* else: SRPM, install to current dir */

	/* Initialize */
	archive_handle = init_handle();
	archive_handle->seek = seek_by_read;
	archive_handle->action_data = data_extract_all;
#if 0 /* For testing (rpm -i only lists the files in internal cpio): */
	archive_handle->action_header = header_list;
	archive_handle->action_data = data_skip;
#endif
	archive_handle->ah_flags = ARCHIVE_RESTORE_DATE | ARCHIVE_CREATE_LEADING_DIRS
		/* compat: overwrite existing files.
		 * try "rpm -i foo.src.rpm" few times in a row -
		 * standard rpm will not complain.
		 */
		| ARCHIVE_REPLACE_VIA_RENAME;
	archive_handle->src_fd = fd;
	/*archive_handle->offset = 0; - init_handle() did it */

	setup_unzip_on_fd(archive_handle->src_fd, /*fail_if_not_compressed:*/ 1);
	while (get_header_cpio(archive_handle) == EXIT_SUCCESS)
		continue;
}

//usage:#define rpm_trivial_usage
//usage:       "-i PACKAGE.rpm; rpm -qp[ildc] PACKAGE.rpm"
//usage:#define rpm_full_usage "\n\n"
//usage:       "Manipulate RPM packages\n"
//usage:     "\nCommands:"
//usage:     "\n	-i	Install package"
//usage:     "\n	-qp	Query package"
//usage:     "\n	-qpi	Show information"
//usage:     "\n	-qpl	List contents"
//usage:     "\n	-qpd	List documents"
//usage:     "\n	-qpc	List config files"

/* RPM version 4.13.0.1:
 * Unlike -q, -i seems to imply -p: -i, -ip and -pi work the same.
 * OTOH, with -q order is important: "-piq FILE.rpm" works as -qp, not -qpi
 * (IOW: shows only package name, not package info).
 * "-iq ARG" works as -q: treats ARG as package name, not a file.
 *
 * "man rpm" on -l option and options implying it:
 * -l, --list		List files in package.
 * -c, --configfiles	List only configuration files (implies -l).
 * -d, --docfiles	List only documentation files (implies -l).
 * -L, --licensefiles	List only license files (implies -l).
 * --dump	Dump file information as follows (implies -l):
 *		path size mtime digest mode owner group isconfig isdoc rdev symlink
 * -s, --state	Display the states of files in the package (implies -l).
 *		The state of each file is one of normal, not installed, or replaced.
 *
 * Looks like we can switch to getopt32 here: in practice, people
 * do place -q first if they intend to use it (misinterpreting "-piq" wouldn't matter).
 */
int rpm_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rpm_main(int argc, char **argv)
{
	int opt, func = 0;

	INIT_G();
	G.pagesize = getpagesize();

	while ((opt = getopt(argc, argv, "iqpldc")) != -1) {
		switch (opt) {
		case 'i': /* First arg: Install mode, with q: Information */
			if (!func) func = rpm_install;
			else func |= rpm_query_info;
			break;
		case 'q': /* First arg: Query mode */
			if (func) bb_show_usage();
			func = rpm_query;
			break;
		case 'p': /* Query a package (IOW: .rpm file, we are not querying RPMDB) */
			func |= rpm_query_package;
			break;
		case 'l': /* List files in a package */
			func |= rpm_query_list;
			break;
		case 'd': /* List doc files in a package (implies -l) */
			func |= rpm_query_list;
			func |= rpm_query_list_doc;
			break;
		case 'c': /* List config files in a package (implies -l) */
			func |= rpm_query_list;
			func |= rpm_query_list_config;
			break;
		default:
			bb_show_usage();
		}
	}
	argv += optind;
	//argc -= optind;
	if (!argv[0]) {
		bb_show_usage();
	}

	for (;;) {
		int rpm_fd;
		const char *source_rpm;

		rpm_fd = rpm_gettags(*argv);
		print_all_tags();

		source_rpm = rpm_getstr0(TAG_SOURCERPM);

		if (func & rpm_install) {
			/* -i (and not -qi) */

			/* Backup any config files */
			loop_through_files(TAG_BASENAMES, fileaction_dobackup);
			/* Extact the archive */
			extract_cpio(rpm_fd, source_rpm);
			/* Set the correct file uid/gid's */
			loop_through_files(TAG_BASENAMES, fileaction_setowngrp);
		}
		else
		if ((func & (rpm_query|rpm_query_package)) == (rpm_query|rpm_query_package)) {
			/* -qp */

			if (!(func & (rpm_query_info|rpm_query_list))) {
				/* If just a straight query, just give package name */
				printf("%s-%s-%s\n", rpm_getstr0(TAG_NAME), rpm_getstr0(TAG_VERSION), rpm_getstr0(TAG_RELEASE));
			}
			if (func & rpm_query_info) {
				/* Do the nice printout */
				time_t bdate_time;
				struct tm *bdate_ptm;
				char bdatestring[50];
				const char *p;

				printf("%-12s: %s\n", "Name"        , rpm_getstr0(TAG_NAME));
				/* TODO compat: add "Epoch" here */
				printf("%-12s: %s\n", "Version"     , rpm_getstr0(TAG_VERSION));
				printf("%-12s: %s\n", "Release"     , rpm_getstr0(TAG_RELEASE));
				/* add "Architecture" */
				/* printf("%-12s: %s\n", "Install Date", "(not installed)"); - we don't know */
				printf("%-12s: %s\n", "Group"       , rpm_getstr0(TAG_GROUP));
				printf("%-12s: %d\n", "Size"        , rpm_getint(TAG_SIZE, 0));
				printf("%-12s: %s\n", "License"     , rpm_getstr0(TAG_LICENSE));
				/* add "Signature" */
				printf("%-12s: %s\n", "Source RPM"  , source_rpm ? source_rpm : "(none)");
				bdate_time = rpm_getint(TAG_BUILDTIME, 0);
				bdate_ptm = localtime(&bdate_time);
				strftime(bdatestring, 50, "%a %d %b %Y %T %Z", bdate_ptm);
				printf("%-12s: %s\n", "Build Date"  , bdatestring);
				printf("%-12s: %s\n", "Build Host"  , rpm_getstr0(TAG_BUILDHOST));
				p = rpm_getstr0(TAG_PREFIXS);
				printf("%-12s: %s\n", "Relocations" , p ? p : "(not relocatable)");
				/* add "Packager" */
				p = rpm_getstr0(TAG_VENDOR);
				if (p) /* rpm 4.13.0.1 does not show "(none)" for Vendor: */
				printf("%-12s: %s\n", "Vendor"      , p);
				p = rpm_getstr0(TAG_URL);
				if (p) /* rpm 4.13.0.1 does not show "(none)"/"(null)" for URL: */
				printf("%-12s: %s\n", "URL"         , p);
				printf("%-12s: %s\n", "Summary"     , rpm_getstr0(TAG_SUMMARY));
				printf("Description :\n%s\n", rpm_getstr0(TAG_DESCRIPTION));
			}
			if (func & rpm_query_list) {
				int count, it, flags;
				count = rpm_getcount(TAG_BASENAMES);
				for (it = 0; it < count; it++) {
					flags = rpm_getint(TAG_FILEFLAGS, it);
					switch (func & (rpm_query_list_doc|rpm_query_list_config)) {
					case rpm_query_list_doc:
						if (!(flags & RPMFILE_DOC)) continue;
						break;
					case rpm_query_list_config:
						if (!(flags & RPMFILE_CONFIG)) continue;
						break;
					case rpm_query_list_doc|rpm_query_list_config:
						if (!(flags & (RPMFILE_CONFIG|RPMFILE_DOC))) continue;
						break;
					}
					printf("%s%s\n",
						rpm_getstr(TAG_DIRNAMES, rpm_getint(TAG_DIRINDEXES, it)),
						rpm_getstr(TAG_BASENAMES, it));
				}
			}
		} else {
			/* Unsupported (help text shows what we support) */
			bb_show_usage();
		}
		if (!*++argv)
			break;
		munmap(G.map, G.mapsize);
		free(G.mytags);
		close(rpm_fd);
	}

	return 0;
}

#endif /* RPM */

/*
 * Mini rpm2cpio implementation for busybox
 *
 * Copyright (C) 2001 by Laurence Anderson
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config RPM2CPIO
//config:	bool "rpm2cpio (20 kb)"
//config:	default y
//config:	help
//config:	Converts a RPM file into a CPIO archive.

//applet:IF_RPM2CPIO(APPLET(rpm2cpio, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RPM2CPIO) += rpm.o

//usage:#define rpm2cpio_trivial_usage
//usage:       "PACKAGE.rpm"
//usage:#define rpm2cpio_full_usage "\n\n"
//usage:       "Output a cpio archive of the rpm file"

#if ENABLE_RPM2CPIO

/* No getopt required */
int rpm2cpio_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rpm2cpio_main(int argc UNUSED_PARAM, char **argv)
{
	const char *str;
	int rpm_fd;

	INIT_G();
	G.pagesize = getpagesize();

	rpm_fd = rpm_gettags(argv[1]);

	//if (SEAMLESS_COMPRESSION) - we do this at the end instead.
	//	/* We need to know whether child (gzip/bzip/etc) exits abnormally */
	//	signal(SIGCHLD, check_errors_in_children);

	if (ENABLE_FEATURE_SEAMLESS_LZMA
	 && (str = rpm_getstr0(TAG_PAYLOADCOMPRESSOR)) != NULL
	 && strcmp(str, "lzma") == 0
	) {
		// lzma compression can't be detected
		// set up decompressor without detection
		setup_lzma_on_fd(rpm_fd);
	} else {
		setup_unzip_on_fd(rpm_fd, /*fail_if_not_compressed:*/ 1);
	}

	if (bb_copyfd_eof(rpm_fd, STDOUT_FILENO) < 0)
		bb_error_msg_and_die("error unpacking");

	if (ENABLE_FEATURE_CLEAN_UP) {
		close(rpm_fd);
	}

	if (SEAMLESS_COMPRESSION) {
		check_errors_in_children(0);
		return bb_got_signal;
	}
	return EXIT_SUCCESS;
}

#endif /* RPM2CPIO */
