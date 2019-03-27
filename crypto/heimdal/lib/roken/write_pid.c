/*
 * Copyright (c) 1999 - 2001 Kungliga Tekniska Högskolan
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

ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
pid_file_write (const char *progname)
{
    char *ret = NULL;
    FILE *fp;

    if (asprintf (&ret, "%s%s.pid", _PATH_VARRUN, progname) < 0 || ret == NULL)
	return NULL;
    fp = fopen (ret, "w");
    if (fp == NULL) {
	free (ret);
	return NULL;
    }
    fprintf (fp, "%u", (unsigned)getpid());
    fclose (fp);
    return ret;
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
pid_file_delete (char **filename)
{
    if (*filename != NULL) {
	unlink (*filename);
	free (*filename);
	*filename = NULL;
    }
}

#ifndef HAVE_PIDFILE
static char *pidfile_path;

static void
pidfile_cleanup(void)
{
    if(pidfile_path != NULL)
	pid_file_delete(&pidfile_path);
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
pidfile(const char *basename)
{
    if(pidfile_path != NULL)
	return;
    if(basename == NULL)
	basename = getprogname();
    pidfile_path = pid_file_write(basename);
#if defined(HAVE_ATEXIT)
    atexit(pidfile_cleanup);
#elif defined(HAVE_ON_EXIT)
    on_exit(pidfile_cleanup);
#endif
}
#endif
