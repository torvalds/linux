/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/*
	devfsd implementation for busybox

	Copyright (C) 2003 by Tito Ragusa <farmatito@tiscali.it>

	Busybox version is based on some previous work and ideas
	Copyright (C) [2003] by [Matteo Croce] <3297627799@wind.it>

	devfsd.c

	Main file for  devfsd  (devfs daemon for Linux).

    Copyright (C) 1998-2002  Richard Gooch

	devfsd.h

    Header file for  devfsd  (devfs daemon for Linux).

    Copyright (C) 1998-2000  Richard Gooch

	compat_name.c

    Compatibility name file for  devfsd  (build compatibility names).

    Copyright (C) 1998-2002  Richard Gooch

	expression.c

    This code provides Borne Shell-like expression expansion.

    Copyright (C) 1997-1999  Richard Gooch

	This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.
*/
//config:config DEVFSD
//config:	bool "devfsd (obsolete)"
//config:	default n
//config:	select PLATFORM_LINUX
//config:	select FEATURE_SYSLOG
//config:	help
//config:	This is deprecated and should NOT be used anymore.
//config:	Use linux >= 2.6 (optionally with hotplug) and mdev instead!
//config:	See docs/mdev.txt for detailed instructions on how to use mdev
//config:	instead.
//config:
//config:	Provides compatibility with old device names on a devfs systems.
//config:	You should set it to true if you have devfs enabled.
//config:	The following keywords in devsfd.conf are supported:
//config:	"CLEAR_CONFIG", "INCLUDE", "OPTIONAL_INCLUDE", "RESTORE",
//config:	"PERMISSIONS", "EXECUTE", "COPY", "IGNORE",
//config:	"MKOLDCOMPAT", "MKNEWCOMPAT","RMOLDCOMPAT", "RMNEWCOMPAT".
//config:
//config:	But only if they are written UPPERCASE!!!!!!!!
//config:
//config:config DEVFSD_MODLOAD
//config:	bool "Adds support for MODLOAD keyword in devsfd.conf"
//config:	default y
//config:	depends on DEVFSD
//config:	help
//config:	This actually doesn't work with busybox modutils but needs
//config:	the external modutils.
//config:
//config:config DEVFSD_FG_NP
//config:	bool "Enable the -fg and -np options"
//config:	default y
//config:	depends on DEVFSD
//config:	help
//config:	-fg  Run the daemon in the foreground.
//config:	-np  Exit after parsing config. Do not poll for events.
//config:
//config:config DEVFSD_VERBOSE
//config:	bool "Increases logging (and size)"
//config:	default y
//config:	depends on DEVFSD
//config:	help
//config:	Increases logging to stderr or syslog.
//config:
//config:config FEATURE_DEVFS
//config:	bool "Use devfs names for all devices (obsolete)"
//config:	default n
//config:	select PLATFORM_LINUX
//config:	help
//config:	This is obsolete and should NOT be used anymore.
//config:	Use linux >= 2.6 (optionally with hotplug) and mdev instead!
//config:
//config:	For legacy systems -- if there is no way around devfsd -- this
//config:	tells busybox to look for names like /dev/loop/0 instead of
//config:	/dev/loop0. If your /dev directory has normal names instead of
//config:	devfs names, you don't want this.

//applet:IF_DEVFSD(APPLET(devfsd, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_DEVFSD) += devfsd.o

//usage:#define devfsd_trivial_usage
//usage:       "mntpnt [-v]" IF_DEVFSD_FG_NP("[-fg][-np]")
//usage:#define devfsd_full_usage "\n\n"
//usage:       "Manage devfs permissions and old device name symlinks\n"
//usage:     "\n	mntpnt	The mount point where devfs is mounted"
//usage:     "\n	-v	Print the protocol version numbers for devfsd"
//usage:     "\n		and the kernel-side protocol version and exit"
//usage:	IF_DEVFSD_FG_NP(
//usage:     "\n	-fg	Run in foreground"
//usage:     "\n	-np	Exit after parsing the configuration file"
//usage:     "\n		and processing synthetic REGISTER events,"
//usage:     "\n		don't poll for events"
//usage:	)

#include "libbb.h"
#include "xregex.h"
#include <syslog.h>

#include <sys/un.h>
#include <sys/sysmacros.h>

/* Various defines taken from linux/major.h */
#define IDE0_MAJOR	3
#define IDE1_MAJOR	22
#define IDE2_MAJOR	33
#define IDE3_MAJOR	34
#define IDE4_MAJOR	56
#define IDE5_MAJOR	57
#define IDE6_MAJOR	88
#define IDE7_MAJOR	89
#define IDE8_MAJOR	90
#define IDE9_MAJOR	91


/* Various defines taken from linux/devfs_fs.h */
#define DEVFSD_PROTOCOL_REVISION_KERNEL  5
#define DEVFSD_IOCTL_BASE	'd'
/*  These are the various ioctls  */
#define DEVFSDIOC_GET_PROTO_REV         _IOR(DEVFSD_IOCTL_BASE, 0, int)
#define DEVFSDIOC_SET_EVENT_MASK        _IOW(DEVFSD_IOCTL_BASE, 2, int)
#define DEVFSDIOC_RELEASE_EVENT_QUEUE   _IOW(DEVFSD_IOCTL_BASE, 3, int)
#define DEVFSDIOC_SET_CONFIG_DEBUG_MASK _IOW(DEVFSD_IOCTL_BASE, 4, int)
#define DEVFSD_NOTIFY_REGISTERED    0
#define DEVFSD_NOTIFY_UNREGISTERED  1
#define DEVFSD_NOTIFY_ASYNC_OPEN    2
#define DEVFSD_NOTIFY_CLOSE         3
#define DEVFSD_NOTIFY_LOOKUP        4
#define DEVFSD_NOTIFY_CHANGE        5
#define DEVFSD_NOTIFY_CREATE        6
#define DEVFSD_NOTIFY_DELETE        7
#define DEVFS_PATHLEN               1024
/*  Never change this otherwise the binary interface will change   */

struct devfsd_notify_struct {
	/*  Use native C types to ensure same types in kernel and user space     */
	unsigned int type;           /*  DEVFSD_NOTIFY_* value                   */
	unsigned int mode;           /*  Mode of the inode or device entry       */
	unsigned int major;          /*  Major number of device entry            */
	unsigned int minor;          /*  Minor number of device entry            */
	unsigned int uid;            /*  Uid of process, inode or device entry   */
	unsigned int gid;            /*  Gid of process, inode or device entry   */
	unsigned int overrun_count;  /*  Number of lost events                   */
	unsigned int namelen;        /*  Number of characters not including '\0' */
	/*  The device name MUST come last                                       */
	char devname[DEVFS_PATHLEN]; /*  This will be '\0' terminated            */
};

#define BUFFER_SIZE 16384
#define DEVFSD_VERSION "1.3.25"
#define CONFIG_FILE  "/etc/devfsd.conf"
#define MODPROBE		"/sbin/modprobe"
#define MODPROBE_SWITCH_1 "-k"
#define MODPROBE_SWITCH_2 "-C"
#define CONFIG_MODULES_DEVFS "/etc/modules.devfs"
#define MAX_ARGS     (6 + 1)
#define MAX_SUBEXPR  10
#define STRING_LENGTH 255

/* for get_uid_gid() */
#define UID			0
#define GID			1

/* fork_and_execute() */
# define DIE			1
# define NO_DIE			0

/* for dir_operation() */
#define RESTORE		0
#define SERVICE		1
#define READ_CONFIG 2

/*  Update only after changing code to reflect new protocol  */
#define DEVFSD_PROTOCOL_REVISION_DAEMON  5

/*  Compile-time check  */
#if DEVFSD_PROTOCOL_REVISION_KERNEL != DEVFSD_PROTOCOL_REVISION_DAEMON
#error protocol version mismatch. Update your kernel headers
#endif

#define AC_PERMISSIONS				0
#define AC_MODLOAD					1
#define AC_EXECUTE					2
#define AC_MFUNCTION				3	/* not supported by busybox */
#define AC_CFUNCTION				4	/* not supported by busybox */
#define AC_COPY						5
#define AC_IGNORE					6
#define AC_MKOLDCOMPAT				7
#define AC_MKNEWCOMPAT				8
#define AC_RMOLDCOMPAT				9
#define AC_RMNEWCOMPAT				10
#define AC_RESTORE					11

struct permissions_type {
	mode_t mode;
	uid_t uid;
	gid_t gid;
};

struct execute_type {
	char *argv[MAX_ARGS + 1];  /*  argv[0] must always be the programme  */
};

struct copy_type {
	const char *source;
	const char *destination;
};

struct action_type {
	unsigned int what;
	unsigned int when;
};

struct config_entry_struct {
	struct action_type action;
	regex_t preg;
	union
	{
	struct permissions_type permissions;
	struct execute_type execute;
	struct copy_type copy;
	}
	u;
	struct config_entry_struct *next;
};

struct get_variable_info {
	const struct devfsd_notify_struct *info;
	const char *devname;
	char devpath[STRING_LENGTH];
};

static void dir_operation(int , const char * , int,  unsigned long*);
static void service(struct stat statbuf, char *path);
static int st_expr_expand(char *, unsigned, const char *, const char *(*)(const char *, void *), void *);
static const char *get_old_name(const char *, unsigned, char *, unsigned, unsigned);
static int mksymlink(const char *oldpath, const char *newpath);
static void read_config_file(char *path, int optional, unsigned long *event_mask);
static void process_config_line(const char *, unsigned long *);
static int  do_servicing(int, unsigned long);
static void service_name(const struct devfsd_notify_struct *);
static void action_permissions(const struct devfsd_notify_struct *, const struct config_entry_struct *);
static void action_execute(const struct devfsd_notify_struct *, const struct config_entry_struct *,
							const regmatch_t *, unsigned);
static void action_modload(const struct devfsd_notify_struct *info, const struct config_entry_struct *entry);
static void action_copy(const struct devfsd_notify_struct *, const struct config_entry_struct *,
						const regmatch_t *, unsigned);
static void action_compat(const struct devfsd_notify_struct *, unsigned);
static void free_config(void);
static void restore(char *spath, struct stat source_stat, int rootlen);
static int copy_inode(const char *, const struct stat *, mode_t, const char *, const struct stat *);
static mode_t get_mode(const char *);
static void signal_handler(int);
static const char *get_variable(const char *, void *);
static int make_dir_tree(const char *);
static int expand_expression(char *, unsigned, const char *, const char *(*)(const char *, void *), void *,
							const char *, const regmatch_t *, unsigned);
static void expand_regexp(char *, size_t, const char *, const char *, const regmatch_t *, unsigned);
static const char *expand_variable(	char *, unsigned, unsigned *, const char *,
									const char *(*)(const char *, void *), void *);
static const char *get_variable_v2(const char *, const char *(*)(const char *, void *), void *);
static char get_old_ide_name(unsigned, unsigned);
static char *write_old_sd_name(char *, unsigned, unsigned, const char *);

/* busybox functions */
static int get_uid_gid(int flag, const char *string);
static void safe_memcpy(char * dest, const char * src, int len);
static unsigned int scan_dev_name_common(const char *d, unsigned int n, int addendum, const char *ptr);
static unsigned int scan_dev_name(const char *d, unsigned int n, const char *ptr);

/* Structs and vars */
static struct config_entry_struct *first_config = NULL;
static struct config_entry_struct *last_config = NULL;
static char *mount_point = NULL;
static volatile int caught_signal = FALSE;
static volatile int caught_sighup = FALSE;
static struct initial_symlink_struct {
	const char *dest;
	const char *name;
} initial_symlinks[] = {
	{"/proc/self/fd", "fd"},
	{"fd/0", "stdin"},
	{"fd/1", "stdout"},
	{"fd/2", "stderr"},
	{NULL, NULL},
};

static struct event_type {
	unsigned int type;        /*  The DEVFSD_NOTIFY_* value                  */
	const char *config_name;  /*  The name used in the config file           */
} event_types[] = {
	{DEVFSD_NOTIFY_REGISTERED,   "REGISTER"},
	{DEVFSD_NOTIFY_UNREGISTERED, "UNREGISTER"},
	{DEVFSD_NOTIFY_ASYNC_OPEN,   "ASYNC_OPEN"},
	{DEVFSD_NOTIFY_CLOSE,        "CLOSE"},
	{DEVFSD_NOTIFY_LOOKUP,       "LOOKUP"},
	{DEVFSD_NOTIFY_CHANGE,       "CHANGE"},
	{DEVFSD_NOTIFY_CREATE,       "CREATE"},
	{DEVFSD_NOTIFY_DELETE,       "DELETE"},
	{0xffffffff,                 NULL}
};

/* Busybox messages */

static const char bb_msg_proto_rev[] ALIGN1          = "protocol revision";
static const char bb_msg_bad_config[] ALIGN1         = "bad %s config file: %s";
static const char bb_msg_small_buffer[] ALIGN1       = "buffer too small";
static const char bb_msg_variable_not_found[] ALIGN1 = "variable: %s not found";

/* Busybox stuff */
#if ENABLE_DEVFSD_VERBOSE || ENABLE_DEBUG
#define info_logger(p, fmt, args...)                 bb_error_msg(fmt, ## args)
#define msg_logger(p, fmt, args...)                  bb_error_msg(fmt, ## args)
#define msg_logger_and_die(p, fmt, args...)          bb_error_msg_and_die(fmt, ## args)
#define error_logger(p, fmt, args...)                bb_perror_msg(fmt, ## args)
#define error_logger_and_die(p, fmt, args...)        bb_perror_msg_and_die(fmt, ## args)
#else
#define info_logger(p, fmt, args...)
#define msg_logger(p, fmt, args...)
#define msg_logger_and_die(p, fmt, args...)           exit(EXIT_FAILURE)
#define error_logger(p, fmt, args...)
#define error_logger_and_die(p, fmt, args...)         exit(EXIT_FAILURE)
#endif

static void safe_memcpy(char *dest, const char *src, int len)
{
	memcpy(dest , src, len);
	dest[len] = '\0';
}

static unsigned int scan_dev_name_common(const char *d, unsigned int n, int addendum, const char *ptr)
{
	if (d[n - 4] == 'd' && d[n - 3] == 'i' && d[n - 2] == 's' && d[n - 1] == 'c')
		return 2 + addendum;
	if (d[n - 2] == 'c' && d[n - 1] == 'd')
		return 3 + addendum;
	if (ptr[0] == 'p' && ptr[1] == 'a' && ptr[2] == 'r' && ptr[3] == 't')
		return 4 + addendum;
	if (ptr[n - 2] == 'm' && ptr[n - 1] == 't')
		return 5 + addendum;
	return 0;
}

static unsigned int scan_dev_name(const char *d, unsigned int n, const char *ptr)
{
	if (d[0] == 's' && d[1] == 'c' && d[2] == 's' && d[3] == 'i' && d[4] == '/') {
		if (d[n - 7] == 'g' && d[n - 6] == 'e' && d[n - 5] == 'n'
			&& d[n - 4] == 'e' && d[n - 3] == 'r' && d[n - 2] == 'i' && d[n - 1] == 'c'
		)
			return 1;
		return scan_dev_name_common(d, n, 0, ptr);
	}
	if (d[0] == 'i' && d[1] == 'd' && d[2] == 'e' && d[3] == '/'
		&& d[4] == 'h' && d[5] == 'o' && d[6] == 's' && d[7] == 't'
	)
		return scan_dev_name_common(d, n, 4, ptr);
	if (d[0] == 's' && d[1] == 'b' && d[2] == 'p' && d[3] == '/')
		return 10;
	if (d[0] == 'v' && d[1] == 'c' && d[2] == 'c' && d[3] == '/')
		return 11;
	if (d[0] == 'p' && d[1] == 't' && d[2] == 'y' && d[3] == '/')
		return 12;
	return 0;
}

/*  Public functions follow  */

int devfsd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int devfsd_main(int argc, char **argv)
{
	int print_version = FALSE;
	int do_daemon = TRUE;
	int no_polling = FALSE;
	int do_scan;
	int fd, proto_rev, count;
	unsigned long event_mask = 0;
	struct sigaction new_action;
	struct initial_symlink_struct *curr;

	if (argc < 2)
		bb_show_usage();

	for (count = 2; count < argc; ++count) {
		if (argv[count][0] == '-') {
			if (argv[count][1] == 'v' && !argv[count][2]) /* -v */
				print_version = TRUE;
			else if (ENABLE_DEVFSD_FG_NP && argv[count][1] == 'f'
			 && argv[count][2] == 'g' && !argv[count][3]) /* -fg */
				do_daemon = FALSE;
			else if (ENABLE_DEVFSD_FG_NP && argv[count][1] == 'n'
			 && argv[count][2] == 'p' && !argv[count][3]) /* -np */
				no_polling = TRUE;
			else
				bb_show_usage();
		}
	}

	mount_point = bb_simplify_path(argv[1]);

	xchdir(mount_point);

	fd = xopen(".devfsd", O_RDONLY);
	close_on_exec_on(fd);
	xioctl(fd, DEVFSDIOC_GET_PROTO_REV, &proto_rev);

	/*setup initial entries */
	for (curr = initial_symlinks; curr->dest != NULL; ++curr)
		symlink(curr->dest, curr->name);

	/* NB: The check for CONFIG_FILE is done in read_config_file() */

	if (print_version || (DEVFSD_PROTOCOL_REVISION_DAEMON != proto_rev)) {
		printf("%s v%s\nDaemon %s:\t%d\nKernel-side %s:\t%d\n",
				applet_name, DEVFSD_VERSION, bb_msg_proto_rev,
				DEVFSD_PROTOCOL_REVISION_DAEMON, bb_msg_proto_rev, proto_rev);
		if (DEVFSD_PROTOCOL_REVISION_DAEMON != proto_rev)
			bb_error_msg_and_die("%s mismatch!", bb_msg_proto_rev);
		exit(EXIT_SUCCESS); /* -v */
	}
	/*  Tell kernel we are special(i.e. we get to see hidden entries)  */
	xioctl(fd, DEVFSDIOC_SET_EVENT_MASK, 0);

	/*  Set up SIGHUP and SIGUSR1 handlers  */
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	new_action.sa_handler = signal_handler;
	sigaction_set(SIGHUP, &new_action);
	sigaction_set(SIGUSR1, &new_action);

	printf("%s v%s started for %s\n", applet_name, DEVFSD_VERSION, mount_point);

	/*  Set umask so that mknod(2), open(2) and mkdir(2) have complete control over permissions  */
	umask(0);
	read_config_file((char*)CONFIG_FILE, FALSE, &event_mask);
	/*  Do the scan before forking, so that boot scripts see the finished product  */
	dir_operation(SERVICE, mount_point, 0, NULL);

	if (ENABLE_DEVFSD_FG_NP && no_polling)
		exit(EXIT_SUCCESS);

	if (ENABLE_DEVFSD_VERBOSE || ENABLE_DEBUG)
		logmode = LOGMODE_BOTH;
	else if (do_daemon == TRUE)
		logmode = LOGMODE_SYSLOG;
	/* This is the default */
	/*else
		logmode = LOGMODE_STDIO; */

	if (do_daemon) {
		/*  Release so that the child can grab it  */
		xioctl(fd, DEVFSDIOC_RELEASE_EVENT_QUEUE, 0);
		bb_daemonize_or_rexec(0, argv);
	} else if (ENABLE_DEVFSD_FG_NP) {
		setpgid(0, 0);  /*  Become process group leader                    */
	}

	while (TRUE) {
		do_scan = do_servicing(fd, event_mask);

		free_config();
		read_config_file((char*)CONFIG_FILE, FALSE, &event_mask);
		if (do_scan)
			dir_operation(SERVICE, mount_point, 0, NULL);
	}
	if (ENABLE_FEATURE_CLEAN_UP) free(mount_point);
}   /*  End Function main  */


/*  Private functions follow  */

static void read_config_file(char *path, int optional, unsigned long *event_mask)
/*  [SUMMARY] Read a configuration database.
    <path> The path to read the database from. If this is a directory, all
    entries in that directory will be read(except hidden entries).
    <optional> If TRUE, the routine will silently ignore a missing config file.
    <event_mask> The event mask is written here. This is not initialised.
    [RETURNS] Nothing.
*/
{
	struct stat statbuf;
	FILE *fp;
	char buf[STRING_LENGTH];
	char *line = NULL;
	char *p;

	if (stat(path, &statbuf) == 0) {
		/* Don't read 0 length files: ignored */
		/*if (statbuf.st_size == 0)
				return;*/
		if (S_ISDIR(statbuf.st_mode)) {
			p = bb_simplify_path(path);
			dir_operation(READ_CONFIG, p, 0, event_mask);
			free(p);
			return;
		}
		fp = fopen_for_read(path);
		if (fp != NULL) {
			while (fgets(buf, STRING_LENGTH, fp) != NULL) {
				/*  Skip whitespace  */
				line = buf;
				line = skip_whitespace(line);
				if (line[0] == '\0' || line[0] == '#')
					continue;
				process_config_line(line, event_mask);
			}
			fclose(fp);
		} else {
			goto read_config_file_err;
		}
	} else {
read_config_file_err:
		if (optional == 0 && errno == ENOENT)
			error_logger_and_die(LOG_ERR, "read config file: %s", path);
	}
}   /*  End Function read_config_file   */

static void process_config_line(const char *line, unsigned long *event_mask)
/*  [SUMMARY] Process a line from a configuration file.
    <line> The configuration line.
    <event_mask> The event mask is written here. This is not initialised.
    [RETURNS] Nothing.
*/
{
	int  num_args, count;
	struct config_entry_struct *new;
	char p[MAX_ARGS][STRING_LENGTH];
	char when[STRING_LENGTH], what[STRING_LENGTH];
	char name[STRING_LENGTH];
	const char *msg = "";
	char *ptr;
	int i;

	/* !!!! Only Uppercase Keywords in devsfd.conf */
	static const char options[] ALIGN1 =
		"CLEAR_CONFIG\0""INCLUDE\0""OPTIONAL_INCLUDE\0"
		"RESTORE\0""PERMISSIONS\0""MODLOAD\0""EXECUTE\0"
		"COPY\0""IGNORE\0""MKOLDCOMPAT\0""MKNEWCOMPAT\0"
		"RMOLDCOMPAT\0""RMNEWCOMPAT\0";

	for (count = 0; count < MAX_ARGS; ++count)
		p[count][0] = '\0';
	num_args = sscanf(line, "%s %s %s %s %s %s %s %s %s %s",
			when, name, what,
			p[0], p[1], p[2], p[3], p[4], p[5], p[6]);

	i = index_in_strings(options, when);

	/* "CLEAR_CONFIG" */
	if (i == 0) {
		free_config();
		*event_mask = 0;
		return;
	}

	if (num_args < 2)
		goto process_config_line_err;

	/* "INCLUDE" & "OPTIONAL_INCLUDE" */
	if (i == 1 || i == 2) {
		st_expr_expand(name, STRING_LENGTH, name, get_variable, NULL);
		info_logger(LOG_INFO, "%sinclude: %s", (toupper(when[0]) == 'I') ? "": "optional_", name);
		read_config_file(name, (toupper(when[0]) == 'I') ? FALSE : TRUE, event_mask);
		return;
	}
	/* "RESTORE" */
	if (i == 3) {
		dir_operation(RESTORE, name, strlen(name),NULL);
		return;
	}
	if (num_args < 3)
		goto process_config_line_err;

	new = xzalloc(sizeof *new);

	for (count = 0; event_types[count].config_name != NULL; ++count) {
		if (strcasecmp(when, event_types[count].config_name) != 0)
			continue;
		new->action.when = event_types[count].type;
		break;
	}
	if (event_types[count].config_name == NULL) {
		msg = "WHEN in";
		goto process_config_line_err;
	}

	i = index_in_strings(options, what);

	switch (i) {
		case 4:	/* "PERMISSIONS" */
			new->action.what = AC_PERMISSIONS;
			/*  Get user and group  */
			ptr = strchr(p[0], '.');
			if (ptr == NULL) {
				msg = "UID.GID";
				goto process_config_line_err; /*"missing '.' in UID.GID"*/
			}

			*ptr++ = '\0';
			new->u.permissions.uid = get_uid_gid(UID, p[0]);
			new->u.permissions.gid = get_uid_gid(GID, ptr);
			/*  Get mode  */
			new->u.permissions.mode = get_mode(p[1]);
			break;
		case 5:	/*  MODLOAD */
			/*This  action will pass "/dev/$devname"(i.e. "/dev/" prefixed to
			the device name) to the module loading  facility.  In  addition,
			the /etc/modules.devfs configuration file is used.*/
			if (ENABLE_DEVFSD_MODLOAD)
				new->action.what = AC_MODLOAD;
			break;
		case 6: /* EXECUTE */
			new->action.what = AC_EXECUTE;
			num_args -= 3;

			for (count = 0; count < num_args; ++count)
				new->u.execute.argv[count] = xstrdup(p[count]);

			new->u.execute.argv[num_args] = NULL;
			break;
		case 7: /* COPY */
			new->action.what = AC_COPY;
			num_args -= 3;
			if (num_args != 2)
				goto process_config_line_err; /* missing path and function in line */

			new->u.copy.source = xstrdup(p[0]);
			new->u.copy.destination = xstrdup(p[1]);
			break;
		case 8: /* IGNORE */
		/* FALLTROUGH */
		case 9: /* MKOLDCOMPAT */
		/* FALLTROUGH */
		case 10: /* MKNEWCOMPAT */
		/* FALLTROUGH */
		case 11:/* RMOLDCOMPAT */
		/* FALLTROUGH */
		case 12: /* RMNEWCOMPAT */
		/*	AC_IGNORE					6
			AC_MKOLDCOMPAT				7
			AC_MKNEWCOMPAT				8
			AC_RMOLDCOMPAT				9
			AC_RMNEWCOMPAT				10*/
			new->action.what = i - 2;
			break;
		default:
			msg = "WHAT in";
			goto process_config_line_err;
		/*esac*/
	} /* switch (i) */

	xregcomp(&new->preg, name, REG_EXTENDED);

	*event_mask |= 1 << new->action.when;
	new->next = NULL;
	if (first_config == NULL)
		first_config = new;
	else
		last_config->next = new;
	last_config = new;
	return;

 process_config_line_err:
	msg_logger_and_die(LOG_ERR, bb_msg_bad_config, msg , line);
}  /*  End Function process_config_line   */

static int do_servicing(int fd, unsigned long event_mask)
/*  [SUMMARY] Service devfs changes until a signal is received.
    <fd> The open control file.
    <event_mask> The event mask.
    [RETURNS] TRUE if SIGHUP was caught, else FALSE.
*/
{
	ssize_t bytes;
	struct devfsd_notify_struct info;

	/* (void*) cast is only in order to match prototype */
	xioctl(fd, DEVFSDIOC_SET_EVENT_MASK, (void*)event_mask);
	while (!caught_signal) {
		errno = 0;
		bytes = read(fd, (char *) &info, sizeof info);
		if (caught_signal)
			break;      /*  Must test for this first     */
		if (errno == EINTR)
			continue;  /*  Yes, the order is important  */
		if (bytes < 1)
			break;
		service_name(&info);
	}
	if (caught_signal) {
		int c_sighup = caught_sighup;

		caught_signal = FALSE;
		caught_sighup = FALSE;
		return c_sighup;
	}
	msg_logger_and_die(LOG_ERR, "read error on control file");
}   /*  End Function do_servicing  */

static void service_name(const struct devfsd_notify_struct *info)
/*  [SUMMARY] Service a single devfs change.
    <info> The devfs change.
    [RETURNS] Nothing.
*/
{
	unsigned int n;
	regmatch_t mbuf[MAX_SUBEXPR];
	struct config_entry_struct *entry;

	if (ENABLE_DEBUG && info->overrun_count > 0)
		msg_logger(LOG_ERR, "lost %u events", info->overrun_count);

	/*  Discard lookups on "/dev/log" and "/dev/initctl"  */
	if (info->type == DEVFSD_NOTIFY_LOOKUP
		&& ((info->devname[0] == 'l' && info->devname[1] == 'o'
		&& info->devname[2] == 'g' && !info->devname[3])
		|| (info->devname[0] == 'i' && info->devname[1] == 'n'
		&& info->devname[2] == 'i' && info->devname[3] == 't'
		&& info->devname[4] == 'c' && info->devname[5] == 't'
		&& info->devname[6] == 'l' && !info->devname[7]))
	)
		return;

	for (entry = first_config; entry != NULL; entry = entry->next) {
		/*  First check if action matches the type, then check if name matches */
		if (info->type != entry->action.when
		|| regexec(&entry->preg, info->devname, MAX_SUBEXPR, mbuf, 0) != 0)
			continue;
		for (n = 0;(n < MAX_SUBEXPR) && (mbuf[n].rm_so != -1); ++n)
			/* VOID */;

		switch (entry->action.what) {
			case AC_PERMISSIONS:
				action_permissions(info, entry);
				break;
			case AC_MODLOAD:
				if (ENABLE_DEVFSD_MODLOAD)
					action_modload(info, entry);
				break;
			case AC_EXECUTE:
				action_execute(info, entry, mbuf, n);
				break;
			case AC_COPY:
				action_copy(info, entry, mbuf, n);
				break;
			case AC_IGNORE:
				return;
				/*break;*/
			case AC_MKOLDCOMPAT:
			case AC_MKNEWCOMPAT:
			case AC_RMOLDCOMPAT:
			case AC_RMNEWCOMPAT:
				action_compat(info, entry->action.what);
				break;
			default:
				msg_logger_and_die(LOG_ERR, "Unknown action");
		}
	}
}   /*  End Function service_name  */

static void action_permissions(const struct devfsd_notify_struct *info,
				const struct config_entry_struct *entry)
/*  [SUMMARY] Update permissions for a device entry.
    <info> The devfs change.
    <entry> The config file entry.
    [RETURNS] Nothing.
*/
{
	struct stat statbuf;

	if (stat(info->devname, &statbuf) != 0
	 || chmod(info->devname, (statbuf.st_mode & S_IFMT) | (entry->u.permissions.mode & ~S_IFMT)) != 0
	 || chown(info->devname, entry->u.permissions.uid, entry->u.permissions.gid) != 0
	)
		error_logger(LOG_ERR, "Can't chmod or chown: %s", info->devname);
}   /*  End Function action_permissions  */

static void action_modload(const struct devfsd_notify_struct *info,
			const struct config_entry_struct *entry UNUSED_PARAM)
/*  [SUMMARY] Load a module.
    <info> The devfs change.
    <entry> The config file entry.
    [RETURNS] Nothing.
*/
{
	char *argv[6];

	argv[0] = (char*)MODPROBE;
	argv[1] = (char*)MODPROBE_SWITCH_1; /* "-k" */
	argv[2] = (char*)MODPROBE_SWITCH_2; /* "-C" */
	argv[3] = (char*)CONFIG_MODULES_DEVFS;
	argv[4] = concat_path_file("/dev", info->devname); /* device */
	argv[5] = NULL;

	spawn_and_wait(argv);
	free(argv[4]);
}  /*  End Function action_modload  */

static void action_execute(const struct devfsd_notify_struct *info,
			const struct config_entry_struct *entry,
			const regmatch_t *regexpr, unsigned int numexpr)
/*  [SUMMARY] Execute a programme.
    <info> The devfs change.
    <entry> The config file entry.
    <regexpr> The number of subexpression(start, end) offsets within the
    device name.
    <numexpr> The number of elements within <<regexpr>>.
    [RETURNS] Nothing.
*/
{
	unsigned int count;
	struct get_variable_info gv_info;
	char *argv[MAX_ARGS + 1];
	char largv[MAX_ARGS + 1][STRING_LENGTH];

	gv_info.info = info;
	gv_info.devname = info->devname;
	snprintf(gv_info.devpath, sizeof(gv_info.devpath), "%s/%s", mount_point, info->devname);
	for (count = 0; entry->u.execute.argv[count] != NULL; ++count) {
		expand_expression(largv[count], STRING_LENGTH,
				entry->u.execute.argv[count],
				get_variable, &gv_info,
				gv_info.devname, regexpr, numexpr);
		argv[count] = largv[count];
	}
	argv[count] = NULL;
	spawn_and_wait(argv);
}   /*  End Function action_execute  */


static void action_copy(const struct devfsd_notify_struct *info,
			const struct config_entry_struct *entry,
			const regmatch_t *regexpr, unsigned int numexpr)
/*  [SUMMARY] Copy permissions.
    <info> The devfs change.
    <entry> The config file entry.
    <regexpr> This list of subexpression(start, end) offsets within the
    device name.
    <numexpr> The number of elements in <<regexpr>>.
    [RETURNS] Nothing.
*/
{
	mode_t new_mode;
	struct get_variable_info gv_info;
	struct stat source_stat, dest_stat;
	char source[STRING_LENGTH], destination[STRING_LENGTH];
	int ret = 0;

	dest_stat.st_mode = 0;

	if ((info->type == DEVFSD_NOTIFY_CHANGE) && S_ISLNK(info->mode))
		return;
	gv_info.info = info;
	gv_info.devname = info->devname;

	snprintf(gv_info.devpath, sizeof(gv_info.devpath), "%s/%s", mount_point, info->devname);
	expand_expression(source, STRING_LENGTH, entry->u.copy.source,
				get_variable, &gv_info, gv_info.devname,
				regexpr, numexpr);

	expand_expression(destination, STRING_LENGTH, entry->u.copy.destination,
				get_variable, &gv_info, gv_info.devname,
				regexpr, numexpr);

	if (!make_dir_tree(destination) || lstat(source, &source_stat) != 0)
			return;
	lstat(destination, &dest_stat);
	new_mode = source_stat.st_mode & ~S_ISVTX;
	if (info->type == DEVFSD_NOTIFY_CREATE)
		new_mode |= S_ISVTX;
	else if ((info->type == DEVFSD_NOTIFY_CHANGE) &&(dest_stat.st_mode & S_ISVTX))
		new_mode |= S_ISVTX;
	ret = copy_inode(destination, &dest_stat, new_mode, source, &source_stat);
	if (ENABLE_DEBUG && ret && (errno != EEXIST))
		error_logger(LOG_ERR, "copy_inode: %s to %s", source, destination);
}   /*  End Function action_copy  */

static void action_compat(const struct devfsd_notify_struct *info, unsigned int action)
/*  [SUMMARY] Process a compatibility request.
    <info> The devfs change.
    <action> The action to take.
    [RETURNS] Nothing.
*/
{
	int ret;
	const char *compat_name = NULL;
	const char *dest_name = info->devname;
	const char *ptr;
	char compat_buf[STRING_LENGTH], dest_buf[STRING_LENGTH];
	int mode, host, bus, target, lun;
	unsigned int i;
	char rewind_;
	/* 1 to 5  "scsi/" , 6 to 9 "ide/host" */
	static const char *const fmt[] = {
		NULL ,
		"sg/c%db%dt%du%d",		/* scsi/generic */
		"sd/c%db%dt%du%d",		/* scsi/disc */
		"sr/c%db%dt%du%d",		/* scsi/cd */
		"sd/c%db%dt%du%dp%d",		/* scsi/part */
		"st/c%db%dt%du%dm%d%c",		/* scsi/mt */
		"ide/hd/c%db%dt%du%d",		/* ide/host/disc */
		"ide/cd/c%db%dt%du%d",		/* ide/host/cd */
		"ide/hd/c%db%dt%du%dp%d",	/* ide/host/part */
		"ide/mt/c%db%dt%du%d%s",	/* ide/host/mt */
		NULL
	};

	/*  First construct compatibility name  */
	switch (action) {
		case AC_MKOLDCOMPAT:
		case AC_RMOLDCOMPAT:
			compat_name = get_old_name(info->devname, info->namelen, compat_buf, info->major, info->minor);
			break;
		case AC_MKNEWCOMPAT:
		case AC_RMNEWCOMPAT:
			ptr = bb_basename(info->devname);
			i = scan_dev_name(info->devname, info->namelen, ptr);

			/* nothing found */
			if (i == 0 || i > 9)
				return;

			sscanf(info->devname + ((i < 6) ? 5 : 4), "host%d/bus%d/target%d/lun%d/", &host, &bus, &target, &lun);
			snprintf(dest_buf, sizeof(dest_buf), "../%s", info->devname + (( i > 5) ? 4 : 0));
			dest_name = dest_buf;
			compat_name = compat_buf;


			/* 1 == scsi/generic  2 == scsi/disc 3 == scsi/cd 6 == ide/host/disc 7 == ide/host/cd */
			if (i == 1 || i == 2 || i == 3 || i == 6 || i ==7)
				sprintf(compat_buf, fmt[i], host, bus, target, lun);

			/* 4 == scsi/part 8 == ide/host/part */
			if (i == 4 || i == 8)
				sprintf(compat_buf, fmt[i], host, bus, target, lun, atoi(ptr + 4));

			/* 5 == scsi/mt */
			if (i == 5) {
				rewind_ = info->devname[info->namelen - 1];
				if (rewind_ != 'n')
					rewind_ = '\0';
				mode=0;
				if (ptr[2] ==  'l' /*108*/ || ptr[2] == 'm'/*109*/)
					mode = ptr[2] - 107; /* 1 or 2 */
				if (ptr[2] ==  'a')
					mode = 3;
				sprintf(compat_buf, fmt[i], host, bus, target, lun, mode, rewind_);
			}

			/* 9 == ide/host/mt */
			if (i ==  9)
				snprintf(compat_buf, sizeof(compat_buf), fmt[i], host, bus, target, lun, ptr + 2);
		/* esac */
	} /* switch (action) */

	if (compat_name == NULL)
		return;

	/*  Now decide what to do with it  */
	switch (action) {
		case AC_MKOLDCOMPAT:
		case AC_MKNEWCOMPAT:
			mksymlink(dest_name, compat_name);
			break;
		case AC_RMOLDCOMPAT:
		case AC_RMNEWCOMPAT:
			ret = unlink(compat_name);
			if (ENABLE_DEBUG && ret)
				error_logger(LOG_ERR, "unlink: %s", compat_name);
			break;
		/*esac*/
	} /* switch (action) */
}   /*  End Function action_compat  */

static void restore(char *spath, struct stat source_stat, int rootlen)
{
	char *dpath;
	struct stat dest_stat;

	dest_stat.st_mode = 0;
	dpath = concat_path_file(mount_point, spath + rootlen);
	lstat(dpath, &dest_stat);
	free(dpath);
	if (S_ISLNK(source_stat.st_mode) || (source_stat.st_mode & S_ISVTX))
		copy_inode(dpath, &dest_stat, (source_stat.st_mode & ~S_ISVTX), spath, &source_stat);

	if (S_ISDIR(source_stat.st_mode))
		dir_operation(RESTORE, spath, rootlen, NULL);
}


static int copy_inode(const char *destpath, const struct stat *dest_stat,
			mode_t new_mode,
			const char *sourcepath, const struct stat *source_stat)
/*  [SUMMARY] Copy an inode.
    <destpath> The destination path. An existing inode may be deleted.
    <dest_stat> The destination stat(2) information.
    <new_mode> The desired new mode for the destination.
    <sourcepath> The source path.
    <source_stat> The source stat(2) information.
    [RETURNS] TRUE on success, else FALSE.
*/
{
	int source_len, dest_len;
	char source_link[STRING_LENGTH], dest_link[STRING_LENGTH];
	int fd, val;
	struct sockaddr_un un_addr;
	char symlink_val[STRING_LENGTH];

	if ((source_stat->st_mode & S_IFMT) ==(dest_stat->st_mode & S_IFMT)) {
		/*  Same type  */
		if (S_ISLNK(source_stat->st_mode)) {
			source_len = readlink(sourcepath, source_link, STRING_LENGTH - 1);
			if ((source_len < 0)
			 || (dest_len = readlink(destpath, dest_link, STRING_LENGTH - 1)) < 0
			)
				return FALSE;
			source_link[source_len]	= '\0';
			dest_link[dest_len]	= '\0';
			if ((source_len != dest_len) || (strcmp(source_link, dest_link) != 0)) {
				unlink(destpath);
				symlink(source_link, destpath);
			}
			return TRUE;
		}   /*  Else not a symlink  */
		chmod(destpath, new_mode & ~S_IFMT);
		chown(destpath, source_stat->st_uid, source_stat->st_gid);
		return TRUE;
	}
	/*  Different types: unlink and create  */
	unlink(destpath);
	switch (source_stat->st_mode & S_IFMT) {
		case S_IFSOCK:
			fd = socket(AF_UNIX, SOCK_STREAM, 0);
			if (fd < 0)
				break;
			un_addr.sun_family = AF_UNIX;
			snprintf(un_addr.sun_path, sizeof(un_addr.sun_path), "%s", destpath);
			val = bind(fd, (struct sockaddr *) &un_addr, (int) sizeof un_addr);
			close(fd);
			if (val != 0 || chmod(destpath, new_mode & ~S_IFMT) != 0)
				break;
			goto do_chown;
		case S_IFLNK:
			val = readlink(sourcepath, symlink_val, STRING_LENGTH - 1);
			if (val < 0)
				break;
			symlink_val[val] = '\0';
			if (symlink(symlink_val, destpath) == 0)
				return TRUE;
			break;
		case S_IFREG:
			fd = open(destpath, O_RDONLY | O_CREAT, new_mode & ~S_IFMT);
			if (fd < 0)
				break;
			close(fd);
			if (chmod(destpath, new_mode & ~S_IFMT) != 0)
				break;
			goto do_chown;
		case S_IFBLK:
		case S_IFCHR:
		case S_IFIFO:
			if (mknod(destpath, new_mode, source_stat->st_rdev) != 0)
				break;
			goto do_chown;
		case S_IFDIR:
			if (mkdir(destpath, new_mode & ~S_IFMT) != 0)
				break;
do_chown:
			if (chown(destpath, source_stat->st_uid, source_stat->st_gid) == 0)
				return TRUE;
		/*break;*/
	}
	return FALSE;
}   /*  End Function copy_inode  */

static void free_config(void)
/*  [SUMMARY] Free the configuration information.
    [RETURNS] Nothing.
*/
{
	struct config_entry_struct *c_entry;
	void *next;

	for (c_entry = first_config; c_entry != NULL; c_entry = next) {
		unsigned int count;

		next = c_entry->next;
		regfree(&c_entry->preg);
		if (c_entry->action.what == AC_EXECUTE) {
			for (count = 0; count < MAX_ARGS; ++count) {
				if (c_entry->u.execute.argv[count] == NULL)
					break;
				free(c_entry->u.execute.argv[count]);
			}
		}
		free(c_entry);
	}
	first_config = NULL;
	last_config = NULL;
}   /*  End Function free_config  */

static int get_uid_gid(int flag, const char *string)
/*  [SUMMARY] Convert a string to a UID or GID value.
	<flag> "UID" or "GID".
	<string> The string.
    [RETURNS] The UID or GID value.
*/
{
	struct passwd *pw_ent;
	struct group *grp_ent;
	const char *msg;

	if (isdigit(string[0]) || ((string[0] == '-') && isdigit(string[1])))
		return atoi(string);

	if (flag == UID && (pw_ent = getpwnam(string)) != NULL)
		return pw_ent->pw_uid;

	if (ENABLE_DEVFSD_VERBOSE)
		msg = "user";

	if (flag == GID) {
		if ((grp_ent = getgrnam(string)) != NULL)
			return grp_ent->gr_gid;
		if (ENABLE_DEVFSD_VERBOSE)
			msg = "group";
	}

	if (ENABLE_DEVFSD_VERBOSE)
		msg_logger(LOG_ERR, "unknown %s: %s, defaulting to %cid=0",  msg, string, msg[0]);
	return 0;
}/*  End Function get_uid_gid  */

static mode_t get_mode(const char *string)
/*  [SUMMARY] Convert a string to a mode value.
    <string> The string.
    [RETURNS] The mode value.
*/
{
	mode_t mode;
	int i;

	if (isdigit(string[0]))
		return strtoul(string, NULL, 8);
	if (strlen(string) != 9)
		msg_logger_and_die(LOG_ERR, "bad mode: %s", string);

	mode = 0;
	i = S_IRUSR;
	while (i > 0) {
		if (string[0] == 'r' || string[0] == 'w' || string[0] == 'x')
			mode += i;
		i = i / 2;
		string++;
	}
	return mode;
}   /*  End Function get_mode  */

static void signal_handler(int sig)
{
	caught_signal = TRUE;
	if (sig == SIGHUP)
		caught_sighup = TRUE;

	info_logger(LOG_INFO, "Caught signal %d", sig);
}   /*  End Function signal_handler  */

static const char *get_variable(const char *variable, void *info)
{
	static char *hostname;

	struct get_variable_info *gv_info = info;
	const char *field_names[] = {
			"hostname", "mntpt", "devpath", "devname", "uid", "gid", "mode",
			NULL, mount_point, gv_info->devpath, gv_info->devname, NULL
	};
	int i;

	if (!hostname)
		hostname = safe_gethostname();
	field_names[7] = hostname;

	/* index_in_str_array returns i>=0  */
	i = index_in_str_array(field_names, variable);

	if (i > 6 || i < 0 || (i > 1 && gv_info == NULL))
		return NULL;
	if (i >= 0 && i <= 3)
		return field_names[i + 7];

	if (i == 4)
		return auto_string(xasprintf("%u", gv_info->info->uid));
	if (i == 5)
		return auto_string(xasprintf("%u", gv_info->info->gid));
	/* i == 6 */
	return auto_string(xasprintf("%o", gv_info->info->mode));
}   /*  End Function get_variable  */

static void service(struct stat statbuf, char *path)
{
	struct devfsd_notify_struct info;

	memset(&info, 0, sizeof info);
	info.type = DEVFSD_NOTIFY_REGISTERED;
	info.mode = statbuf.st_mode;
	info.major = major(statbuf.st_rdev);
	info.minor = minor(statbuf.st_rdev);
	info.uid = statbuf.st_uid;
	info.gid = statbuf.st_gid;
	snprintf(info.devname, sizeof(info.devname), "%s", path + strlen(mount_point) + 1);
	info.namelen = strlen(info.devname);
	service_name(&info);
	if (S_ISDIR(statbuf.st_mode))
		dir_operation(SERVICE, path, 0, NULL);
}

static void dir_operation(int type, const char * dir_name, int var, unsigned long *event_mask)
/*  [SUMMARY] Scan a directory tree and generate register events on leaf nodes.
	<flag> To choose which function to perform
	<dp> The directory pointer. This is closed upon completion.
    <dir_name> The name of the directory.
	<rootlen> string length parameter.
    [RETURNS] Nothing.
*/
{
	struct stat statbuf;
	DIR *dp;
	struct dirent *de;
	char *path;

	dp = warn_opendir(dir_name);
	if (dp == NULL)
		return;

	while ((de = readdir(dp)) != NULL) {

		if (de->d_name && DOT_OR_DOTDOT(de->d_name))
			continue;
		path = concat_path_file(dir_name, de->d_name);
		if (lstat(path, &statbuf) == 0) {
			switch (type) {
				case SERVICE:
					service(statbuf, path);
					break;
				case RESTORE:
					restore(path, statbuf, var);
					break;
				case READ_CONFIG:
					read_config_file(path, var, event_mask);
					break;
			}
		}
		free(path);
	}
	closedir(dp);
}   /*  End Function do_scan_and_service  */

static int mksymlink(const char *oldpath, const char *newpath)
/*  [SUMMARY] Create a symlink, creating intervening directories as required.
    <oldpath> The string contained in the symlink.
    <newpath> The name of the new symlink.
    [RETURNS] 0 on success, else -1.
*/
{
	if (!make_dir_tree(newpath))
		return -1;

	if (symlink(oldpath, newpath) != 0) {
		if (errno != EEXIST)
			return -1;
	}
	return 0;
}   /*  End Function mksymlink  */


static int make_dir_tree(const char *path)
/*  [SUMMARY] Creating intervening directories for a path as required.
    <path> The full pathname(including the leaf node).
    [RETURNS] TRUE on success, else FALSE.
*/
{
	if (bb_make_directory(dirname((char *)path), -1, FILEUTILS_RECUR) == -1)
		return FALSE;
	return TRUE;
} /*  End Function make_dir_tree  */

static int expand_expression(char *output, unsigned int outsize,
			const char *input,
			const char *(*get_variable_func)(const char *variable, void *info),
			void *info,
			const char *devname,
			const regmatch_t *ex, unsigned int numexp)
/*  [SUMMARY] Expand environment variables and regular subexpressions in string.
    <output> The output expanded expression is written here.
    <length> The size of the output buffer.
    <input> The input expression. This may equal <<output>>.
    <get_variable> A function which will be used to get variable values. If
    this returns NULL, the environment is searched instead. If this is NULL,
    only the environment is searched.
    <info> An arbitrary pointer passed to <<get_variable>>.
    <devname> Device name; specifically, this is the string that contains all
    of the regular subexpressions.
    <ex> Array of start / end offsets into info->devname for each subexpression
    <numexp> Number of regular subexpressions found in <<devname>>.
    [RETURNS] TRUE on success, else FALSE.
*/
{
	char temp[STRING_LENGTH];

	if (!st_expr_expand(temp, STRING_LENGTH, input, get_variable_func, info))
		return FALSE;
	expand_regexp(output, outsize, temp, devname, ex, numexp);
	return TRUE;
}   /*  End Function expand_expression  */

static void expand_regexp(char *output, size_t outsize, const char *input,
			const char *devname,
			const regmatch_t *ex, unsigned int numex)
/*  [SUMMARY] Expand all occurrences of the regular subexpressions \0 to \9.
    <output> The output expanded expression is written here.
    <outsize> The size of the output buffer.
    <input> The input expression. This may NOT equal <<output>>, because
    supporting that would require yet another string-copy. However, it's not
    hard to write a simple wrapper function to add this functionality for those
    few cases that need it.
    <devname> Device name; specifically, this is the string that contains all
    of the regular subexpressions.
    <ex> An array of start and end offsets into <<devname>>, one for each
    subexpression
    <numex> Number of subexpressions in the offset-array <<ex>>.
    [RETURNS] Nothing.
*/
{
	const char last_exp = '0' - 1 + numex;
	int c = -1;

	/*  Guarantee NULL termination by writing an explicit '\0' character into
	the very last byte  */
	if (outsize)
		output[--outsize] = '\0';
	/*  Copy the input string into the output buffer, replacing '\\' with '\'
	and '\0' .. '\9' with subexpressions 0 .. 9, if they exist. Other \x
	codes are deleted  */
	while ((c != '\0') && (outsize != 0)) {
		c = *input;
		++input;
		if (c == '\\') {
			c = *input;
			++input;
			if (c != '\\') {
				if ((c >= '0') && (c <= last_exp)) {
					const regmatch_t *subexp = ex + (c - '0');
					unsigned int sublen = subexp->rm_eo - subexp->rm_so;

					/*  Range checking  */
					if (sublen > outsize)
						sublen = outsize;
					strncpy(output, devname + subexp->rm_so, sublen);
					output += sublen;
					outsize -= sublen;
				}
				continue;
			}
		}
		*output = c;
		++output;
		--outsize;
	} /* while */
}   /*  End Function expand_regexp  */


/* from compat_name.c */

struct translate_struct {
	const char *match;    /*  The string to match to(up to length)                */
	const char *format;   /*  Format of output, "%s" takes data past match string,
			NULL is effectively "%s"(just more efficient)       */
};

static struct translate_struct translate_table[] =
{
	{"sound/",     NULL},
	{"printers/",  "lp%s"},
	{"v4l/",       NULL},
	{"parports/",  "parport%s"},
	{"fb/",        "fb%s"},
	{"netlink/",   NULL},
	{"loop/",      "loop%s"},
	{"floppy/",    "fd%s"},
	{"rd/",        "ram%s"},
	{"md/",        "md%s"},         /*  Meta-devices                         */
	{"vc/",        "tty%s"},
	{"misc/",      NULL},
	{"isdn/",      NULL},
	{"pg/",        "pg%s"},         /*  Parallel port generic ATAPI interface*/
	{"i2c/",       "i2c-%s"},
	{"staliomem/", "staliomem%s"},  /*  Stallion serial driver control       */
	{"tts/E",      "ttyE%s"},       /*  Stallion serial driver               */
	{"cua/E",      "cue%s"},        /*  Stallion serial driver callout       */
	{"tts/R",      "ttyR%s"},       /*  Rocketport serial driver             */
	{"cua/R",      "cur%s"},        /*  Rocketport serial driver callout     */
	{"ip2/",       "ip2%s"},        /*  Computone serial driver control      */
	{"tts/F",      "ttyF%s"},       /*  Computone serial driver              */
	{"cua/F",      "cuf%s"},        /*  Computone serial driver callout      */
	{"tts/C",      "ttyC%s"},       /*  Cyclades serial driver               */
	{"cua/C",      "cub%s"},        /*  Cyclades serial driver callout       */
	{"tts/",       "ttyS%s"},       /*  Generic serial: must be after others */
	{"cua/",       "cua%s"},        /*  Generic serial: must be after others */
	{"input/js",   "js%s"},         /*  Joystick driver                      */
	{NULL,         NULL}
};

const char *get_old_name(const char *devname, unsigned int namelen,
			char *buffer, unsigned int major, unsigned int minor)
/*  [SUMMARY] Translate a kernel-supplied name into an old name.
    <devname> The device name provided by the kernel.
    <namelen> The length of the name.
    <buffer> A buffer that may be used. This should be at least 128 bytes long.
    <major> The major number for the device.
    <minor> The minor number for the device.
    [RETURNS] A pointer to the old name if known, else NULL.
*/
{
	const char *compat_name = NULL;
	const char *ptr;
	struct translate_struct *trans;
	unsigned int i;
	char mode;
	int indexx;
	const char *pty1;
	const char *pty2;
	/* 1 to 5  "scsi/" , 6 to 9 "ide/host", 10 sbp/, 11 vcc/, 12 pty/ */
	static const char *const fmt[] = {
		NULL ,
		"sg%u",			/* scsi/generic */
		NULL,			/* scsi/disc */
		"sr%u",			/* scsi/cd */
		NULL,			/* scsi/part */
		"nst%u%c",		/* scsi/mt */
		"hd%c"	,		/* ide/host/disc */
		"hd%c"	,		/* ide/host/cd */
		"hd%c%s",		/* ide/host/part */
		"%sht%d",		/* ide/host/mt */
		"sbpcd%u",		/* sbp/ */
		"vcs%s",		/* vcc/ */
		"%cty%c%c",		/* pty/ */
		NULL
	};

	for (trans = translate_table; trans->match != NULL; ++trans) {
		char *after_match = is_prefixed_with(devname, trans->match);
		if (after_match) {
			if (trans->format == NULL)
				return after_match;
			sprintf(buffer, trans->format, after_match);
			return buffer;
		}
	}

	ptr = bb_basename(devname);
	i = scan_dev_name(devname, namelen, ptr);

	if (i > 0 && i < 13)
		compat_name = buffer;
	else
		return NULL;

	/* 1 == scsi/generic, 3 == scsi/cd, 10 == sbp/ */
	if (i == 1 || i == 3 || i == 10)
		sprintf(buffer, fmt[i], minor);

	/* 2 ==scsi/disc, 4 == scsi/part */
	if (i == 2 || i == 4)
		compat_name = write_old_sd_name(buffer, major, minor, ((i == 2) ? "" : (ptr + 4)));

	/* 5 == scsi/mt */
	if (i == 5) {
		mode = ptr[2];
		if (mode == 'n')
			mode = '\0';
		sprintf(buffer, fmt[i], minor & 0x1f, mode);
		if (devname[namelen - 1] != 'n')
			++compat_name;
	}
	/* 6 == ide/host/disc, 7 == ide/host/cd, 8 == ide/host/part */
	if (i == 6 || i == 7 || i == 8)
		/* last arg should be ignored for i == 6 or i== 7 */
		sprintf(buffer, fmt[i] , get_old_ide_name(major, minor), ptr + 4);

	/* 9 ==  ide/host/mt */
	if (i == 9)
		sprintf(buffer, fmt[i], ptr + 2, minor & 0x7f);

	/*  11 == vcc/ */
	if (i == 11) {
		sprintf(buffer, fmt[i], devname + 4);
		if (buffer[3] == '0')
			buffer[3] = '\0';
	}
	/* 12 ==  pty/ */
	if (i == 12) {
		pty1 = "pqrstuvwxyzabcde";
		pty2 = "0123456789abcdef";
		indexx = atoi(devname + 5);
		sprintf(buffer, fmt[i], (devname[4] == 'm') ? 'p' : 't', pty1[indexx >> 4], pty2[indexx & 0x0f]);
	}
	return compat_name;
}   /*  End Function get_old_name  */

static char get_old_ide_name(unsigned int major, unsigned int minor)
/*  [SUMMARY] Get the old IDE name for a device.
    <major> The major number for the device.
    <minor> The minor number for the device.
    [RETURNS] The drive letter.
*/
{
	char letter = 'y';	/* 121 */
	char c = 'a';		/*  97 */
	int i = IDE0_MAJOR;

	/* I hope it works like the previous code as it saves a few bytes. Tito ;P */
	do {
		if (i == IDE0_MAJOR || i == IDE1_MAJOR || i == IDE2_MAJOR
		 || i == IDE3_MAJOR || i == IDE4_MAJOR || i == IDE5_MAJOR
		 || i == IDE6_MAJOR || i == IDE7_MAJOR || i == IDE8_MAJOR
		 || i == IDE9_MAJOR
		) {
			if ((unsigned int)i == major) {
				letter = c;
				break;
			}
			c += 2;
		}
		i++;
	} while (i <= IDE9_MAJOR);

	if (minor > 63)
		++letter;
	return letter;
}   /*  End Function get_old_ide_name  */

static char *write_old_sd_name(char *buffer,
				unsigned int major, unsigned int minor,
				const char *part)
/*  [SUMMARY] Write the old SCSI disc name to a buffer.
    <buffer> The buffer to write to.
    <major> The major number for the device.
    <minor> The minor number for the device.
    <part> The partition string. Must be "" for a whole-disc entry.
    [RETURNS] A pointer to the buffer on success, else NULL.
*/
{
	unsigned int disc_index;

	if (major == 8) {
		sprintf(buffer, "sd%c%s", 'a' + (minor >> 4), part);
		return buffer;
	}
	if ((major > 64) && (major < 72)) {
		disc_index = ((major - 64) << 4) +(minor >> 4);
		if (disc_index < 26)
			sprintf(buffer, "sd%c%s", 'a' + disc_index, part);
		else
			sprintf(buffer, "sd%c%c%s", 'a' +(disc_index / 26) - 1, 'a' + disc_index % 26, part);
		return buffer;
	}
	return NULL;
}   /*  End Function write_old_sd_name  */


/*  expression.c */

/*EXPERIMENTAL_FUNCTION*/

int st_expr_expand(char *output, unsigned int length, const char *input,
		const char *(*get_variable_func)(const char *variable,
						void *info),
		void *info)
/*  [SUMMARY] Expand an expression using Borne Shell-like unquoted rules.
    <output> The output expanded expression is written here.
    <length> The size of the output buffer.
    <input> The input expression. This may equal <<output>>.
    <get_variable> A function which will be used to get variable values. If
    this returns NULL, the environment is searched instead. If this is NULL,
    only the environment is searched.
    <info> An arbitrary pointer passed to <<get_variable>>.
    [RETURNS] TRUE on success, else FALSE.
*/
{
	char ch;
	unsigned int len;
	unsigned int out_pos = 0;
	const char *env;
	const char *ptr;
	struct passwd *pwent;
	char buffer[BUFFER_SIZE], tmp[STRING_LENGTH];

	if (length > BUFFER_SIZE)
		length = BUFFER_SIZE;
	for (; TRUE; ++input) {
		switch (ch = *input) {
			case '$':
				/*  Variable expansion  */
				input = expand_variable(buffer, length, &out_pos, ++input, get_variable_func, info);
				if (input == NULL)
					return FALSE;
				break;
			case '~':
				/*  Home directory expansion  */
				ch = input[1];
				if (isspace(ch) ||(ch == '/') ||(ch == '\0')) {
					/* User's own home directory: leave separator for next time */
					env = getenv("HOME");
					if (env == NULL) {
						info_logger(LOG_INFO, bb_msg_variable_not_found, "HOME");
						return FALSE;
					}
					len = strlen(env);
					if (len + out_pos >= length)
						goto st_expr_expand_out;
					memcpy(buffer + out_pos, env, len + 1);
					out_pos += len;
					continue;
				}
				/*  Someone else's home directory  */
				for (ptr = ++input; !isspace(ch) && (ch != '/') && (ch != '\0'); ch = *++ptr)
					/* VOID */;
				len = ptr - input;
				if (len >= sizeof tmp)
					goto st_expr_expand_out;
				safe_memcpy(tmp, input, len);
				input = ptr - 1;
				pwent = getpwnam(tmp);
				if (pwent == NULL) {
					info_logger(LOG_INFO, "no pwent for: %s", tmp);
					return FALSE;
				}
				len = strlen(pwent->pw_dir);
				if (len + out_pos >= length)
					goto st_expr_expand_out;
				memcpy(buffer + out_pos, pwent->pw_dir, len + 1);
				out_pos += len;
				break;
			case '\0':
			/* Falltrough */
			default:
				if (out_pos >= length)
					goto st_expr_expand_out;
				buffer[out_pos++] = ch;
				if (ch == '\0') {
					memcpy(output, buffer, out_pos);
					return TRUE;
				}
				break;
			/* esac */
		}
	}
	return FALSE;
st_expr_expand_out:
	info_logger(LOG_INFO, bb_msg_small_buffer);
	return FALSE;
}   /*  End Function st_expr_expand  */


/*  Private functions follow  */

static const char *expand_variable(char *buffer, unsigned int length,
				unsigned int *out_pos, const char *input,
				const char *(*func)(const char *variable,
							void *info),
				void *info)
/*  [SUMMARY] Expand a variable.
    <buffer> The buffer to write to.
    <length> The length of the output buffer.
    <out_pos> The current output position. This is updated.
    <input> A pointer to the input character pointer.
    <func> A function which will be used to get variable values. If this
    returns NULL, the environment is searched instead. If this is NULL, only
    the environment is searched.
    <info> An arbitrary pointer passed to <<func>>.
    <errfp> Diagnostic messages are written here.
    [RETURNS] A pointer to the end of this subexpression on success, else NULL.
*/
{
	char ch;
	int len;
	unsigned int open_braces;
	const char *env, *ptr;
	char tmp[STRING_LENGTH];

	ch = input[0];
	if (ch == '$') {
		/*  Special case for "$$": PID  */
		sprintf(tmp, "%d", (int) getpid());
		len = strlen(tmp);
		if (len + *out_pos >= length)
			goto expand_variable_out;

		memcpy(buffer + *out_pos, tmp, len + 1);
		out_pos += len;
		return input;
	}
	/*  Ordinary variable expansion, possibly in braces  */
	if (ch != '{') {
		/*  Simple variable expansion  */
		for (ptr = input; isalnum(ch) || (ch == '_') || (ch == ':'); ch = *++ptr)
			/* VOID */;
		len = ptr - input;
		if ((size_t)len >= sizeof tmp)
			goto expand_variable_out;

		safe_memcpy(tmp, input, len);
		input = ptr - 1;
		env = get_variable_v2(tmp, func, info);
		if (env == NULL) {
			info_logger(LOG_INFO, bb_msg_variable_not_found, tmp);
			return NULL;
		}
		len = strlen(env);
		if (len + *out_pos >= length)
			goto expand_variable_out;

		memcpy(buffer + *out_pos, env, len + 1);
		*out_pos += len;
		return input;
	}
	/*  Variable in braces: check for ':' tricks  */
	ch = *++input;
	for (ptr = input; isalnum(ch) || (ch == '_'); ch = *++ptr)
		/* VOID */;
	if (ch == '}') {
		/*  Must be simple variable expansion with "${var}"  */
		len = ptr - input;
		if ((size_t)len >= sizeof tmp)
			goto expand_variable_out;

		safe_memcpy(tmp, input, len);
		ptr = expand_variable(buffer, length, out_pos, tmp, func, info);
		if (ptr == NULL)
			return NULL;
		return input + len;
	}
	if (ch != ':' || ptr[1] != '-') {
		info_logger(LOG_INFO, "illegal char in var name");
		return NULL;
	}
	/*  It's that handy "${var:-word}" expression. Check if var is defined  */
	len = ptr - input;
	if ((size_t)len >= sizeof tmp)
		goto expand_variable_out;

	safe_memcpy(tmp, input, len);
	/*  Move input pointer to ':'  */
	input = ptr;
	/*  First skip to closing brace, taking note of nested expressions  */
	ptr += 2;
	ch = ptr[0];
	for (open_braces = 1; open_braces > 0; ch = *++ptr) {
		switch (ch) {
			case '{':
				++open_braces;
				break;
			case '}':
				--open_braces;
				break;
			case '\0':
				info_logger(LOG_INFO, "\"}\" not found in: %s", input);
				return NULL;
			default:
				break;
		}
	}
	--ptr;
	/*  At this point ptr should point to closing brace of "${var:-word}"  */
	env = get_variable_v2(tmp, func, info);
	if (env != NULL) {
		/*  Found environment variable, so skip the input to the closing brace
			and return the variable  */
		input = ptr;
		len = strlen(env);
		if (len + *out_pos >= length)
			goto expand_variable_out;

		memcpy(buffer + *out_pos, env, len + 1);
		*out_pos += len;
		return input;
	}
	/*  Environment variable was not found, so process word. Advance input
	pointer to start of word in "${var:-word}"  */
	input += 2;
	len = ptr - input;
	if ((size_t)len >= sizeof tmp)
		goto expand_variable_out;

	safe_memcpy(tmp, input, len);
	input = ptr;
	if (!st_expr_expand(tmp, STRING_LENGTH, tmp, func, info))
		return NULL;
	len = strlen(tmp);
	if (len + *out_pos >= length)
		goto expand_variable_out;

	memcpy(buffer + *out_pos, tmp, len + 1);
	*out_pos += len;
	return input;
expand_variable_out:
	info_logger(LOG_INFO, bb_msg_small_buffer);
	return NULL;
}   /*  End Function expand_variable  */


static const char *get_variable_v2(const char *variable,
				const char *(*func)(const char *variable, void *info),
				void *info)
/*  [SUMMARY] Get a variable from the environment or .
    <variable> The variable name.
    <func> A function which will be used to get the variable. If this returns
    NULL, the environment is searched instead. If this is NULL, only the
    environment is searched.
    [RETURNS] The value of the variable on success, else NULL.
*/
{
	const char *value;

	if (func != NULL) {
		value = (*func)(variable, info);
		if (value != NULL)
			return value;
	}
	return getenv(variable);
}   /*  End Function get_variable  */

/* END OF CODE */
