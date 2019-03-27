/*	$NetBSD: k_helper3.c,v 1.3 2012/04/17 21:39:19 martin Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: k_helper3.c,v 1.3 2012/04/17 21:39:19 martin Exp $");

#include <sys/module.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <prop/proplib.h>

static int load(const char *, ...);

/*
 * A program that loads a module and returns the errno(2) from modctl(8).
 */
int
main(int argc, char *argv[])
{
	assert(argc == 3);

	return load(argv[1], argv[2]);
}

static __printflike(1, 2) int
load(const char *fmt, ...)
{
	char filename[MAXPATHLEN], *propsstr, *shortname, *dot;
	prop_dictionary_t props;
	modctl_load_t ml;
	int serrno, rv;
	va_list ap;

	props = prop_dictionary_create();
	propsstr = prop_dictionary_externalize(props);
	assert(propsstr != NULL);
	prop_object_release(props);

	va_start(ap, fmt);
	(void)vsnprintf(filename, sizeof(filename), fmt, ap);
	va_end(ap);

	ml.ml_filename = filename;
	ml.ml_flags = 0;
	ml.ml_props = propsstr;
	ml.ml_propslen = strlen(propsstr);

	printf("Loading module %s\n", filename);
	errno = serrno = 0;

	rv = modctl(MODCTL_LOAD, &ml);

	if (rv != 0)
		serrno = errno;

	shortname = strrchr(filename, '/');
	if (shortname != NULL)
		shortname++;
	else
		shortname = filename;
	dot = strrchr(shortname, '.');
	if (dot)
		*dot = 0;
	(void)modctl(MODCTL_UNLOAD, shortname);

	free(propsstr);

	return serrno;
}
