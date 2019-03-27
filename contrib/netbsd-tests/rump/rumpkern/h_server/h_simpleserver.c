/*	$NetBSD: h_simpleserver.c,v 1.4 2016/01/25 12:21:42 pooka Exp $	*/

#include <sys/types.h>

#include <rump/rump.h>

#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../kernspace/kernspace.h"

#define NOFAIL(e) do { int rv = e; if (rv) err(1, #e); } while (/*CONSTCOND*/0)

struct {
	const char *str;
	void (*dofun)(char *);
} actions[] = {
	{ "sendsig", rumptest_sendsig },
};

int
main(int argc, char *argv[])
{
	unsigned i;
	bool match;

	if (argc < 2)
		exit(1);

	NOFAIL(rump_daemonize_begin());
	NOFAIL(rump_init());
	NOFAIL(rump_init_server(argv[1]));
	NOFAIL(rump_daemonize_done(RUMP_DAEMONIZE_SUCCESS));

	if (argc > 2) {
		char *arg = NULL;

		if (argc == 4)
			arg = argv[3];

		for (i = 0; i < __arraycount(actions); i++) {
			if (strcmp(actions[i].str, argv[2]) == 0) {
				rump_schedule();
				actions[i].dofun(arg);
				rump_unschedule();
				match = true;
			}
		}

		if (!match) {
			exit(1);
		}
		pause();
	} else {
		for (;;)
			pause();
	}

	return 0;
}
