/* $Header: /p/tcsh/cvsroot/tcsh/tw.init.c,v 3.42 2011/04/17 14:49:30 christos Exp $ */
/*
 * tw.init.c: Handle lists of things to complete
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

RCSID("$tcsh: tw.init.c,v 3.42 2011/04/17 14:49:30 christos Exp $")

#include "tw.h"
#include "ed.h"
#include "tc.h"
#include "sh.proc.h"

#define TW_INCR	128

typedef struct {
    Char **list, 			/* List of command names	*/
	  *buff;			/* Space holding command names	*/
    size_t nlist, 			/* Number of items		*/
           nbuff,			/* Current space in name buf	*/
           tlist,			/* Total space in list		*/
	   tbuff;			/* Total space in name buf	*/
} stringlist_t;


static struct varent *tw_vptr = NULL;	/* Current shell variable 	*/
static Char **tw_env = NULL;		/* Current environment variable */
static const Char *tw_word;		/* Current word pointer		*/
static struct KeyFuncs *tw_bind = NULL;	/* List of the bindings		*/
#ifndef HAVENOLIMIT
static struct limits *tw_limit = NULL;	/* List of the resource limits	*/
#endif /* HAVENOLIMIT */
static int tw_index = 0;		/* signal and job index		*/
static DIR   *tw_dir_fd = NULL;		/* Current directory descriptor	*/
static int    tw_cmd_got = 0;		/* What we need to do		*/
static stringlist_t tw_cmd  = { NULL, NULL, 0, 0, 0, 0 };
static stringlist_t tw_item = { NULL, NULL, 0, 0, 0, 0 };
#define TW_FL_CMD	0x01
#define TW_FL_ALIAS	0x02
#define TW_FL_BUILTIN	0x04
#define TW_FL_SORT	0x08
#define TW_FL_REL	0x10

static struct {				/* Current element pointer	*/
    size_t cur;				/* Current element number	*/
    Char **pathv;			/* Current element in path	*/
    DIR   *dfd;				/* Current directory descriptor	*/
} tw_cmd_state;


#define SETDIR(dfd) \
    { \
	tw_dir_fd = dfd; \
	if (tw_dir_fd != NULL) \
	    rewinddir(tw_dir_fd); \
    }

#define CLRDIR(dfd) \
    if (dfd != NULL) { \
	pintr_disabled++; \
	xclosedir(dfd); \
	dfd = NULL; \
	disabled_cleanup(&pintr_disabled); \
    }

static Char	*tw_str_add		(stringlist_t *, size_t);
static void	 tw_str_free		(stringlist_t *);
static int       tw_dir_next		(struct Strbuf *, DIR *);
static void	 tw_cmd_add 		(const Char *name);
static void 	 tw_cmd_cmd		(void);
static void	 tw_cmd_builtin		(void);
static void	 tw_cmd_alias		(void);
static void	 tw_cmd_sort		(void);
static void 	 tw_vptr_start		(struct varent *);


/* tw_str_add():
 *	Add an item to the string list
 */
static Char *
tw_str_add(stringlist_t *sl, size_t len)
{
    Char *ptr;

    if (sl->tlist <= sl->nlist) {
	pintr_disabled++;
	sl->tlist += TW_INCR;
	sl->list = xrealloc(sl->list, sl->tlist * sizeof(Char *));
	disabled_cleanup(&pintr_disabled);
    }
    if (sl->tbuff <= sl->nbuff + len) {
	size_t i;

	ptr = sl->buff;
	pintr_disabled++;
	sl->tbuff += TW_INCR + len;
	sl->buff = xrealloc(sl->buff, sl->tbuff * sizeof(Char));
	/* Re-thread the new pointer list, if changed */
	if (ptr != NULL && ptr != sl->buff) {
	    for (i = 0; i < sl->nlist; i++)
		sl->list[i] = sl->buff + (sl->list[i] - ptr);
	}
	disabled_cleanup(&pintr_disabled);
    }
    ptr = sl->list[sl->nlist++] = &sl->buff[sl->nbuff];
    sl->nbuff += len;
    return ptr;
} /* tw_str_add */


/* tw_str_free():
 *	Free a stringlist
 */
static void
tw_str_free(stringlist_t *sl)
{
    pintr_disabled++;
    if (sl->list) {
	xfree(sl->list);
	sl->list = NULL;
	sl->tlist = sl->nlist = 0;
    }
    if (sl->buff) {
	xfree(sl->buff);
	sl->buff = NULL;
	sl->tbuff = sl->nbuff = 0;
    }
    disabled_cleanup(&pintr_disabled);
} /* end tw_str_free */


static int
tw_dir_next(struct Strbuf *res, DIR *dfd)
{
    struct dirent *dirp;

    if (dfd == NULL)
	return 0;

    if ((dirp = readdir(dfd)) != NULL) {
	Strbuf_append(res, str2short(dirp->d_name));
	return 1;
    }
    return 0;
} /* end tw_dir_next */


/* tw_cmd_add():
 *	Add the name to the command list
 */
static void
tw_cmd_add(const Char *name)
{
    size_t len;

    len = Strlen(name) + 2;
    (void) Strcpy(tw_str_add(&tw_cmd, len), name);
} /* end tw_cmd_add */


/* tw_cmd_free():
 *	Free the command list
 */
void
tw_cmd_free(void)
{
    CLRDIR(tw_dir_fd)
    tw_str_free(&tw_cmd);
    tw_cmd_got = 0;
} /* end tw_cmd_free */

/* tw_cmd_cmd():
 *	Add system commands to the command list
 */
static void
tw_cmd_cmd(void)
{
    DIR *dirp;
    struct dirent *dp;
    Char *dir = NULL, *name;
    Char **pv;
    struct varent *v = adrof(STRpath);
    struct varent *recexec = adrof(STRrecognize_only_executables);
    size_t len;


    if (v == NULL || v->vec == NULL) /* if no path */
	return;

    for (pv = v->vec; *pv; pv++) {
	if (pv[0][0] != '/') {
	    tw_cmd_got |= TW_FL_REL;
	    continue;
	}

	if ((dirp = opendir(short2str(*pv))) == NULL)
	    continue;

	cleanup_push(dirp, opendir_cleanup);
	if (recexec) {
	    dir = Strspl(*pv, STRslash);
	    cleanup_push(dir, xfree);
	}
	while ((dp = readdir(dirp)) != NULL) {
#if defined(_UWIN) || defined(__CYGWIN__)
	    /* Turn foo.{exe,com,bat} into foo since UWIN's readdir returns
	     * the file with the .exe, .com, .bat extension
	     *
	     * Same for Cygwin, but only for .exe and .com extension.
	     */
	    len = strlen(dp->d_name);
	    if (len > 4 && (strcmp(&dp->d_name[len - 4], ".exe") == 0 ||
#ifndef __CYGWIN__
		strcmp(&dp->d_name[len - 4], ".bat") == 0 ||
#endif /* !__CYGWIN__ */
		strcmp(&dp->d_name[len - 4], ".com") == 0))
		dp->d_name[len - 4] = '\0';
#endif /* _UWIN || __CYGWIN__ */
	    /* the call to executable() may make this a bit slow */
	    name = str2short(dp->d_name);
	    if (dp->d_ino == 0 || (recexec && !executable(dir, name, 0)))
		continue;
            len = Strlen(name);
            if (name[0] == '#' ||	/* emacs temp files	*/
		name[0] == '.' ||	/* .files		*/
		name[len - 1] == '~' ||	/* emacs backups	*/
		name[len - 1] == '%')	/* textedit backups	*/
                continue;		/* Ignore!		*/
            tw_cmd_add(name);
	}
	cleanup_until(dirp);
    }
} /* end tw_cmd_cmd */


/* tw_cmd_builtin():
 *	Add builtins to the command list
 */
static void
tw_cmd_builtin(void)
{
    const struct biltins *bptr;

    for (bptr = bfunc; bptr < &bfunc[nbfunc]; bptr++)
	if (bptr->bname)
	    tw_cmd_add(str2short(bptr->bname));
#ifdef WINNT_NATIVE
    for (bptr = nt_bfunc; bptr < &nt_bfunc[nt_nbfunc]; bptr++)
	if (bptr->bname)
	    tw_cmd_add(str2short(bptr->bname));
#endif /* WINNT_NATIVE*/
} /* end tw_cmd_builtin */


/* tw_cmd_alias():
 *	Add aliases to the command list
 */
static void
tw_cmd_alias(void)
{
    struct varent *p;
    struct varent *c;

    p = &aliases;
    for (;;) {
	while (p->v_left)
	    p = p->v_left;
x:
	if (p->v_parent == 0) /* is it the header? */
	    return;
	if (p->v_name)
	    tw_cmd_add(p->v_name);
	if (p->v_right) {
	    p = p->v_right;
	    continue;
	}
	do {
	    c = p;
	    p = p->v_parent;
	} while (p->v_right == c);
	goto x;
    }
} /* end tw_cmd_alias */


/* tw_cmd_sort():
 *	Sort the command list removing duplicate elements
 */
static void
tw_cmd_sort(void)
{
    size_t fwd, i;

    pintr_disabled++;
    /* sort the list. */
    qsort(tw_cmd.list, tw_cmd.nlist, sizeof(Char *), fcompare);

    /* get rid of multiple entries */
    for (i = 0, fwd = 0; i + 1 < tw_cmd.nlist; i++) {
	if (Strcmp(tw_cmd.list[i], tw_cmd.list[i + 1]) == 0) /* garbage */
	    fwd++;		/* increase the forward ref. count */
	else if (fwd) 
	    tw_cmd.list[i - fwd] = tw_cmd.list[i];
    }
    /* Fix fencepost error -- Theodore Ts'o <tytso@athena.mit.edu> */
    if (fwd)
	tw_cmd.list[i - fwd] = tw_cmd.list[i];
    tw_cmd.nlist -= fwd;
    disabled_cleanup(&pintr_disabled);
} /* end tw_cmd_sort */


/* tw_cmd_start():
 *	Get the command list and sort it, if not done yet.
 *	Reset the current pointer to the beginning of the command list
 */
/*ARGSUSED*/
void
tw_cmd_start(DIR *dfd, const Char *pat)
{
    static Char *defpath[] = { STRNULL, 0 };
    USE(pat);
    SETDIR(dfd)
    if ((tw_cmd_got & TW_FL_CMD) == 0) {
	tw_cmd_free();
	tw_cmd_cmd();
	tw_cmd_got |= TW_FL_CMD;
    }
    if ((tw_cmd_got & TW_FL_ALIAS) == 0) {
	tw_cmd_alias();
	tw_cmd_got &= ~TW_FL_SORT;
	tw_cmd_got |= TW_FL_ALIAS;
    }
    if ((tw_cmd_got & TW_FL_BUILTIN) == 0) {
	tw_cmd_builtin();
	tw_cmd_got &= ~TW_FL_SORT;
	tw_cmd_got |= TW_FL_BUILTIN;
    }
    if ((tw_cmd_got & TW_FL_SORT) == 0) {
	tw_cmd_sort();
	tw_cmd_got |= TW_FL_SORT;
    }

    tw_cmd_state.cur = 0;
    CLRDIR(tw_cmd_state.dfd)
    if (tw_cmd_got & TW_FL_REL) {
	struct varent *vp = adrof(STRpath);
	if (vp && vp->vec)
	    tw_cmd_state.pathv = vp->vec;
	else
	    tw_cmd_state.pathv = defpath;
    }
    else 
	tw_cmd_state.pathv = defpath;
} /* tw_cmd_start */


/* tw_cmd_next():
 *	Return the next element in the command list or
 *	Look for commands in the relative path components
 */
int
tw_cmd_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    int ret = 0;
    Char *ptr;

    if (tw_cmd_state.cur < tw_cmd.nlist) {
	*flags = TW_DIR_OK;
	Strbuf_append(res, tw_cmd.list[tw_cmd_state.cur++]);
	return 1;
    }

    /*
     * We need to process relatives in the path.
     */
    while ((tw_cmd_state.dfd == NULL ||
	    (res->len = 0, ret = tw_dir_next(res, tw_cmd_state.dfd)) == 0) &&
	   *tw_cmd_state.pathv != NULL) {

        CLRDIR(tw_cmd_state.dfd)

	while (*tw_cmd_state.pathv && tw_cmd_state.pathv[0][0] == '/')
	    tw_cmd_state.pathv++;
	if ((ptr = *tw_cmd_state.pathv) != 0) {
	    res->len = 0;
	    Strbuf_append(res, ptr);
	    ret = 1;
	    /*
	     * We complete directories only on '.' should that
	     * be changed?
	     */
	    dir->len = 0;
	    if (ptr[0] == '\0' || (ptr[0] == '.' && ptr[1] == '\0')) {
		tw_cmd_state.dfd = opendir(".");
		*flags = TW_DIR_OK | TW_EXEC_CHK;
	    }
	    else {
		Strbuf_append(dir, *tw_cmd_state.pathv);
		Strbuf_append1(dir, '/');
		tw_cmd_state.dfd = opendir(short2str(*tw_cmd_state.pathv));
		*flags = TW_EXEC_CHK;
	    }
	    Strbuf_terminate(dir);
	    tw_cmd_state.pathv++;
	}
    }
    return ret;
} /* end tw_cmd_next */


/* tw_vptr_start():
 *	Find the first variable in the variable list
 */
static void
tw_vptr_start(struct varent *c)
{
    tw_vptr = c;		/* start at beginning of variable list */

    for (;;) {
	while (tw_vptr->v_left)
	    tw_vptr = tw_vptr->v_left;
x:
	if (tw_vptr->v_parent == 0) {	/* is it the header? */
	    tw_vptr = NULL;
	    return;
	}
	if (tw_vptr->v_name)
	    return;		/* found first one */
	if (tw_vptr->v_right) {
	    tw_vptr = tw_vptr->v_right;
	    continue;
	}
	do {
	    c = tw_vptr;
	    tw_vptr = tw_vptr->v_parent;
	} while (tw_vptr->v_right == c);
	goto x;
    }
} /* end tw_shvar_start */


/* tw_shvar_next():
 *	Return the next shell variable
 */
/*ARGSUSED*/
int
tw_shvar_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    struct varent *p;
    struct varent *c;

    USE(flags);
    USE(dir);
    if ((p = tw_vptr) == NULL)
	return 0;		/* just in case */

    Strbuf_append(res, p->v_name); /* we know that this name is here now */

    /* now find the next one */
    for (;;) {
	if (p->v_right) {	/* if we can go right */
	    p = p->v_right;
	    while (p->v_left)
		p = p->v_left;
	}
	else {			/* else go up */
	    do {
		c = p;
		p = p->v_parent;
	    } while (p->v_right == c);
	}
	if (p->v_parent == 0) {	/* is it the header? */
	    tw_vptr = NULL;
	    return 1;
	}
	if (p->v_name) {
	    tw_vptr = p;	/* save state for the next call */
	    return 1;
	}
    }
} /* end tw_shvar_next */


/* tw_envvar_next():
 *	Return the next environment variable
 */
/*ARGSUSED*/
int
tw_envvar_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    const Char *ps;

    USE(flags);
    USE(dir);
    if (tw_env == NULL || *tw_env == NULL)
	return 0;
    for (ps = *tw_env; *ps && *ps != '='; ps++)
	continue;
    Strbuf_appendn(res, *tw_env, ps - *tw_env);
    tw_env++;
    return 1;
} /* end tw_envvar_next */


/* tw_var_start():
 *	Begin the list of the shell and environment variables
 */
/*ARGSUSED*/
void
tw_var_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
    tw_vptr_start(&shvhed);
    tw_env = STR_environ;
} /* end tw_var_start */


/* tw_alias_start():
 *	Begin the list of the shell aliases
 */
/*ARGSUSED*/
void
tw_alias_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
    tw_vptr_start(&aliases);
    tw_env = NULL;
} /* tw_alias_start */


/* tw_complete_start():
 *	Begin the list of completions
 */
/*ARGSUSED*/
void
tw_complete_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
    tw_vptr_start(&completions);
    tw_env = NULL;
} /* end tw_complete_start */


/* tw_var_next():
 *	Return the next shell or environment variable
 */
int
tw_var_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    int ret = 0;

    if (tw_vptr)
	ret = tw_shvar_next(res, dir, flags);
    if (ret == 0 && tw_env)
	ret = tw_envvar_next(res, dir, flags);
    return ret;
} /* end tw_var_next */


/* tw_logname_start():
 *	Initialize lognames to the beginning of the list
 */
/*ARGSUSED*/
void 
tw_logname_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
#ifdef HAVE_GETPWENT
    (void) setpwent();	/* Open passwd file */
#endif
} /* end tw_logname_start */


/* tw_logname_next():
 *	Return the next entry from the passwd file
 */
/*ARGSUSED*/
int
tw_logname_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    struct passwd *pw;

    /*
     * We don't want to get interrupted inside getpwent()
     * because the yellow pages code is not interruptible,
     * and if we call endpwent() immediatetely after
     * (in pintr()) we may be freeing an invalid pointer
     */
    USE(flags);
    USE(dir);
    pintr_disabled++;
#ifdef HAVE_GETPWENT
    pw = getpwent();
#else
    pw = NULL;
#endif
    disabled_cleanup(&pintr_disabled);

    if (pw == NULL) {
#ifdef YPBUGS
	fix_yp_bugs();
#endif
	return 0;
    }
    Strbuf_append(res, str2short(pw->pw_name));
    return 1;
} /* end tw_logname_next */


/* tw_logname_end():
 *	Close the passwd file to finish the logname list
 */
void
tw_logname_end(void)
{
#ifdef YPBUGS
    fix_yp_bugs();
#endif
#ifdef HAVE_GETPWENT
   (void) endpwent();
#endif
} /* end tw_logname_end */


/* tw_grpname_start():
 *	Initialize grpnames to the beginning of the list
 */
/*ARGSUSED*/
void 
tw_grpname_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
#if !defined(_VMS_POSIX) && !defined(_OSD_POSIX) && !defined(WINNT_NATIVE) && !defined (__ANDROID__)
    (void) setgrent();	/* Open group file */
#endif /* !_VMS_POSIX && !_OSD_POSIX && !WINNT_NATIVE */
} /* end tw_grpname_start */


/* tw_grpname_next():
 *	Return the next entry from the group file
 */
/*ARGSUSED*/
int
tw_grpname_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    struct group *gr;

    /*
     * We don't want to get interrupted inside getgrent()
     * because the yellow pages code is not interruptible,
     * and if we call endgrent() immediatetely after
     * (in pintr()) we may be freeing an invalid pointer
     */
    USE(flags);
    USE(dir);
    pintr_disabled++;
#if !defined(_VMS_POSIX) && !defined(_OSD_POSIX) && !defined(WINNT_NATIVE) && !defined(__ANDROID__)
    errno = 0;
    while ((gr = getgrent()) == NULL && errno == EINTR) {
	handle_pending_signals();
	errno = 0;
    }
#else /* _VMS_POSIX || _OSD_POSIX || WINNT_NATIVE */
    gr = NULL;
#endif /* !_VMS_POSIX && !_OSD_POSIX && !WINNT_NATIVE */
    disabled_cleanup(&pintr_disabled);

    if (gr == NULL) {
#ifdef YPBUGS
	fix_yp_bugs();
#endif
	return 0;
    }
    Strbuf_append(res, str2short(gr->gr_name));
    return 1;
} /* end tw_grpname_next */


/* tw_grpname_end():
 *	Close the group file to finish the groupname list
 */
void
tw_grpname_end(void)
{
#ifdef YPBUGS
    fix_yp_bugs();
#endif
#if !defined(_VMS_POSIX) && !defined(_OSD_POSIX) && !defined(WINNT_NATIVE) && !defined (__ANDROID__)
   (void) endgrent();
#endif /* !_VMS_POSIX && !_OSD_POSIX && !WINNT_NATIVE */
} /* end tw_grpname_end */

/* tw_file_start():
 *	Initialize the directory for the file list
 */
/*ARGSUSED*/
void
tw_file_start(DIR *dfd, const Char *pat)
{
    struct varent *vp;
    USE(pat);
    SETDIR(dfd)
    if ((vp = adrof(STRcdpath)) != NULL)
	tw_env = vp->vec;
} /* end tw_file_start */


/* tw_file_next():
 *	Return the next file in the directory 
 */
int
tw_file_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    int ret = tw_dir_next(res, tw_dir_fd);
    if (ret == 0 && (*flags & TW_DIR_OK) != 0) {
	CLRDIR(tw_dir_fd)
	while (tw_env && *tw_env)
	    if ((tw_dir_fd = opendir(short2str(*tw_env))) != NULL)
		break;
	    else
		tw_env++;

	if (tw_dir_fd) {
	    dir->len = 0;
	    Strbuf_append(dir, *tw_env++);
	    Strbuf_append1(dir, '/');
	    Strbuf_terminate(dir);
	    ret = tw_dir_next(res, tw_dir_fd);
	}
    }
    return ret;
} /* end tw_file_next */


/* tw_dir_end():
 *	Clear directory related lists
 */
void
tw_dir_end(void)
{
   CLRDIR(tw_dir_fd)
   CLRDIR(tw_cmd_state.dfd)
} /* end tw_dir_end */


/* tw_item_free():
 *	Free the item list
 */
void
tw_item_free(void)
{
    tw_str_free(&tw_item);
} /* end tw_item_free */


/* tw_item_get(): 
 *	Return the list of items 
 */
Char **
tw_item_get(void)
{
    return tw_item.list;
} /* end tw_item_get */


/* tw_item_add():
 *	Return a new item for a Strbuf_terminate()'d s
 */
void
tw_item_add(const struct Strbuf *s)
{
    Char *p;

    p = tw_str_add(&tw_item, s->len + 1);
    Strcpy(p, s->s);
} /* tw_item_add */


/* tw_item_find():
 *      Find the string if it exists in the item list 
 *	end return it.
 */
Char *
tw_item_find(Char *str)
{
    size_t i;

    if (tw_item.list == NULL || str == NULL)
	return NULL;

    for (i = 0; i < tw_item.nlist; i++)
	if (tw_item.list[i] != NULL && Strcmp(tw_item.list[i], str) == 0)
	    return tw_item.list[i];
    return NULL;
} /* end tw_item_find */


/* tw_vl_start():
 *	Initialize a variable list
 */
void
tw_vl_start(DIR *dfd, const Char *pat)
{
    SETDIR(dfd)
    if ((tw_vptr = adrof(pat)) != NULL) {
	tw_env = tw_vptr->vec;
	tw_vptr = NULL;
    }
    else
	tw_env = NULL;
} /* end tw_vl_start */


/*
 * Initialize a word list
 */
void
tw_wl_start(DIR *dfd, const Char *pat)
{
    SETDIR(dfd);
    tw_word = pat;
} /* end tw_wl_start */


/*
 * Return the next word from the word list
 */
/*ARGSUSED*/
int
tw_wl_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    const Char *p;

    USE(dir);
    USE(flags);
    if (tw_word == NULL || tw_word[0] == '\0')
	return 0;

    while (*tw_word && Isspace(*tw_word)) tw_word++;

    for (p = tw_word; *tw_word && !Isspace(*tw_word); tw_word++)
	continue;
    if (tw_word == p)
	return 0;
    Strbuf_appendn(res, p, tw_word - p);
    if (*tw_word)
	tw_word++;
    return 1;
} /* end tw_wl_next */


/* tw_bind_start():
 *	Begin the list of the shell bindings
 */
/*ARGSUSED*/
void
tw_bind_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
    tw_bind = FuncNames;
} /* end tw_bind_start */


/* tw_bind_next():
 *	Begin the list of the shell bindings
 */
/*ARGSUSED*/
int
tw_bind_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    USE(dir);
    USE(flags);
    if (tw_bind && tw_bind->name) {
	const char *ptr;

	for (ptr = tw_bind->name; *ptr != '\0'; ptr++)
	    Strbuf_append1(res, *ptr);
	tw_bind++;
	return 1;
    }
    return 0;
} /* end tw_bind_next */


/* tw_limit_start():
 *	Begin the list of the shell limitings
 */
/*ARGSUSED*/
void
tw_limit_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
#ifndef HAVENOLIMIT
    tw_limit = limits;
#endif /* ! HAVENOLIMIT */
} /* end tw_limit_start */


/* tw_limit_next():
 *	Begin the list of the shell limitings
 */
/*ARGSUSED*/
int
tw_limit_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    USE(dir);
    USE(flags);
#ifndef HAVENOLIMIT
    if (tw_limit && tw_limit->limname) {
	const char *ptr;

	for (ptr = tw_limit->limname; *ptr != '\0'; ptr++)
	    Strbuf_append1(res, *ptr);
	tw_limit++;
	return 1;
    }
#endif /* ! HAVENOLIMIT */
    return 0;
} /* end tw_limit_next */


/* tw_sig_start():
 *	Begin the list of the shell sigings
 */
/*ARGSUSED*/
void
tw_sig_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
    tw_index = 0;
} /* end tw_sig_start */


/* tw_sig_next():
 *	Begin the list of the shell sigings
 */
/*ARGSUSED*/
int
tw_sig_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    USE(dir);
    USE(flags);
    for (;tw_index < nsig; tw_index++) {
	const char *ptr;

	if (mesg[tw_index].iname == NULL)
	    continue;

	for (ptr = mesg[tw_index].iname; *ptr != '\0'; ptr++)
	    Strbuf_append1(res, *ptr);
	tw_index++;
	return 1;
    }
    return 0;
} /* end tw_sig_next */


/* tw_job_start():
 *	Begin the list of the shell jobings
 */
/*ARGSUSED*/
void
tw_job_start(DIR *dfd, const Char *pat)
{
    USE(pat);
    SETDIR(dfd)
    tw_index = 1;
} /* end tw_job_start */


/* tw_job_next():
 *	Begin the list of the shell jobings
 */
/*ARGSUSED*/
int
tw_job_next(struct Strbuf *res, struct Strbuf *dir, int *flags)
{
    struct process *j;

    USE(dir);
    USE(flags);
    for (;tw_index <= pmaxindex; tw_index++) {
	for (j = proclist.p_next; j != NULL; j = j->p_next)
	    if (j->p_index == tw_index && j->p_procid == j->p_jobid)
		break;
	if (j == NULL) 
	    continue;
	Strbuf_append(res, j->p_command);
	tw_index++;
	return 1;
    }
    return 0;
} /* end tw_job_next */
