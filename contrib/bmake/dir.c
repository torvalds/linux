/*	$NetBSD: dir.c,v 1.73 2018/07/12 18:03:31 christos Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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

/*
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: dir.c,v 1.73 2018/07/12 18:03:31 christos Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)dir.c	8.2 (Berkeley) 1/2/94";
#else
__RCSID("$NetBSD: dir.c,v 1.73 2018/07/12 18:03:31 christos Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * dir.c --
 *	Directory searching using wildcards and/or normal names...
 *	Used both for source wildcarding in the Makefile and for finding
 *	implicit sources.
 *
 * The interface for this module is:
 *	Dir_Init  	    Initialize the module.
 *
 *	Dir_InitCur	    Set the cur Path.
 *
 *	Dir_InitDot	    Set the dot Path.
 *
 *	Dir_End  	    Cleanup the module.
 *
 *	Dir_SetPATH	    Set ${.PATH} to reflect state of dirSearchPath.
 *
 *	Dir_HasWildcards    Returns TRUE if the name given it needs to
 *	    	  	    be wildcard-expanded.
 *
 *	Dir_Expand	    Given a pattern and a path, return a Lst of names
 *	    	  	    which match the pattern on the search path.
 *
 *	Dir_FindFile	    Searches for a file on a given search path.
 *	    	  	    If it exists, the entire path is returned.
 *	    	  	    Otherwise NULL is returned.
 *
 *	Dir_FindHereOrAbove Search for a path in the current directory and
 *			    then all the directories above it in turn until
 *			    the path is found or we reach the root ("/").
 * 
 *	Dir_MTime 	    Return the modification time of a node. The file
 *	    	  	    is searched for along the default search path.
 *	    	  	    The path and mtime fields of the node are filled
 *	    	  	    in.
 *
 *	Dir_AddDir	    Add a directory to a search path.
 *
 *	Dir_MakeFlags	    Given a search path and a command flag, create
 *	    	  	    a string with each of the directories in the path
 *	    	  	    preceded by the command flag and all of them
 *	    	  	    separated by a space.
 *
 *	Dir_Destroy	    Destroy an element of a search path. Frees up all
 *	    	  	    things that can be freed for the element as long
 *	    	  	    as the element is no longer referenced by any other
 *	    	  	    search path.
 *	Dir_ClearPath	    Resets a search path to the empty list.
 *
 * For debugging:
 *	Dir_PrintDirectories	Print stats about the directory cache.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>

#include "make.h"
#include "hash.h"
#include "dir.h"
#include "job.h"

/*
 *	A search path consists of a Lst of Path structures. A Path structure
 *	has in it the name of the directory and a hash table of all the files
 *	in the directory. This is used to cut down on the number of system
 *	calls necessary to find implicit dependents and their like. Since
 *	these searches are made before any actions are taken, we need not
 *	worry about the directory changing due to creation commands. If this
 *	hampers the style of some makefiles, they must be changed.
 *
 *	A list of all previously-read directories is kept in the
 *	openDirectories Lst. This list is checked first before a directory
 *	is opened.
 *
 *	The need for the caching of whole directories is brought about by
 *	the multi-level transformation code in suff.c, which tends to search
 *	for far more files than regular make does. In the initial
 *	implementation, the amount of time spent performing "stat" calls was
 *	truly astronomical. The problem with hashing at the start is,
 *	of course, that pmake doesn't then detect changes to these directories
 *	during the course of the make. Three possibilities suggest themselves:
 *
 *	    1) just use stat to test for a file's existence. As mentioned
 *	       above, this is very inefficient due to the number of checks
 *	       engendered by the multi-level transformation code.
 *	    2) use readdir() and company to search the directories, keeping
 *	       them open between checks. I have tried this and while it
 *	       didn't slow down the process too much, it could severely
 *	       affect the amount of parallelism available as each directory
 *	       open would take another file descriptor out of play for
 *	       handling I/O for another job. Given that it is only recently
 *	       that UNIX OS's have taken to allowing more than 20 or 32
 *	       file descriptors for a process, this doesn't seem acceptable
 *	       to me.
 *	    3) record the mtime of the directory in the Path structure and
 *	       verify the directory hasn't changed since the contents were
 *	       hashed. This will catch the creation or deletion of files,
 *	       but not the updating of files. However, since it is the
 *	       creation and deletion that is the problem, this could be
 *	       a good thing to do. Unfortunately, if the directory (say ".")
 *	       were fairly large and changed fairly frequently, the constant
 *	       rehashing could seriously degrade performance. It might be
 *	       good in such cases to keep track of the number of rehashes
 *	       and if the number goes over a (small) limit, resort to using
 *	       stat in its place.
 *
 *	An additional thing to consider is that pmake is used primarily
 *	to create C programs and until recently pcc-based compilers refused
 *	to allow you to specify where the resulting object file should be
 *	placed. This forced all objects to be created in the current
 *	directory. This isn't meant as a full excuse, just an explanation of
 *	some of the reasons for the caching used here.
 *
 *	One more note: the location of a target's file is only performed
 *	on the downward traversal of the graph and then only for terminal
 *	nodes in the graph. This could be construed as wrong in some cases,
 *	but prevents inadvertent modification of files when the "installed"
 *	directory for a file is provided in the search path.
 *
 *	Another data structure maintained by this module is an mtime
 *	cache used when the searching of cached directories fails to find
 *	a file. In the past, Dir_FindFile would simply perform an access()
 *	call in such a case to determine if the file could be found using
 *	just the name given. When this hit, however, all that was gained
 *	was the knowledge that the file existed. Given that an access() is
 *	essentially a stat() without the copyout() call, and that the same
 *	filesystem overhead would have to be incurred in Dir_MTime, it made
 *	sense to replace the access() with a stat() and record the mtime
 *	in a cache for when Dir_MTime was actually called.
 */

Lst          dirSearchPath;	/* main search path */

static Lst   openDirectories;	/* the list of all open directories */

/*
 * Variables for gathering statistics on the efficiency of the hashing
 * mechanism.
 */
static int    hits,	      /* Found in directory cache */
	      misses,	      /* Sad, but not evil misses */
	      nearmisses,     /* Found under search path */
	      bigmisses;      /* Sought by itself */

static Path    	  *dot;	    /* contents of current directory */
static Path    	  *cur;	    /* contents of current directory, if not dot */
static Path	  *dotLast; /* a fake path entry indicating we need to
			     * look for . last */
static Hash_Table mtimes;   /* Results of doing a last-resort stat in
			     * Dir_FindFile -- if we have to go to the
			     * system to find the file, we might as well
			     * have its mtime on record. XXX: If this is done
			     * way early, there's a chance other rules will
			     * have already updated the file, in which case
			     * we'll update it again. Generally, there won't
			     * be two rules to update a single file, so this
			     * should be ok, but... */

static Hash_Table lmtimes;  /* same as mtimes but for lstat */

static int DirFindName(const void *, const void *);
static int DirMatchFiles(const char *, Path *, Lst);
static void DirExpandCurly(const char *, const char *, Lst, Lst);
static void DirExpandInt(const char *, Lst, Lst);
static int DirPrintWord(void *, void *);
static int DirPrintDir(void *, void *);
static char *DirLookup(Path *, const char *, const char *, Boolean);
static char *DirLookupSubdir(Path *, const char *);
static char *DirFindDot(Boolean, const char *, const char *);
static char *DirLookupAbs(Path *, const char *, const char *);


/*
 * We use stat(2) a lot, cache the results
 * mtime and mode are all we care about.
 */
struct cache_st {
    time_t mtime;
    mode_t  mode;
};

/* minimize changes below */
#define CST_LSTAT 1
#define CST_UPDATE 2

static int
cached_stats(Hash_Table *htp, const char *pathname, struct stat *st, int flags)
{
    Hash_Entry *entry;
    struct cache_st *cst;
    int rc;

    if (!pathname || !pathname[0])
	return -1;

    entry = Hash_FindEntry(htp, pathname);

    if (entry && (flags & CST_UPDATE) == 0) {
	cst = entry->clientPtr;

	memset(st, 0, sizeof(*st));
	st->st_mtime = cst->mtime;
	st->st_mode = cst->mode;
        if (DEBUG(DIR)) {
            fprintf(debug_file, "Using cached time %s for %s\n",
		Targ_FmtTime(st->st_mtime), pathname);
	}
	return 0;
    }

    rc = (flags & CST_LSTAT) ? lstat(pathname, st) : stat(pathname, st);
    if (rc == -1)
	return -1;

    if (st->st_mtime == 0)
	st->st_mtime = 1;      /* avoid confusion with missing file */

    if (!entry)
	entry = Hash_CreateEntry(htp, pathname, NULL);
    if (!entry->clientPtr)
	entry->clientPtr = bmake_malloc(sizeof(*cst));
    cst = entry->clientPtr;
    cst->mtime = st->st_mtime;
    cst->mode = st->st_mode;
    if (DEBUG(DIR)) {
	fprintf(debug_file, "   Caching %s for %s\n",
	    Targ_FmtTime(st->st_mtime), pathname);
    }

    return 0;
}

int
cached_stat(const char *pathname, void *st)
{
    return cached_stats(&mtimes, pathname, st, 0);
}

int
cached_lstat(const char *pathname, void *st)
{
    return cached_stats(&lmtimes, pathname, st, CST_LSTAT);
}

/*-
 *-----------------------------------------------------------------------
 * Dir_Init --
 *	initialize things for this module
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	some directories may be opened.
 *-----------------------------------------------------------------------
 */
void
Dir_Init(const char *cdname)
{
    if (!cdname) {
	dirSearchPath = Lst_Init(FALSE);
	openDirectories = Lst_Init(FALSE);
	Hash_InitTable(&mtimes, 0);
	Hash_InitTable(&lmtimes, 0);
	return;
    }
    Dir_InitCur(cdname);

    dotLast = bmake_malloc(sizeof(Path));
    dotLast->refCount = 1;
    dotLast->hits = 0;
    dotLast->name = bmake_strdup(".DOTLAST");
    Hash_InitTable(&dotLast->files, -1);
}

/*
 * Called by Dir_Init() and whenever .CURDIR is assigned to.
 */
void
Dir_InitCur(const char *cdname)
{
    Path *p;
    
    if (cdname != NULL) {
	/*
	 * Our build directory is not the same as our source directory.
	 * Keep this one around too.
	 */
	if ((p = Dir_AddDir(NULL, cdname))) {
	    p->refCount += 1;
	    if (cur && cur != p) {
		/*
		 * We've been here before, cleanup.
		 */
		cur->refCount -= 1;
		Dir_Destroy(cur);
	    }
	    cur = p;
	}
    }
}

/*-
 *-----------------------------------------------------------------------
 * Dir_InitDot --
 *	(re)initialize "dot" (current/object directory) path hash
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	some directories may be opened.
 *-----------------------------------------------------------------------
 */
void
Dir_InitDot(void)
{
    if (dot != NULL) {
	LstNode ln;

	/* Remove old entry from openDirectories, but do not destroy. */
	ln = Lst_Member(openDirectories, dot);
	(void)Lst_Remove(openDirectories, ln);
    }

    dot = Dir_AddDir(NULL, ".");

    if (dot == NULL) {
	Error("Cannot open `.' (%s)", strerror(errno));
	exit(1);
    }

    /*
     * We always need to have dot around, so we increment its reference count
     * to make sure it's not destroyed.
     */
    dot->refCount += 1;
    Dir_SetPATH();			/* initialize */
}

/*-
 *-----------------------------------------------------------------------
 * Dir_End --
 *	cleanup things for this module
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	none
 *-----------------------------------------------------------------------
 */
void
Dir_End(void)
{
#ifdef CLEANUP
    if (cur) {
	cur->refCount -= 1;
	Dir_Destroy(cur);
    }
    dot->refCount -= 1;
    dotLast->refCount -= 1;
    Dir_Destroy(dotLast);
    Dir_Destroy(dot);
    Dir_ClearPath(dirSearchPath);
    Lst_Destroy(dirSearchPath, NULL);
    Dir_ClearPath(openDirectories);
    Lst_Destroy(openDirectories, NULL);
    Hash_DeleteTable(&mtimes);
#endif
}

/*
 * We want ${.PATH} to indicate the order in which we will actually
 * search, so we rebuild it after any .PATH: target.
 * This is the simplest way to deal with the effect of .DOTLAST.
 */
void
Dir_SetPATH(void)
{
    LstNode       ln;		/* a list element */
    Path *p;
    Boolean	  hasLastDot = FALSE;	/* true we should search dot last */

    Var_Delete(".PATH", VAR_GLOBAL);
    
    if (Lst_Open(dirSearchPath) == SUCCESS) {
	if ((ln = Lst_First(dirSearchPath)) != NULL) {
	    p = (Path *)Lst_Datum(ln);
	    if (p == dotLast) {
		hasLastDot = TRUE;
		Var_Append(".PATH", dotLast->name, VAR_GLOBAL);
	    }
	}

	if (!hasLastDot) {
	    if (dot)
		Var_Append(".PATH", dot->name, VAR_GLOBAL);
	    if (cur)
		Var_Append(".PATH", cur->name, VAR_GLOBAL);
	}

	while ((ln = Lst_Next(dirSearchPath)) != NULL) {
	    p = (Path *)Lst_Datum(ln);
	    if (p == dotLast)
		continue;
	    if (p == dot && hasLastDot)
		continue;
	    Var_Append(".PATH", p->name, VAR_GLOBAL);
	}

	if (hasLastDot) {
	    if (dot)
		Var_Append(".PATH", dot->name, VAR_GLOBAL);
	    if (cur)
		Var_Append(".PATH", cur->name, VAR_GLOBAL);
	}
	Lst_Close(dirSearchPath);
    }
}

/*-
 *-----------------------------------------------------------------------
 * DirFindName --
 *	See if the Path structure describes the same directory as the
 *	given one by comparing their names. Called from Dir_AddDir via
 *	Lst_Find when searching the list of open directories.
 *
 * Input:
 *	p		Current name
 *	dname		Desired name
 *
 * Results:
 *	0 if it is the same. Non-zero otherwise
 *
 * Side Effects:
 *	None
 *-----------------------------------------------------------------------
 */
static int
DirFindName(const void *p, const void *dname)
{
    return (strcmp(((const Path *)p)->name, dname));
}

/*-
 *-----------------------------------------------------------------------
 * Dir_HasWildcards  --
 *	see if the given name has any wildcard characters in it
 *	be careful not to expand unmatching brackets or braces.
 *	XXX: This code is not 100% correct. ([^]] fails etc.) 
 *	I really don't think that make(1) should be expanding
 *	patterns, because then you have to set a mechanism for
 *	escaping the expansion!
 *
 * Input:
 *	name		name to check
 *
 * Results:
 *	returns TRUE if the word should be expanded, FALSE otherwise
 *
 * Side Effects:
 *	none
 *-----------------------------------------------------------------------
 */
Boolean
Dir_HasWildcards(char *name)
{
    char *cp;
    int wild = 0, brace = 0, bracket = 0;

    for (cp = name; *cp; cp++) {
	switch(*cp) {
	case '{':
		brace++;
		wild = 1;
		break;
	case '}':
		brace--;
		break;
	case '[':
		bracket++;
		wild = 1;
		break;
	case ']':
		bracket--;
		break;
	case '?':
	case '*':
		wild = 1;
		break;
	default:
		break;
	}
    }
    return wild && bracket == 0 && brace == 0;
}

/*-
 *-----------------------------------------------------------------------
 * DirMatchFiles --
 * 	Given a pattern and a Path structure, see if any files
 *	match the pattern and add their names to the 'expansions' list if
 *	any do. This is incomplete -- it doesn't take care of patterns like
 *	src / *src / *.c properly (just *.c on any of the directories), but it
 *	will do for now.
 *
 * Input:
 *	pattern		Pattern to look for
 *	p		Directory to search
 *	expansion	Place to store the results
 *
 * Results:
 *	Always returns 0
 *
 * Side Effects:
 *	File names are added to the expansions lst. The directory will be
 *	fully hashed when this is done.
 *-----------------------------------------------------------------------
 */
static int
DirMatchFiles(const char *pattern, Path *p, Lst expansions)
{
    Hash_Search	  search;   	/* Index into the directory's table */
    Hash_Entry	  *entry;   	/* Current entry in the table */
    Boolean 	  isDot;    	/* TRUE if the directory being searched is . */

    isDot = (*p->name == '.' && p->name[1] == '\0');

    for (entry = Hash_EnumFirst(&p->files, &search);
	 entry != NULL;
	 entry = Hash_EnumNext(&search))
    {
	/*
	 * See if the file matches the given pattern. Note we follow the UNIX
	 * convention that dot files will only be found if the pattern
	 * begins with a dot (note also that as a side effect of the hashing
	 * scheme, .* won't match . or .. since they aren't hashed).
	 */
	if (Str_Match(entry->name, pattern) &&
	    ((entry->name[0] != '.') ||
	     (pattern[0] == '.')))
	{
	    (void)Lst_AtEnd(expansions,
			    (isDot ? bmake_strdup(entry->name) :
			     str_concat(p->name, entry->name,
					STR_ADDSLASH)));
	}
    }
    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * DirExpandCurly --
 *	Expand curly braces like the C shell. Does this recursively.
 *	Note the special case: if after the piece of the curly brace is
 *	done there are no wildcard characters in the result, the result is
 *	placed on the list WITHOUT CHECKING FOR ITS EXISTENCE.
 *
 * Input:
 *	word		Entire word to expand
 *	brace		First curly brace in it
 *	path		Search path to use
 *	expansions	Place to store the expansions
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The given list is filled with the expansions...
 *
 *-----------------------------------------------------------------------
 */
static void
DirExpandCurly(const char *word, const char *brace, Lst path, Lst expansions)
{
    const char   *end;	    	/* Character after the closing brace */
    const char   *cp;	    	/* Current position in brace clause */
    const char   *start;   	/* Start of current piece of brace clause */
    int	    	  bracelevel;	/* Number of braces we've seen. If we see a
				 * right brace when this is 0, we've hit the
				 * end of the clause. */
    char    	 *file;    	/* Current expansion */
    int	    	  otherLen; 	/* The length of the other pieces of the
				 * expansion (chars before and after the
				 * clause in 'word') */
    char    	 *cp2;	    	/* Pointer for checking for wildcards in
				 * expansion before calling Dir_Expand */

    start = brace+1;

    /*
     * Find the end of the brace clause first, being wary of nested brace
     * clauses.
     */
    for (end = start, bracelevel = 0; *end != '\0'; end++) {
	if (*end == '{') {
	    bracelevel++;
	} else if ((*end == '}') && (bracelevel-- == 0)) {
	    break;
	}
    }
    if (*end == '\0') {
	Error("Unterminated {} clause \"%s\"", start);
	return;
    } else {
	end++;
    }
    otherLen = brace - word + strlen(end);

    for (cp = start; cp < end; cp++) {
	/*
	 * Find the end of this piece of the clause.
	 */
	bracelevel = 0;
	while (*cp != ',') {
	    if (*cp == '{') {
		bracelevel++;
	    } else if ((*cp == '}') && (bracelevel-- <= 0)) {
		break;
	    }
	    cp++;
	}
	/*
	 * Allocate room for the combination and install the three pieces.
	 */
	file = bmake_malloc(otherLen + cp - start + 1);
	if (brace != word) {
	    strncpy(file, word, brace-word);
	}
	if (cp != start) {
	    strncpy(&file[brace-word], start, cp-start);
	}
	strcpy(&file[(brace-word)+(cp-start)], end);

	/*
	 * See if the result has any wildcards in it. If we find one, call
	 * Dir_Expand right away, telling it to place the result on our list
	 * of expansions.
	 */
	for (cp2 = file; *cp2 != '\0'; cp2++) {
	    switch(*cp2) {
	    case '*':
	    case '?':
	    case '{':
	    case '[':
		Dir_Expand(file, path, expansions);
		goto next;
	    }
	}
	if (*cp2 == '\0') {
	    /*
	     * Hit the end w/o finding any wildcards, so stick the expansion
	     * on the end of the list.
	     */
	    (void)Lst_AtEnd(expansions, file);
	} else {
	next:
	    free(file);
	}
	start = cp+1;
    }
}


/*-
 *-----------------------------------------------------------------------
 * DirExpandInt --
 *	Internal expand routine. Passes through the directories in the
 *	path one by one, calling DirMatchFiles for each. NOTE: This still
 *	doesn't handle patterns in directories...
 *
 * Input:
 *	word		Word to expand
 *	path		Path on which to look
 *	expansions	Place to store the result
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Things are added to the expansions list.
 *
 *-----------------------------------------------------------------------
 */
static void
DirExpandInt(const char *word, Lst path, Lst expansions)
{
    LstNode 	  ln;	    	/* Current node */
    Path	  *p;	    	/* Directory in the node */

    if (Lst_Open(path) == SUCCESS) {
	while ((ln = Lst_Next(path)) != NULL) {
	    p = (Path *)Lst_Datum(ln);
	    DirMatchFiles(word, p, expansions);
	}
	Lst_Close(path);
    }
}

/*-
 *-----------------------------------------------------------------------
 * DirPrintWord --
 *	Print a word in the list of expansions. Callback for Dir_Expand
 *	when DEBUG(DIR), via Lst_ForEach.
 *
 * Results:
 *	=== 0
 *
 * Side Effects:
 *	The passed word is printed, followed by a space.
 *
 *-----------------------------------------------------------------------
 */
static int
DirPrintWord(void *word, void *dummy MAKE_ATTR_UNUSED)
{
    fprintf(debug_file, "%s ", (char *)word);

    return 0;
}

/*-
 *-----------------------------------------------------------------------
 * Dir_Expand  --
 *	Expand the given word into a list of words by globbing it looking
 *	in the directories on the given search path.
 *
 * Input:
 *	word		the word to expand
 *	path		the list of directories in which to find the
 *			resulting files
 *	expansions	the list on which to place the results
 *
 * Results:
 *	A list of words consisting of the files which exist along the search
 *	path matching the given pattern.
 *
 * Side Effects:
 *	Directories may be opened. Who knows?
 *-----------------------------------------------------------------------
 */
void
Dir_Expand(const char *word, Lst path, Lst expansions)
{
    const char    	  *cp;

    if (DEBUG(DIR)) {
	fprintf(debug_file, "Expanding \"%s\"... ", word);
    }

    cp = strchr(word, '{');
    if (cp) {
	DirExpandCurly(word, cp, path, expansions);
    } else {
	cp = strchr(word, '/');
	if (cp) {
	    /*
	     * The thing has a directory component -- find the first wildcard
	     * in the string.
	     */
	    for (cp = word; *cp; cp++) {
		if (*cp == '?' || *cp == '[' || *cp == '*' || *cp == '{') {
		    break;
		}
	    }
	    if (*cp == '{') {
		/*
		 * This one will be fun.
		 */
		DirExpandCurly(word, cp, path, expansions);
		return;
	    } else if (*cp != '\0') {
		/*
		 * Back up to the start of the component
		 */
		char  *dirpath;

		while (cp > word && *cp != '/') {
		    cp--;
		}
		if (cp != word) {
		    char sc;
		    /*
		     * If the glob isn't in the first component, try and find
		     * all the components up to the one with a wildcard.
		     */
		    sc = cp[1];
		    ((char *)UNCONST(cp))[1] = '\0';
		    dirpath = Dir_FindFile(word, path);
		    ((char *)UNCONST(cp))[1] = sc;
		    /*
		     * dirpath is null if can't find the leading component
		     * XXX: Dir_FindFile won't find internal components.
		     * i.e. if the path contains ../Etc/Object and we're
		     * looking for Etc, it won't be found. Ah well.
		     * Probably not important.
		     */
		    if (dirpath != NULL) {
			char *dp = &dirpath[strlen(dirpath) - 1];
			if (*dp == '/')
			    *dp = '\0';
			path = Lst_Init(FALSE);
			(void)Dir_AddDir(path, dirpath);
			DirExpandInt(cp+1, path, expansions);
			Lst_Destroy(path, NULL);
		    }
		} else {
		    /*
		     * Start the search from the local directory
		     */
		    DirExpandInt(word, path, expansions);
		}
	    } else {
		/*
		 * Return the file -- this should never happen.
		 */
		DirExpandInt(word, path, expansions);
	    }
	} else {
	    /*
	     * First the files in dot
	     */
	    DirMatchFiles(word, dot, expansions);

	    /*
	     * Then the files in every other directory on the path.
	     */
	    DirExpandInt(word, path, expansions);
	}
    }
    if (DEBUG(DIR)) {
	Lst_ForEach(expansions, DirPrintWord, NULL);
	fprintf(debug_file, "\n");
    }
}

/*-
 *-----------------------------------------------------------------------
 * DirLookup  --
 *	Find if the file with the given name exists in the given path.
 *
 * Results:
 *	The path to the file or NULL. This path is guaranteed to be in a
 *	different part of memory than name and so may be safely free'd.
 *
 * Side Effects:
 *	None.
 *-----------------------------------------------------------------------
 */
static char *
DirLookup(Path *p, const char *name MAKE_ATTR_UNUSED, const char *cp, 
          Boolean hasSlash MAKE_ATTR_UNUSED)
{
    char *file;		/* the current filename to check */

    if (DEBUG(DIR)) {
	fprintf(debug_file, "   %s ...\n", p->name);
    }

    if (Hash_FindEntry(&p->files, cp) == NULL)
	return NULL;

    file = str_concat(p->name, cp, STR_ADDSLASH);
    if (DEBUG(DIR)) {
	fprintf(debug_file, "   returning %s\n", file);
    }
    p->hits += 1;
    hits += 1;
    return file;
}


/*-
 *-----------------------------------------------------------------------
 * DirLookupSubdir  --
 *	Find if the file with the given name exists in the given path.
 *
 * Results:
 *	The path to the file or NULL. This path is guaranteed to be in a
 *	different part of memory than name and so may be safely free'd.
 *
 * Side Effects:
 *	If the file is found, it is added in the modification times hash
 *	table.
 *-----------------------------------------------------------------------
 */
static char *
DirLookupSubdir(Path *p, const char *name)
{
    struct stat	  stb;		/* Buffer for stat, if necessary */
    char 	 *file;		/* the current filename to check */

    if (p != dot) {
	file = str_concat(p->name, name, STR_ADDSLASH);
    } else {
	/*
	 * Checking in dot -- DON'T put a leading ./ on the thing.
	 */
	file = bmake_strdup(name);
    }

    if (DEBUG(DIR)) {
	fprintf(debug_file, "checking %s ...\n", file);
    }

    if (cached_stat(file, &stb) == 0) {
	nearmisses += 1;
	return (file);
    }
    free(file);
    return NULL;
}

/*-
 *-----------------------------------------------------------------------
 * DirLookupAbs  --
 *	Find if the file with the given name exists in the given path.
 *
 * Results:
 *	The path to the file, the empty string or NULL. If the file is
 *	the empty string, the search should be terminated.
 *	This path is guaranteed to be in a different part of memory
 *	than name and so may be safely free'd.
 *
 * Side Effects:
 *	None.
 *-----------------------------------------------------------------------
 */
static char *
DirLookupAbs(Path *p, const char *name, const char *cp)
{
	char *p1;		/* pointer into p->name */
	const char *p2;		/* pointer into name */

	if (DEBUG(DIR)) {
		fprintf(debug_file, "   %s ...\n", p->name);
	}

	/*
	 * If the file has a leading path component and that component
	 * exactly matches the entire name of the current search
	 * directory, we can attempt another cache lookup. And if we don't
	 * have a hit, we can safely assume the file does not exist at all.
	 */
	for (p1 = p->name, p2 = name; *p1 && *p1 == *p2; p1++, p2++) {
		continue;
	}
	if (*p1 != '\0' || p2 != cp - 1) {
		return NULL;
	}

	if (Hash_FindEntry(&p->files, cp) == NULL) {
		if (DEBUG(DIR)) {
			fprintf(debug_file, "   must be here but isn't -- returning\n");
		}
		/* Return empty string: terminates search */
		return bmake_strdup("");
	}

	p->hits += 1;
	hits += 1;
	if (DEBUG(DIR)) {
		fprintf(debug_file, "   returning %s\n", name);
	}
	return (bmake_strdup(name));
}

/*-
 *-----------------------------------------------------------------------
 * DirFindDot  --
 *	Find the file given on "." or curdir
 *
 * Results:
 *	The path to the file or NULL. This path is guaranteed to be in a
 *	different part of memory than name and so may be safely free'd.
 *
 * Side Effects:
 *	Hit counts change
 *-----------------------------------------------------------------------
 */
static char *
DirFindDot(Boolean hasSlash MAKE_ATTR_UNUSED, const char *name, const char *cp)
{

	if (Hash_FindEntry(&dot->files, cp) != NULL) {
	    if (DEBUG(DIR)) {
		fprintf(debug_file, "   in '.'\n");
	    }
	    hits += 1;
	    dot->hits += 1;
	    return (bmake_strdup(name));
	}
	if (cur &&
	    Hash_FindEntry(&cur->files, cp) != NULL) {
	    if (DEBUG(DIR)) {
		fprintf(debug_file, "   in ${.CURDIR} = %s\n", cur->name);
	    }
	    hits += 1;
	    cur->hits += 1;
	    return str_concat(cur->name, cp, STR_ADDSLASH);
	}

	return NULL;
}

/*-
 *-----------------------------------------------------------------------
 * Dir_FindFile  --
 *	Find the file with the given name along the given search path.
 *
 * Input:
 *	name		the file to find
 *	path		the Lst of directories to search
 *
 * Results:
 *	The path to the file or NULL. This path is guaranteed to be in a
 *	different part of memory than name and so may be safely free'd.
 *
 * Side Effects:
 *	If the file is found in a directory which is not on the path
 *	already (either 'name' is absolute or it is a relative path
 *	[ dir1/.../dirn/file ] which exists below one of the directories
 *	already on the search path), its directory is added to the end
 *	of the path on the assumption that there will be more files in
 *	that directory later on. Sometimes this is true. Sometimes not.
 *-----------------------------------------------------------------------
 */
char *
Dir_FindFile(const char *name, Lst path)
{
    LstNode       ln;			/* a list element */
    char	  *file;		/* the current filename to check */
    Path	  *p;			/* current path member */
    const char	  *cp;			/* Terminal name of file */
    Boolean	  hasLastDot = FALSE;	/* true we should search dot last */
    Boolean	  hasSlash;		/* true if 'name' contains a / */
    struct stat	  stb;			/* Buffer for stat, if necessary */
    const char   *trailing_dot = ".";

    /*
     * Find the final component of the name and note whether it has a
     * slash in it (the name, I mean)
     */
    cp = strrchr(name, '/');
    if (cp) {
	hasSlash = TRUE;
	cp += 1;
    } else {
	hasSlash = FALSE;
	cp = name;
    }

    if (DEBUG(DIR)) {
	fprintf(debug_file, "Searching for %s ...", name);
    }

    if (Lst_Open(path) == FAILURE) {
	if (DEBUG(DIR)) {
	    fprintf(debug_file, "couldn't open path, file not found\n");
	}
	misses += 1;
	return NULL;
    }

    if ((ln = Lst_First(path)) != NULL) {
	p = (Path *)Lst_Datum(ln);
	if (p == dotLast) {
	    hasLastDot = TRUE;
            if (DEBUG(DIR))
		fprintf(debug_file, "[dot last]...");
	}
    }
    if (DEBUG(DIR)) {
	fprintf(debug_file, "\n");
    }

    /*
     * If there's no leading directory components or if the leading
     * directory component is exactly `./', consult the cached contents
     * of each of the directories on the search path.
     */
    if (!hasSlash || (cp - name == 2 && *name == '.')) {
	    /*
	     * We look through all the directories on the path seeking one which
	     * contains the final component of the given name.  If such a beast
	     * is found, we concatenate the directory name and the final
	     * component and return the resulting string. If we don't find any
	     * such thing, we go on to phase two...
	     * 
	     * No matter what, we always look for the file in the current
	     * directory before anywhere else (unless we found the magic
	     * DOTLAST path, in which case we search it last) and we *do not*
	     * add the ./ to it if it exists.
	     * This is so there are no conflicts between what the user
	     * specifies (fish.c) and what pmake finds (./fish.c).
	     */
	    if (!hasLastDot &&
			(file = DirFindDot(hasSlash, name, cp)) != NULL) {
		    Lst_Close(path);
		    return file;
	    }

	    while ((ln = Lst_Next(path)) != NULL) {
		p = (Path *)Lst_Datum(ln);
		if (p == dotLast)
		    continue;
		if ((file = DirLookup(p, name, cp, hasSlash)) != NULL) {
		    Lst_Close(path);
		    return file;
		}
	    }

	    if (hasLastDot &&
			(file = DirFindDot(hasSlash, name, cp)) != NULL) {
		    Lst_Close(path);
		    return file;
	    }
    }
    Lst_Close(path);

    /*
     * We didn't find the file on any directory in the search path.
     * If the name doesn't contain a slash, that means it doesn't exist.
     * If it *does* contain a slash, however, there is still hope: it
     * could be in a subdirectory of one of the members of the search
     * path. (eg. /usr/include and sys/types.h. The above search would
     * fail to turn up types.h in /usr/include, but it *is* in
     * /usr/include/sys/types.h).
     * [ This no longer applies: If we find such a beast, we assume there
     * will be more (what else can we assume?) and add all but the last
     * component of the resulting name onto the search path (at the
     * end).]
     * This phase is only performed if the file is *not* absolute.
     */
    if (!hasSlash) {
	if (DEBUG(DIR)) {
	    fprintf(debug_file, "   failed.\n");
	}
	misses += 1;
	return NULL;
    }

    if (*cp == '\0') {
	/* we were given a trailing "/" */
	cp = trailing_dot;
    }

    if (name[0] != '/') {
	Boolean	checkedDot = FALSE;

	if (DEBUG(DIR)) {
	    fprintf(debug_file, "   Trying subdirectories...\n");
	}

	if (!hasLastDot) {
		if (dot) {
			checkedDot = TRUE;
			if ((file = DirLookupSubdir(dot, name)) != NULL)
				return file;
		}
		if (cur && (file = DirLookupSubdir(cur, name)) != NULL)
			return file;
	}

	(void)Lst_Open(path);
	while ((ln = Lst_Next(path)) != NULL) {
	    p = (Path *)Lst_Datum(ln);
	    if (p == dotLast)
		continue;
	    if (p == dot) {
		    if (checkedDot)
			    continue;
		checkedDot = TRUE;
	    }
	    if ((file = DirLookupSubdir(p, name)) != NULL) {
		Lst_Close(path);
		return file;
	    }
	}
	Lst_Close(path);

	if (hasLastDot) {
		if (dot && !checkedDot) {
			checkedDot = TRUE;
			if ((file = DirLookupSubdir(dot, name)) != NULL)
				return file;
		}
		if (cur && (file = DirLookupSubdir(cur, name)) != NULL)
			return file;
	}

	if (checkedDot) {
	    /*
	     * Already checked by the given name, since . was in the path,
	     * so no point in proceeding...
	     */
	    if (DEBUG(DIR)) {
		fprintf(debug_file, "   Checked . already, returning NULL\n");
	    }
	    return NULL;
	}

    } else { /* name[0] == '/' */

	/*
	 * For absolute names, compare directory path prefix against the
	 * the directory path of each member on the search path for an exact
	 * match. If we have an exact match on any member of the search path,
	 * use the cached contents of that member to lookup the final file
	 * component. If that lookup fails we can safely assume that the
	 * file does not exist at all.  This is signified by DirLookupAbs()
	 * returning an empty string.
	 */
	if (DEBUG(DIR)) {
	    fprintf(debug_file, "   Trying exact path matches...\n");
	}

	if (!hasLastDot && cur && ((file = DirLookupAbs(cur, name, cp))
		!= NULL)) {
	    if (file[0] == '\0') {
		free(file);
		return NULL;
	    }
	    return file;
	}

	(void)Lst_Open(path);
	while ((ln = Lst_Next(path)) != NULL) {
	    p = (Path *)Lst_Datum(ln);
	    if (p == dotLast)
		continue;
	    if ((file = DirLookupAbs(p, name, cp)) != NULL) {
		Lst_Close(path);
		if (file[0] == '\0') {
		    free(file);
		    return NULL;
		}
		return file;
	    }
	}
	Lst_Close(path);

	if (hasLastDot && cur && ((file = DirLookupAbs(cur, name, cp))
		!= NULL)) {
	    if (file[0] == '\0') {
		free(file);
		return NULL;
	    }
	    return file;
	}
    }

    /*
     * Didn't find it that way, either. Sigh. Phase 3. Add its directory
     * onto the search path in any case, just in case, then look for the
     * thing in the hash table. If we find it, grand. We return a new
     * copy of the name. Otherwise we sadly return a NULL pointer. Sigh.
     * Note that if the directory holding the file doesn't exist, this will
     * do an extra search of the final directory on the path. Unless something
     * weird happens, this search won't succeed and life will be groovy.
     *
     * Sigh. We cannot add the directory onto the search path because
     * of this amusing case:
     * $(INSTALLDIR)/$(FILE): $(FILE)
     *
     * $(FILE) exists in $(INSTALLDIR) but not in the current one.
     * When searching for $(FILE), we will find it in $(INSTALLDIR)
     * b/c we added it here. This is not good...
     */
#ifdef notdef
    if (cp == traling_dot) {
	cp = strrchr(name, '/');
	cp += 1;
    }
    cp[-1] = '\0';
    (void)Dir_AddDir(path, name);
    cp[-1] = '/';

    bigmisses += 1;
    ln = Lst_Last(path);
    if (ln == NULL) {
	return NULL;
    } else {
	p = (Path *)Lst_Datum(ln);
    }

    if (Hash_FindEntry(&p->files, cp) != NULL) {
	return (bmake_strdup(name));
    } else {
	return NULL;
    }
#else /* !notdef */
    if (DEBUG(DIR)) {
	fprintf(debug_file, "   Looking for \"%s\" ...\n", name);
    }

    bigmisses += 1;
    if (cached_stat(name, &stb) == 0) {
	return (bmake_strdup(name));
    }

    if (DEBUG(DIR)) {
	fprintf(debug_file, "   failed. Returning NULL\n");
    }
    return NULL;
#endif /* notdef */
}


/*-
 *-----------------------------------------------------------------------
 * Dir_FindHereOrAbove  --
 *	search for a path starting at a given directory and then working 
 *	our way up towards the root.
 *
 * Input:
 *	here		starting directory
 *	search_path	the path we are looking for
 *	result		the result of a successful search is placed here
 *	rlen		the length of the result buffer 
 *			(typically MAXPATHLEN + 1)
 *
 * Results:
 *	0 on failure, 1 on success [in which case the found path is put
 *	in the result buffer].
 *
 * Side Effects:
 *-----------------------------------------------------------------------
 */
int 
Dir_FindHereOrAbove(char *here, char *search_path, char *result, int rlen) {

	struct stat st;
	char dirbase[MAXPATHLEN + 1], *db_end;
        char try[MAXPATHLEN + 1], *try_end;

	/* copy out our starting point */
	snprintf(dirbase, sizeof(dirbase), "%s", here);
	db_end = dirbase + strlen(dirbase);

	/* loop until we determine a result */
	while (1) {

		/* try and stat(2) it ... */
		snprintf(try, sizeof(try), "%s/%s", dirbase, search_path);
		if (cached_stat(try, &st) != -1) {
			/*
			 * success!  if we found a file, chop off
			 * the filename so we return a directory.
			 */
			if ((st.st_mode & S_IFMT) != S_IFDIR) {
				try_end = try + strlen(try);
				while (try_end > try && *try_end != '/')
					try_end--;
				if (try_end > try) 
					*try_end = 0;	/* chop! */
			}

			/*
			 * done!
			 */
			snprintf(result, rlen, "%s", try);
			return(1);
		}

		/* 
		 * nope, we didn't find it.  if we used up dirbase we've
		 * reached the root and failed.
		 */
		if (db_end == dirbase)
			break;		/* failed! */

		/*
		 * truncate dirbase from the end to move up a dir
		 */
		while (db_end > dirbase && *db_end != '/')
			db_end--;
		*db_end = 0;		/* chop! */

	} /* while (1) */

	/*
	 * we failed... 
	 */
	return(0);
}

/*-
 *-----------------------------------------------------------------------
 * Dir_MTime  --
 *	Find the modification time of the file described by gn along the
 *	search path dirSearchPath.
 *
 * Input:
 *	gn		the file whose modification time is desired
 *
 * Results:
 *	The modification time or 0 if it doesn't exist
 *
 * Side Effects:
 *	The modification time is placed in the node's mtime slot.
 *	If the node didn't have a path entry before, and Dir_FindFile
 *	found one for it, the full name is placed in the path slot.
 *-----------------------------------------------------------------------
 */
int
Dir_MTime(GNode *gn, Boolean recheck)
{
    char          *fullName;  /* the full pathname of name */
    struct stat	  stb;	      /* buffer for finding the mod time */

    if (gn->type & OP_ARCHV) {
	return Arch_MTime(gn);
    } else if (gn->type & OP_PHONY) {
	gn->mtime = 0;
	return 0;
    } else if (gn->path == NULL) {
	if (gn->type & OP_NOPATH)
	    fullName = NULL;
	else {
	    fullName = Dir_FindFile(gn->name, Suff_FindPath(gn));
	    if (fullName == NULL && gn->flags & FROM_DEPEND &&
		!Lst_IsEmpty(gn->iParents)) {
		char *cp;

		cp = strrchr(gn->name, '/');
		if (cp) {
		    /*
		     * This is an implied source, and it may have moved,
		     * see if we can find it via the current .PATH
		     */
		    cp++;
			
		    fullName = Dir_FindFile(cp, Suff_FindPath(gn));
		    if (fullName) {
			/*
			 * Put the found file in gn->path
			 * so that we give that to the compiler.
			 */
			gn->path = bmake_strdup(fullName);
			if (!Job_RunTarget(".STALE", gn->fname))
			    fprintf(stdout,
				"%s: %s, %d: ignoring stale %s for %s, "
				"found %s\n", progname, gn->fname, gn->lineno,
				makeDependfile, gn->name, fullName);
		    }
		}
	    }
	    if (DEBUG(DIR))
		fprintf(debug_file, "Found '%s' as '%s'\n",
			gn->name, fullName ? fullName : "(not found)" );
	}
    } else {
	fullName = gn->path;
    }

    if (fullName == NULL) {
	fullName = bmake_strdup(gn->name);
    }

    if (cached_stats(&mtimes, fullName, &stb, recheck ? CST_UPDATE : 0) < 0) {
	if (gn->type & OP_MEMBER) {
	    if (fullName != gn->path)
		free(fullName);
	    return Arch_MemMTime(gn);
	} else {
	    stb.st_mtime = 0;
	}
    }

    if (fullName && gn->path == NULL) {
	gn->path = fullName;
    }

    gn->mtime = stb.st_mtime;
    return (gn->mtime);
}

/*-
 *-----------------------------------------------------------------------
 * Dir_AddDir --
 *	Add the given name to the end of the given path. The order of
 *	the arguments is backwards so ParseDoDependency can do a
 *	Lst_ForEach of its list of paths...
 *
 * Input:
 *	path		the path to which the directory should be
 *			added
 *	name		the name of the directory to add
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	A structure is added to the list and the directory is
 *	read and hashed.
 *-----------------------------------------------------------------------
 */
Path *
Dir_AddDir(Lst path, const char *name)
{
    LstNode       ln = NULL; /* node in case Path structure is found */
    Path	  *p = NULL;  /* pointer to new Path structure */
    DIR     	  *d;	      /* for reading directory */
    struct dirent *dp;	      /* entry in directory */

    if (strcmp(name, ".DOTLAST") == 0) {
	ln = Lst_Find(path, name, DirFindName);
	if (ln != NULL)
	    return (Path *)Lst_Datum(ln);
	else {
	    dotLast->refCount += 1;
	    (void)Lst_AtFront(path, dotLast);
	}
    }

    if (path)
	ln = Lst_Find(openDirectories, name, DirFindName);
    if (ln != NULL) {
	p = (Path *)Lst_Datum(ln);
	if (path && Lst_Member(path, p) == NULL) {
	    p->refCount += 1;
	    (void)Lst_AtEnd(path, p);
	}
    } else {
	if (DEBUG(DIR)) {
	    fprintf(debug_file, "Caching %s ...", name);
	}

	if ((d = opendir(name)) != NULL) {
	    p = bmake_malloc(sizeof(Path));
	    p->name = bmake_strdup(name);
	    p->hits = 0;
	    p->refCount = 1;
	    Hash_InitTable(&p->files, -1);

	    while ((dp = readdir(d)) != NULL) {
#if defined(sun) && defined(d_ino) /* d_ino is a sunos4 #define for d_fileno */
		/*
		 * The sun directory library doesn't check for a 0 inode
		 * (0-inode slots just take up space), so we have to do
		 * it ourselves.
		 */
		if (dp->d_fileno == 0) {
		    continue;
		}
#endif /* sun && d_ino */
		(void)Hash_CreateEntry(&p->files, dp->d_name, NULL);
	    }
	    (void)closedir(d);
	    (void)Lst_AtEnd(openDirectories, p);
	    if (path != NULL)
		(void)Lst_AtEnd(path, p);
	}
	if (DEBUG(DIR)) {
	    fprintf(debug_file, "done\n");
	}
    }
    return p;
}

/*-
 *-----------------------------------------------------------------------
 * Dir_CopyDir --
 *	Callback function for duplicating a search path via Lst_Duplicate.
 *	Ups the reference count for the directory.
 *
 * Results:
 *	Returns the Path it was given.
 *
 * Side Effects:
 *	The refCount of the path is incremented.
 *
 *-----------------------------------------------------------------------
 */
void *
Dir_CopyDir(void *p)
{
    ((Path *)p)->refCount += 1;

    return (p);
}

/*-
 *-----------------------------------------------------------------------
 * Dir_MakeFlags --
 *	Make a string by taking all the directories in the given search
 *	path and preceding them by the given flag. Used by the suffix
 *	module to create variables for compilers based on suffix search
 *	paths.
 *
 * Input:
 *	flag		flag which should precede each directory
 *	path		list of directories
 *
 * Results:
 *	The string mentioned above. Note that there is no space between
 *	the given flag and each directory. The empty string is returned if
 *	Things don't go well.
 *
 * Side Effects:
 *	None
 *-----------------------------------------------------------------------
 */
char *
Dir_MakeFlags(const char *flag, Lst path)
{
    char	  *str;	  /* the string which will be returned */
    char	  *s1, *s2;/* the current directory preceded by 'flag' */
    LstNode	  ln;	  /* the node of the current directory */
    Path	  *p;	  /* the structure describing the current directory */

    str = bmake_strdup("");

    if (Lst_Open(path) == SUCCESS) {
	while ((ln = Lst_Next(path)) != NULL) {
	    p = (Path *)Lst_Datum(ln);
	    s2 = str_concat(flag, p->name, 0);
	    str = str_concat(s1 = str, s2, STR_ADDSPACE);
	    free(s1);
	    free(s2);
	}
	Lst_Close(path);
    }

    return (str);
}

/*-
 *-----------------------------------------------------------------------
 * Dir_Destroy --
 *	Nuke a directory descriptor, if possible. Callback procedure
 *	for the suffixes module when destroying a search path.
 *
 * Input:
 *	pp		The directory descriptor to nuke
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	If no other path references this directory (refCount == 0),
 *	the Path and all its data are freed.
 *
 *-----------------------------------------------------------------------
 */
void
Dir_Destroy(void *pp)
{
    Path    	  *p = (Path *)pp;
    p->refCount -= 1;

    if (p->refCount == 0) {
	LstNode	ln;

	ln = Lst_Member(openDirectories, p);
	(void)Lst_Remove(openDirectories, ln);

	Hash_DeleteTable(&p->files);
	free(p->name);
	free(p);
    }
}

/*-
 *-----------------------------------------------------------------------
 * Dir_ClearPath --
 *	Clear out all elements of the given search path. This is different
 *	from destroying the list, notice.
 *
 * Input:
 *	path		Path to clear
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The path is set to the empty list.
 *
 *-----------------------------------------------------------------------
 */
void
Dir_ClearPath(Lst path)
{
    Path    *p;
    while (!Lst_IsEmpty(path)) {
	p = (Path *)Lst_DeQueue(path);
	Dir_Destroy(p);
    }
}


/*-
 *-----------------------------------------------------------------------
 * Dir_Concat --
 *	Concatenate two paths, adding the second to the end of the first.
 *	Makes sure to avoid duplicates.
 *
 * Input:
 *	path1		Dest
 *	path2		Source
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Reference counts for added dirs are upped.
 *
 *-----------------------------------------------------------------------
 */
void
Dir_Concat(Lst path1, Lst path2)
{
    LstNode ln;
    Path    *p;

    for (ln = Lst_First(path2); ln != NULL; ln = Lst_Succ(ln)) {
	p = (Path *)Lst_Datum(ln);
	if (Lst_Member(path1, p) == NULL) {
	    p->refCount += 1;
	    (void)Lst_AtEnd(path1, p);
	}
    }
}

/********** DEBUG INFO **********/
void
Dir_PrintDirectories(void)
{
    LstNode	ln;
    Path	*p;

    fprintf(debug_file, "#*** Directory Cache:\n");
    fprintf(debug_file, "# Stats: %d hits %d misses %d near misses %d losers (%d%%)\n",
	      hits, misses, nearmisses, bigmisses,
	      (hits+bigmisses+nearmisses ?
	       hits * 100 / (hits + bigmisses + nearmisses) : 0));
    fprintf(debug_file, "# %-20s referenced\thits\n", "directory");
    if (Lst_Open(openDirectories) == SUCCESS) {
	while ((ln = Lst_Next(openDirectories)) != NULL) {
	    p = (Path *)Lst_Datum(ln);
	    fprintf(debug_file, "# %-20s %10d\t%4d\n", p->name, p->refCount, p->hits);
	}
	Lst_Close(openDirectories);
    }
}

static int
DirPrintDir(void *p, void *dummy MAKE_ATTR_UNUSED)
{
    fprintf(debug_file, "%s ", ((Path *)p)->name);
    return 0;
}

void
Dir_PrintPath(Lst path)
{
    Lst_ForEach(path, DirPrintDir, NULL);
}
