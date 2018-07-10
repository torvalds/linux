/*
  setfiles: based on policycoreutils 2.0.19
  policycoreutils was released under GPL 2.
  Port to BusyBox (c) 2007 by Yuichi Nakamura <ynakam@hitachisoft.jp>
*/
//config:config SETFILES
//config:	bool "setfiles (13 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to modify to relabel files.
//config:	Notice: If you built libselinux with -D_FILE_OFFSET_BITS=64,
//config:	(It is default in libselinux's Makefile), you _must_ enable
//config:	CONFIG_LFS.
//config:
//config:config FEATURE_SETFILES_CHECK_OPTION
//config:	bool "Enable check option"
//config:	default n
//config:	depends on SETFILES
//config:	help
//config:	Support "-c" option (check the validity of the contexts against
//config:	the specified binary policy) for setfiles. Requires libsepol.
//config:
//config:config RESTORECON
//config:	bool "restorecon (12 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to relabel files. The feature is almost
//config:	the same as setfiles, but usage is a little different.

//applet:IF_SETFILES(APPLET(setfiles, BB_DIR_SBIN, BB_SUID_DROP))
//                     APPLET_ODDNAME:name        main      location     suid_type     help
//applet:IF_RESTORECON(APPLET_ODDNAME(restorecon, setfiles, BB_DIR_SBIN, BB_SUID_DROP, restorecon))

//kbuild:lib-$(CONFIG_SETFILES) += setfiles.o
//kbuild:lib-$(CONFIG_RESTORECON) += setfiles.o

//usage:#define setfiles_trivial_usage
//usage:       "[-dnpqsvW] [-e DIR]... [-o FILE] [-r alt_root_path]"
//usage:	IF_FEATURE_SETFILES_CHECK_OPTION(
//usage:       " [-c policyfile] spec_file"
//usage:	)
//usage:       " pathname"
//usage:#define setfiles_full_usage "\n\n"
//usage:       "Reset file contexts under pathname according to spec_file\n"
//usage:	IF_FEATURE_SETFILES_CHECK_OPTION(
//usage:     "\n	-c FILE	Check the validity of the contexts against the specified binary policy"
//usage:	)
//usage:     "\n	-d	Show which specification matched each file"
//usage:     "\n	-l	Log changes in file labels to syslog"
//TODO: log to syslog is not yet implemented, it goes to stdout only now
//usage:     "\n	-n	Don't change any file labels"
//usage:     "\n	-q	Suppress warnings"
//usage:     "\n	-r DIR	Use an alternate root path"
//usage:     "\n	-e DIR	Exclude DIR"
//usage:     "\n	-F	Force reset of context to match file_context for customizable files"
//usage:     "\n	-o FILE	Save list of files with incorrect context"
//usage:     "\n	-s	Take a list of files from stdin (instead of command line)"
//usage:     "\n	-v	Show changes in file labels, if type or role are changing"
//usage:     "\n	-vv	Show changes in file labels, if type, role, or user are changing"
//usage:     "\n	-W	Display warnings about entries that had no matching files"
//usage:
//usage:#define restorecon_trivial_usage
//usage:       "[-iFnRv] [-e EXCLUDEDIR]... [-o FILE] [-f FILE]"
//usage:#define restorecon_full_usage "\n\n"
//usage:       "Reset security contexts of files in pathname\n"
//usage:     "\n	-i	Ignore files that don't exist"
//usage:     "\n	-f FILE	File with list of files to process"
//usage:     "\n	-e DIR	Directory to exclude"
//usage:     "\n	-R,-r	Recurse"
//usage:     "\n	-n	Don't change any file labels"
//usage:     "\n	-o FILE	Save list of files with incorrect context"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-vv	Show changed labels"
//usage:     "\n	-F	Force reset of context to match file_context"
//usage:     "\n		for customizable files, or the user section,"
//usage:     "\n		if it has changed"

#include "libbb.h"
#include "common_bufsiz.h"
#if ENABLE_FEATURE_SETFILES_CHECK_OPTION
#include <sepol/sepol.h>
#endif

#define MAX_EXCLUDES 50

struct edir {
	char *directory;
	size_t size;
};

struct globals {
	FILE *outfile;
	char *policyfile;
	char *rootpath;
	int rootpathlen;
	unsigned count;
	int excludeCtr;
	int errors;
	int verbose; /* getopt32 uses it, has to be int */
	smallint recurse; /* Recursive descent */
	smallint follow_mounts;
	/* Behavior flags determined based on setfiles vs. restorecon */
	smallint expand_realpath;  /* Expand paths via realpath */
	smallint abort_on_error; /* Abort the file tree walk upon an error */
	int add_assoc; /* Track inode associations for conflict detection */
	int matchpathcon_flags; /* Flags to matchpathcon */
	dev_t dev_id; /* Device id where target file exists */
	int nerr;
	struct edir excludeArray[MAX_EXCLUDES];
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
void BUG_setfiles_globals_too_big(void);
#define INIT_G() do { \
	setup_common_bufsiz(); \
	if (sizeof(G) > COMMON_BUFSIZE) \
		BUG_setfiles_globals_too_big(); \
	/* memset(&G, 0, sizeof(G)); - already is */ \
} while (0)
#define outfile            (G.outfile           )
#define policyfile         (G.policyfile        )
#define rootpath           (G.rootpath          )
#define rootpathlen        (G.rootpathlen       )
#define count              (G.count             )
#define excludeCtr         (G.excludeCtr        )
#define errors             (G.errors            )
#define verbose            (G.verbose           )
#define recurse            (G.recurse           )
#define follow_mounts      (G.follow_mounts     )
#define expand_realpath    (G.expand_realpath   )
#define abort_on_error     (G.abort_on_error    )
#define add_assoc          (G.add_assoc         )
#define matchpathcon_flags (G.matchpathcon_flags)
#define dev_id             (G.dev_id            )
#define nerr               (G.nerr              )
#define excludeArray       (G.excludeArray      )

/* Must match getopt32 string! */
enum {
	OPT_d = (1 << 0),
	OPT_e = (1 << 1),
	OPT_f = (1 << 2),
	OPT_i = (1 << 3),
	OPT_l = (1 << 4),
	OPT_n = (1 << 5),
	OPT_p = (1 << 6),
	OPT_q = (1 << 7),
	OPT_r = (1 << 8),
	OPT_s = (1 << 9),
	OPT_v = (1 << 10),
	OPT_o = (1 << 11),
	OPT_F = (1 << 12),
	OPT_W = (1 << 13),
	OPT_c = (1 << 14), /* c only for setfiles */
	OPT_R = (1 << 14), /* R only for restorecon */
};
#define FLAG_d_debug         (option_mask32 & OPT_d)
#define FLAG_e               (option_mask32 & OPT_e)
#define FLAG_f               (option_mask32 & OPT_f)
#define FLAG_i_ignore_enoent (option_mask32 & OPT_i)
#define FLAG_l_take_log      (option_mask32 & OPT_l)
#define FLAG_n_dry_run       (option_mask32 & OPT_n)
#define FLAG_p_progress      (option_mask32 & OPT_p)
#define FLAG_q_quiet         (option_mask32 & OPT_q)
#define FLAG_r               (option_mask32 & OPT_r)
#define FLAG_s               (option_mask32 & OPT_s)
#define FLAG_v               (option_mask32 & OPT_v)
#define FLAG_o               (option_mask32 & OPT_o)
#define FLAG_F_force         (option_mask32 & OPT_F)
#define FLAG_W_warn_no_match (option_mask32 & OPT_W)
#define FLAG_c               (option_mask32 & OPT_c)
#define FLAG_R               (option_mask32 & OPT_R)


static void qprintf(const char *fmt UNUSED_PARAM, ...)
{
	/* quiet, do nothing */
}

static void inc_err(void)
{
	nerr++;
	if (nerr > 9 && !FLAG_d_debug) {
		bb_error_msg_and_die("exiting after 10 errors");
	}
}

static void add_exclude(const char *directory)
{
	struct stat sb;
	size_t len;

	if (directory == NULL || directory[0] != '/') {
		bb_error_msg_and_die("full path required for exclude: %s", directory);
	}
	if (lstat(directory, &sb)) {
		bb_error_msg("directory \"%s\" not found, ignoring", directory);
		return;
	}
	if ((sb.st_mode & S_IFDIR) == 0) {
		bb_error_msg("\"%s\" is not a directory: mode %o, ignoring",
			directory, sb.st_mode);
		return;
	}
	if (excludeCtr == MAX_EXCLUDES) {
		bb_error_msg_and_die("maximum excludes %d exceeded", MAX_EXCLUDES);
	}

	len = strlen(directory);
	while (len > 1 && directory[len - 1] == '/') {
		len--;
	}
	excludeArray[excludeCtr].directory = xstrndup(directory, len);
	excludeArray[excludeCtr++].size = len;
}

static bool exclude(const char *file)
{
	int i = 0;
	for (i = 0; i < excludeCtr; i++) {
		if (strncmp(file, excludeArray[i].directory,
					excludeArray[i].size) == 0) {
			if (file[excludeArray[i].size] == '\0'
			 || file[excludeArray[i].size] == '/') {
				return 1;
			}
		}
	}
	return 0;
}

static int match(const char *name, struct stat *sb, char **con)
{
	int ret;
	char path[PATH_MAX + 1];
	char *tmp_path = xstrdup(name);

	if (excludeCtr > 0 && exclude(name)) {
		goto err;
	}
	ret = lstat(name, sb);
	if (ret) {
		if (FLAG_i_ignore_enoent && errno == ENOENT) {
			free(tmp_path);
			return 0;
		}
		bb_error_msg("stat(%s)", name);
		goto err;
	}

	if (expand_realpath) {
		if (S_ISLNK(sb->st_mode)) {
			char *p = NULL;
			char *file_sep;

			size_t len = 0;

			if (verbose > 1)
				bb_error_msg("warning! %s refers to a symbolic link, not following last component", name);

			file_sep = strrchr(tmp_path, '/');
			if (file_sep == tmp_path) {
				file_sep++;
				path[0] = '\0';
				p = path;
			} else if (file_sep) {
				*file_sep++ = '\0';
				p = realpath(tmp_path, path);
			} else {
				file_sep = tmp_path;
				p = realpath("./", path);
			}
			if (p)
				len = strlen(p);
			if (!p || len + strlen(file_sep) + 2 > PATH_MAX) {
				bb_perror_msg("realpath(%s) failed", name);
				goto err;
			}
			p += len;
			/* ensure trailing slash of directory name */
			if (len == 0 || p[-1] != '/') {
				*p++ = '/';
			}
			strcpy(p, file_sep);
			name = path;
			if (excludeCtr > 0 && exclude(name))
				goto err;
		} else {
			char *p;
			p = realpath(name, path);
			if (!p) {
				bb_perror_msg("realpath(%s)", name);
				goto err;
			}
			name = p;
			if (excludeCtr > 0 && exclude(name))
				goto err;
		}
	}

	/* name will be what is matched in the policy */
	if (NULL != rootpath) {
		if (0 != strncmp(rootpath, name, rootpathlen)) {
			bb_error_msg("%s is not located in %s",
				name, rootpath);
			goto err;
		}
		name += rootpathlen;
	}

	free(tmp_path);
	if (rootpath != NULL && name[0] == '\0')
		/* this is actually the root dir of the alt root */
		return matchpathcon_index("/", sb->st_mode, con);
	return matchpathcon_index(name, sb->st_mode, con);
 err:
	free(tmp_path);
	return -1;
}

/* Compare two contexts to see if their differences are "significant",
 * or whether the only difference is in the user. */
static bool only_changed_user(const char *a, const char *b)
{
	if (FLAG_F_force)
		return 0;
	if (!a || !b)
		return 0;
	a = strchr(a, ':'); /* Rest of the context after the user */
	b = strchr(b, ':');
	if (!a || !b)
		return 0;
	return (strcmp(a, b) == 0);
}

static int restore(const char *file)
{
	char *my_file;
	struct stat my_sb;
	int i, j, ret;
	char *context = NULL;
	char *newcon = NULL;
	bool user_only_changed = 0;
	int retval = 0;

	my_file = bb_simplify_path(file);

	i = match(my_file, &my_sb, &newcon);

	if (i < 0) /* No matching specification. */
		goto out;

	if (FLAG_p_progress) {
		count++;
		if (count % 0x400 == 0) { /* every 1024 times */
			count = (count % (80*0x400));
			if (count == 0)
				bb_putchar('\n');
			bb_putchar('*');
			fflush_all();
		}
	}

	/*
	 * Try to add an association between this inode and
	 * this specification. If there is already an association
	 * for this inode and it conflicts with this specification,
	 * then use the last matching specification.
	 */
	if (add_assoc) {
		j = matchpathcon_filespec_add(my_sb.st_ino, i, my_file);
		if (j < 0)
			goto err;

		if (j != i) {
			/* There was already an association and it took precedence. */
			goto out;
		}
	}

	if (FLAG_d_debug)
		printf("%s: %s matched by %s\n", applet_name, my_file, newcon);

	/* Get the current context of the file. */
	ret = lgetfilecon_raw(my_file, &context);
	if (ret < 0) {
		if (errno == ENODATA) {
			context = NULL; /* paranoia */
		} else {
			bb_perror_msg("lgetfilecon_raw on %s", my_file);
			goto err;
		}
		user_only_changed = 0;
	} else
		user_only_changed = only_changed_user(context, newcon);

	/*
	 * Do not relabel the file if the matching specification is
	 * <<none>> or the file is already labeled according to the
	 * specification.
	 */
	if ((strcmp(newcon, "<<none>>") == 0)
	 || (context && (strcmp(context, newcon) == 0) && !FLAG_F_force)) {
		goto out;
	}

	if (!FLAG_F_force && context && (is_context_customizable(context) > 0)) {
		if (verbose > 1) {
			bb_error_msg("skipping %s. %s is customizable_types",
				my_file, context);
		}
		goto out;
	}

	if (verbose) {
		/* If we're just doing "-v", trim out any relabels where
		 * the user has changed but the role and type are the
		 * same.  For "-vv", emit everything. */
		if (verbose > 1 || !user_only_changed) {
			printf("%s: reset %s context %s->%s\n",
				applet_name, my_file, context ? context : "", newcon);
		}
	}

	if (FLAG_l_take_log && !user_only_changed) {
		if (context)
			printf("relabeling %s from %s to %s\n", my_file, context, newcon);
		else
			printf("labeling %s to %s\n", my_file, newcon);
	}

	if (outfile && !user_only_changed)
		fprintf(outfile, "%s\n", my_file);

	/*
	 * Do not relabel the file if -n was used.
	 */
	if (FLAG_n_dry_run || user_only_changed)
		goto out;

	/*
	 * Relabel the file to the specified context.
	 */
	ret = lsetfilecon(my_file, newcon);
	if (ret) {
		bb_perror_msg("lsetfileconon(%s,%s)", my_file, newcon);
		goto err;
	}

 out:
	freecon(context);
	freecon(newcon);
	free(my_file);
	return retval;
 err:
	retval--; /* -1 */
	goto out;
}

/*
 * Apply the last matching specification to a file.
 * This function is called by recursive_action on each file during
 * the directory traversal.
 */
static int FAST_FUNC apply_spec(
		const char *file,
		struct stat *sb,
		void *userData UNUSED_PARAM,
		int depth UNUSED_PARAM)
{
	if (!follow_mounts) {
		/* setfiles does not process across different mount points */
		if (sb->st_dev != dev_id) {
			return SKIP;
		}
	}
	errors |= restore(file);
	if (abort_on_error && errors)
		return FALSE;
	return TRUE;
}


static int canoncon(const char *path, unsigned lineno, char **contextp)
{
	static const char err_msg[] ALIGN1 = "%s: line %u has invalid context %s";

	char *tmpcon;
	char *context = *contextp;
	int invalid = 0;

#if ENABLE_FEATURE_SETFILES_CHECK_OPTION
	if (policyfile) {
		if (sepol_check_context(context) >= 0)
			return 0;
		/* Exit immediately if we're in checking mode. */
		bb_error_msg_and_die(err_msg, path, lineno, context);
	}
#endif

	if (security_canonicalize_context_raw(context, &tmpcon) < 0) {
		if (errno != ENOENT) {
			invalid = 1;
			inc_err();
		}
	} else {
		free(context);
		*contextp = tmpcon;
	}

	if (invalid) {
		bb_error_msg(err_msg, path, lineno, context);
	}

	return invalid;
}

static int process_one(char *name)
{
	struct stat sb;
	int rc;

	rc = lstat(name, &sb);
	if (rc < 0) {
		if (FLAG_i_ignore_enoent && errno == ENOENT)
			return 0;
		bb_perror_msg("stat(%s)", name);
		goto err;
	}
	dev_id = sb.st_dev;

	if (S_ISDIR(sb.st_mode) && recurse) {
		if (recursive_action(name,
				ACTION_RECURSE,
				apply_spec,
				apply_spec,
				NULL, 0) != TRUE
		) {
			bb_error_msg("error while labeling %s", name);
			goto err;
		}
	} else {
		rc = restore(name);
		if (rc)
			goto err;
	}

 out:
	if (add_assoc) {
		if (FLAG_q_quiet)
			set_matchpathcon_printf(&qprintf);
		matchpathcon_filespec_eval();
		set_matchpathcon_printf(NULL);
		matchpathcon_filespec_destroy();
	}

	return rc;

 err:
	rc = -1;
	goto out;
}

int setfiles_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setfiles_main(int argc UNUSED_PARAM, char **argv)
{
	struct stat sb;
	int rc, i = 0;
	const char *input_filename = NULL;
	char *buf = NULL;
	size_t buf_len;
	int flags;
	llist_t *exclude_dir = NULL;
	char *out_filename = NULL;

	INIT_G();

	if (applet_name[0] == 's') { /* "setfiles" */
		/*
		 * setfiles:
		 * Recursive descent,
		 * Does not expand paths via realpath,
		 * Aborts on errors during the file tree walk,
		 * Try to track inode associations for conflict detection,
		 * Does not follow mounts,
		 * Validates all file contexts at init time.
		 */
		recurse = 1;
		abort_on_error = 1;
		add_assoc = 1;
		/* follow_mounts = 0; - already is */
		matchpathcon_flags = MATCHPATHCON_VALIDATE | MATCHPATHCON_NOTRANS;
	} else {
		/*
		 * restorecon:
		 * No recursive descent unless -r/-R,
		 * Expands paths via realpath,
		 * Do not abort on errors during the file tree walk,
		 * Do not try to track inode associations for conflict detection,
		 * Follows mounts,
		 * Does lazy validation of contexts upon use.
		 */
		expand_realpath = 1;
		follow_mounts = 1;
		matchpathcon_flags = MATCHPATHCON_NOTRANS;
		/* restorecon only */
		selinux_or_die();
	}

	set_matchpathcon_flags(matchpathcon_flags);

	/* Option order must match OPT_x definitions! */
	if (applet_name[0] == 'r') { /* restorecon */
		flags = getopt32(argv, "^"
			"de:*f:ilnpqrsvo:FWR"
				"\0"
				"vv:v--p:p--v:v--q:q--v",
			&exclude_dir, &input_filename, &out_filename,
				&verbose
		);
	} else { /* setfiles */
		flags = getopt32(argv, "^"
			"de:*f:ilnpqr:svo:FW"
			IF_FEATURE_SETFILES_CHECK_OPTION("c:")
				"\0"
				"vv:v--p:p--v:v--q:q--v",
			&exclude_dir, &input_filename, &rootpath, &out_filename,
			IF_FEATURE_SETFILES_CHECK_OPTION(&policyfile,)
				&verbose
		);
	}
	argv += optind;

#if ENABLE_FEATURE_SETFILES_CHECK_OPTION
	if ((applet_name[0] == 's') && (flags & OPT_c)) {
		FILE *policystream;

		policystream = xfopen_for_read(policyfile);
		if (sepol_set_policydb_from_file(policystream) < 0) {
			bb_error_msg_and_die("sepol_set_policydb_from_file on %s", policyfile);
		}
		fclose(policystream);

		/* Only process the specified file_contexts file, not
		 * any .homedirs or .local files, and do not perform
		 * context translations. */
		set_matchpathcon_flags(MATCHPATHCON_BASEONLY |
				       MATCHPATHCON_NOTRANS |
				       MATCHPATHCON_VALIDATE);
	}
#endif

	while (exclude_dir)
		add_exclude(llist_pop(&exclude_dir));

	if (flags & OPT_o) {
		outfile = stdout;
		if (NOT_LONE_CHAR(out_filename, '-')) {
			outfile = xfopen_for_write(out_filename);
		}
	}
	if (applet_name[0] == 'r') { /* restorecon */
		if (flags & (OPT_r | OPT_R))
			recurse = 1;
	} else { /* setfiles */
		if (flags & OPT_r)
			rootpathlen = strlen(rootpath);
	}
	if (flags & OPT_s) {
		input_filename = "-";
		add_assoc = 0;
	}

	if (applet_name[0] == 's') { /* setfiles */
		/* Use our own invalid context checking function so that
		 * we can support either checking against the active policy or
		 * checking against a binary policy file. */
		set_matchpathcon_canoncon(&canoncon);
		if (!argv[0])
			bb_show_usage();
		xstat(argv[0], &sb);
		if (!S_ISREG(sb.st_mode)) {
			bb_error_msg_and_die("'%s' is not a regular file", argv[0]);
		}
		/* Load the file contexts configuration and check it. */
		rc = matchpathcon_init(argv[0]);
		if (rc < 0) {
			bb_simple_perror_msg_and_die(argv[0]);
		}
		if (nerr)
			exit(EXIT_FAILURE);
		argv++;
	}

	if (input_filename) {
		ssize_t len;
		FILE *f = stdin;

		if (NOT_LONE_CHAR(input_filename, '-'))
			f = xfopen_for_read(input_filename);
		while ((len = getline(&buf, &buf_len, f)) > 0) {
			buf[len - 1] = '\0';
			errors |= process_one(buf);
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			fclose_if_not_stdin(f);
	} else {
		if (!argv[0])
			bb_show_usage();
		for (i = 0; argv[i]; i++) {
			errors |= process_one(argv[i]);
		}
	}

	if (FLAG_W_warn_no_match)
		matchpathcon_checkmatches(argv[0]);

	if (ENABLE_FEATURE_CLEAN_UP && outfile)
		fclose(outfile);

	return errors;
}
