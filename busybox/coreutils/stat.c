/* vi: set sw=4 ts=4: */
/*
 * stat -- display file or file system status
 *
 * Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation.
 * Copyright (C) 2005 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2005 by Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2006 by Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * Written by Michael Meskes
 * Taken from coreutils and turned into a busybox applet by Mike Frysinger
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config STAT
//config:	bool "stat (10 kb)"
//config:	default y
//config:	help
//config:	display file or filesystem status.
//config:
//config:config FEATURE_STAT_FORMAT
//config:	bool "Enable custom formats (-c)"
//config:	default y
//config:	depends on STAT
//config:	help
//config:	Without this, stat will not support the '-c format' option where
//config:	users can pass a custom format string for output. This adds about
//config:	7k to a nonstatic build on amd64.
//config:
//config:config FEATURE_STAT_FILESYSTEM
//config:	bool "Enable display of filesystem status (-f)"
//config:	default y
//config:	depends on STAT
//config:	select PLATFORM_LINUX # statfs()
//config:	help
//config:	Without this, stat will not support the '-f' option to display
//config:	information about filesystem status.

//applet:IF_STAT(APPLET_NOEXEC(stat, stat, BB_DIR_BIN, BB_SUID_DROP, stat))

//kbuild:lib-$(CONFIG_STAT) += stat.o

//usage:#define stat_trivial_usage
//usage:       "[OPTIONS] FILE..."
//usage:#define stat_full_usage "\n\n"
//usage:       "Display file"
//usage:            IF_FEATURE_STAT_FILESYSTEM(" (default) or filesystem")
//usage:            " status\n"
//usage:	IF_FEATURE_STAT_FORMAT(
//usage:     "\n	-c FMT	Use the specified format"
//usage:	)
//usage:	IF_FEATURE_STAT_FILESYSTEM(
//usage:     "\n	-f	Display filesystem status"
//usage:	)
//usage:     "\n	-L	Follow links"
//usage:     "\n	-t	Terse display"
//usage:	IF_SELINUX(
//usage:     "\n	-Z	Print security context"
//usage:	)
//usage:	IF_FEATURE_STAT_FORMAT(
//usage:       "\n\nFMT sequences"IF_FEATURE_STAT_FILESYSTEM(" for files")":\n"
//usage:       " %a	Access rights in octal\n"
//usage:       " %A	Access rights in human readable form\n"
//usage:       " %b	Number of blocks allocated (see %B)\n"
//usage:       " %B	Size in bytes of each block reported by %b\n"
//usage:       " %d	Device number in decimal\n"
//usage:       " %D	Device number in hex\n"
//usage:       " %f	Raw mode in hex\n"
//usage:       " %F	File type\n"
//usage:       " %g	Group ID\n"
//usage:       " %G	Group name\n"
//usage:       " %h	Number of hard links\n"
//usage:       " %i	Inode number\n"
//usage:       " %n	File name\n"
//usage:       " %N	File name, with -> TARGET if symlink\n"
//usage:       " %o	I/O block size\n"
//usage:       " %s	Total size in bytes\n"
//usage:       " %t	Major device type in hex\n"
//usage:       " %T	Minor device type in hex\n"
//usage:       " %u	User ID\n"
//usage:       " %U	User name\n"
//usage:       " %x	Time of last access\n"
//usage:       " %X	Time of last access as seconds since Epoch\n"
//usage:       " %y	Time of last modification\n"
//usage:       " %Y	Time of last modification as seconds since Epoch\n"
//usage:       " %z	Time of last change\n"
//usage:       " %Z	Time of last change as seconds since Epoch\n"
//usage:	IF_FEATURE_STAT_FILESYSTEM(
//usage:       "\nFMT sequences for file systems:\n"
//usage:       " %a	Free blocks available to non-superuser\n"
//usage:       " %b	Total data blocks\n"
//usage:       " %c	Total file nodes\n"
//usage:       " %d	Free file nodes\n"
//usage:       " %f	Free blocks\n"
//usage:	IF_SELINUX(
//usage:       " %C	Security context in selinux\n"
//usage:	)
//usage:       " %i	File System ID in hex\n"
//usage:       " %l	Maximum length of filenames\n"
//usage:       " %n	File name\n"
//usage:       " %s	Block size (for faster transfer)\n"
//usage:       " %S	Fundamental block size (for block counts)\n"
//usage:       " %t	Type in hex\n"
//usage:       " %T	Type in human readable form"
//usage:	)
//usage:	)

#include "libbb.h"
#include "common_bufsiz.h"

enum {
	OPT_TERSE       = (1 << 0),
	OPT_DEREFERENCE = (1 << 1),
	OPT_FILESYS     = (1 << 2) * ENABLE_FEATURE_STAT_FILESYSTEM,
	OPT_SELINUX     = (1 << (2+ENABLE_FEATURE_STAT_FILESYSTEM)) * ENABLE_SELINUX,
};

#if ENABLE_FEATURE_STAT_FORMAT
typedef bool (*statfunc_ptr)(const char *, const char *);
#else
typedef bool (*statfunc_ptr)(const char *);
#endif

static const char *file_type(const struct stat *st)
{
	/* See POSIX 1003.1-2001 XCU Table 4-8 lines 17093-17107
	 * for some of these formats.
	 * To keep diagnostics grammatical in English, the
	 * returned string must start with a consonant.
	 */
	if (S_ISREG(st->st_mode))  return st->st_size == 0 ? "regular empty file" : "regular file";
	if (S_ISDIR(st->st_mode))  return "directory";
	if (S_ISBLK(st->st_mode))  return "block special file";
	if (S_ISCHR(st->st_mode))  return "character special file";
	if (S_ISFIFO(st->st_mode)) return "fifo";
	if (S_ISLNK(st->st_mode))  return "symbolic link";
	if (S_ISSOCK(st->st_mode)) return "socket";
#ifdef S_TYPEISMQ
	if (S_TYPEISMQ(st))        return "message queue";
#endif
#ifdef S_TYPEISSEM
	if (S_TYPEISSEM(st))       return "semaphore";
#endif
#ifdef S_TYPEISSHM
	if (S_TYPEISSHM(st))       return "shared memory object";
#endif
#ifdef S_TYPEISTMO
	if (S_TYPEISTMO(st))       return "typed memory object";
#endif
	return "weird file";
}

static const char *human_time(time_t t)
{
	/* Old
	static char *str;
	str = ctime(&t);
	str[strlen(str)-1] = '\0';
	return str;
	*/
	/* coreutils 6.3 compat: */

	/*static char buf[sizeof("YYYY-MM-DD HH:MM:SS.000000000")] ALIGN1;*/
#define buf bb_common_bufsiz1
	setup_common_bufsiz();
	strcpy(strftime_YYYYMMDDHHMMSS(buf, COMMON_BUFSIZE, &t), ".000000000");
	return buf;
#undef buf
}

#if ENABLE_FEATURE_STAT_FILESYSTEM
/* Return the type of the specified file system.
 * Some systems have statfvs.f_basetype[FSTYPSZ]. (AIX, HP-UX, and Solaris)
 * Others have statfs.f_fstypename[MFSNAMELEN]. (NetBSD 1.5.2)
 * Still others have neither and have to get by with f_type (Linux).
 */
static const char *human_fstype(uint32_t f_type)
{
	static const struct types {
		uint32_t type;
		const char *const fs;
	} humantypes[] = {
		{ 0xADFF,     "affs" },
		{ 0x1Cd1,     "devpts" },
		{ 0x137D,     "ext" },
		{ 0xEF51,     "ext2" },
		{ 0xEF53,     "ext2/ext3" },
		{ 0x3153464a, "jfs" },
		{ 0x58465342, "xfs" },
		{ 0xF995E849, "hpfs" },
		{ 0x9660,     "isofs" },
		{ 0x4000,     "isofs" },
		{ 0x4004,     "isofs" },
		{ 0x137F,     "minix" },
		{ 0x138F,     "minix (30 char.)" },
		{ 0x2468,     "minix v2" },
		{ 0x2478,     "minix v2 (30 char.)" },
		{ 0x4d44,     "msdos" },
		{ 0x4006,     "fat" },
		{ 0x564c,     "novell" },
		{ 0x6969,     "nfs" },
		{ 0x9fa0,     "proc" },
		{ 0x517B,     "smb" },
		{ 0x012FF7B4, "xenix" },
		{ 0x012FF7B5, "sysv4" },
		{ 0x012FF7B6, "sysv2" },
		{ 0x012FF7B7, "coh" },
		{ 0x00011954, "ufs" },
		{ 0x012FD16D, "xia" },
		{ 0x5346544e, "ntfs" },
		{ 0x1021994,  "tmpfs" },
		{ 0x52654973, "reiserfs" },
		{ 0x28cd3d45, "cramfs" },
		{ 0x7275,     "romfs" },
		{ 0x858458f6, "ramfs" },
		{ 0x73717368, "squashfs" },
		{ 0x62656572, "sysfs" },
		{ 0, "UNKNOWN" }
	};

	int i;

	for (i = 0; humantypes[i].type; ++i)
		if (humantypes[i].type == f_type)
			break;
	return humantypes[i].fs;
}

/* "man statfs" says that statfsbuf->f_fsid is a mess */
/* coreutils treats it as an array of ints, most significant first */
static unsigned long long get_f_fsid(const struct statfs *statfsbuf)
{
	const unsigned *p = (const void*) &statfsbuf->f_fsid;
	unsigned sz = sizeof(statfsbuf->f_fsid) / sizeof(unsigned);
	unsigned long long r = 0;

	do
		r = (r << (sizeof(unsigned)*8)) | *p++;
	while (--sz > 0);
	return r;
}
#endif  /* FEATURE_STAT_FILESYSTEM */

#if ENABLE_FEATURE_STAT_FORMAT
static void strcatc(char *str, char c)
{
	int len = strlen(str);
	str[len++] = c;
	str[len] = '\0';
}

static void printfs(char *pformat, const char *msg)
{
	strcatc(pformat, 's');
	printf(pformat, msg);
}

#if ENABLE_FEATURE_STAT_FILESYSTEM
/* print statfs info */
static void FAST_FUNC print_statfs(char *pformat, const char m,
		const char *const filename, const void *data
		IF_SELINUX(, security_context_t scontext))
{
	const struct statfs *statfsbuf = data;
	if (m == 'n') {
		printfs(pformat, filename);
	} else if (m == 'i') {
		strcat(pformat, "llx");
		printf(pformat, get_f_fsid(statfsbuf));
	} else if (m == 'l') {
		strcat(pformat, "lu");
		printf(pformat, (unsigned long) statfsbuf->f_namelen);
	} else if (m == 't') {
		strcat(pformat, "lx");
		printf(pformat, (unsigned long) statfsbuf->f_type); /* no equiv */
	} else if (m == 'T') {
		printfs(pformat, human_fstype(statfsbuf->f_type));
	} else if (m == 'b') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statfsbuf->f_blocks);
	} else if (m == 'f') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statfsbuf->f_bfree);
	} else if (m == 'a') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statfsbuf->f_bavail);
	} else if (m == 's' || m == 'S') {
		strcat(pformat, "lu");
		printf(pformat, (unsigned long) statfsbuf->f_bsize);
	} else if (m == 'c') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statfsbuf->f_files);
	} else if (m == 'd') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statfsbuf->f_ffree);
# if ENABLE_SELINUX
	} else if (m == 'C' && (option_mask32 & OPT_SELINUX)) {
		printfs(pformat, scontext);
# endif
	} else {
		strcatc(pformat, 'c');
		printf(pformat, m);
	}
}
#endif

/* print stat info */
static void FAST_FUNC print_stat(char *pformat, const char m,
		const char *const filename, const void *data
		IF_SELINUX(, security_context_t scontext))
{
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))
	struct stat *statbuf = (struct stat *) data;
	struct passwd *pw_ent;
	struct group *gw_ent;

	if (m == 'n') {
		printfs(pformat, filename);
	} else if (m == 'N') {
		strcatc(pformat, 's');
		if (S_ISLNK(statbuf->st_mode)) {
			char *linkname = xmalloc_readlink_or_warn(filename);
			if (linkname == NULL)
				return;
			printf("'%s' -> '%s'", filename, linkname);
			free(linkname);
		} else {
			printf(pformat, filename);
		}
	} else if (m == 'd') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statbuf->st_dev);
	} else if (m == 'D') {
		strcat(pformat, "llx");
		printf(pformat, (unsigned long long) statbuf->st_dev);
	} else if (m == 'i') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statbuf->st_ino);
	} else if (m == 'a') {
		strcat(pformat, "lo");
		printf(pformat, (unsigned long) (statbuf->st_mode & (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)));
	} else if (m == 'A') {
		printfs(pformat, bb_mode_string(statbuf->st_mode));
	} else if (m == 'f') {
		strcat(pformat, "lx");
		printf(pformat, (unsigned long) statbuf->st_mode);
	} else if (m == 'F') {
		printfs(pformat, file_type(statbuf));
	} else if (m == 'h') {
		strcat(pformat, "lu");
		printf(pformat, (unsigned long) statbuf->st_nlink);
	} else if (m == 'u') {
		strcat(pformat, "lu");
		printf(pformat, (unsigned long) statbuf->st_uid);
	} else if (m == 'U') {
		pw_ent = getpwuid(statbuf->st_uid);
		printfs(pformat, (pw_ent != NULL) ? pw_ent->pw_name : "UNKNOWN");
	} else if (m == 'g') {
		strcat(pformat, "lu");
		printf(pformat, (unsigned long) statbuf->st_gid);
	} else if (m == 'G') {
		gw_ent = getgrgid(statbuf->st_gid);
		printfs(pformat, (gw_ent != NULL) ? gw_ent->gr_name : "UNKNOWN");
	} else if (m == 't') {
		strcat(pformat, "lx");
		printf(pformat, (unsigned long) major(statbuf->st_rdev));
	} else if (m == 'T') {
		strcat(pformat, "lx");
		printf(pformat, (unsigned long) minor(statbuf->st_rdev));
	} else if (m == 's') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statbuf->st_size);
	} else if (m == 'B') {
		strcat(pformat, "lu");
		printf(pformat, (unsigned long) 512); //ST_NBLOCKSIZE
	} else if (m == 'b') {
		strcat(pformat, "llu");
		printf(pformat, (unsigned long long) statbuf->st_blocks);
	} else if (m == 'o') {
		strcat(pformat, "lu");
		printf(pformat, (unsigned long) statbuf->st_blksize);
	} else if (m == 'x') {
		printfs(pformat, human_time(statbuf->st_atime));
	} else if (m == 'X') {
		strcat(pformat, TYPE_SIGNED(time_t) ? "ld" : "lu");
		/* note: (unsigned long) would be wrong:
		 * imagine (unsigned long64)int32 */
		printf(pformat, (long) statbuf->st_atime);
	} else if (m == 'y') {
		printfs(pformat, human_time(statbuf->st_mtime));
	} else if (m == 'Y') {
		strcat(pformat, TYPE_SIGNED(time_t) ? "ld" : "lu");
		printf(pformat, (long) statbuf->st_mtime);
	} else if (m == 'z') {
		printfs(pformat, human_time(statbuf->st_ctime));
	} else if (m == 'Z') {
		strcat(pformat, TYPE_SIGNED(time_t) ? "ld" : "lu");
		printf(pformat, (long) statbuf->st_ctime);
# if ENABLE_SELINUX
	} else if (m == 'C' && (option_mask32 & OPT_SELINUX)) {
		printfs(pformat, scontext);
# endif
	} else {
		strcatc(pformat, 'c');
		printf(pformat, m);
	}
}

static void print_it(const char *masterformat,
		const char *filename,
		void FAST_FUNC (*print_func)(char*, char, const char*, const void* IF_SELINUX(, security_context_t scontext)),
		const void *data
		IF_SELINUX(, security_context_t scontext))
{
	/* Create a working copy of the format string */
	char *format = xstrdup(masterformat);
	/* Add 2 to accommodate our conversion of the stat '%s' format string
	 * to the printf '%llu' one.  */
	char *dest = xmalloc(strlen(format) + 2 + 1);
	char *b;

	b = format;
	while (b) {
		/* Each iteration finds next %spec,
		 * prints preceding string and handles found %spec
		 */
		size_t len;
		char *p = strchr(b, '%');
		if (!p) {
			/* coreutils 6.3 always prints newline at the end */
			/*fputs(b, stdout);*/
			puts(b);
			break;
		}

		/* dest = "%<modifiers>" */
		len = 1 + strspn(p + 1, "#-+.I 0123456789");
		memcpy(dest, p, len);
		dest[len] = '\0';

		/* print preceding string */
		*p = '\0';
		fputs(b, stdout);

		p += len;
		b = p + 1;
		switch (*p) {
		case '\0':
			b = NULL;
			/* fall through */
		case '%':
			bb_putchar('%');
			break;
		default:
			/* Completes "%<modifiers>" with specifier and printfs */
			print_func(dest, *p, filename, data IF_SELINUX(,scontext));
			break;
		}
	}

	free(format);
	free(dest);
}
#endif  /* FEATURE_STAT_FORMAT */

#if ENABLE_FEATURE_STAT_FILESYSTEM
/* Stat the file system and print what we find.  */
#if !ENABLE_FEATURE_STAT_FORMAT
#define do_statfs(filename, format) do_statfs(filename)
#endif
static bool do_statfs(const char *filename, const char *format)
{
	struct statfs statfsbuf;
#if !ENABLE_FEATURE_STAT_FORMAT
	const char *format;
#endif
#if ENABLE_SELINUX
	security_context_t scontext = NULL;

	if (option_mask32 & OPT_SELINUX) {
		if ((option_mask32 & OPT_DEREFERENCE
		     ? lgetfilecon(filename, &scontext)
		     : getfilecon(filename, &scontext)
		    ) < 0
		) {
			bb_simple_perror_msg(filename);
			return 0;
		}
	}
#endif
	if (statfs(filename, &statfsbuf) != 0) {
		bb_perror_msg("can't read file system information for '%s'", filename);
		return 0;
	}

#if ENABLE_FEATURE_STAT_FORMAT
	if (format == NULL) {
# if !ENABLE_SELINUX
		format = (option_mask32 & OPT_TERSE
			? "%n %i %l %t %s %b %f %a %c %d\n"
			: "  File: \"%n\"\n"
			  "    ID: %-8i Namelen: %-7l Type: %T\n"
			  "Block size: %-10s\n"
			  "Blocks: Total: %-10b Free: %-10f Available: %a\n"
			  "Inodes: Total: %-10c Free: %d");
# else
		format = (option_mask32 & OPT_TERSE
			? (option_mask32 & OPT_SELINUX ? "%n %i %l %t %s %b %f %a %c %d %C\n":
			"%n %i %l %t %s %b %f %a %c %d\n")
			: (option_mask32 & OPT_SELINUX ?
			"  File: \"%n\"\n"
			"    ID: %-8i Namelen: %-7l Type: %T\n"
			"Block size: %-10s\n"
			"Blocks: Total: %-10b Free: %-10f Available: %a\n"
			"Inodes: Total: %-10c Free: %d"
			"  S_context: %C\n":
			"  File: \"%n\"\n"
			"    ID: %-8i Namelen: %-7l Type: %T\n"
			"Block size: %-10s\n"
			"Blocks: Total: %-10b Free: %-10f Available: %a\n"
			"Inodes: Total: %-10c Free: %d\n")
			);
# endif /* SELINUX */
	}
	print_it(format, filename, print_statfs, &statfsbuf IF_SELINUX(, scontext));
#else /* FEATURE_STAT_FORMAT */
	format = (option_mask32 & OPT_TERSE
		? "%s %llx %lu "
		: "  File: \"%s\"\n"
		  "    ID: %-8llx Namelen: %-7lu ");
	printf(format,
	       filename,
	       get_f_fsid(&statfsbuf),
	       statfsbuf.f_namelen);

	if (option_mask32 & OPT_TERSE)
		printf("%lx ", (unsigned long) statfsbuf.f_type);
	else
		printf("Type: %s\n", human_fstype(statfsbuf.f_type));

# if !ENABLE_SELINUX
	format = (option_mask32 & OPT_TERSE
		? "%lu %llu %llu %llu %llu %llu\n"
		: "Block size: %-10lu\n"
		  "Blocks: Total: %-10llu Free: %-10llu Available: %llu\n"
		  "Inodes: Total: %-10llu Free: %llu\n");
	printf(format,
	       (unsigned long) statfsbuf.f_bsize,
	       (unsigned long long) statfsbuf.f_blocks,
	       (unsigned long long) statfsbuf.f_bfree,
	       (unsigned long long) statfsbuf.f_bavail,
	       (unsigned long long) statfsbuf.f_files,
	       (unsigned long long) statfsbuf.f_ffree);
# else
	format = (option_mask32 & OPT_TERSE
		? (option_mask32 & OPT_SELINUX ? "%lu %llu %llu %llu %llu %llu %C\n" : "%lu %llu %llu %llu %llu %llu\n")
		: (option_mask32 & OPT_SELINUX
			?	"Block size: %-10lu\n"
				"Blocks: Total: %-10llu Free: %-10llu Available: %llu\n"
				"Inodes: Total: %-10llu Free: %llu"
				"S_context: %C\n"
			:	"Block size: %-10lu\n"
				"Blocks: Total: %-10llu Free: %-10llu Available: %llu\n"
				"Inodes: Total: %-10llu Free: %llu\n"
			)
		);
	printf(format,
		(unsigned long) statfsbuf.f_bsize,
		(unsigned long long) statfsbuf.f_blocks,
		(unsigned long long) statfsbuf.f_bfree,
		(unsigned long long) statfsbuf.f_bavail,
		(unsigned long long) statfsbuf.f_files,
		(unsigned long long) statfsbuf.f_ffree,
		scontext);

	if (scontext)
		freecon(scontext);
# endif
#endif  /* FEATURE_STAT_FORMAT */
	return 1;
}
#endif  /* FEATURE_STAT_FILESYSTEM */

/* stat the file and print what we find */
#if !ENABLE_FEATURE_STAT_FORMAT
#define do_stat(filename, format) do_stat(filename)
#endif
static bool do_stat(const char *filename, const char *format)
{
	struct stat statbuf;
#if ENABLE_SELINUX
	security_context_t scontext = NULL;

	if (option_mask32 & OPT_SELINUX) {
		if ((option_mask32 & OPT_DEREFERENCE
		     ? lgetfilecon(filename, &scontext)
		     : getfilecon(filename, &scontext)
		    ) < 0
		) {
			bb_simple_perror_msg(filename);
			return 0;
		}
	}
#endif
	if ((option_mask32 & OPT_DEREFERENCE ? stat : lstat) (filename, &statbuf) != 0) {
		bb_perror_msg("can't stat '%s'", filename);
		return 0;
	}

#if ENABLE_FEATURE_STAT_FORMAT
	if (format == NULL) {
# if !ENABLE_SELINUX
		if (option_mask32 & OPT_TERSE) {
			format = "%n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %o";
		} else {
			if (S_ISBLK(statbuf.st_mode) || S_ISCHR(statbuf.st_mode)) {
				format =
					"  File: %N\n"
					"  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
					"Device: %Dh/%dd\tInode: %-10i  Links: %-5h"
					" Device type: %t,%T\n"
					"Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
					"Access: %x\n" "Modify: %y\n" "Change: %z\n";
			} else {
				format =
					"  File: %N\n"
					"  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
					"Device: %Dh/%dd\tInode: %-10i  Links: %h\n"
					"Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
					"Access: %x\n" "Modify: %y\n" "Change: %z\n";
			}
		}
# else
		if (option_mask32 & OPT_TERSE) {
			format = (option_mask32 & OPT_SELINUX ?
				"%n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %o %C\n"
				:
				"%n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %o\n"
				);
		} else {
			if (S_ISBLK(statbuf.st_mode) || S_ISCHR(statbuf.st_mode)) {
				format = (option_mask32 & OPT_SELINUX ?
					"  File: %N\n"
					"  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
					"Device: %Dh/%dd\tInode: %-10i  Links: %-5h"
					" Device type: %t,%T\n"
					"Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
					"   S_Context: %C\n"
					"Access: %x\n" "Modify: %y\n" "Change: %z\n"
					:
					"  File: %N\n"
					"  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
					"Device: %Dh/%dd\tInode: %-10i  Links: %-5h"
					" Device type: %t,%T\n"
					"Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
					"Access: %x\n" "Modify: %y\n" "Change: %z\n"
					);
			} else {
				format = (option_mask32 & OPT_SELINUX ?
					"  File: %N\n"
					"  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
					"Device: %Dh/%dd\tInode: %-10i  Links: %h\n"
					"Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
					"S_Context: %C\n"
					"Access: %x\n" "Modify: %y\n" "Change: %z\n"
					:
					"  File: %N\n"
					"  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
					"Device: %Dh/%dd\tInode: %-10i  Links: %h\n"
					"Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
					"Access: %x\n" "Modify: %y\n" "Change: %z\n"
					);
			}
		}
# endif
	}
	print_it(format, filename, print_stat, &statbuf IF_SELINUX(, scontext));
#else	/* FEATURE_STAT_FORMAT */
	if (option_mask32 & OPT_TERSE) {
		printf("%s %llu %llu %lx %lu %lu %llx %llu %lu %lx %lx %lu %lu %lu %lu"
		       IF_NOT_SELINUX("\n"),
		       filename,
		       (unsigned long long) statbuf.st_size,
		       (unsigned long long) statbuf.st_blocks,
		       (unsigned long) statbuf.st_mode,
		       (unsigned long) statbuf.st_uid,
		       (unsigned long) statbuf.st_gid,
		       (unsigned long long) statbuf.st_dev,
		       (unsigned long long) statbuf.st_ino,
		       (unsigned long) statbuf.st_nlink,
		       (unsigned long) major(statbuf.st_rdev),
		       (unsigned long) minor(statbuf.st_rdev),
		       (unsigned long) statbuf.st_atime,
		       (unsigned long) statbuf.st_mtime,
		       (unsigned long) statbuf.st_ctime,
		       (unsigned long) statbuf.st_blksize
		);
# if ENABLE_SELINUX
		if (option_mask32 & OPT_SELINUX)
			printf(" %s\n", scontext);
		else
			bb_putchar('\n');
# endif
	} else {
		char *linkname = NULL;
		struct passwd *pw_ent;
		struct group *gw_ent;

		gw_ent = getgrgid(statbuf.st_gid);
		pw_ent = getpwuid(statbuf.st_uid);

		if (S_ISLNK(statbuf.st_mode))
			linkname = xmalloc_readlink_or_warn(filename);
		if (linkname) {
			printf("  File: '%s' -> '%s'\n", filename, linkname);
			free(linkname);
		} else {
			printf("  File: '%s'\n", filename);
		}

		printf("  Size: %-10llu\tBlocks: %-10llu IO Block: %-6lu %s\n"
		       "Device: %llxh/%llud\tInode: %-10llu  Links: %-5lu",
		       (unsigned long long) statbuf.st_size,
		       (unsigned long long) statbuf.st_blocks,
		       (unsigned long) statbuf.st_blksize,
		       file_type(&statbuf),
		       (unsigned long long) statbuf.st_dev,
		       (unsigned long long) statbuf.st_dev,
		       (unsigned long long) statbuf.st_ino,
		       (unsigned long) statbuf.st_nlink);
		if (S_ISBLK(statbuf.st_mode) || S_ISCHR(statbuf.st_mode))
			printf(" Device type: %lx,%lx\n",
			       (unsigned long) major(statbuf.st_rdev),
			       (unsigned long) minor(statbuf.st_rdev));
		else
			bb_putchar('\n');
		printf("Access: (%04lo/%10.10s)  Uid: (%5lu/%8s)   Gid: (%5lu/%8s)\n",
		       (unsigned long) (statbuf.st_mode & (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)),
		       bb_mode_string(statbuf.st_mode),
		       (unsigned long) statbuf.st_uid,
		       (pw_ent != NULL) ? pw_ent->pw_name : "UNKNOWN",
		       (unsigned long) statbuf.st_gid,
		       (gw_ent != NULL) ? gw_ent->gr_name : "UNKNOWN");
# if ENABLE_SELINUX
		if (option_mask32 & OPT_SELINUX)
			printf("   S_Context: %s\n", scontext);
# endif
		printf("Access: %s\n", human_time(statbuf.st_atime));
		printf("Modify: %s\n", human_time(statbuf.st_mtime));
		printf("Change: %s\n", human_time(statbuf.st_ctime));
	}
#endif  /* FEATURE_STAT_FORMAT */
	return 1;
}

int stat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int stat_main(int argc UNUSED_PARAM, char **argv)
{
	IF_FEATURE_STAT_FORMAT(char *format = NULL;)
	int i;
	int ok;
	statfunc_ptr statfunc = do_stat;
#if ENABLE_FEATURE_STAT_FILESYSTEM || ENABLE_SELINUX
	unsigned opts;

	opts =
#endif
	getopt32(argv, "^"
		"tL"
		IF_FEATURE_STAT_FILESYSTEM("f")
		IF_SELINUX("Z")
		IF_FEATURE_STAT_FORMAT("c:")
		"\0" "-1" /* min one arg */
		IF_FEATURE_STAT_FORMAT(,&format)
	);
#if ENABLE_FEATURE_STAT_FILESYSTEM
	if (opts & OPT_FILESYS) /* -f */
		statfunc = do_statfs;
#endif
#if ENABLE_SELINUX
	if (opts & OPT_SELINUX) {
		selinux_or_die();
	}
#endif
	ok = 1;
	argv += optind;
	for (i = 0; argv[i]; ++i)
		ok &= statfunc(argv[i] IF_FEATURE_STAT_FORMAT(, format));

	return (ok ? EXIT_SUCCESS : EXIT_FAILURE);
}
