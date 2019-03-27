/*	$Id: mandocdb.c,v 1.258 2018/02/23 18:25:57 schwarze Exp $ */
/*
 * Copyright (c) 2011, 2012 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2011-2017 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2016 Ed Maste <emaste@freebsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#if HAVE_ERR
#include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#if HAVE_FTS
#include <fts.h>
#else
#include "compat_fts.h"
#endif
#include <limits.h>
#if HAVE_SANDBOX_INIT
#include <sandbox.h>
#endif
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mandoc_aux.h"
#include "mandoc_ohash.h"
#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "man.h"
#include "manconf.h"
#include "mansearch.h"
#include "dba_array.h"
#include "dba.h"

extern const char *const mansearch_keynames[];

enum	op {
	OP_DEFAULT = 0, /* new dbs from dir list or default config */
	OP_CONFFILE, /* new databases from custom config file */
	OP_UPDATE, /* delete/add entries in existing database */
	OP_DELETE, /* delete entries from existing database */
	OP_TEST /* change no databases, report potential problems */
};

struct	str {
	const struct mpage *mpage; /* if set, the owning parse */
	uint64_t	 mask; /* bitmask in sequence */
	char		 key[]; /* rendered text */
};

struct	inodev {
	ino_t		 st_ino;
	dev_t		 st_dev;
};

struct	mpage {
	struct inodev	 inodev;  /* used for hashing routine */
	struct dba_array *dba;
	char		*sec;     /* section from file content */
	char		*arch;    /* architecture from file content */
	char		*title;   /* title from file content */
	char		*desc;    /* description from file content */
	struct mpage	*next;    /* singly linked list */
	struct mlink	*mlinks;  /* singly linked list */
	int		 name_head_done;
	enum form	 form;    /* format from file content */
};

struct	mlink {
	char		 file[PATH_MAX]; /* filename rel. to manpath */
	char		*dsec;    /* section from directory */
	char		*arch;    /* architecture from directory */
	char		*name;    /* name from file name (not empty) */
	char		*fsec;    /* section from file name suffix */
	struct mlink	*next;    /* singly linked list */
	struct mpage	*mpage;   /* parent */
	int		 gzip;	  /* filename has a .gz suffix */
	enum form	 dform;   /* format from directory */
	enum form	 fform;   /* format from file name suffix */
};

typedef	int (*mdoc_fp)(struct mpage *, const struct roff_meta *,
			const struct roff_node *);

struct	mdoc_handler {
	mdoc_fp		 fp; /* optional handler */
	uint64_t	 mask;  /* set unless handler returns 0 */
	int		 taboo;  /* node flags that must not be set */
};


int		 mandocdb(int, char *[]);

static	void	 dbadd(struct dba *, struct mpage *);
static	void	 dbadd_mlink(const struct mlink *mlink);
static	void	 dbprune(struct dba *);
static	void	 dbwrite(struct dba *);
static	void	 filescan(const char *);
#if HAVE_FTS_COMPARE_CONST
static	int	 fts_compare(const FTSENT *const *, const FTSENT *const *);
#else
static	int	 fts_compare(const FTSENT **, const FTSENT **);
#endif
static	void	 mlink_add(struct mlink *, const struct stat *);
static	void	 mlink_check(struct mpage *, struct mlink *);
static	void	 mlink_free(struct mlink *);
static	void	 mlinks_undupe(struct mpage *);
static	void	 mpages_free(void);
static	void	 mpages_merge(struct dba *, struct mparse *);
static	void	 parse_cat(struct mpage *, int);
static	void	 parse_man(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	void	 parse_mdoc(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_head(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Fa(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Fd(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	void	 parse_mdoc_fname(struct mpage *, const struct roff_node *);
static	int	 parse_mdoc_Fn(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Fo(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Nd(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Nm(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Sh(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Va(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	int	 parse_mdoc_Xr(struct mpage *, const struct roff_meta *,
			const struct roff_node *);
static	void	 putkey(const struct mpage *, char *, uint64_t);
static	void	 putkeys(const struct mpage *, char *, size_t, uint64_t);
static	void	 putmdockey(const struct mpage *,
			const struct roff_node *, uint64_t, int);
static	int	 render_string(char **, size_t *);
static	void	 say(const char *, const char *, ...)
			__attribute__((__format__ (__printf__, 2, 3)));
static	int	 set_basedir(const char *, int);
static	int	 treescan(void);
static	size_t	 utf8(unsigned int, char [7]);

static	int		 nodb; /* no database changes */
static	int		 mparse_options; /* abort the parse early */
static	int		 use_all; /* use all found files */
static	int		 debug; /* print what we're doing */
static	int		 warnings; /* warn about crap */
static	int		 write_utf8; /* write UTF-8 output; else ASCII */
static	int		 exitcode; /* to be returned by main */
static	enum op		 op; /* operational mode */
static	char		 basedir[PATH_MAX]; /* current base directory */
static	struct mpage	*mpage_head; /* list of distinct manual pages */
static	struct ohash	 mpages; /* table of distinct manual pages */
static	struct ohash	 mlinks; /* table of directory entries */
static	struct ohash	 names; /* table of all names */
static	struct ohash	 strings; /* table of all strings */
static	uint64_t	 name_mask;

static	const struct mdoc_handler __mdocs[MDOC_MAX - MDOC_Dd] = {
	{ NULL, 0, NODE_NOPRT },  /* Dd */
	{ NULL, 0, NODE_NOPRT },  /* Dt */
	{ NULL, 0, NODE_NOPRT },  /* Os */
	{ parse_mdoc_Sh, TYPE_Sh, 0 }, /* Sh */
	{ parse_mdoc_head, TYPE_Ss, 0 }, /* Ss */
	{ NULL, 0, 0 },  /* Pp */
	{ NULL, 0, 0 },  /* D1 */
	{ NULL, 0, 0 },  /* Dl */
	{ NULL, 0, 0 },  /* Bd */
	{ NULL, 0, 0 },  /* Ed */
	{ NULL, 0, 0 },  /* Bl */
	{ NULL, 0, 0 },  /* El */
	{ NULL, 0, 0 },  /* It */
	{ NULL, 0, 0 },  /* Ad */
	{ NULL, TYPE_An, 0 },  /* An */
	{ NULL, 0, 0 },  /* Ap */
	{ NULL, TYPE_Ar, 0 },  /* Ar */
	{ NULL, TYPE_Cd, 0 },  /* Cd */
	{ NULL, TYPE_Cm, 0 },  /* Cm */
	{ NULL, TYPE_Dv, 0 },  /* Dv */
	{ NULL, TYPE_Er, 0 },  /* Er */
	{ NULL, TYPE_Ev, 0 },  /* Ev */
	{ NULL, 0, 0 },  /* Ex */
	{ parse_mdoc_Fa, 0, 0 },  /* Fa */
	{ parse_mdoc_Fd, 0, 0 },  /* Fd */
	{ NULL, TYPE_Fl, 0 },  /* Fl */
	{ parse_mdoc_Fn, 0, 0 },  /* Fn */
	{ NULL, TYPE_Ft | TYPE_Vt, 0 },  /* Ft */
	{ NULL, TYPE_Ic, 0 },  /* Ic */
	{ NULL, TYPE_In, 0 },  /* In */
	{ NULL, TYPE_Li, 0 },  /* Li */
	{ parse_mdoc_Nd, 0, 0 },  /* Nd */
	{ parse_mdoc_Nm, 0, 0 },  /* Nm */
	{ NULL, 0, 0 },  /* Op */
	{ NULL, 0, 0 },  /* Ot */
	{ NULL, TYPE_Pa, NODE_NOSRC },  /* Pa */
	{ NULL, 0, 0 },  /* Rv */
	{ NULL, TYPE_St, 0 },  /* St */
	{ parse_mdoc_Va, TYPE_Va, 0 },  /* Va */
	{ parse_mdoc_Va, TYPE_Vt, 0 },  /* Vt */
	{ parse_mdoc_Xr, 0, 0 },  /* Xr */
	{ NULL, 0, 0 },  /* %A */
	{ NULL, 0, 0 },  /* %B */
	{ NULL, 0, 0 },  /* %D */
	{ NULL, 0, 0 },  /* %I */
	{ NULL, 0, 0 },  /* %J */
	{ NULL, 0, 0 },  /* %N */
	{ NULL, 0, 0 },  /* %O */
	{ NULL, 0, 0 },  /* %P */
	{ NULL, 0, 0 },  /* %R */
	{ NULL, 0, 0 },  /* %T */
	{ NULL, 0, 0 },  /* %V */
	{ NULL, 0, 0 },  /* Ac */
	{ NULL, 0, 0 },  /* Ao */
	{ NULL, 0, 0 },  /* Aq */
	{ NULL, TYPE_At, 0 },  /* At */
	{ NULL, 0, 0 },  /* Bc */
	{ NULL, 0, 0 },  /* Bf */
	{ NULL, 0, 0 },  /* Bo */
	{ NULL, 0, 0 },  /* Bq */
	{ NULL, TYPE_Bsx, NODE_NOSRC },  /* Bsx */
	{ NULL, TYPE_Bx, NODE_NOSRC },  /* Bx */
	{ NULL, 0, 0 },  /* Db */
	{ NULL, 0, 0 },  /* Dc */
	{ NULL, 0, 0 },  /* Do */
	{ NULL, 0, 0 },  /* Dq */
	{ NULL, 0, 0 },  /* Ec */
	{ NULL, 0, 0 },  /* Ef */
	{ NULL, TYPE_Em, 0 },  /* Em */
	{ NULL, 0, 0 },  /* Eo */
	{ NULL, TYPE_Fx, NODE_NOSRC },  /* Fx */
	{ NULL, TYPE_Ms, 0 },  /* Ms */
	{ NULL, 0, 0 },  /* No */
	{ NULL, 0, 0 },  /* Ns */
	{ NULL, TYPE_Nx, NODE_NOSRC },  /* Nx */
	{ NULL, TYPE_Ox, NODE_NOSRC },  /* Ox */
	{ NULL, 0, 0 },  /* Pc */
	{ NULL, 0, 0 },  /* Pf */
	{ NULL, 0, 0 },  /* Po */
	{ NULL, 0, 0 },  /* Pq */
	{ NULL, 0, 0 },  /* Qc */
	{ NULL, 0, 0 },  /* Ql */
	{ NULL, 0, 0 },  /* Qo */
	{ NULL, 0, 0 },  /* Qq */
	{ NULL, 0, 0 },  /* Re */
	{ NULL, 0, 0 },  /* Rs */
	{ NULL, 0, 0 },  /* Sc */
	{ NULL, 0, 0 },  /* So */
	{ NULL, 0, 0 },  /* Sq */
	{ NULL, 0, 0 },  /* Sm */
	{ NULL, 0, 0 },  /* Sx */
	{ NULL, TYPE_Sy, 0 },  /* Sy */
	{ NULL, TYPE_Tn, 0 },  /* Tn */
	{ NULL, 0, NODE_NOSRC },  /* Ux */
	{ NULL, 0, 0 },  /* Xc */
	{ NULL, 0, 0 },  /* Xo */
	{ parse_mdoc_Fo, 0, 0 },  /* Fo */
	{ NULL, 0, 0 },  /* Fc */
	{ NULL, 0, 0 },  /* Oo */
	{ NULL, 0, 0 },  /* Oc */
	{ NULL, 0, 0 },  /* Bk */
	{ NULL, 0, 0 },  /* Ek */
	{ NULL, 0, 0 },  /* Bt */
	{ NULL, 0, 0 },  /* Hf */
	{ NULL, 0, 0 },  /* Fr */
	{ NULL, 0, 0 },  /* Ud */
	{ NULL, TYPE_Lb, NODE_NOSRC },  /* Lb */
	{ NULL, 0, 0 },  /* Lp */
	{ NULL, TYPE_Lk, 0 },  /* Lk */
	{ NULL, TYPE_Mt, NODE_NOSRC },  /* Mt */
	{ NULL, 0, 0 },  /* Brq */
	{ NULL, 0, 0 },  /* Bro */
	{ NULL, 0, 0 },  /* Brc */
	{ NULL, 0, 0 },  /* %C */
	{ NULL, 0, 0 },  /* Es */
	{ NULL, 0, 0 },  /* En */
	{ NULL, TYPE_Dx, NODE_NOSRC },  /* Dx */
	{ NULL, 0, 0 },  /* %Q */
	{ NULL, 0, 0 },  /* %U */
	{ NULL, 0, 0 },  /* Ta */
};
static	const struct mdoc_handler *const mdocs = __mdocs - MDOC_Dd;


int
mandocdb(int argc, char *argv[])
{
	struct manconf	  conf;
	struct mparse	 *mp;
	struct dba	 *dba;
	const char	 *path_arg, *progname;
	size_t		  j, sz;
	int		  ch, i;

#if HAVE_PLEDGE
	if (pledge("stdio rpath wpath cpath", NULL) == -1) {
		warn("pledge");
		return (int)MANDOCLEVEL_SYSERR;
	}
#endif

#if HAVE_SANDBOX_INIT
	if (sandbox_init(kSBXProfileNoInternet, SANDBOX_NAMED, NULL) == -1) {
		warnx("sandbox_init");
		return (int)MANDOCLEVEL_SYSERR;
	}
#endif

	memset(&conf, 0, sizeof(conf));

	/*
	 * We accept a few different invocations.
	 * The CHECKOP macro makes sure that invocation styles don't
	 * clobber each other.
	 */
#define	CHECKOP(_op, _ch) do \
	if (OP_DEFAULT != (_op)) { \
		warnx("-%c: Conflicting option", (_ch)); \
		goto usage; \
	} while (/*CONSTCOND*/0)

	path_arg = NULL;
	op = OP_DEFAULT;

	while (-1 != (ch = getopt(argc, argv, "aC:Dd:npQT:tu:v")))
		switch (ch) {
		case 'a':
			use_all = 1;
			break;
		case 'C':
			CHECKOP(op, ch);
			path_arg = optarg;
			op = OP_CONFFILE;
			break;
		case 'D':
			debug++;
			break;
		case 'd':
			CHECKOP(op, ch);
			path_arg = optarg;
			op = OP_UPDATE;
			break;
		case 'n':
			nodb = 1;
			break;
		case 'p':
			warnings = 1;
			break;
		case 'Q':
			mparse_options |= MPARSE_QUICK;
			break;
		case 'T':
			if (strcmp(optarg, "utf8")) {
				warnx("-T%s: Unsupported output format",
				    optarg);
				goto usage;
			}
			write_utf8 = 1;
			break;
		case 't':
			CHECKOP(op, ch);
			dup2(STDOUT_FILENO, STDERR_FILENO);
			op = OP_TEST;
			nodb = warnings = 1;
			break;
		case 'u':
			CHECKOP(op, ch);
			path_arg = optarg;
			op = OP_DELETE;
			break;
		case 'v':
			/* Compatibility with espie@'s makewhatis. */
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

#if HAVE_PLEDGE
	if (nodb) {
		if (pledge("stdio rpath", NULL) == -1) {
			warn("pledge");
			return (int)MANDOCLEVEL_SYSERR;
		}
	}
#endif

	if (OP_CONFFILE == op && argc > 0) {
		warnx("-C: Too many arguments");
		goto usage;
	}

	exitcode = (int)MANDOCLEVEL_OK;
	mchars_alloc();
	mp = mparse_alloc(mparse_options, MANDOCERR_MAX, NULL,
	    MANDOC_OS_OTHER, NULL);
	mandoc_ohash_init(&mpages, 6, offsetof(struct mpage, inodev));
	mandoc_ohash_init(&mlinks, 6, offsetof(struct mlink, file));

	if (OP_UPDATE == op || OP_DELETE == op || OP_TEST == op) {

		/*
		 * Most of these deal with a specific directory.
		 * Jump into that directory first.
		 */
		if (OP_TEST != op && 0 == set_basedir(path_arg, 1))
			goto out;

		dba = nodb ? dba_new(128) : dba_read(MANDOC_DB);
		if (dba != NULL) {
			/*
			 * The existing database is usable.  Process
			 * all files specified on the command-line.
			 */
			use_all = 1;
			for (i = 0; i < argc; i++)
				filescan(argv[i]);
			if (nodb == 0)
				dbprune(dba);
		} else {
			/* Database missing or corrupt. */
			if (op != OP_UPDATE || errno != ENOENT)
				say(MANDOC_DB, "%s: Automatically recreating"
				    " from scratch", strerror(errno));
			exitcode = (int)MANDOCLEVEL_OK;
			op = OP_DEFAULT;
			if (0 == treescan())
				goto out;
			dba = dba_new(128);
		}
		if (OP_DELETE != op)
			mpages_merge(dba, mp);
		if (nodb == 0)
			dbwrite(dba);
		dba_free(dba);
	} else {
		/*
		 * If we have arguments, use them as our manpaths.
		 * If we don't, use man.conf(5).
		 */
		if (argc > 0) {
			conf.manpath.paths = mandoc_reallocarray(NULL,
			    argc, sizeof(char *));
			conf.manpath.sz = (size_t)argc;
			for (i = 0; i < argc; i++)
				conf.manpath.paths[i] = mandoc_strdup(argv[i]);
		} else
			manconf_parse(&conf, path_arg, NULL, NULL);

		if (conf.manpath.sz == 0) {
			exitcode = (int)MANDOCLEVEL_BADARG;
			say("", "Empty manpath");
		}

		/*
		 * First scan the tree rooted at a base directory, then
		 * build a new database and finally move it into place.
		 * Ignore zero-length directories and strip trailing
		 * slashes.
		 */
		for (j = 0; j < conf.manpath.sz; j++) {
			sz = strlen(conf.manpath.paths[j]);
			if (sz && conf.manpath.paths[j][sz - 1] == '/')
				conf.manpath.paths[j][--sz] = '\0';
			if (0 == sz)
				continue;

			if (j) {
				mandoc_ohash_init(&mpages, 6,
				    offsetof(struct mpage, inodev));
				mandoc_ohash_init(&mlinks, 6,
				    offsetof(struct mlink, file));
			}

			if ( ! set_basedir(conf.manpath.paths[j], argc > 0))
				continue;
			if (0 == treescan())
				continue;
			dba = dba_new(128);
			mpages_merge(dba, mp);
			if (nodb == 0)
				dbwrite(dba);
			dba_free(dba);

			if (j + 1 < conf.manpath.sz) {
				mpages_free();
				ohash_delete(&mpages);
				ohash_delete(&mlinks);
			}
		}
	}
out:
	manconf_free(&conf);
	mparse_free(mp);
	mchars_free();
	mpages_free();
	ohash_delete(&mpages);
	ohash_delete(&mlinks);
	return exitcode;
usage:
	progname = getprogname();
	fprintf(stderr, "usage: %s [-aDnpQ] [-C file] [-Tutf8]\n"
			"       %s [-aDnpQ] [-Tutf8] dir ...\n"
			"       %s [-DnpQ] [-Tutf8] -d dir [file ...]\n"
			"       %s [-Dnp] -u dir [file ...]\n"
			"       %s [-Q] -t file ...\n",
		        progname, progname, progname, progname, progname);

	return (int)MANDOCLEVEL_BADARG;
}

/*
 * To get a singly linked list in alpha order while inserting entries
 * at the beginning, process directory entries in reverse alpha order.
 */
static int
#if HAVE_FTS_COMPARE_CONST
fts_compare(const FTSENT *const *a, const FTSENT *const *b)
#else
fts_compare(const FTSENT **a, const FTSENT **b)
#endif
{
	return -strcmp((*a)->fts_name, (*b)->fts_name);
}

/*
 * Scan a directory tree rooted at "basedir" for manpages.
 * We use fts(), scanning directory parts along the way for clues to our
 * section and architecture.
 *
 * If use_all has been specified, grok all files.
 * If not, sanitise paths to the following:
 *
 *   [./]man*[/<arch>]/<name>.<section>
 *   or
 *   [./]cat<section>[/<arch>]/<name>.0
 *
 * TODO: accommodate for multi-language directories.
 */
static int
treescan(void)
{
	char		 buf[PATH_MAX];
	FTS		*f;
	FTSENT		*ff;
	struct mlink	*mlink;
	int		 gzip;
	enum form	 dform;
	char		*dsec, *arch, *fsec, *cp;
	const char	*path;
	const char	*argv[2];

	argv[0] = ".";
	argv[1] = NULL;

	f = fts_open((char * const *)argv, FTS_PHYSICAL | FTS_NOCHDIR,
	    fts_compare);
	if (f == NULL) {
		exitcode = (int)MANDOCLEVEL_SYSERR;
		say("", "&fts_open");
		return 0;
	}

	dsec = arch = NULL;
	dform = FORM_NONE;

	while ((ff = fts_read(f)) != NULL) {
		path = ff->fts_path + 2;
		switch (ff->fts_info) {

		/*
		 * Symbolic links require various sanity checks,
		 * then get handled just like regular files.
		 */
		case FTS_SL:
			if (realpath(path, buf) == NULL) {
				if (warnings)
					say(path, "&realpath");
				continue;
			}
			if (strstr(buf, basedir) != buf
#ifdef HOMEBREWDIR
			    && strstr(buf, HOMEBREWDIR) != buf
#endif
			) {
				if (warnings) say("",
				    "%s: outside base directory", buf);
				continue;
			}
			/* Use logical inode to avoid mpages dupe. */
			if (stat(path, ff->fts_statp) == -1) {
				if (warnings)
					say(path, "&stat");
				continue;
			}
			/* FALLTHROUGH */

		/*
		 * If we're a regular file, add an mlink by using the
		 * stored directory data and handling the filename.
		 */
		case FTS_F:
			if ( ! strcmp(path, MANDOC_DB))
				continue;
			if ( ! use_all && ff->fts_level < 2) {
				if (warnings)
					say(path, "Extraneous file");
				continue;
			}
			gzip = 0;
			fsec = NULL;
			while (fsec == NULL) {
				fsec = strrchr(ff->fts_name, '.');
				if (fsec == NULL || strcmp(fsec+1, "gz"))
					break;
				gzip = 1;
				*fsec = '\0';
				fsec = NULL;
			}
			if (fsec == NULL) {
				if ( ! use_all) {
					if (warnings)
						say(path,
						    "No filename suffix");
					continue;
				}
			} else if ( ! strcmp(++fsec, "html")) {
				if (warnings)
					say(path, "Skip html");
				continue;
			} else if ( ! strcmp(fsec, "ps")) {
				if (warnings)
					say(path, "Skip ps");
				continue;
			} else if ( ! strcmp(fsec, "pdf")) {
				if (warnings)
					say(path, "Skip pdf");
				continue;
			} else if ( ! use_all &&
			    ((dform == FORM_SRC &&
			      strncmp(fsec, dsec, strlen(dsec))) ||
			     (dform == FORM_CAT && strcmp(fsec, "0")))) {
				if (warnings)
					say(path, "Wrong filename suffix");
				continue;
			} else
				fsec[-1] = '\0';

			mlink = mandoc_calloc(1, sizeof(struct mlink));
			if (strlcpy(mlink->file, path,
			    sizeof(mlink->file)) >=
			    sizeof(mlink->file)) {
				say(path, "Filename too long");
				free(mlink);
				continue;
			}
			mlink->dform = dform;
			mlink->dsec = dsec;
			mlink->arch = arch;
			mlink->name = ff->fts_name;
			mlink->fsec = fsec;
			mlink->gzip = gzip;
			mlink_add(mlink, ff->fts_statp);
			continue;

		case FTS_D:
		case FTS_DP:
			break;

		default:
			if (warnings)
				say(path, "Not a regular file");
			continue;
		}

		switch (ff->fts_level) {
		case 0:
			/* Ignore the root directory. */
			break;
		case 1:
			/*
			 * This might contain manX/ or catX/.
			 * Try to infer this from the name.
			 * If we're not in use_all, enforce it.
			 */
			cp = ff->fts_name;
			if (ff->fts_info == FTS_DP) {
				dform = FORM_NONE;
				dsec = NULL;
				break;
			}

			if ( ! strncmp(cp, "man", 3)) {
				dform = FORM_SRC;
				dsec = cp + 3;
			} else if ( ! strncmp(cp, "cat", 3)) {
				dform = FORM_CAT;
				dsec = cp + 3;
			} else {
				dform = FORM_NONE;
				dsec = NULL;
			}

			if (dsec != NULL || use_all)
				break;

			if (warnings)
				say(path, "Unknown directory part");
			fts_set(f, ff, FTS_SKIP);
			break;
		case 2:
			/*
			 * Possibly our architecture.
			 * If we're descending, keep tabs on it.
			 */
			if (ff->fts_info != FTS_DP && dsec != NULL)
				arch = ff->fts_name;
			else
				arch = NULL;
			break;
		default:
			if (ff->fts_info == FTS_DP || use_all)
				break;
			if (warnings)
				say(path, "Extraneous directory part");
			fts_set(f, ff, FTS_SKIP);
			break;
		}
	}

	fts_close(f);
	return 1;
}

/*
 * Add a file to the mlinks table.
 * Do not verify that it's a "valid" looking manpage (we'll do that
 * later).
 *
 * Try to infer the manual section, architecture, and page name from the
 * path, assuming it looks like
 *
 *   [./]man*[/<arch>]/<name>.<section>
 *   or
 *   [./]cat<section>[/<arch>]/<name>.0
 *
 * See treescan() for the fts(3) version of this.
 */
static void
filescan(const char *file)
{
	char		 buf[PATH_MAX];
	struct stat	 st;
	struct mlink	*mlink;
	char		*p, *start;

	assert(use_all);

	if (0 == strncmp(file, "./", 2))
		file += 2;

	/*
	 * We have to do lstat(2) before realpath(3) loses
	 * the information whether this is a symbolic link.
	 * We need to know that because for symbolic links,
	 * we want to use the orginal file name, while for
	 * regular files, we want to use the real path.
	 */
	if (-1 == lstat(file, &st)) {
		exitcode = (int)MANDOCLEVEL_BADARG;
		say(file, "&lstat");
		return;
	} else if (0 == ((S_IFREG | S_IFLNK) & st.st_mode)) {
		exitcode = (int)MANDOCLEVEL_BADARG;
		say(file, "Not a regular file");
		return;
	}

	/*
	 * We have to resolve the file name to the real path
	 * in any case for the base directory check.
	 */
	if (NULL == realpath(file, buf)) {
		exitcode = (int)MANDOCLEVEL_BADARG;
		say(file, "&realpath");
		return;
	}

	if (OP_TEST == op)
		start = buf;
	else if (strstr(buf, basedir) == buf)
		start = buf + strlen(basedir);
#ifdef HOMEBREWDIR
	else if (strstr(buf, HOMEBREWDIR) == buf)
		start = buf;
#endif
	else {
		exitcode = (int)MANDOCLEVEL_BADARG;
		say("", "%s: outside base directory", buf);
		return;
	}

	/*
	 * Now we are sure the file is inside our tree.
	 * If it is a symbolic link, ignore the real path
	 * and use the original name.
	 * This implies passing stuff like "cat1/../man1/foo.1"
	 * on the command line won't work.  So don't do that.
	 * Note the stat(2) can still fail if the link target
	 * doesn't exist.
	 */
	if (S_IFLNK & st.st_mode) {
		if (-1 == stat(buf, &st)) {
			exitcode = (int)MANDOCLEVEL_BADARG;
			say(file, "&stat");
			return;
		}
		if (strlcpy(buf, file, sizeof(buf)) >= sizeof(buf)) {
			say(file, "Filename too long");
			return;
		}
		start = buf;
		if (OP_TEST != op && strstr(buf, basedir) == buf)
			start += strlen(basedir);
	}

	mlink = mandoc_calloc(1, sizeof(struct mlink));
	mlink->dform = FORM_NONE;
	if (strlcpy(mlink->file, start, sizeof(mlink->file)) >=
	    sizeof(mlink->file)) {
		say(start, "Filename too long");
		free(mlink);
		return;
	}

	/*
	 * In test mode or when the original name is absolute
	 * but outside our tree, guess the base directory.
	 */

	if (op == OP_TEST || (start == buf && *start == '/')) {
		if (strncmp(buf, "man/", 4) == 0)
			start = buf + 4;
		else if ((start = strstr(buf, "/man/")) != NULL)
			start += 5;
		else
			start = buf;
	}

	/*
	 * First try to guess our directory structure.
	 * If we find a separator, try to look for man* or cat*.
	 * If we find one of these and what's underneath is a directory,
	 * assume it's an architecture.
	 */
	if (NULL != (p = strchr(start, '/'))) {
		*p++ = '\0';
		if (0 == strncmp(start, "man", 3)) {
			mlink->dform = FORM_SRC;
			mlink->dsec = start + 3;
		} else if (0 == strncmp(start, "cat", 3)) {
			mlink->dform = FORM_CAT;
			mlink->dsec = start + 3;
		}

		start = p;
		if (NULL != mlink->dsec && NULL != (p = strchr(start, '/'))) {
			*p++ = '\0';
			mlink->arch = start;
			start = p;
		}
	}

	/*
	 * Now check the file suffix.
	 * Suffix of `.0' indicates a catpage, `.1-9' is a manpage.
	 */
	p = strrchr(start, '\0');
	while (p-- > start && '/' != *p && '.' != *p)
		/* Loop. */ ;

	if ('.' == *p) {
		*p++ = '\0';
		mlink->fsec = p;
	}

	/*
	 * Now try to parse the name.
	 * Use the filename portion of the path.
	 */
	mlink->name = start;
	if (NULL != (p = strrchr(start, '/'))) {
		mlink->name = p + 1;
		*p = '\0';
	}
	mlink_add(mlink, &st);
}

static void
mlink_add(struct mlink *mlink, const struct stat *st)
{
	struct inodev	 inodev;
	struct mpage	*mpage;
	unsigned int	 slot;

	assert(NULL != mlink->file);

	mlink->dsec = mandoc_strdup(mlink->dsec ? mlink->dsec : "");
	mlink->arch = mandoc_strdup(mlink->arch ? mlink->arch : "");
	mlink->name = mandoc_strdup(mlink->name ? mlink->name : "");
	mlink->fsec = mandoc_strdup(mlink->fsec ? mlink->fsec : "");

	if ('0' == *mlink->fsec) {
		free(mlink->fsec);
		mlink->fsec = mandoc_strdup(mlink->dsec);
		mlink->fform = FORM_CAT;
	} else if ('1' <= *mlink->fsec && '9' >= *mlink->fsec)
		mlink->fform = FORM_SRC;
	else
		mlink->fform = FORM_NONE;

	slot = ohash_qlookup(&mlinks, mlink->file);
	assert(NULL == ohash_find(&mlinks, slot));
	ohash_insert(&mlinks, slot, mlink);

	memset(&inodev, 0, sizeof(inodev));  /* Clear padding. */
	inodev.st_ino = st->st_ino;
	inodev.st_dev = st->st_dev;
	slot = ohash_lookup_memory(&mpages, (char *)&inodev,
	    sizeof(struct inodev), inodev.st_ino);
	mpage = ohash_find(&mpages, slot);
	if (NULL == mpage) {
		mpage = mandoc_calloc(1, sizeof(struct mpage));
		mpage->inodev.st_ino = inodev.st_ino;
		mpage->inodev.st_dev = inodev.st_dev;
		mpage->form = FORM_NONE;
		mpage->next = mpage_head;
		mpage_head = mpage;
		ohash_insert(&mpages, slot, mpage);
	} else
		mlink->next = mpage->mlinks;
	mpage->mlinks = mlink;
	mlink->mpage = mpage;
}

static void
mlink_free(struct mlink *mlink)
{

	free(mlink->dsec);
	free(mlink->arch);
	free(mlink->name);
	free(mlink->fsec);
	free(mlink);
}

static void
mpages_free(void)
{
	struct mpage	*mpage;
	struct mlink	*mlink;

	while ((mpage = mpage_head) != NULL) {
		while ((mlink = mpage->mlinks) != NULL) {
			mpage->mlinks = mlink->next;
			mlink_free(mlink);
		}
		mpage_head = mpage->next;
		free(mpage->sec);
		free(mpage->arch);
		free(mpage->title);
		free(mpage->desc);
		free(mpage);
	}
}

/*
 * For each mlink to the mpage, check whether the path looks like
 * it is formatted, and if it does, check whether a source manual
 * exists by the same name, ignoring the suffix.
 * If both conditions hold, drop the mlink.
 */
static void
mlinks_undupe(struct mpage *mpage)
{
	char		  buf[PATH_MAX];
	struct mlink	**prev;
	struct mlink	 *mlink;
	char		 *bufp;

	mpage->form = FORM_CAT;
	prev = &mpage->mlinks;
	while (NULL != (mlink = *prev)) {
		if (FORM_CAT != mlink->dform) {
			mpage->form = FORM_NONE;
			goto nextlink;
		}
		(void)strlcpy(buf, mlink->file, sizeof(buf));
		bufp = strstr(buf, "cat");
		assert(NULL != bufp);
		memcpy(bufp, "man", 3);
		if (NULL != (bufp = strrchr(buf, '.')))
			*++bufp = '\0';
		(void)strlcat(buf, mlink->dsec, sizeof(buf));
		if (NULL == ohash_find(&mlinks,
		    ohash_qlookup(&mlinks, buf)))
			goto nextlink;
		if (warnings)
			say(mlink->file, "Man source exists: %s", buf);
		if (use_all)
			goto nextlink;
		*prev = mlink->next;
		mlink_free(mlink);
		continue;
nextlink:
		prev = &(*prev)->next;
	}
}

static void
mlink_check(struct mpage *mpage, struct mlink *mlink)
{
	struct str	*str;
	unsigned int	 slot;

	/*
	 * Check whether the manual section given in a file
	 * agrees with the directory where the file is located.
	 * Some manuals have suffixes like (3p) on their
	 * section number either inside the file or in the
	 * directory name, some are linked into more than one
	 * section, like encrypt(1) = makekey(8).
	 */

	if (FORM_SRC == mpage->form &&
	    strcasecmp(mpage->sec, mlink->dsec))
		say(mlink->file, "Section \"%s\" manual in %s directory",
		    mpage->sec, mlink->dsec);

	/*
	 * Manual page directories exist for each kernel
	 * architecture as returned by machine(1).
	 * However, many manuals only depend on the
	 * application architecture as returned by arch(1).
	 * For example, some (2/ARM) manuals are shared
	 * across the "armish" and "zaurus" kernel
	 * architectures.
	 * A few manuals are even shared across completely
	 * different architectures, for example fdformat(1)
	 * on amd64, i386, and sparc64.
	 */

	if (strcasecmp(mpage->arch, mlink->arch))
		say(mlink->file, "Architecture \"%s\" manual in "
		    "\"%s\" directory", mpage->arch, mlink->arch);

	/*
	 * XXX
	 * parse_cat() doesn't set NAME_TITLE yet.
	 */

	if (FORM_CAT == mpage->form)
		return;

	/*
	 * Check whether this mlink
	 * appears as a name in the NAME section.
	 */

	slot = ohash_qlookup(&names, mlink->name);
	str = ohash_find(&names, slot);
	assert(NULL != str);
	if ( ! (NAME_TITLE & str->mask))
		say(mlink->file, "Name missing in NAME section");
}

/*
 * Run through the files in the global vector "mpages"
 * and add them to the database specified in "basedir".
 *
 * This handles the parsing scheme itself, using the cues of directory
 * and filename to determine whether the file is parsable or not.
 */
static void
mpages_merge(struct dba *dba, struct mparse *mp)
{
	struct mpage		*mpage, *mpage_dest;
	struct mlink		*mlink, *mlink_dest;
	struct roff_man		*man;
	char			*sodest;
	char			*cp;
	int			 fd;

	for (mpage = mpage_head; mpage != NULL; mpage = mpage->next) {
		mlinks_undupe(mpage);
		if ((mlink = mpage->mlinks) == NULL)
			continue;

		name_mask = NAME_MASK;
		mandoc_ohash_init(&names, 4, offsetof(struct str, key));
		mandoc_ohash_init(&strings, 6, offsetof(struct str, key));
		mparse_reset(mp);
		man = NULL;
		sodest = NULL;

		if ((fd = mparse_open(mp, mlink->file)) == -1) {
			say(mlink->file, "&open");
			goto nextpage;
		}

		/*
		 * Interpret the file as mdoc(7) or man(7) source
		 * code, unless it is known to be formatted.
		 */
		if (mlink->dform != FORM_CAT || mlink->fform != FORM_CAT) {
			mparse_readfd(mp, fd, mlink->file);
			close(fd);
			fd = -1;
			mparse_result(mp, &man, &sodest);
		}

		if (sodest != NULL) {
			mlink_dest = ohash_find(&mlinks,
			    ohash_qlookup(&mlinks, sodest));
			if (mlink_dest == NULL) {
				mandoc_asprintf(&cp, "%s.gz", sodest);
				mlink_dest = ohash_find(&mlinks,
				    ohash_qlookup(&mlinks, cp));
				free(cp);
			}
			if (mlink_dest != NULL) {

				/* The .so target exists. */

				mpage_dest = mlink_dest->mpage;
				while (1) {
					mlink->mpage = mpage_dest;

					/*
					 * If the target was already
					 * processed, add the links
					 * to the database now.
					 * Otherwise, this will
					 * happen when we come
					 * to the target.
					 */

					if (mpage_dest->dba != NULL)
						dbadd_mlink(mlink);

					if (mlink->next == NULL)
						break;
					mlink = mlink->next;
				}

				/* Move all links to the target. */

				mlink->next = mlink_dest->next;
				mlink_dest->next = mpage->mlinks;
				mpage->mlinks = NULL;
			}
			goto nextpage;
		} else if (man != NULL && man->macroset == MACROSET_MDOC) {
			mdoc_validate(man);
			mpage->form = FORM_SRC;
			mpage->sec = man->meta.msec;
			mpage->sec = mandoc_strdup(
			    mpage->sec == NULL ? "" : mpage->sec);
			mpage->arch = man->meta.arch;
			mpage->arch = mandoc_strdup(
			    mpage->arch == NULL ? "" : mpage->arch);
			mpage->title = mandoc_strdup(man->meta.title);
		} else if (man != NULL && man->macroset == MACROSET_MAN) {
			man_validate(man);
			if (*man->meta.msec != '\0' ||
			    *man->meta.title != '\0') {
				mpage->form = FORM_SRC;
				mpage->sec = mandoc_strdup(man->meta.msec);
				mpage->arch = mandoc_strdup(mlink->arch);
				mpage->title = mandoc_strdup(man->meta.title);
			} else
				man = NULL;
		}

		assert(mpage->desc == NULL);
		if (man == NULL) {
			mpage->form = FORM_CAT;
			mpage->sec = mandoc_strdup(mlink->dsec);
			mpage->arch = mandoc_strdup(mlink->arch);
			mpage->title = mandoc_strdup(mlink->name);
			parse_cat(mpage, fd);
		} else if (man->macroset == MACROSET_MDOC)
			parse_mdoc(mpage, &man->meta, man->first);
		else
			parse_man(mpage, &man->meta, man->first);
		if (mpage->desc == NULL) {
			mpage->desc = mandoc_strdup(mlink->name);
			if (warnings)
				say(mlink->file, "No one-line description, "
				    "using filename \"%s\"", mlink->name);
		}

		for (mlink = mpage->mlinks;
		     mlink != NULL;
		     mlink = mlink->next) {
			putkey(mpage, mlink->name, NAME_FILE);
			if (warnings && !use_all)
				mlink_check(mpage, mlink);
		}

		dbadd(dba, mpage);

nextpage:
		ohash_delete(&strings);
		ohash_delete(&names);
	}
}

static void
parse_cat(struct mpage *mpage, int fd)
{
	FILE		*stream;
	struct mlink	*mlink;
	char		*line, *p, *title, *sec;
	size_t		 linesz, plen, titlesz;
	ssize_t		 len;
	int		 offs;

	mlink = mpage->mlinks;
	stream = fd == -1 ? fopen(mlink->file, "r") : fdopen(fd, "r");
	if (stream == NULL) {
		if (fd != -1)
			close(fd);
		if (warnings)
			say(mlink->file, "&fopen");
		return;
	}

	line = NULL;
	linesz = 0;

	/* Parse the section number from the header line. */

	while (getline(&line, &linesz, stream) != -1) {
		if (*line == '\n')
			continue;
		if ((sec = strchr(line, '(')) == NULL)
			break;
		if ((p = strchr(++sec, ')')) == NULL)
			break;
		free(mpage->sec);
		mpage->sec = mandoc_strndup(sec, p - sec);
		if (warnings && *mlink->dsec != '\0' &&
		    strcasecmp(mpage->sec, mlink->dsec))
			say(mlink->file,
			    "Section \"%s\" manual in %s directory",
			    mpage->sec, mlink->dsec);
		break;
	}

	/* Skip to first blank line. */

	while (line == NULL || *line != '\n')
		if (getline(&line, &linesz, stream) == -1)
			break;

	/*
	 * Assume the first line that is not indented
	 * is the first section header.  Skip to it.
	 */

	while (getline(&line, &linesz, stream) != -1)
		if (*line != '\n' && *line != ' ')
			break;

	/*
	 * Read up until the next section into a buffer.
	 * Strip the leading and trailing newline from each read line,
	 * appending a trailing space.
	 * Ignore empty (whitespace-only) lines.
	 */

	titlesz = 0;
	title = NULL;

	while ((len = getline(&line, &linesz, stream)) != -1) {
		if (*line != ' ')
			break;
		offs = 0;
		while (isspace((unsigned char)line[offs]))
			offs++;
		if (line[offs] == '\0')
			continue;
		title = mandoc_realloc(title, titlesz + len - offs);
		memcpy(title + titlesz, line + offs, len - offs);
		titlesz += len - offs;
		title[titlesz - 1] = ' ';
	}
	free(line);

	/*
	 * If no page content can be found, or the input line
	 * is already the next section header, or there is no
	 * trailing newline, reuse the page title as the page
	 * description.
	 */

	if (NULL == title || '\0' == *title) {
		if (warnings)
			say(mlink->file, "Cannot find NAME section");
		fclose(stream);
		free(title);
		return;
	}

	title[titlesz - 1] = '\0';

	/*
	 * Skip to the first dash.
	 * Use the remaining line as the description (no more than 70
	 * bytes).
	 */

	if (NULL != (p = strstr(title, "- "))) {
		for (p += 2; ' ' == *p || '\b' == *p; p++)
			/* Skip to next word. */ ;
	} else {
		if (warnings)
			say(mlink->file, "No dash in title line, "
			    "reusing \"%s\" as one-line description", title);
		p = title;
	}

	plen = strlen(p);

	/* Strip backspace-encoding from line. */

	while (NULL != (line = memchr(p, '\b', plen))) {
		len = line - p;
		if (0 == len) {
			memmove(line, line + 1, plen--);
			continue;
		}
		memmove(line - 1, line + 1, plen - len);
		plen -= 2;
	}

	/*
	 * Cut off excessive one-line descriptions.
	 * Bad pages are not worth better heuristics.
	 */

	mpage->desc = mandoc_strndup(p, 150);
	fclose(stream);
	free(title);
}

/*
 * Put a type/word pair into the word database for this particular file.
 */
static void
putkey(const struct mpage *mpage, char *value, uint64_t type)
{
	putkeys(mpage, value, strlen(value), type);
}

/*
 * Grok all nodes at or below a certain mdoc node into putkey().
 */
static void
putmdockey(const struct mpage *mpage,
	const struct roff_node *n, uint64_t m, int taboo)
{

	for ( ; NULL != n; n = n->next) {
		if (n->flags & taboo)
			continue;
		if (NULL != n->child)
			putmdockey(mpage, n->child, m, taboo);
		if (n->type == ROFFT_TEXT)
			putkey(mpage, n->string, m);
	}
}

static void
parse_man(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{
	const struct roff_node *head, *body;
	char		*start, *title;
	char		 byte;
	size_t		 sz;

	if (n == NULL)
		return;

	/*
	 * We're only searching for one thing: the first text child in
	 * the BODY of a NAME section.  Since we don't keep track of
	 * sections in -man, run some hoops to find out whether we're in
	 * the correct section or not.
	 */

	if (n->type == ROFFT_BODY && n->tok == MAN_SH) {
		body = n;
		if ((head = body->parent->head) != NULL &&
		    (head = head->child) != NULL &&
		    head->next == NULL &&
		    head->type == ROFFT_TEXT &&
		    strcmp(head->string, "NAME") == 0 &&
		    body->child != NULL) {

			/*
			 * Suck the entire NAME section into memory.
			 * Yes, we might run away.
			 * But too many manuals have big, spread-out
			 * NAME sections over many lines.
			 */

			title = NULL;
			deroff(&title, body);
			if (NULL == title)
				return;

			/*
			 * Go through a special heuristic dance here.
			 * Conventionally, one or more manual names are
			 * comma-specified prior to a whitespace, then a
			 * dash, then a description.  Try to puzzle out
			 * the name parts here.
			 */

			start = title;
			for ( ;; ) {
				sz = strcspn(start, " ,");
				if ('\0' == start[sz])
					break;

				byte = start[sz];
				start[sz] = '\0';

				/*
				 * Assume a stray trailing comma in the
				 * name list if a name begins with a dash.
				 */

				if ('-' == start[0] ||
				    ('\\' == start[0] && '-' == start[1]))
					break;

				putkey(mpage, start, NAME_TITLE);
				if ( ! (mpage->name_head_done ||
				    strcasecmp(start, meta->title))) {
					putkey(mpage, start, NAME_HEAD);
					mpage->name_head_done = 1;
				}

				if (' ' == byte) {
					start += sz + 1;
					break;
				}

				assert(',' == byte);
				start += sz + 1;
				while (' ' == *start)
					start++;
			}

			if (start == title) {
				putkey(mpage, start, NAME_TITLE);
				if ( ! (mpage->name_head_done ||
				    strcasecmp(start, meta->title))) {
					putkey(mpage, start, NAME_HEAD);
					mpage->name_head_done = 1;
				}
				free(title);
				return;
			}

			while (isspace((unsigned char)*start))
				start++;

			if (0 == strncmp(start, "-", 1))
				start += 1;
			else if (0 == strncmp(start, "\\-\\-", 4))
				start += 4;
			else if (0 == strncmp(start, "\\-", 2))
				start += 2;
			else if (0 == strncmp(start, "\\(en", 4))
				start += 4;
			else if (0 == strncmp(start, "\\(em", 4))
				start += 4;

			while (' ' == *start)
				start++;

			/*
			 * Cut off excessive one-line descriptions.
			 * Bad pages are not worth better heuristics.
			 */

			mpage->desc = mandoc_strndup(start, 150);
			free(title);
			return;
		}
	}

	for (n = n->child; n; n = n->next) {
		if (NULL != mpage->desc)
			break;
		parse_man(mpage, meta, n);
	}
}

static void
parse_mdoc(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{

	for (n = n->child; n != NULL; n = n->next) {
		if (n->tok == TOKEN_NONE ||
		    n->tok < ROFF_MAX ||
		    n->flags & mdocs[n->tok].taboo)
			continue;
		assert(n->tok >= MDOC_Dd && n->tok < MDOC_MAX);
		switch (n->type) {
		case ROFFT_ELEM:
		case ROFFT_BLOCK:
		case ROFFT_HEAD:
		case ROFFT_BODY:
		case ROFFT_TAIL:
			if (mdocs[n->tok].fp != NULL &&
			    (*mdocs[n->tok].fp)(mpage, meta, n) == 0)
				break;
			if (mdocs[n->tok].mask)
				putmdockey(mpage, n->child,
				    mdocs[n->tok].mask, mdocs[n->tok].taboo);
			break;
		default:
			continue;
		}
		if (NULL != n->child)
			parse_mdoc(mpage, meta, n);
	}
}

static int
parse_mdoc_Fa(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{
	uint64_t mask;

	mask = TYPE_Fa;
	if (n->sec == SEC_SYNOPSIS)
		mask |= TYPE_Vt;

	putmdockey(mpage, n->child, mask, 0);
	return 0;
}

static int
parse_mdoc_Fd(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{
	char		*start, *end;
	size_t		 sz;

	if (SEC_SYNOPSIS != n->sec ||
	    NULL == (n = n->child) ||
	    n->type != ROFFT_TEXT)
		return 0;

	/*
	 * Only consider those `Fd' macro fields that begin with an
	 * "inclusion" token (versus, e.g., #define).
	 */

	if (strcmp("#include", n->string))
		return 0;

	if ((n = n->next) == NULL || n->type != ROFFT_TEXT)
		return 0;

	/*
	 * Strip away the enclosing angle brackets and make sure we're
	 * not zero-length.
	 */

	start = n->string;
	if ('<' == *start || '"' == *start)
		start++;

	if (0 == (sz = strlen(start)))
		return 0;

	end = &start[(int)sz - 1];
	if ('>' == *end || '"' == *end)
		end--;

	if (end > start)
		putkeys(mpage, start, end - start + 1, TYPE_In);
	return 0;
}

static void
parse_mdoc_fname(struct mpage *mpage, const struct roff_node *n)
{
	char	*cp;
	size_t	 sz;

	if (n->type != ROFFT_TEXT)
		return;

	/* Skip function pointer punctuation. */

	cp = n->string;
	while (*cp == '(' || *cp == '*')
		cp++;
	sz = strcspn(cp, "()");

	putkeys(mpage, cp, sz, TYPE_Fn);
	if (n->sec == SEC_SYNOPSIS)
		putkeys(mpage, cp, sz, NAME_SYN);
}

static int
parse_mdoc_Fn(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{
	uint64_t mask;

	if (n->child == NULL)
		return 0;

	parse_mdoc_fname(mpage, n->child);

	n = n->child->next;
	if (n != NULL && n->type == ROFFT_TEXT) {
		mask = TYPE_Fa;
		if (n->sec == SEC_SYNOPSIS)
			mask |= TYPE_Vt;
		putmdockey(mpage, n, mask, 0);
	}

	return 0;
}

static int
parse_mdoc_Fo(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{

	if (n->type != ROFFT_HEAD)
		return 1;

	if (n->child != NULL)
		parse_mdoc_fname(mpage, n->child);

	return 0;
}

static int
parse_mdoc_Va(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{
	char *cp;

	if (n->type != ROFFT_ELEM && n->type != ROFFT_BODY)
		return 0;

	if (n->child != NULL &&
	    n->child->next == NULL &&
	    n->child->type == ROFFT_TEXT)
		return 1;

	cp = NULL;
	deroff(&cp, n);
	if (cp != NULL) {
		putkey(mpage, cp, TYPE_Vt | (n->tok == MDOC_Va ||
		    n->type == ROFFT_BODY ? TYPE_Va : 0));
		free(cp);
	}

	return 0;
}

static int
parse_mdoc_Xr(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{
	char	*cp;

	if (NULL == (n = n->child))
		return 0;

	if (NULL == n->next) {
		putkey(mpage, n->string, TYPE_Xr);
		return 0;
	}

	mandoc_asprintf(&cp, "%s(%s)", n->string, n->next->string);
	putkey(mpage, cp, TYPE_Xr);
	free(cp);
	return 0;
}

static int
parse_mdoc_Nd(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{

	if (n->type == ROFFT_BODY)
		deroff(&mpage->desc, n);
	return 0;
}

static int
parse_mdoc_Nm(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{

	if (SEC_NAME == n->sec)
		putmdockey(mpage, n->child, NAME_TITLE, 0);
	else if (n->sec == SEC_SYNOPSIS && n->type == ROFFT_HEAD) {
		if (n->child == NULL)
			putkey(mpage, meta->name, NAME_SYN);
		else
			putmdockey(mpage, n->child, NAME_SYN, 0);
	}
	if ( ! (mpage->name_head_done ||
	    n->child == NULL || n->child->string == NULL ||
	    strcasecmp(n->child->string, meta->title))) {
		putkey(mpage, n->child->string, NAME_HEAD);
		mpage->name_head_done = 1;
	}
	return 0;
}

static int
parse_mdoc_Sh(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{

	return n->sec == SEC_CUSTOM && n->type == ROFFT_HEAD;
}

static int
parse_mdoc_head(struct mpage *mpage, const struct roff_meta *meta,
	const struct roff_node *n)
{

	return n->type == ROFFT_HEAD;
}

/*
 * Add a string to the hash table for the current manual.
 * Each string has a bitmask telling which macros it belongs to.
 * When we finish the manual, we'll dump the table.
 */
static void
putkeys(const struct mpage *mpage, char *cp, size_t sz, uint64_t v)
{
	struct ohash	*htab;
	struct str	*s;
	const char	*end;
	unsigned int	 slot;
	int		 i, mustfree;

	if (0 == sz)
		return;

	mustfree = render_string(&cp, &sz);

	if (TYPE_Nm & v) {
		htab = &names;
		v &= name_mask;
		if (v & NAME_FIRST)
			name_mask &= ~NAME_FIRST;
		if (debug > 1)
			say(mpage->mlinks->file,
			    "Adding name %*s, bits=0x%llx", (int)sz, cp,
			    (unsigned long long)v);
	} else {
		htab = &strings;
		if (debug > 1)
		    for (i = 0; i < KEY_MAX; i++)
			if ((uint64_t)1 << i & v)
			    say(mpage->mlinks->file,
				"Adding key %s=%*s",
				mansearch_keynames[i], (int)sz, cp);
	}

	end = cp + sz;
	slot = ohash_qlookupi(htab, cp, &end);
	s = ohash_find(htab, slot);

	if (NULL != s && mpage == s->mpage) {
		s->mask |= v;
		return;
	} else if (NULL == s) {
		s = mandoc_calloc(1, sizeof(struct str) + sz + 1);
		memcpy(s->key, cp, sz);
		ohash_insert(htab, slot, s);
	}
	s->mpage = mpage;
	s->mask = v;

	if (mustfree)
		free(cp);
}

/*
 * Take a Unicode codepoint and produce its UTF-8 encoding.
 * This isn't the best way to do this, but it works.
 * The magic numbers are from the UTF-8 packaging.
 * They're not as scary as they seem: read the UTF-8 spec for details.
 */
static size_t
utf8(unsigned int cp, char out[7])
{
	size_t		 rc;

	rc = 0;
	if (cp <= 0x0000007F) {
		rc = 1;
		out[0] = (char)cp;
	} else if (cp <= 0x000007FF) {
		rc = 2;
		out[0] = (cp >> 6  & 31) | 192;
		out[1] = (cp       & 63) | 128;
	} else if (cp <= 0x0000FFFF) {
		rc = 3;
		out[0] = (cp >> 12 & 15) | 224;
		out[1] = (cp >> 6  & 63) | 128;
		out[2] = (cp       & 63) | 128;
	} else if (cp <= 0x001FFFFF) {
		rc = 4;
		out[0] = (cp >> 18 &  7) | 240;
		out[1] = (cp >> 12 & 63) | 128;
		out[2] = (cp >> 6  & 63) | 128;
		out[3] = (cp       & 63) | 128;
	} else if (cp <= 0x03FFFFFF) {
		rc = 5;
		out[0] = (cp >> 24 &  3) | 248;
		out[1] = (cp >> 18 & 63) | 128;
		out[2] = (cp >> 12 & 63) | 128;
		out[3] = (cp >> 6  & 63) | 128;
		out[4] = (cp       & 63) | 128;
	} else if (cp <= 0x7FFFFFFF) {
		rc = 6;
		out[0] = (cp >> 30 &  1) | 252;
		out[1] = (cp >> 24 & 63) | 128;
		out[2] = (cp >> 18 & 63) | 128;
		out[3] = (cp >> 12 & 63) | 128;
		out[4] = (cp >> 6  & 63) | 128;
		out[5] = (cp       & 63) | 128;
	} else
		return 0;

	out[rc] = '\0';
	return rc;
}

/*
 * If the string contains escape sequences,
 * replace it with an allocated rendering and return 1,
 * such that the caller can free it after use.
 * Otherwise, do nothing and return 0.
 */
static int
render_string(char **public, size_t *psz)
{
	const char	*src, *scp, *addcp, *seq;
	char		*dst;
	size_t		 ssz, dsz, addsz;
	char		 utfbuf[7], res[6];
	int		 seqlen, unicode;

	res[0] = '\\';
	res[1] = '\t';
	res[2] = ASCII_NBRSP;
	res[3] = ASCII_HYPH;
	res[4] = ASCII_BREAK;
	res[5] = '\0';

	src = scp = *public;
	ssz = *psz;
	dst = NULL;
	dsz = 0;

	while (scp < src + *psz) {

		/* Leave normal characters unchanged. */

		if (strchr(res, *scp) == NULL) {
			if (dst != NULL)
				dst[dsz++] = *scp;
			scp++;
			continue;
		}

		/*
		 * Found something that requires replacing,
		 * make sure we have a destination buffer.
		 */

		if (dst == NULL) {
			dst = mandoc_malloc(ssz + 1);
			dsz = scp - src;
			memcpy(dst, src, dsz);
		}

		/* Handle single-char special characters. */

		switch (*scp) {
		case '\\':
			break;
		case '\t':
		case ASCII_NBRSP:
			dst[dsz++] = ' ';
			scp++;
			continue;
		case ASCII_HYPH:
			dst[dsz++] = '-';
			/* FALLTHROUGH */
		case ASCII_BREAK:
			scp++;
			continue;
		default:
			abort();
		}

		/*
		 * Found an escape sequence.
		 * Read past the slash, then parse it.
		 * Ignore everything except characters.
		 */

		scp++;
		if (mandoc_escape(&scp, &seq, &seqlen) != ESCAPE_SPECIAL)
			continue;

		/*
		 * Render the special character
		 * as either UTF-8 or ASCII.
		 */

		if (write_utf8) {
			unicode = mchars_spec2cp(seq, seqlen);
			if (unicode <= 0)
				continue;
			addsz = utf8(unicode, utfbuf);
			if (addsz == 0)
				continue;
			addcp = utfbuf;
		} else {
			addcp = mchars_spec2str(seq, seqlen, &addsz);
			if (addcp == NULL)
				continue;
			if (*addcp == ASCII_NBRSP) {
				addcp = " ";
				addsz = 1;
			}
		}

		/* Copy the rendered glyph into the stream. */

		ssz += addsz;
		dst = mandoc_realloc(dst, ssz + 1);
		memcpy(dst + dsz, addcp, addsz);
		dsz += addsz;
	}
	if (dst != NULL) {
		*public = dst;
		*psz = dsz;
	}

	/* Trim trailing whitespace and NUL-terminate. */

	while (*psz > 0 && (*public)[*psz - 1] == ' ')
		--*psz;
	if (dst != NULL) {
		(*public)[*psz] = '\0';
		return 1;
	} else
		return 0;
}

static void
dbadd_mlink(const struct mlink *mlink)
{
	dba_page_alias(mlink->mpage->dba, mlink->name, NAME_FILE);
	dba_page_add(mlink->mpage->dba, DBP_SECT, mlink->dsec);
	dba_page_add(mlink->mpage->dba, DBP_SECT, mlink->fsec);
	dba_page_add(mlink->mpage->dba, DBP_ARCH, mlink->arch);
	dba_page_add(mlink->mpage->dba, DBP_FILE, mlink->file);
}

/*
 * Flush the current page's terms (and their bits) into the database.
 * Also, handle escape sequences at the last possible moment.
 */
static void
dbadd(struct dba *dba, struct mpage *mpage)
{
	struct mlink	*mlink;
	struct str	*key;
	char		*cp;
	uint64_t	 mask;
	size_t		 i;
	unsigned int	 slot;
	int		 mustfree;

	mlink = mpage->mlinks;

	if (nodb) {
		for (key = ohash_first(&names, &slot); NULL != key;
		     key = ohash_next(&names, &slot))
			free(key);
		for (key = ohash_first(&strings, &slot); NULL != key;
		     key = ohash_next(&strings, &slot))
			free(key);
		if (0 == debug)
			return;
		while (NULL != mlink) {
			fputs(mlink->name, stdout);
			if (NULL == mlink->next ||
			    strcmp(mlink->dsec, mlink->next->dsec) ||
			    strcmp(mlink->fsec, mlink->next->fsec) ||
			    strcmp(mlink->arch, mlink->next->arch)) {
				putchar('(');
				if ('\0' == *mlink->dsec)
					fputs(mlink->fsec, stdout);
				else
					fputs(mlink->dsec, stdout);
				if ('\0' != *mlink->arch)
					printf("/%s", mlink->arch);
				putchar(')');
			}
			mlink = mlink->next;
			if (NULL != mlink)
				fputs(", ", stdout);
		}
		printf(" - %s\n", mpage->desc);
		return;
	}

	if (debug)
		say(mlink->file, "Adding to database");

	cp = mpage->desc;
	i = strlen(cp);
	mustfree = render_string(&cp, &i);
	mpage->dba = dba_page_new(dba->pages,
	    *mpage->arch == '\0' ? mlink->arch : mpage->arch,
	    cp, mlink->file, mpage->form);
	if (mustfree)
		free(cp);
	dba_page_add(mpage->dba, DBP_SECT, mpage->sec);

	while (mlink != NULL) {
		dbadd_mlink(mlink);
		mlink = mlink->next;
	}

	for (key = ohash_first(&names, &slot); NULL != key;
	     key = ohash_next(&names, &slot)) {
		assert(key->mpage == mpage);
		dba_page_alias(mpage->dba, key->key, key->mask);
		free(key);
	}
	for (key = ohash_first(&strings, &slot); NULL != key;
	     key = ohash_next(&strings, &slot)) {
		assert(key->mpage == mpage);
		i = 0;
		for (mask = TYPE_Xr; mask <= TYPE_Lb; mask *= 2) {
			if (key->mask & mask)
				dba_macro_add(dba->macros, i,
				    key->key, mpage->dba);
			i++;
		}
		free(key);
	}
}

static void
dbprune(struct dba *dba)
{
	struct dba_array	*page, *files;
	char			*file;

	dba_array_FOREACH(dba->pages, page) {
		files = dba_array_get(page, DBP_FILE);
		dba_array_FOREACH(files, file) {
			if (*file < ' ')
				file++;
			if (ohash_find(&mlinks, ohash_qlookup(&mlinks,
			    file)) != NULL) {
				if (debug)
					say(file, "Deleting from database");
				dba_array_del(dba->pages);
				break;
			}
		}
	}
}

/*
 * Write the database from memory to disk.
 */
static void
dbwrite(struct dba *dba)
{
	struct stat	 sb1, sb2;
	char		 tfn[33], *cp1, *cp2;
	off_t		 i;
	int		 fd1, fd2;

	/*
	 * Do not write empty databases, and delete existing ones
	 * when makewhatis -u causes them to become empty.
	 */

	dba_array_start(dba->pages);
	if (dba_array_next(dba->pages) == NULL) {
		if (unlink(MANDOC_DB) == -1 && errno != ENOENT)
			say(MANDOC_DB, "&unlink");
		return;
	}

	/*
	 * Build the database in a temporary file,
	 * then atomically move it into place.
	 */

	if (dba_write(MANDOC_DB "~", dba) != -1) {
		if (rename(MANDOC_DB "~", MANDOC_DB) == -1) {
			exitcode = (int)MANDOCLEVEL_SYSERR;
			say(MANDOC_DB, "&rename");
			unlink(MANDOC_DB "~");
		}
		return;
	}

	/*
	 * We lack write permission and cannot replace the database
	 * file, but let's at least check whether the data changed.
	 */

	(void)strlcpy(tfn, "/tmp/mandocdb.XXXXXXXX", sizeof(tfn));
	if (mkdtemp(tfn) == NULL) {
		exitcode = (int)MANDOCLEVEL_SYSERR;
		say("", "&%s", tfn);
		return;
	}
	cp1 = cp2 = MAP_FAILED;
	fd1 = fd2 = -1;
	(void)strlcat(tfn, "/" MANDOC_DB, sizeof(tfn));
	if (dba_write(tfn, dba) == -1) {
		say(tfn, "&dba_write");
		goto err;
	}
	if ((fd1 = open(MANDOC_DB, O_RDONLY, 0)) == -1) {
		say(MANDOC_DB, "&open");
		goto err;
	}
	if ((fd2 = open(tfn, O_RDONLY, 0)) == -1) {
		say(tfn, "&open");
		goto err;
	}
	if (fstat(fd1, &sb1) == -1) {
		say(MANDOC_DB, "&fstat");
		goto err;
	}
	if (fstat(fd2, &sb2) == -1) {
		say(tfn, "&fstat");
		goto err;
	}
	if (sb1.st_size != sb2.st_size)
		goto err;
	if ((cp1 = mmap(NULL, sb1.st_size, PROT_READ, MAP_PRIVATE,
	    fd1, 0)) == MAP_FAILED) {
		say(MANDOC_DB, "&mmap");
		goto err;
	}
	if ((cp2 = mmap(NULL, sb2.st_size, PROT_READ, MAP_PRIVATE,
	    fd2, 0)) == MAP_FAILED) {
		say(tfn, "&mmap");
		goto err;
	}
	for (i = 0; i < sb1.st_size; i++)
		if (cp1[i] != cp2[i])
			goto err;
	goto out;

err:
	exitcode = (int)MANDOCLEVEL_SYSERR;
	say(MANDOC_DB, "Data changed, but cannot replace database");

out:
	if (cp1 != MAP_FAILED)
		munmap(cp1, sb1.st_size);
	if (cp2 != MAP_FAILED)
		munmap(cp2, sb2.st_size);
	if (fd1 != -1)
		close(fd1);
	if (fd2 != -1)
		close(fd2);
	unlink(tfn);
	*strrchr(tfn, '/') = '\0';
	rmdir(tfn);
}

static int
set_basedir(const char *targetdir, int report_baddir)
{
	static char	 startdir[PATH_MAX];
	static int	 getcwd_status;  /* 1 = ok, 2 = failure */
	static int	 chdir_status;  /* 1 = changed directory */
	char		*cp;

	/*
	 * Remember the original working directory, if possible.
	 * This will be needed if the second or a later directory
	 * on the command line is given as a relative path.
	 * Do not error out if the current directory is not
	 * searchable: Maybe it won't be needed after all.
	 */
	if (0 == getcwd_status) {
		if (NULL == getcwd(startdir, sizeof(startdir))) {
			getcwd_status = 2;
			(void)strlcpy(startdir, strerror(errno),
			    sizeof(startdir));
		} else
			getcwd_status = 1;
	}

	/*
	 * We are leaving the old base directory.
	 * Do not use it any longer, not even for messages.
	 */
	*basedir = '\0';

	/*
	 * If and only if the directory was changed earlier and
	 * the next directory to process is given as a relative path,
	 * first go back, or bail out if that is impossible.
	 */
	if (chdir_status && '/' != *targetdir) {
		if (2 == getcwd_status) {
			exitcode = (int)MANDOCLEVEL_SYSERR;
			say("", "getcwd: %s", startdir);
			return 0;
		}
		if (-1 == chdir(startdir)) {
			exitcode = (int)MANDOCLEVEL_SYSERR;
			say("", "&chdir %s", startdir);
			return 0;
		}
	}

	/*
	 * Always resolve basedir to the canonicalized absolute
	 * pathname and append a trailing slash, such that
	 * we can reliably check whether files are inside.
	 */
	if (NULL == realpath(targetdir, basedir)) {
		if (report_baddir || errno != ENOENT) {
			exitcode = (int)MANDOCLEVEL_BADARG;
			say("", "&%s: realpath", targetdir);
		}
		return 0;
	} else if (-1 == chdir(basedir)) {
		if (report_baddir || errno != ENOENT) {
			exitcode = (int)MANDOCLEVEL_BADARG;
			say("", "&chdir");
		}
		return 0;
	}
	chdir_status = 1;
	cp = strchr(basedir, '\0');
	if ('/' != cp[-1]) {
		if (cp - basedir >= PATH_MAX - 1) {
			exitcode = (int)MANDOCLEVEL_SYSERR;
			say("", "Filename too long");
			return 0;
		}
		*cp++ = '/';
		*cp = '\0';
	}
	return 1;
}

static void
say(const char *file, const char *format, ...)
{
	va_list		 ap;
	int		 use_errno;

	if ('\0' != *basedir)
		fprintf(stderr, "%s", basedir);
	if ('\0' != *basedir && '\0' != *file)
		fputc('/', stderr);
	if ('\0' != *file)
		fprintf(stderr, "%s", file);

	use_errno = 1;
	if (NULL != format) {
		switch (*format) {
		case '&':
			format++;
			break;
		case '\0':
			format = NULL;
			break;
		default:
			use_errno = 0;
			break;
		}
	}
	if (NULL != format) {
		if ('\0' != *basedir || '\0' != *file)
			fputs(": ", stderr);
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		va_end(ap);
	}
	if (use_errno) {
		if ('\0' != *basedir || '\0' != *file || NULL != format)
			fputs(": ", stderr);
		perror(NULL);
	} else
		fputc('\n', stderr);
}
