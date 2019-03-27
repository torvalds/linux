/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthias Schmidt <matthias@dragonflybsd.org>, University of Marburg,
 * Germany.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>

#include "dma.h"

#define DP	": \t"
#define EQS	" \t"


/*
 * Remove trailing \n's
 */
void
trim_line(char *line)
{
	size_t linelen;
	char *p;

	if ((p = strchr(line, '\n')))
		*p = (char)0;

	/* Escape leading dot in every case */
	linelen = strlen(line);
	if (line[0] == '.') {
		if ((linelen + 2) > 1000) {
			syslog(LOG_CRIT, "Cannot escape leading dot.  Buffer overflow");
			exit(EX_DATAERR);
		}
		memmove((line + 1), line, (linelen + 1));
		line[0] = '.';
	}
}

static void
chomp(char *str)
{
	size_t len = strlen(str);

	if (len == 0)
		return;
	if (str[len - 1] == '\n')
		str[len - 1] = 0;
}

/*
 * Read the SMTP authentication config file
 *
 * file format is:
 * user|host:password
 *
 * A line starting with # is treated as comment and ignored.
 */
void
parse_authfile(const char *path)
{
	char line[2048];
	struct authuser *au;
	FILE *a;
	char *data;
	int lineno = 0;

	a = fopen(path, "r");
	if (a == NULL) {
		errlog(EX_NOINPUT, "can not open auth file `%s'", path);
		/* NOTREACHED */
	}

	while (!feof(a)) {
		if (fgets(line, sizeof(line), a) == NULL)
			break;
		lineno++;

		chomp(line);

		/* We hit a comment */
		if (*line == '#')
			continue;
		/* Ignore empty lines */
		if (*line == 0)
			continue;

		au = calloc(1, sizeof(*au));
		if (au == NULL)
			errlog(EX_OSERR, "calloc()");

		data = strdup(line);
		au->login = strsep(&data, "|");
		au->host = strsep(&data, DP);
		au->password = data;

		if (au->login == NULL ||
		    au->host == NULL ||
		    au->password == NULL) {
			errlogx(EX_CONFIG, "syntax error in authfile %s:%d", path, lineno);
			/* NOTREACHED */
		}

		SLIST_INSERT_HEAD(&authusers, au, next);
	}

	fclose(a);
}

/*
 * XXX TODO
 * Check for bad things[TM]
 */
void
parse_conf(const char *config_path)
{
	char *word;
	char *data;
	FILE *conf;
	char line[2048];
	int lineno = 0;

	conf = fopen(config_path, "r");
	if (conf == NULL) {
		/* Don't treat a non-existing config file as error */
		if (errno == ENOENT)
			return;
		errlog(EX_NOINPUT, "can not open config `%s'", config_path);
		/* NOTREACHED */
	}

	while (!feof(conf)) {
		if (fgets(line, sizeof(line), conf) == NULL)
			break;
		lineno++;

		chomp(line);

		/* We hit a comment */
		if (strchr(line, '#'))
			*strchr(line, '#') = 0;

		data = line;
		word = strsep(&data, EQS);

		/* Ignore empty lines */
		if (word == NULL || *word == 0)
			continue;

		if (data != NULL && *data != 0)
			data = strdup(data);
		else
			data = NULL;

		if (strcmp(word, "SMARTHOST") == 0 && data != NULL)
			config.smarthost = data;
		else if (strcmp(word, "PORT") == 0 && data != NULL)
			config.port = atoi(data);
		else if (strcmp(word, "ALIASES") == 0 && data != NULL)
			config.aliases = data;
		else if (strcmp(word, "SPOOLDIR") == 0 && data != NULL)
			config.spooldir = data;
		else if (strcmp(word, "AUTHPATH") == 0 && data != NULL)
			config.authpath= data;
		else if (strcmp(word, "CERTFILE") == 0 && data != NULL)
			config.certfile = data;
		else if (strcmp(word, "MAILNAME") == 0 && data != NULL)
			config.mailname = data;
		else if (strcmp(word, "MASQUERADE") == 0 && data != NULL) {
			char *user = NULL, *host = NULL;
			if (strrchr(data, '@')) {
				host = strrchr(data, '@');
				*host = 0;
				host++;
				user = data;
			} else {
				host = data;
			}
			if (host && *host == 0)
				host = NULL;
                        if (user && *user == 0)
                                user = NULL;
			config.masquerade_host = host;
			config.masquerade_user = user;
		} else if (strcmp(word, "STARTTLS") == 0 && data == NULL)
			config.features |= STARTTLS;
		else if (strcmp(word, "OPPORTUNISTIC_TLS") == 0 && data == NULL)
			config.features |= TLS_OPP;
		else if (strcmp(word, "SECURETRANSFER") == 0 && data == NULL)
			config.features |= SECURETRANS;
		else if (strcmp(word, "DEFER") == 0 && data == NULL)
			config.features |= DEFER;
		else if (strcmp(word, "INSECURE") == 0 && data == NULL)
			config.features |= INSECURE;
		else if (strcmp(word, "FULLBOUNCE") == 0 && data == NULL)
			config.features |= FULLBOUNCE;
		else if (strcmp(word, "NULLCLIENT") == 0 && data == NULL)
			config.features |= NULLCLIENT;
		else {
			errlogx(EX_CONFIG, "syntax error in %s:%d", config_path, lineno);
			/* NOTREACHED */
		}
	}

	if ((config.features & NULLCLIENT) && config.smarthost == NULL) {
		errlogx(EX_CONFIG, "%s: NULLCLIENT requires SMARTHOST", config_path);
		/* NOTREACHED */
	}

	fclose(conf);
}
