/*
 * Copyright (c) 2000 Kungliga Tekniska Högskolan
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

/*
 * the environment we will send to execle and the shell.
 */

char **env;
int num_env;

void
extend_env(char *str)
{
    env = realloc(env, (num_env + 1) * sizeof(*env));
    if(env == NULL)
	errx(1, "Out of memory!");
    env[num_env++] = str;
}

void
add_env(const char *var, const char *value)
{
    int i;
    char *str;
    asprintf(&str, "%s=%s", var, value);
    if(str == NULL)
	errx(1, "Out of memory!");
    for(i = 0; i < num_env; i++)
	if(strncmp(env[i], var, strlen(var)) == 0 &&
	   env[i][strlen(var)] == '='){
	    free(env[i]);
	    env[i] = str;
	    return;
	}

    extend_env(str);
}

#if !HAVE_DECL_ENVIRON
extern char **environ;
#endif


void
copy_env(void)
{
    char **p;
    for(p = environ; *p; p++)
	extend_env(*p);
}

void
login_read_env(const char *file)
{
    char **newenv;
    char *p;
    int i, j;

    newenv = NULL;
    i = read_environment(file, &newenv);
    for (j = 0; j < i; j++) {
	p = strchr(newenv[j], '=');
	if (p == NULL)
	    errx(1, "%s: missing = in string %s",
		 file, newenv[j]);
	*p++ = 0;
	add_env(newenv[j], p);
	*--p = '=';
	free(newenv[j]);
    }
    free(newenv);
}
