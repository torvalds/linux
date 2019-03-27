/* $Header: /p/tcsh/cvsroot/tcsh/tw.parse.c,v 3.139 2015/10/16 14:59:56 christos Exp $ */
/*
 * tw.parse.c: Everyone has taken a shot in this futile effort to
 *	       lexically analyze a csh line... Well we cannot good
 *	       a job as good as sh.lex.c; but we try. Amazing that
 *	       it works considering how many hands have touched this code
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"

RCSID("$tcsh: tw.parse.c,v 3.139 2015/10/16 14:59:56 christos Exp $")

#include "tw.h"
#include "ed.h"
#include "tc.h"

#include <assert.h>

#ifdef WINNT_NATIVE
#include "nt.const.h"
#endif /* WINNT_NATIVE */
#define EVEN(x) (((x) & 1) != 1)

#define DOT_NONE	0	/* Don't display dot files		*/
#define DOT_NOT		1	/* Don't display dot or dot-dot		*/
#define DOT_ALL		2	/* Display all dot files		*/

/*  TW_NONE,	       TW_COMMAND,     TW_VARIABLE,    TW_LOGNAME,	*/
/*  TW_FILE,	       TW_DIRECTORY,   TW_VARLIST,     TW_USER,		*/
/*  TW_COMPLETION,     TW_ALIAS,       TW_SHELLVAR,    TW_ENVVAR,	*/
/*  TW_BINDING,        TW_WORDLIST,    TW_LIMIT,       TW_SIGNAL	*/
/*  TW_JOB,	       TW_EXPLAIN,     TW_TEXT,	       TW_GRPNAME	*/
static void (*const tw_start_entry[]) (DIR *, const Char *) = {
    tw_file_start,     tw_cmd_start,   tw_var_start,   tw_logname_start, 
    tw_file_start,     tw_file_start,  tw_vl_start,    tw_logname_start, 
    tw_complete_start, tw_alias_start, tw_var_start,   tw_var_start,     
    tw_bind_start,     tw_wl_start,    tw_limit_start, tw_sig_start,
    tw_job_start,      tw_file_start,  tw_file_start,  tw_grpname_start
};

static int (*const tw_next_entry[]) (struct Strbuf *, struct Strbuf *,
				     int *) = {
    tw_file_next,      tw_cmd_next,    tw_var_next,    tw_logname_next,  
    tw_file_next,      tw_file_next,   tw_var_next,    tw_logname_next,  
    tw_var_next,       tw_var_next,    tw_shvar_next,  tw_envvar_next,   
    tw_bind_next,      tw_wl_next,     tw_limit_next,  tw_sig_next,
    tw_job_next,       tw_file_next,   tw_file_next,   tw_grpname_next
};

static void (*const tw_end_entry[]) (void) = {
    tw_dir_end,        tw_dir_end,     tw_dir_end,    tw_logname_end,
    tw_dir_end,        tw_dir_end,     tw_dir_end,    tw_logname_end, 
    tw_dir_end,        tw_dir_end,     tw_dir_end,    tw_dir_end,
    tw_dir_end,        tw_dir_end,     tw_dir_end,    tw_dir_end,
    tw_dir_end,	       tw_dir_end,     tw_dir_end,    tw_grpname_end
};

/* #define TDEBUG */

/* Set to TRUE if recexact is set and an exact match is found
 * along with other, longer, matches.
 */

int curchoice = -1;

int match_unique_match = FALSE;
int non_unique_match = FALSE;
static int SearchNoDirErr = 0;	/* t_search returns -2 if dir is unreadable */

/* state so if a completion is interrupted, the input line doesn't get
   nuked */
int InsideCompletion = 0;

/* do the expand or list on the command line -- SHOULD BE REPLACED */

static	void	 extract_dir_and_name	(const Char *, struct Strbuf *,
					 Char **);
static	int	 insert_meta		(const Char *, const Char *,
					 const Char *, int);
static	int	 tilde			(struct Strbuf *, Char *);
static  int      expand_dir		(const Char *, struct Strbuf *, DIR **,
					 COMMAND);
static	int	 nostat			(Char *);
static	Char	 filetype		(Char *, Char *);
static	int	 t_glob			(Char ***, int);
static	int	 c_glob			(Char ***);
static	int	 is_prefix		(Char *, Char *);
static	int	 is_prefixmatch		(Char *, Char *, int);
static	int	 is_suffix		(Char *, Char *);
static	int	 recognize		(struct Strbuf *, const Char *, size_t,
					 int, int, int);
static	int	 ignored		(Char *);
static	int	 isadirectory		(const Char *, const Char *);
static  int      tw_collect_items	(COMMAND, int, struct Strbuf *,
					 struct Strbuf *, Char *, const Char *,
					 int);
static  int      tw_collect		(COMMAND, int, struct Strbuf *,
					 struct Strbuf *, Char *, Char *, int,
					 DIR *);
static	Char 	 tw_suffix		(int, struct Strbuf *,const Char *,
					 Char *);
static	void 	 tw_fixword		(int, struct Strbuf *, Char *, Char *);
static	void	 tw_list_items		(int, int, int);
static 	void	 add_scroll_tab		(Char *);
static 	void 	 choose_scroll_tab	(struct Strbuf *, int);
static	void	 free_scroll_tab	(void);
static	int	 find_rows		(Char *[], int, int);

#ifdef notdef
/*
 * If we find a set command, then we break a=b to a= and word becomes
 * b else, we don't break a=b. [don't use that; splits words badly and
 * messes up tw_complete()]
 */
#define isaset(c, w) ((w)[-1] == '=' && \
		      ((c)[0] == 's' && (c)[1] == 'e' && (c)[2] == 't' && \
		       ((c[3] == ' ' || (c)[3] == '\t'))))
#endif

/* TRUE if character must be quoted */
#define tricky(w) (cmap(w, _META | _DOL | _QF | _QB | _ESC | _GLOB) && w != '#')
/* TRUE if double quotes don't protect character */
#define tricky_dq(w) (cmap(w, _DOL | _QB))

/* tenematch():
 *	Return:
 *		> 1:    No. of items found
 *		= 1:    Exactly one match / spelling corrected
 *		= 0:    No match / spelling was correct
 *		< 0:    Error (incl spelling correction impossible)
 */
int
tenematch(Char *inputline, int num_read, COMMAND command)
{
    struct Strbuf qline = Strbuf_INIT;
    Char    qu = 0, *pat = STRNULL;
    size_t wp, word, wordp, cmd_start, oword = 0, ocmd_start = 0;
    Char   *str_end, *cp;
    Char   *word_start;
    Char   *oword_start = NULL;
    eChar suf = 0;
    int     looking;		/* what we are looking for		*/
    int     search_ret;		/* what search returned for debugging 	*/
    int     backq = 0;

    str_end = &inputline[num_read];
    cleanup_push(&qline, Strbuf_cleanup);

    word_start = inputline;
    word = cmd_start = 0;
    for (cp = inputline; cp < str_end; cp++) {
        if (!cmap(qu, _ESC)) {
	    if (cmap(*cp, _QF|_ESC)) {
		if (qu == 0 || qu == *cp) {
		    qu ^= *cp;
		    continue;
		}
	    }
	    if (qu != '\'' && cmap(*cp, _QB)) {
		if ((backq ^= 1) != 0) {
		    ocmd_start = cmd_start;
		    oword_start = word_start;
		    oword = word;
		    word_start = cp + 1;
		    word = cmd_start = qline.len + 1;
		}
		else {
		    cmd_start = ocmd_start;
		    word_start = oword_start;
		    word = oword;
		}
		Strbuf_append1(&qline, *cp);
		continue;
	    }
	}
	if (iscmdmeta(*cp))
	    cmd_start = qline.len + 1;

	/* Don't quote '/' to make the recognize stuff work easily */
	/* Don't quote '$' in double quotes */

	if (cmap(*cp, _ESC) && cp < str_end - 1 && cp[1] == HIST &&
	    HIST != '\0')
	    Strbuf_append1(&qline, *++cp | QUOTE);
	else if (qu && (tricky(*cp) || *cp == '~') &&
	    !(qu == '\"' && tricky_dq(*cp)))
	    Strbuf_append1(&qline, *cp | QUOTE);
	else
	    Strbuf_append1(&qline, *cp);
	if (ismetahash(qline.s[qline.len - 1])
	    /* || isaset(qline.s + cmd_start, qline.s + qline.len) */)
	    word = qline.len, word_start = cp + 1;
	if (cmap(qu, _ESC))
	    qu = 0;
      }
    Strbuf_terminate(&qline);
    wp = qline.len;

    /*
     *  SPECIAL HARDCODED COMPLETIONS:
     *    first word of command       -> TW_COMMAND
     *    everything else             -> TW_ZERO
     *
     */
    looking = starting_a_command(qline.s + word - 1, qline.s) ?
	TW_COMMAND : TW_ZERO;

    wordp = word;

#ifdef TDEBUG
    {
	const Char *p;

	xprintf(CGETS(30, 1, "starting_a_command %d\n"), looking);
	xprintf("\ncmd_start:%S:\n", qline.s + cmd_start);
	xprintf("qline:%S:\n", qline.s);
	xprintf("qline:");
	for (p = qline.s; *p; p++)
	    xprintf("%c", *p & QUOTE ? '-' : ' ');
	xprintf(":\n");
	xprintf("word:%S:\n", qline.s + word);
	xprintf("word:");
	for (p = qline.s + word; *p; p++)
	    xprintf("%c", *p & QUOTE ? '-' : ' ');
	xprintf(":\n");
    }
#endif

    if ((looking == TW_COMMAND || looking == TW_ZERO) &&
        (command == RECOGNIZE || command == LIST || command == SPELL ||
	 command == RECOGNIZE_SCROLL)) {
	Char *p;

#ifdef TDEBUG
	xprintf(CGETS(30, 2, "complete %d "), looking);
#endif
	p = qline.s + wordp;
	looking = tw_complete(qline.s + cmd_start, &p, &pat, looking, &suf);
	wordp = p - qline.s;
#ifdef TDEBUG
	xprintf(CGETS(30, 3, "complete %d %S\n"), looking, pat);
#endif
    }

    switch (command) {
	Char   *bptr;
	Char   *items[2], **ptr;
	int     i, count;

    case RECOGNIZE:
    case RECOGNIZE_SCROLL:
    case RECOGNIZE_ALL: {
	struct Strbuf wordbuf = Strbuf_INIT;
	Char   *slshp;

	if (adrof(STRautocorrect)) {
	    if ((slshp = Strrchr(qline.s + wordp, '/')) != NULL &&
		slshp[1] != '\0') {
		SearchNoDirErr = 1;
		for (bptr = qline.s + wordp; bptr < slshp; bptr++) {
		    /*
		     * do not try to correct spelling of words containing
		     * globbing characters
		     */
		    if (isglob(*bptr)) {
			SearchNoDirErr = 0;
			break;
		    }
		}
	    }
	}
	else
	    slshp = STRNULL;
	Strbuf_append(&wordbuf, qline.s + wordp);
	Strbuf_terminate(&wordbuf);
	cleanup_push(&wordbuf, Strbuf_cleanup);
	search_ret = t_search(&wordbuf, command, looking, 1, pat, suf);
	qline.len = wordp;
	Strbuf_append(&qline, wordbuf.s);
	Strbuf_terminate(&qline);
	cleanup_until(&wordbuf);
	SearchNoDirErr = 0;

	if (search_ret == -2) {
	    Char *rword;

	    rword = Strsave(slshp);
	    cleanup_push(rword, xfree);
	    if (slshp != STRNULL)
		*slshp = '\0';
	    wordbuf = Strbuf_init;
	    Strbuf_append(&wordbuf, qline.s + wordp);
	    Strbuf_terminate(&wordbuf);
	    cleanup_push(&wordbuf, Strbuf_cleanup);
	    search_ret = spell_me(&wordbuf, looking, pat, suf);
	    if (search_ret == 1) {
		Strbuf_append(&wordbuf, rword);
		Strbuf_terminate(&wordbuf);
		wp = wordp + wordbuf.len;
		search_ret = t_search(&wordbuf, command, looking, 1, pat, suf);
	    }
	    qline.len = wordp;
	    Strbuf_append(&qline, wordbuf.s);
	    Strbuf_terminate(&qline);
	    cleanup_until(rword);
	}
	if (qline.s[wp] != '\0' &&
	    insert_meta(word_start, str_end, qline.s + word, !qu) < 0)
	    goto err;		/* error inserting */
	break;
    }

    case SPELL: {
	struct Strbuf wordbuf = Strbuf_INIT;

	for (bptr = word_start; bptr < str_end; bptr++) {
	    /*
	     * do not try to correct spelling of words containing globbing
	     * characters
	     */
	    if (isglob(*bptr)) {
		search_ret = 0;
		goto end;
	    }
	}
	Strbuf_append(&wordbuf, qline.s + wordp);
	Strbuf_terminate(&wordbuf);
	cleanup_push(&wordbuf, Strbuf_cleanup);

	/*
	 * Don't try to spell things that we know they are correct.
	 * Trying to spell can hang when we have NFS mounted hung
	 * volumes.
	 */
	if ((looking == TW_COMMAND) && Strchr(wordbuf.s, '/') != NULL) {
	    if (executable(NULL, wordbuf.s, 0)) {
		cleanup_until(&wordbuf);
		search_ret = 0;
		goto end;
	    }
	}

	search_ret = spell_me(&wordbuf, looking, pat, suf);
	qline.len = wordp;
	Strbuf_append(&qline, wordbuf.s);
	Strbuf_terminate(&qline);
	cleanup_until(&wordbuf);
	if (search_ret == 1) {
	    if (insert_meta(word_start, str_end, qline.s + word, !qu) < 0)
		goto err;		/* error inserting */
	}
	break;
    }

    case PRINT_HELP:
	do_help(qline.s + cmd_start);
	search_ret = 1;
	break;

    case GLOB:
    case GLOB_EXPAND:
	items[0] = Strsave(qline.s + wordp);
	items[1] = NULL;
	cleanup_push(items[0], xfree);
	ptr = items;
	count = (looking == TW_COMMAND && Strchr(qline.s + wordp, '/') == 0) ?
		c_glob(&ptr) :
		t_glob(&ptr, looking == TW_COMMAND);
	cleanup_until(items[0]);
	if (ptr != items)
	    cleanup_push(ptr, blk_cleanup);
	if (count > 0) {
	    if (command == GLOB)
		print_by_column(STRNULL, ptr, count, 0);
	    else {
		DeleteBack(str_end - word_start);/* get rid of old word */
		for (i = 0; i < count; i++)
		    if (ptr[i] && *ptr[i]) {
			(void) quote(ptr[i]);
			if (insert_meta(0, 0, ptr[i], 0) < 0 ||
			    InsertStr(STRspace) < 0) {
			    if (ptr != items)
				cleanup_until(ptr);
			    goto err;		/* error inserting */
			}
		    }
	    }
	}
	if (ptr != items)
	    cleanup_until(ptr);
	search_ret = count;
	break;

    case VARS_EXPAND:
	bptr = dollar(qline.s + word);
	if (bptr != NULL) {
	    if (insert_meta(word_start, str_end, bptr, !qu) < 0) {
		xfree(bptr);
		goto err;		/* error inserting */
	    }
	    xfree(bptr);
	    search_ret = 1;
	    break;
	}
	search_ret = 0;
	break;

    case PATH_NORMALIZE:
	if ((bptr = dnormalize(qline.s + wordp, symlinks == SYM_IGNORE ||
			       symlinks == SYM_EXPAND)) != NULL) {
	    if (insert_meta(word_start, str_end, bptr, !qu) < 0) {
		xfree(bptr);
		goto err;		/* error inserting */
	    }
	    xfree(bptr);
	    search_ret = 1;
	    break;
	}
	search_ret = 0;
	break;

    case COMMAND_NORMALIZE: {
	Char *p;
	int found;

	found = cmd_expand(qline.s + wordp, &p);
	
	if (!found) {
	    xfree(p);
	    search_ret = 0;
	    break;
	}
	if (insert_meta(word_start, str_end, p, !qu) < 0) {
	    xfree(p);
	    goto err;		/* error inserting */
	}
	xfree(p);
	search_ret = 1;
	break;
    }

    case LIST:
    case LIST_ALL: {
	struct Strbuf wordbuf = Strbuf_INIT;

	Strbuf_append(&wordbuf, qline.s + wordp);
	Strbuf_terminate(&wordbuf);
	cleanup_push(&wordbuf, Strbuf_cleanup);
	search_ret = t_search(&wordbuf, LIST, looking, 1, pat, suf);
	qline.len = wordp;
	Strbuf_append(&qline, wordbuf.s);
	Strbuf_terminate(&qline);
	cleanup_until(&wordbuf);
	break;
    }

    default:
	xprintf(CGETS(30, 4, "%s: Internal match error.\n"), progname);
	search_ret = 1;
    }
 end:
    cleanup_until(&qline);
    return search_ret;

 err:
    cleanup_until(&qline);
    return -1;
} /* end tenematch */


/* t_glob():
 * 	Return a list of files that match the pattern
 */
static int
t_glob(Char ***v, int cmd)
{
    jmp_buf_t osetexit;
    int gflag;

    if (**v == 0)
	return (0);
    gflag = tglob(*v);
    if (gflag) {
	size_t omark;

	getexit(osetexit);	/* make sure to come back here */
	omark = cleanup_push_mark();
	if (setexit() == 0)
	    *v = globall(*v, gflag);
	cleanup_pop_mark(omark);
	resexit(osetexit);
	if (haderr) {
	    haderr = 0;
	    NeedsRedraw = 1;
	    return (-1);
	}
	if (*v == 0)
	    return (0);
    }
    else
	return (0);

    if (cmd) {
	Char **av = *v, *p;
	int fwd, i;

	for (i = 0, fwd = 0; av[i] != NULL; i++) 
	    if (!executable(NULL, av[i], 0)) {
		fwd++;		
		p = av[i];
		av[i] = NULL;
		xfree(p);
	    }
	    else if (fwd) 
		av[i - fwd] = av[i];

	if (fwd)
	    av[i - fwd] = av[i];
    }

    return blklen(*v);
} /* end t_glob */


/* c_glob():
 * 	Return a list of commands that match the pattern
 */
static int
c_glob(Char ***v)
{
    struct blk_buf av = BLK_BUF_INIT;
    struct Strbuf cmd = Strbuf_INIT, dir = Strbuf_INIT;
    Char *pat = **v;
    int flag;

    if (pat == NULL)
	return (0);

    cleanup_push(&av, bb_cleanup);
    cleanup_push(&cmd, Strbuf_cleanup);
    cleanup_push(&dir, Strbuf_cleanup);

    tw_cmd_start(NULL, NULL);
    while (cmd.len = 0, tw_cmd_next(&cmd, &dir, &flag) != 0) {
	Strbuf_terminate(&cmd);
	if (Gmatch(cmd.s, pat))
	    bb_append(&av, Strsave(cmd.s));
    }
    tw_dir_end();
    *v = bb_finish(&av);
    cleanup_ignore(&av);
    cleanup_until(&av);

    return av.len;
} /* end c_glob */


/* insert_meta():
 *      change the word before the cursor.
 *        cp must point to the start of the unquoted word.
 *        cpend to the end of it.
 *        word is the text that has to be substituted.
 *      strategy:
 *        try to keep all the quote characters of the user's input.
 *        change quote type only if necessary.
 */
static int
insert_meta(const Char *cp, const Char *cpend, const Char *word,
	    int closequotes)
{
    struct Strbuf buffer = Strbuf_INIT;
    Char *bptr;
    const Char *wptr;
    int in_sync = (cp != NULL);
    Char qu = 0;
    int ndel = (int) (cp ? cpend - cp : 0);
    Char w, wq;
    int res;

    for (wptr = word;;) {
	if (cp >= cpend)
	    in_sync = 0;
	if (in_sync && !cmap(qu, _ESC) && cmap(*cp, _QF|_ESC))
	    if (qu == 0 || qu == *cp) {
		qu ^= *cp;
		Strbuf_append1(&buffer, *cp++);
		continue;
	    }
	w = *wptr;
	if (w == 0)
	    break;

	wq = w & QUOTE;
#if INVALID_BYTE != 0
	/* add checking INVALID_BYTE for FIX UTF32 */
	if ((w & INVALID_BYTE) != INVALID_BYTE)		/* w < INVALID_BYTE */
#endif
	    w &= ~QUOTE;

	if (cmap(w, _ESC | _QF))
	    wq = QUOTE;		/* quotes are always quoted */

	if (!wq && qu && tricky(w) && !(qu == '\"' && tricky_dq(w))) {
	    /* We have to unquote the character */
	    in_sync = 0;
	    if (cmap(qu, _ESC))
		buffer.s[buffer.len - 1] = w;
	    else {
		Strbuf_append1(&buffer, qu);
		Strbuf_append1(&buffer, w);
		if (wptr[1] == 0)
		    qu = 0;
		else
		    Strbuf_append1(&buffer, qu);
	    }
	} else if (qu && w == qu) {
	    in_sync = 0;
	    if (buffer.len != 0 && buffer.s[buffer.len - 1] == qu) {
		/* User misunderstanding :) */
		buffer.s[buffer.len - 1] = '\\';
		Strbuf_append1(&buffer, w);
		qu = 0;
	    } else {
		Strbuf_append1(&buffer, qu);
		Strbuf_append1(&buffer, '\\');
		Strbuf_append1(&buffer, w);
		Strbuf_append1(&buffer, qu);
	    }
	}
	else if (wq && qu == '\"' && tricky_dq(w)) {
	    in_sync = 0;
	    Strbuf_append1(&buffer, qu);
	    Strbuf_append1(&buffer, '\\');
	    Strbuf_append1(&buffer, w);
	    Strbuf_append1(&buffer, qu);
	} else if (wq &&
		   ((!qu && (tricky(w) || (w == HISTSUB && HISTSUB != '\0'
		       && buffer.len == 0))) ||
		    (!cmap(qu, _ESC) && w == HIST && HIST != '\0'))) {
	    in_sync = 0;
	    Strbuf_append1(&buffer, '\\');
	    Strbuf_append1(&buffer, w);
	} else {
	    if (in_sync && *cp++ != w)
		in_sync = 0;
	    Strbuf_append1(&buffer, w);
	}
	wptr++;
	if (cmap(qu, _ESC))
	    qu = 0;
    }
    if (closequotes && qu && !cmap(qu, _ESC))
	Strbuf_append1(&buffer, w);
    bptr = Strbuf_finish(&buffer);
    if (ndel)
	DeleteBack(ndel);
    res = InsertStr(bptr);
    xfree(bptr);
    return res;
} /* end insert_meta */



/* is_prefix():
 *	return true if check matches initial chars in template
 *	This differs from PWB imatch in that if check is null
 *	it matches anything
 */
static int
is_prefix(Char *check, Char *template)
{
    for (; *check; check++, template++)
	if ((*check & TRIM) != (*template & TRIM))
	    return (FALSE);
    return (TRUE);
} /* end is_prefix */


/* is_prefixmatch():
 *	return true if check matches initial chars in template
 *	This differs from PWB imatch in that if check is null
 *	it matches anything
 * and matches on shortening of commands
 */
static int
is_prefixmatch(Char *check, Char *template, int enhanced)
{
    Char MCH1, MCH2, LCH1, LCH2;

    for (; *check; check++, template++) {
	if ((*check & TRIM) != (*template & TRIM)) {
	    MCH1 = (*check & TRIM);
	    MCH2 = (*template & TRIM);
            LCH1 = Isupper(MCH1) ? Tolower(MCH1) : 
		enhanced == 2 && MCH1 == '_' ? '-' : MCH1;
            LCH2 = Isupper(MCH2) ? Tolower(MCH2) :
		enhanced == 2 && MCH2 == '_' ? '-' : MCH2;
	    if (MCH1 != MCH2 && MCH1 != LCH2 &&
		(LCH1 != MCH2 || enhanced == 2)) {
		if (enhanced && ((*check & TRIM) == '-' || 
				 (*check & TRIM) == '.' ||
				 (*check & TRIM) == '_')) {
		    MCH1 = MCH2 = (*check & TRIM);
		    if (MCH1 == '_' && enhanced != 2) {
			MCH2 = '-';
		    } else if (MCH1 == '-') {
			MCH2 = '_';
		    }
		    for (; *template && (*template & TRIM) != MCH1 &&
					(*template & TRIM) != MCH2; template++)
			continue;
		    if (!*template) {
	                return (FALSE);
		    }
		} else {
		    return (FALSE);
		}
	    }
	}
    }
    return (TRUE);
} /* end is_prefixmatch */


/* is_suffix():
 *	Return true if the chars in template appear at the
 *	end of check, I.e., are it's suffix.
 */
static int
is_suffix(Char *check, Char *template)
{
    Char *t, *c;

    t = Strend(template);
    c = Strend(check);
    for (;;) {
	if (t == template)
	    return 1;
	if (c == check || (*--t & TRIM) != (*--c & TRIM))
	    return 0;
    }
} /* end is_suffix */


/* ignored():
 *	Return true if this is an ignored item
 */
static int
ignored(Char *item)
{
    struct varent *vp;
    Char **cp;

    if ((vp = adrof(STRfignore)) == NULL || (cp = vp->vec) == NULL)
	return (FALSE);
    for (; *cp != NULL; cp++)
	if (is_suffix(item, *cp))
	    return (TRUE);
    return (FALSE);
} /* end ignored */



/* starting_a_command():
 *	return true if the command starting at wordstart is a command
 */
int
starting_a_command(Char *wordstart, Char *inputline)
{
    Char *ptr, *ncmdstart;
    int     count, bsl;
    static  Char
            cmdstart[] = {'`', ';', '&', '(', '|', '\0'},
            cmdalive[] = {' ', '\t', '\'', '"', '<', '>', '\0'};

    /*
     * Find if the number of backquotes is odd or even.
     */
    for (ptr = wordstart, count = 0;
	 ptr >= inputline;
	 count += (*ptr-- == '`'))
	continue;
    /*
     * if the number of backquotes is even don't include the backquote char in
     * the list of command starting delimiters [if it is zero, then it does not
     * matter]
     */
    ncmdstart = cmdstart + EVEN(count);

    /*
     * look for the characters previous to this word if we find a command
     * starting delimiter we break. if we find whitespace and another previous
     * word then we are not a command
     * 
     * count is our state machine: 0 looking for anything 1 found white-space
     * looking for non-ws
     */
    for (count = 0; wordstart >= inputline; wordstart--) {
	if (*wordstart == '\0')
	    continue;
	if (Strchr(ncmdstart, *wordstart)) {
	    for (ptr = wordstart, bsl = 0; *(--ptr) == '\\'; bsl++);
	    if (bsl & 1) {
		wordstart--;
		continue;
	    } else
		break;
	}
	/*
	 * found white space
	 */
	if ((ptr = Strchr(cmdalive, *wordstart)) != NULL)
	    count = 1;
	if (count == 1 && !ptr)
	    return (FALSE);
    }

    if (wordstart > inputline)
	switch (*wordstart) {
	case '&':		/* Look for >& */
	    while (wordstart > inputline &&
		   (*--wordstart == ' ' || *wordstart == '\t'))
		continue;
	    if (*wordstart == '>')
		return (FALSE);
	    break;
	case '(':		/* check for foreach, if etc. */
	    while (wordstart > inputline &&
		   (*--wordstart == ' ' || *wordstart == '\t'))
		continue;
	    if (!iscmdmeta(*wordstart) &&
		(*wordstart != ' ' && *wordstart != '\t'))
		return (FALSE);
	    break;
	default:
	    break;
	}
    return (TRUE);
} /* end starting_a_command */


/* recognize():
 *	Object: extend what user typed up to an ambiguity.
 *	Algorithm:
 *	On first match, copy full item (assume it'll be the only match)
 *	On subsequent matches, shorten exp_name to the first
 *	character mismatch between exp_name and item.
 *	If we shorten it back to the prefix length, stop searching.
 */
static int
recognize(struct Strbuf *exp_name, const Char *item, size_t name_length,
	  int numitems, int enhanced, int igncase)
{
    Char MCH1, MCH2, LCH1, LCH2;
    Char *x;
    const Char *ent;
    size_t len = 0;

    if (numitems == 1) {	/* 1st match */
	exp_name->len = 0;
	Strbuf_append(exp_name, item);
	Strbuf_terminate(exp_name);
	return (0);
    }
    if (!enhanced && !igncase) {
	for (x = exp_name->s, ent = item; *x && (*x & TRIM) == (*ent & TRIM);
	     x++, ent++)
	    len++;
    } else {
	for (x = exp_name->s, ent = item; *x; x++, ent++) {
	    MCH1 = *x & TRIM;
	    MCH2 = *ent & TRIM;
	    LCH1 = Isupper(MCH1) ? Tolower(MCH1) : MCH1;
	    LCH2 = Isupper(MCH2) ? Tolower(MCH2) : MCH2;
	    if (MCH1 != MCH2) {
		if (LCH1 == MCH2 || (MCH1 == '_' && MCH2 == '-'))
		    *x = *ent;
		else if (LCH1 != LCH2)
		    break;
	    }
	    len++;
	}
    }
    *x = '\0';		/* Shorten at 1st char diff */
    exp_name->len = x - exp_name->s;
    if (!(match_unique_match || is_set(STRrecexact) || (enhanced && *ent)) &&
	len == name_length)	/* Ambiguous to prefix? */
	return (-1);	/* So stop now and save time */
    return (0);
} /* end recognize */


/* tw_collect_items():
 *	Collect items that match target.
 *	SPELL command:
 *		Returns the spelling distance of the closest match.
 *	else
 *		Returns the number of items found.
 *		If none found, but some ignored items were found,
 *		It returns the -number of ignored items.
 */
static int
tw_collect_items(COMMAND command, int looking, struct Strbuf *exp_dir,
		 struct Strbuf *exp_name, Char *target, const Char *pat,
		 int flags)
{
    int done = FALSE;			 /* Search is done */
    int showdots;			 /* Style to show dot files */
    int nignored = 0;			 /* Number of fignored items */
    int numitems = 0;			 /* Number of matched items */
    size_t name_length = Strlen(target); /* Length of prefix (file name) */
    int exec_check = flags & TW_EXEC_CHK;/* need to check executability	*/
    int dir_check  = flags & TW_DIR_CHK; /* Need to check for directories */
    int text_check = flags & TW_TEXT_CHK;/* Need to check for non-directories */
    int dir_ok     = flags & TW_DIR_OK;  /* Ignore directories? */
    int gpat       = flags & TW_PAT_OK;	 /* Match against a pattern */
    int ignoring   = flags & TW_IGN_OK;	 /* Use fignore? */
    int d = 4, nd;			 /* Spelling distance */
    Char **cp;
    Char *ptr;
    struct varent *vp;
    struct Strbuf buf = Strbuf_INIT, item = Strbuf_INIT;
    int enhanced = 0;
    int cnt = 0;
    int igncase = 0;


    flags = 0;

    showdots = DOT_NONE;
    if ((ptr = varval(STRlistflags)) != STRNULL)
	while (*ptr) 
	    switch (*ptr++) {
	    case 'a':
		showdots = DOT_ALL;
		break;
	    case 'A':
		showdots = DOT_NOT;
		break;
	    default:
		break;
	    }

    if (looking == TW_COMMAND
	&& (vp = adrof(STRautorehash)) != NULL && vp->vec != NULL)
	for (cp = vp->vec; *cp; cp++)
	    if (Strcmp(*cp, STRalways) == 0
		|| (Strcmp(*cp, STRcorrect) == 0 && command == SPELL)
		|| (Strcmp(*cp, STRcomplete) == 0 && command != SPELL)) {
		tw_cmd_free();
		tw_cmd_start(NULL, NULL);
		break;
	    }

    cleanup_push(&item, Strbuf_cleanup);
    cleanup_push(&buf, Strbuf_cleanup);
    while (!done &&
	   (item.len = 0,
	    tw_next_entry[looking](&item, exp_dir, &flags) != 0)) {
	Strbuf_terminate(&item);
#ifdef TDEBUG
	xprintf("item = %S\n", item.s);
#endif
	switch (looking) {
	case TW_FILE:
	case TW_DIRECTORY:
	case TW_TEXT:
	    /*
	     * Don't match . files on null prefix match
	     */
	    if (showdots == DOT_NOT && (ISDOT(item.s) || ISDOTDOT(item.s)))
		done = TRUE;
	    if (name_length == 0 && item.s[0] == '.' && showdots == DOT_NONE)
		done = TRUE;
	    break;

	case TW_COMMAND:
#if defined(_UWIN) || defined(__CYGWIN__)
	    /*
	     * Turn foo.{exe,com,bat,cmd} into foo since UWIN's readdir returns
	     * the file with the .exe, .com, .bat, .cmd extension
	     *
	     * Same for Cygwin, but only for .exe and .com extension.
	     */
	    {
#ifdef __CYGWIN__
		static const char *rext[] = { ".exe", ".com" };
#else
		static const char *rext[] = { ".exe", ".bat", ".com", ".cmd" };
#endif
		size_t exti = Strlen(item.s);

		if (exti > 4) {
		    char *ext = short2str(&item.s[exti -= 4]);
		    size_t i;

		    for (i = 0; i < sizeof(rext) / sizeof(rext[0]); i++)
			if (strcasecmp(ext, rext[i]) == 0) {
			    item.len = exti;
			    Strbuf_terminate(&item);
			    break;
			}
		}
	    }
#endif /* _UWIN || __CYGWIN__ */
	    exec_check = flags & TW_EXEC_CHK;
	    dir_ok = flags & TW_DIR_OK;
	    break;

	default:
	    break;
	}

	if (done) {
	    done = FALSE;
	    continue;
	}

	switch (command) {

	case SPELL:		/* correct the spelling of the last bit */
	    if (name_length == 0) {/* zero-length word can't be misspelled */
		exp_name->len = 0; /* (not trying is important for ~) */
		Strbuf_terminate(exp_name);
		d = 0;
		done = TRUE;
		break;
	    }
	    if (gpat && !Gmatch(item.s, pat))
		break;
	    /*
	     * Swapped the order of the spdist() arguments as suggested
	     * by eeide@asylum.cs.utah.edu (Eric Eide)
	     */
	    nd = spdist(target, item.s); /* test the item against original */
	    if (nd <= d && nd != 4) {
		if (!(exec_check && !executable(exp_dir->s, item.s, dir_ok))) {
		    exp_name->len = 0;
		    Strbuf_append(exp_name, item.s);
		    Strbuf_terminate(exp_name);
		    d = nd;
		    if (d == 0)	/* if found it exactly */
			done = TRUE;
		}
	    }
	    else if (nd == 4) {
	        if (spdir(exp_name, exp_dir->s, item.s, target)) {
		    if (exec_check &&
			!executable(exp_dir->s, exp_name->s, dir_ok))
			break;
#ifdef notdef
		    /*
		     * We don't want to stop immediately, because
		     * we might find an exact/better match later.
		     */
		    d = 0;
		    done = TRUE;
#endif
		    d = 3;
		}
	    }
	    break;

	case LIST:
	case RECOGNIZE:
	case RECOGNIZE_ALL:
	case RECOGNIZE_SCROLL:

	    if ((vp = adrof(STRcomplete)) != NULL && vp->vec != NULL)
		for (cp = vp->vec; *cp; cp++) {
		    if (Strcmp(*cp, STREnhance) == 0)
			enhanced = 2;
		    else if (Strcmp(*cp, STRigncase) == 0)
			igncase = 1;
		    else if (Strcmp(*cp, STRenhance) == 0)
			enhanced = 1;
		}

	    if (enhanced || igncase) {
	        if (!is_prefixmatch(target, item.s, enhanced))
		    break;
     	    } else {
	        if (!is_prefix(target, item.s))
		    break;
	    }

	    if (exec_check && !executable(exp_dir->s, item.s, dir_ok))
		break;

	    if (dir_check && !isadirectory(exp_dir->s, item.s))
		break;

	    if (text_check && isadirectory(exp_dir->s, item.s))
		break;

	    /*
	     * Only pattern match directories if we're checking
	     * for directories.
	     */
	    if (gpat && !Gmatch(item.s, pat) &&
		(dir_check || !isadirectory(exp_dir->s, item.s)))
		    break;

	    /*
	     * Remove duplicates in command listing and completion
             * AFEB added code for TW_LOGNAME and TW_USER cases
	     */
	    if (looking == TW_COMMAND || looking == TW_LOGNAME
		|| looking == TW_USER || command == LIST) {
		buf.len = 0;
		Strbuf_append(&buf, item.s);
		switch (looking) {
		case TW_COMMAND:
		    if (!(dir_ok && exec_check))
			break;
		    if (filetype(exp_dir->s, item.s) == '/')
			Strbuf_append1(&buf, '/');
		    break;

		case TW_FILE:
		case TW_DIRECTORY:
		    Strbuf_append1(&buf, filetype(exp_dir->s, item.s));
		    break;

		default:
		    break;
		}
		Strbuf_terminate(&buf);
		if ((looking == TW_COMMAND || looking == TW_USER
                     || looking == TW_LOGNAME) && tw_item_find(buf.s))
		    break;
		else {
		    /* maximum length 1 (NULL) + 1 (~ or $) + 1 (filetype) */
		    tw_item_add(&buf);
		    if (command == LIST)
			numitems++;
		}
	    }
		    
	    if (command == RECOGNIZE || command == RECOGNIZE_ALL ||
		command == RECOGNIZE_SCROLL) {
		if (ignoring && ignored(item.s)) {
		    nignored++;
		    break;
		}
		else if (command == RECOGNIZE_SCROLL) {
		    add_scroll_tab(item.s);
		    cnt++;
		}

		if (match_unique_match || is_set(STRrecexact)) {
		    if (StrQcmp(target, item.s) == 0) {	/* EXACT match */
			exp_name->len = 0;
			Strbuf_append(exp_name, item.s);
			Strbuf_terminate(exp_name);
			numitems = 1;	/* fake into expanding */
			non_unique_match = TRUE;
			done = TRUE;
			break;
		    }
		}
		if (recognize(exp_name, item.s, name_length, ++numitems,
		    enhanced, igncase))
		    if (command != RECOGNIZE_SCROLL)
			done = TRUE;
		if (enhanced && exp_name->len < name_length) {
		    exp_name->len = 0;
		    Strbuf_append(exp_name, target);
		    Strbuf_terminate(exp_name);
		}
	    }
	    break;

	default:
	    break;
	}
#ifdef TDEBUG
	xprintf("done item = %S\n", item.s);
#endif
    }
    cleanup_until(&item);

    if (command == RECOGNIZE_SCROLL) {
	if ((cnt <= curchoice) || (curchoice == -1)) {
	    curchoice = -1;
	    nignored = 0;
	    numitems = 0;
	} else if (numitems > 1) {
	    if (curchoice < -1)
		curchoice = cnt - 1;
	    choose_scroll_tab(exp_name, cnt);
	    numitems = 1;
	}
    }
    free_scroll_tab();

    if (command == SPELL)
	return d;
    else {
	if (ignoring && numitems == 0 && nignored > 0) 
	    return -nignored;
	else
	    return numitems;
    }
}


/* tw_suffix():
 *	Find and return the appropriate suffix character
 */
/*ARGSUSED*/
static Char
tw_suffix(int looking, struct Strbuf *word, const Char *exp_dir, Char *exp_name)
{
    Char *ptr;
    Char *dol;
    struct varent *vp;

    (void) strip(exp_name);

    switch (looking) {

    case TW_LOGNAME:
	return '/';

    case TW_VARIABLE:
	/*
	 * Don't consider array variables or empty variables
	 */
	if ((vp = adrof(exp_name)) != NULL && vp->vec != NULL) {
	    if ((ptr = vp->vec[0]) == NULL || *ptr == '\0' ||
		vp->vec[1] != NULL) 
		return ' ';
	}
	else if ((ptr = tgetenv(exp_name)) == NULL || *ptr == '\0')
	    return ' ';

	if ((dol = Strrchr(word->s, '$')) != 0 && 
	    dol[1] == '{' && Strchr(dol, '}') == NULL)
	  return '}';

	return isadirectory(exp_dir, ptr) ? '/' : ' ';


    case TW_DIRECTORY:
	return '/';

    case TW_COMMAND:
    case TW_FILE:
	return isadirectory(exp_dir, exp_name) ? '/' : ' ';

    case TW_ALIAS:
    case TW_VARLIST:
    case TW_WORDLIST:
    case TW_SHELLVAR:
    case TW_ENVVAR:
    case TW_USER:
    case TW_BINDING:
    case TW_LIMIT:
    case TW_SIGNAL:
    case TW_JOB:
    case TW_COMPLETION:
    case TW_TEXT:
    case TW_GRPNAME:
	return ' ';

    default:
	return '\0';
    }
} /* end tw_suffix */


/* tw_fixword():
 *	Repair a word after a spalling or a recognizwe
 */
static void
tw_fixword(int looking, struct Strbuf *word, Char *dir, Char *exp_name)
{
    Char *ptr;

    switch (looking) {
    case TW_LOGNAME:
	word->len = 0;
	Strbuf_append1(word, '~');
	break;

    case TW_VARIABLE:
	if ((ptr = Strrchr(word->s, '$')) != NULL) {
	    if (ptr[1] == '{') ptr++;
	    word->len = ptr + 1 - word->s; /* Delete after the dollar */
	} else
	    word->len = 0;
	break;

    case TW_DIRECTORY:
    case TW_FILE:
    case TW_TEXT:
	word->len = 0;
	Strbuf_append(word, dir);		/* put back dir part */
	break;

    default:
	word->len = 0;
	break;
    }

    (void) quote(exp_name);
    Strbuf_append(word, exp_name);		/* add extended name */
    Strbuf_terminate(word);
} /* end tw_fixword */


/* tw_collect():
 *	Collect items. Return -1 in case we were interrupted or
 *	the return value of tw_collect
 *	This is really a wrapper for tw_collect_items, serving two
 *	purposes:
 *		1. Handles interrupt cleanups.
 *		2. Retries if we had no matches, but there were ignored matches
 */
static int
tw_collect(COMMAND command, int looking, struct Strbuf *exp_dir,
	   struct Strbuf *exp_name, Char *target, Char *pat, int flags,
	   DIR *dir_fd)
{
    volatile int ni;
    jmp_buf_t osetexit;

#ifdef TDEBUG
    xprintf("target = %S\n", target);
#endif
    ni = 0;
    getexit(osetexit);
    for (;;) {
	volatile size_t omark;

	(*tw_start_entry[looking])(dir_fd, pat);
	InsideCompletion = 1;
	if (setexit()) {
	    cleanup_pop_mark(omark);
	    resexit(osetexit);
	    /* interrupted, clean up */
	    haderr = 0;
	    ni = -1; /* flag error */
	    break;
	}
	omark = cleanup_push_mark();
	ni = tw_collect_items(command, looking, exp_dir, exp_name, target, pat,
			      ni >= 0 ? flags : flags & ~TW_IGN_OK);
	cleanup_pop_mark(omark);
	resexit(osetexit);
        if (ni >= 0)
	    break;
    }
    InsideCompletion = 0;
#if defined(SOLARIS2) && defined(i386) && !defined(__GNUC__)
    /* Compiler bug? (from PWP) */
    if ((looking == TW_LOGNAME) || (looking == TW_USER))
	tw_logname_end();
    else if (looking == TW_GRPNAME)
	tw_grpname_end();
    else
	tw_dir_end();
#else /* !(SOLARIS2 && i386 && !__GNUC__) */
    (*tw_end_entry[looking])();
#endif /* !(SOLARIS2 && i386 && !__GNUC__) */
    return(ni);
} /* end tw_collect */


/* tw_list_items():
 *	List the items that were found
 *
 *	NOTE instead of looking at numerical vars listmax and listmaxrows
 *	we can look at numerical var listmax, and have a string value
 *	listmaxtype (or similar) than can have values 'items' and 'rows'
 *	(by default interpreted as 'items', for backwards compatibility)
 */
static void
tw_list_items(int looking, int numitems, int list_max)
{
    Char *ptr;
    int max_items = 0;
    int max_rows = 0;

    if (numitems == 0)
	return;

    if ((ptr = varval(STRlistmax)) != STRNULL) {
	while (*ptr) {
	    if (!Isdigit(*ptr)) {
		max_items = 0;
		break;
	    }
	    max_items = max_items * 10 + *ptr++ - '0';
	}
	if ((max_items > 0) && (numitems > max_items) && list_max)
	    max_items = numitems;
	else
	    max_items = 0;
    }

    if (max_items == 0 && (ptr = varval(STRlistmaxrows)) != STRNULL) {
	int rows;

	while (*ptr) {
	    if (!Isdigit(*ptr)) {
		max_rows = 0;
		break;
	    }
	    max_rows = max_rows * 10 + *ptr++ - '0';
	}
	if (max_rows != 0 && looking != TW_JOB)
	    rows = find_rows(tw_item_get(), numitems, TRUE);
	else
	    rows = numitems; /* underestimate for lines wider than the termH */
	if ((max_rows > 0) && (rows > max_rows) && list_max)
	    max_rows = rows;
	else
	    max_rows = 0;
    }


    if (max_items || max_rows) {
	char    	 tc, *sname;
	const char	*name;
	int maxs;

	if (max_items) {
	    name = CGETS(30, 5, "items");
	    maxs = max_items;
	}
	else {
	    name = CGETS(30, 6, "rows");
	    maxs = max_rows;
	}

	sname = strsave(name);
	cleanup_push(sname, xfree);
	xprintf(CGETS(30, 7, "There are %d %s, list them anyway? [n/y] "),
		maxs, sname);
	cleanup_until(sname);
	flush();
	/* We should be in Rawmode here, so no \n to catch */
	(void) xread(SHIN, &tc, 1);
	xprintf("%c\r\n", tc);	/* echo the char, do a newline */
	/*
	 * Perhaps we should use the yesexpr from the
	 * actual locale
	 */
	if (strchr(CGETS(30, 13, "Yy"), tc) == NULL)
	    return;
    }

    if (looking != TW_SIGNAL)
	qsort(tw_item_get(), numitems, sizeof(Char *), fcompare);
    if (looking != TW_JOB)
	print_by_column(STRNULL, tw_item_get(), numitems, TRUE);
    else {
	/*
	 * print one item on every line because jobs can have spaces
	 * and it is confusing.
	 */
	int i;
	Char **w = tw_item_get();

	for (i = 0; i < numitems; i++) {
	    xprintf("%S", w[i]);
	    if (Tty_raw_mode)
		xputchar('\r');
	    xputchar('\n');
	}
    }
} /* end tw_list_items */


/* t_search():
 *	Perform a RECOGNIZE, LIST or SPELL command on string "word".
 *
 *	Return value:
 *		>= 0:   SPELL command: "distance" (see spdist())
 *		                other: No. of items found
 *  		 < 0:   Error (message or beep is output)
 */
/*ARGSUSED*/
int
t_search(struct Strbuf *word, COMMAND command, int looking, int list_max,
	 Char *pat, eChar suf)
{
    int     numitems,			/* Number of items matched */
	    flags = 0,			/* search flags */
	    gpat = pat[0] != '\0',	/* Glob pattern search */
	    res;			/* Return value */
    struct Strbuf exp_dir = Strbuf_INIT;/* dir after ~ expansion */
    struct Strbuf dir = Strbuf_INIT;	/* /x/y/z/ part in /x/y/z/f */
    struct Strbuf exp_name = Strbuf_INIT;/* the recognized (extended) */
    Char   *name,			/* f part in /d/d/d/f name */
           *target;			/* Target to expand/correct/list */
    DIR    *dir_fd = NULL;

    /*
     * bugfix by Marty Grossman (grossman@CC5.BBN.COM): directory listing can
     * dump core when interrupted
     */
    tw_item_free();

    non_unique_match = FALSE;	/* See the recexact code below */

    extract_dir_and_name(word->s, &dir, &name);
    cleanup_push(&dir, Strbuf_cleanup);
    cleanup_push(&name, xfree_indirect);

    /*
     *  SPECIAL HARDCODED COMPLETIONS:
     *    foo$variable                -> TW_VARIABLE
     *    ~user                       -> TW_LOGNAME
     *
     */
    if ((*word->s == '~') && (Strchr(word->s, '/') == NULL)) {
	looking = TW_LOGNAME;
	target = name;
	gpat = 0;	/* Override pattern mechanism */
    }
    else if ((target = Strrchr(name, '$')) != 0 && 
	     (target[1] != '{' || Strchr(target, '}') == NULL) &&
	     (Strchr(name, '/') == NULL)) {
	target++;
	if (target[0] == '{') target++;
	looking = TW_VARIABLE;
	gpat = 0;	/* Override pattern mechanism */
    }
    else
	target = name;

    /*
     * Try to figure out what we should be looking for
     */
    if (looking & TW_PATH) {
	gpat = 0;	/* pattern holds the pathname to be used */
	Strbuf_append(&exp_dir, pat);
	if (exp_dir.len != 0 && exp_dir.s[exp_dir.len - 1] != '/')
	    Strbuf_append1(&exp_dir, '/');
	Strbuf_append(&exp_dir, dir.s);
    }
    Strbuf_terminate(&exp_dir);
    cleanup_push(&exp_dir, Strbuf_cleanup);

    switch (looking & ~TW_PATH) {
    case TW_NONE:
	res = -1;
	goto err_dir;

    case TW_ZERO:
	looking = TW_FILE;
	break;

    case TW_COMMAND:
	if (Strchr(word->s, '/') || (looking & TW_PATH)) {
	    looking = TW_FILE;
	    flags |= TW_EXEC_CHK;
	    flags |= TW_DIR_OK;
	}
#ifdef notdef
	/* PWP: don't even bother when doing ALL of the commands */
	if (looking == TW_COMMAND && word->len == 0) {
	    res = -1;
	    goto err_dir;
	}
#endif
	break;


    case TW_VARLIST:
    case TW_WORDLIST:
	gpat = 0;	/* pattern holds the name of the variable */
	break;

    case TW_EXPLAIN:
	if (command == LIST && pat != NULL) {
	    xprintf("%S", pat);
	    if (Tty_raw_mode)
		xputchar('\r');
	    xputchar('\n');
	}
	res = 2;
	goto err_dir;

    default:
	break;
    }

    /*
     * let fignore work only when we are not using a pattern
     */
    flags |= (gpat == 0) ? TW_IGN_OK : TW_PAT_OK;

#ifdef TDEBUG
    xprintf(CGETS(30, 8, "looking = %d\n"), looking);
#endif

    switch (looking) {
	Char *user_name;

    case TW_ALIAS:
    case TW_SHELLVAR:
    case TW_ENVVAR:
    case TW_BINDING:
    case TW_LIMIT:
    case TW_SIGNAL:
    case TW_JOB:
    case TW_COMPLETION:
    case TW_GRPNAME:
	break;


    case TW_VARIABLE:
	if ((res = expand_dir(dir.s, &exp_dir, &dir_fd, command)) != 0)
	    goto err_dir;
	break;

    case TW_DIRECTORY:
	flags |= TW_DIR_CHK;

#ifdef notyet
	/*
	 * This is supposed to expand the directory stack.
	 * Problems:
	 * 1. Slow
	 * 2. directories with the same name
	 */
	flags |= TW_DIR_OK;
#endif
#ifdef notyet
	/*
	 * Supposed to do delayed expansion, but it is inconsistent
	 * from a user-interface point of view, since it does not
	 * immediately obey addsuffix
	 */
	if ((res = expand_dir(dir.s, &exp_dir, &dir_fd, command)) != 0)
	    goto err_dir;
	if (isadirectory(exp_dir.s, name)) {
	    if (exp_dir.len != 0 || name[0] != '\0') {
		Strbuf_append(&dir, name);
		if (dir.s[dir.len - 1] != '/')
		    Strbuf_append1(&dir, '/');
		Strbuf_terminate(&dir);
		if ((res = expand_dir(dir.s, &exp_dir, &dir_fd, command)) != 0)
		    goto err_dir;
		if (word->len != 0 && word->s[word->len - 1] != '/') {
		    Strbuf_append1(word, '/');
		    Strbuf_terminate(word);
		}
		name[0] = '\0';
	    }
	}
#endif
	if ((res = expand_dir(dir.s, &exp_dir, &dir_fd, command)) != 0)
	    goto err_dir;
	break;

    case TW_TEXT:
	flags |= TW_TEXT_CHK;
	/*FALLTHROUGH*/
    case TW_FILE:
	if ((res = expand_dir(dir.s, &exp_dir, &dir_fd, command)) != 0)
	    goto err_dir;
	break;

    case TW_PATH | TW_TEXT:
    case TW_PATH | TW_FILE:
    case TW_PATH | TW_DIRECTORY:
    case TW_PATH | TW_COMMAND:
	if ((dir_fd = opendir(short2str(exp_dir.s))) == NULL) {
 	    if (command == RECOGNIZE)
 		xprintf("\n");
 	    xprintf("%S: %s", exp_dir.s, strerror(errno));
 	    if (command != RECOGNIZE)
 		xprintf("\n");
 	    NeedsRedraw = 1;
	    res = -1;
	    goto err_dir;
	}
	if (exp_dir.len != 0 && exp_dir.s[exp_dir.len - 1] != '/') {
	    Strbuf_append1(&exp_dir, '/');
	    Strbuf_terminate(&exp_dir);
	}

	looking &= ~TW_PATH;

	switch (looking) {
	case TW_TEXT:
	    flags |= TW_TEXT_CHK;
	    break;

	case TW_FILE:
	    break;

	case TW_DIRECTORY:
	    flags |= TW_DIR_CHK;
	    break;

	case TW_COMMAND:
	    xfree(name);
	    target = name = Strsave(word->s);	/* so it can match things */
	    break;

	default:
	    abort();	/* Cannot happen */
	    break;
	}
	break;

    case TW_LOGNAME:
	user_name = word->s + 1;
	goto do_user;

	/*FALLTHROUGH*/
    case TW_USER:
	user_name = word->s;
    do_user:
	/*
	 * Check if the spelling was already correct
	 * From: Rob McMahon <cudcv@cu.warwick.ac.uk>
	 */
	if (command == SPELL && xgetpwnam(short2str(user_name)) != NULL) {
#ifdef YPBUGS
	    fix_yp_bugs();
#endif /* YPBUGS */
	    res = 0;
	    goto err_dir;
	}
	xfree(name);
	target = name = Strsave(user_name);
	break;

    case TW_COMMAND:
    case TW_VARLIST:
    case TW_WORDLIST:
	target = name = Strsave(word->s);	/* so it can match things */
	break;

    default:
	xprintf(CGETS(30, 9,
		"\n%s internal error: I don't know what I'm looking for!\n"),
		progname);
	NeedsRedraw = 1;
	res = -1;
	goto err_dir;
    }

    cleanup_push(&exp_name, Strbuf_cleanup);
    numitems = tw_collect(command, looking, &exp_dir, &exp_name, target, pat,
			  flags, dir_fd);
    if (numitems == -1)
	goto end;

    switch (command) {
    case RECOGNIZE:
    case RECOGNIZE_ALL:
    case RECOGNIZE_SCROLL:
	if (numitems <= 0) 
	    break;

	Strbuf_terminate(&exp_name);
	tw_fixword(looking, word, dir.s, exp_name.s);

	if (!match_unique_match && is_set(STRaddsuffix) && numitems == 1) {
	    switch (suf) {
	    case 0: 	/* Automatic suffix */
		Strbuf_append1(word,
			       tw_suffix(looking, word, exp_dir.s, exp_name.s));
		break;

	    case CHAR_ERR:	/* No suffix */
		break;

	    default:	/* completion specified suffix */
		Strbuf_append1(word, suf);
		break;
	    }
	    Strbuf_terminate(word);
	}
	break;

    case LIST:
	tw_list_items(looking, numitems, list_max);
	tw_item_free();
	break;

    case SPELL:
	Strbuf_terminate(&exp_name);
	tw_fixword(looking, word, dir.s, exp_name.s);
	break;

    default:
	xprintf("Bad tw_command\n");
	numitems = 0;
    }
 end:
    res = numitems;
 err_dir:
    cleanup_until(&dir);
    return res;
} /* end t_search */


/* extract_dir_and_name():
 * 	parse full path in file into 2 parts: directory and file names
 * 	Should leave final slash (/) at end of dir.
 */
static void
extract_dir_and_name(const Char *path, struct Strbuf *dir, Char **name)
{
    Char *p;

    p = Strrchr(path, '/');
#ifdef WINNT_NATIVE
    if (p == NULL)
	p = Strrchr(path, ':');
#endif /* WINNT_NATIVE */
    if (p == NULL)
	*name = Strsave(path);
    else {
	p++;
	*name = Strsave(p);
	Strbuf_appendn(dir, path, p - path);
    }
    Strbuf_terminate(dir);
} /* end extract_dir_and_name */


/* dollar():
 * 	expand "/$old1/$old2/old3/"
 * 	to "/value_of_old1/value_of_old2/old3/"
 */
Char *
dollar(const Char *old)
{
    struct Strbuf buf = Strbuf_INIT;

    while (*old) {
	if (*old != '$')
	    Strbuf_append1(&buf, *old++);
	else {
	    if (expdollar(&buf, &old, QUOTE) == 0) {
		xfree(buf.s);
		return NULL;
	    }
	}
    }
    return Strbuf_finish(&buf);
} /* end dollar */


/* tilde():
 * 	expand ~person/foo to home_directory_of_person/foo
 *	or =<stack-entry> to <dir in stack entry>
 */
static int
tilde(struct Strbuf *new, Char *old)
{
    Char *o, *p;

    new->len = 0;
    switch (old[0]) {
    case '~': {
	Char *name, *home;

	old++;
	for (o = old; *o && *o != '/'; o++)
	    continue;
	name = Strnsave(old, o - old);
	home = gethdir(name);
	xfree(name);
	if (home == NULL)
	    goto err;
	Strbuf_append(new, home);
	xfree(home);
	/* If the home directory expands to "/", we do
	 * not want to create "//" by appending a slash from o.
	 */
	if (new->s[0] == '/' && new->len == 1 && *o == '/')
	    ++o;
	Strbuf_append(new, o);
	break;
    }

    case '=':
	if ((p = globequal(old)) == NULL)
	    goto err;
	if (p != old) {
	    Strbuf_append(new, p);
	    xfree(p);
	    break;
	}
	/*FALLTHROUGH*/

    default:
	Strbuf_append(new, old);
	break;
    }
    Strbuf_terminate(new);
    return 0;

 err:
    Strbuf_terminate(new);
    return -1;
} /* end tilde */


/* expand_dir():
 *	Open the directory given, expanding ~user and $var
 *	Optionally normalize the path given
 */
static int
expand_dir(const Char *dir, struct Strbuf *edir, DIR **dfd, COMMAND cmd)
{
    Char   *nd = NULL;
    Char *tdir;

    tdir = dollar(dir);
    cleanup_push(tdir, xfree);
    if (tdir == NULL ||
	(tilde(edir, tdir) != 0) ||
	!(nd = dnormalize(edir->len ? edir->s : STRdot,
			  symlinks == SYM_IGNORE || symlinks == SYM_EXPAND)) ||
	((*dfd = opendir(short2str(nd))) == NULL)) {
	xfree(nd);
	if (cmd == SPELL || SearchNoDirErr) {
	    cleanup_until(tdir);
	    return (-2);
	}
	/*
	 * From: Amos Shapira <amoss@cs.huji.ac.il>
	 * Print a better message when completion fails
	 */
	xprintf("\n%S %s\n", edir->len ? edir->s : (tdir ? tdir : dir),
		(errno == ENOTDIR ? CGETS(30, 10, "not a directory") :
		(errno == ENOENT ? CGETS(30, 11, "not found") :
		 CGETS(30, 12, "unreadable"))));
	NeedsRedraw = 1;
	cleanup_until(tdir);
	return (-1);
    }
    cleanup_until(tdir);
    if (nd) {
	if (*dir != '\0') {
	    int slash;

	    /*
	     * Copy and append a / if there was one
	     */
	    slash = edir->len != 0 && edir->s[edir->len - 1] == '/';
	    edir->len = 0;
	    Strbuf_append(edir, nd);
	    if (slash != 0 && edir->s[edir->len - 1] != '/')
		Strbuf_append1(edir, '/');
	    Strbuf_terminate(edir);
	}
	xfree(nd);
    }
    return 0;
} /* end expand_dir */


/* nostat():
 *	Returns true if the directory should not be stat'd,
 *	false otherwise.
 *	This way, things won't grind to a halt when you complete in /afs
 *	or very large directories.
 */
static int
nostat(Char *dir)
{
    struct varent *vp;
    Char **cp;

    if ((vp = adrof(STRnostat)) == NULL || (cp = vp->vec) == NULL)
	return FALSE;
    for (; *cp != NULL; cp++) {
	if (Strcmp(*cp, STRstar) == 0)
	    return TRUE;
	if (Gmatch(dir, *cp))
	    return TRUE;
    }
    return FALSE;
} /* end nostat */


/* filetype():
 *	Return a character that signifies a filetype
 *	symbology from 4.3 ls command.
 */
static  Char
filetype(Char *dir, Char *file)
{
    if (dir) {
	Char *path;
	char   *ptr;
	struct stat statb;

	if (nostat(dir)) return(' ');

	path = Strspl(dir, file);
	ptr = short2str(path);
	xfree(path);

	if (lstat(ptr, &statb) != -1) {
#ifdef S_ISLNK
	    if (S_ISLNK(statb.st_mode)) {	/* Symbolic link */
		if (adrof(STRlistlinks)) {
		    if (stat(ptr, &statb) == -1)
			return ('&');
		    else if (S_ISDIR(statb.st_mode))
			return ('>');
		    else
			return ('@');
		}
		else
		    return ('@');
	    }
#endif
#ifdef S_ISSOCK
	    if (S_ISSOCK(statb.st_mode))	/* Socket */
		return ('=');
#endif
#ifdef S_ISFIFO
	    if (S_ISFIFO(statb.st_mode)) /* Named Pipe */
		return ('|');
#endif
#ifdef S_ISHIDDEN
	    if (S_ISHIDDEN(statb.st_mode)) /* Hidden Directory [aix] */
		return ('+');
#endif
#ifdef S_ISCDF
	    {
		struct stat hpstatb;
		char *p2;

		p2 = strspl(ptr, "+");	/* Must append a '+' and re-stat(). */
		if ((stat(p2, &hpstatb) != -1) && S_ISCDF(hpstatb.st_mode)) {
		    xfree(p2);
		    return ('+');	/* Context Dependent Files [hpux] */
		}
		xfree(p2);
	    }
#endif
#ifdef S_ISNWK
	    if (S_ISNWK(statb.st_mode)) /* Network Special [hpux] */
		return (':');
#endif
#ifdef S_ISCHR
	    if (S_ISCHR(statb.st_mode))	/* char device */
		return ('%');
#endif
#ifdef S_ISBLK
	    if (S_ISBLK(statb.st_mode))	/* block device */
		return ('#');
#endif
#ifdef S_ISDIR
	    if (S_ISDIR(statb.st_mode))	/* normal Directory */
		return ('/');
#endif
	    if (statb.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH))
		return ('*');
	}
    }
    return (' ');
} /* end filetype */


/* isadirectory():
 *	Return trus if the file is a directory
 */
static int
isadirectory(const Char *dir, const Char *file)
     /* return 1 if dir/file is a directory */
     /* uses stat rather than lstat to get dest. */
{
    if (dir) {
	Char *path;
	char *cpath;
	struct stat statb;

	path = Strspl(dir, file);
	cpath = short2str(path);
	xfree(path);
	if (stat(cpath, &statb) >= 0) {	/* resolve through symlink */
#ifdef S_ISSOCK
	    if (S_ISSOCK(statb.st_mode))	/* Socket */
		return 0;
#endif
#ifdef S_ISFIFO
	    if (S_ISFIFO(statb.st_mode))	/* Named Pipe */
		return 0;
#endif
	    if (S_ISDIR(statb.st_mode))	/* normal Directory */
		return 1;
	}
    }
    return 0;
} /* end isadirectory */



/* find_rows():
 * 	Return how many rows needed to print sorted down columns
 */
static int
find_rows(Char *items[], int count, int no_file_suffix)
{
    int i, columns, rows;
    unsigned int maxwidth = 0;

    for (i = 0; i < count; i++)	/* find widest string */
	maxwidth = max(maxwidth, (unsigned int) Strlen(items[i]));

    maxwidth += no_file_suffix ? 1 : 2;	/* for the file tag and space */
    columns = (TermH + 1) / maxwidth;	/* PWP: terminal size change */
    if (!columns)
	columns = 1;
    rows = (count + (columns - 1)) / columns;

    return rows;
} /* end rows_needed_by_print_by_column */


/* print_by_column():
 * 	Print sorted down columns or across columns when the first
 *	word of $listflags shell variable contains 'x'.
 *
 */
void
print_by_column(Char *dir, Char *items[], int count, int no_file_suffix)
{
    int i, r, c, columns, rows;
    size_t w;
    unsigned int wx, maxwidth = 0;
    Char *val;
    int across;

    lbuffed = 0;		/* turn off line buffering */

    
    across = ((val = varval(STRlistflags)) != STRNULL) && 
	     (Strchr(val, 'x') != NULL);

    for (i = 0; i < count; i++)	{ /* find widest string */
	maxwidth = max(maxwidth, (unsigned int) NLSStringWidth(items[i]));
    }

    maxwidth += no_file_suffix ? 1 : 2;	/* for the file tag and space */
    columns = TermH / maxwidth;		/* PWP: terminal size change */
    if (!columns || !isatty(didfds ? 1 : SHOUT))
	columns = 1;
    rows = (count + (columns - 1)) / columns;

    i = -1;
    for (r = 0; r < rows; r++) {
	for (c = 0; c < columns; c++) {
	    i = across ? (i + 1) : (c * rows + r);

	    if (i < count) {
		wx = 0;
		w = Strlen(items[i]);

#ifdef COLOR_LS_F
		if (no_file_suffix) {
		    /* Print the command name */
		    Char f = items[i][w - 1];
		    items[i][w - 1] = 0;
		    print_with_color(items[i], w - 1, f);
		    items[i][w - 1] = f;
		}
		else {
		    /* Print filename followed by '/' or '*' or ' ' */
		    print_with_color(items[i], w, filetype(dir, items[i]));
		    wx++;
		}
#else /* ifndef COLOR_LS_F */
		if (no_file_suffix) {
		    /* Print the command name */
		    xprintf("%S", items[i]);
		}
		else {
		    /* Print filename followed by '/' or '*' or ' ' */
		    xprintf("%-S%c", items[i], filetype(dir, items[i]));
		    wx++;
		}
#endif /* COLOR_LS_F */

		if (c < (columns - 1)) {	/* Not last column? */
		    w = NLSStringWidth(items[i]) + wx;
		    for (; w < maxwidth; w++)
			xputchar(' ');
		}
	    }
	    else if (across)
		break;
	}
	if (Tty_raw_mode)
	    xputchar('\r');
	xputchar('\n');
    }

    lbuffed = 1;		/* turn back on line buffering */
    flush();
} /* end print_by_column */


/* StrQcmp():
 *	Compare strings ignoring the quoting chars
 */
int
StrQcmp(const Char *str1, const Char *str2)
{
    for (; *str1 && samecase(*str1 & TRIM) == samecase(*str2 & TRIM); 
	 str1++, str2++)
	continue;
    /*
     * The following case analysis is necessary so that characters which look
     * negative collate low against normal characters but high against the
     * end-of-string NUL.
     */
    if (*str1 == '\0' && *str2 == '\0')
	return (0);
    else if (*str1 == '\0')
	return (-1);
    else if (*str2 == '\0')
	return (1);
    else
	return ((*str1 & TRIM) - (*str2 & TRIM));
} /* end StrQcmp */


/* fcompare():
 * 	Comparison routine for qsort, (Char **, Char **)
 */
int
fcompare(const void *xfile1, const void *xfile2)
{
    const Char *const *file1 = xfile1, *const *file2 = xfile2;

    return collate(*file1, *file2);
} /* end fcompare */


/* catn():
 *	Concatenate src onto tail of des.
 *	Des is a string whose maximum length is count.
 *	Always null terminate.
 */
void
catn(Char *des, const Char *src, int count)
{
    while (*des && --count > 0)
	des++;
    while (--count > 0)
	if ((*des++ = *src++) == 0)
	    return;
    *des = '\0';
} /* end catn */


/* copyn():
 *	 like strncpy but always leave room for trailing \0
 *	 and always null terminate.
 */
void
copyn(Char *des, const Char *src, size_t count)
{
    while (--count != 0)
	if ((*des++ = *src++) == 0)
	    return;
    *des = '\0';
} /* end copyn */


/* tgetenv():
 *	like it's normal string counter-part
 */
Char *
tgetenv(Char *str)
{
    Char  **var;
    size_t  len;
    int     res;

    len = Strlen(str);
    /* Search the STR_environ for the entry matching str. */
    for (var = STR_environ; var != NULL && *var != NULL; var++)
	if (Strlen(*var) >= len && (*var)[len] == '=') {
	  /* Temporarily terminate the string so we can copy the variable
	     name. */
	    (*var)[len] = '\0';
	    res = StrQcmp(*var, str);
	    /* Restore the '=' and return a pointer to the value of the
	       environment variable. */
	    (*var)[len] = '=';
	    if (res == 0)
		return (&((*var)[len + 1]));
	}
    return (NULL);
} /* end tgetenv */


struct scroll_tab_list *scroll_tab = 0;

static void
add_scroll_tab(Char *item)
{
    struct scroll_tab_list *new_scroll;

    new_scroll = xmalloc(sizeof(struct scroll_tab_list));
    new_scroll->element = Strsave(item);
    new_scroll->next = scroll_tab;
    scroll_tab = new_scroll;
}

static void
choose_scroll_tab(struct Strbuf *exp_name, int cnt)
{
    struct scroll_tab_list *loop;
    int tmp = cnt;
    Char **ptr;

    ptr = xmalloc(sizeof(Char *) * cnt);
    cleanup_push(ptr, xfree);

    for(loop = scroll_tab; loop && (tmp >= 0); loop = loop->next)
	ptr[--tmp] = loop->element;

    qsort(ptr, cnt, sizeof(Char *), fcompare);

    exp_name->len = 0;
    Strbuf_append(exp_name, ptr[curchoice]);
    Strbuf_terminate(exp_name);
    cleanup_until(ptr);
}

static void
free_scroll_tab(void)
{
    struct scroll_tab_list *loop;

    while(scroll_tab) {
	loop = scroll_tab;
	scroll_tab = scroll_tab->next;
	xfree(loop->element);
	xfree(loop);
    }
}
