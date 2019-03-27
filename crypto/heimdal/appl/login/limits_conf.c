/*
 * Copyright (c) 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "login_locl.h"

RCSID("$Id$");

#include <errno.h>
#include <limits.h>
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

struct limit {
    const char *name;
    int resource;
    int scale;
    int has_limit;
    struct rlimit limit;
} limits[] = {
#define LIM(X, S) { #X, RLIMIT_##X, S, 0 }
    LIM(CORE, 1024),
    LIM(CPU, 60),
    LIM(DATA, 1024),
    LIM(FSIZE, 1024),
#ifdef RLIMIT_MEMLOCK
    LIM(MEMLOCK, 1024),
#endif
    LIM(NOFILE, 1),
#ifdef RLIMIT_NPROC
    LIM(NPROC, 1),
#endif
#ifdef RLIMIT_RSS
    LIM(RSS, 1024),
#endif
    LIM(STACK, 1024),

#ifdef RLIMIT_AS
    LIM(AS, 1024),
#endif
#ifdef RLIMIT_LOCKS
    LIM(LOCKS, 1),
#endif
    /*
      maxlogins
      priority
    */
    { NULL, 0 }
};

static struct limit *
find_limit(const char *name)
{
    struct limit *l;
    for(l = limits; l->name != NULL; l++)
	if(strcasecmp(name, l->name) == 0)
	    return l;
    return NULL;
}

/* this function reads limits.conf files similar to pam_limits
   unimplemented features include:
   	% maxlogins
	"-" no limits,
	priorities etc that are not set via setrlimit
   XXX uses static storage, and clobbers getgr*
*/

int
read_limits_conf(const char *file, const struct passwd *pwd)
{
    FILE *f;
    char *args[4];
    int lineno = 0;
    char buf[1024];
    struct limit *l;
    rlim_t value;

    f = fopen(file, "r");
    if(f == NULL) {
	if(errno != ENOENT && errno != ENOTDIR)
	    syslog(LOG_ERR, "%s: %m", file);
	return -1;
    }

    while(fgets(buf, sizeof(buf), f) != NULL) {
	char *last = NULL;
	char *end = NULL;
	int level;

	lineno++;

	if(buf[0] == '\0') {
	    syslog(LOG_ERR, "%s: line %d: NUL character", file, lineno);
	    continue;
	}
	if(buf[strlen(buf) - 1] != '\n') {
	    /* file did not end with a newline, figure out if we're at
               the EOF, or if our buffer was too small */
	    int eof = 1;
	    int c;
	    while((c = fgetc(f)) != EOF) {
		eof = 0;
		if(c == '\n')
		    break;
	    }
	    if(!eof) {
		syslog(LOG_ERR, "%s: line %d: line too long", file, lineno);
		continue;
	    }
	}
	buf[strcspn(buf, "#\r\n")] = '\0';
	if((args[0] = strtok_r(buf, " \t", &last)) == NULL ||
	   (args[1] = strtok_r(NULL, " \t", &last)) == NULL ||
	   (args[2] = strtok_r(NULL, " \t", &last)) == NULL ||
	   (args[3] = strtok_r(NULL, " \t", &last)) == NULL) {
	    if(args[0] != NULL) /* this would include comment lines */
		syslog(LOG_ERR, "%s: line %d: malformed line", file, lineno);
	    continue;
	}

	l = find_limit(args[2]);
	if(l == NULL) {
	    syslog(LOG_ERR, "%s: line %d: unknown limit %s", file, lineno, args[2]);
	    continue;
	}
	if(strcmp(args[3], "-") == 0) {
	    value = RLIM_INFINITY;
	} else {
	    errno = 0;
	    value = strtol(args[3], &end, 10);
	    if(*end != '\0') {
		syslog(LOG_ERR, "%s: line %d: bad value %s", file, lineno, args[3]);
		continue;
	    }
	    if((value == LONG_MIN || value == LONG_MAX) && errno == ERANGE) {
		syslog(LOG_ERR, "%s: line %d: bad value %s", file, lineno, args[3]);
		continue;
	    }
	    if(value * l->scale < value)
		value = RLIM_INFINITY;
	    else
		value *= l->scale;
	}
	level = 0;
	/* XXX unclear: if you set group hard and user soft limit,
           should the hard limit still apply? this code doesn't. */
	if(strcmp(args[0], pwd->pw_name) == 0)
	    level = 3;
	if(*args[0] == '@') {
	    struct group *gr;
	    gr = getgrnam(args[0] + 1);
	    if(gr != NULL && gr->gr_gid == pwd->pw_gid)
		level = 2;
	}
	if(strcmp(args[0], "*") == 0)
	    level = 1;
	if(level == 0 || level < l->has_limit) /* not for us */
	    continue;
	if(l->has_limit < level) {
	    if(getrlimit(l->resource, &l->limit) < 0)
		continue;
	    l->has_limit = level;
	}

	/* XXX unclear: if you soft to more than default hard, should
           we set hard to soft? this code doesn't. */
	if(strcasecmp(args[1], "soft") == 0 || strcmp(args[1], "-") == 0)
	    l->limit.rlim_cur = value;
	if(strcasecmp(args[1], "hard") == 0 || strcmp(args[1], "-") == 0)
	    l->limit.rlim_max = value;
    }
    fclose(f);
    for(l = limits; l->name != NULL; l++) {
	if(l->has_limit) {
	    if(l->limit.rlim_cur > l->limit.rlim_max)
		l->limit.rlim_cur = l->limit.rlim_max;
	    if(setrlimit(l->resource, &l->limit) != 0)
		syslog(LOG_ERR, "setrlimit RLIM_%s failed: %m", l->name);
	}
	l->has_limit = 0;
    }
    return 0;
}
