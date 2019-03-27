/*	$NetBSD: h_pad.c,v 1.2 2016/10/15 07:08:06 nat Exp $	*/

/*
 * Copyright (c) 2010 Antti Kantee.  All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "h_pad_musa.c"

/*
 * Stuff some audio into /dev/audio, read it from /dev/pad.  Use in
 * conjunction with t_pad, which tests that we got sensible output
 * by comparing against a previous audibly good result.
 */

#define BUFSIZE 1024

int
main(int argc, char *argv[])
{
	char buf[BUFSIZE];
	char zeros[BUFSIZE];
	int padfd, audiofd;
	ssize_t n;

	rump_init();
	padfd = rump_sys_open("/dev/pad0", O_RDONLY);
	if (padfd == -1)
		err(1, "open pad");

	audiofd = rump_sys_open("/dev/audio0", O_RDWR);
	if (audiofd == -1)
		err(1, "open audio");

	if ((n = rump_sys_write(audiofd, musa, sizeof(musa))) != sizeof(musa))
		err(1, "write");

	memset(zeros, 0, sizeof(zeros));
	while ((n = rump_sys_read(padfd, buf, sizeof(buf))) > 0) {
		if (memcmp(buf, zeros, sizeof(buf)) == 0)
			break;
		write(STDOUT_FILENO, buf, n);
	}
}
