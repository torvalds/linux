// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014, 2017-2022 ARM Limited. All rights reserved.
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

/* Kernel UTF suite, test and fixture management including user to kernel
 * interaction
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/atomic.h>
#include <linux/sched.h>

#include <generated/autoconf.h>

#include <kutf/kutf_suite.h>
#include <kutf/kutf_resultset.h>
#include <kutf/kutf_utils.h>
#include <kutf/kutf_helpers.h>

/**
 * struct kutf_application - Structure which represents kutf application
 * @name:	The name of this test application.
 * @dir:	The debugfs directory for this test
 * @suite_list:	List head to store all the suites which are part of this
 *              application
 */
struct kutf_application {
	const char         *name;
	struct dentry      *dir;
	struct list_head   suite_list;
};

/**
 * struct kutf_test_function - Structure which represents kutf test function
 * @suite:		Back reference to the suite this test function
 *                      belongs to
 * @filters:		Filters that apply to this test function
 * @test_id:		Test ID
 * @execute:		Function to run for this test
 * @test_data:		Static data for this test
 * @node:		List node for test_list
 * @variant_list:	List head to store all the variants which can run on
 *                      this function
 * @dir:		debugfs directory for this test function
 */
struct kutf_test_function {
	struct kutf_suite  *suite;
	unsigned int       filters;
	unsigned int       test_id;
	void (*execute)(struct kutf_context *context);
	union kutf_callback_data test_data;
	struct list_head   node;
	struct list_head   variant_list;
	struct dentry      *dir;
};

/**
 * struct kutf_test_fixture - Structure which holds information on the kutf
 *                            test fixture
 * @test_func:		Test function this fixture belongs to
 * @fixture_index:	Index of this fixture
 * @node:		List node for variant_list
 * @dir:		debugfs directory for this test fixture
 */
struct kutf_test_fixture {
	struct kutf_test_function *test_func;
	unsigned int              fixture_index;
	struct list_head          node;
	struct dentry             *dir;
};

static struct dentry *base_dir;
static struct workqueue_struct *kutf_workq;

/**
 * struct kutf_convert_table - Structure which keeps test results
 * @result_name:	Status of the test result
 * @result:		Status value for a single test
 */
struct kutf_convert_table {
	char                    result_name[50];
	enum kutf_result_status result;
};

static const struct kutf_convert_table kutf_convert[] = {
#define ADD_UTF_RESULT(_name)                                                                      \
	{                                                                                          \
#_name, _name,                                                                     \
	}
	ADD_UTF_RESULT(KUTF_RESULT_BENCHMARK), ADD_UTF_RESULT(KUTF_RESULT_SKIP),
	ADD_UTF_RESULT(KUTF_RESULT_UNKNOWN),   ADD_UTF_RESULT(KUTF_RESULT_PASS),
	ADD_UTF_RESULT(KUTF_RESULT_DEBUG),     ADD_UTF_RESULT(KUTF_RESULT_INFO),
	ADD_UTF_RESULT(KUTF_RESULT_WARN),      ADD_UTF_RESULT(KUTF_RESULT_FAIL),
	ADD_UTF_RESULT(KUTF_RESULT_FATAL),     ADD_UTF_RESULT(KUTF_RESULT_ABORT),
};

#define UTF_CONVERT_SIZE (ARRAY_SIZE(kutf_convert))

/**
 * kutf_create_context() - Create a test context in which a specific fixture
 *                         of an application will be run and its results
 *                         reported back to the user
 * @test_fix:	Test fixture to be run.
 *
 * The context's refcount will be initialized to 1.
 *
 * Return: Returns the created test context on success or NULL on failure
 */
static struct kutf_context *kutf_create_context(
		struct kutf_test_fixture *test_fix);

/**
 * kutf_destroy_context() - Destroy a previously created test context, only
 *                          once its refcount has become zero
 * @kref:	pointer to kref member within the context
 *
 * This should only be used via a kref_put() call on the context's kref member
 */
static void kutf_destroy_context(struct kref *kref);

/**
 * kutf_context_get() - increment refcount on a context
 * @context:	the kutf context
 *
 * This must be used when the lifetime of the context might exceed that of the
 * thread creating @context
 */
static void kutf_context_get(struct kutf_context *context);

/**
 * kutf_context_put() - decrement refcount on a context, destroying it when it
 *                      reached zero
 * @context:	the kutf context
 *
 * This must be used only after a corresponding kutf_context_get() call on
 * @context, and the caller no longer needs access to @context.
 */
static void kutf_context_put(struct kutf_context *context);

/**
 * kutf_set_result() - Set the test result against the specified test context
 * @context:	Test context
 * @status:	Result status
 */
static void kutf_set_result(struct kutf_context *context,
		enum kutf_result_status status);

/**
 * kutf_set_expected_result() - Set the expected test result for the specified
 *                              test context
 * @context:		Test context
 * @expected_status:	Expected result status
 */
static void kutf_set_expected_result(struct kutf_context *context,
		enum kutf_result_status expected_status);

/**
 * kutf_result_to_string() - Converts a KUTF result into a string
 * @result_str:      Output result string
 * @result:          Result status to convert
 *
 * Return: 1 if test result was successfully converted to string, 0 otherwise
 */
static int kutf_result_to_string(const char **result_str, enum kutf_result_status result)
{
	int i;
	int ret = 0;

	for (i = 0; i < UTF_CONVERT_SIZE; i++) {
		if (result == kutf_convert[i].result) {
			*result_str = kutf_convert[i].result_name;
			ret = 1;
		}
	}
	return ret;
}

/**
 * kutf_debugfs_const_string_read() - Simple debugfs read callback which
 *                                    returns a constant string
 * @file:	Opened file to read from
 * @buf:	User buffer to write the data into
 * @len:	Amount of data to read
 * @ppos:	Offset into file to read from
 *
 * Return: On success, the number of bytes read and offset @ppos advanced by
 *         this number; on error, negative value
 */
static ssize_t kutf_debugfs_const_string_read(struct file *file,
		char __user *buf, size_t len, loff_t *ppos)
{
	char *str = file->private_data;

	return simple_read_from_buffer(buf, len, ppos, str, strlen(str));
}

static const struct file_operations kutf_debugfs_const_string_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = kutf_debugfs_const_string_read,
	.llseek  = default_llseek,
};

/**
 * kutf_add_explicit_result() - Check if an explicit result needs to be added
 * @context:	KUTF test context
 */
static void kutf_add_explicit_result(struct kutf_context *context)
{
	switch (context->expected_status) {
	case KUTF_RESULT_UNKNOWN:
		break;

	case KUTF_RESULT_WARN:
		if (context->status == KUTF_RESULT_WARN)
			kutf_test_pass(context,
					"Pass (expected warn occurred)");
		else if (context->status != KUTF_RESULT_SKIP)
			kutf_test_fail(context,
					"Fail (expected warn missing)");
		break;

	case KUTF_RESULT_FAIL:
		if (context->status == KUTF_RESULT_FAIL)
			kutf_test_pass(context,
					"Pass (expected fail occurred)");
		else if (context->status != KUTF_RESULT_SKIP) {
			/* Force the expected status so the fail gets logged */
			context->expected_status = KUTF_RESULT_PASS;
			kutf_test_fail(context,
					"Fail (expected fail missing)");
		}
		break;

	case KUTF_RESULT_FATAL:
		if (context->status == KUTF_RESULT_FATAL)
			kutf_test_pass(context,
					"Pass (expected fatal occurred)");
		else if (context->status != KUTF_RESULT_SKIP)
			kutf_test_fail(context,
					"Fail (expected fatal missing)");
		break;

	case KUTF_RESULT_ABORT:
		if (context->status == KUTF_RESULT_ABORT)
			kutf_test_pass(context,
					"Pass (expected abort occurred)");
		else if (context->status != KUTF_RESULT_SKIP)
			kutf_test_fail(context,
					"Fail (expected abort missing)");
		break;
	default:
		break;
	}
}

static void kutf_run_test(struct work_struct *data)
{
	struct kutf_context *test_context = container_of(data,
			struct kutf_context, work);
	struct kutf_suite *suite = test_context->suite;
	struct kutf_test_function *test_func;

	test_func = test_context->test_fix->test_func;

	/*
	 * Call the create fixture function if required before the
	 * fixture is run
	 */
	if (suite->create_fixture)
		test_context->fixture = suite->create_fixture(test_context);

	/* Only run the test if the fixture was created (if required) */
	if ((suite->create_fixture && test_context->fixture) ||
			(!suite->create_fixture)) {
		/* Run this fixture */
		test_func->execute(test_context);

		if (suite->remove_fixture)
			suite->remove_fixture(test_context);

		kutf_add_explicit_result(test_context);
	}

	kutf_add_result(test_context, KUTF_RESULT_TEST_FINISHED, NULL);

	kutf_context_put(test_context);
}

/**
 * kutf_debugfs_run_open() - Debugfs open callback for the "run" entry.
 *
 * @inode:	inode of the opened file
 * @file:	Opened file to read from
 *
 * This function creates a KUTF context and queues it onto a workqueue to be
 * run asynchronously. The resulting file descriptor can be used to communicate
 * userdata to the test and to read back the results of the test execution.
 *
 * Return: 0 on success
 */
static int kutf_debugfs_run_open(struct inode *inode, struct file *file)
{
	struct kutf_test_fixture *test_fix = inode->i_private;
	struct kutf_context *test_context;
	int err = 0;

	test_context = kutf_create_context(test_fix);
	if (!test_context) {
		err = -ENOMEM;
		goto finish;
	}

	file->private_data = test_context;

	/* This reference is release by the kutf_run_test */
	kutf_context_get(test_context);

	queue_work(kutf_workq, &test_context->work);

finish:
	return err;
}

#define USERDATA_WARNING_MESSAGE "WARNING: This test requires userdata\n"

/**
 * kutf_debugfs_run_read() - Debugfs read callback for the "run" entry.
 * @file:	Opened file to read from
 * @buf:	User buffer to write the data into
 * @len:	Amount of data to read
 * @ppos:	Offset into file to read from
 *
 * This function emits the results of the test, blocking until they are
 * available.
 *
 * If the test involves user data then this will also return user data records
 * to user space. If the test is waiting for user data then this function will
 * output a message (to make the likes of 'cat' display it), followed by
 * returning 0 to mark the end of file.
 *
 * Results will be emitted one at a time, once all the results have been read
 * 0 will be returned to indicate there is no more data.
 *
 * Return: Number of bytes read.
 */
static ssize_t kutf_debugfs_run_read(struct file *file, char __user *buf,
		size_t len, loff_t *ppos)
{
	struct kutf_context *test_context = file->private_data;
	struct kutf_result *res;
	unsigned long bytes_not_copied;
	ssize_t bytes_copied = 0;
	const char *kutf_str_ptr = NULL;
	size_t kutf_str_len = 0;
	size_t message_len = 0;
	char separator = ':';
	char terminator = '\n';

	res = kutf_remove_result(test_context->result_set);

	if (IS_ERR(res))
		return PTR_ERR(res);

	/*
	 * Handle 'fake' results - these results are converted to another
	 * form before being returned from the kernel
	 */
	switch (res->status) {
	case KUTF_RESULT_TEST_FINISHED:
		return 0;
	case KUTF_RESULT_USERDATA_WAIT:
		if (test_context->userdata.flags &
				KUTF_USERDATA_WARNING_OUTPUT) {
			/*
			 * Warning message already output,
			 * signal end-of-file
			 */
			return 0;
		}

		message_len = sizeof(USERDATA_WARNING_MESSAGE)-1;
		if (message_len > len)
			message_len = len;

		bytes_not_copied = copy_to_user(buf,
				USERDATA_WARNING_MESSAGE,
				message_len);
		if (bytes_not_copied != 0)
			return -EFAULT;
		test_context->userdata.flags |= KUTF_USERDATA_WARNING_OUTPUT;
		return message_len;
	case KUTF_RESULT_USERDATA:
		message_len = strlen(res->message);
		if (message_len > len-1) {
			message_len = len-1;
			pr_warn("User data truncated, read not long enough\n");
		}
		bytes_not_copied = copy_to_user(buf, res->message,
				message_len);
		if (bytes_not_copied != 0) {
			pr_warn("Failed to copy data to user space buffer\n");
			return -EFAULT;
		}
		/* Finally the terminator */
		bytes_not_copied = copy_to_user(&buf[message_len],
				&terminator, 1);
		if (bytes_not_copied != 0) {
			pr_warn("Failed to copy data to user space buffer\n");
			return -EFAULT;
		}
		return message_len+1;
	default:
		/* Fall through - this is a test result */
		break;
	}

	/* Note: This code assumes a result is read completely */
	kutf_result_to_string(&kutf_str_ptr, res->status);
	if (kutf_str_ptr)
		kutf_str_len = strlen(kutf_str_ptr);

	if (res->message)
		message_len = strlen(res->message);

	if ((kutf_str_len + 1 + message_len + 1) > len) {
		pr_err("Not enough space in user buffer for a single result");
		return 0;
	}

	/* First copy the result string */
	if (kutf_str_ptr) {
		bytes_not_copied = copy_to_user(&buf[0], kutf_str_ptr,
						kutf_str_len);
		bytes_copied += kutf_str_len - bytes_not_copied;
		if (bytes_not_copied)
			goto exit;
	}

	/* Then the separator */
	bytes_not_copied = copy_to_user(&buf[bytes_copied],
					&separator, 1);
	bytes_copied += 1 - bytes_not_copied;
	if (bytes_not_copied)
		goto exit;

	/* Finally Next copy the result string */
	if (res->message) {
		bytes_not_copied = copy_to_user(&buf[bytes_copied],
						res->message, message_len);
		bytes_copied += message_len - bytes_not_copied;
		if (bytes_not_copied)
			goto exit;
	}

	/* Finally the terminator */
	bytes_not_copied = copy_to_user(&buf[bytes_copied],
					&terminator, 1);
	bytes_copied += 1 - bytes_not_copied;

exit:
	return bytes_copied;
}

/**
 * kutf_debugfs_run_write() - Debugfs write callback for the "run" entry.
 * @file:	Opened file to write to
 * @buf:	User buffer to read the data from
 * @len:	Amount of data to write
 * @ppos:	Offset into file to write to
 *
 * This function allows user and kernel to exchange extra data necessary for
 * the test fixture.
 *
 * The data is added to the first struct kutf_context running the fixture
 *
 * Return: Number of bytes written
 */
static ssize_t kutf_debugfs_run_write(struct file *file,
		const char __user *buf, size_t len, loff_t *ppos)
{
	int ret = 0;
	struct kutf_context *test_context = file->private_data;

	if (len > KUTF_MAX_LINE_LENGTH)
		return -EINVAL;

	ret = kutf_helper_input_enqueue(test_context, buf, len);
	if (ret < 0)
		return ret;

	return len;
}

/**
 * kutf_debugfs_run_release() - Debugfs release callback for the "run" entry.
 * @inode:	File entry representation
 * @file:	A specific opening of the file
 *
 * Release any resources that were created during the opening of the file
 *
 * Note that resources may not be released immediately, that might only happen
 * later when other users of the kutf_context release their refcount.
 *
 * Return: 0 on success
 */
static int kutf_debugfs_run_release(struct inode *inode, struct file *file)
{
	struct kutf_context *test_context = file->private_data;

	kutf_helper_input_enqueue_end_of_data(test_context);

	kutf_context_put(test_context);
	return 0;
}

static const struct file_operations kutf_debugfs_run_ops = {
	.owner = THIS_MODULE,
	.open = kutf_debugfs_run_open,
	.read = kutf_debugfs_run_read,
	.write = kutf_debugfs_run_write,
	.release = kutf_debugfs_run_release,
	.llseek  = default_llseek,
};

/**
 * create_fixture_variant() - Creates a fixture variant for the specified
 *                            test function and index and the debugfs entries
 *                            that represent it.
 * @test_func:		Test function
 * @fixture_index:	Fixture index
 *
 * Return: 0 on success, negative value corresponding to error code in failure
 */
static int create_fixture_variant(struct kutf_test_function *test_func,
		unsigned int fixture_index)
{
	struct kutf_test_fixture *test_fix;
	char name[11];	/* Enough to print the MAX_UINT32 + the null terminator */
	struct dentry *tmp;
	int err;

	test_fix = kmalloc(sizeof(*test_fix), GFP_KERNEL);
	if (!test_fix) {
		pr_err("Failed to create debugfs directory when adding fixture\n");
		err = -ENOMEM;
		goto fail_alloc;
	}

	test_fix->test_func = test_func;
	test_fix->fixture_index = fixture_index;

	snprintf(name, sizeof(name), "%d", fixture_index);
	test_fix->dir = debugfs_create_dir(name, test_func->dir);
	if (IS_ERR_OR_NULL(test_func->dir)) {
		pr_err("Failed to create debugfs directory when adding fixture\n");
		/* Might not be the right error, we don't get it passed back to us */
		err = -EEXIST;
		goto fail_dir;
	}

	tmp = debugfs_create_file("type", 0004, test_fix->dir, "fixture\n",
				  &kutf_debugfs_const_string_ops);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("Failed to create debugfs file \"type\" when adding fixture\n");
		/* Might not be the right error, we don't get it passed back to us */
		err = -EEXIST;
		goto fail_file;
	}

	tmp = debugfs_create_file_unsafe(
			"run", 0600, test_fix->dir,
			test_fix,
			&kutf_debugfs_run_ops);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("Failed to create debugfs file \"run\" when adding fixture\n");
		/* Might not be the right error, we don't get it passed back to us */
		err = -EEXIST;
		goto fail_file;
	}

	list_add(&test_fix->node, &test_func->variant_list);
	return 0;

fail_file:
	debugfs_remove_recursive(test_fix->dir);
fail_dir:
	kfree(test_fix);
fail_alloc:
	return err;
}

/**
 * kutf_remove_test_variant() - Destroy a previously created fixture variant.
 * @test_fix:	Test fixture
 */
static void kutf_remove_test_variant(struct kutf_test_fixture *test_fix)
{
	debugfs_remove_recursive(test_fix->dir);
	kfree(test_fix);
}

#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
/* Adapting to the upstream debugfs_create_x32() change */
static int ktufp_u32_get(void *data, u64 *val)
{
	*val = *(u32 *)data;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(kutfp_fops_x32_ro, ktufp_u32_get, NULL, "0x%08llx\n");
#endif

void kutf_add_test_with_filters_and_data(
		struct kutf_suite *suite,
		unsigned int id,
		const char *name,
		void (*execute)(struct kutf_context *context),
		unsigned int filters,
		union kutf_callback_data test_data)
{
	struct kutf_test_function *test_func;
	struct dentry *tmp;
	unsigned int i;

	test_func = kmalloc(sizeof(*test_func), GFP_KERNEL);
	if (!test_func) {
		pr_err("Failed to allocate memory when adding test %s\n", name);
		goto fail_alloc;
	}

	INIT_LIST_HEAD(&test_func->variant_list);

	test_func->dir = debugfs_create_dir(name, suite->dir);
	if (IS_ERR_OR_NULL(test_func->dir)) {
		pr_err("Failed to create debugfs directory when adding test %s\n", name);
		goto fail_dir;
	}

	tmp = debugfs_create_file("type", 0004, test_func->dir, "test\n",
				  &kutf_debugfs_const_string_ops);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("Failed to create debugfs file \"type\" when adding test %s\n", name);
		goto fail_file;
	}

	test_func->filters = filters;
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
	tmp = debugfs_create_file_unsafe("filters", 0004, test_func->dir,
					 &test_func->filters, &kutfp_fops_x32_ro);
#else
	tmp = debugfs_create_x32("filters", 0004, test_func->dir,
				 &test_func->filters);
#endif
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("Failed to create debugfs file \"filters\" when adding test %s\n", name);
		goto fail_file;
	}

	test_func->test_id = id;
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
	debugfs_create_u32("test_id", 0004, test_func->dir,
		&test_func->test_id);
#else
	tmp = debugfs_create_u32("test_id", 0004, test_func->dir,
				 &test_func->test_id);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("Failed to create debugfs file \"test_id\" when adding test %s\n", name);
		goto fail_file;
	}
#endif

	for (i = 0; i < suite->fixture_variants; i++) {
		if (create_fixture_variant(test_func, i)) {
			pr_err("Failed to create fixture %d when adding test %s\n", i, name);
			goto fail_file;
		}
	}

	test_func->suite = suite;
	test_func->execute = execute;
	test_func->test_data = test_data;

	list_add(&test_func->node, &suite->test_list);
	return;

fail_file:
	debugfs_remove_recursive(test_func->dir);
fail_dir:
	kfree(test_func);
fail_alloc:
	return;
}
EXPORT_SYMBOL(kutf_add_test_with_filters_and_data);

void kutf_add_test_with_filters(
		struct kutf_suite *suite,
		unsigned int id,
		const char *name,
		void (*execute)(struct kutf_context *context),
		unsigned int filters)
{
	union kutf_callback_data data;

	data.ptr_value = NULL;

	kutf_add_test_with_filters_and_data(suite,
					    id,
					    name,
					    execute,
					    suite->suite_default_flags,
					    data);
}
EXPORT_SYMBOL(kutf_add_test_with_filters);

void kutf_add_test(struct kutf_suite *suite,
		unsigned int id,
		const char *name,
		void (*execute)(struct kutf_context *context))
{
	union kutf_callback_data data;

	data.ptr_value = NULL;

	kutf_add_test_with_filters_and_data(suite,
					    id,
					    name,
					    execute,
					    suite->suite_default_flags,
					    data);
}
EXPORT_SYMBOL(kutf_add_test);

/**
 * kutf_remove_test() - Remove a previously added test function.
 * @test_func: Test function
 */
static void kutf_remove_test(struct kutf_test_function *test_func)
{
	struct list_head *pos;
	struct list_head *tmp;

	list_for_each_safe(pos, tmp, &test_func->variant_list) {
		struct kutf_test_fixture *test_fix;

		test_fix = list_entry(pos, struct kutf_test_fixture, node);
		kutf_remove_test_variant(test_fix);
	}

	list_del(&test_func->node);
	debugfs_remove_recursive(test_func->dir);
	kfree(test_func);
}

struct kutf_suite *kutf_create_suite_with_filters_and_data(
		struct kutf_application *app,
		const char *name,
		unsigned int fixture_count,
		void *(*create_fixture)(struct kutf_context *context),
		void (*remove_fixture)(struct kutf_context *context),
		unsigned int filters,
		union kutf_callback_data suite_data)
{
	struct kutf_suite *suite;
	struct dentry *tmp;

	suite = kmalloc(sizeof(*suite), GFP_KERNEL);
	if (!suite) {
		pr_err("Failed to allocate memory when creating suite %s\n", name);
		goto fail_kmalloc;
	}

	suite->dir = debugfs_create_dir(name, app->dir);
	if (IS_ERR_OR_NULL(suite->dir)) {
		pr_err("Failed to create debugfs directory when adding test %s\n", name);
		goto fail_debugfs;
	}

	tmp = debugfs_create_file("type", 0004, suite->dir, "suite\n",
				  &kutf_debugfs_const_string_ops);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("Failed to create debugfs file \"type\" when adding test %s\n", name);
		goto fail_file;
	}

	INIT_LIST_HEAD(&suite->test_list);
	suite->app = app;
	suite->name = name;
	suite->fixture_variants = fixture_count;
	suite->create_fixture = create_fixture;
	suite->remove_fixture = remove_fixture;
	suite->suite_default_flags = filters;
	suite->suite_data = suite_data;

	list_add(&suite->node, &app->suite_list);

	return suite;

fail_file:
	debugfs_remove_recursive(suite->dir);
fail_debugfs:
	kfree(suite);
fail_kmalloc:
	return NULL;
}
EXPORT_SYMBOL(kutf_create_suite_with_filters_and_data);

struct kutf_suite *kutf_create_suite_with_filters(
		struct kutf_application *app,
		const char *name,
		unsigned int fixture_count,
		void *(*create_fixture)(struct kutf_context *context),
		void (*remove_fixture)(struct kutf_context *context),
		unsigned int filters)
{
	union kutf_callback_data data;

	data.ptr_value = NULL;
	return kutf_create_suite_with_filters_and_data(app,
						       name,
						       fixture_count,
						       create_fixture,
						       remove_fixture,
						       filters,
						       data);
}
EXPORT_SYMBOL(kutf_create_suite_with_filters);

struct kutf_suite *kutf_create_suite(
		struct kutf_application *app,
		const char *name,
		unsigned int fixture_count,
		void *(*create_fixture)(struct kutf_context *context),
		void (*remove_fixture)(struct kutf_context *context))
{
	union kutf_callback_data data;

	data.ptr_value = NULL;
	return kutf_create_suite_with_filters_and_data(app,
						       name,
						       fixture_count,
						       create_fixture,
						       remove_fixture,
						       KUTF_F_TEST_GENERIC,
						       data);
}
EXPORT_SYMBOL(kutf_create_suite);

/**
 * kutf_destroy_suite() - Destroy a previously added test suite.
 * @suite:	Test suite
 */
static void kutf_destroy_suite(struct kutf_suite *suite)
{
	struct list_head *pos;
	struct list_head *tmp;

	list_for_each_safe(pos, tmp, &suite->test_list) {
		struct kutf_test_function *test_func;

		test_func = list_entry(pos, struct kutf_test_function, node);
		kutf_remove_test(test_func);
	}

	list_del(&suite->node);
	debugfs_remove_recursive(suite->dir);
	kfree(suite);
}

struct kutf_application *kutf_create_application(const char *name)
{
	struct kutf_application *app;
	struct dentry *tmp;

	app = kmalloc(sizeof(*app), GFP_KERNEL);
	if (!app) {
		pr_err("Failed to create allocate memory when creating application %s\n", name);
		goto fail_kmalloc;
	}

	app->dir = debugfs_create_dir(name, base_dir);
	if (IS_ERR_OR_NULL(app->dir)) {
		pr_err("Failed to create debugfs direcotry when creating application %s\n", name);
		goto fail_debugfs;
	}

	tmp = debugfs_create_file("type", 0004, app->dir, "application\n",
				  &kutf_debugfs_const_string_ops);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("Failed to create debugfs file \"type\" when creating application %s\n", name);
		goto fail_file;
	}

	INIT_LIST_HEAD(&app->suite_list);
	app->name = name;

	return app;

fail_file:
	debugfs_remove_recursive(app->dir);
fail_debugfs:
	kfree(app);
fail_kmalloc:
	return NULL;
}
EXPORT_SYMBOL(kutf_create_application);

void kutf_destroy_application(struct kutf_application *app)
{
	struct list_head *pos;
	struct list_head *tmp;

	list_for_each_safe(pos, tmp, &app->suite_list) {
		struct kutf_suite *suite;

		suite = list_entry(pos, struct kutf_suite, node);
		kutf_destroy_suite(suite);
	}

	debugfs_remove_recursive(app->dir);
	kfree(app);
}
EXPORT_SYMBOL(kutf_destroy_application);

static struct kutf_context *kutf_create_context(
		struct kutf_test_fixture *test_fix)
{
	struct kutf_context *new_context;

	new_context = kmalloc(sizeof(*new_context), GFP_KERNEL);
	if (!new_context) {
		pr_err("Failed to allocate test context");
		goto fail_alloc;
	}

	new_context->result_set = kutf_create_result_set();
	if (!new_context->result_set) {
		pr_err("Failed to create result set");
		goto fail_result_set;
	}

	new_context->test_fix = test_fix;
	/* Save the pointer to the suite as the callbacks will require it */
	new_context->suite = test_fix->test_func->suite;
	new_context->status = KUTF_RESULT_UNKNOWN;
	new_context->expected_status = KUTF_RESULT_UNKNOWN;

	kutf_mempool_init(&new_context->fixture_pool);
	new_context->fixture = NULL;
	new_context->fixture_index = test_fix->fixture_index;
	new_context->fixture_name = NULL;
	new_context->test_data = test_fix->test_func->test_data;

	new_context->userdata.flags = 0;
	INIT_LIST_HEAD(&new_context->userdata.input_head);
	init_waitqueue_head(&new_context->userdata.input_waitq);

	INIT_WORK(&new_context->work, kutf_run_test);

	kref_init(&new_context->kref);

	return new_context;

fail_result_set:
	kfree(new_context);
fail_alloc:
	return NULL;
}

static void kutf_destroy_context(struct kref *kref)
{
	struct kutf_context *context;

	context = container_of(kref, struct kutf_context, kref);
	kutf_destroy_result_set(context->result_set);
	kutf_mempool_destroy(&context->fixture_pool);
	kfree(context);
}

static void kutf_context_get(struct kutf_context *context)
{
	kref_get(&context->kref);
}

static void kutf_context_put(struct kutf_context *context)
{
	kref_put(&context->kref, kutf_destroy_context);
}


static void kutf_set_result(struct kutf_context *context,
		enum kutf_result_status status)
{
	context->status = status;
}

static void kutf_set_expected_result(struct kutf_context *context,
		enum kutf_result_status expected_status)
{
	context->expected_status = expected_status;
}

/**
 * kutf_test_log_result() - Log a result for the specified test context
 * @context:	Test context
 * @message:	Result string
 * @new_status:	Result status
 */
static void kutf_test_log_result(
	struct kutf_context *context,
	const char *message,
	enum kutf_result_status new_status)
{
	if (context->status < new_status)
		context->status = new_status;

	if (context->expected_status != new_status)
		kutf_add_result(context, new_status, message);
}

void kutf_test_log_result_external(
	struct kutf_context *context,
	const char *message,
	enum kutf_result_status new_status)
{
	kutf_test_log_result(context, message, new_status);
}
EXPORT_SYMBOL(kutf_test_log_result_external);

void kutf_test_expect_abort(struct kutf_context *context)
{
	kutf_set_expected_result(context, KUTF_RESULT_ABORT);
}
EXPORT_SYMBOL(kutf_test_expect_abort);

void kutf_test_expect_fatal(struct kutf_context *context)
{
	kutf_set_expected_result(context, KUTF_RESULT_FATAL);
}
EXPORT_SYMBOL(kutf_test_expect_fatal);

void kutf_test_expect_fail(struct kutf_context *context)
{
	kutf_set_expected_result(context, KUTF_RESULT_FAIL);
}
EXPORT_SYMBOL(kutf_test_expect_fail);

void kutf_test_expect_warn(struct kutf_context *context)
{
	kutf_set_expected_result(context, KUTF_RESULT_WARN);
}
EXPORT_SYMBOL(kutf_test_expect_warn);

void kutf_test_expect_pass(struct kutf_context *context)
{
	kutf_set_expected_result(context, KUTF_RESULT_PASS);
}
EXPORT_SYMBOL(kutf_test_expect_pass);

void kutf_test_skip(struct kutf_context *context)
{
	kutf_set_result(context, KUTF_RESULT_SKIP);
	kutf_set_expected_result(context, KUTF_RESULT_UNKNOWN);

	kutf_test_log_result(context, "Test skipped", KUTF_RESULT_SKIP);
}
EXPORT_SYMBOL(kutf_test_skip);

void kutf_test_skip_msg(struct kutf_context *context, const char *message)
{
	kutf_set_result(context, KUTF_RESULT_SKIP);
	kutf_set_expected_result(context, KUTF_RESULT_UNKNOWN);

	kutf_test_log_result(context, kutf_dsprintf(&context->fixture_pool,
			     "Test skipped: %s", message), KUTF_RESULT_SKIP);
	kutf_test_log_result(context, "!!!Test skipped!!!", KUTF_RESULT_SKIP);
}
EXPORT_SYMBOL(kutf_test_skip_msg);

void kutf_test_debug(struct kutf_context *context, char const *message)
{
	kutf_test_log_result(context, message, KUTF_RESULT_DEBUG);
}
EXPORT_SYMBOL(kutf_test_debug);

void kutf_test_pass(struct kutf_context *context, char const *message)
{
	static const char explicit_message[] = "(explicit pass)";

	if (!message)
		message = explicit_message;

	kutf_test_log_result(context, message, KUTF_RESULT_PASS);
}
EXPORT_SYMBOL(kutf_test_pass);

void kutf_test_info(struct kutf_context *context, char const *message)
{
	kutf_test_log_result(context, message, KUTF_RESULT_INFO);
}
EXPORT_SYMBOL(kutf_test_info);

void kutf_test_warn(struct kutf_context *context, char const *message)
{
	kutf_test_log_result(context, message, KUTF_RESULT_WARN);
}
EXPORT_SYMBOL(kutf_test_warn);

void kutf_test_fail(struct kutf_context *context, char const *message)
{
	kutf_test_log_result(context, message, KUTF_RESULT_FAIL);
}
EXPORT_SYMBOL(kutf_test_fail);

void kutf_test_fatal(struct kutf_context *context, char const *message)
{
	kutf_test_log_result(context, message, KUTF_RESULT_FATAL);
}
EXPORT_SYMBOL(kutf_test_fatal);

void kutf_test_abort(struct kutf_context *context)
{
	kutf_test_log_result(context, "", KUTF_RESULT_ABORT);
}
EXPORT_SYMBOL(kutf_test_abort);

#if IS_ENABLED(CONFIG_DEBUG_FS)

/**
 * init_kutf_core() - Module entry point.
 * Create the base entry point in debugfs.
 *
 * Return: 0 on success, error code otherwise.
 */
static int __init init_kutf_core(void)
{
	kutf_workq = alloc_workqueue("kutf workq", WQ_UNBOUND, 1);
	if (!kutf_workq)
		return -ENOMEM;

	base_dir = debugfs_create_dir("kutf_tests", NULL);
	if (IS_ERR_OR_NULL(base_dir)) {
		destroy_workqueue(kutf_workq);
		kutf_workq = NULL;
		return -ENOMEM;
	}

	return 0;
}

/**
 * exit_kutf_core() - Module exit point.
 *
 * Remove the base entry point in debugfs.
 */
static void __exit exit_kutf_core(void)
{
	debugfs_remove_recursive(base_dir);

	if (kutf_workq)
		destroy_workqueue(kutf_workq);
}

#else	/* CONFIG_DEBUG_FS */

/**
 * init_kutf_core - Module entry point
 * Stub for when build against a kernel without debugfs support.
 *
 * Return: -ENODEV
 */
static int __init init_kutf_core(void)
{
	pr_debug("KUTF requires a kernel with debug fs support");

	return -ENODEV;
}

/**
 * exit_kutf_core() - Module exit point.
 *
 * Stub for when build against a kernel without debugfs support
 */
static void __exit exit_kutf_core(void)
{
}
#endif	/* CONFIG_DEBUG_FS */

MODULE_LICENSE("GPL");

module_init(init_kutf_core);
module_exit(exit_kutf_core);
