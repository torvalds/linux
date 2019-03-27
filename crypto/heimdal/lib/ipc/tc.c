/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <krb5-types.h>
#include <asn1-common.h>
#include <heim-ipc.h>
#include <getarg.h>
#include <err.h>
#include <roken.h>

static int help_flag;
static int version_flag;

static struct getargs args[] = {
    {	"help",		'h',	arg_flag,   &help_flag },
    {	"version",	'v',	arg_flag,   &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int ret)
{
    arg_printusage (args, num_args, NULL, "");
    exit (ret);
}

static void
reply(void *ctx, int errorcode, heim_idata *reply, heim_icred cred)
{
    printf("got reply\n");
    heim_ipc_semaphore_signal((heim_isemaphore)ctx); /* tell caller we are done */
}

static void
test_ipc(const char *service)
{
    heim_isemaphore s;
    heim_idata req, rep;
    heim_ipc ipc;
    int ret;

    ret = heim_ipc_init_context(service, &ipc);
    if (ret)
	errx(1, "heim_ipc_init_context: %d", ret);

    req.length = 0;
    req.data = NULL;

    ret = heim_ipc_call(ipc, &req, &rep, NULL);
    if (ret)
	errx(1, "heim_ipc_call: %d", ret);

    s = heim_ipc_semaphore_create(0);
    if (s == NULL)
	errx(1, "heim_ipc_semaphore_create");

    ret = heim_ipc_async(ipc, &req, s, reply);
    if (ret)
	errx(1, "heim_ipc_async: %d", ret);

    heim_ipc_semaphore_wait(s, HEIM_IPC_WAIT_FOREVER); /* wait for reply to complete the work */

    heim_ipc_free_context(ipc);
}


int
main(int argc, char **argv)
{
    int optidx = 0;

    setprogname(argv[0]);

    if (getarg(args, num_args, argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage(0);

    if (version_flag) {
	print_version(NULL);
	exit(0);
    }

#ifdef __APPLE__
    test_ipc("MACH:org.h5l.test-ipc");
#endif
    test_ipc("ANY:org.h5l.test-ipc");
    test_ipc("UNIX:org.h5l.test-ipc");

    return 0;
}
