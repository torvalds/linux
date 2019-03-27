/*-
 * Copyright (c) 1994, 1996
 *	Rob Mayoff.  All rights reserved.
 * Copyright (c) 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_cscope.c,v 10.25 2012/10/04 09:23:03 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../common/common.h"
#include "pathnames.h"
#include "tag.h"

#define	CSCOPE_DBFILE		"cscope.out"
#define	CSCOPE_PATHS		"cscope.tpath"

/*
 * 0name	find all uses of name
 * 1name	find definition of name
 * 2name	find all function calls made from name
 * 3name	find callers of name
 * 4string	find text string (cscope 12.9)
 * 4name	find assignments to name (cscope 13.3)
 * 5pattern	change pattern -- NOT USED
 * 6pattern	find pattern
 * 7name	find files with name as substring
 * 8name	find files #including name
 */
#define	FINDHELP "\
find c|d|e|f|g|i|s|t buffer|pattern\n\
      c: find callers of name\n\
      d: find all function calls made from name\n\
      e: find pattern\n\
      f: find files with name as substring\n\
      g: find definition of name\n\
      i: find files #including name\n\
      s: find all uses of name\n\
      t: find assignments to name"

static int cscope_add(SCR *, EXCMD *, CHAR_T *);
static int cscope_find(SCR *, EXCMD*, CHAR_T *);
static int cscope_help(SCR *, EXCMD *, CHAR_T *);
static int cscope_kill(SCR *, EXCMD *, CHAR_T *);
static int cscope_reset(SCR *, EXCMD *, CHAR_T *);

typedef struct _cc {
	char	 *name;
	int	(*function)(SCR *, EXCMD *, CHAR_T *);
	char	 *help_msg;
	char	 *usage_msg;
} CC;

static CC const cscope_cmds[] = {
	{ "add",   cscope_add,
	  "Add a new cscope database", "add file | directory" },
	{ "find",  cscope_find,
	  "Query the databases for a pattern", FINDHELP },
	{ "help",  cscope_help,
	  "Show help for cscope commands", "help [command]" },
	{ "kill",  cscope_kill,
	  "Kill a cscope connection", "kill number" },
	{ "reset", cscope_reset,
	  "Discard all current cscope connections", "reset" },
	{ NULL }
};

static TAGQ	*create_cs_cmd(SCR *, char *, size_t *);
static int	 csc_help(SCR *, char *);
static void	 csc_file(SCR *,
		    CSC *, char *, char **, size_t *, int *);
static int	 get_paths(SCR *, CSC *);
static CC const	*lookup_ccmd(char *);
static int	 parse(SCR *, CSC *, TAGQ *, int *);
static int	 read_prompt(SCR *, CSC *);
static int	 run_cscope(SCR *, CSC *, char *);
static int	 start_cscopes(SCR *, EXCMD *);
static int	 terminate(SCR *, CSC *, int);

/*
 * ex_cscope --
 *	Perform an ex cscope.
 *
 * PUBLIC: int ex_cscope(SCR *, EXCMD *);
 */
int
ex_cscope(SCR *sp, EXCMD *cmdp)
{
	CC const *ccp;
	EX_PRIVATE *exp;
	int i;
	CHAR_T *cmd;
	CHAR_T *p;
	char *np;
	size_t nlen;

	/* Initialize the default cscope directories. */
	exp = EXP(sp);
	if (!F_ISSET(exp, EXP_CSCINIT) && start_cscopes(sp, cmdp))
		return (1);
	F_SET(exp, EXP_CSCINIT);

	/* Skip leading whitespace. */
	for (p = cmdp->argv[0]->bp, i = cmdp->argv[0]->len; i > 0; --i, ++p)
		if (!isspace(*p))
			break;
	if (i == 0)
		goto usage;

	/* Skip the command to any arguments. */
	for (cmd = p; i > 0; --i, ++p)
		if (isspace(*p))
			break;
	if (*p != '\0') {
		*p++ = '\0';
		for (; *p && isspace(*p); ++p);
	}

	INT2CHAR(sp, cmd, STRLEN(cmd) + 1, np, nlen);
	if ((ccp = lookup_ccmd(np)) == NULL) {
usage:		msgq(sp, M_ERR, "309|Use \"cscope help\" for help");
		return (1);
	}

	/* Call the underlying function. */
	return (ccp->function(sp, cmdp, p));
}

/*
 * start_cscopes --
 *	Initialize the cscope package.
 */
static int
start_cscopes(SCR *sp, EXCMD *cmdp)
{
	size_t blen, len;
	char *bp, *cscopes, *p, *t;
	CHAR_T *wp;
	size_t wlen;

	/*
	 * EXTENSION #1:
	 *
	 * If the CSCOPE_DIRS environment variable is set, we treat it as a
	 * list of cscope directories that we're using, similar to the tags
	 * edit option.
	 *
	 * XXX
	 * This should probably be an edit option, although that implies that
	 * we start/stop cscope processes periodically, instead of once when
	 * the editor starts.
	 */
	if ((cscopes = getenv("CSCOPE_DIRS")) == NULL)
		return (0);
	len = strlen(cscopes);
	GET_SPACE_RETC(sp, bp, blen, len);
	memcpy(bp, cscopes, len + 1);

	for (cscopes = t = bp; (p = strsep(&t, "\t :")) != NULL;)
		if (*p != '\0') {
			CHAR2INT(sp, p, strlen(p) + 1, wp, wlen);
			(void)cscope_add(sp, cmdp, wp);
		}

	FREE_SPACE(sp, bp, blen);
	return (0);
}

/*
 * cscope_add --
 *	The cscope add command.
 */
static int
cscope_add(SCR *sp, EXCMD *cmdp, CHAR_T *dname)
{
	struct stat sb;
	EX_PRIVATE *exp;
	CSC *csc;
	size_t len;
	int cur_argc;
	char *dbname, *path;
	char *np = NULL;
	size_t nlen;

	exp = EXP(sp);

	/*
	 *  0 additional args: usage.
	 *  1 additional args: matched a file.
	 * >1 additional args: object, too many args.
	 */
	cur_argc = cmdp->argc;
	if (argv_exp2(sp, cmdp, dname, STRLEN(dname))) {
		return (1);
	}
	if (cmdp->argc == cur_argc) {
		(void)csc_help(sp, "add");
		return (1);
	}
	if (cmdp->argc == cur_argc + 1)
		dname = cmdp->argv[cur_argc]->bp;
	else {
		ex_emsg(sp, np, EXM_FILECOUNT);
		return (1);
	}

	INT2CHAR(sp, dname, STRLEN(dname)+1, np, nlen);

	/*
	 * The user can specify a specific file (so they can have multiple
	 * Cscope databases in a single directory) or a directory.  If the
	 * file doesn't exist, we're done.  If it's a directory, append the
	 * standard database file name and try again.  Store the directory
	 * name regardless so that we can use it as a base for searches.
	 */
	if (stat(np, &sb)) {
		msgq(sp, M_SYSERR, "%s", np);
		return (1);
	}
	if (S_ISDIR(sb.st_mode)) {
		if ((path = join(np, CSCOPE_DBFILE)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			return (1);
		}
		if (stat(path, &sb)) {
			msgq(sp, M_SYSERR, "%s", path);
			free(path);
			return (1);
		}
		free(path);
		dbname = CSCOPE_DBFILE;
	} else if ((dbname = strrchr(np, '/')) != NULL)
		*dbname++ = '\0';
	else {
		dbname = np;
		np = ".";
	}

	/* Allocate a cscope connection structure and initialize its fields. */
	len = strlen(np);
	CALLOC_RET(sp, csc, CSC *, 1, sizeof(CSC) + len);
	csc->dname = csc->buf;
	csc->dlen = len;
	memcpy(csc->dname, np, len);
	csc->mtim = sb.st_mtimespec;

	/* Get the search paths for the cscope. */
	if (get_paths(sp, csc))
		goto err;

	/* Start the cscope process. */
	if (run_cscope(sp, csc, dbname))
		goto err;

	/*
	 * Add the cscope connection to the screen's list.  From now on, 
	 * on error, we have to call terminate, which expects the csc to
	 * be on the chain.
	 */
	SLIST_INSERT_HEAD(exp->cscq, csc, q);

	/* Read the initial prompt from the cscope to make sure it's okay. */
	return read_prompt(sp, csc);

err:	free(csc);
	return (1);
}

/*
 * get_paths --
 *	Get the directories to search for the files associated with this
 *	cscope database.
 */
static int
get_paths(SCR *sp, CSC *csc)
{
	struct stat sb;
	int fd, nentries;
	size_t len;
	char *p, **pathp, *buf;

	/*
	 * EXTENSION #2:
	 *
	 * If there's a cscope directory with a file named CSCOPE_PATHS, it
	 * contains a colon-separated list of paths in which to search for
	 * files returned by cscope.
	 *
	 * XXX
	 * These paths are absolute paths, and not relative to the cscope
	 * directory.  To fix this, rewrite the each path using the cscope
	 * directory as a prefix.
	 */
	if ((buf = join(csc->dname, CSCOPE_PATHS)) == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}
	if (stat(buf, &sb) == 0) {
		/* Read in the CSCOPE_PATHS file. */
		len = sb.st_size;
		MALLOC_RET(sp, csc->pbuf, char *, len + 1);
		if ((fd = open(buf, O_RDONLY, 0)) < 0 ||
		    read(fd, csc->pbuf, len) != len) {
			 msgq_str(sp, M_SYSERR, buf, "%s");
			 if (fd >= 0)
				(void)close(fd);
			 free(buf);
			 return (1);
		}
		(void)close(fd);
		free(buf);
		csc->pbuf[len] = '\0';

		/* Count up the entries. */
		for (nentries = 0, p = csc->pbuf; *p != '\0'; ++p)
			if (p[0] == ':' && p[1] != '\0')
				++nentries;

		/* Build an array of pointers to the paths. */
		CALLOC_GOTO(sp,
		    csc->paths, char **, nentries + 1, sizeof(char **));
		for (pathp = csc->paths, p = strtok(csc->pbuf, ":");
		    p != NULL; p = strtok(NULL, ":"))
			*pathp++ = p;
		return (0);
	}
	free(buf);

	/*
	 * If the CSCOPE_PATHS file doesn't exist, we look for files
	 * relative to the cscope directory.
	 */
	if ((csc->pbuf = strdup(csc->dname)) == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}
	CALLOC_GOTO(sp, csc->paths, char **, 2, sizeof(char *));
	csc->paths[0] = csc->pbuf;
	return (0);

alloc_err:
	if (csc->pbuf != NULL) {
		free(csc->pbuf);
		csc->pbuf = NULL;
	}
	return (1);
}

/*
 * run_cscope --
 *	Fork off the cscope process.
 */
static int
run_cscope(SCR *sp, CSC *csc, char *dbname)
{
	int to_cs[2], from_cs[2];
	char *cmd;

	/*
	 * Cscope reads from to_cs[0] and writes to from_cs[1]; vi reads from
	 * from_cs[0] and writes to to_cs[1].
	 */
	to_cs[0] = to_cs[1] = from_cs[0] = from_cs[1] = -1;
	if (pipe(to_cs) < 0 || pipe(from_cs) < 0) {
		msgq(sp, M_SYSERR, "pipe");
		goto err;
	}
	switch (csc->pid = vfork()) {
		char *dn, *dbn;
	case -1:
		msgq(sp, M_SYSERR, "vfork");
err:		if (to_cs[0] != -1)
			(void)close(to_cs[0]);
		if (to_cs[1] != -1)
			(void)close(to_cs[1]);
		if (from_cs[0] != -1)
			(void)close(from_cs[0]);
		if (from_cs[1] != -1)
			(void)close(from_cs[1]);
		return (1);
	case 0:				/* child: run cscope. */
		(void)dup2(to_cs[0], STDIN_FILENO);
		(void)dup2(from_cs[1], STDOUT_FILENO);
		(void)dup2(from_cs[1], STDERR_FILENO);

		/* Close unused file descriptors. */
		(void)close(to_cs[1]);
		(void)close(from_cs[0]);

		/* Run the cscope command. */
#define	CSCOPE_CMD_FMT		"cd %s && exec cscope -dl -f %s"
		if ((dn = quote(csc->dname)) == NULL)
			goto nomem;
		if ((dbn = quote(dbname)) == NULL) {
			free(dn);
			goto nomem;
		}
		(void)asprintf(&cmd, CSCOPE_CMD_FMT, dn, dbn);
		free(dbn);
		free(dn);
		if (cmd == NULL) {
nomem:			msgq(sp, M_SYSERR, NULL);
			_exit (1);
		}
		(void)execl(_PATH_BSHELL, "sh", "-c", cmd, (char *)NULL);
		msgq_str(sp, M_SYSERR, cmd, "execl: %s");
		free(cmd);
		_exit (127);
		/* NOTREACHED */
	default:			/* parent. */
		/* Close unused file descriptors. */
		(void)close(to_cs[0]);
		(void)close(from_cs[1]);

		/*
		 * Save the file descriptors for later duplication, and
		 * reopen as streams.
		 */
		csc->to_fd = to_cs[1];
		csc->to_fp = fdopen(to_cs[1], "w");
		csc->from_fd = from_cs[0];
		csc->from_fp = fdopen(from_cs[0], "r");
		break;
	}
	return (0);
}

/*
 * cscope_find --
 *	The cscope find command.
 */
static int
cscope_find(SCR *sp, EXCMD *cmdp, CHAR_T *pattern)
{
	CSC *csc, *csc_next;
	EX_PRIVATE *exp;
	FREF *frp;
	TAGQ *rtqp, *tqp;
	TAG *rtp;
	recno_t lno;
	size_t cno, search;
	int force, istmp, matches;
	char *np = NULL;
	size_t nlen;

	exp = EXP(sp);

	/* Check for connections. */
	if (SLIST_EMPTY(exp->cscq)) {
		msgq(sp, M_ERR, "310|No cscope connections running");
		return (1);
	}

	/*
	 * Allocate all necessary memory before doing anything hard.  If the
	 * tags stack is empty, we'll need the `local context' TAGQ structure
	 * later.
	 */
	rtp = NULL;
	rtqp = NULL;
	if (TAILQ_EMPTY(exp->tq)) {
		/* Initialize the `local context' tag queue structure. */
		CALLOC_GOTO(sp, rtqp, TAGQ *, 1, sizeof(TAGQ));
		TAILQ_INIT(rtqp->tagq);

		/* Initialize and link in its tag structure. */
		CALLOC_GOTO(sp, rtp, TAG *, 1, sizeof(TAG));
		TAILQ_INSERT_HEAD(rtqp->tagq, rtp, q);
		rtqp->current = rtp;
	}

	/* Create the cscope command. */
	INT2CHAR(sp, pattern, STRLEN(pattern) + 1, np, nlen);
	np = strdup(np);
	if ((tqp = create_cs_cmd(sp, np, &search)) == NULL)
		goto err;
	if (np != NULL)
		free(np);

	/*
	 * Stick the current context in a convenient place, we'll lose it
	 * when we switch files.
	 */
	frp = sp->frp;
	lno = sp->lno;
	cno = sp->cno;
	istmp = F_ISSET(sp->frp, FR_TMPFILE) && !F_ISSET(cmdp, E_NEWSCREEN);

	/* Search all open connections for a match. */
	matches = 0;
	/* Copy next connect here in case csc is killed. */
	SLIST_FOREACH_SAFE(csc, exp->cscq, q, csc_next) {
		/*
		 * Send the command to the cscope program.  (We skip the
		 * first two bytes of the command, because we stored the
		 * search cscope command character and a leading space
		 * there.)
		 */
		(void)fprintf(csc->to_fp, "%lu%s\n", search, tqp->tag + 2);
		(void)fflush(csc->to_fp);

		/* Read the output. */
		if (parse(sp, csc, tqp, &matches))
			goto nomatch;
	}

	if (matches == 0) {
		msgq(sp, M_INFO, "278|No matches for query");
nomatch:	if (rtp != NULL)
			free(rtp);
		if (rtqp != NULL)
			free(rtqp);
		tagq_free(sp, tqp);
		return (1);
	}

	/* Try to switch to the first tag. */
	force = FL_ISSET(cmdp->iflags, E_C_FORCE);
	if (F_ISSET(cmdp, E_NEWSCREEN)) {
		if (ex_tag_Nswitch(sp, tqp->current, force))
			goto err;

		/* Everything else gets done in the new screen. */
		sp = sp->nextdisp;
		exp = EXP(sp);
	} else
		if (ex_tag_nswitch(sp, tqp->current, force))
			goto err;

	/*
	 * If this is the first tag, put a `current location' queue entry
	 * in place, so we can pop all the way back to the current mark.
	 * Note, it doesn't point to much of anything, it's a placeholder.
	 */
	if (TAILQ_EMPTY(exp->tq)) {
		TAILQ_INSERT_HEAD(exp->tq, rtqp, q);
	} else
		rtqp = TAILQ_FIRST(exp->tq);

	/* Link the current TAGQ structure into place. */
	TAILQ_INSERT_HEAD(exp->tq, tqp, q);

	(void)cscope_search(sp, tqp, tqp->current);

	/*
	 * Move the current context from the temporary save area into the
	 * right structure.
	 *
	 * If we were in a temporary file, we don't have a context to which
	 * we can return, so just make it be the same as what we're moving
	 * to.  It will be a little odd that ^T doesn't change anything, but
	 * I don't think it's a big deal.
	 */
	if (istmp) {
		rtqp->current->frp = sp->frp;
		rtqp->current->lno = sp->lno;
		rtqp->current->cno = sp->cno;
	} else {
		rtqp->current->frp = frp;
		rtqp->current->lno = lno;
		rtqp->current->cno = cno;
	}

	return (0);

err:
alloc_err:
	if (rtqp != NULL)
		free(rtqp);
	if (rtp != NULL)
		free(rtp);
	if (np != NULL)
		free(np);
	return (1);
}

/*
 * create_cs_cmd --
 *	Build a cscope command, creating and initializing the base TAGQ.
 */
static TAGQ *
create_cs_cmd(SCR *sp, char *pattern, size_t *searchp)
{
	CB *cbp;
	TAGQ *tqp;
	size_t tlen;
	char *p;

	/*
	 * Cscope supports a "change pattern" command which we never use,
	 * cscope command 5.  Set CSCOPE_QUERIES[5] to " " since the user
	 * can't pass " " as the first character of pattern.  That way the
	 * user can't ask for pattern 5 so we don't need any special-case
	 * code.
	 */
#define	CSCOPE_QUERIES		"sgdct efi"

	if (pattern == NULL)
		goto usage;

	/* Skip leading blanks, check for command character. */
	for (; cmdskip(pattern[0]); ++pattern);
	if (pattern[0] == '\0' || !cmdskip(pattern[1]))
		goto usage;
	for (*searchp = 0, p = CSCOPE_QUERIES;
	    *p != '\0' && *p != pattern[0]; ++*searchp, ++p);
	if (*p == '\0') {
		msgq(sp, M_ERR,
		    "311|%s: unknown search type: use one of %s",
		    KEY_NAME(sp, pattern[0]), CSCOPE_QUERIES);
		return (NULL);
	}

	/* Skip <blank> characters to the pattern. */
	for (p = pattern + 1; *p != '\0' && cmdskip(*p); ++p);
	if (*p == '\0') {
usage:		(void)csc_help(sp, "find");
		return (NULL);
	}

	/* The user can specify the contents of a buffer as the pattern. */
	cbp = NULL;
	if (p[0] == '"' && p[1] != '\0' && p[2] == '\0')
		CBNAME(sp, cbp, p[1]);
	if (cbp != NULL) {
		INT2CHAR(sp, TAILQ_FIRST(cbp->textq)->lb,
			TAILQ_FIRST(cbp->textq)->len, p, tlen);
	} else
		tlen = strlen(p);

	/* Allocate and initialize the TAGQ structure. */
	CALLOC(sp, tqp, TAGQ *, 1, sizeof(TAGQ) + tlen + 3);
	if (tqp == NULL)
		return (NULL);
	TAILQ_INIT(tqp->tagq);
	tqp->tag = tqp->buf;
	tqp->tag[0] = pattern[0];
	tqp->tag[1] = ' ';
	tqp->tlen = tlen + 2;
	memcpy(tqp->tag + 2, p, tlen);
	tqp->tag[tlen + 2] = '\0';
	F_SET(tqp, TAG_CSCOPE);

	return (tqp);
}

/*
 * parse --
 *	Parse the cscope output.
 */
static int
parse(SCR *sp, CSC *csc, TAGQ *tqp, int *matchesp)
{
	TAG *tp;
	recno_t slno = 0;
	size_t dlen, nlen = 0, slen = 0;
	int ch, i, isolder = 0, nlines;
	char *dname = NULL, *name = NULL, *search, *p, *t, dummy[2], buf[2048];
	CHAR_T *wp;
	size_t wlen;

	for (;;) {
		if (!fgets(buf, sizeof(buf), csc->from_fp))
			goto io_err;

		/*
		 * If the database is out of date, or there's some other
		 * problem, cscope will output error messages before the
		 * number-of-lines output.  Display/discard any output
		 * that doesn't match what we want.
		 */
#define	CSCOPE_NLINES_FMT	"cscope: %d lines%1[\n]"
		if (sscanf(buf, CSCOPE_NLINES_FMT, &nlines, dummy) == 2)
			break;
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		msgq(sp, M_ERR, "%s: \"%s\"", csc->dname, buf);
	}

	while (nlines--) {
		if (fgets(buf, sizeof(buf), csc->from_fp) == NULL)
			goto io_err;

		/* If the line's too long for the buffer, discard it. */
		if ((p = strchr(buf, '\n')) == NULL) {
			while ((ch = getc(csc->from_fp)) != EOF && ch != '\n');
			continue;
		}
		*p = '\0';

		/*
		 * The cscope output is in the following format:
		 *
		 *	<filename> <context> <line number> <pattern>
		 *
		 * Figure out how long everything is so we can allocate in one
		 * swell foop, but discard anything that looks wrong.
		 */
		for (p = buf, i = 0;
		    i < 3 && (t = strsep(&p, "\t ")) != NULL; ++i)
			switch (i) {
			case 0:			/* Filename. */
				name = t;
				nlen = strlen(name);
				break;
			case 1:			/* Context. */
				break;
			case 2:			/* Line number. */
				slno = (recno_t)atol(t);
				break;
			}
		if (i != 3 || p == NULL || t == NULL)
			continue;

		/* The rest of the string is the search pattern. */
		search = p;
		slen = strlen(p);

		/* Resolve the file name. */
		csc_file(sp, csc, name, &dname, &dlen, &isolder);

		/*
		 * If the file is older than the cscope database, that is,
		 * the database was built since the file was last modified,
		 * or there wasn't a search string, use the line number.
		 */
		if (isolder || strcmp(search, "<unknown>") == 0) {
			search = NULL;
			slen = 0;
		}

		/*
		 * Allocate and initialize a tag structure plus the variable
		 * length cscope information that follows it.
		 */
		CALLOC_RET(sp, tp,
		    TAG *, 1, sizeof(TAG) + dlen + 2 + nlen + 1 +
		    (slen + 1) * sizeof(CHAR_T));
		tp->fname = (char *)tp->buf;
		if (dlen == 1 && *dname == '.')
			--dlen;
		else if (dlen != 0) {
			memcpy(tp->fname, dname, dlen);
			tp->fname[dlen] = '/';
			++dlen;
		}
		memcpy(tp->fname + dlen, name, nlen + 1);
		tp->fnlen = dlen + nlen;
		tp->slno = slno;
		tp->search = (CHAR_T*)(tp->fname + tp->fnlen + 1);
		CHAR2INT(sp, search, slen + 1, wp, wlen);
		MEMCPY(tp->search, wp, (tp->slen = slen) + 1);
		TAILQ_INSERT_TAIL(tqp->tagq, tp, q);

		/* Try to preset the tag within the current file. */
		if (sp->frp != NULL && sp->frp->name != NULL &&
		    tqp->current == NULL && !strcmp(tp->fname, sp->frp->name))
			tqp->current = tp;

		++*matchesp;
	}

	if (tqp->current == NULL)
		tqp->current = TAILQ_FIRST(tqp->tagq);

	return read_prompt(sp, csc);

io_err:	if (feof(csc->from_fp))
		errno = EIO;
	msgq_str(sp, M_SYSERR, "%s", csc->dname);
	terminate(sp, csc, 0);
	return (1);
}

/*
 * csc_file --
 *	Search for the right path to this file.
 */
static void
csc_file(SCR *sp, CSC *csc, char *name, char **dirp, size_t *dlenp, int *isolderp)
{
	struct stat sb;
	char **pp, *buf;

	/*
	 * Check for the file in all of the listed paths.  If we don't
	 * find it, we simply return it unchanged.  We have to do this
	 * now, even though it's expensive, because if the user changes
	 * directories, we can't change our minds as to where the file
	 * lives.
	 */
	for (pp = csc->paths; *pp != NULL; ++pp) {
		if ((buf = join(*pp, name)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			*dlenp = 0;
			return;
		}
		if (stat(buf, &sb) == 0) {
			free(buf);
			*dirp = *pp;
			*dlenp = strlen(*pp);
			*isolderp = timespeccmp(
			    &sb.st_mtimespec, &csc->mtim, <);
			return;
		}
		free(buf);
	}
	*dlenp = 0;
}

/*
 * cscope_help --
 *	The cscope help command.
 */
static int
cscope_help(SCR *sp, EXCMD *cmdp, CHAR_T *subcmd)
{
	char *np;
	size_t nlen;

	INT2CHAR(sp, subcmd, STRLEN(subcmd) + 1, np, nlen);
	return (csc_help(sp, np));
}

/*
 * csc_help --
 *	Display help/usage messages.
 */
static int
csc_help(SCR *sp, char *cmd)
{
	CC const *ccp;

	if (cmd != NULL && *cmd != '\0')
		if ((ccp = lookup_ccmd(cmd)) == NULL) {
			ex_printf(sp,
			    "%s doesn't match any cscope command\n", cmd);
			return (1);
		} else {
			ex_printf(sp,
			  "Command: %s (%s)\n", ccp->name, ccp->help_msg);
			ex_printf(sp, "  Usage: %s\n", ccp->usage_msg);
			return (0);
		}

	ex_printf(sp, "cscope commands:\n");
	for (ccp = cscope_cmds; ccp->name != NULL; ++ccp)
		ex_printf(sp, "  %*s: %s\n", 5, ccp->name, ccp->help_msg);
	return (0);
}

/*
 * cscope_kill --
 *	The cscope kill command.
 */
static int
cscope_kill(SCR *sp, EXCMD *cmdp, CHAR_T *cn)
{
	char *np;
	size_t nlen;
	int n = 1;

	if (*cn) {
		INT2CHAR(sp, cn, STRLEN(cn) + 1, np, nlen);
		n = atoi(np);
	}
	return (terminate(sp, NULL, n));
}

/*
 * terminate --
 *	Detach from a cscope process.
 */
static int
terminate(SCR *sp, CSC *csc, int n)
{
	EX_PRIVATE *exp;
	int i = 0, pstat;
	CSC *cp, *pre_cp = NULL;

	exp = EXP(sp);

	/*
	 * We either get a csc structure or a number.  Locate and remove
	 * the candidate which matches the structure or the number.
	 */
	if (csc == NULL && n < 1)
		goto badno;
	SLIST_FOREACH(cp, exp->cscq, q) {
		++i;
		if (csc == NULL ? i != n : cp != csc) {
			pre_cp = cp;
			continue;
		}
		if (cp == SLIST_FIRST(exp->cscq))
			SLIST_REMOVE_HEAD(exp->cscq, q);
		else
			SLIST_REMOVE_AFTER(pre_cp, q);
		csc = cp;
		break;
	}
	if (csc == NULL) {
badno:		msgq(sp, M_ERR, "312|%d: no such cscope session", n);
		return (1);
	}

	/*
	 * XXX
	 * Theoretically, we have the only file descriptors to the process,
	 * so closing them should let it exit gracefully, deleting temporary
	 * files, etc.  However, the earlier created cscope processes seems
	 * to refuse to quit unless we send a SIGTERM signal.
	 */
	if (csc->from_fp != NULL)
		(void)fclose(csc->from_fp);
	if (csc->to_fp != NULL)
		(void)fclose(csc->to_fp);
	if (i > 1)
		(void)kill(csc->pid, SIGTERM);
	(void)waitpid(csc->pid, &pstat, 0);

	/* Discard cscope connection information. */
	if (csc->pbuf != NULL)
		free(csc->pbuf);
	if (csc->paths != NULL)
		free(csc->paths);
	free(csc);
	return (0);
}

/*
 * cscope_reset --
 *	The cscope reset command.
 */
static int
cscope_reset(SCR *sp, EXCMD *cmdp, CHAR_T *notusedp)
{
	return cscope_end(sp);
}

/*
 * cscope_end --
 *	End all cscope connections.
 *
 * PUBLIC: int cscope_end(SCR *);
 */
int
cscope_end(SCR *sp)
{
	EX_PRIVATE *exp;

	for (exp = EXP(sp); !SLIST_EMPTY(exp->cscq);)
		if (terminate(sp, NULL, 1))
			return (1);
	return (0);
}

/*
 * cscope_display --
 *	Display current connections.
 *
 * PUBLIC: int cscope_display(SCR *);
 */
int
cscope_display(SCR *sp)
{
	EX_PRIVATE *exp;
	CSC *csc;
	int i = 0;

	exp = EXP(sp);
	if (SLIST_EMPTY(exp->cscq)) {
		ex_printf(sp, "No cscope connections.\n");
		return (0);
	}
	SLIST_FOREACH(csc, exp->cscq, q)
		ex_printf(sp, "%2d %s (process %lu)\n",
		    ++i, csc->dname, (u_long)csc->pid);
	return (0);
}

/*
 * cscope_search --
 *	Search a file for a cscope entry.
 *
 * PUBLIC: int cscope_search(SCR *, TAGQ *, TAG *);
 */
int
cscope_search(SCR *sp, TAGQ *tqp, TAG *tp)
{
	MARK m;

	/* If we don't have a search pattern, use the line number. */
	if (tp->search == NULL) {
		if (!db_exist(sp, tp->slno)) {
			tag_msg(sp, TAG_BADLNO, tqp->tag);
			return (1);
		}
		m.lno = tp->slno;
	} else {
		/*
		 * Search for the tag; cheap fallback for C functions
		 * if the name is the same but the arguments have changed.
		 */
		m.lno = 1;
		m.cno = 0;
		if (f_search(sp, &m, &m,
		    tp->search, tp->slen, NULL, SEARCH_CSCOPE | SEARCH_FILE)) {
			tag_msg(sp, TAG_SEARCH, tqp->tag);
			return (1);
		}

		/*
		 * !!!
		 * Historically, tags set the search direction if it wasn't
		 * already set.
		 */
		if (sp->searchdir == NOTSET)
			sp->searchdir = FORWARD;
	}

	/*
	 * !!!
	 * Tags move to the first non-blank, NOT the search pattern start.
	 */
	sp->lno = m.lno;
	sp->cno = 0;
	(void)nonblank(sp, sp->lno, &sp->cno);
	return (0);
}


/*
 * lookup_ccmd --
 *	Return a pointer to the command structure.
 */
static CC const *
lookup_ccmd(char *name)
{
	CC const *ccp;
	size_t len;

	len = strlen(name);
	for (ccp = cscope_cmds; ccp->name != NULL; ++ccp)
		if (strncmp(name, ccp->name, len) == 0)
			return (ccp);
	return (NULL);
}

/*
 * read_prompt --
 *	Read a prompt from cscope.
 */
static int
read_prompt(SCR *sp, CSC *csc)
{
	int ch;

#define	CSCOPE_PROMPT		">> "
	for (;;) {
		while ((ch =
		    getc(csc->from_fp)) != EOF && ch != CSCOPE_PROMPT[0]);
		if (ch == EOF) {
			terminate(sp, csc, 0);
			return (1);
		}
		if (getc(csc->from_fp) != CSCOPE_PROMPT[1])
			continue;
		if (getc(csc->from_fp) != CSCOPE_PROMPT[2])
			continue;
		break;
	}
	return (0);
}
