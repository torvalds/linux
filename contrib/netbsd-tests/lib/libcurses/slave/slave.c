/*	$NetBSD: slave.c,v 1.6 2011/09/15 11:46:19 blymn Exp $	*/

/*-
 * Copyright 2009 Brett Lymn <blymn@NetBSD.org>
 *
 * All rights reserved.
 *
 * This code has been donated to The NetBSD Foundation by the Author.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include "returns.h"
#include "slave.h"

int cmdpipe[2];
int slvpipe[2];

#if 0
static const char *returns_enum_names[] = {
	"unused", "numeric", "string", "byte", "ERR", "OK", "NULL", "not NULL",
	"variable"
};
#endif

/*
 * Read the command pipe for the function to execute, gather the args
 * and then process the command.
 */
static void
process_commands(WINDOW *mainscr)
{
	int len, maxlen, argslen, i, ret, type;
	char *cmdbuf, *tmpbuf, **args, **tmpargs;

	len = maxlen = 30;
	if ((cmdbuf = malloc(maxlen)) == NULL)
		err(1, "slave cmdbuf malloc failed");

	while(1) {
		if (read(cmdpipe[READ_PIPE], &type, sizeof(int)) < 0)
			err(1, "slave command type read failed");

		if (type != ret_string)
			errx(1, "Unexpected type for command, got %d", type);

		if (read(cmdpipe[READ_PIPE], &len, sizeof(int)) < 0)
			err(1, "slave command len read failed");

		if ((len + 1) > maxlen) {
			maxlen = len + 1;
			if ((tmpbuf = realloc(cmdbuf, maxlen)) == NULL)
				err(1, "slave cmdbuf realloc to %d "
				    "bytes failed", maxlen);
			cmdbuf = tmpbuf;
		}

		if (read(cmdpipe[READ_PIPE], cmdbuf, len) < 0)
			err(1, "slave command read failed");
		cmdbuf[len] = '\0';
		argslen = 0;
		args = NULL;

		do {
			if (read(cmdpipe[READ_PIPE], &type, sizeof(int)) < 0)
				err(1, "slave arg type read failed");

			if (read(cmdpipe[READ_PIPE], &len, sizeof(int)) < 0)
				err(1, "slave arg len read failed");

			if (len >= 0) {
				tmpargs = realloc(args,
				    (argslen + 1) * sizeof(char *));
				if (tmpargs == NULL)
					err(1, "slave realloc of args array "
					    "failed");

				args = tmpargs;
				if (type != ret_null) {
					args[argslen] = malloc(len + 1);

					if (args[argslen] == NULL)
						err(1, "slave alloc of %d bytes"
						    " for args failed", len);
				}

				if (len == 0) {
					if (type == ret_null)
						args[argslen] = NULL;
					else
						args[argslen][0] = '\0';
				} else {
					read(cmdpipe[READ_PIPE], args[argslen],
					     len);
					if (type != ret_byte)
						args[argslen][len] = '\0';

					if (len == 6) {
						if (strcmp(args[argslen],
							   "STDSCR") == 0) {
							ret = asprintf(&tmpbuf,
								 "%p",
								 stdscr);
							if (ret < 0)
								err(2,
								    "asprintf of stdscr failed");
							free(args[argslen]);
							args[argslen] = tmpbuf;
						}
					}
				}

				argslen++;
			}
		}
		while(len >= 0);

		command_execute(cmdbuf, argslen, args);

		if (args != NULL) {
			for (i = 0; i < argslen; i++)
				free(args[i]);

			free(args);
		}
	}
}

int
main(int argc, char *argv[])
{
	WINDOW *mainscr;

	if (argc != 5) {
		fprintf(stderr, "Usage: %s <cmdin> <cmdout> <slvin> slvout>\n",
			getprogname());
		return 0;
	}
	sscanf(argv[1], "%d", &cmdpipe[0]);
	sscanf(argv[2], "%d", &cmdpipe[1]);
	sscanf(argv[3], "%d", &slvpipe[0]);
	sscanf(argv[4], "%d", &slvpipe[1]);

	mainscr = initscr();
	if (mainscr == NULL)
		err(1, "initscr failed");

	process_commands(mainscr);

	return 0;
}
