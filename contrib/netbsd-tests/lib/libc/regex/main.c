/*	$NetBSD: main.c,v 1.2 2011/09/16 16:13:18 plunky Exp $	*/

/*-
 * Copyright (c) 1993 The NetBSD Foundation, Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

#include "test_regex.h"

char *progname;
int debug = 0;
int line = 0;
int status = 0;

int copts = REG_EXTENDED;
int eopts = 0;
regoff_t startoff = 0;
regoff_t endoff = 0;

static char empty = '\0';

static char *eprint(int);
static int efind(char *);

/*
 * main - do the simple case, hand off to regress() for regression
 */
int
main(int argc, char *argv[])
{
	regex_t re;
#	define	NS	10
	regmatch_t subs[NS];
	char erbuf[100];
	int err;
	size_t len;
	int c;
	int errflg = 0;
	int i;
	extern int optind;
	extern char *optarg;

	progname = argv[0];

	while ((c = getopt(argc, argv, "c:e:S:E:x")) != -1)
		switch (c) {
		case 'c':	/* compile options */
			copts = options('c', optarg);
			break;
		case 'e':	/* execute options */
			eopts = options('e', optarg);
			break;
		case 'S':	/* start offset */
			startoff = (regoff_t)atoi(optarg);
			break;
		case 'E':	/* end offset */
			endoff = (regoff_t)atoi(optarg);
			break;
		case 'x':	/* Debugging. */
			debug++;
			break;
		case '?':
		default:
			errflg++;
			break;
		}
	if (errflg) {
		fprintf(stderr, "usage: %s ", progname);
		fprintf(stderr, "[-c copt][-C][-d] [re]\n");
		exit(2);
	}

	if (optind >= argc) {
		regress(stdin);
		exit(status);
	}

	err = regcomp(&re, argv[optind++], copts);
	if (err) {
		len = regerror(err, &re, erbuf, sizeof(erbuf));
		fprintf(stderr, "error %s, %zd/%zd `%s'\n",
			eprint(err), len, (size_t)sizeof(erbuf), erbuf);
		exit(status);
	}
	regprint(&re, stdout);	

	if (optind >= argc) {
		regfree(&re);
		exit(status);
	}

	if (eopts&REG_STARTEND) {
		subs[0].rm_so = startoff;
		subs[0].rm_eo = strlen(argv[optind]) - endoff;
	}
	err = regexec(&re, argv[optind], (size_t)NS, subs, eopts);
	if (err) {
		len = regerror(err, &re, erbuf, sizeof(erbuf));
		fprintf(stderr, "error %s, %zd/%zd `%s'\n",
			eprint(err), len, (size_t)sizeof(erbuf), erbuf);
		exit(status);
	}
	if (!(copts&REG_NOSUB)) {
		len = (int)(subs[0].rm_eo - subs[0].rm_so);
		if (subs[0].rm_so != -1) {
			if (len != 0)
				printf("match `%.*s'\n", (int)len,
					argv[optind] + subs[0].rm_so);
			else
				printf("match `'@%.1s\n",
					argv[optind] + subs[0].rm_so);
		}
		for (i = 1; i < NS; i++)
			if (subs[i].rm_so != -1)
				printf("(%d) `%.*s'\n", i,
					(int)(subs[i].rm_eo - subs[i].rm_so),
					argv[optind] + subs[i].rm_so);
	}
	exit(status);
}

/*
 * regress - main loop of regression test
 */
void
regress(FILE *in)
{
	char inbuf[1000];
#	define	MAXF	10
	char *f[MAXF];
	int nf;
	int i;
	char erbuf[100];
	size_t ne;
	const char *badpat = "invalid regular expression";
#	define	SHORT	10
	const char *bpname = "REG_BADPAT";
	regex_t re;

	while (fgets(inbuf, sizeof(inbuf), in) != NULL) {
		line++;
		if (inbuf[0] == '#' || inbuf[0] == '\n')
			continue;			/* NOTE CONTINUE */
		inbuf[strlen(inbuf)-1] = '\0';	/* get rid of stupid \n */
		if (debug)
			fprintf(stdout, "%d:\n", line);
		nf = split(inbuf, f, MAXF, "\t\t");
		if (nf < 3) {
			fprintf(stderr, "bad input, line %d\n", line);
			exit(1);
		}
		for (i = 0; i < nf; i++)
			if (strcmp(f[i], "\"\"") == 0)
				f[i] = &empty;
		if (nf <= 3)
			f[3] = NULL;
		if (nf <= 4)
			f[4] = NULL;
		try(f[0], f[1], f[2], f[3], f[4], options('c', f[1]));
		if (opt('&', f[1]))	/* try with either type of RE */
			try(f[0], f[1], f[2], f[3], f[4],
					options('c', f[1]) &~ REG_EXTENDED);
	}

	ne = regerror(REG_BADPAT, NULL, erbuf, sizeof(erbuf));
	if (strcmp(erbuf, badpat) != 0 || ne != strlen(badpat)+1) {
		fprintf(stderr, "end: regerror() test gave `%s' not `%s'\n",
							erbuf, badpat);
		status = 1;
	}
	ne = regerror(REG_BADPAT, NULL, erbuf, (size_t)SHORT);
	if (strncmp(erbuf, badpat, SHORT-1) != 0 || erbuf[SHORT-1] != '\0' ||
						ne != strlen(badpat)+1) {
		fprintf(stderr, "end: regerror() short test gave `%s' not `%.*s'\n",
						erbuf, SHORT-1, badpat);
		status = 1;
	}
	ne = regerror(REG_ITOA|REG_BADPAT, NULL, erbuf, sizeof(erbuf));
	if (strcmp(erbuf, bpname) != 0 || ne != strlen(bpname)+1) {
		fprintf(stderr, "end: regerror() ITOA test gave `%s' not `%s'\n",
						erbuf, bpname);
		status = 1;
	}
	re.re_endp = bpname;
	ne = regerror(REG_ATOI, &re, erbuf, sizeof(erbuf));
	if (atoi(erbuf) != (int)REG_BADPAT) {
		fprintf(stderr, "end: regerror() ATOI test gave `%s' not `%ld'\n",
						erbuf, (long)REG_BADPAT);
		status = 1;
	} else if (ne != strlen(erbuf)+1) {
		fprintf(stderr, "end: regerror() ATOI test len(`%s') = %ld\n",
						erbuf, (long)REG_BADPAT);
		status = 1;
	}
}

/*
 - try - try it, and report on problems
 == void try(char *f0, char *f1, char *f2, char *f3, char *f4, int opts);
 */
void
try(char *f0, char *f1, char *f2, char *f3, char *f4, int opts)
{
	regex_t re;
#	define	NSUBS	10
	regmatch_t subs[NSUBS];
#	define	NSHOULD	15
	char *should[NSHOULD];
	int nshould;
	char erbuf[100];
	int err;
	int len;
	const char *type = (opts & REG_EXTENDED) ? "ERE" : "BRE";
	int i;
	char *grump;
	char f0copy[1000];
	char f2copy[1000];

	strcpy(f0copy, f0);
	re.re_endp = (opts&REG_PEND) ? f0copy + strlen(f0copy) : NULL;
	fixstr(f0copy);
	err = regcomp(&re, f0copy, opts);
	if (err != 0 && (!opt('C', f1) || err != efind(f2))) {
		/* unexpected error or wrong error */
		len = regerror(err, &re, erbuf, sizeof(erbuf));
		fprintf(stderr, "%d: %s error %s, %d/%d `%s'\n",
					line, type, eprint(err), len,
					(int)sizeof(erbuf), erbuf);
		status = 1;
	} else if (err == 0 && opt('C', f1)) {
		/* unexpected success */
		fprintf(stderr, "%d: %s should have given REG_%s\n",
						line, type, f2);
		status = 1;
		err = 1;	/* so we won't try regexec */
	}

	if (err != 0) {
		regfree(&re);
		return;
	}

	strcpy(f2copy, f2);
	fixstr(f2copy);

	if (options('e', f1)&REG_STARTEND) {
		if (strchr(f2, '(') == NULL || strchr(f2, ')') == NULL)
			fprintf(stderr, "%d: bad STARTEND syntax\n", line);
		subs[0].rm_so = strchr(f2, '(') - f2 + 1;
		subs[0].rm_eo = strchr(f2, ')') - f2;
	}
	err = regexec(&re, f2copy, NSUBS, subs, options('e', f1));

	if (err != 0 && (f3 != NULL || err != REG_NOMATCH)) {
		/* unexpected error or wrong error */
		len = regerror(err, &re, erbuf, sizeof(erbuf));
		fprintf(stderr, "%d: %s exec error %s, %d/%d `%s'\n",
					line, type, eprint(err), len,
					(int)sizeof(erbuf), erbuf);
		status = 1;
	} else if (err != 0) {
		/* nothing more to check */
	} else if (f3 == NULL) {
		/* unexpected success */
		fprintf(stderr, "%d: %s exec should have failed\n",
						line, type);
		status = 1;
		err = 1;		/* just on principle */
	} else if (opts&REG_NOSUB) {
		/* nothing more to check */
	} else if ((grump = check(f2, subs[0], f3)) != NULL) {
		fprintf(stderr, "%d: %s %s\n", line, type, grump);
		status = 1;
		err = 1;
	}

	if (err != 0 || f4 == NULL) {
		regfree(&re);
		return;
	}

	for (i = 1; i < NSHOULD; i++)
		should[i] = NULL;
	nshould = split(f4, &should[1], NSHOULD-1, ",");
	if (nshould == 0) {
		nshould = 1;
		should[1] = &empty;
	}
	for (i = 1; i < NSUBS; i++) {
		grump = check(f2, subs[i], should[i]);
		if (grump != NULL) {
			fprintf(stderr, "%d: %s $%d %s\n", line,
							type, i, grump);
			status = 1;
			err = 1;
		}
	}

	regfree(&re);
}

/*
 - options - pick options out of a regression-test string
 == int options(int type, char *s);
 */
int
options(int type, char *s)
{
	char *p;
	int o = (type == 'c') ? copts : eopts;
	const char *legal = (type == 'c') ? "bisnmp" : "^$#tl";

	for (p = s; *p != '\0'; p++)
		if (strchr(legal, *p) != NULL)
			switch (*p) {
			case 'b':
				o &= ~REG_EXTENDED;
				break;
			case 'i':
				o |= REG_ICASE;
				break;
			case 's':
				o |= REG_NOSUB;
				break;
			case 'n':
				o |= REG_NEWLINE;
				break;
			case 'm':
				o &= ~REG_EXTENDED;
				o |= REG_NOSPEC;
				break;
			case 'p':
				o |= REG_PEND;
				break;
			case '^':
				o |= REG_NOTBOL;
				break;
			case '$':
				o |= REG_NOTEOL;
				break;
			case '#':
				o |= REG_STARTEND;
				break;
			case 't':	/* trace */
				o |= REG_TRACE;
				break;
			case 'l':	/* force long representation */
				o |= REG_LARGE;
				break;
			case 'r':	/* force backref use */
				o |= REG_BACKR;
				break;
			}
	return(o);
}

/*
 - opt - is a particular option in a regression string?
 == int opt(int c, char *s);
 */
int				/* predicate */
opt(int c, char *s)
{
	return(strchr(s, c) != NULL);
}

/*
 - fixstr - transform magic characters in strings
 == void fixstr(char *p);
 */
void
fixstr(char *p)
{
	if (p == NULL)
		return;

	for (; *p != '\0'; p++)
		if (*p == 'N')
			*p = '\n';
		else if (*p == 'T')
			*p = '\t';
		else if (*p == 'S')
			*p = ' ';
		else if (*p == 'Z')
			*p = '\0';
}

/*
 * check - check a substring match
 */
char *				/* NULL or complaint */
check(char *str, regmatch_t sub, char *should)
{
	int len;
	int shlen;
	char *p;
	static char grump[500];
	char *at = NULL;

	if (should != NULL && strcmp(should, "-") == 0)
		should = NULL;
	if (should != NULL && should[0] == '@') {
		at = should + 1;
		should = &empty;
	}

	/* check rm_so and rm_eo for consistency */
	if (sub.rm_so > sub.rm_eo || (sub.rm_so == -1 && sub.rm_eo != -1) ||
				(sub.rm_so != -1 && sub.rm_eo == -1) ||
				(sub.rm_so != -1 && sub.rm_so < 0) ||
				(sub.rm_eo != -1 && sub.rm_eo < 0) ) {
		sprintf(grump, "start %ld end %ld", (long)sub.rm_so,
							(long)sub.rm_eo);
		return(grump);
	}

	/* check for no match */
	if (sub.rm_so == -1) {
		if (should == NULL)
			return(NULL);
		else {
			sprintf(grump, "did not match");
			return(grump);
		}
	}

	/* check for in range */
	if (sub.rm_eo > (ssize_t)strlen(str)) {
		sprintf(grump, "start %ld end %ld, past end of string",
					(long)sub.rm_so, (long)sub.rm_eo);
		return(grump);
	}

	len = (int)(sub.rm_eo - sub.rm_so);
	p = str + sub.rm_so;

	/* check for not supposed to match */
	if (should == NULL) {
		sprintf(grump, "matched `%.*s'", len, p);
		return(grump);
	}

	/* check for wrong match */
	shlen = (int)strlen(should);
	if (len != shlen || strncmp(p, should, (size_t)shlen) != 0) {
		sprintf(grump, "matched `%.*s' instead", len, p);
		return(grump);
	}
	if (shlen > 0)
		return(NULL);

	/* check null match in right place */
	if (at == NULL)
		return(NULL);
	shlen = strlen(at);
	if (shlen == 0)
		shlen = 1;	/* force check for end-of-string */
	if (strncmp(p, at, shlen) != 0) {
		sprintf(grump, "matched null at `%.20s'", p);
		return(grump);
	}
	return(NULL);
}

/*
 * eprint - convert error number to name
 */
static char *
eprint(int err)
{
	static char epbuf[100];
	size_t len;

	len = regerror(REG_ITOA|err, NULL, epbuf, sizeof(epbuf));
	assert(len <= sizeof(epbuf));
	return(epbuf);
}

/*
 * efind - convert error name to number
 */
static int
efind(char *name)
{
	static char efbuf[100];
	regex_t re;

	sprintf(efbuf, "REG_%s", name);
	assert(strlen(efbuf) < sizeof(efbuf));
	re.re_endp = efbuf;
	(void) regerror(REG_ATOI, &re, efbuf, sizeof(efbuf));
	return(atoi(efbuf));
}
