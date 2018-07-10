/* vi: set sw=4 ts=4: */
/*
 * Mini find implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Reworked by David Douthitt <n9ubh@callsign.net> and
 *  Matt Kraai <kraai@alumni.carnegiemellon.edu>.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
/* findutils-4.1.20:
 *
 * # find file.txt -exec 'echo {}' '{}  {}' ';'
 * find: echo file.txt: No such file or directory
 * # find file.txt -exec 'echo' '{}  {}' '; '
 * find: missing argument to '-exec'
 * # find file.txt -exec 'echo {}' '{}  {}' ';' junk
 * find: paths must precede expression
 * # find file.txt -exec 'echo {}' '{}  {}' ';' junk ';'
 * find: paths must precede expression
 * # find file.txt -exec 'echo' '{}  {}' ';'
 * file.txt  file.txt
 * (strace: execve("/bin/echo", ["echo", "file.txt  file.txt"], [ 30 vars ]))
 * # find file.txt -exec 'echo' '{}  {}' ';' -print -exec pwd ';'
 * file.txt  file.txt
 * file.txt
 * /tmp
 * # find -name '*.c' -o -name '*.h'
 * [shows files, *.c and *.h intermixed]
 * # find file.txt -name '*f*' -o -name '*t*'
 * file.txt
 * # find file.txt -name '*z*' -o -name '*t*'
 * file.txt
 * # find file.txt -name '*f*' -o -name '*z*'
 * file.txt
 *
 * # find t z -name '*t*' -print -o -name '*z*'
 * t
 * # find t z t z -name '*t*' -o -name '*z*' -print
 * z
 * z
 * # find t z t z '(' -name '*t*' -o -name '*z*' ')' -o -print
 * (no output)
 */
/* Testing script
 * ./busybox find "$@" | tee /tmp/bb_find
 * echo ==================
 * /path/to/gnu/find "$@" | tee /tmp/std_find
 * echo ==================
 * diff -u /tmp/std_find /tmp/bb_find && echo Identical
 */
//config:config FIND
//config:	bool "find (14 kb)"
//config:	default y
//config:	help
//config:	find is used to search your system to find specified files.
//config:
//config:config FEATURE_FIND_PRINT0
//config:	bool "Enable -print0: NUL-terminated output"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Causes output names to be separated by a NUL character
//config:	rather than a newline. This allows names that contain
//config:	newlines and other whitespace to be more easily
//config:	interpreted by other programs.
//config:
//config:config FEATURE_FIND_MTIME
//config:	bool "Enable -mtime: modified time matching"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Allow searching based on the modification time of
//config:	files, in days.
//config:
//config:config FEATURE_FIND_MMIN
//config:	bool "Enable -mmin: modified time matching by minutes"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Allow searching based on the modification time of
//config:	files, in minutes.
//config:
//config:config FEATURE_FIND_PERM
//config:	bool "Enable -perm: permissions matching"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_TYPE
//config:	bool "Enable -type: file type matching (file/dir/link/...)"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Enable searching based on file type (file,
//config:	directory, socket, device, etc.).
//config:
//config:config FEATURE_FIND_EXECUTABLE
//config:	bool "Enable -executable: file is executable"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_XDEV
//config:	bool "Enable -xdev: 'stay in filesystem'"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_MAXDEPTH
//config:	bool "Enable -mindepth N and -maxdepth N"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_NEWER
//config:	bool "Enable -newer: compare file modification times"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Support the 'find -newer' option for finding any files which have
//config:	modification time that is more recent than the specified FILE.
//config:
//config:config FEATURE_FIND_INUM
//config:	bool "Enable -inum: inode number matching"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_EXEC
//config:	bool "Enable -exec: execute commands"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Support the 'find -exec' option for executing commands based upon
//config:	the files matched.
//config:
//config:config FEATURE_FIND_EXEC_PLUS
//config:	bool "Enable -exec ... {} +"
//config:	default y
//config:	depends on FEATURE_FIND_EXEC
//config:	help
//config:	Support the 'find -exec ... {} +' option for executing commands
//config:	for all matched files at once.
//config:	Without this option, -exec + is a synonym for -exec ;
//config:	(IOW: it works correctly, but without expected speedup)
//config:
//config:config FEATURE_FIND_USER
//config:	bool "Enable -user: username/uid matching"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_GROUP
//config:	bool "Enable -group: group/gid matching"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_NOT
//config:	bool "Enable the 'not' (!) operator"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Support the '!' operator to invert the test results.
//config:	If 'Enable full-blown desktop' is enabled, then will also support
//config:	the non-POSIX notation '-not'.
//config:
//config:config FEATURE_FIND_DEPTH
//config:	bool "Enable -depth"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Process each directory's contents before the directory itself.
//config:
//config:config FEATURE_FIND_PAREN
//config:	bool "Enable parens in options"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Enable usage of parens '(' to specify logical order of arguments.
//config:
//config:config FEATURE_FIND_SIZE
//config:	bool "Enable -size: file size matching"
//config:	default y
//config:	depends on FIND
//config:
//config:config FEATURE_FIND_PRUNE
//config:	bool "Enable -prune: exclude subdirectories"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	If the file is a directory, don't descend into it. Useful for
//config:	exclusion .svn and CVS directories.
//config:
//config:config FEATURE_FIND_QUIT
//config:	bool "Enable -quit: exit"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	If this action is reached, 'find' exits.
//config:
//config:config FEATURE_FIND_DELETE
//config:	bool "Enable -delete: delete files/dirs"
//config:	default y
//config:	depends on FIND && FEATURE_FIND_DEPTH
//config:	help
//config:	Support the 'find -delete' option for deleting files and directories.
//config:	WARNING: This option can do much harm if used wrong. Busybox will not
//config:	try to protect the user from doing stupid things. Use with care.
//config:
//config:config FEATURE_FIND_PATH
//config:	bool "Enable -path: match pathname with shell pattern"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	The -path option matches whole pathname instead of just filename.
//config:
//config:config FEATURE_FIND_REGEX
//config:	bool "Enable -regex: match pathname with regex"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	The -regex option matches whole pathname against regular expression.
//config:
//config:config FEATURE_FIND_CONTEXT
//config:	bool "Enable -context: security context matching"
//config:	default n
//config:	depends on FIND && SELINUX
//config:	help
//config:	Support the 'find -context' option for matching security context.
//config:
//config:config FEATURE_FIND_LINKS
//config:	bool "Enable -links: link count matching"
//config:	default y
//config:	depends on FIND
//config:	help
//config:	Support the 'find -links' option for matching number of links.

//applet:IF_FIND(APPLET_NOEXEC(find, find, BB_DIR_USR_BIN, BB_SUID_DROP, find))

//kbuild:lib-$(CONFIG_FIND) += find.o

//usage:#define find_trivial_usage
//usage:       "[-HL] [PATH]... [OPTIONS] [ACTIONS]"
//usage:#define find_full_usage "\n\n"
//usage:       "Search for files and perform actions on them.\n"
//usage:       "First failed action stops processing of current file.\n"
//usage:       "Defaults: PATH is current directory, action is '-print'\n"
//usage:     "\n	-L,-follow	Follow symlinks"
//usage:     "\n	-H		...on command line only"
//usage:	IF_FEATURE_FIND_XDEV(
//usage:     "\n	-xdev		Don't descend directories on other filesystems"
//usage:	)
//usage:	IF_FEATURE_FIND_MAXDEPTH(
//usage:     "\n	-maxdepth N	Descend at most N levels. -maxdepth 0 applies"
//usage:     "\n			actions to command line arguments only"
//usage:     "\n	-mindepth N	Don't act on first N levels"
//usage:	)
//usage:	IF_FEATURE_FIND_DEPTH(
//usage:     "\n	-depth		Act on directory *after* traversing it"
//usage:	)
//usage:     "\n"
//usage:     "\nActions:"
//usage:	IF_FEATURE_FIND_PAREN(
//usage:     "\n	( ACTIONS )	Group actions for -o / -a"
//usage:	)
//usage:	IF_FEATURE_FIND_NOT(
//usage:     "\n	! ACT		Invert ACT's success/failure"
//usage:	)
//usage:     "\n	ACT1 [-a] ACT2	If ACT1 fails, stop, else do ACT2"
//usage:     "\n	ACT1 -o ACT2	If ACT1 succeeds, stop, else do ACT2"
//usage:     "\n			Note: -a has higher priority than -o"
//usage:     "\n	-name PATTERN	Match file name (w/o directory name) to PATTERN"
//usage:     "\n	-iname PATTERN	Case insensitive -name"
//usage:	IF_FEATURE_FIND_PATH(
//usage:     "\n	-path PATTERN	Match path to PATTERN"
//usage:     "\n	-ipath PATTERN	Case insensitive -path"
//usage:	)
//usage:	IF_FEATURE_FIND_REGEX(
//usage:     "\n	-regex PATTERN	Match path to regex PATTERN"
//usage:	)
//usage:	IF_FEATURE_FIND_TYPE(
//usage:     "\n	-type X		File type is X (one of: f,d,l,b,c,s,p)"
//usage:	)
//usage:	IF_FEATURE_FIND_EXECUTABLE(
//usage:     "\n	-executable	File is executable"
//usage:	)
//usage:	IF_FEATURE_FIND_PERM(
//usage:     "\n	-perm MASK	At least one mask bit (+MASK), all bits (-MASK),"
//usage:     "\n			or exactly MASK bits are set in file's mode"
//usage:	)
//usage:	IF_FEATURE_FIND_MTIME(
//usage:     "\n	-mtime DAYS	mtime is greater than (+N), less than (-N),"
//usage:     "\n			or exactly N days in the past"
//usage:	)
//usage:	IF_FEATURE_FIND_MMIN(
//usage:     "\n	-mmin MINS	mtime is greater than (+N), less than (-N),"
//usage:     "\n			or exactly N minutes in the past"
//usage:	)
//usage:	IF_FEATURE_FIND_NEWER(
//usage:     "\n	-newer FILE	mtime is more recent than FILE's"
//usage:	)
//usage:	IF_FEATURE_FIND_INUM(
//usage:     "\n	-inum N		File has inode number N"
//usage:	)
//usage:	IF_FEATURE_FIND_USER(
//usage:     "\n	-user NAME/ID	File is owned by given user"
//usage:	)
//usage:	IF_FEATURE_FIND_GROUP(
//usage:     "\n	-group NAME/ID	File is owned by given group"
//usage:	)
//usage:	IF_FEATURE_FIND_SIZE(
//usage:     "\n	-size N[bck]	File size is N (c:bytes,k:kbytes,b:512 bytes(def.))"
//usage:     "\n			+/-N: file size is bigger/smaller than N"
//usage:	)
//usage:	IF_FEATURE_FIND_LINKS(
//usage:     "\n	-links N	Number of links is greater than (+N), less than (-N),"
//usage:     "\n			or exactly N"
//usage:	)
//usage:	IF_FEATURE_FIND_CONTEXT(
//usage:     "\n	-context CTX	File has specified security context"
//usage:	)
//usage:	IF_FEATURE_FIND_PRUNE(
//usage:     "\n	-prune		If current file is directory, don't descend into it"
//usage:	)
//usage:     "\nIf none of the following actions is specified, -print is assumed"
//usage:     "\n	-print		Print file name"
//usage:	IF_FEATURE_FIND_PRINT0(
//usage:     "\n	-print0		Print file name, NUL terminated"
//usage:	)
//usage:	IF_FEATURE_FIND_EXEC(
//usage:     "\n	-exec CMD ARG ;	Run CMD with all instances of {} replaced by"
//usage:     "\n			file name. Fails if CMD exits with nonzero"
//usage:	)
//usage:	IF_FEATURE_FIND_EXEC_PLUS(
//usage:     "\n	-exec CMD ARG + Run CMD with {} replaced by list of file names"
//usage:	)
//usage:	IF_FEATURE_FIND_DELETE(
//usage:     "\n	-delete		Delete current file/directory. Turns on -depth option"
//usage:	)
//usage:	IF_FEATURE_FIND_QUIT(
//usage:     "\n	-quit		Exit"
//usage:	)
//usage:
//usage:#define find_example_usage
//usage:       "$ find / -name passwd\n"
//usage:       "/etc/passwd\n"

#include <fnmatch.h>
#include "libbb.h"
#include "common_bufsiz.h"
#if ENABLE_FEATURE_FIND_REGEX
# include "xregex.h"
#endif
/* GNUism: */
#ifndef FNM_CASEFOLD
# define FNM_CASEFOLD 0
#endif

#if 1
# define dbg(...) ((void)0)
#else
# define dbg(...) bb_error_msg(__VA_ARGS__)
#endif


/* This is a NOEXEC applet. Be very careful! */


typedef int (*action_fp)(const char *fileName, const struct stat *statbuf, void *) FAST_FUNC;

typedef struct {
	action_fp f;
#if ENABLE_FEATURE_FIND_NOT
	bool invert;
#endif
} action;

#define ACTS(name, ...) typedef struct { action a; __VA_ARGS__ } action_##name;
#define ACTF(name) \
	static int FAST_FUNC func_##name(const char *fileName UNUSED_PARAM, \
		const struct stat *statbuf UNUSED_PARAM, \
		action_##name* ap UNUSED_PARAM)

                        ACTS(print)
                        ACTS(name,  const char *pattern; bool iname;)
IF_FEATURE_FIND_PATH(   ACTS(path,  const char *pattern; bool ipath;))
IF_FEATURE_FIND_REGEX(  ACTS(regex, regex_t compiled_pattern;))
IF_FEATURE_FIND_PRINT0( ACTS(print0))
IF_FEATURE_FIND_TYPE(   ACTS(type,  int type_mask;))
IF_FEATURE_FIND_EXECUTABLE(ACTS(executable))
IF_FEATURE_FIND_PERM(   ACTS(perm,  char perm_char; mode_t perm_mask;))
IF_FEATURE_FIND_MTIME(  ACTS(mtime, char mtime_char; unsigned mtime_days;))
IF_FEATURE_FIND_MMIN(   ACTS(mmin,  char mmin_char; unsigned mmin_mins;))
IF_FEATURE_FIND_NEWER(  ACTS(newer, time_t newer_mtime;))
IF_FEATURE_FIND_INUM(   ACTS(inum,  ino_t inode_num;))
IF_FEATURE_FIND_USER(   ACTS(user,  uid_t uid;))
IF_FEATURE_FIND_SIZE(   ACTS(size,  char size_char; off_t size;))
IF_FEATURE_FIND_CONTEXT(ACTS(context, security_context_t context;))
IF_FEATURE_FIND_PAREN(  ACTS(paren, action ***subexpr;))
IF_FEATURE_FIND_PRUNE(  ACTS(prune))
IF_FEATURE_FIND_QUIT(   ACTS(quit))
IF_FEATURE_FIND_DELETE( ACTS(delete))
IF_FEATURE_FIND_EXEC(   ACTS(exec,
				char **exec_argv; /* -exec ARGS */
				unsigned *subst_count;
				int exec_argc; /* count of ARGS */
				IF_FEATURE_FIND_EXEC_PLUS(
					/*
					 * filelist is NULL if "exec ;"
					 * non-NULL if "exec +"
					 */
					char **filelist;
					int filelist_idx;
					int file_len;
				)
				))
IF_FEATURE_FIND_GROUP(  ACTS(group, gid_t gid;))
IF_FEATURE_FIND_LINKS(  ACTS(links, char links_char; int links_count;))

struct globals {
	IF_FEATURE_FIND_XDEV(dev_t *xdev_dev;)
	IF_FEATURE_FIND_XDEV(int xdev_count;)
#if ENABLE_FEATURE_FIND_MAXDEPTH
	int minmaxdepth[2];
#endif
	action ***actions;
	smallint need_print;
	smallint xdev_on;
	smalluint exitstatus;
	recurse_flags_t recurse_flags;
	IF_FEATURE_FIND_EXEC_PLUS(unsigned max_argv_len;)
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
	/* we have to zero it out because of NOEXEC */ \
	memset(&G, 0, sizeof(G)); \
	IF_FEATURE_FIND_MAXDEPTH(G.minmaxdepth[1] = INT_MAX;) \
	IF_FEATURE_FIND_EXEC_PLUS(G.max_argv_len = bb_arg_max() - 2048;) \
	G.need_print = 1; \
	G.recurse_flags = ACTION_RECURSE; \
} while (0)

/* Return values of ACTFs ('action functions') are a bit mask:
 * bit 1=1: prune (use SKIP constant for setting it)
 * bit 0=1: matched successfully (TRUE)
 */

static int exec_actions(action ***appp, const char *fileName, const struct stat *statbuf)
{
	int cur_group;
	int cur_action;
	int rc = 0;
	action **app, *ap;

	/* "action group" is a set of actions ANDed together.
	 * groups are ORed together.
	 * We simply evaluate each group until we find one in which all actions
	 * succeed. */

	/* -prune is special: if it is encountered, then we won't
	 * descend into current directory. It doesn't matter whether
	 * action group (in which -prune sits) will succeed or not:
	 * find * -prune -name 'f*' -o -name 'm*' -- prunes every dir
	 * find * -name 'f*' -o -prune -name 'm*' -- prunes all dirs
	 *     not starting with 'f' */

	/* We invert TRUE bit (bit 0). Now 1 there means 'failure'.
	 * and bitwise OR in "rc |= TRUE ^ ap->f()" will:
	 * (1) make SKIP (-prune) bit stick; and (2) detect 'failure'.
	 * On return, bit is restored.  */

	cur_group = -1;
	while ((app = appp[++cur_group]) != NULL) {
		rc &= ~TRUE; /* 'success' so far, clear TRUE bit */
		cur_action = -1;
		while (1) {
			ap = app[++cur_action];
			if (!ap) /* all actions in group were successful */
				return rc ^ TRUE; /* restore TRUE bit */
			rc |= TRUE ^ ap->f(fileName, statbuf, ap);
#if ENABLE_FEATURE_FIND_NOT
			if (ap->invert) rc ^= TRUE;
#endif
			dbg("grp %d action %d rc:0x%x", cur_group, cur_action, rc);
			if (rc & TRUE) /* current group failed, try next */
				break;
		}
	}
	dbg("returning:0x%x", rc ^ TRUE);
	return rc ^ TRUE; /* restore TRUE bit */
}

#if !FNM_CASEFOLD
static char *strcpy_upcase(char *dst, const char *src)
{
	char *d = dst;
	while (1) {
		unsigned char ch = *src++;
		if (ch >= 'a' && ch <= 'z')
			ch -= ('a' - 'A');
		*d++ = ch;
		if (ch == '\0')
			break;
	}
	return dst;
}
#endif

ACTF(name)
{
	int r;
	const char *tmp = bb_basename(fileName);
	/* GNU findutils: find DIR/ -name DIR
	 * prints "DIR/" (DIR// prints "DIR//" etc).
	 * Need to strip trailing "/".
	 * Such names can come only from top-level names, but
	 * we can't do this before recursive_action() call,
	 * since then "find FILE/ -name FILE"
	 * would also work (on non-directories), which is wrong.
	 */
	char *trunc_slash = NULL;

	if (*tmp == '\0') {
		/* "foo/bar/[//...]" */
		while (tmp != fileName && tmp[-1] == '/')
			tmp--;
		if (tmp == fileName) { /* entire fileName is "//.."? */
			/* yes, convert "//..." to "/"
			 * Testcases:
			 * find / -maxdepth 1 -name /: prints /
			 * find // -maxdepth 1 -name /: prints //
			 * find / -maxdepth 1 -name //: prints nothing
			 * find // -maxdepth 1 -name //: prints nothing
			 */
			if (tmp[1])
				trunc_slash = (char*)tmp + 1;
		} else {
			/* no, it's "foo/bar/[//...]", go back to 'b' */
			trunc_slash = (char*)tmp;
			while (tmp != fileName && tmp[-1] != '/')
				tmp--;
		}
	}

	/* Was using FNM_PERIOD flag too,
	 * but somewhere between 4.1.20 and 4.4.0 GNU find stopped using it.
	 * find -name '*foo' should match .foo too:
	 */
	if (trunc_slash) *trunc_slash = '\0';
#if FNM_CASEFOLD
	r = fnmatch(ap->pattern, tmp, (ap->iname ? FNM_CASEFOLD : 0));
#else
	if (ap->iname)
		tmp = strcpy_upcase(alloca(strlen(tmp) + 1), tmp);
	r = fnmatch(ap->pattern, tmp, 0);
#endif
	if (trunc_slash) *trunc_slash = '/';
	return r == 0;
}

#if ENABLE_FEATURE_FIND_PATH
ACTF(path)
{
# if FNM_CASEFOLD
	return fnmatch(ap->pattern, fileName, (ap->ipath ? FNM_CASEFOLD : 0)) == 0;
# else
	if (ap->ipath)
		fileName = strcpy_upcase(alloca(strlen(fileName) + 1), fileName);
	return fnmatch(ap->pattern, fileName, 0) == 0;
# endif
}
#endif
#if ENABLE_FEATURE_FIND_REGEX
ACTF(regex)
{
	regmatch_t match;
	if (regexec(&ap->compiled_pattern, fileName, 1, &match, 0 /*eflags*/))
		return 0; /* no match */
	if (match.rm_so)
		return 0; /* match doesn't start at pos 0 */
	if (fileName[match.rm_eo])
		return 0; /* match doesn't end exactly at end of pathname */
	return 1;
}
#endif
#if ENABLE_FEATURE_FIND_TYPE
ACTF(type)
{
	return ((statbuf->st_mode & S_IFMT) == ap->type_mask);
}
#endif
#if ENABLE_FEATURE_FIND_EXECUTABLE
ACTF(executable)
{
	return access(fileName, X_OK) == 0;
}
#endif
#if ENABLE_FEATURE_FIND_PERM
ACTF(perm)
{
	/* -perm [+/]mode: at least one of perm_mask bits are set */
	if (ap->perm_char == '+' || ap->perm_char == '/')
		return (statbuf->st_mode & ap->perm_mask) != 0;
	/* -perm -mode: all of perm_mask are set */
	if (ap->perm_char == '-')
		return (statbuf->st_mode & ap->perm_mask) == ap->perm_mask;
	/* -perm mode: file mode must match perm_mask */
	return (statbuf->st_mode & 07777) == ap->perm_mask;
}
#endif
#if ENABLE_FEATURE_FIND_MTIME
ACTF(mtime)
{
	time_t file_age = time(NULL) - statbuf->st_mtime;
	time_t mtime_secs = ap->mtime_days * 24*60*60;
	if (ap->mtime_char == '+')
		return file_age >= mtime_secs + 24*60*60;
	if (ap->mtime_char == '-')
		return file_age < mtime_secs;
	/* just numeric mtime */
	return file_age >= mtime_secs && file_age < (mtime_secs + 24*60*60);
}
#endif
#if ENABLE_FEATURE_FIND_MMIN
ACTF(mmin)
{
	time_t file_age = time(NULL) - statbuf->st_mtime;
	time_t mmin_secs = ap->mmin_mins * 60;
	if (ap->mmin_char == '+')
		return file_age >= mmin_secs + 60;
	if (ap->mmin_char == '-')
		return file_age < mmin_secs;
	/* just numeric mmin */
	return file_age >= mmin_secs && file_age < (mmin_secs + 60);
}
#endif
#if ENABLE_FEATURE_FIND_NEWER
ACTF(newer)
{
	return (ap->newer_mtime < statbuf->st_mtime);
}
#endif
#if ENABLE_FEATURE_FIND_INUM
ACTF(inum)
{
	return (statbuf->st_ino == ap->inode_num);
}
#endif
#if ENABLE_FEATURE_FIND_EXEC
static int do_exec(action_exec *ap, const char *fileName)
{
	int i, rc;
# if ENABLE_FEATURE_FIND_EXEC_PLUS
	int size = ap->exec_argc + ap->filelist_idx + 1;
# else
	int size = ap->exec_argc + 1;
# endif
# if ENABLE_USE_PORTABLE_CODE
	char **argv = alloca(sizeof(char*) * size);
# else /* gcc 4.3.1 generates smaller code: */
	char *argv[size];
# endif
	char **pp = argv;

	for (i = 0; i < ap->exec_argc; i++) {
		const char *arg = ap->exec_argv[i];

# if ENABLE_FEATURE_FIND_EXEC_PLUS
		if (ap->filelist) {
			/* Handling "-exec +"
			 * Only one exec_argv[i] has substitution in it.
			 * Expand that one exec_argv[i] into file list.
			 */
			if (ap->subst_count[i] == 0) {
				*pp++ = xstrdup(arg);
			} else {
				int j = 0;
				while (ap->filelist[j]) {
					/* 2nd arg here should be ap->subst_count[i], but it is always 1: */
					*pp++ = xmalloc_substitute_string(arg, 1, "{}", ap->filelist[j]);
					free(ap->filelist[j]);
					j++;
				}
			}
		} else
# endif
		{
			/* Handling "-exec ;" */
			*pp++ = xmalloc_substitute_string(arg, ap->subst_count[i], "{}", fileName);
		}
	}
	*pp = NULL; /* terminate the list */

# if ENABLE_FEATURE_FIND_EXEC_PLUS
	if (ap->filelist) {
		ap->filelist[0] = NULL;
		ap->filelist_idx = 0;
		ap->file_len = 0;
	}
# endif

	rc = spawn_and_wait(argv);
	if (rc < 0)
		bb_simple_perror_msg(argv[0]);

	i = 0;
	while (argv[i])
		free(argv[i++]);
	return rc == 0; /* return 1 if exitcode 0 */
}
ACTF(exec)
{
# if ENABLE_FEATURE_FIND_EXEC_PLUS
	if (ap->filelist) {
		int rc;

		ap->filelist = xrealloc_vector(ap->filelist, 8, ap->filelist_idx);
		ap->filelist[ap->filelist_idx++] = xstrdup(fileName);
		ap->file_len += strlen(fileName) + sizeof(char*) + 1;
		/* If we have lots of files already, exec the command */
		rc = 1;
		if (ap->file_len >= G.max_argv_len)
			rc = do_exec(ap, NULL);
		return rc;
	}
# endif
	return do_exec(ap, fileName);
}
# if ENABLE_FEATURE_FIND_EXEC_PLUS
static int flush_exec_plus(void)
{
	action *ap;
	action **app;
	action ***appp = G.actions;
	while ((app = *appp++) != NULL) {
		while ((ap = *app++) != NULL) {
			if (ap->f == (action_fp)func_exec) {
				action_exec *ae = (void*)ap;
				if (ae->filelist_idx != 0) {
					int rc = do_exec(ae, NULL);
#  if ENABLE_FEATURE_FIND_NOT
					if (ap->invert) rc = !rc;
#  endif
					if (rc == 0)
						return 1;
				}
			}
		}
	}
	return 0;
}
# endif
#endif
#if ENABLE_FEATURE_FIND_USER
ACTF(user)
{
	return (statbuf->st_uid == ap->uid);
}
#endif
#if ENABLE_FEATURE_FIND_GROUP
ACTF(group)
{
	return (statbuf->st_gid == ap->gid);
}
#endif
#if ENABLE_FEATURE_FIND_PRINT0
ACTF(print0)
{
	printf("%s%c", fileName, '\0');
	return TRUE;
}
#endif
ACTF(print)
{
	puts(fileName);
	return TRUE;
}
#if ENABLE_FEATURE_FIND_PAREN
ACTF(paren)
{
	return exec_actions(ap->subexpr, fileName, statbuf);
}
#endif
#if ENABLE_FEATURE_FIND_SIZE
ACTF(size)
{
	if (ap->size_char == '+')
		return statbuf->st_size > ap->size;
	if (ap->size_char == '-')
		return statbuf->st_size < ap->size;
	return statbuf->st_size == ap->size;
}
#endif
#if ENABLE_FEATURE_FIND_PRUNE
/*
 * -prune: if -depth is not given, return true and do not descend
 * current dir; if -depth is given, return false with no effect.
 * Example:
 * find dir -name 'asm-*' -prune -o -name '*.[chS]' -print
 */
ACTF(prune)
{
	return SKIP + TRUE;
}
#endif
#if ENABLE_FEATURE_FIND_QUIT
ACTF(quit)
{
	exit(G.exitstatus);
}
#endif
#if ENABLE_FEATURE_FIND_DELETE
ACTF(delete)
{
	int rc;
	if (S_ISDIR(statbuf->st_mode)) {
		/* "find . -delete" skips rmdir(".") */
		rc = 0;
		if (NOT_LONE_CHAR(fileName, '.'))
			rc = rmdir(fileName);
	} else {
		rc = unlink(fileName);
	}
	if (rc < 0)
		bb_simple_perror_msg(fileName);
	return TRUE;
}
#endif
#if ENABLE_FEATURE_FIND_CONTEXT
ACTF(context)
{
	security_context_t con;
	int rc;

	if (G.recurse_flags & ACTION_FOLLOWLINKS) {
		rc = getfilecon(fileName, &con);
	} else {
		rc = lgetfilecon(fileName, &con);
	}
	if (rc < 0)
		return FALSE;
	rc = strcmp(ap->context, con);
	freecon(con);
	return rc == 0;
}
#endif
#if ENABLE_FEATURE_FIND_LINKS
ACTF(links)
{
	switch(ap->links_char) {
	case '-' : return (statbuf->st_nlink <  ap->links_count);
	case '+' : return (statbuf->st_nlink >  ap->links_count);
	default:   return (statbuf->st_nlink == ap->links_count);
	}
}
#endif

static int FAST_FUNC fileAction(const char *fileName,
		struct stat *statbuf,
		void *userData UNUSED_PARAM,
		int depth IF_NOT_FEATURE_FIND_MAXDEPTH(UNUSED_PARAM))
{
	int r;
	int same_fs = 1;

#if ENABLE_FEATURE_FIND_XDEV
	if (S_ISDIR(statbuf->st_mode) && G.xdev_count) {
		int i;
		for (i = 0; i < G.xdev_count; i++) {
			if (G.xdev_dev[i] == statbuf->st_dev)
				goto found;
		}
		//bb_error_msg("'%s': not same fs", fileName);
		same_fs = 0;
 found: ;
	}
#endif

#if ENABLE_FEATURE_FIND_MAXDEPTH
	if (depth < G.minmaxdepth[0]) {
		if (same_fs)
			return TRUE; /* skip this, continue recursing */
		return SKIP; /* stop recursing */
	}
	if (depth > G.minmaxdepth[1])
		return SKIP; /* stop recursing */
#endif

	r = exec_actions(G.actions, fileName, statbuf);
	/* Had no explicit -print[0] or -exec? then print */
	if ((r & TRUE) && G.need_print)
		puts(fileName);

#if ENABLE_FEATURE_FIND_MAXDEPTH
	if (S_ISDIR(statbuf->st_mode)) {
		if (depth == G.minmaxdepth[1])
			return SKIP;
	}
#endif
	/* -xdev stops on mountpoints, but AFTER mountpoit itself
	 * is processed as usual */
	if (!same_fs) {
		return SKIP;
	}

	/* Cannot return 0: our caller, recursive_action(),
	 * will perror() and skip dirs (if called on dir) */
	return (r & SKIP) ? SKIP : TRUE;
}


#if ENABLE_FEATURE_FIND_TYPE
static int find_type(const char *type)
{
	int mask = 0;

	if (*type == 'b')
		mask = S_IFBLK;
	else if (*type == 'c')
		mask = S_IFCHR;
	else if (*type == 'd')
		mask = S_IFDIR;
	else if (*type == 'p')
		mask = S_IFIFO;
	else if (*type == 'f')
		mask = S_IFREG;
	else if (*type == 'l')
		mask = S_IFLNK;
	else if (*type == 's')
		mask = S_IFSOCK;

	if (mask == 0 || type[1] != '\0')
		bb_error_msg_and_die(bb_msg_invalid_arg_to, type, "-type");

	return mask;
}
#endif

#if ENABLE_FEATURE_FIND_PERM \
 || ENABLE_FEATURE_FIND_MTIME || ENABLE_FEATURE_FIND_MMIN \
 || ENABLE_FEATURE_FIND_SIZE  || ENABLE_FEATURE_FIND_LINKS
static const char* plus_minus_num(const char* str)
{
	if (*str == '-' || *str == '+')
		str++;
	return str;
}
#endif

/* Say no to GCCism */
#define USE_NESTED_FUNCTION 0

#if !USE_NESTED_FUNCTION
struct pp_locals {
	action*** appp;
	unsigned cur_group;
	unsigned cur_action;
	IF_FEATURE_FIND_NOT( bool invert_flag; )
};
static action* alloc_action(struct pp_locals *ppl, int sizeof_struct, action_fp f)
{
	action *ap = xzalloc(sizeof_struct);
	action **app;
	action ***group = &ppl->appp[ppl->cur_group];
	*group = app = xrealloc(*group, (ppl->cur_action+2) * sizeof(ppl->appp[0][0]));
	app[ppl->cur_action++] = ap;
	app[ppl->cur_action] = NULL;
	ap->f = f;
	IF_FEATURE_FIND_NOT( ap->invert = ppl->invert_flag; )
	IF_FEATURE_FIND_NOT( ppl->invert_flag = 0; )
	return ap;
}
#endif

static action*** parse_params(char **argv)
{
	enum {
	                        OPT_FOLLOW     ,
	IF_FEATURE_FIND_XDEV(   OPT_XDEV       ,)
	IF_FEATURE_FIND_DEPTH(  OPT_DEPTH      ,)
	                        PARM_a         ,
	                        PARM_o         ,
	IF_FEATURE_FIND_NOT(	PARM_char_not  ,)
#if ENABLE_DESKTOP
	                        PARM_and       ,
	                        PARM_or        ,
	IF_FEATURE_FIND_NOT(    PARM_not       ,)
#endif
	                        PARM_print     ,
	IF_FEATURE_FIND_PRINT0( PARM_print0    ,)
	IF_FEATURE_FIND_PRUNE(  PARM_prune     ,)
	IF_FEATURE_FIND_QUIT(   PARM_quit      ,)
	IF_FEATURE_FIND_DELETE( PARM_delete    ,)
	IF_FEATURE_FIND_EXEC(   PARM_exec      ,)
	IF_FEATURE_FIND_EXECUTABLE(PARM_executable,)
	IF_FEATURE_FIND_PAREN(  PARM_char_brace,)
	/* All options/actions starting from here require argument */
	                        PARM_name      ,
	                        PARM_iname     ,
	IF_FEATURE_FIND_PATH(   PARM_path      ,)
#if ENABLE_DESKTOP
	/* -wholename is a synonym for -path */
	/* We support it because Linux kernel's "make tags" uses it */
	IF_FEATURE_FIND_PATH(   PARM_wholename ,)
#endif
	IF_FEATURE_FIND_PATH(   PARM_ipath     ,)
	IF_FEATURE_FIND_REGEX(  PARM_regex     ,)
	IF_FEATURE_FIND_TYPE(   PARM_type      ,)
	IF_FEATURE_FIND_PERM(   PARM_perm      ,)
	IF_FEATURE_FIND_MTIME(  PARM_mtime     ,)
	IF_FEATURE_FIND_MMIN(   PARM_mmin      ,)
	IF_FEATURE_FIND_NEWER(  PARM_newer     ,)
	IF_FEATURE_FIND_INUM(   PARM_inum      ,)
	IF_FEATURE_FIND_USER(   PARM_user      ,)
	IF_FEATURE_FIND_GROUP(  PARM_group     ,)
	IF_FEATURE_FIND_SIZE(   PARM_size      ,)
	IF_FEATURE_FIND_CONTEXT(PARM_context   ,)
	IF_FEATURE_FIND_LINKS(  PARM_links     ,)
	IF_FEATURE_FIND_MAXDEPTH(OPT_MINDEPTH,OPT_MAXDEPTH,)
	};

	static const char params[] ALIGN1 =
	                        "-follow\0"
	IF_FEATURE_FIND_XDEV(   "-xdev\0"                 )
	IF_FEATURE_FIND_DEPTH(  "-depth\0"                )
	                        "-a\0"
	                        "-o\0"
	IF_FEATURE_FIND_NOT(    "!\0"       )
#if ENABLE_DESKTOP
	                        "-and\0"
	                        "-or\0"
	IF_FEATURE_FIND_NOT(	"-not\0"    )
#endif
	                        "-print\0"
	IF_FEATURE_FIND_PRINT0( "-print0\0" )
	IF_FEATURE_FIND_PRUNE(  "-prune\0"  )
	IF_FEATURE_FIND_QUIT(   "-quit\0"  )
	IF_FEATURE_FIND_DELETE( "-delete\0" )
	IF_FEATURE_FIND_EXEC(   "-exec\0"   )
	IF_FEATURE_FIND_EXECUTABLE("-executable\0")
	IF_FEATURE_FIND_PAREN(  "(\0"       )
	/* All options/actions starting from here require argument */
	                        "-name\0"
	                        "-iname\0"
	IF_FEATURE_FIND_PATH(   "-path\0"   )
#if ENABLE_DESKTOP
	IF_FEATURE_FIND_PATH(   "-wholename\0")
#endif
	IF_FEATURE_FIND_PATH(   "-ipath\0"  )
	IF_FEATURE_FIND_REGEX(  "-regex\0"  )
	IF_FEATURE_FIND_TYPE(   "-type\0"   )
	IF_FEATURE_FIND_PERM(   "-perm\0"   )
	IF_FEATURE_FIND_MTIME(  "-mtime\0"  )
	IF_FEATURE_FIND_MMIN(   "-mmin\0"   )
	IF_FEATURE_FIND_NEWER(  "-newer\0"  )
	IF_FEATURE_FIND_INUM(   "-inum\0"   )
	IF_FEATURE_FIND_USER(   "-user\0"   )
	IF_FEATURE_FIND_GROUP(  "-group\0"  )
	IF_FEATURE_FIND_SIZE(   "-size\0"   )
	IF_FEATURE_FIND_CONTEXT("-context\0")
	IF_FEATURE_FIND_LINKS(  "-links\0"  )
	IF_FEATURE_FIND_MAXDEPTH("-mindepth\0""-maxdepth\0")
	;

#if !USE_NESTED_FUNCTION
	struct pp_locals ppl;
#define appp        (ppl.appp       )
#define cur_group   (ppl.cur_group  )
#define cur_action  (ppl.cur_action )
#define invert_flag (ppl.invert_flag)
#define ALLOC_ACTION(name) (action_##name*)alloc_action(&ppl, sizeof(action_##name), (action_fp) func_##name)
#else
	action*** appp;
	unsigned cur_group;
	unsigned cur_action;
	IF_FEATURE_FIND_NOT( bool invert_flag; )

	/* This is the only place in busybox where we use nested function.
	 * So far more standard alternatives were bigger. */
	/* Auto decl suppresses "func without a prototype" warning: */
	auto action* alloc_action(int sizeof_struct, action_fp f);
	action* alloc_action(int sizeof_struct, action_fp f)
	{
		action *ap;
		appp[cur_group] = xrealloc(appp[cur_group], (cur_action+2) * sizeof(appp[0][0]));
		appp[cur_group][cur_action++] = ap = xzalloc(sizeof_struct);
		appp[cur_group][cur_action] = NULL;
		ap->f = f;
		IF_FEATURE_FIND_NOT( ap->invert = invert_flag; )
		IF_FEATURE_FIND_NOT( invert_flag = 0; )
		return ap;
	}
#define ALLOC_ACTION(name) (action_##name*)alloc_action(sizeof(action_##name), (action_fp) func_##name)
#endif

	cur_group = 0;
	cur_action = 0;
	IF_FEATURE_FIND_NOT( invert_flag = 0; )
	appp = xzalloc(2 * sizeof(appp[0])); /* appp[0],[1] == NULL */

	while (*argv) {
		const char *arg = argv[0];
		int parm = index_in_strings(params, arg);
		const char *arg1 = argv[1];

		dbg("arg:'%s' arg1:'%s' parm:%d PARM_type:%d", arg, arg1, parm, PARM_type);

		if (parm >= PARM_name) {
			/* All options/actions starting from -name require argument */
			if (!arg1)
				bb_error_msg_and_die(bb_msg_requires_arg, arg);
			argv++;
		}

		/* We can use big switch() here, but on i386
		 * it doesn't give smaller code. Other arches? */

/* Options always return true. They always take effect
 * rather than being processed only when their place in the
 * expression is reached.
 */
		/* Options */
		if (parm == OPT_FOLLOW) {
			dbg("follow enabled: %d", __LINE__);
			G.recurse_flags |= ACTION_FOLLOWLINKS | ACTION_DANGLING_OK;
		}
#if ENABLE_FEATURE_FIND_XDEV
		else if (parm == OPT_XDEV) {
			dbg("%d", __LINE__);
			G.xdev_on = 1;
		}
#endif
#if ENABLE_FEATURE_FIND_MAXDEPTH
		else if (parm == OPT_MINDEPTH || parm == OPT_MINDEPTH + 1) {
			dbg("%d", __LINE__);
			G.minmaxdepth[parm - OPT_MINDEPTH] = xatoi_positive(arg1);
		}
#endif
#if ENABLE_FEATURE_FIND_DEPTH
		else if (parm == OPT_DEPTH) {
			dbg("%d", __LINE__);
			G.recurse_flags |= ACTION_DEPTHFIRST;
		}
#endif
/* Actions are grouped by operators
 * ( expr )              Force precedence
 * ! expr                True if expr is false
 * -not expr             Same as ! expr
 * expr1 [-a[nd]] expr2  And; expr2 is not evaluated if expr1 is false
 * expr1 -o[r] expr2     Or; expr2 is not evaluated if expr1 is true
 * expr1 , expr2         List; both expr1 and expr2 are always evaluated
 * We implement: (), -a, -o
 */
		/* Operators */
		else if (parm == PARM_a IF_DESKTOP(|| parm == PARM_and)) {
			dbg("%d", __LINE__);
			/* no further special handling required */
		}
		else if (parm == PARM_o IF_DESKTOP(|| parm == PARM_or)) {
			dbg("%d", __LINE__);
			/* start new OR group */
			cur_group++;
			appp = xrealloc(appp, (cur_group+2) * sizeof(appp[0]));
			/*appp[cur_group] = NULL; - already NULL */
			appp[cur_group+1] = NULL;
			cur_action = 0;
		}
#if ENABLE_FEATURE_FIND_NOT
		else if (parm == PARM_char_not IF_DESKTOP(|| parm == PARM_not)) {
			/* also handles "find ! ! -name 'foo*'" */
			invert_flag ^= 1;
			dbg("invert_flag:%d", invert_flag);
		}
#endif
		/* Actions */
		else if (parm == PARM_print) {
			dbg("%d", __LINE__);
			G.need_print = 0;
			(void) ALLOC_ACTION(print);
		}
#if ENABLE_FEATURE_FIND_PRINT0
		else if (parm == PARM_print0) {
			dbg("%d", __LINE__);
			G.need_print = 0;
			(void) ALLOC_ACTION(print0);
		}
#endif
#if ENABLE_FEATURE_FIND_PRUNE
		else if (parm == PARM_prune) {
			dbg("%d", __LINE__);
			(void) ALLOC_ACTION(prune);
		}
#endif
#if ENABLE_FEATURE_FIND_QUIT
		else if (parm == PARM_quit) {
			dbg("%d", __LINE__);
			(void) ALLOC_ACTION(quit);
		}
#endif
#if ENABLE_FEATURE_FIND_DELETE
		else if (parm == PARM_delete) {
			dbg("%d", __LINE__);
			G.need_print = 0;
			G.recurse_flags |= ACTION_DEPTHFIRST;
			(void) ALLOC_ACTION(delete);
		}
#endif
#if ENABLE_FEATURE_FIND_EXEC
		else if (parm == PARM_exec) {
			int i;
			action_exec *ap;
			IF_FEATURE_FIND_EXEC_PLUS(int all_subst = 0;)
			dbg("%d", __LINE__);
			G.need_print = 0;
			ap = ALLOC_ACTION(exec);
			ap->exec_argv = ++argv; /* first arg after -exec */
			/*ap->exec_argc = 0; - ALLOC_ACTION did it */
			while (1) {
				if (!*argv) /* did not see ';' or '+' until end */
					bb_error_msg_and_die(bb_msg_requires_arg, "-exec");
				// find -exec echo Foo ">{}<" ";"
				// executes "echo Foo >FILENAME<",
				// find -exec echo Foo ">{}<" "+"
				// executes "echo Foo FILENAME1 FILENAME2 FILENAME3...".
				if ((argv[0][0] == ';' || argv[0][0] == '+')
				 && argv[0][1] == '\0'
				) {
# if ENABLE_FEATURE_FIND_EXEC_PLUS
					if (argv[0][0] == '+')
						ap->filelist = xzalloc(sizeof(ap->filelist[0]));
# endif
					break;
				}
				argv++;
				ap->exec_argc++;
			}
			if (ap->exec_argc == 0)
				bb_error_msg_and_die(bb_msg_requires_arg, arg);
			ap->subst_count = xmalloc(ap->exec_argc * sizeof(int));
			i = ap->exec_argc;
			while (i--) {
				ap->subst_count[i] = count_strstr(ap->exec_argv[i], "{}");
				IF_FEATURE_FIND_EXEC_PLUS(all_subst += ap->subst_count[i];)
			}
# if ENABLE_FEATURE_FIND_EXEC_PLUS
			/*
			 * coreutils expects {} to appear only once in "-exec +"
			 */
			if (all_subst != 1 && ap->filelist)
				bb_error_msg_and_die("only one '{}' allowed for -exec +");
# endif
		}
#endif
#if ENABLE_FEATURE_FIND_PAREN
		else if (parm == PARM_char_brace) {
			action_paren *ap;
			char **endarg;
			unsigned nested = 1;

			dbg("%d", __LINE__);
			endarg = argv;
			while (1) {
				if (!*++endarg)
					bb_error_msg_and_die("unpaired '('");
				if (LONE_CHAR(*endarg, '('))
					nested++;
				else if (LONE_CHAR(*endarg, ')') && !--nested) {
					*endarg = NULL;
					break;
				}
			}
			ap = ALLOC_ACTION(paren);
			ap->subexpr = parse_params(argv + 1);
			*endarg = (char*) ")"; /* restore NULLed parameter */
			argv = endarg;
		}
#endif
		else if (parm == PARM_name || parm == PARM_iname) {
			action_name *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(name);
			ap->pattern = arg1;
			ap->iname = (parm == PARM_iname);
		}
#if ENABLE_FEATURE_FIND_PATH
		else if (parm == PARM_path IF_DESKTOP(|| parm == PARM_wholename) || parm == PARM_ipath) {
			action_path *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(path);
			ap->pattern = arg1;
			ap->ipath = (parm == PARM_ipath);
		}
#endif
#if ENABLE_FEATURE_FIND_REGEX
		else if (parm == PARM_regex) {
			action_regex *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(regex);
			xregcomp(&ap->compiled_pattern, arg1, 0 /*cflags*/);
		}
#endif
#if ENABLE_FEATURE_FIND_TYPE
		else if (parm == PARM_type) {
			action_type *ap;
			ap = ALLOC_ACTION(type);
			ap->type_mask = find_type(arg1);
			dbg("created:type mask:%x", ap->type_mask);
		}
#endif
#if ENABLE_FEATURE_FIND_EXECUTABLE
		else if (parm == PARM_executable) {
			(void) ALLOC_ACTION(executable);
		}
#endif
#if ENABLE_FEATURE_FIND_PERM
/* -perm BITS   File's mode bits are exactly BITS (octal or symbolic).
 *              Symbolic modes use mode 0 as a point of departure.
 * -perm -BITS  All of the BITS are set in file's mode.
 * -perm [+/]BITS  At least one of the BITS is set in file's mode.
 */
		else if (parm == PARM_perm) {
			action_perm *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(perm);
			ap->perm_char = arg1[0];
			arg1 = (arg1[0] == '/' ? arg1+1 : plus_minus_num(arg1));
			/*ap->perm_mask = 0; - ALLOC_ACTION did it */
			ap->perm_mask = bb_parse_mode(arg1, ap->perm_mask);
			if (ap->perm_mask == (mode_t)-1)
				bb_error_msg_and_die("invalid mode '%s'", arg1);
		}
#endif
#if ENABLE_FEATURE_FIND_MTIME
		else if (parm == PARM_mtime) {
			action_mtime *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(mtime);
			ap->mtime_char = arg1[0];
			ap->mtime_days = xatoul(plus_minus_num(arg1));
		}
#endif
#if ENABLE_FEATURE_FIND_MMIN
		else if (parm == PARM_mmin) {
			action_mmin *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(mmin);
			ap->mmin_char = arg1[0];
			ap->mmin_mins = xatoul(plus_minus_num(arg1));
		}
#endif
#if ENABLE_FEATURE_FIND_NEWER
		else if (parm == PARM_newer) {
			struct stat stat_newer;
			action_newer *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(newer);
			xstat(arg1, &stat_newer);
			ap->newer_mtime = stat_newer.st_mtime;
		}
#endif
#if ENABLE_FEATURE_FIND_INUM
		else if (parm == PARM_inum) {
			action_inum *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(inum);
			ap->inode_num = xatoul(arg1);
		}
#endif
#if ENABLE_FEATURE_FIND_USER
		else if (parm == PARM_user) {
			action_user *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(user);
			ap->uid = bb_strtou(arg1, NULL, 10);
			if (errno)
				ap->uid = xuname2uid(arg1);
		}
#endif
#if ENABLE_FEATURE_FIND_GROUP
		else if (parm == PARM_group) {
			action_group *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(group);
			ap->gid = bb_strtou(arg1, NULL, 10);
			if (errno)
				ap->gid = xgroup2gid(arg1);
		}
#endif
#if ENABLE_FEATURE_FIND_SIZE
		else if (parm == PARM_size) {
/* -size n[bckw]: file uses n units of space
 * b (default): units are 512-byte blocks
 * c: 1 byte
 * k: kilobytes
 * w: 2-byte words
 */
#if ENABLE_LFS
#define XATOU_SFX xatoull_sfx
#else
#define XATOU_SFX xatoul_sfx
#endif
			static const struct suffix_mult find_suffixes[] = {
				{ "c", 1 },
				{ "w", 2 },
				{ "", 512 },
				{ "b", 512 },
				{ "k", 1024 },
				{ "", 0 }
			};
			action_size *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(size);
			ap->size_char = arg1[0];
			ap->size = XATOU_SFX(plus_minus_num(arg1), find_suffixes);
		}
#endif
#if ENABLE_FEATURE_FIND_CONTEXT
		else if (parm == PARM_context) {
			action_context *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(context);
			/*ap->context = NULL; - ALLOC_ACTION did it */
			/* SELinux headers erroneously declare non-const parameter */
			if (selinux_raw_to_trans_context((char*)arg1, &ap->context))
				bb_simple_perror_msg(arg1);
		}
#endif
#if ENABLE_FEATURE_FIND_LINKS
		else if (parm == PARM_links) {
			action_links *ap;
			dbg("%d", __LINE__);
			ap = ALLOC_ACTION(links);
			ap->links_char = arg1[0];
			ap->links_count = xatoul(plus_minus_num(arg1));
		}
#endif
		else {
			bb_error_msg("unrecognized: %s", arg);
			bb_show_usage();
		}
		argv++;
	}
	dbg("exiting %s", __func__);
	return appp;
#undef ALLOC_ACTION
#undef appp
#undef cur_action
#undef invert_flag
}

int find_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int find_main(int argc UNUSED_PARAM, char **argv)
{
	int i, firstopt;
	char **past_HLP, *saved;

	INIT_G();

	/* "find -type f" + getopt("+HLP") => disaster.
	 * Need to avoid getopt running into a non-HLP option.
	 * Do this by temporarily storing NULL there:
	 */
	past_HLP = argv;
	for (;;) {
		saved = *++past_HLP;
		if (!saved)
			break;
		if (saved[0] != '-')
			break;
		if (!saved[1])
			break; /* it is "-" */
		if ((saved+1)[strspn(saved+1, "HLP")] != '\0')
			break;
	}
	*past_HLP = NULL;
	/* "+": stop on first non-option */
	i = getopt32(argv, "+HLP");
	if (i & (1<<0))
		G.recurse_flags |= ACTION_FOLLOWLINKS_L0 | ACTION_DANGLING_OK;
	if (i & (1<<1))
		G.recurse_flags |= ACTION_FOLLOWLINKS | ACTION_DANGLING_OK;
	/* -P is default and is ignored */
	argv = past_HLP; /* same result as "argv += optind;" */
	*past_HLP = saved;

	for (firstopt = 0; argv[firstopt]; firstopt++) {
		if (argv[firstopt][0] == '-')
			break;
		if (ENABLE_FEATURE_FIND_NOT && LONE_CHAR(argv[firstopt], '!'))
			break;
		if (ENABLE_FEATURE_FIND_PAREN && LONE_CHAR(argv[firstopt], '('))
			break;
	}
	if (firstopt == 0) {
		*--argv = (char*)".";
		firstopt++;
	}

	G.actions = parse_params(&argv[firstopt]);
	argv[firstopt] = NULL;

#if ENABLE_FEATURE_FIND_XDEV
	if (G.xdev_on) {
		struct stat stbuf;

		G.xdev_count = firstopt;
		G.xdev_dev = xzalloc(G.xdev_count * sizeof(G.xdev_dev[0]));
		for (i = 0; argv[i]; i++) {
			/* not xstat(): shouldn't bomb out on
			 * "find not_exist exist -xdev" */
			if (stat(argv[i], &stbuf) == 0)
				G.xdev_dev[i] = stbuf.st_dev;
			/* else G.xdev_dev[i] stays 0 and
			 * won't match any real device dev_t
			 */
		}
	}
#endif

	for (i = 0; argv[i]; i++) {
		if (!recursive_action(argv[i],
				G.recurse_flags,/* flags */
				fileAction,     /* file action */
				fileAction,     /* dir action */
				NULL,           /* user data */
				0)              /* depth */
		) {
			G.exitstatus |= EXIT_FAILURE;
		}
	}

	IF_FEATURE_FIND_EXEC_PLUS(G.exitstatus |= flush_exec_plus();)
	return G.exitstatus;
}
