/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "cpio_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_GRP_H
#include <grp.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "cpio.h"
#include "err.h"

/*
 * Short options for cpio.  Please keep this sorted.
 */
static const char *short_options = "0AaBC:cdE:F:f:H:hI:iJjLlmnO:opR:rtuVvW:yZz";

/*
 * Long options for cpio.  Please keep this sorted.
 */
static const struct option {
	const char *name;
	int required;	/* 1 if this option requires an argument */
	int equivalent;	/* Equivalent short option. */
} cpio_longopts[] = {
	{ "b64encode",			0, OPTION_B64ENCODE },
	{ "create",			0, 'o' },
	{ "dereference",		0, 'L' },
	{ "dot",			0, 'V' },
	{ "extract",			0, 'i' },
	{ "file",			1, 'F' },
	{ "format",             	1, 'H' },
	{ "grzip",			0, OPTION_GRZIP },
	{ "help",			0, 'h' },
	{ "insecure",			0, OPTION_INSECURE },
	{ "link",			0, 'l' },
	{ "list",			0, 't' },
	{ "lrzip",			0, OPTION_LRZIP },
	{ "lz4",			0, OPTION_LZ4 },
	{ "lzma",			0, OPTION_LZMA },
	{ "lzop",			0, OPTION_LZOP },
	{ "make-directories",		0, 'd' },
	{ "no-preserve-owner",		0, OPTION_NO_PRESERVE_OWNER },
	{ "null",			0, '0' },
	{ "numeric-uid-gid",		0, 'n' },
	{ "owner",			1, 'R' },
	{ "passphrase",			1, OPTION_PASSPHRASE },
	{ "pass-through",		0, 'p' },
	{ "preserve-modification-time", 0, 'm' },
	{ "preserve-owner",		0, OPTION_PRESERVE_OWNER },
	{ "quiet",			0, OPTION_QUIET },
	{ "unconditional",		0, 'u' },
	{ "uuencode",			0, OPTION_UUENCODE },
	{ "verbose",			0, 'v' },
	{ "version",			0, OPTION_VERSION },
	{ "xz",				0, 'J' },
	{ "zstd",			0, OPTION_ZSTD },
	{ NULL, 0, 0 }
};

/*
 * I used to try to select platform-provided getopt() or
 * getopt_long(), but that caused a lot of headaches.  In particular,
 * I couldn't consistently use long options in the test harness
 * because not all platforms have getopt_long().  That in turn led to
 * overuse of the -W hack in the test harness, which made it rough to
 * run the test harness against GNU cpio.  (I periodically run the
 * test harness here against GNU cpio as a sanity-check.  Yes,
 * I've found a couple of bugs in GNU cpio that way.)
 */
int
cpio_getopt(struct cpio *cpio)
{
	enum { state_start = 0, state_next_word, state_short, state_long };
	static int state = state_start;
	static char *opt_word;

	const struct option *popt, *match = NULL, *match2 = NULL;
	const char *p, *long_prefix = "--";
	size_t optlength;
	int opt = '?';
	int required = 0;

	cpio->argument = NULL;

	/* First time through, initialize everything. */
	if (state == state_start) {
		/* Skip program name. */
		++cpio->argv;
		--cpio->argc;
		state = state_next_word;
	}

	/*
	 * We're ready to look at the next word in argv.
	 */
	if (state == state_next_word) {
		/* No more arguments, so no more options. */
		if (cpio->argv[0] == NULL)
			return (-1);
		/* Doesn't start with '-', so no more options. */
		if (cpio->argv[0][0] != '-')
			return (-1);
		/* "--" marks end of options; consume it and return. */
		if (strcmp(cpio->argv[0], "--") == 0) {
			++cpio->argv;
			--cpio->argc;
			return (-1);
		}
		/* Get next word for parsing. */
		opt_word = *cpio->argv++;
		--cpio->argc;
		if (opt_word[1] == '-') {
			/* Set up long option parser. */
			state = state_long;
			opt_word += 2; /* Skip leading '--' */
		} else {
			/* Set up short option parser. */
			state = state_short;
			++opt_word;  /* Skip leading '-' */
		}
	}

	/*
	 * We're parsing a group of POSIX-style single-character options.
	 */
	if (state == state_short) {
		/* Peel next option off of a group of short options. */
		opt = *opt_word++;
		if (opt == '\0') {
			/* End of this group; recurse to get next option. */
			state = state_next_word;
			return cpio_getopt(cpio);
		}

		/* Does this option take an argument? */
		p = strchr(short_options, opt);
		if (p == NULL)
			return ('?');
		if (p[1] == ':')
			required = 1;

		/* If it takes an argument, parse that. */
		if (required) {
			/* If arg is run-in, opt_word already points to it. */
			if (opt_word[0] == '\0') {
				/* Otherwise, pick up the next word. */
				opt_word = *cpio->argv;
				if (opt_word == NULL) {
					lafe_warnc(0,
					    "Option -%c requires an argument",
					    opt);
					return ('?');
				}
				++cpio->argv;
				--cpio->argc;
			}
			if (opt == 'W') {
				state = state_long;
				long_prefix = "-W "; /* For clearer errors. */
			} else {
				state = state_next_word;
				cpio->argument = opt_word;
			}
		}
	}

	/* We're reading a long option, including -W long=arg convention. */
	if (state == state_long) {
		/* After this long option, we'll be starting a new word. */
		state = state_next_word;

		/* Option name ends at '=' if there is one. */
		p = strchr(opt_word, '=');
		if (p != NULL) {
			optlength = (size_t)(p - opt_word);
			cpio->argument = (char *)(uintptr_t)(p + 1);
		} else {
			optlength = strlen(opt_word);
		}

		/* Search the table for an unambiguous match. */
		for (popt = cpio_longopts; popt->name != NULL; popt++) {
			/* Short-circuit if first chars don't match. */
			if (popt->name[0] != opt_word[0])
				continue;
			/* If option is a prefix of name in table, record it.*/
			if (strncmp(opt_word, popt->name, optlength) == 0) {
				match2 = match; /* Record up to two matches. */
				match = popt;
				/* If it's an exact match, we're done. */
				if (strlen(popt->name) == optlength) {
					match2 = NULL; /* Forget the others. */
					break;
				}
			}
		}

		/* Fail if there wasn't a unique match. */
		if (match == NULL) {
			lafe_warnc(0,
			    "Option %s%s is not supported",
			    long_prefix, opt_word);
			return ('?');
		}
		if (match2 != NULL) {
			lafe_warnc(0,
			    "Ambiguous option %s%s (matches --%s and --%s)",
			    long_prefix, opt_word, match->name, match2->name);
			return ('?');
		}

		/* We've found a unique match; does it need an argument? */
		if (match->required) {
			/* Argument required: get next word if necessary. */
			if (cpio->argument == NULL) {
				cpio->argument = *cpio->argv;
				if (cpio->argument == NULL) {
					lafe_warnc(0,
					    "Option %s%s requires an argument",
					    long_prefix, match->name);
					return ('?');
				}
				++cpio->argv;
				--cpio->argc;
			}
		} else {
			/* Argument forbidden: fail if there is one. */
			if (cpio->argument != NULL) {
				lafe_warnc(0,
				    "Option %s%s does not allow an argument",
				    long_prefix, match->name);
				return ('?');
			}
		}
		return (match->equivalent);
	}

	return (opt);
}


/*
 * Parse the argument to the -R or --owner flag.
 *
 * The format is one of the following:
 *   <username|uid>    - Override user but not group
 *   <username>:   - Override both, group is user's default group
 *   <uid>:    - Override user but not group
 *   <username|uid>:<groupname|gid> - Override both
 *   :<groupname|gid>  - Override group but not user
 *
 * Where uid/gid are decimal representations and groupname/username
 * are names to be looked up in system database.  Note that we try
 * to look up an argument as a name first, then try numeric parsing.
 *
 * A period can be used instead of the colon.
 *
 * Sets uid/gid return as appropriate, -1 indicates uid/gid not specified.
 * TODO: If the spec uses uname/gname, then return those to the caller
 * as well.  If the spec provides uid/gid, just return names as NULL.
 *
 * Returns NULL if no error, otherwise returns error string for display.
 *
 */
const char *
owner_parse(const char *spec, int *uid, int *gid)
{
	static char errbuff[128];
	const char *u, *ue, *g;

	*uid = -1;
	*gid = -1;

	if (spec[0] == '\0')
		return ("Invalid empty user/group spec");

	/*
	 * Split spec into [user][:.][group]
	 *  u -> first char of username, NULL if no username
	 *  ue -> first char after username (colon, period, or \0)
	 *  g -> first char of group name
	 */
	if (*spec == ':' || *spec == '.') {
		/* If spec starts with ':' or '.', then just group. */
		ue = u = NULL;
		g = spec + 1;
	} else {
		/* Otherwise, [user] or [user][:] or [user][:][group] */
		ue = u = spec;
		while (*ue != ':' && *ue != '.' && *ue != '\0')
			++ue;
		g = ue;
		if (*g != '\0') /* Skip : or . to find first char of group. */
			++g;
	}

	if (u != NULL) {
		/* Look up user: ue is first char after end of user. */
		char *user;
		struct passwd *pwent;

		user = (char *)malloc(ue - u + 1);
		if (user == NULL)
			return ("Couldn't allocate memory");
		memcpy(user, u, ue - u);
		user[ue - u] = '\0';
		if ((pwent = getpwnam(user)) != NULL) {
			*uid = pwent->pw_uid;
			if (*ue != '\0')
				*gid = pwent->pw_gid;
		} else {
			char *end;
			errno = 0;
			*uid = (int)strtoul(user, &end, 10);
			if (errno || *end != '\0') {
				snprintf(errbuff, sizeof(errbuff),
				    "Couldn't lookup user ``%s''", user);
				errbuff[sizeof(errbuff) - 1] = '\0';
				free(user);
				return (errbuff);
			}
		}
		free(user);
	}

	if (*g != '\0') {
		struct group *grp;
		if ((grp = getgrnam(g)) != NULL) {
			*gid = grp->gr_gid;
		} else {
			char *end;
			errno = 0;
			*gid = (int)strtoul(g, &end, 10);
			if (errno || *end != '\0') {
				snprintf(errbuff, sizeof(errbuff),
				    "Couldn't lookup group ``%s''", g);
				errbuff[sizeof(errbuff) - 1] = '\0';
				return (errbuff);
			}
		}
	}
	return (NULL);
}
