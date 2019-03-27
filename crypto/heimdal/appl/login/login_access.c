/************************************************************************
* Copyright 1995 by Wietse Venema.  All rights reserved.  Some individual
* files may be covered by other copyrights.
*
* This material was originally written and compiled by Wietse Venema at
* Eindhoven University of Technology, The Netherlands, in 1990, 1991,
* 1992, 1993, 1994 and 1995.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that this entire copyright notice
* is duplicated in all such copies.
*
* This software is provided "as is" and without any expressed or implied
* warranties, including, without limitation, the implied warranties of
* merchantibility and fitness for any particular purpose.
************************************************************************/
 /*
  * This module implements a simple but effective form of login access
  * control based on login names and on host (or domain) names, internet
  * addresses (or network numbers), or on terminal line names in case of
  * non-networked logins. Diagnostics are reported through syslog(3).
  *
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#include "login_locl.h"

RCSID("$Id$");

 /* Delimiters for fields and for lists of users, ttys or hosts. */

static char fs[] = ":";			/* field separator */
static char sep[] = ", \t";		/* list-element separator */

 /* Constants to be used in assignments only, not in comparisons... */

#define YES             1
#define NO              0

 /*
  * A structure to bundle up all login-related information to keep the
  * functional interfaces as generic as possible.
  */
struct login_info {
    struct passwd *user;
    char   *from;
};

static int list_match(char *list, struct login_info *item,
		      int (*match_fn)(char *, struct login_info *));
static int user_match(char *tok, struct login_info *item);
static int from_match(char *tok, struct login_info *item);
static int string_match(char *tok, char *string);

/* login_access - match username/group and host/tty with access control file */

int login_access(struct passwd *user, char *from)
{
    struct login_info item;
    FILE   *fp;
    char    line[BUFSIZ];
    char   *perm;			/* becomes permission field */
    char   *users;			/* becomes list of login names */
    char   *froms;			/* becomes list of terminals or hosts */
    int     match = NO;
    int     end;
    int     lineno = 0;			/* for diagnostics */
    char   *foo;

    /*
     * Bundle up the arguments to avoid unnecessary clumsiness lateron.
     */
    item.user = user;
    item.from = from;

    /*
     * Process the table one line at a time and stop at the first match.
     * Blank lines and lines that begin with a '#' character are ignored.
     * Non-comment lines are broken at the ':' character. All fields are
     * mandatory. The first field should be a "+" or "-" character. A
     * non-existing table means no access control.
     */

    if ((fp = fopen(_PATH_LOGACCESS, "r")) != 0) {
	while (!match && fgets(line, sizeof(line), fp)) {
	    lineno++;
	    if (line[end = strlen(line) - 1] != '\n') {
		syslog(LOG_ERR, "%s: line %d: missing newline or line too long",
		       _PATH_LOGACCESS, lineno);
		continue;
	    }
	    if (line[0] == '#')
		continue;			/* comment line */
	    while (end > 0 && isspace((unsigned char)line[end - 1]))
		end--;
	    line[end] = 0;			/* strip trailing whitespace */
	    if (line[0] == 0)			/* skip blank lines */
		continue;
	    foo = NULL;
	    if (!(perm = strtok_r(line, fs, &foo))
		|| !(users = strtok_r(NULL, fs, &foo))
		|| !(froms = strtok_r(NULL, fs, &foo))
		|| strtok_r(NULL, fs, &foo)) {
		syslog(LOG_ERR, "%s: line %d: bad field count",
		       _PATH_LOGACCESS,
		       lineno);
		continue;
	    }
	    if (perm[0] != '+' && perm[0] != '-') {
		syslog(LOG_ERR, "%s: line %d: bad first field",
		       _PATH_LOGACCESS,
		       lineno);
		continue;
	    }
	    match = (list_match(froms, &item, from_match)
		     && list_match(users, &item, user_match));
	}
	fclose(fp);
    } else if (errno != ENOENT) {
	syslog(LOG_ERR, "cannot open %s: %m", _PATH_LOGACCESS);
    }
    return (match == 0 || (line[0] == '+'));
}

/* list_match - match an item against a list of tokens with exceptions */

static int
list_match(char *list,
	   struct login_info *item,
	   int (*match_fn)(char *, struct login_info *))
{
    char   *tok;
    int     match = NO;
    char   *foo = NULL;

    /*
     * Process tokens one at a time. We have exhausted all possible matches
     * when we reach an "EXCEPT" token or the end of the list. If we do find
     * a match, look for an "EXCEPT" list and recurse to determine whether
     * the match is affected by any exceptions.
     */

    for (tok = strtok_r(list, sep, &foo);
	 tok != NULL;
	 tok = strtok_r(NULL, sep, &foo)) {
	if (strcasecmp(tok, "EXCEPT") == 0)	/* EXCEPT: give up */
	    break;
	if ((match = (*match_fn) (tok, item)) != 0)	/* YES */
	    break;
    }
    /* Process exceptions to matches. */

    if (match != NO) {
	while ((tok = strtok_r(NULL, sep, &foo)) && strcasecmp(tok, "EXCEPT"))
	     /* VOID */ ;
	if (tok == 0 || list_match(NULL, item, match_fn) == NO)
	    return (match);
    }
    return (NO);
}

/* myhostname - figure out local machine name */

static char *myhostname(void)
{
    static char name[MAXHOSTNAMELEN + 1] = "";

    if (name[0] == 0) {
	gethostname(name, sizeof(name));
	name[MAXHOSTNAMELEN] = 0;
    }
    return (name);
}

/* netgroup_match - match group against machine or user */

static int netgroup_match(char *group, char *machine, char *user)
{
#ifdef HAVE_YP_GET_DEFAULT_DOMAIN
    static char *mydomain = 0;

    if (mydomain == 0)
	yp_get_default_domain(&mydomain);
    return (innetgr(group, machine, user, mydomain));
#else
    syslog(LOG_ERR, "NIS netgroup support not configured");
    return 0;
#endif
}

/* user_match - match a username against one token */

static int user_match(char *tok, struct login_info *item)
{
    char   *string = item->user->pw_name;
    struct login_info fake_item;
    struct group *group;
    int     i;
    char   *at;

    /*
     * If a token has the magic value "ALL" the match always succeeds.
     * Otherwise, return YES if the token fully matches the username, if the
     * token is a group that contains the username, or if the token is the
     * name of the user's primary group.
     */

    if ((at = strchr(tok + 1, '@')) != 0) {	/* split user@host pattern */
	*at = 0;
	fake_item.from = myhostname();
	return (user_match(tok, item) && from_match(at + 1, &fake_item));
    } else if (tok[0] == '@') {			/* netgroup */
	return (netgroup_match(tok + 1, (char *) 0, string));
    } else if (string_match(tok, string)) {	/* ALL or exact match */
	return (YES);
    } else if ((group = getgrnam(tok)) != 0) { /* try group membership */
	if (item->user->pw_gid == group->gr_gid)
	    return (YES);
	for (i = 0; group->gr_mem[i]; i++)
	    if (strcasecmp(string, group->gr_mem[i]) == 0)
		return (YES);
    }
    return (NO);
}

/* from_match - match a host or tty against a list of tokens */

static int from_match(char *tok, struct login_info *item)
{
    char   *string = item->from;
    int     tok_len;
    int     str_len;

    /*
     * If a token has the magic value "ALL" the match always succeeds. Return
     * YES if the token fully matches the string. If the token is a domain
     * name, return YES if it matches the last fields of the string. If the
     * token has the magic value "LOCAL", return YES if the string does not
     * contain a "." character. If the token is a network number, return YES
     * if it matches the head of the string.
     */

    if (tok[0] == '@') {			/* netgroup */
	return (netgroup_match(tok + 1, string, (char *) 0));
    } else if (string_match(tok, string)) {	/* ALL or exact match */
	return (YES);
    } else if (tok[0] == '.') {			/* domain: match last fields */
	if ((str_len = strlen(string)) > (tok_len = strlen(tok))
	    && strcasecmp(tok, string + str_len - tok_len) == 0)
	    return (YES);
    } else if (strcasecmp(tok, "LOCAL") == 0) {	/* local: no dots */
	if (strchr(string, '.') == 0)
	    return (YES);
    } else if (tok[(tok_len = strlen(tok)) - 1] == '.'	/* network */
	       && strncmp(tok, string, tok_len) == 0) {
	return (YES);
    }
    return (NO);
}

/* string_match - match a string against one token */

static int string_match(char *tok, char *string)
{

    /*
     * If the token has the magic value "ALL" the match always succeeds.
     * Otherwise, return YES if the token fully matches the string.
     */

    if (strcasecmp(tok, "ALL") == 0) {		/* all: always matches */
	return (YES);
    } else if (strcasecmp(tok, string) == 0) {	/* try exact match */
	return (YES);
    }
    return (NO);
}
