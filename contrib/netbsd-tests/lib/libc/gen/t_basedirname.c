/*	$NetBSD: t_basedirname.c,v 1.2 2011/07/07 09:49:59 jruoho Exp $	*/

/*
 * Regression test for basename(3).
 *
 * Written by Jason R. Thorpe <thorpej@NetBSD.org>, Oct. 2002.
 * Public domain.
 */

#include <atf-c.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

struct {
	const char *input;
	const char *output;
} test_basename_table[] = {
/*
 * The following are taken from the "Sample Input and Output Strings
 * for basename()" table in IEEE Std 1003.1-2001.
 */
	{ "/usr/lib",		"lib" },
	{ "/usr/",		"usr" },
	{ "/",			"/" },
	{ "///",		"/" },
	{ "//usr//lib//",	"lib" },
/*
 * IEEE Std 1003.1-2001:
 *
 *	If path is a null pointer or points to an empty string,
 *	basename() shall return a pointer to the string "." .
 */
	{ "",			"." },
	{ NULL,			"." },
/*
 * IEEE Std 1003.1-2001:
 *
 *	If the string is exactly "//", it is implementation-defined
 *	whether "/" or "//" is returned.
 *
 * The NetBSD implementation returns "/".
 */
	{ "//",			"/" },

	{ NULL,			NULL }
};

struct {
	const char *input;
	const char *output;
} test_dirname_table[] = {
/*
 * The following are taken from the "Sample Input and Output Strings
 * for dirname()" table in IEEE Std 1003.1-2001.
 */
	{ "/usr/lib",		"/usr" },
	{ "/usr/",		"/" },
	{ "usr",		"." },
	{ "/",			"/" },
	{ ".",			"." },
	{ "..",			"." },
/*
 * IEEE Std 1003.1-2001:
 *
 *	If path is a null pointer or points to an empty string,
 *	dirname() shall return a pointer to the string "." .
 */
	{ "",			"." },
	{ NULL,			"." },
/*
 * IEEE Std 1003.1-2001:
 *
 *	Since the meaning of the leading "//" is implementation-defined,
 *	dirname("//foo") may return either "//" or "/" (but nothing else).
 *
 * The NetBSD implementation returns "/".
 */
	{ "//foo",		"/" },
/*
 * Make sure the trailing slashes after the directory name component
 * get trimmed.  The Std does not talk about this, but this is what
 * Solaris 8's dirname(3) does.
 */
	{ "/usr///lib",		"/usr" },

	{ NULL,			NULL }
};

ATF_TC(basename_posix);
ATF_TC_HEAD(basename_posix, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test basename(3) with POSIX examples");
}

ATF_TC_BODY(basename_posix, tc)
{
	char testbuf[32], *base;
	int i;

	for (i = 0; test_basename_table[i].output != NULL; i++) {
		if (test_basename_table[i].input != NULL) {
			if (strlen(test_basename_table[i].input) >=
			    sizeof(testbuf))
				atf_tc_skip("Testbuf too small!");
			strcpy(testbuf, test_basename_table[i].input);
			base = basename(testbuf);
		} else
			base = basename(NULL);

#ifdef __NetBSD__
		/*
		 * basename(3) is allowed to modify the input buffer.
		 * However, that is considered hostile by some programs,
		 * and so we elect to consider this an error.
		 *
		 * This is not a problem, as basename(3) is also allowed
		 * to return a pointer to a statically-allocated buffer
		 * (it is explicitly not required to be reentrant).
		 */
		if (test_basename_table[i].input != NULL &&
		    strcmp(test_basename_table[i].input, testbuf) != 0) {
			fprintf(stderr,
			    "Input buffer for \"%s\" was modified\n",
			    test_basename_table[i].input);
			atf_tc_fail("Input buffer was modified.");
		}
#endif

		/* Make sure the result is correct. */
		if (strcmp(test_basename_table[i].output, base) != 0) {
			fprintf(stderr,
			    "Input \"%s\", output \"%s\", expected \"%s\"\n",
			    test_basename_table[i].input ==
				NULL ? "(null)" : test_basename_table[i].input,
			    base, test_basename_table[i].output);
			atf_tc_fail("Output does not match expected value.");
		}
	}
}


ATF_TC(dirname_posix);
ATF_TC_HEAD(dirname_posix, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test dirname(3) with POSIX examples");
}

ATF_TC_BODY(dirname_posix, tc)
{
	char testbuf[32], *base;
	int i;

	for (i = 0; test_dirname_table[i].output != NULL; i++) {
		if (test_dirname_table[i].input != NULL) {
			if (strlen(test_dirname_table[i].input) >=
			    sizeof(testbuf))
				atf_tc_skip("Testbuf too small!");
			strcpy(testbuf, test_dirname_table[i].input);
			base = dirname(testbuf);
		} else
			base = dirname(NULL);

#ifdef __NetBSD__
		/*
		 * dirname(3) is allowed to modify the input buffer.
		 * However, that is considered hostile by some programs,
		 * and so we elect to consider this an error.
		 *
		 * This is not a problem, as dirname(3) is also allowed
		 * to return a pointer to a statically-allocated buffer
		 * (it is explicitly not required to be reentrant).
		 */
		if (test_dirname_table[i].input != NULL &&
		    strcmp(test_dirname_table[i].input, testbuf) != 0) {
			fprintf(stderr,
			    "Input buffer for \"%s\" was modified\n",
			    test_dirname_table[i].input);
			atf_tc_fail("Input buffer was modified.");
		}
#endif

		/* Make sure the result is correct. */
		if (strcmp(test_dirname_table[i].output, base) != 0) {
			fprintf(stderr,
			    "Input \"%s\", output \"%s\", expected \"%s\"\n",
			    test_dirname_table[i].input ==
				NULL ? "(null)" : test_dirname_table[i].input,
			    base, test_dirname_table[i].output);
			atf_tc_fail("Output does not match expected value.");
		}
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, basename_posix);
	ATF_TP_ADD_TC(tp, dirname_posix);

	return atf_no_error();
}
