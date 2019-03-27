/*	$Id: manpath.c,v 1.35 2017/07/01 09:47:30 schwarze Exp $ */
/*
 * Copyright (c) 2011, 2014, 2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2011 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <sys/stat.h>

#include <ctype.h>
#if HAVE_ERR
#include <err.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc_aux.h"
#include "manconf.h"

static	void	 manconf_file(struct manconf *, const char *);
static	void	 manpath_add(struct manpaths *, const char *, int);
static	void	 manpath_parseline(struct manpaths *, char *, int);


void
manconf_parse(struct manconf *conf, const char *file,
		char *defp, char *auxp)
{
	char		*insert;

	/* Always prepend -m. */
	manpath_parseline(&conf->manpath, auxp, 1);

	/* If -M is given, it overrides everything else. */
	if (NULL != defp) {
		manpath_parseline(&conf->manpath, defp, 1);
		return;
	}

	/* MANPATH and man.conf(5) cooperate. */
	defp = getenv("MANPATH");
	if (NULL == file)
		file = MAN_CONF_FILE;

	/* No MANPATH; use man.conf(5) only. */
	if (NULL == defp || '\0' == defp[0]) {
		manconf_file(conf, file);
		return;
	}

	/* Prepend man.conf(5) to MANPATH. */
	if (':' == defp[0]) {
		manconf_file(conf, file);
		manpath_parseline(&conf->manpath, defp, 0);
		return;
	}

	/* Append man.conf(5) to MANPATH. */
	if (':' == defp[strlen(defp) - 1]) {
		manpath_parseline(&conf->manpath, defp, 0);
		manconf_file(conf, file);
		return;
	}

	/* Insert man.conf(5) into MANPATH. */
	insert = strstr(defp, "::");
	if (NULL != insert) {
		*insert++ = '\0';
		manpath_parseline(&conf->manpath, defp, 0);
		manconf_file(conf, file);
		manpath_parseline(&conf->manpath, insert + 1, 0);
		return;
	}

	/* MANPATH overrides man.conf(5) completely. */
	manpath_parseline(&conf->manpath, defp, 0);
}

void
manpath_base(struct manpaths *dirs)
{
	char path_base[] = MANPATH_BASE;
	manpath_parseline(dirs, path_base, 0);
}

/*
 * Parse a FULL pathname from a colon-separated list of arrays.
 */
static void
manpath_parseline(struct manpaths *dirs, char *path, int complain)
{
	char	*dir;

	if (NULL == path)
		return;

	for (dir = strtok(path, ":"); dir; dir = strtok(NULL, ":"))
		manpath_add(dirs, dir, complain);
}

/*
 * Add a directory to the array, ignoring bad directories.
 * Grow the array one-by-one for simplicity's sake.
 */
static void
manpath_add(struct manpaths *dirs, const char *dir, int complain)
{
	char		 buf[PATH_MAX];
	struct stat	 sb;
	char		*cp;
	size_t		 i;

	if (NULL == (cp = realpath(dir, buf))) {
		if (complain)
			warn("manpath: %s", dir);
		return;
	}

	for (i = 0; i < dirs->sz; i++)
		if (0 == strcmp(dirs->paths[i], dir))
			return;

	if (stat(cp, &sb) == -1) {
		if (complain)
			warn("manpath: %s", dir);
		return;
	}

	dirs->paths = mandoc_reallocarray(dirs->paths,
	    dirs->sz + 1, sizeof(char *));

	dirs->paths[dirs->sz++] = mandoc_strdup(cp);
}

void
manconf_free(struct manconf *conf)
{
	size_t		 i;

	for (i = 0; i < conf->manpath.sz; i++)
		free(conf->manpath.paths[i]);

	free(conf->manpath.paths);
	free(conf->output.includes);
	free(conf->output.man);
	free(conf->output.paper);
	free(conf->output.style);
}

static void
manconf_file(struct manconf *conf, const char *file)
{
	const char *const toks[] = { "manpath", "output", "_whatdb" };
	char manpath_default[] = MANPATH_DEFAULT;

	FILE		*stream;
	char		*line, *cp, *ep;
	size_t		 linesz, tok, toklen;
	ssize_t		 linelen;

	if ((stream = fopen(file, "r")) == NULL)
		goto out;

	line = NULL;
	linesz = 0;

	while ((linelen = getline(&line, &linesz, stream)) != -1) {
		cp = line;
		ep = cp + linelen - 1;
		while (ep > cp && isspace((unsigned char)*ep))
			*ep-- = '\0';
		while (isspace((unsigned char)*cp))
			cp++;
		if (cp == ep || *cp == '#')
			continue;

		for (tok = 0; tok < sizeof(toks)/sizeof(toks[0]); tok++) {
			toklen = strlen(toks[tok]);
			if (cp + toklen < ep &&
			    isspace((unsigned char)cp[toklen]) &&
			    strncmp(cp, toks[tok], toklen) == 0) {
				cp += toklen;
				while (isspace((unsigned char)*cp))
					cp++;
				break;
			}
		}

		switch (tok) {
		case 2:  /* _whatdb */
			while (ep > cp && ep[-1] != '/')
				ep--;
			if (ep == cp)
				continue;
			*ep = '\0';
			/* FALLTHROUGH */
		case 0:  /* manpath */
			manpath_add(&conf->manpath, cp, 0);
			*manpath_default = '\0';
			break;
		case 1:  /* output */
			manconf_output(&conf->output, cp, 1);
			break;
		default:
			break;
		}
	}
	free(line);
	fclose(stream);

out:
	if (*manpath_default != '\0')
		manpath_parseline(&conf->manpath, manpath_default, 0);
}

int
manconf_output(struct manoutput *conf, const char *cp, int fromfile)
{
	const char *const toks[] = {
	    "includes", "man", "paper", "style",
	    "indent", "width", "fragment", "mdoc", "noval"
	};

	const char	*errstr;
	char		*oldval;
	size_t		 len, tok;

	for (tok = 0; tok < sizeof(toks)/sizeof(toks[0]); tok++) {
		len = strlen(toks[tok]);
		if ( ! strncmp(cp, toks[tok], len) &&
		    strchr(" =	", cp[len]) != NULL) {
			cp += len;
			if (*cp == '=')
				cp++;
			while (isspace((unsigned char)*cp))
				cp++;
			break;
		}
	}

	if (tok < 6 && *cp == '\0') {
		warnx("-O %s=?: Missing argument value", toks[tok]);
		return -1;
	}
	if ((tok == 6 || tok == 7) && *cp != '\0') {
		warnx("-O %s: Does not take a value: %s", toks[tok], cp);
		return -1;
	}

	switch (tok) {
	case 0:
		if (conf->includes != NULL) {
			oldval = mandoc_strdup(conf->includes);
			break;
		}
		conf->includes = mandoc_strdup(cp);
		return 0;
	case 1:
		if (conf->man != NULL) {
			oldval = mandoc_strdup(conf->man);
			break;
		}
		conf->man = mandoc_strdup(cp);
		return 0;
	case 2:
		if (conf->paper != NULL) {
			oldval = mandoc_strdup(conf->paper);
			break;
		}
		conf->paper = mandoc_strdup(cp);
		return 0;
	case 3:
		if (conf->style != NULL) {
			oldval = mandoc_strdup(conf->style);
			break;
		}
		conf->style = mandoc_strdup(cp);
		return 0;
	case 4:
		if (conf->indent) {
			mandoc_asprintf(&oldval, "%zu", conf->indent);
			break;
		}
		conf->indent = strtonum(cp, 0, 1000, &errstr);
		if (errstr == NULL)
			return 0;
		warnx("-O indent=%s is %s", cp, errstr);
		return -1;
	case 5:
		if (conf->width) {
			mandoc_asprintf(&oldval, "%zu", conf->width);
			break;
		}
		conf->width = strtonum(cp, 1, 1000, &errstr);
		if (errstr == NULL)
			return 0;
		warnx("-O width=%s is %s", cp, errstr);
		return -1;
	case 6:
		conf->fragment = 1;
		return 0;
	case 7:
		conf->mdoc = 1;
		return 0;
	case 8:
		conf->noval = 1;
		return 0;
	default:
		if (fromfile)
			warnx("-O %s: Bad argument", cp);
		return -1;
	}
	if (fromfile == 0)
		warnx("-O %s=%s: Option already set to %s",
		    toks[tok], cp, oldval);
	free(oldval);
	return -1;
}
