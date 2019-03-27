/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include "bsdtar_platform.h"
#include <stdio.h>

#define	DEFAULT_BYTES_PER_BLOCK	(20*512)
#define ENV_READER_OPTIONS	"TAR_READER_OPTIONS"
#define ENV_WRITER_OPTIONS	"TAR_WRITER_OPTIONS"
#define IGNORE_WRONG_MODULE_NAME "__ignore_wrong_module_name__,"

struct creation_set;
/*
 * The internal state for the "bsdtar" program.
 *
 * Keeping all of the state in a structure like this simplifies memory
 * leak testing (at exit, anything left on the heap is suspect).  A
 * pointer to this structure is passed to most bsdtar internal
 * functions.
 */
struct bsdtar {
	/* Options */
	const char	 *filename; /* -f filename */
	char		 *pending_chdir; /* -C dir */
	const char	 *names_from_file; /* -T file */
	int		  bytes_per_block; /* -b block_size */
	int		  bytes_in_last_block; /* See -b handling. */
	int		  verbose;   /* -v */
	unsigned int	  flags; /* Bitfield of boolean options */
	int		  extract_flags; /* Flags for extract operation */
	int		  readdisk_flags; /* Flags for read disk operation */
	int		  strip_components; /* Remove this many leading dirs */
	int		  gid;  /* --gid */
	const char	 *gname; /* --gname */
	int		  uid;  /* --uid */
	const char	 *uname; /* --uname */
	const char	 *passphrase; /* --passphrase */
	char		  mode; /* Program mode: 'c', 't', 'r', 'u', 'x' */
	char		  symlink_mode; /* H or L, per BSD conventions */
	const char	 *option_options; /* --options */
	char		  day_first; /* show day before month in -tv output */
	struct creation_set *cset;

	/* Option parser state */
	int		  getopt_state;
	char		 *getopt_word;

	/* If >= 0, then close this when done. */
	int		  fd;

	/* Miscellaneous state information */
	int		  argc;
	char		**argv;
	const char	 *argument;
	size_t		  gs_width; /* For 'list_item' in read.c */
	size_t		  u_width; /* for 'list_item' in read.c */
	uid_t		  user_uid; /* UID running this program */
	int		  return_value; /* Value returned by main() */
	char		  warned_lead_slash; /* Already displayed warning */
	char		  next_line_is_dir; /* Used for -C parsing in -cT */

	/*
	 * Data for various subsystems.  Full definitions are located in
	 * the file where they are used.
	 */
	struct archive		*diskreader;	/* for write.c */
	struct archive_entry_linkresolver *resolver; /* for write.c */
	struct archive_dir	*archive_dir;	/* for write.c */
	struct name_cache	*gname_cache;	/* for write.c */
	char			*buff;		/* for write.c */
	size_t			 buff_size;	/* for write.c */
	int			 first_fs;	/* for write.c */
	struct archive		*matching;	/* for matching.c */
	struct security		*security;	/* for read.c */
	struct name_cache	*uname_cache;	/* for write.c */
	struct siginfo_data	*siginfo;	/* for siginfo.c */
	struct substitution	*substitution;	/* for subst.c */
	char			*ppbuff;	/* for util.c */
};

/* Options for flags bitfield */
#define	OPTFLAG_AUTO_COMPRESS	(0x00000001)	/* -a */
#define	OPTFLAG_ABSOLUTE_PATHS	(0x00000002)	/* -P */
#define	OPTFLAG_CHROOT		(0x00000004)	/* --chroot */
#define	OPTFLAG_FAST_READ	(0x00000008)	/* --fast-read */
#define	OPTFLAG_IGNORE_ZEROS	(0x00000010)	/* --ignore-zeros */
#define	OPTFLAG_INTERACTIVE	(0x00000020)	/* -w */
#define	OPTFLAG_NO_OWNER	(0x00000040)	/* -o */
#define	OPTFLAG_NO_SUBDIRS	(0x00000080)	/* -n */
#define	OPTFLAG_NULL		(0x00000100)	/* --null */
#define	OPTFLAG_NUMERIC_OWNER	(0x00000200)	/* --numeric-owner */
#define	OPTFLAG_O		(0x00000400)	/* -o */
#define	OPTFLAG_STDOUT		(0x00000800)	/* -O */
#define	OPTFLAG_TOTALS		(0x00001000)	/* --totals */
#define	OPTFLAG_UNLINK_FIRST	(0x00002000)	/* -U */
#define	OPTFLAG_WARN_LINKS	(0x00004000)	/* --check-links */
#define	OPTFLAG_NO_XATTRS	(0x00008000)	/* --no-xattrs */
#define	OPTFLAG_XATTRS		(0x00010000)	/* --xattrs */
#define	OPTFLAG_NO_ACLS		(0x00020000)	/* --no-acls */
#define	OPTFLAG_ACLS		(0x00040000)	/* --acls */
#define	OPTFLAG_NO_FFLAGS	(0x00080000)	/* --no-fflags */
#define	OPTFLAG_FFLAGS		(0x00100000)	/* --fflags */
#define	OPTFLAG_NO_MAC_METADATA	(0x00200000)	/* --no-mac-metadata */
#define	OPTFLAG_MAC_METADATA	(0x00400000)	/* --mac-metadata */

/* Fake short equivalents for long options that otherwise lack them. */
enum {
	OPTION_ACLS = 1,
	OPTION_B64ENCODE,
	OPTION_CHECK_LINKS,
	OPTION_CHROOT,
	OPTION_CLEAR_NOCHANGE_FFLAGS,
	OPTION_EXCLUDE,
	OPTION_FFLAGS,
	OPTION_FORMAT,
	OPTION_GID,
	OPTION_GNAME,
	OPTION_GRZIP,
	OPTION_HELP,
	OPTION_HFS_COMPRESSION,
	OPTION_IGNORE_ZEROS,
	OPTION_INCLUDE,
	OPTION_KEEP_NEWER_FILES,
	OPTION_LRZIP,
	OPTION_LZ4,
	OPTION_LZIP,
	OPTION_LZMA,
	OPTION_LZOP,
	OPTION_MAC_METADATA,
	OPTION_NEWER_CTIME,
	OPTION_NEWER_CTIME_THAN,
	OPTION_NEWER_MTIME,
	OPTION_NEWER_MTIME_THAN,
	OPTION_NODUMP,
	OPTION_NOPRESERVE_HFS_COMPRESSION,
	OPTION_NO_ACLS,
	OPTION_NO_FFLAGS,
	OPTION_NO_MAC_METADATA,
	OPTION_NO_SAME_OWNER,
	OPTION_NO_SAME_PERMISSIONS,
	OPTION_NO_XATTRS,
	OPTION_NULL,
	OPTION_NUMERIC_OWNER,
	OPTION_OLDER_CTIME,
	OPTION_OLDER_CTIME_THAN,
	OPTION_OLDER_MTIME,
	OPTION_OLDER_MTIME_THAN,
	OPTION_ONE_FILE_SYSTEM,
	OPTION_OPTIONS,
	OPTION_PASSPHRASE,
	OPTION_POSIX,
	OPTION_SAME_OWNER,
	OPTION_STRIP_COMPONENTS,
	OPTION_TOTALS,
	OPTION_UID,
	OPTION_UNAME,
	OPTION_USE_COMPRESS_PROGRAM,
	OPTION_UUENCODE,
	OPTION_VERSION,
	OPTION_XATTRS,
	OPTION_ZSTD,
};

int	bsdtar_getopt(struct bsdtar *);
void	do_chdir(struct bsdtar *);
int	edit_pathname(struct bsdtar *, struct archive_entry *);
int	need_report(void);
int	pathcmp(const char *a, const char *b);
void	safe_fprintf(FILE *, const char *fmt, ...) __LA_PRINTF(2, 3);
void	set_chdir(struct bsdtar *, const char *newdir);
const char *tar_i64toa(int64_t);
void	tar_mode_c(struct bsdtar *bsdtar);
void	tar_mode_r(struct bsdtar *bsdtar);
void	tar_mode_t(struct bsdtar *bsdtar);
void	tar_mode_u(struct bsdtar *bsdtar);
void	tar_mode_x(struct bsdtar *bsdtar);
void	usage(void) __LA_DEAD;
int	yes(const char *fmt, ...) __LA_PRINTF(1, 2);

#if defined(HAVE_REGEX_H) || defined(HAVE_PCREPOSIX_H)
void	add_substitution(struct bsdtar *, const char *);
int	apply_substitution(struct bsdtar *, const char *, char **, int, int);
void	cleanup_substitution(struct bsdtar *);
#endif

void		cset_add_filter(struct creation_set *, const char *);
void		cset_add_filter_program(struct creation_set *, const char *);
int		cset_auto_compress(struct creation_set *, const char *);
void		cset_free(struct creation_set *);
const char *	cset_get_format(struct creation_set *);
struct creation_set *cset_new(void);
int		cset_read_support_filter_program(struct creation_set *,
		    struct archive *);
void		cset_set_format(struct creation_set *, const char *);
int		cset_write_add_filters(struct creation_set *,
		    struct archive *, const void **);

const char * passphrase_callback(struct archive *, void *);
void	     passphrase_free(char *);
void	list_item_verbose(struct bsdtar *, FILE *,
		    struct archive_entry *);
