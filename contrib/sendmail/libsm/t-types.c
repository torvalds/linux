/*
 * Copyright (c) 2000-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-types.c,v 1.19 2013-11-22 20:51:44 ca Exp $")

#include <sm/limits.h>
#include <sm/io.h>
#include <sm/string.h>
#include <sm/test.h>
#include <sm/types.h>

int
main(argc, argv)
	int argc;
	char **argv;
{
	LONGLONG_T ll;
	LONGLONG_T volatile lt;
	ULONGLONG_T ull;
	char buf[128];
	char *r;

	sm_test_begin(argc, argv, "test standard integral types");

	SM_TEST(sizeof(LONGLONG_T) == sizeof(ULONGLONG_T));

	/*
	**  sendmail assumes that ino_t, off_t and void* can be cast
	**  to ULONGLONG_T without losing information.
	*/

	if (!SM_TEST(sizeof(ino_t) <= sizeof(ULONGLONG_T)) ||
	    !SM_TEST(sizeof(off_t) <= sizeof(ULONGLONG_T)) ||
	    !SM_TEST(sizeof(void*) <= sizeof(ULONGLONG_T)))
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "\
Your C compiler appears to support a 64 bit integral type,\n\
but libsm is not configured to use it.  You will need to set\n\
either SM_CONF_LONGLONG or SM_CONF_QUAD_T to 1.  See libsm/README\n\
for more details.\n");
	}

	/*
	**  Most compilers notice that LLONG_MIN - 1 generate an underflow.
	**  Some compiler generate code that will use the 'X' status bit
	**  in a CPU and hence (LLONG_MIN - 1 > LLONG_MIN) will be false.
	**  So we have to decide whether we want compiler warnings or
	**  a wrong test...
	**  Question: where do we really need what this test tests?
	*/

#if SM_CONF_TEST_LLONG
	ll = LLONG_MIN;
	(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "\
Your C compiler maybe issued a warning during compilation,\n\
please IGNORE the compiler warning!.\n");
	lt = LLONG_MIN - 1;
	SM_TEST(lt > ll);
	sm_snprintf(buf, sizeof(buf), "%llx", ll);
	r = "0";
	if (!SM_TEST(buf[0] == '8')
	    || !SM_TEST(strspn(&buf[1], r) == sizeof(ll) * 2 - 1))
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			"oops: LLONG_MIN=%s\n", buf);
	}

	ll = LLONG_MAX;
	lt = ll + 1;
	SM_TEST(lt < ll);
	sm_snprintf(buf, sizeof(buf), "%llx", ll);
	r = "f";
	if (!SM_TEST(buf[0] == '7')
	    || !SM_TEST(strspn(&buf[1], r) == sizeof(ll) * 2 - 1))
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			"oops: LLONG_MAX=%s\n", buf);
	}
#endif /* SM_CONF_TEST_LLONG */

	ull = ULLONG_MAX;
	SM_TEST(ull + 1 == 0);
	sm_snprintf(buf, sizeof(buf), "%llx", ull);
	r = "f";
	SM_TEST(strspn(buf, r) == sizeof(ll) * 2);

	/*
	**  If QUAD_MAX is defined by <limits.h> then quad_t is defined.
	**  Make sure LONGLONG_T is at least as big as quad_t.
	*/
#ifdef QUAD_MAX
	SM_TEST(QUAD_MAX <= LLONG_MAX);
#endif

	return sm_test_end();
}
