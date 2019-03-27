/* tinytest.h -- Copyright 2009-2012 Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TINYTEST_H_INCLUDED_
#define TINYTEST_H_INCLUDED_

/** Flag for a test that needs to run in a subprocess. */
#define TT_FORK  (1<<0)
/** Runtime flag for a test we've decided to skip. */
#define TT_SKIP  (1<<1)
/** Internal runtime flag for a test we've decided to run. */
#define TT_ENABLED_  (1<<2)
/** Flag for a test that's off by default. */
#define TT_OFF_BY_DEFAULT  (1<<3)
/** If you add your own flags, make them start at this point. */
#define TT_FIRST_USER_FLAG (1<<4)

typedef void (*testcase_fn)(void *);

struct testcase_t;

/** Functions to initialize/teardown a structure for a testcase. */
struct testcase_setup_t {
	/** Return a new structure for use by a given testcase. */
	void *(*setup_fn)(const struct testcase_t *);
	/** Clean/free a structure from setup_fn. Return 1 if ok, 0 on err. */
	int (*cleanup_fn)(const struct testcase_t *, void *);
};

/** A single test-case that you can run. */
struct testcase_t {
	const char *name; /**< An identifier for this case. */
	testcase_fn fn; /**< The function to run to implement this case. */
	unsigned long flags; /**< Bitfield of TT_* flags. */
	const struct testcase_setup_t *setup; /**< Optional setup/cleanup fns*/
	void *setup_data; /**< Extra data usable by setup function */
};
#define END_OF_TESTCASES { NULL, NULL, 0, NULL, NULL }

/** A group of tests that are selectable together. */
struct testgroup_t {
	const char *prefix; /**< Prefix to prepend to testnames. */
	struct testcase_t *cases; /** Array, ending with END_OF_TESTCASES */
};
#define END_OF_GROUPS { NULL, NULL}

struct testlist_alias_t {
	const char *name;
	const char **tests;
};
#define END_OF_ALIASES { NULL, NULL }

/** Implementation: called from a test to indicate failure, before logging. */
void tinytest_set_test_failed_(void);
/** Implementation: called from a test to indicate that we're skipping. */
void tinytest_set_test_skipped_(void);
/** Implementation: return 0 for quiet, 1 for normal, 2 for loud. */
int tinytest_get_verbosity_(void);
/** Implementation: Set a flag on tests matching a name; returns number
 * of tests that matched. */
int tinytest_set_flag_(struct testgroup_t *, const char *, int set, unsigned long);
/** Implementation: Put a chunk of memory into hex. */
char *tinytest_format_hex_(const void *, unsigned long);

/** Set all tests in 'groups' matching the name 'named' to be skipped. */
#define tinytest_skip(groups, named) \
	tinytest_set_flag_(groups, named, 1, TT_SKIP)

/** Run a single testcase in a single group. */
int testcase_run_one(const struct testgroup_t *,const struct testcase_t *);

void tinytest_set_aliases(const struct testlist_alias_t *aliases);

/** Run a set of testcases from an END_OF_GROUPS-terminated array of groups,
    as selected from the command line. */
int tinytest_main(int argc, const char **argv, struct testgroup_t *groups);

#endif
