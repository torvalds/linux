/*	$NetBSD: h_ps_strings1.c,v 1.1 2011/03/05 18:14:33 pgoyette Exp $	*/
/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Joerg Sonnenberger.
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

#include <sys/types.h>
#include <sys/exec.h>
#include <stdlib.h>
#include <unistd.h>

extern struct ps_strings *__ps_strings;

int
main(int argc, char **argv, char **environ)
{
	int ret = 0;
	int nenv;

	if (__ps_strings->ps_nargvstr != argc) {
		static const char nargv_err[] = "Wrong argc in ps_strings";
		write(STDOUT_FILENO, nargv_err, sizeof(nargv_err));
		ret = 1;
	}

	if (__ps_strings->ps_argvstr != argv) {
		static const char argv_err[] = "Wrong argv in ps_strings";
		write(STDOUT_FILENO, argv_err, sizeof(argv_err));
		ret = 1;
	}

	if (__ps_strings->ps_envstr != environ) {
		static const char env_err[] = "Wrong env in ps_strings";
		write(STDOUT_FILENO, env_err, sizeof(env_err));
		ret = 1;
	}
	nenv = 0;
	while (environ[nenv])
		++nenv;
	if (__ps_strings->ps_nenvstr != nenv) {
		static const char nenv_err[] = "Wrong nenv in ps_strings";
		write(STDOUT_FILENO, nenv_err, sizeof(nenv_err));
		ret = 1;
	}

	return ret;
}
