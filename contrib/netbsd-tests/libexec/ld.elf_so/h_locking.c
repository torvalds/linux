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

#include <dlfcn.h>
#include <link_elf.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int sleep_init;
int sleep_fini;
int dlopen_cookie;
int dlclose_cookie;

void (*tls_callback_sym)(void);

static int
dl_iterate_phdr_cb(struct dl_phdr_info *info, size_t size, void *data)
{
	(*tls_callback_sym)();
	return 0;
}

static void *
test_dl_iterate_phdr_helper(void *dummy)
{
	sleep(10);
	_exit(1);
}

static void
test_dl_iterate_phdr(void)
{
	pthread_t t;
	void *dso;
	sleep_init = 0;
	sleep_fini = 0;
	if ((dso = dlopen("libh_helper_dso2.so", RTLD_LAZY)) == NULL) {
		fprintf(stderr, "opening helper failed\n");
		_exit(1);
	}
	tls_callback_sym = dlsym(dso, "tls_callback");
	if (tls_callback_sym == NULL) {
		fprintf(stderr, "bad helper\n");
		_exit(1);
	}
	pthread_create(&t, NULL, test_dl_iterate_phdr_helper, NULL);
	if (dl_iterate_phdr(dl_iterate_phdr_cb, NULL))
		_exit(1);
	_exit(0);
}

static void *
init_fini_helper(void *arg)
{
	void *dso;
	if ((dso = dlopen(arg, RTLD_LAZY)) == NULL) {
		fprintf(stderr, "opening %s failed\n", (char *)arg);
		exit(1);
	}
	dlclose(dso);
	return NULL;
}

static void
test_dlopen(void)
{
	pthread_t t1, t2;
	sleep_init = 1;
	sleep_fini = 0;
	printf("%d\n", dlopen_cookie);
	pthread_create(&t1, NULL, init_fini_helper,
	    __UNCONST("libh_helper_dso2.so"));
	sleep(1);
	printf("%d\n", dlopen_cookie);
	if (dlopen_cookie != 1)
		_exit(1);
	sleep(1);
	pthread_create(&t2, NULL, init_fini_helper,
	    __UNCONST("libutil.so"));
	printf("%d\n", dlopen_cookie);
	if (dlopen_cookie != 1)
		_exit(1);
	_exit(0);
}

static void
test_dlclose(void)
{
	pthread_t t1, t2;
	sleep_init = 0;
	sleep_fini = 1;
	printf("%d\n", dlclose_cookie);
	pthread_create(&t1, NULL, init_fini_helper,
	    __UNCONST("libh_helper_dso2.so"));
	sleep(1);
	printf("%d\n", dlclose_cookie);
	if (dlclose_cookie != 2)
		_exit(1);
	pthread_create(&t2, NULL, init_fini_helper,
	    __UNCONST("libutil.so"));
	sleep(1);
	printf("%d\n", dlclose_cookie);
	if (dlclose_cookie != 2)
		_exit(1);
	_exit(0);
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		return 1;
	if (strcmp(argv[1], "dl_iterate_phdr") == 0)
		test_dl_iterate_phdr();
	if (strcmp(argv[1], "dlopen") == 0)
		test_dlopen();
	if (strcmp(argv[1], "dlclose") == 0)
		test_dlclose();
	return 1;
}
