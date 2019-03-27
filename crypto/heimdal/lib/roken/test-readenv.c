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

#include <config.h>

#include "roken.h"
#include "test-mem.h"

char *s1 = "VAR1=VAL1#comment\n\
VAR2=VAL2 VAL2 #comment\n\
#this another comment\n\
\n\
VAR3=FOO";

char *s2 = "VAR1=ENV2\n\
";

static void
make_file(char *tmpl, size_t l)
{
    int fd;
    strlcpy(tmpl, "env.XXXXXX", l);
    fd = mkstemp(tmpl);
    if(fd < 0)
	err(1, "mkstemp");
    close(fd);
}

static void
write_file(const char *fn, const char *s)
{
    FILE *f;
    f = fopen(fn, "w");
    if(f == NULL) {
	unlink(fn);
	err(1, "fopen");
    }
    if(fwrite(s, 1, strlen(s), f) != strlen(s))
	err(1, "short write");
    if(fclose(f) != 0) {
	unlink(fn);
	err(1, "fclose");
    }
}

int
main(int argc, char **argv)
{
    char **env = NULL;
    int count = 0;
    char fn[MAXPATHLEN];
    int error = 0;

    make_file(fn, sizeof(fn));

    write_file(fn, s1);
    count = read_environment(fn, &env);
    if(count != 3) {
	warnx("test 1: variable count %d != 3", count);
	error++;
    }

    write_file(fn, s2);
    count = read_environment(fn, &env);
    if(count != 1) {
	warnx("test 2: variable count %d != 1", count);
	error++;
    }

    unlink(fn);
    count = read_environment(fn, &env);
    if(count != 0) {
	warnx("test 3: variable count %d != 0", count);
	error++;
    }
    for(count = 0; env && env[count]; count++);
    if(count != 3) {
	warnx("total variable count %d != 3", count);
	error++;
    }
    free_environment(env);


    return error;
}
