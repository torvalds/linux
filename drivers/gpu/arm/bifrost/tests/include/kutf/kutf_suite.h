/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2017, 2020-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KERNEL_UTF_SUITE_H_
#define _KERNEL_UTF_SUITE_H_

/* kutf_suite.h
 * Functions for management of test suites.
 *
 * This collection of data structures, macros, and functions are used to
 * create Test Suites, Tests within those Test Suites, and Fixture variants
 * of each test.
 */

#include <linux/kref.h>
#include <linux/workqueue.h>
#include <linux/wait.h>

#include <kutf/kutf_mem.h>
#include <kutf/kutf_resultset.h>

/* Arbitrary maximum size to prevent user space allocating too much kernel
 * memory
 */
#define KUTF_MAX_LINE_LENGTH (1024u)

/**
 * KUTF_F_TEST_NONE - Pseudo-flag indicating an absence of any specified test class.
 * Note that tests should not be annotated with this constant as it is simply a zero
 * value; tests without a more specific class must be marked with the flag
 * KUTF_F_TEST_GENERIC.
 */
#define KUTF_F_TEST_NONE                ((unsigned int)(0))

/**
 * KUTF_F_TEST_SMOKETEST - Class indicating this test is a smoke test.
 * A given set of smoke tests should be quick to run, enabling rapid turn-around
 * of "regress-on-commit" test runs.
 */
#define KUTF_F_TEST_SMOKETEST           ((unsigned int)(1 << 1))

/**
 * KUTF_F_TEST_PERFORMANCE - Class indicating this test is a performance test.
 * These tests typically produce a performance metric, such as "time to run" or
 * "frames per second",
 */
#define KUTF_F_TEST_PERFORMANCE         ((unsigned int)(1 << 2))

/**
 * KUTF_F_TEST_DEPRECATED - Class indicating that this test is a deprecated test.
 * These tests have typically been replaced by an alternative test which is
 * more efficient, or has better coverage.
 */
#define KUTF_F_TEST_DEPRECATED          ((unsigned int)(1 << 3))

/**
 * KUTF_F_TEST_EXPECTED_FAILURE - Class indicating that this test is a known failure.
 * These tests have typically been run and failed, but marking them as a known
 * failure means it is easier to triage results.
 *
 * It is typically more convenient to triage known failures using the
 * results database and web UI, as this means there is no need to modify the
 * test code.
 */
#define KUTF_F_TEST_EXPECTED_FAILURE    ((unsigned int)(1 << 4))

/**
 * KUTF_F_TEST_GENERIC - Class indicating that this test is a generic test,
 * which is not a member of a more specific test class.
 * Tests which are not created with a specific set
 * of filter flags by the user are assigned this test class by default.
 */
#define KUTF_F_TEST_GENERIC             ((unsigned int)(1 << 5))

/**
 * KUTF_F_TEST_RESFAIL - Class indicating this test is a resource allocation failure test.
 * A resource allocation failure test will test that an error code is
 * correctly propagated when an allocation fails.
 */
#define KUTF_F_TEST_RESFAIL             ((unsigned int)(1 << 6))

/**
 * KUTF_F_TEST_EXPECTED_FAILURE_RF - Additional flag indicating that this test
 * is an expected failure when run in resource failure mode.
 * These tests are never run when running the low resource mode.
 */
#define KUTF_F_TEST_EXPECTED_FAILURE_RF ((unsigned int)(1 << 7))

/**
 * KUTF_F_TEST_USER_0 - Flag reserved for user-defined filter zero.
 */
#define KUTF_F_TEST_USER_0 ((unsigned int)(1 << 24))

/**
 * KUTF_F_TEST_USER_1 - Flag reserved for user-defined filter one.
 */
#define KUTF_F_TEST_USER_1 ((unsigned int)(1 << 25))

/**
 * KUTF_F_TEST_USER_2 - Flag reserved for user-defined filter two.
 */
#define KUTF_F_TEST_USER_2 ((unsigned int)(1 << 26))

/**
 * KUTF_F_TEST_USER_3 - Flag reserved for user-defined filter three.
 */
#define KUTF_F_TEST_USER_3 ((unsigned int)(1 << 27))

/**
 * KUTF_F_TEST_USER_4 - Flag reserved for user-defined filter four.
 */
#define KUTF_F_TEST_USER_4 ((unsigned int)(1 << 28))

/**
 * KUTF_F_TEST_USER_5 - Flag reserved for user-defined filter five.
 */
#define KUTF_F_TEST_USER_5 ((unsigned int)(1 << 29))

/**
 * KUTF_F_TEST_USER_6 - Flag reserved for user-defined filter six.
 */
#define KUTF_F_TEST_USER_6 ((unsigned int)(1 << 30))

/**
 * KUTF_F_TEST_USER_7 - Flag reserved for user-defined filter seven.
 */
#define KUTF_F_TEST_USER_7 ((unsigned int)(1 << 31))

/**
 * KUTF_F_TEST_ALL - Pseudo-flag indicating that all test classes should be executed.
 */
#define KUTF_F_TEST_ALL                 ((unsigned int)(0xFFFFFFFFU))

/**
 * union kutf_callback_data - Union used to store test callback data
 * @ptr_value:		pointer to the location where test callback data
 *                      are stored
 * @u32_value:		a number which represents test callback data
 */
union kutf_callback_data {
	void *ptr_value;
	u32  u32_value;
};

/**
 * struct kutf_userdata_line - A line of user data to be returned to the user
 * @node:   struct list_head to link this into a list
 * @str:    The line of user data to return to user space
 * @size:   The number of bytes within @str
 */
struct kutf_userdata_line {
	struct list_head node;
	char *str;
	size_t size;
};

/**
 * KUTF_USERDATA_WARNING_OUTPUT - Flag specifying that a warning has been output
 *
 * If user space reads the "run" file while the test is waiting for user data,
 * then the framework will output a warning message and set this flag within
 * struct kutf_userdata. A subsequent read will then simply return an end of
 * file condition rather than outputting the warning again. The upshot of this
 * is that simply running 'cat' on a test which requires user data will produce
 * the warning followed by 'cat' exiting due to EOF - which is much more user
 * friendly than blocking indefinitely waiting for user data.
 */
#define KUTF_USERDATA_WARNING_OUTPUT  1

/**
 * struct kutf_userdata - Structure holding user data
 * @flags:       See %KUTF_USERDATA_WARNING_OUTPUT
 * @input_head:  List of struct kutf_userdata_line containing user data
 *               to be read by the kernel space test.
 * @input_waitq: Wait queue signalled when there is new user data to be
 *               read by the kernel space test.
 */
struct kutf_userdata {
	unsigned long flags;
	struct list_head input_head;
	wait_queue_head_t input_waitq;
};

/**
 * struct kutf_context - Structure representing a kernel test context
 * @kref:		Refcount for number of users of this context
 * @suite:		Convenience pointer to the suite this context
 *                      is running
 * @test_fix:		The fixture that is being run in this context
 * @fixture_pool:	The memory pool used for the duration of
 *                      the fixture/text context.
 * @fixture:		The user provided fixture structure.
 * @fixture_index:	The index (id) of the current fixture.
 * @fixture_name:	The name of the current fixture (or NULL if unnamed).
 * @test_data:		Any user private data associated with this test
 * @result_set:		All the results logged by this test context
 * @status:		The status of the currently running fixture.
 * @expected_status:	The expected status on exist of the currently
 *                      running fixture.
 * @work:		Work item to enqueue onto the work queue to run the test
 * @userdata:		Structure containing the user data for the test to read
 */
struct kutf_context {
	struct kref                     kref;
	struct kutf_suite               *suite;
	struct kutf_test_fixture        *test_fix;
	struct kutf_mempool             fixture_pool;
	void                            *fixture;
	unsigned int                    fixture_index;
	const char                      *fixture_name;
	union kutf_callback_data        test_data;
	struct kutf_result_set          *result_set;
	enum kutf_result_status         status;
	enum kutf_result_status         expected_status;

	struct work_struct              work;
	struct kutf_userdata            userdata;
};

/**
 * struct kutf_suite - Structure representing a kernel test suite
 * @app:			The application this suite belongs to.
 * @name:			The name of this suite.
 * @suite_data:			Any user private data associated with this
 *                              suite.
 * @create_fixture:		Function used to create a new fixture instance
 * @remove_fixture:		Function used to destroy a new fixture instance
 * @fixture_variants:		The number of variants (must be at least 1).
 * @suite_default_flags:	Suite global filter flags which are set on
 *                              all tests.
 * @node:			List node for suite_list
 * @dir:			The debugfs directory for this suite
 * @test_list:			List head to store all the tests which are
 *                              part of this suite
 */
struct kutf_suite {
	struct kutf_application        *app;
	const char                     *name;
	union kutf_callback_data       suite_data;
	void *(*create_fixture)(struct kutf_context *context);
	void  (*remove_fixture)(struct kutf_context *context);
	unsigned int                   fixture_variants;
	unsigned int                   suite_default_flags;
	struct list_head               node;
	struct dentry                  *dir;
	struct list_head               test_list;
};

/** ===========================================================================
 * Application functions
 * ============================================================================
 */

/**
 * kutf_create_application() - Create an in kernel test application.
 * @name:	The name of the test application.
 *
 * Return: pointer to the kutf_application  on success or NULL
 * on failure
 */
struct kutf_application *kutf_create_application(const char *name);

/**
 * kutf_destroy_application() - Destroy an in kernel test application.
 *
 * @app:	The test application to destroy.
 */
void kutf_destroy_application(struct kutf_application *app);

/**============================================================================
 * Suite functions
 * ============================================================================
 */

/**
 * kutf_create_suite() - Create a kernel test suite.
 * @app:		The test application to create the suite in.
 * @name:		The name of the suite.
 * @fixture_count:	The number of fixtures to run over the test
 *                      functions in this suite
 * @create_fixture:	Callback used to create a fixture. The returned value
 *                      is stored in the fixture pointer in the context for
 *                      use in the test functions.
 * @remove_fixture:	Callback used to remove a previously created fixture.
 *
 * Suite names must be unique. Should two suites with the same name be
 * registered with the same application then this function will fail, if they
 * are registered with different applications then the function will not detect
 * this and the call will succeed.
 *
 * Return: pointer to the created kutf_suite on success or NULL
 * on failure
 */
struct kutf_suite *kutf_create_suite(
		struct kutf_application *app,
		const char *name,
		unsigned int fixture_count,
		void *(*create_fixture)(struct kutf_context *context),
		void (*remove_fixture)(struct kutf_context *context));

/**
 * kutf_create_suite_with_filters() - Create a kernel test suite with user
 *                                    defined default filters.
 * @app:		The test application to create the suite in.
 * @name:		The name of the suite.
 * @fixture_count:	The number of fixtures to run over the test
 *                      functions in this suite
 * @create_fixture:	Callback used to create a fixture. The returned value
 *			is stored in the fixture pointer in the context for
 *			use in the test functions.
 * @remove_fixture:	Callback used to remove a previously created fixture.
 * @filters:		Filters to apply to a test if it doesn't provide its own
 *
 * Suite names must be unique. Should two suites with the same name be
 * registered with the same application then this function will fail, if they
 * are registered with different applications then the function will not detect
 * this and the call will succeed.
 *
 * Return: pointer to the created kutf_suite on success or NULL on failure
 */
struct kutf_suite *kutf_create_suite_with_filters(
		struct kutf_application *app,
		const char *name,
		unsigned int fixture_count,
		void *(*create_fixture)(struct kutf_context *context),
		void (*remove_fixture)(struct kutf_context *context),
		unsigned int filters);

/**
 * kutf_create_suite_with_filters_and_data() - Create a kernel test suite with
 *                                             user defined default filters.
 * @app:		The test application to create the suite in.
 * @name:		The name of the suite.
 * @fixture_count:	The number of fixtures to run over the test
 *			functions in this suite
 * @create_fixture:	Callback used to create a fixture. The returned value
 *			is stored in the fixture pointer in the context for
 *			use in the test functions.
 * @remove_fixture:	Callback used to remove a previously created fixture.
 * @filters:		Filters to apply to a test if it doesn't provide its own
 * @suite_data:		Suite specific callback data, provided during the
 *			running of the test in the kutf_context
 *
 * Return: pointer to the created kutf_suite on success or NULL
 * on failure
 */
struct kutf_suite *kutf_create_suite_with_filters_and_data(
		struct kutf_application *app,
		const char *name,
		unsigned int fixture_count,
		void *(*create_fixture)(struct kutf_context *context),
		void (*remove_fixture)(struct kutf_context *context),
		unsigned int filters,
		union kutf_callback_data suite_data);

/**
 * kutf_add_test() - Add a test to a kernel test suite.
 * @suite:	The suite to add the test to.
 * @id:		The ID of the test.
 * @name:	The name of the test.
 * @execute:	Callback to the test function to run.
 *
 * Note: As no filters are provided the test will use the suite filters instead
 */
void kutf_add_test(struct kutf_suite *suite,
		unsigned int id,
		const char *name,
		void (*execute)(struct kutf_context *context));

/**
 * kutf_add_test_with_filters() - Add a test to a kernel test suite with filters
 * @suite:	The suite to add the test to.
 * @id:		The ID of the test.
 * @name:	The name of the test.
 * @execute:	Callback to the test function to run.
 * @filters:	A set of filtering flags, assigning test categories.
 */
void kutf_add_test_with_filters(struct kutf_suite *suite,
		unsigned int id,
		const char *name,
		void (*execute)(struct kutf_context *context),
		unsigned int filters);

/**
 * kutf_add_test_with_filters_and_data() - Add a test to a kernel test suite
 *					   with filters.
 * @suite:	The suite to add the test to.
 * @id:		The ID of the test.
 * @name:	The name of the test.
 * @execute:	Callback to the test function to run.
 * @filters:	A set of filtering flags, assigning test categories.
 * @test_data:	Test specific callback data, provided during the
 *		running of the test in the kutf_context
 */
void kutf_add_test_with_filters_and_data(
		struct kutf_suite *suite,
		unsigned int id,
		const char *name,
		void (*execute)(struct kutf_context *context),
		unsigned int filters,
		union kutf_callback_data test_data);

/** ===========================================================================
 * Test functions
 * ============================================================================
 */
/**
 * kutf_test_log_result_external() - Log a result which has been created
 *                                   externally into a in a standard form
 *                                   recognized by the log parser.
 * @context:	The test context the test is running in
 * @message:	The message for this result
 * @new_status:	The result status of this log message
 */
void kutf_test_log_result_external(
	struct kutf_context *context,
	const char *message,
	enum kutf_result_status new_status);

/**
 * kutf_test_expect_abort() - Tell the kernel that you expect the current
 *                            fixture to produce an abort.
 * @context:	The test context this test is running in.
 */
void kutf_test_expect_abort(struct kutf_context *context);

/**
 * kutf_test_expect_fatal() - Tell the kernel that you expect the current
 *                            fixture to produce a fatal error.
 * @context:	The test context this test is running in.
 */
void kutf_test_expect_fatal(struct kutf_context *context);

/**
 * kutf_test_expect_fail() - Tell the kernel that you expect the current
 *                           fixture to fail.
 * @context:	The test context this test is running in.
 */
void kutf_test_expect_fail(struct kutf_context *context);

/**
 * kutf_test_expect_warn() - Tell the kernel that you expect the current
 *                           fixture to produce a warning.
 * @context:	The test context this test is running in.
 */
void kutf_test_expect_warn(struct kutf_context *context);

/**
 * kutf_test_expect_pass() - Tell the kernel that you expect the current
 *                           fixture to pass.
 * @context:	The test context this test is running in.
 */
void kutf_test_expect_pass(struct kutf_context *context);

/**
 * kutf_test_skip() - Tell the kernel that the test should be skipped.
 * @context:	The test context this test is running in.
 */
void kutf_test_skip(struct kutf_context *context);

/**
 * kutf_test_skip_msg() - Tell the kernel that this test has been skipped,
 *                        supplying a reason string.
 * @context:	The test context this test is running in.
 * @message:	A message string containing the reason for the skip.
 *
 * Note: The message must not be freed during the lifetime of the test run.
 * This means it should either be a prebaked string, or if a dynamic string
 * is required it must be created with kutf_dsprintf which will store
 * the resultant string in a buffer who's lifetime is the same as the test run.
 */
void kutf_test_skip_msg(struct kutf_context *context, const char *message);

/**
 * kutf_test_pass() - Tell the kernel that this test has passed.
 * @context:	The test context this test is running in.
 * @message:	A message string containing the reason for the pass.
 *
 * Note: The message must not be freed during the lifetime of the test run.
 * This means it should either be a pre-baked string, or if a dynamic string
 * is required it must be created with kutf_dsprintf which will store
 * the resultant string in a buffer who's lifetime is the same as the test run.
 */
void kutf_test_pass(struct kutf_context *context, char const *message);

/**
 * kutf_test_debug() - Send a debug message
 * @context:	The test context this test is running in.
 * @message:	A message string containing the debug information.
 *
 * Note: The message must not be freed during the lifetime of the test run.
 * This means it should either be a pre-baked string, or if a dynamic string
 * is required it must be created with kutf_dsprintf which will store
 * the resultant string in a buffer who's lifetime is the same as the test run.
 */
void kutf_test_debug(struct kutf_context *context, char const *message);

/**
 * kutf_test_info() - Send an information message
 * @context:	The test context this test is running in.
 * @message:	A message string containing the information message.
 *
 * Note: The message must not be freed during the lifetime of the test run.
 * This means it should either be a pre-baked string, or if a dynamic string
 * is required it must be created with kutf_dsprintf which will store
 * the resultant string in a buffer who's lifetime is the same as the test run.
 */
void kutf_test_info(struct kutf_context *context, char const *message);

/**
 * kutf_test_warn() - Send a warning message
 * @context:	The test context this test is running in.
 * @message:	A message string containing the warning message.
 *
 * Note: The message must not be freed during the lifetime of the test run.
 * This means it should either be a pre-baked string, or if a dynamic string
 * is required it must be created with kutf_dsprintf which will store
 * the resultant string in a buffer who's lifetime is the same as the test run.
 */
void kutf_test_warn(struct kutf_context *context, char const *message);

/**
 * kutf_test_fail() - Tell the kernel that a test has failed
 * @context:	The test context this test is running in.
 * @message:	A message string containing the failure message.
 *
 * Note: The message must not be freed during the lifetime of the test run.
 * This means it should either be a pre-baked string, or if a dynamic string
 * is required it must be created with kutf_dsprintf which will store
 * the resultant string in a buffer who's lifetime is the same as the test run.
 */
void kutf_test_fail(struct kutf_context *context, char const *message);

/**
 * kutf_test_fatal() - Tell the kernel that a test has triggered a fatal error
 * @context:	The test context this test is running in.
 * @message:	A message string containing the fatal error message.
 *
 * Note: The message must not be freed during the lifetime of the test run.
 * This means it should either be a pre-baked string, or if a dynamic string
 * is required it must be created with kutf_dsprintf which will store
 * the resultant string in a buffer who's lifetime is the same as the test run.
 */
void kutf_test_fatal(struct kutf_context *context, char const *message);

/**
 * kutf_test_abort() - Tell the kernel that a test triggered an abort in the test
 *
 * @context:	The test context this test is running in.
 */
void kutf_test_abort(struct kutf_context *context);

#endif	/* _KERNEL_UTF_SUITE_H_ */
