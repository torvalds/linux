/* vi: set sw=4 ts=4: */
/*
 * Mini cpio implementation for busybox
 *
 * Copyright (C) 2001 by Glenn McGrath
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Limitations:
 * Doesn't check CRC's
 * Only supports new ASCII and CRC formats
 */
//config:config CPIO
//config:	bool "cpio (14 kb)"
//config:	default y
//config:	help
//config:	cpio is an archival utility program used to create, modify, and
//config:	extract contents from archives.
//config:	cpio has 110 bytes of overheads for every stored file.
//config:
//config:	This implementation of cpio can extract cpio archives created in the
//config:	"newc" or "crc" format.
//config:
//config:	Unless you have a specific application which requires cpio, you
//config:	should probably say N here.
//config:
//config:config FEATURE_CPIO_O
//config:	bool "Support archive creation"
//config:	default y
//config:	depends on CPIO
//config:	help
//config:	This implementation of cpio can create cpio archives in the "newc"
//config:	format only.
//config:
//config:config FEATURE_CPIO_P
//config:	bool "Support passthrough mode"
//config:	default y
//config:	depends on FEATURE_CPIO_O
//config:	help
//config:	Passthrough mode. Rarely used.

//applet:IF_CPIO(APPLET(cpio, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_CPIO) += cpio.o

//usage:#define cpio_trivial_usage
//usage:       "[-dmvu] [-F FILE] [-R USER[:GRP]]" IF_FEATURE_CPIO_O(" [-H newc]")
//usage:       " [-ti"IF_FEATURE_CPIO_O("o")"]" IF_FEATURE_CPIO_P(" [-p DIR]")
//usage:       " [EXTR_FILE]..."
//usage:#define cpio_full_usage "\n\n"
//usage:       "Extract (-i) or list (-t) files from a cpio archive"
//usage:	IF_FEATURE_CPIO_O(", or"
//usage:     "\ntake file list from stdin and create an archive (-o)"
//usage:                IF_FEATURE_CPIO_P(" or copy files (-p)")
//usage:	)
//usage:     "\n"
//usage:     "\nMain operation mode:"
//usage:     "\n	-t	List"
//usage:     "\n	-i	Extract EXTR_FILEs (or all)"
//usage:	IF_FEATURE_CPIO_O(
//usage:     "\n	-o	Create (requires -H newc)"
//usage:	)
//usage:	IF_FEATURE_CPIO_P(
//usage:     "\n	-p DIR	Copy files to DIR"
//usage:	)
//usage:     "\nOptions:"
//usage:	IF_FEATURE_CPIO_O(
//usage:     "\n	-H newc	Archive format"
//usage:	)
//usage:     "\n	-d	Make leading directories"
//usage:     "\n	-m	Preserve mtime"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-u	Overwrite"
//usage:     "\n	-F FILE	Input (-t,-i,-p) or output (-o) file"
//usage:     "\n	-R USER[:GRP]	Set owner of created files"
//usage:     "\n	-L	Dereference symlinks"
//usage:     "\n	-0	Input is separated by NULs"

/* GNU cpio 2.9 --help (abridged):

 Modes:
  -t, --list                 List the archive
  -i, --extract              Extract files from an archive
  -o, --create               Create the archive
  -p, --pass-through         Copy-pass mode

 Options valid in any mode:
      --block-size=SIZE      I/O block size = SIZE * 512 bytes
  -B                         I/O block size = 5120 bytes
  -c                         Use the old portable (ASCII) archive format
  -C, --io-size=NUMBER       I/O block size in bytes
  -f, --nonmatching          Only copy files that do not match given pattern
  -F, --file=FILE            Use FILE instead of standard input or output
  -H, --format=FORMAT        Use given archive FORMAT
  -M, --message=STRING       Print STRING when the end of a volume of the
                             backup media is reached
  -n, --numeric-uid-gid      If -v, show numeric UID and GID
      --quiet                Do not print the number of blocks copied
      --rsh-command=COMMAND  Use remote COMMAND instead of rsh
  -v, --verbose              Verbosely list the files processed
  -V, --dot                  Print a "." for each file processed
  -W, --warning=FLAG         Control warning display: 'none','truncate','all';
                             multiple options accumulate

 Options valid only in --extract mode:
  -b, --swap                 Swap both halfwords of words and bytes of
                             halfwords in the data (equivalent to -sS)
  -r, --rename               Interactively rename files
  -s, --swap-bytes           Swap the bytes of each halfword in the files
  -S, --swap-halfwords       Swap the halfwords of each word (4 bytes)
      --to-stdout            Extract files to standard output
  -E, --pattern-file=FILE    Read additional patterns specifying filenames to
                             extract or list from FILE
      --only-verify-crc      Verify CRC's, don't actually extract the files

 Options valid only in --create mode:
  -A, --append               Append to an existing archive
  -O FILE                    File to use instead of standard output

 Options valid only in --pass-through mode:
  -l, --link                 Link files instead of copying them, when possible

 Options valid in --extract and --create modes:
      --absolute-filenames   Do not strip file system prefix components from
                             the file names
      --no-absolute-filenames Create all files relative to the current dir

 Options valid in --create and --pass-through modes:
  -0, --null                 A list of filenames is terminated by a NUL
  -a, --reset-access-time    Reset the access times of files after reading them
  -I FILE                    File to use instead of standard input
  -L, --dereference          Dereference symbolic links (copy the files
                             that they point to instead of copying the links)
  -R, --owner=[USER][:.][GRP] Set owner of created files

 Options valid in --extract and --pass-through modes:
  -d, --make-directories     Create leading directories where needed
  -m, --preserve-modification-time  Retain mtime when creating files
      --no-preserve-owner    Do not change the ownership of the files
      --sparse               Write files with blocks of zeros as sparse files
  -u, --unconditional        Replace all files unconditionally
 */

#include "libbb.h"
#include "common_bufsiz.h"
#include "bb_archive.h"

enum {
	OPT_EXTRACT            = (1 << 0),
	OPT_TEST               = (1 << 1),
	OPT_NUL_TERMINATED     = (1 << 2),
	OPT_UNCONDITIONAL      = (1 << 3),
	OPT_VERBOSE            = (1 << 4),
	OPT_CREATE_LEADING_DIR = (1 << 5),
	OPT_PRESERVE_MTIME     = (1 << 6),
	OPT_DEREF              = (1 << 7),
	OPT_FILE               = (1 << 8),
	OPT_OWNER              = (1 << 9),
	OPTBIT_OWNER = 9,
	IF_FEATURE_CPIO_O(OPTBIT_CREATE     ,)
	IF_FEATURE_CPIO_O(OPTBIT_FORMAT     ,)
	IF_FEATURE_CPIO_P(OPTBIT_PASSTHROUGH,)
	IF_LONG_OPTS(     OPTBIT_QUIET      ,)
	IF_LONG_OPTS(     OPTBIT_2STDOUT    ,)
	OPT_CREATE             = IF_FEATURE_CPIO_O((1 << OPTBIT_CREATE     )) + 0,
	OPT_FORMAT             = IF_FEATURE_CPIO_O((1 << OPTBIT_FORMAT     )) + 0,
	OPT_PASSTHROUGH        = IF_FEATURE_CPIO_P((1 << OPTBIT_PASSTHROUGH)) + 0,
	OPT_QUIET              = IF_LONG_OPTS(     (1 << OPTBIT_QUIET      )) + 0,
	OPT_2STDOUT            = IF_LONG_OPTS(     (1 << OPTBIT_2STDOUT    )) + 0,
};

#define OPTION_STR "it0uvdmLF:R:"

struct globals {
	struct bb_uidgid_t owner_ugid;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
void BUG_cpio_globals_too_big(void);
#define INIT_G() do { \
	setup_common_bufsiz(); \
	G.owner_ugid.uid = -1L; \
	G.owner_ugid.gid = -1L; \
} while (0)

#if ENABLE_FEATURE_CPIO_O
static off_t cpio_pad4(off_t size)
{
	int i;

	i = (- size) & 3;
	size += i;
	while (--i >= 0)
		bb_putchar('\0');
	return size;
}

/* Return value will become exit code.
 * It's ok to exit instead of return. */
static NOINLINE int cpio_o(void)
{
	struct name_s {
		struct name_s *next;
		char name[1];
	};
	struct inodes_s {
		struct inodes_s *next;
		struct name_s *names;
		struct stat st;
	};

	struct inodes_s *links = NULL;
	off_t bytes = 0; /* output bytes count */

	while (1) {
		const char *name;
		char *line;
		struct stat st;

		line = (option_mask32 & OPT_NUL_TERMINATED)
				? bb_get_chunk_from_file(stdin, NULL)
				: xmalloc_fgetline(stdin);

		if (line) {
			/* Strip leading "./[./]..." from the filename */
			name = line;
			while (name[0] == '.' && name[1] == '/') {
				while (*++name == '/')
					continue;
			}
			if (!*name) { /* line is empty */
				free(line);
				continue;
			}
			if ((option_mask32 & OPT_DEREF)
					? stat(name, &st)
					: lstat(name, &st)
			) {
 abort_cpio_o:
				bb_simple_perror_msg_and_die(name);
			}

			if (G.owner_ugid.uid != (uid_t)-1L)
				st.st_uid = G.owner_ugid.uid;
			if (G.owner_ugid.gid != (gid_t)-1L)
				st.st_gid = G.owner_ugid.gid;

			if (!(S_ISLNK(st.st_mode) || S_ISREG(st.st_mode)))
				st.st_size = 0; /* paranoia */

			/* Store hardlinks for later processing, dont output them */
			if (!S_ISDIR(st.st_mode) && st.st_nlink > 1) {
				struct name_s *n;
				struct inodes_s *l;

				/* Do we have this hardlink remembered? */
				l = links;
				while (1) {
					if (l == NULL) {
						/* Not found: add new item to "links" list */
						l = xzalloc(sizeof(*l));
						l->st = st;
						l->next = links;
						links = l;
						break;
					}
					if (l->st.st_ino == st.st_ino) {
						/* found */
						break;
					}
					l = l->next;
				}
				/* Add new name to "l->names" list */
				n = xmalloc(sizeof(*n) + strlen(name));
				strcpy(n->name, name);
				n->next = l->names;
				l->names = n;

				free(line);
				continue;
			}
		} else { /* line == NULL: EOF */
 next_link:
			if (links) {
				/* Output hardlink's data */
				st = links->st;
				name = links->names->name;
				links->names = links->names->next;
				/* GNU cpio is reported to emit file data
				 * only for the last instance. Mimic that. */
				if (links->names == NULL)
					links = links->next;
				else
					st.st_size = 0;
				/* NB: we leak links->names and/or links,
				 * this is intended (we exit soon anyway) */
			} else {
				/* If no (more) hardlinks to output,
				 * output "trailer" entry */
				name = cpio_TRAILER;
				/* st.st_size == 0 is a must, but for uniformity
				 * in the output, we zero out everything */
				memset(&st, 0, sizeof(st));
				/* st.st_nlink = 1; - GNU cpio does this */
			}
		}

		bytes += printf("070701"
				"%08X%08X%08X%08X%08X%08X%08X"
				"%08X%08X%08X%08X" /* GNU cpio uses uppercase hex */
				/* strlen+1: */ "%08X"
				/* chksum: */   "00000000" /* (only for "070702" files) */
				/* name,NUL: */ "%s%c",
				(unsigned)(uint32_t) st.st_ino,
				(unsigned)(uint32_t) st.st_mode,
				(unsigned)(uint32_t) st.st_uid,
				(unsigned)(uint32_t) st.st_gid,
				(unsigned)(uint32_t) st.st_nlink,
				(unsigned)(uint32_t) st.st_mtime,
				(unsigned)(uint32_t) st.st_size,
				(unsigned)(uint32_t) major(st.st_dev),
				(unsigned)(uint32_t) minor(st.st_dev),
				(unsigned)(uint32_t) major(st.st_rdev),
				(unsigned)(uint32_t) minor(st.st_rdev),
				(unsigned)(strlen(name) + 1),
				name, '\0');
		bytes = cpio_pad4(bytes);

		if (st.st_size) {
			if (S_ISLNK(st.st_mode)) {
				char *lpath = xmalloc_readlink_or_warn(name);
				if (!lpath)
					goto abort_cpio_o;
				bytes += printf("%s", lpath);
				free(lpath);
			} else { /* S_ISREG */
				int fd = xopen(name, O_RDONLY);
				fflush_all();
				/* We must abort if file got shorter too! */
				bb_copyfd_exact_size(fd, STDOUT_FILENO, st.st_size);
				bytes += st.st_size;
				close(fd);
			}
			bytes = cpio_pad4(bytes);
		}

		if (!line) {
			if (name != cpio_TRAILER)
				goto next_link;
			/* TODO: GNU cpio pads trailer to 512 bytes, do we want that? */
			return EXIT_SUCCESS;
		}

		free(line);
	} /* end of "while (1)" */
}
#endif

int cpio_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int cpio_main(int argc UNUSED_PARAM, char **argv)
{
	archive_handle_t *archive_handle;
	char *cpio_filename;
	char *cpio_owner;
	IF_FEATURE_CPIO_O(const char *cpio_fmt = "";)
	unsigned opt;
#if ENABLE_LONG_OPTS
	const char *long_opts =
		"extract\0"      No_argument       "i"
		"list\0"         No_argument       "t"
#if ENABLE_FEATURE_CPIO_O
		"create\0"       No_argument       "o"
		"format\0"       Required_argument "H"
#if ENABLE_FEATURE_CPIO_P
		"pass-through\0" No_argument       "p"
#endif
#endif
		"owner\0"        Required_argument "R"
		"verbose\0"      No_argument       "v"
		"null\0"         No_argument       "0"
		"quiet\0"        No_argument       "\xff"
		"to-stdout\0"    No_argument       "\xfe"
		;
#endif

	INIT_G();
	archive_handle = init_handle();
	/* archive_handle->src_fd = STDIN_FILENO; - done by init_handle */
	archive_handle->ah_flags = ARCHIVE_EXTRACT_NEWER;

	/* As of now we do not enforce this: */
	/* -i,-t,-o,-p are mutually exclusive */
	/* -u,-d,-m make sense only with -i or -p */
	/* -L makes sense only with -o or -p */

#if !ENABLE_FEATURE_CPIO_O
	opt = getopt32long(argv, OPTION_STR, long_opts, &cpio_filename, &cpio_owner);
#else
	opt = getopt32long(argv, OPTION_STR "oH:" IF_FEATURE_CPIO_P("p"), long_opts,
		       &cpio_filename, &cpio_owner, &cpio_fmt);
#endif
	argv += optind;
	if (opt & OPT_OWNER) { /* -R */
		parse_chown_usergroup_or_die(&G.owner_ugid, cpio_owner);
		archive_handle->cpio__owner = G.owner_ugid;
	}
#if !ENABLE_FEATURE_CPIO_O
	if (opt & OPT_FILE) { /* -F */
		xmove_fd(xopen(cpio_filename, O_RDONLY), STDIN_FILENO);
	}
#else
	if ((opt & (OPT_FILE|OPT_CREATE)) == OPT_FILE) { /* -F without -o */
		xmove_fd(xopen(cpio_filename, O_RDONLY), STDIN_FILENO);
	}
	if (opt & OPT_PASSTHROUGH) {
		pid_t pid;
		struct fd_pair pp;

		if (argv[0] == NULL)
			bb_show_usage();
		if (opt & OPT_CREATE_LEADING_DIR)
			mkdir(argv[0], 0777);
		/* Crude existence check:
		 * close(xopen(argv[0], O_RDONLY | O_DIRECTORY));
		 * We can also xopen, fstat, IS_DIR, later fchdir.
		 * This would check for existence earlier and cleaner.
		 * As it stands now, if we fail xchdir later,
		 * child dies on EPIPE, unless it caught
		 * a diffrerent problem earlier.
		 * This is good enough for now.
		 */
#if !BB_MMU
		pp.rd = 3;
		pp.wr = 4;
		if (!re_execed) {
			close(3);
			close(4);
			xpiped_pair(pp);
		}
#else
		xpiped_pair(pp);
#endif
		pid = fork_or_rexec(argv - optind);
		if (pid == 0) { /* child */
			close(pp.rd);
			xmove_fd(pp.wr, STDOUT_FILENO);
			goto dump;
		}
		/* parent */
		USE_FOR_NOMMU(argv[-optind][0] &= 0x7f); /* undo fork_or_rexec() damage */
		xchdir(*argv++);
		close(pp.wr);
		xmove_fd(pp.rd, STDIN_FILENO);
		//opt &= ~OPT_PASSTHROUGH;
		opt |= OPT_EXTRACT;
		goto skip;
	}
	/* -o */
	if (opt & OPT_CREATE) {
		if (cpio_fmt[0] != 'n') /* we _require_ "-H newc" */
			bb_show_usage();
		if (opt & OPT_FILE) {
			xmove_fd(xopen(cpio_filename, O_WRONLY | O_CREAT | O_TRUNC), STDOUT_FILENO);
		}
 dump:
		return cpio_o();
	}
 skip:
#endif

	/* One of either extract or test options must be given */
	if ((opt & (OPT_TEST | OPT_EXTRACT)) == 0) {
		bb_show_usage();
	}

	if (opt & OPT_TEST) {
		/* if both extract and test options are given, ignore extract option */
		opt &= ~OPT_EXTRACT;
		archive_handle->action_header = header_list;
	}
	if (opt & OPT_EXTRACT) {
		archive_handle->action_data = data_extract_all;
		if (opt & OPT_2STDOUT)
			archive_handle->action_data = data_extract_to_stdout;
	}
	if (opt & OPT_UNCONDITIONAL) {
		archive_handle->ah_flags |= ARCHIVE_UNLINK_OLD;
		archive_handle->ah_flags &= ~ARCHIVE_EXTRACT_NEWER;
	}
	if (opt & OPT_VERBOSE) {
		if (archive_handle->action_header == header_list) {
			archive_handle->action_header = header_verbose_list;
		} else {
			archive_handle->action_header = header_list;
		}
	}
	if (opt & OPT_CREATE_LEADING_DIR) {
		archive_handle->ah_flags |= ARCHIVE_CREATE_LEADING_DIRS;
	}
	if (opt & OPT_PRESERVE_MTIME) {
		archive_handle->ah_flags |= ARCHIVE_RESTORE_DATE;
	}

	while (*argv) {
		archive_handle->filter = filter_accept_list;
		llist_add_to(&archive_handle->accept, *argv);
		argv++;
	}

	/* see get_header_cpio */
	archive_handle->cpio__blocks = (off_t)-1;
	while (get_header_cpio(archive_handle) == EXIT_SUCCESS)
		continue;

	create_links_from_list(archive_handle->link_placeholders);

	if (archive_handle->cpio__blocks != (off_t)-1
	 && !(opt & OPT_QUIET)
	) {
		fprintf(stderr, "%"OFF_FMT"u blocks\n", archive_handle->cpio__blocks);
	}

	return EXIT_SUCCESS;
}
