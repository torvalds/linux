/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992 Keith Muller.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)pat_rep.c	8.2 (Berkeley) 4/18/94";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef NET2_REGEX
#include <regexp.h>
#else
#include <regex.h>
#endif
#include "pax.h"
#include "pat_rep.h"
#include "extern.h"

/*
 * routines to handle pattern matching, name modification (regular expression
 * substitution and interactive renames), and destination name modification for
 * copy (-rw). Both file name and link names are adjusted as required in these
 * routines.
 */

#define MAXSUBEXP	10		/* max subexpressions, DO NOT CHANGE */
static PATTERN *pathead = NULL;		/* file pattern match list head */
static PATTERN *pattail = NULL;		/* file pattern match list tail */
static REPLACE *rephead = NULL;		/* replacement string list head */
static REPLACE *reptail = NULL;		/* replacement string list tail */

static int rep_name(char *, int *, int);
static int tty_rename(ARCHD *);
static int fix_path(char *, int *, char *, int);
static int fn_match(char *, char *, char **);
static char * range_match(char *, int);
#ifdef NET2_REGEX
static int resub(regexp *, char *, char *, char *);
#else
static int resub(regex_t *, regmatch_t *, char *, char *, char *, char *);
#endif

/*
 * rep_add()
 *	parses the -s replacement string; compiles the regular expression
 *	and stores the compiled value and it's replacement string together in
 *	replacement string list. Input to this function is of the form:
 *		/old/new/pg
 *	The first char in the string specifies the delimiter used by this
 *	replacement string. "Old" is a regular expression in "ed" format which
 *	is compiled by regcomp() and is applied to filenames. "new" is the
 *	substitution string; p and g are options flags for printing and global
 *	replacement (over the single filename)
 * Return:
 *	0 if a proper replacement string and regular expression was added to
 *	the list of replacement patterns; -1 otherwise.
 */

int
rep_add(char *str)
{
	char *pt1;
	char *pt2;
	REPLACE *rep;
#	ifndef NET2_REGEX
	int res;
	char rebuf[BUFSIZ];
#	endif

	/*
	 * throw out the bad parameters
	 */
	if ((str == NULL) || (*str == '\0')) {
		paxwarn(1, "Empty replacement string");
		return(-1);
	}

	/*
	 * first character in the string specifies what the delimiter is for
	 * this expression
	 */
	if ((pt1 = strchr(str+1, *str)) == NULL) {
		paxwarn(1, "Invalid replacement string %s", str);
		return(-1);
	}

	/*
	 * allocate space for the node that handles this replacement pattern
	 * and split out the regular expression and try to compile it
	 */
	if ((rep = (REPLACE *)malloc(sizeof(REPLACE))) == NULL) {
		paxwarn(1, "Unable to allocate memory for replacement string");
		return(-1);
	}

	*pt1 = '\0';
#	ifdef NET2_REGEX
	if ((rep->rcmp = regcomp(str+1)) == NULL) {
#	else
	if ((res = regcomp(&(rep->rcmp), str+1, 0)) != 0) {
		regerror(res, &(rep->rcmp), rebuf, sizeof(rebuf));
		paxwarn(1, "%s while compiling regular expression %s", rebuf, str);
#	endif
		free(rep);
		return(-1);
	}

	/*
	 * put the delimiter back in case we need an error message and
	 * locate the delimiter at the end of the replacement string
	 * we then point the node at the new substitution string
	 */
	*pt1++ = *str;
	if ((pt2 = strchr(pt1, *str)) == NULL) {
#		ifdef NET2_REGEX
		free(rep->rcmp);
#		else
		regfree(&rep->rcmp);
#		endif
		free(rep);
		paxwarn(1, "Invalid replacement string %s", str);
		return(-1);
	}

	*pt2 = '\0';
	rep->nstr = pt1;
	pt1 = pt2++;
	rep->flgs = 0;

	/*
	 * set the options if any
	 */
	while (*pt2 != '\0') {
		switch(*pt2) {
		case 'g':
		case 'G':
			rep->flgs  |= GLOB;
			break;
		case 'p':
		case 'P':
			rep->flgs  |= PRNT;
			break;
		default:
#			ifdef NET2_REGEX
			free(rep->rcmp);
#			else
			regfree(&rep->rcmp);
#			endif
			free(rep);
			*pt1 = *str;
			paxwarn(1, "Invalid replacement string option %s", str);
			return(-1);
		}
		++pt2;
	}

	/*
	 * all done, link it in at the end
	 */
	rep->fow = NULL;
	if (rephead == NULL) {
		reptail = rephead = rep;
		return(0);
	}
	reptail->fow = rep;
	reptail = rep;
	return(0);
}

/*
 * pat_add()
 *	add a pattern match to the pattern match list. Pattern matches are used
 *	to select which archive members are extracted. (They appear as
 *	arguments to pax in the list and read modes). If no patterns are
 *	supplied to pax, all members in the archive will be selected (and the
 *	pattern match list is empty).
 * Return:
 *	0 if the pattern was added to the list, -1 otherwise
 */

int
pat_add(char *str, char *chdnam)
{
	PATTERN *pt;

	/*
	 * throw out the junk
	 */
	if ((str == NULL) || (*str == '\0')) {
		paxwarn(1, "Empty pattern string");
		return(-1);
	}

	/*
	 * allocate space for the pattern and store the pattern. the pattern is
	 * part of argv so do not bother to copy it, just point at it. Add the
	 * node to the end of the pattern list
	 */
	if ((pt = (PATTERN *)malloc(sizeof(PATTERN))) == NULL) {
		paxwarn(1, "Unable to allocate memory for pattern string");
		return(-1);
	}

	pt->pstr = str;
	pt->pend = NULL;
	pt->plen = strlen(str);
	pt->fow = NULL;
	pt->flgs = 0;
	pt->chdname = chdnam;

	if (pathead == NULL) {
		pattail = pathead = pt;
		return(0);
	}
	pattail->fow = pt;
	pattail = pt;
	return(0);
}

/*
 * pat_chk()
 *	complain if any the user supplied pattern did not result in a match to
 *	a selected archive member.
 */

void
pat_chk(void)
{
	PATTERN *pt;
	int wban = 0;

	/*
	 * walk down the list checking the flags to make sure MTCH was set,
	 * if not complain
	 */
	for (pt = pathead; pt != NULL; pt = pt->fow) {
		if (pt->flgs & MTCH)
			continue;
		if (!wban) {
			paxwarn(1, "WARNING! These patterns were not matched:");
			++wban;
		}
		(void)fprintf(stderr, "%s\n", pt->pstr);
	}
}

/*
 * pat_sel()
 *	the archive member which matches a pattern was selected. Mark the
 *	pattern as having selected an archive member. arcn->pat points at the
 *	pattern that was matched. arcn->pat is set in pat_match()
 *
 *	NOTE: When the -c option is used, we are called when there was no match
 *	by pat_match() (that means we did match before the inverted sense of
 *	the logic). Now this seems really strange at first, but with -c  we
 *	need to keep track of those patterns that cause an archive member to NOT
 *	be selected (it found an archive member with a specified pattern)
 * Return:
 *	0 if the pattern pointed at by arcn->pat was tagged as creating a
 *	match, -1 otherwise.
 */

int
pat_sel(ARCHD *arcn)
{
	PATTERN *pt;
	PATTERN **ppt;
	int len;

	/*
	 * if no patterns just return
	 */
	if ((pathead == NULL) || ((pt = arcn->pat) == NULL))
		return(0);

	/*
	 * when we are NOT limited to a single match per pattern mark the
	 * pattern and return
	 */
	if (!nflag) {
		pt->flgs |= MTCH;
		return(0);
	}

	/*
	 * we reach this point only when we allow a single selected match per
	 * pattern, if the pattern matches a directory and we do not have -d
	 * (dflag) we are done with this pattern. We may also be handed a file
	 * in the subtree of a directory. in that case when we are operating
	 * with -d, this pattern was already selected and we are done
	 */
	if (pt->flgs & DIR_MTCH)
		return(0);

	if (!dflag && ((pt->pend != NULL) || (arcn->type == PAX_DIR))) {
		/*
		 * ok we matched a directory and we are allowing
		 * subtree matches but because of the -n only its children will
		 * match. This is tagged as a DIR_MTCH type.
		 * WATCH IT, the code assumes that pt->pend points
		 * into arcn->name and arcn->name has not been modified.
		 * If not we will have a big mess. Yup this is another kludge
		 */

		/*
		 * if this was a prefix match, remove trailing part of path
		 * so we can copy it. Future matches will be exact prefix match
		 */
		if (pt->pend != NULL)
			*pt->pend = '\0';

		if ((pt->pstr = strdup(arcn->name)) == NULL) {
			paxwarn(1, "Pattern select out of memory");
			if (pt->pend != NULL)
				*pt->pend = '/';
			pt->pend = NULL;
			return(-1);
		}

		/*
		 * put the trailing / back in the source string
		 */
		if (pt->pend != NULL) {
			*pt->pend = '/';
			pt->pend = NULL;
		}
		pt->plen = strlen(pt->pstr);

		/*
		 * strip off any trailing /, this should really never happen
		 */
		len = pt->plen - 1;
		if (*(pt->pstr + len) == '/') {
			*(pt->pstr + len) = '\0';
			pt->plen = len;
		}
		pt->flgs = DIR_MTCH | MTCH;
		arcn->pat = pt;
		return(0);
	}

	/*
	 * we are then done with this pattern, so we delete it from the list
	 * because it can never be used for another match.
	 * Seems kind of strange to do for a -c, but the pax spec is really
	 * vague on the interaction of -c -n and -d. We assume that when -c
	 * and the pattern rejects a member (i.e. it matched it) it is done.
	 * In effect we place the order of the flags as having -c last.
	 */
	pt = pathead;
	ppt = &pathead;
	while ((pt != NULL) && (pt != arcn->pat)) {
		ppt = &(pt->fow);
		pt = pt->fow;
	}

	if (pt == NULL) {
		/*
		 * should never happen....
		 */
		paxwarn(1, "Pattern list inconsistent");
		return(-1);
	}
	*ppt = pt->fow;
	free(pt);
	arcn->pat = NULL;
	return(0);
}

/*
 * pat_match()
 *	see if this archive member matches any supplied pattern, if a match
 *	is found, arcn->pat is set to point at the potential pattern. Later if
 *	this archive member is "selected" we process and mark the pattern as
 *	one which matched a selected archive member (see pat_sel())
 * Return:
 *	0 if this archive member should be processed, 1 if it should be
 *	skipped and -1 if we are done with all patterns (and pax should quit
 *	looking for more members)
 */

int
pat_match(ARCHD *arcn)
{
	PATTERN *pt;

	arcn->pat = NULL;

	/*
	 * if there are no more patterns and we have -n (and not -c) we are
	 * done. otherwise with no patterns to match, matches all
	 */
	if (pathead == NULL) {
		if (nflag && !cflag)
			return(-1);
		return(0);
	}

	/*
	 * have to search down the list one at a time looking for a match.
	 */
	pt = pathead;
	while (pt != NULL) {
		/*
		 * check for a file name match unless we have DIR_MTCH set in
		 * this pattern then we want a prefix match
		 */
		if (pt->flgs & DIR_MTCH) {
			/*
			 * this pattern was matched before to a directory
			 * as we must have -n set for this (but not -d). We can
			 * only match CHILDREN of that directory so we must use
			 * an exact prefix match (no wildcards).
			 */
			if ((arcn->name[pt->plen] == '/') &&
			    (strncmp(pt->pstr, arcn->name, pt->plen) == 0))
				break;
		} else if (fn_match(pt->pstr, arcn->name, &pt->pend) == 0)
			break;
		pt = pt->fow;
	}

	/*
	 * return the result, remember that cflag (-c) inverts the sense of a
	 * match
	 */
	if (pt == NULL)
		return(cflag ? 0 : 1);

	/*
	 * We had a match, now when we invert the sense (-c) we reject this
	 * member. However we have to tag the pattern a being successful, (in a
	 * match, not in selecting an archive member) so we call pat_sel() here.
	 */
	arcn->pat = pt;
	if (!cflag)
		return(0);

	if (pat_sel(arcn) < 0)
		return(-1);
	arcn->pat = NULL;
	return(1);
}

/*
 * fn_match()
 * Return:
 *	0 if this archive member should be processed, 1 if it should be
 *	skipped and -1 if we are done with all patterns (and pax should quit
 *	looking for more members)
 *	Note: *pend may be changed to show where the prefix ends.
 */

static int
fn_match(char *pattern, char *string, char **pend)
{
	char c;
	char test;

	*pend = NULL;
	for (;;) {
		switch (c = *pattern++) {
		case '\0':
			/*
			 * Ok we found an exact match
			 */
			if (*string == '\0')
				return(0);

			/*
			 * Check if it is a prefix match
			 */
			if ((dflag == 1) || (*string != '/'))
				return(-1);

			/*
			 * It is a prefix match, remember where the trailing
			 * / is located
			 */
			*pend = string;
			return(0);
		case '?':
			if ((test = *string++) == '\0')
				return (-1);
			break;
		case '*':
			c = *pattern;
			/*
			 * Collapse multiple *'s.
			 */
			while (c == '*')
				c = *++pattern;

			/*
			 * Optimized hack for pattern with a * at the end
			 */
			if (c == '\0')
				return (0);

			/*
			 * General case, use recursion.
			 */
			while ((test = *string) != '\0') {
				if (!fn_match(pattern, string, pend))
					return (0);
				++string;
			}
			return (-1);
		case '[':
			/*
			 * range match
			 */
			if (((test = *string++) == '\0') ||
			    ((pattern = range_match(pattern, test)) == NULL))
				return (-1);
			break;
		case '\\':
		default:
			if (c != *string++)
				return (-1);
			break;
		}
	}
	/* NOTREACHED */
}

static char *
range_match(char *pattern, int test)
{
	char c;
	char c2;
	int negate;
	int ok = 0;

	if ((negate = (*pattern == '!')) != 0)
		++pattern;

	while ((c = *pattern++) != ']') {
		/*
		 * Illegal pattern
		 */
		if (c == '\0')
			return (NULL);

		if ((*pattern == '-') && ((c2 = pattern[1]) != '\0') &&
		    (c2 != ']')) {
			if ((c <= test) && (test <= c2))
				ok = 1;
			pattern += 2;
		} else if (c == test)
			ok = 1;
	}
	return (ok == negate ? NULL : pattern);
}

/*
 * mod_name()
 *	modify a selected file name. first attempt to apply replacement string
 *	expressions, then apply interactive file rename. We apply replacement
 *	string expressions to both filenames and file links (if we didn't the
 *	links would point to the wrong place, and we could never be able to
 *	move an archive that has a file link in it). When we rename files
 *	interactively, we store that mapping (old name to user input name) so
 *	if we spot any file links to the old file name in the future, we will
 *	know exactly how to fix the file link.
 * Return:
 *	0 continue to  process file, 1 skip this file, -1 pax is finished
 */

int
mod_name(ARCHD *arcn)
{
	int res = 0;

	/*
	 * Strip off leading '/' if appropriate.
	 * Currently, this option is only set for the tar format.
	 */
	if (rmleadslash && arcn->name[0] == '/') {
		if (arcn->name[1] == '\0') {
			arcn->name[0] = '.';
		} else {
			(void)memmove(arcn->name, &arcn->name[1],
			    strlen(arcn->name));
			arcn->nlen--;
		}
		if (rmleadslash < 2) {
			rmleadslash = 2;
			paxwarn(0, "Removing leading / from absolute path names in the archive");
		}
	}
	if (rmleadslash && arcn->ln_name[0] == '/' &&
	    (arcn->type == PAX_HLK || arcn->type == PAX_HRG)) {
		if (arcn->ln_name[1] == '\0') {
			arcn->ln_name[0] = '.';
		} else {
			(void)memmove(arcn->ln_name, &arcn->ln_name[1],
			    strlen(arcn->ln_name));
			arcn->ln_nlen--;
		}
		if (rmleadslash < 2) {
			rmleadslash = 2;
			paxwarn(0, "Removing leading / from absolute path names in the archive");
		}
	}

	/*
	 * IMPORTANT: We have a problem. what do we do with symlinks?
	 * Modifying a hard link name makes sense, as we know the file it
	 * points at should have been seen already in the archive (and if it
	 * wasn't seen because of a read error or a bad archive, we lose
	 * anyway). But there are no such requirements for symlinks. On one
	 * hand the symlink that refers to a file in the archive will have to
	 * be modified to so it will still work at its new location in the
	 * file system. On the other hand a symlink that points elsewhere (and
	 * should continue to do so) should not be modified. There is clearly
	 * no perfect solution here. So we handle them like hardlinks. Clearly
	 * a replacement made by the interactive rename mapping is very likely
	 * to be correct since it applies to a single file and is an exact
	 * match. The regular expression replacements are a little harder to
	 * justify though. We claim that the symlink name is only likely
	 * to be replaced when it points within the file tree being moved and
	 * in that case it should be modified. what we really need to do is to
	 * call an oracle here. :)
	 */
	if (rephead != NULL) {
		/*
		 * we have replacement strings, modify the name and the link
		 * name if any.
		 */
		if ((res = rep_name(arcn->name, &(arcn->nlen), 1)) != 0)
			return(res);

		if (((arcn->type == PAX_SLK) || (arcn->type == PAX_HLK) ||
		    (arcn->type == PAX_HRG)) &&
		    ((res = rep_name(arcn->ln_name, &(arcn->ln_nlen), 0)) != 0))
			return(res);
	}

	if (iflag) {
		/*
		 * perform interactive file rename, then map the link if any
		 */
		if ((res = tty_rename(arcn)) != 0)
			return(res);
		if ((arcn->type == PAX_SLK) || (arcn->type == PAX_HLK) ||
		    (arcn->type == PAX_HRG))
			sub_name(arcn->ln_name, &(arcn->ln_nlen), sizeof(arcn->ln_name));
	}
	return(res);
}

/*
 * tty_rename()
 *	Prompt the user for a replacement file name. A "." keeps the old name,
 *	a empty line skips the file, and an EOF on reading the tty, will cause
 *	pax to stop processing and exit. Otherwise the file name input, replaces
 *	the old one.
 * Return:
 *	0 process this file, 1 skip this file, -1 we need to exit pax
 */

static int
tty_rename(ARCHD *arcn)
{
	char tmpname[PAXPATHLEN+2];
	int res;

	/*
	 * prompt user for the replacement name for a file, keep trying until
	 * we get some reasonable input. Archives may have more than one file
	 * on them with the same name (from updates etc). We print verbose info
	 * on the file so the user knows what is up.
	 */
	tty_prnt("\nATTENTION: %s interactive file rename operation.\n", argv0);

	for (;;) {
		ls_tty(arcn);
		tty_prnt("Input new name, or a \".\" to keep the old name, ");
		tty_prnt("or a \"return\" to skip this file.\n");
		tty_prnt("Input > ");
		if (tty_read(tmpname, sizeof(tmpname)) < 0)
			return(-1);
		if (strcmp(tmpname, "..") == 0) {
			tty_prnt("Try again, illegal file name: ..\n");
			continue;
		}
		if (strlen(tmpname) > PAXPATHLEN) {
			tty_prnt("Try again, file name too long\n");
			continue;
		}
		break;
	}

	/*
	 * empty file name, skips this file. a "." leaves it alone
	 */
	if (tmpname[0] == '\0') {
		tty_prnt("Skipping file.\n");
		return(1);
	}
	if ((tmpname[0] == '.') && (tmpname[1] == '\0')) {
		tty_prnt("Processing continues, name unchanged.\n");
		return(0);
	}

	/*
	 * ok the name changed. We may run into links that point at this
	 * file later. we have to remember where the user sent the file
	 * in order to repair any links.
	 */
	tty_prnt("Processing continues, name changed to: %s\n", tmpname);
	res = add_name(arcn->name, arcn->nlen, tmpname);
	arcn->nlen = l_strncpy(arcn->name, tmpname, sizeof(arcn->name) - 1);
	arcn->name[arcn->nlen] = '\0';
	if (res < 0)
		return(-1);
	return(0);
}

/*
 * set_dest()
 *	fix up the file name and the link name (if any) so this file will land
 *	in the destination directory (used during copy() -rw).
 * Return:
 *	0 if ok, -1 if failure (name too long)
 */

int
set_dest(ARCHD *arcn, char *dest_dir, int dir_len)
{
	if (fix_path(arcn->name, &(arcn->nlen), dest_dir, dir_len) < 0)
		return(-1);

	/*
	 * It is really hard to deal with symlinks here, we cannot be sure
	 * if the name they point was moved (or will be moved). It is best to
	 * leave them alone.
	 */
	if ((arcn->type != PAX_HLK) && (arcn->type != PAX_HRG))
		return(0);

	if (fix_path(arcn->ln_name, &(arcn->ln_nlen), dest_dir, dir_len) < 0)
		return(-1);
	return(0);
}

/*
 * fix_path
 *	concatenate dir_name and or_name and store the result in or_name (if
 *	it fits). This is one ugly function.
 * Return:
 *	0 if ok, -1 if the final name is too long
 */

static int
fix_path( char *or_name, int *or_len, char *dir_name, int dir_len)
{
	char *src;
	char *dest;
	char *start;
	int len;

	/*
	 * we shift the or_name to the right enough to tack in the dir_name
	 * at the front. We make sure we have enough space for it all before
	 * we start. since dest always ends in a slash, we skip of or_name
	 * if it also starts with one.
	 */
	start = or_name;
	src = start + *or_len;
	dest = src + dir_len;
	if (*start == '/') {
		++start;
		--dest;
	}
	if ((len = dest - or_name) > PAXPATHLEN) {
		paxwarn(1, "File name %s/%s, too long", dir_name, start);
		return(-1);
	}
	*or_len = len;

	/*
	 * enough space, shift
	 */
	while (src >= start)
		*dest-- = *src--;
	src = dir_name + dir_len - 1;

	/*
	 * splice in the destination directory name
	 */
	while (src >= dir_name)
		*dest-- = *src--;

	*(or_name + len) = '\0';
	return(0);
}

/*
 * rep_name()
 *	walk down the list of replacement strings applying each one in order.
 *	when we find one with a successful substitution, we modify the name
 *	as specified. if required, we print the results. if the resulting name
 *	is empty, we will skip this archive member. We use the regexp(3)
 *	routines (regexp() ought to win a prize as having the most cryptic
 *	library function manual page).
 *	--Parameters--
 *	name is the file name we are going to apply the regular expressions to
 *	(and may be modified)
 *	nlen is the length of this name (and is modified to hold the length of
 *	the final string).
 *	prnt is a flag that says whether to print the final result.
 * Return:
 *	0 if substitution was successful, 1 if we are to skip the file (the name
 *	ended up empty)
 */

static int
rep_name(char *name, int *nlen, int prnt)
{
	REPLACE *pt;
	char *inpt;
	char *outpt;
	char *endpt;
	char *rpt;
	int found = 0;
	int res;
#	ifndef NET2_REGEX
	regmatch_t pm[MAXSUBEXP];
#	endif
	char nname[PAXPATHLEN+1];	/* final result of all replacements */
	char buf1[PAXPATHLEN+1];	/* where we work on the name */

	/*
	 * copy the name into buf1, where we will work on it. We need to keep
	 * the orig string around so we can print out the result of the final
	 * replacement. We build up the final result in nname. inpt points at
	 * the string we apply the regular expression to. prnt is used to
	 * suppress printing when we handle replacements on the link field
	 * (the user already saw that substitution go by)
	 */
	pt = rephead;
	(void)strlcpy(buf1, name, sizeof(buf1));
	inpt = buf1;
	outpt = nname;
	endpt = outpt + PAXPATHLEN;

	/*
	 * try each replacement string in order
	 */
	while (pt != NULL) {
		do {
			/*
			 * check for a successful substitution, if not go to
			 * the next pattern, or cleanup if we were global
			 */
#			ifdef NET2_REGEX
			if (regexec(pt->rcmp, inpt) == 0)
#			else
			if (regexec(&(pt->rcmp), inpt, MAXSUBEXP, pm, 0) != 0)
#			endif
				break;

			/*
			 * ok we found one. We have three parts, the prefix
			 * which did not match, the section that did and the
			 * tail (that also did not match). Copy the prefix to
			 * the final output buffer (watching to make sure we
			 * do not create a string too long).
			 */
			found = 1;
#			ifdef NET2_REGEX
			rpt = pt->rcmp->startp[0];
#			else
			rpt = inpt + pm[0].rm_so;
#			endif

			while ((inpt < rpt) && (outpt < endpt))
				*outpt++ = *inpt++;
			if (outpt == endpt)
				break;

			/*
			 * for the second part (which matched the regular
			 * expression) apply the substitution using the
			 * replacement string and place it the prefix in the
			 * final output. If we have problems, skip it.
			 */
#			ifdef NET2_REGEX
			if ((res = resub(pt->rcmp,pt->nstr,outpt,endpt)) < 0) {
#			else
			if ((res = resub(&(pt->rcmp),pm,inpt,pt->nstr,outpt,endpt))
			    < 0) {
#			endif
				if (prnt)
					paxwarn(1, "Replacement name error %s",
					    name);
				return(1);
			}
			outpt += res;

			/*
			 * we set up to look again starting at the first
			 * character in the tail (of the input string right
			 * after the last character matched by the regular
			 * expression (inpt always points at the first char in
			 * the string to process). If we are not doing a global
			 * substitution, we will use inpt to copy the tail to
			 * the final result. Make sure we do not overrun the
			 * output buffer
			 */
#			ifdef NET2_REGEX
			inpt = pt->rcmp->endp[0];
#			else
			inpt += pm[0].rm_eo - pm[0].rm_so;
#			endif

			if ((outpt == endpt) || (*inpt == '\0'))
				break;

			/*
			 * if the user wants global we keep trying to
			 * substitute until it fails, then we are done.
			 */
		} while (pt->flgs & GLOB);

		if (found)
			break;

		/*
		 * a successful substitution did NOT occur, try the next one
		 */
		pt = pt->fow;
	}

	if (found) {
		/*
		 * we had a substitution, copy the last tail piece (if there is
		 * room) to the final result
		 */
		while ((outpt < endpt) && (*inpt != '\0'))
			*outpt++ = *inpt++;

		*outpt = '\0';
		if ((outpt == endpt) && (*inpt != '\0')) {
			if (prnt)
				paxwarn(1,"Replacement name too long %s >> %s",
				    name, nname);
			return(1);
		}

		/*
		 * inform the user of the result if wanted
		 */
		if (prnt && (pt->flgs & PRNT)) {
			if (*nname == '\0')
				(void)fprintf(stderr,"%s >> <empty string>\n",
				    name);
			else
				(void)fprintf(stderr,"%s >> %s\n", name, nname);
		}

		/*
		 * if empty inform the caller this file is to be skipped
		 * otherwise copy the new name over the orig name and return
		 */
		if (*nname == '\0')
			return(1);
		*nlen = l_strncpy(name, nname, PAXPATHLEN + 1);
		name[PAXPATHLEN] = '\0';
	}
	return(0);
}

#ifdef NET2_REGEX
/*
 * resub()
 *	apply the replacement to the matched expression. expand out the old
 * 	style ed(1) subexpression expansion.
 * Return:
 *	-1 if error, or the number of characters added to the destination.
 */

static int
resub(regexp *prog, char *src, char *dest, char *destend)
{
	char *spt;
	char *dpt;
	char c;
	int no;
	int len;

	spt = src;
	dpt = dest;
	while ((dpt < destend) && ((c = *spt++) != '\0')) {
		if (c == '&')
			no = 0;
		else if ((c == '\\') && (*spt >= '0') && (*spt <= '9'))
			no = *spt++ - '0';
		else {
 			if ((c == '\\') && ((*spt == '\\') || (*spt == '&')))
 				c = *spt++;
 			*dpt++ = c;
			continue;
		}
 		if ((prog->startp[no] == NULL) || (prog->endp[no] == NULL) ||
		    ((len = prog->endp[no] - prog->startp[no]) <= 0))
			continue;

		/*
		 * copy the subexpression to the destination.
		 * fail if we run out of space or the match string is damaged
		 */
		if (len > (destend - dpt))
			len = destend - dpt;
		if (l_strncpy(dpt, prog->startp[no], len) != len)
			return(-1);
		dpt += len;
	}
	return(dpt - dest);
}

#else

/*
 * resub()
 *	apply the replacement to the matched expression. expand out the old
 * 	style ed(1) subexpression expansion.
 * Return:
 *	-1 if error, or the number of characters added to the destination.
 */

static int
resub(regex_t *rp, regmatch_t *pm, char *orig, char *src, char *dest,
	char *destend)
{
	char *spt;
	char *dpt;
	char c;
	regmatch_t *pmpt;
	int len;
	int subexcnt;

	spt =  src;
	dpt = dest;
	subexcnt = rp->re_nsub;
	while ((dpt < destend) && ((c = *spt++) != '\0')) {
		/*
		 * see if we just have an ordinary replacement character
		 * or we refer to a subexpression.
		 */
		if (c == '&') {
			pmpt = pm;
		} else if ((c == '\\') && (*spt >= '0') && (*spt <= '9')) {
			/*
			 * make sure there is a subexpression as specified
			 */
			if ((len = *spt++ - '0') > subexcnt)
				return(-1);
			pmpt = pm + len;
		} else {
 			/*
			 * Ordinary character, just copy it
			 */
 			if ((c == '\\') && ((*spt == '\\') || (*spt == '&')))
 				c = *spt++;
 			*dpt++ = c;
			continue;
		}

		/*
		 * continue if the subexpression is bogus
		 */
		if ((pmpt->rm_so < 0) || (pmpt->rm_eo < 0) ||
		    ((len = pmpt->rm_eo - pmpt->rm_so) <= 0))
			continue;

		/*
		 * copy the subexpression to the destination.
		 * fail if we run out of space or the match string is damaged
		 */
		if (len > (destend - dpt))
			len = destend - dpt;
		if (l_strncpy(dpt, orig + pmpt->rm_so, len) != len)
			return(-1);
		dpt += len;
	}
	return(dpt - dest);
}
#endif
