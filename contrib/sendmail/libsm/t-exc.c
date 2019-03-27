/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-exc.c,v 1.21 2013-11-22 20:51:43 ca Exp $")

#include <string.h>
#include <sm/heap.h>
#include <sm/io.h>
#include <sm/test.h>

const SM_EXC_TYPE_T EtypeTest1 =
{
	SmExcTypeMagic,
	"E:test1",
	"i",
	sm_etype_printf,
	"test1 exception argv[0]=%0",
};

const SM_EXC_TYPE_T EtypeTest2 =
{
	SmExcTypeMagic,
	"E:test2",
	"i",
	sm_etype_printf,
	"test2 exception argv[0]=%0",
};

int
main(argc, argv)
	int argc;
	char **argv;
{
	void *p;
	int volatile x;
	char *unknown, *cant;

	sm_test_begin(argc, argv, "test exception handling");

	/*
	**  SM_TRY
	*/

	cant = "can't happen";
	x = 0;
	SM_TRY
		x = 1;
	SM_END_TRY
	SM_TEST(x == 1);

	/*
	**  SM_FINALLY-0
	*/

	x = 0;
	SM_TRY
		x = 1;
	SM_FINALLY
		x = 2;
	SM_END_TRY
	SM_TEST(x == 2);

	/*
	**  SM_FINALLY-1
	*/

	x = 0;
	SM_TRY
		SM_TRY
			x = 1;
			sm_exc_raisenew_x(&EtypeTest1, 17);
		SM_FINALLY
			x = 2;
			sm_exc_raisenew_x(&EtypeTest2, 42);
		SM_END_TRY
	SM_EXCEPT(exc, "E:test2")
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
			 "got exception test2: can't happen\n");
	SM_EXCEPT(exc, "E:test1")
		SM_TEST(x == 2 && exc->exc_argv[0].v_int == 17);
		if (!(x == 2 && exc->exc_argv[0].v_int == 17))
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				"can't happen: x=%d argv[0]=%d\n",
				x, exc->exc_argv[0].v_int);
		}
	SM_EXCEPT(exc, "*")
	{
		unknown = "unknown exception: ";
		SM_TEST(strcmp(unknown, cant) == 0);
	}
	SM_END_TRY

	x = 3;
	SM_TRY
		x = 4;
		sm_exc_raisenew_x(&EtypeTest1, 94);
	SM_FINALLY
		x = 5;
		sm_exc_raisenew_x(&EtypeTest2, 95);
	SM_EXCEPT(exc, "E:test2")
	{
		unknown = "got exception test2: ";
		SM_TEST(strcmp(unknown, cant) == 0);
	}
	SM_EXCEPT(exc, "E:test1")
		SM_TEST(x == 5 && exc->exc_argv[0].v_int == 94);
		if (!(x == 5 && exc->exc_argv[0].v_int == 94))
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				"can't happen: x=%d argv[0]=%d\n",
				x, exc->exc_argv[0].v_int);
		}
	SM_EXCEPT(exc, "*")
	{
		unknown = "unknown exception: ";
		SM_TEST(strcmp(unknown, cant) == 0);
	}
	SM_END_TRY

	SM_TRY
		sm_exc_raisenew_x(&SmEtypeErr, "test %d", 0);
	SM_EXCEPT(exc, "*")
#if DEBUG
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
			"test 0 got an exception, as expected:\n");
		sm_exc_print(exc, smioout);
#endif /* DEBUG */
		return sm_test_end();
	SM_END_TRY

	p = sm_malloc_x((size_t)(-1));
	(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
		"sm_malloc_x unexpectedly succeeded, returning %p\n", p);
	unknown = "sm_malloc_x unexpectedly succeeded";
	SM_TEST(strcmp(unknown, cant) == 0);
	return sm_test_end();
}
