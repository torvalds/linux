/* Miniature re-implementation of the "check" library.

   This is intended to support just enough of check to run the Expat
   tests.  This interface is based entirely on the portion of the
   check library being used.
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
   Copyright (c) 2000-2017 Expat development team
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>
#include <string.h>

#include "internal.h"  /* for UNUSED_P only */
#include "minicheck.h"

Suite *
suite_create(const char *name)
{
    Suite *suite = (Suite *) calloc(1, sizeof(Suite));
    if (suite != NULL) {
        suite->name = name;
    }
    return suite;
}

TCase *
tcase_create(const char *name)
{
    TCase *tc = (TCase *) calloc(1, sizeof(TCase));
    if (tc != NULL) {
        tc->name = name;
    }
    return tc;
}

void
suite_add_tcase(Suite *suite, TCase *tc) 
{
    assert(suite != NULL);
    assert(tc != NULL);
    assert(tc->next_tcase == NULL);

    tc->next_tcase = suite->tests;
    suite->tests = tc;
}

void
tcase_add_checked_fixture(TCase *tc,
                          tcase_setup_function setup,
                          tcase_teardown_function teardown)
{
    assert(tc != NULL);
    tc->setup = setup;
    tc->teardown = teardown;
}

void
tcase_add_test(TCase *tc, tcase_test_function test)
{
    assert(tc != NULL);
    if (tc->allocated == tc->ntests) {
        int nalloc = tc->allocated + 100;
        size_t new_size = sizeof(tcase_test_function) * nalloc;
        tcase_test_function *new_tests = realloc(tc->tests, new_size);
        assert(new_tests != NULL);
        tc->tests = new_tests;
        tc->allocated = nalloc;
    }
    tc->tests[tc->ntests] = test;
    tc->ntests++;
}

static void
tcase_free(TCase *tc)
{
    if (! tc) {
        return;
    }

    free(tc->tests);
    free(tc);
}

static void
suite_free(Suite *suite)
{
    if (! suite) {
        return;
    }

    while (suite->tests != NULL) {
        TCase *next = suite->tests->next_tcase;
        tcase_free(suite->tests);
        suite->tests = next;
    }
    free(suite);
}

SRunner *
srunner_create(Suite *suite)
{
    SRunner *runner = calloc(1, sizeof(SRunner));
    if (runner != NULL) {
        runner->suite = suite;
    }
    return runner;
}

static jmp_buf env;

static char const *_check_current_function = NULL;
static int _check_current_lineno = -1;
static char const *_check_current_filename = NULL;

void
_check_set_test_info(char const *function, char const *filename, int lineno)
{
    _check_current_function = function;
    _check_current_lineno = lineno;
    _check_current_filename = filename;
}


static void
add_failure(SRunner *runner, int verbosity)
{
    runner->nfailures++;
    if (verbosity >= CK_VERBOSE) {
        printf("%s:%d: %s\n", _check_current_filename,
               _check_current_lineno, _check_current_function);
    }
}

void
srunner_run_all(SRunner *runner, int verbosity)
{
    Suite *suite;
    TCase *tc;
    assert(runner != NULL);
    suite = runner->suite;
    tc = suite->tests;
    while (tc != NULL) {
        int i;
        for (i = 0; i < tc->ntests; ++i) {
            runner->nchecks++;

            if (tc->setup != NULL) {
                /* setup */
                if (setjmp(env)) {
                    add_failure(runner, verbosity);
                    continue;
                }
                tc->setup();
            }
            /* test */
            if (setjmp(env)) {
                add_failure(runner, verbosity);
                continue;
            }
            (tc->tests[i])();

            /* teardown */
            if (tc->teardown != NULL) {
                if (setjmp(env)) {
                    add_failure(runner, verbosity);
                    continue;
                }
                tc->teardown();
            }
        }
        tc = tc->next_tcase;
    }
    if (verbosity) {
        int passed = runner->nchecks - runner->nfailures;
        double percentage = ((double) passed) / runner->nchecks;
        int display = (int) (percentage * 100);
        printf("%d%%: Checks: %d, Failed: %d\n",
               display, runner->nchecks, runner->nfailures);
    }
}

void
_fail_unless(int UNUSED_P(condition), const char *UNUSED_P(file), int UNUSED_P(line), const char *msg)
{
    /* Always print the error message so it isn't lost.  In this case,
       we have a failure, so there's no reason to be quiet about what
       it is.
    */
    if (msg != NULL) {
        const int has_newline = (msg[strlen(msg) - 1] == '\n');
        fprintf(stderr, "ERROR: %s%s", msg, has_newline ? "" : "\n");
    }
    longjmp(env, 1);
}

int
srunner_ntests_failed(SRunner *runner)
{
    assert(runner != NULL);
    return runner->nfailures;
}

void
srunner_free(SRunner *runner)
{
    if (! runner) {
        return;
    }

    suite_free(runner->suite);
    free(runner);
}
