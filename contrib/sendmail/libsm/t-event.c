/*
 * Copyright (c) 2001-2002, 2004 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: t-event.c,v 1.14 2013-11-22 20:51:43 ca Exp $")

#include <stdio.h>

#include <stdlib.h>
#include <unistd.h>
# include <sys/wait.h>
#if SM_CONF_SETITIMER
# include <sm/time.h>
#endif /* SM_CONF_SETITIMER */

#include <sm/clock.h>
#include <sm/test.h>

static void	evcheck __P((int));
static void	ev1 __P((int));

static int check;

static void
evcheck(arg)
	int arg;
{
	SM_TEST(arg == 3);
	SM_TEST(check == 0);
	check++;
}

static void
ev1(arg)
	int arg;
{
	SM_TEST(arg == 1);
}

/* define as x if you want debug output */
#define DBG_OUT(x)

int
main(argc, argv)
	int argc;
	char *argv[];
{
	SM_EVENT *ev;

	sm_test_begin(argc, argv, "test event handling");
	fprintf(stdout, "This test may hang. If there is no output within twelve seconds, abort it\nand recompile with -DSM_CONF_SETITIMER=%d\n",
		SM_CONF_SETITIMER == 0 ? 1 : 0);
	sleep(1);
	SM_TEST(1 == 1);
	DBG_OUT(fprintf(stdout, "We're back, test 1 seems to work.\n"));
	ev = sm_seteventm(1000, ev1, 1);
	sleep(1);
	SM_TEST(2 == 2);
	DBG_OUT(fprintf(stdout, "We're back, test 2 seems to work.\n"));

	/* schedule an event in 9s */
	ev = sm_seteventm(9000, ev1, 2);
	sleep(1);

	/* clear the event before it can fire */
	sm_clrevent(ev);
	SM_TEST(3 == 3);
	DBG_OUT(fprintf(stdout, "We're back, test 3 seems to work.\n"));

	/* schedule an event in 1s */
	check = 0;
	ev = sm_seteventm(1000, evcheck, 3);
	sleep(2);

	/* clear the event */
	sm_clrevent(ev);
	SM_TEST(4 == 4);
	SM_TEST(check == 1);
	DBG_OUT(fprintf(stdout, "We're back, test 4 seems to work.\n"));

	return sm_test_end();
}
