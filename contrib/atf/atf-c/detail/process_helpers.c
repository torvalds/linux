/* Copyright (c) 2008 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include <sys/types.h>

#include <assert.h> /* NO_CHECK_STYLE */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static
int
h_echo(const char *msg)
{
    printf("%s\n", msg);
    return EXIT_SUCCESS;
}

static
int
h_exit_failure(void)
{
    return EXIT_FAILURE;
}

static
int
h_exit_signal(void)
{
    kill(getpid(), SIGKILL);
    assert(0); /* NO_CHECK_STYLE */
    return EXIT_FAILURE;
}

static
int
h_exit_success(void)
{
    return EXIT_SUCCESS;
}

static
int
h_stdout_stderr(const char *id)
{
    fprintf(stdout, "Line 1 to stdout for %s\n", id);
    fprintf(stdout, "Line 2 to stdout for %s\n", id);
    fprintf(stderr, "Line 1 to stderr for %s\n", id);
    fprintf(stderr, "Line 2 to stderr for %s\n", id);

    return EXIT_SUCCESS;
}

static
void
check_args(const int argc, const char *const argv[], const int required)
{
    if (argc < required) {
        fprintf(stderr, "Usage: %s helper-name [args]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

int
main(int argc, const char *const argv[])
{
    int exitcode;

    check_args(argc, argv, 2);

    if (strcmp(argv[1], "echo") == 0) {
        check_args(argc, argv, 3);
        exitcode = h_echo(argv[2]);
    } else if (strcmp(argv[1], "exit-failure") == 0)
        exitcode = h_exit_failure();
    else if (strcmp(argv[1], "exit-signal") == 0)
        exitcode = h_exit_signal();
    else if (strcmp(argv[1], "exit-success") == 0)
        exitcode = h_exit_success();
    else if (strcmp(argv[1], "stdout-stderr") == 0) {
        check_args(argc, argv, 3);
        exitcode = h_stdout_stderr(argv[2]);
    } else {
        fprintf(stderr, "%s: Unknown helper %s\n", argv[0], argv[1]);
        exitcode = EXIT_FAILURE;
    }

    return exitcode;
}
