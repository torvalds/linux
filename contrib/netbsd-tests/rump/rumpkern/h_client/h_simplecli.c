/*	$NetBSD: h_simplecli.c,v 1.2 2011/01/14 13:23:15 pooka Exp $	*/

#include <sys/types.h>

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rumpclient.h>

int
main(int argc, char *argv[])
{

	if (rumpclient_init() == -1)
		err(1, "rumpclient init");

	if (argc > 1) {
		for (;;) {
			rump_sys_getpid();
			usleep(10000);
		}
	} else {
		if (rump_sys_getpid() > 0)
			exit(0);
		err(1, "getpid");
	}
}
