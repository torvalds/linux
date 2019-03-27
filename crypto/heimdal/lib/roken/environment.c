/*
 * Copyright (c) 2000, 2005 Kungliga Tekniska Högskolan
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


#include <config.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "roken.h"

/* find assignment in env list; len is length of variable including
 * equal
 */

static int
find_var(char **env, char *assignment, size_t len)
{
    int i;
    for(i = 0; env != NULL && env[i] != NULL; i++)
	if(strncmp(env[i], assignment, len) == 0)
	    return i;
    return -1;
}

/*
 * return count of environment assignments from open file F in
 * assigned and list of malloced strings in env, return 0 or errno
 * number
 */

static int
read_env_file(FILE *F, char ***env, int *assigned)
{
    int idx = 0;
    int i;
    char **l;
    char buf[BUFSIZ], *p, *r;
    char **tmp;
    int ret = 0;

    *assigned = 0;

    for(idx = 0; *env != NULL && (*env)[idx] != NULL; idx++);
    l = *env;

    /* This is somewhat more relaxed on what it accepts then
     * Wietses sysv_environ from K4 was...
     */
    while (fgets(buf, BUFSIZ, F) != NULL) {
	buf[strcspn(buf, "#\n")] = '\0';

	for(p = buf; isspace((unsigned char)*p); p++);
	if (*p == '\0')
	    continue;

	/* Here one should check that it's a 'valid' env string... */
	r = strchr(p, '=');
	if (r == NULL)
	    continue;

	if((i = find_var(l, p, r - p + 1)) >= 0) {
	    char *val = strdup(p);
	    if(val == NULL) {
		ret = ENOMEM;
		break;
	    }
	    free(l[i]);
	    l[i] = val;
	    (*assigned)++;
	    continue;
	}

	tmp = realloc(l, (idx+2) * sizeof (char *));
	if(tmp == NULL) {
	    ret = ENOMEM;
	    break;
	}

	l = tmp;
	l[idx] = strdup(p);
	if(l[idx] == NULL) {
	    ret = ENOMEM;
	    break;
	}
	l[++idx] = NULL;
	(*assigned)++;
    }
    if(ferror(F))
	ret = errno;
    *env = l;
    return ret;
}

/*
 * return count of environment assignments from file and
 * list of malloced strings in `env'
 */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
read_environment(const char *file, char ***env)
{
    int assigned;
    FILE *F;

    if ((F = fopen(file, "r")) == NULL)
	return 0;

    read_env_file(F, env, &assigned);
    fclose(F);
    return assigned;
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
free_environment(char **env)
{
    int i;
    if (env == NULL)
	return;
    for (i = 0; env[i]; i++)
	free(env[i]);
    free(env);
}
