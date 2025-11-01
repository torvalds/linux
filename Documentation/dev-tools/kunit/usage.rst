.. SPDX-License-Identifier: GPL-2.0

Writing Tests
=============

Test Cases
----------

The fundamental unit in KUnit is the test case. A test case is a function with
the signature ``void (*)(struct kunit *test)``. It calls the function under test
and then sets *expectations* for what should happen. For example:

.. code-block:: c

	void example_test_success(struct kunit *test)
	{
	}

	void example_test_failure(struct kunit *test)
	{
		KUNIT_FAIL(test, "This test never passes.");
	}

In the above example, ``example_test_success`` always passes because it does
nothing; no expectations are set, and therefore all expectations pass. On the
other hand ``example_test_failure`` always fails because it calls ``KUNIT_FAIL``,
which is a special expectation that logs a message and causes the test case to
fail.

Expectations
~~~~~~~~~~~~
An *expectation* specifies that we expect a piece of code to do something in a
test. An expectation is called like a function. A test is made by setting
expectations about the behavior of a piece of code under test. When one or more
expectations fail, the test case fails and information about the failure is
logged. For example:

.. code-block:: c

	void add_test_basic(struct kunit *test)
	{
		KUNIT_EXPECT_EQ(test, 1, add(1, 0));
		KUNIT_EXPECT_EQ(test, 2, add(1, 1));
	}

In the above example, ``add_test_basic`` makes a number of assertions about the
behavior of a function called ``add``. The first parameter is always of type
``struct kunit *``, which contains information about the current test context.
The second parameter, in this case, is what the value is expected to be. The
last value is what the value actually is. If ``add`` passes all of these
expectations, the test case, ``add_test_basic`` will pass; if any one of these
expectations fails, the test case will fail.

A test case *fails* when any expectation is violated; however, the test will
continue to run, and try other expectations until the test case ends or is
otherwise terminated. This is as opposed to *assertions* which are discussed
later.

To learn about more KUnit expectations, see Documentation/dev-tools/kunit/api/test.rst.

.. note::
   A single test case should be short, easy to understand, and focused on a
   single behavior.

For example, if we want to rigorously test the ``add`` function above, create
additional tests cases which would test each property that an ``add`` function
should have as shown below:

.. code-block:: c

	void add_test_basic(struct kunit *test)
	{
		KUNIT_EXPECT_EQ(test, 1, add(1, 0));
		KUNIT_EXPECT_EQ(test, 2, add(1, 1));
	}

	void add_test_negative(struct kunit *test)
	{
		KUNIT_EXPECT_EQ(test, 0, add(-1, 1));
	}

	void add_test_max(struct kunit *test)
	{
		KUNIT_EXPECT_EQ(test, INT_MAX, add(0, INT_MAX));
		KUNIT_EXPECT_EQ(test, -1, add(INT_MAX, INT_MIN));
	}

	void add_test_overflow(struct kunit *test)
	{
		KUNIT_EXPECT_EQ(test, INT_MIN, add(INT_MAX, 1));
	}

Assertions
~~~~~~~~~~

An assertion is like an expectation, except that the assertion immediately
terminates the test case if the condition is not satisfied. For example:

.. code-block:: c

	static void test_sort(struct kunit *test)
	{
		int *a, i, r = 1;
		a = kunit_kmalloc_array(test, TEST_LEN, sizeof(*a), GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, a);
		for (i = 0; i < TEST_LEN; i++) {
			r = (r * 725861) % 6599;
			a[i] = r;
		}
		sort(a, TEST_LEN, sizeof(*a), cmpint, NULL);
		for (i = 0; i < TEST_LEN-1; i++)
			KUNIT_EXPECT_LE(test, a[i], a[i + 1]);
	}

In this example, we need to be able to allocate an array to test the ``sort()``
function. So we use ``KUNIT_ASSERT_NOT_ERR_OR_NULL()`` to abort the test if
there's an allocation error.

.. note::
   In other test frameworks, ``ASSERT`` macros are often implemented by calling
   ``return`` so they only work from the test function. In KUnit, we stop the
   current kthread on failure, so you can call them from anywhere.

.. note::
   Warning: There is an exception to the above rule. You shouldn't use assertions
   in the suite's exit() function, or in the free function for a resource. These
   run when a test is shutting down, and an assertion here prevents further
   cleanup code from running, potentially leading to a memory leak.

Customizing error messages
--------------------------

Each of the ``KUNIT_EXPECT`` and ``KUNIT_ASSERT`` macros have a ``_MSG``
variant.  These take a format string and arguments to provide additional
context to the automatically generated error messages.

.. code-block:: c

	char some_str[41];
	generate_sha1_hex_string(some_str);

	/* Before. Not easy to tell why the test failed. */
	KUNIT_EXPECT_EQ(test, strlen(some_str), 40);

	/* After. Now we see the offending string. */
	KUNIT_EXPECT_EQ_MSG(test, strlen(some_str), 40, "some_str='%s'", some_str);

Alternatively, one can take full control over the error message by using
``KUNIT_FAIL()``, e.g.

.. code-block:: c

	/* Before */
	KUNIT_EXPECT_EQ(test, some_setup_function(), 0);

	/* After: full control over the failure message. */
	if (some_setup_function())
		KUNIT_FAIL(test, "Failed to setup thing for testing");


Test Suites
~~~~~~~~~~~

We need many test cases covering all the unit's behaviors. It is common to have
many similar tests. In order to reduce duplication in these closely related
tests, most unit testing frameworks (including KUnit) provide the concept of a
*test suite*. A test suite is a collection of test cases for a unit of code
with optional setup and teardown functions that run before/after the whole
suite and/or every test case.

.. note::
   A test case will only run if it is associated with a test suite.

For example:

.. code-block:: c

	static struct kunit_case example_test_cases[] = {
		KUNIT_CASE(example_test_foo),
		KUNIT_CASE(example_test_bar),
		KUNIT_CASE(example_test_baz),
		{}
	};

	static struct kunit_suite example_test_suite = {
		.name = "example",
		.init = example_test_init,
		.exit = example_test_exit,
		.suite_init = example_suite_init,
		.suite_exit = example_suite_exit,
		.test_cases = example_test_cases,
	};
	kunit_test_suite(example_test_suite);

In the above example, the test suite ``example_test_suite`` would first run
``example_suite_init``, then run the test cases ``example_test_foo``,
``example_test_bar``, and ``example_test_baz``. Each would have
``example_test_init`` called immediately before it and ``example_test_exit``
called immediately after it. Finally, ``example_suite_exit`` would be called
after everything else. ``kunit_test_suite(example_test_suite)`` registers the
test suite with the KUnit test framework.

.. note::
   The ``exit`` and ``suite_exit`` functions will run even if ``init`` or
   ``suite_init`` fail. Make sure that they can handle any inconsistent
   state which may result from ``init`` or ``suite_init`` encountering errors
   or exiting early.

``kunit_test_suite(...)`` is a macro which tells the linker to put the
specified test suite in a special linker section so that it can be run by KUnit
either after ``late_init``, or when the test module is loaded (if the test was
built as a module).

For more information, see Documentation/dev-tools/kunit/api/test.rst.

.. _kunit-on-non-uml:

Writing Tests For Other Architectures
-------------------------------------

It is better to write tests that run on UML to tests that only run under a
particular architecture. It is better to write tests that run under QEMU or
another easy to obtain (and monetarily free) software environment to a specific
piece of hardware.

Nevertheless, there are still valid reasons to write a test that is architecture
or hardware specific. For example, we might want to test code that really
belongs in ``arch/some-arch/*``. Even so, try to write the test so that it does
not depend on physical hardware. Some of our test cases may not need hardware,
only few tests actually require the hardware to test it. When hardware is not
available, instead of disabling tests, we can skip them.

Now that we have narrowed down exactly what bits are hardware specific, the
actual procedure for writing and running the tests is same as writing normal
KUnit tests.

.. important::
   We may have to reset hardware state. If this is not possible, we may only
   be able to run one test case per invocation.

.. TODO(brendanhiggins@google.com): Add an actual example of an architecture-
   dependent KUnit test.

Common Patterns
===============

Isolating Behavior
------------------

Unit testing limits the amount of code under test to a single unit. It controls
what code gets run when the unit under test calls a function. Where a function
is exposed as part of an API such that the definition of that function can be
changed without affecting the rest of the code base. In the kernel, this comes
from two constructs: classes, which are structs that contain function pointers
provided by the implementer, and architecture-specific functions, which have
definitions selected at compile time.

Classes
~~~~~~~

Classes are not a construct that is built into the C programming language;
however, it is an easily derived concept. Accordingly, in most cases, every
project that does not use a standardized object oriented library (like GNOME's
GObject) has their own slightly different way of doing object oriented
programming; the Linux kernel is no exception.

The central concept in kernel object oriented programming is the class. In the
kernel, a *class* is a struct that contains function pointers. This creates a
contract between *implementers* and *users* since it forces them to use the
same function signature without having to call the function directly. To be a
class, the function pointers must specify that a pointer to the class, known as
a *class handle*, be one of the parameters. Thus the member functions (also
known as *methods*) have access to member variables (also known as *fields*)
allowing the same implementation to have multiple *instances*.

A class can be *overridden* by *child classes* by embedding the *parent class*
in the child class. Then when the child class *method* is called, the child
implementation knows that the pointer passed to it is of a parent contained
within the child. Thus, the child can compute the pointer to itself because the
pointer to the parent is always a fixed offset from the pointer to the child.
This offset is the offset of the parent contained in the child struct. For
example:

.. code-block:: c

	struct shape {
		int (*area)(struct shape *this);
	};

	struct rectangle {
		struct shape parent;
		int length;
		int width;
	};

	int rectangle_area(struct shape *this)
	{
		struct rectangle *self = container_of(this, struct rectangle, parent);

		return self->length * self->width;
	};

	void rectangle_new(struct rectangle *self, int length, int width)
	{
		self->parent.area = rectangle_area;
		self->length = length;
		self->width = width;
	}

In this example, computing the pointer to the child from the pointer to the
parent is done by ``container_of``.

Faking Classes
~~~~~~~~~~~~~~

In order to unit test a piece of code that calls a method in a class, the
behavior of the method must be controllable, otherwise the test ceases to be a
unit test and becomes an integration test.

A fake class implements a piece of code that is different than what runs in a
production instance, but behaves identical from the standpoint of the callers.
This is done to replace a dependency that is hard to deal with, or is slow. For
example, implementing a fake EEPROM that stores the "contents" in an
internal buffer. Assume we have a class that represents an EEPROM:

.. code-block:: c

	struct eeprom {
		ssize_t (*read)(struct eeprom *this, size_t offset, char *buffer, size_t count);
		ssize_t (*write)(struct eeprom *this, size_t offset, const char *buffer, size_t count);
	};

And we want to test code that buffers writes to the EEPROM:

.. code-block:: c

	struct eeprom_buffer {
		ssize_t (*write)(struct eeprom_buffer *this, const char *buffer, size_t count);
		int flush(struct eeprom_buffer *this);
		size_t flush_count; /* Flushes when buffer exceeds flush_count. */
	};

	struct eeprom_buffer *new_eeprom_buffer(struct eeprom *eeprom);
	void destroy_eeprom_buffer(struct eeprom *eeprom);

We can test this code by *faking out* the underlying EEPROM:

.. code-block:: c

	struct fake_eeprom {
		struct eeprom parent;
		char contents[FAKE_EEPROM_CONTENTS_SIZE];
	};

	ssize_t fake_eeprom_read(struct eeprom *parent, size_t offset, char *buffer, size_t count)
	{
		struct fake_eeprom *this = container_of(parent, struct fake_eeprom, parent);

		count = min(count, FAKE_EEPROM_CONTENTS_SIZE - offset);
		memcpy(buffer, this->contents + offset, count);

		return count;
	}

	ssize_t fake_eeprom_write(struct eeprom *parent, size_t offset, const char *buffer, size_t count)
	{
		struct fake_eeprom *this = container_of(parent, struct fake_eeprom, parent);

		count = min(count, FAKE_EEPROM_CONTENTS_SIZE - offset);
		memcpy(this->contents + offset, buffer, count);

		return count;
	}

	void fake_eeprom_init(struct fake_eeprom *this)
	{
		this->parent.read = fake_eeprom_read;
		this->parent.write = fake_eeprom_write;
		memset(this->contents, 0, FAKE_EEPROM_CONTENTS_SIZE);
	}

We can now use it to test ``struct eeprom_buffer``:

.. code-block:: c

	struct eeprom_buffer_test {
		struct fake_eeprom *fake_eeprom;
		struct eeprom_buffer *eeprom_buffer;
	};

	static void eeprom_buffer_test_does_not_write_until_flush(struct kunit *test)
	{
		struct eeprom_buffer_test *ctx = test->priv;
		struct eeprom_buffer *eeprom_buffer = ctx->eeprom_buffer;
		struct fake_eeprom *fake_eeprom = ctx->fake_eeprom;
		char buffer[] = {0xff};

		eeprom_buffer->flush_count = SIZE_MAX;

		eeprom_buffer->write(eeprom_buffer, buffer, 1);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[0], 0);

		eeprom_buffer->write(eeprom_buffer, buffer, 1);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[1], 0);

		eeprom_buffer->flush(eeprom_buffer);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[0], 0xff);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[1], 0xff);
	}

	static void eeprom_buffer_test_flushes_after_flush_count_met(struct kunit *test)
	{
		struct eeprom_buffer_test *ctx = test->priv;
		struct eeprom_buffer *eeprom_buffer = ctx->eeprom_buffer;
		struct fake_eeprom *fake_eeprom = ctx->fake_eeprom;
		char buffer[] = {0xff};

		eeprom_buffer->flush_count = 2;

		eeprom_buffer->write(eeprom_buffer, buffer, 1);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[0], 0);

		eeprom_buffer->write(eeprom_buffer, buffer, 1);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[0], 0xff);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[1], 0xff);
	}

	static void eeprom_buffer_test_flushes_increments_of_flush_count(struct kunit *test)
	{
		struct eeprom_buffer_test *ctx = test->priv;
		struct eeprom_buffer *eeprom_buffer = ctx->eeprom_buffer;
		struct fake_eeprom *fake_eeprom = ctx->fake_eeprom;
		char buffer[] = {0xff, 0xff};

		eeprom_buffer->flush_count = 2;

		eeprom_buffer->write(eeprom_buffer, buffer, 1);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[0], 0);

		eeprom_buffer->write(eeprom_buffer, buffer, 2);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[0], 0xff);
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[1], 0xff);
		/* Should have only flushed the first two bytes. */
		KUNIT_EXPECT_EQ(test, fake_eeprom->contents[2], 0);
	}

	static int eeprom_buffer_test_init(struct kunit *test)
	{
		struct eeprom_buffer_test *ctx;

		ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

		ctx->fake_eeprom = kunit_kzalloc(test, sizeof(*ctx->fake_eeprom), GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->fake_eeprom);
		fake_eeprom_init(ctx->fake_eeprom);

		ctx->eeprom_buffer = new_eeprom_buffer(&ctx->fake_eeprom->parent);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->eeprom_buffer);

		test->priv = ctx;

		return 0;
	}

	static void eeprom_buffer_test_exit(struct kunit *test)
	{
		struct eeprom_buffer_test *ctx = test->priv;

		destroy_eeprom_buffer(ctx->eeprom_buffer);
	}

Testing Against Multiple Inputs
-------------------------------

Testing just a few inputs is not enough to ensure that the code works correctly,
for example: testing a hash function.

We can write a helper macro or function. The function is called for each input.
For example, to test ``sha1sum(1)``, we can write:

.. code-block:: c

	#define TEST_SHA1(in, want) \
		sha1sum(in, out); \
		KUNIT_EXPECT_STREQ_MSG(test, out, want, "sha1sum(%s)", in);

	char out[40];
	TEST_SHA1("hello world",  "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
	TEST_SHA1("hello world!", "430ce34d020724ed75a196dfc2ad67c77772d169");

Note the use of the ``_MSG`` version of ``KUNIT_EXPECT_STREQ`` to print a more
detailed error and make the assertions clearer within the helper macros.

The ``_MSG`` variants are useful when the same expectation is called multiple
times (in a loop or helper function) and thus the line number is not enough to
identify what failed, as shown below.

In complicated cases, we recommend using a *table-driven test* compared to the
helper macro variation, for example:

.. code-block:: c

	int i;
	char out[40];

	struct sha1_test_case {
		const char *str;
		const char *sha1;
	};

	struct sha1_test_case cases[] = {
		{
			.str = "hello world",
			.sha1 = "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed",
		},
		{
			.str = "hello world!",
			.sha1 = "430ce34d020724ed75a196dfc2ad67c77772d169",
		},
	};
	for (i = 0; i < ARRAY_SIZE(cases); ++i) {
		sha1sum(cases[i].str, out);
		KUNIT_EXPECT_STREQ_MSG(test, out, cases[i].sha1,
		                      "sha1sum(%s)", cases[i].str);
	}


There is more boilerplate code involved, but it can:

* be more readable when there are multiple inputs/outputs (due to field names).

  * For example, see ``fs/ext4/inode-test.c``.

* reduce duplication if test cases are shared across multiple tests.

  * For example: if we want to test ``sha256sum``, we could add a ``sha256``
    field and reuse ``cases``.

* be converted to a "parameterized test".

Parameterized Testing
~~~~~~~~~~~~~~~~~~~~~

To run a test case against multiple inputs, KUnit provides a parameterized
testing framework. This feature formalizes and extends the concept of
table-driven tests discussed previously.

A KUnit test is determined to be parameterized if a parameter generator function
is provided when registering the test case. A test user can either write their
own generator function or use one that is provided by KUnit. The generator
function is stored in  ``kunit_case->generate_params`` and can be set using the
macros described in the section below.

To establish the terminology, a "parameterized test" is a test which is run
multiple times (once per "parameter" or "parameter run"). Each parameter run has
both its own independent ``struct kunit`` (the "parameter run context") and
access to a shared parent ``struct kunit`` (the "parameterized test context").

Passing Parameters to a Test
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
There are three ways to provide the parameters to a test:

Array Parameter Macros:

   KUnit provides special support for the common table-driven testing pattern.
   By applying either ``KUNIT_ARRAY_PARAM`` or ``KUNIT_ARRAY_PARAM_DESC`` to the
   ``cases`` array from the previous section, we can create a parameterized test
   as shown below:

.. code-block:: c

	// This is copy-pasted from above.
	struct sha1_test_case {
		const char *str;
		const char *sha1;
	};
	static const struct sha1_test_case cases[] = {
		{
			.str = "hello world",
			.sha1 = "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed",
		},
		{
			.str = "hello world!",
			.sha1 = "430ce34d020724ed75a196dfc2ad67c77772d169",
		},
	};

	// Creates `sha1_gen_params()` to iterate over `cases` while using
	// the struct member `str` for the case description.
	KUNIT_ARRAY_PARAM_DESC(sha1, cases, str);

	// Looks no different from a normal test.
	static void sha1_test(struct kunit *test)
	{
		// This function can just contain the body of the for-loop.
		// The former `cases[i]` is accessible under test->param_value.
		char out[40];
		struct sha1_test_case *test_param = (struct sha1_test_case *)(test->param_value);

		sha1sum(test_param->str, out);
		KUNIT_EXPECT_STREQ_MSG(test, out, test_param->sha1,
				      "sha1sum(%s)", test_param->str);
	}

	// Instead of KUNIT_CASE, we use KUNIT_CASE_PARAM and pass in the
	// function declared by KUNIT_ARRAY_PARAM or KUNIT_ARRAY_PARAM_DESC.
	static struct kunit_case sha1_test_cases[] = {
		KUNIT_CASE_PARAM(sha1_test, sha1_gen_params),
		{}
	};

Custom Parameter Generator Function:

   The generator function is responsible for generating parameters one-by-one
   and has the following signature:
   ``const void* (*)(struct kunit *test, const void *prev, char *desc)``.
   You can pass the generator function to the ``KUNIT_CASE_PARAM``
   or ``KUNIT_CASE_PARAM_WITH_INIT`` macros.

   The function receives the previously generated parameter as the ``prev`` argument
   (which is ``NULL`` on the first call) and can also access the parameterized
   test context passed as the ``test`` argument. KUnit calls this function
   repeatedly until it returns ``NULL``, which signifies that a parameterized
   test ended.

   Below is an example of how it works:

.. code-block:: c

	#define MAX_TEST_BUFFER_SIZE 8

	// Example generator function. It produces a sequence of buffer sizes that
	// are powers of two, starting at 1 (e.g., 1, 2, 4, 8).
	static const void *buffer_size_gen_params(struct kunit *test, const void *prev, char *desc)
	{
		long prev_buffer_size = (long)prev;
		long next_buffer_size = 1; // Start with an initial size of 1.

		// Stop generating parameters if the limit is reached or exceeded.
		if (prev_buffer_size >= MAX_TEST_BUFFER_SIZE)
			return NULL;

		// For subsequent calls, calculate the next size by doubling the previous one.
		if (prev)
			next_buffer_size = prev_buffer_size << 1;

		return (void *)next_buffer_size;
	}

	// Simple test to validate that kunit_kzalloc provides zeroed memory.
	static void buffer_zero_test(struct kunit *test)
	{
		long buffer_size = (long)test->param_value;
		// Use kunit_kzalloc to allocate a zero-initialized buffer. This makes the
		// memory "parameter run managed," meaning it's automatically cleaned up at
		// the end of each parameter run.
		int *buf = kunit_kzalloc(test, buffer_size * sizeof(int), GFP_KERNEL);

		// Ensure the allocation was successful.
		KUNIT_ASSERT_NOT_NULL(test, buf);

		// Loop through the buffer and confirm every element is zero.
		for (int i = 0; i < buffer_size; i++)
			KUNIT_EXPECT_EQ(test, buf[i], 0);
	}

	static struct kunit_case buffer_test_cases[] = {
		KUNIT_CASE_PARAM(buffer_zero_test, buffer_size_gen_params),
		{}
	};

Runtime Parameter Array Registration in the Init Function:

   For scenarios where you might need to initialize a parameterized test, you
   can directly register a parameter array to the parameterized test context.

   To do this, you must pass the parameterized test context, the array itself,
   the array size, and a ``get_description()`` function to the
   ``kunit_register_params_array()`` macro. This macro populates
   ``struct kunit_params`` within the parameterized test context, effectively
   storing a parameter array object. The ``get_description()`` function will
   be used for populating parameter descriptions and has the following signature:
   ``void (*)(struct kunit *test, const void *param, char *desc)``. Note that it
   also has access to the parameterized test context.

      .. important::
         When using this way to register a parameter array, you will need to
         manually pass ``kunit_array_gen_params()`` as the generator function to
         ``KUNIT_CASE_PARAM_WITH_INIT``. ``kunit_array_gen_params()`` is a KUnit
         helper that will use the registered array to generate the parameters.

	 If needed, instead of passing the KUnit helper, you can also pass your
	 own custom generator function that utilizes the parameter array. To
	 access the parameter array from within the parameter generator
	 function use ``test->params_array.params``.

   The ``kunit_register_params_array()`` macro should be called within a
   ``param_init()`` function that initializes the parameterized test and has
   the following signature ``int (*)(struct kunit *test)``. For a detailed
   explanation of this mechanism please refer to the "Adding Shared Resources"
   section that is after this one. This method supports registering both
   dynamically built and static parameter arrays.

   The code snippet below shows the ``example_param_init_dynamic_arr`` test that
   utilizes ``make_fibonacci_params()`` to create a dynamic array, which is then
   registered using ``kunit_register_params_array()``. To see the full code
   please refer to lib/kunit/kunit-example-test.c.

.. code-block:: c

	/*
	* Example of a parameterized test param_init() function that registers a dynamic
	* array of parameters.
	*/
	static int example_param_init_dynamic_arr(struct kunit *test)
	{
		size_t seq_size;
		int *fibonacci_params;

		kunit_info(test, "initializing parameterized test\n");

		seq_size = 6;
		fibonacci_params = make_fibonacci_params(test, seq_size);
		if (!fibonacci_params)
			return -ENOMEM;
		/*
		* Passes the dynamic parameter array information to the parameterized test
		* context struct kunit. The array and its metadata will be stored in
		* test->parent->params_array. The array itself will be located in
		* params_data.params.
		*/
		kunit_register_params_array(test, fibonacci_params, seq_size,
					example_param_dynamic_arr_get_desc);
		return 0;
	}

	static struct kunit_case example_test_cases[] = {
		/*
		 * Note how we pass kunit_array_gen_params() to use the array we
		 * registered in example_param_init_dynamic_arr() to generate
		 * parameters.
		 */
		KUNIT_CASE_PARAM_WITH_INIT(example_params_test_with_init_dynamic_arr,
					   kunit_array_gen_params,
					   example_param_init_dynamic_arr,
					   example_param_exit_dynamic_arr),
		{}
	};

Adding Shared Resources
^^^^^^^^^^^^^^^^^^^^^^^
All parameter runs in this framework hold a reference to the parameterized test
context, which can be accessed using the parent ``struct kunit`` pointer. The
parameterized test context is not used to execute any test logic itself; instead,
it serves as a container for shared resources.

It's possible to add resources to share between parameter runs within a
parameterized test by using ``KUNIT_CASE_PARAM_WITH_INIT``, to which you pass
custom ``param_init()`` and ``param_exit()`` functions. These functions run once
before and once after the parameterized test, respectively.

The ``param_init()`` function, with the signature ``int (*)(struct kunit *test)``,
can be used for adding resources to the ``resources`` or ``priv`` fields of
the parameterized test context, registering the parameter array, and any other
initialization logic.

The ``param_exit()`` function, with the signature ``void (*)(struct kunit *test)``,
can be used to release any resources that were not parameterized test managed (i.e.
not automatically cleaned up after the parameterized test ends) and for any other
exit logic.

Both ``param_init()`` and ``param_exit()`` are passed the parameterized test
context behind the scenes. However, the test case function receives the parameter
run context. Therefore, to manage and access shared resources from within a test
case function, you must use ``test->parent``.

For instance, finding a shared resource allocated by the Resource API requires
passing ``test->parent`` to ``kunit_find_resource()``. This principle extends to
all other APIs that might be used in the test case function, including
``kunit_kzalloc()``, ``kunit_kmalloc_array()``, and others (see
Documentation/dev-tools/kunit/api/test.rst and the
Documentation/dev-tools/kunit/api/resource.rst).

.. note::
   The ``suite->init()`` function, which executes before each parameter run,
   receives the parameter run context. Therefore, any resources set up in
   ``suite->init()`` are cleaned up after each parameter run.

The code below shows how you can add the shared resources. Note that this code
utilizes the Resource API, which you can read more about here:
Documentation/dev-tools/kunit/api/resource.rst. To see the full version of this
code please refer to lib/kunit/kunit-example-test.c.

.. code-block:: c

	static int example_resource_init(struct kunit_resource *res, void *context)
	{
		... /* Code that allocates memory and stores context in res->data. */
	}

	/* This function deallocates memory for the kunit_resource->data field. */
	static void example_resource_free(struct kunit_resource *res)
	{
		kfree(res->data);
	}

	/* This match function locates a test resource based on defined criteria. */
	static bool example_resource_alloc_match(struct kunit *test, struct kunit_resource *res,
						 void *match_data)
	{
		return res->data && res->free == example_resource_free;
	}

	/* Function to initialize the parameterized test. */
	static int example_param_init(struct kunit *test)
	{
		int ctx = 3; /* Data to be stored. */
		void *data = kunit_alloc_resource(test, example_resource_init,
						  example_resource_free,
						  GFP_KERNEL, &ctx);
		if (!data)
			return -ENOMEM;
		kunit_register_params_array(test, example_params_array,
					    ARRAY_SIZE(example_params_array));
		return 0;
	}

	/* Example test that uses shared resources in test->resources. */
	static void example_params_test_with_init(struct kunit *test)
	{
		int threshold;
		const struct example_param *param = test->param_value;
		/*  Here we pass test->parent to access the parameterized test context. */
		struct kunit_resource *res = kunit_find_resource(test->parent,
								 example_resource_alloc_match,
								 NULL);

		threshold = *((int *)res->data);
		KUNIT_ASSERT_LE(test, param->value, threshold);
		kunit_put_resource(res);
	}

	static struct kunit_case example_test_cases[] = {
		KUNIT_CASE_PARAM_WITH_INIT(example_params_test_with_init, kunit_array_gen_params,
					   example_param_init, NULL),
		{}
	};

As an alternative to using the KUnit Resource API for sharing resources, you can
place them in ``test->parent->priv``. This serves as a more lightweight method
for resource storage, best for scenarios where complex resource management is
not required.

As stated previously ``param_init()`` and ``param_exit()`` get the parameterized
test context. So, you can directly use ``test->priv`` within ``param_init/exit``
to manage shared resources. However, from within the test case function, you must
navigate up to the parent ``struct kunit`` i.e. the parameterized test context.
Therefore, you need to use ``test->parent->priv`` to access those same
resources.

The resources placed in ``test->parent->priv`` will need to be allocated in
memory to persist across the parameter runs. If memory is allocated using the
KUnit memory allocation APIs (described more in the "Allocating Memory" section
below), you won't need to worry about deallocation. The APIs will make the memory
parameterized test 'managed', ensuring that it will automatically get cleaned up
after the parameterized test concludes.

The code below demonstrates example usage of the ``priv`` field for shared
resources:

.. code-block:: c

	static const struct example_param {
		int value;
	} example_params_array[] = {
		{ .value = 3, },
		{ .value = 2, },
		{ .value = 1, },
		{ .value = 0, },
	};

	/* Initialize the parameterized test context. */
	static int example_param_init_priv(struct kunit *test)
	{
		int ctx = 3; /* Data to be stored. */
		int arr_size = ARRAY_SIZE(example_params_array);

		/*
		 * Allocate memory using kunit_kzalloc(). Since the `param_init`
		 * function receives the parameterized test context, this memory
		 * allocation will be scoped to the lifetime of the parameterized test.
		 */
		test->priv = kunit_kzalloc(test, sizeof(int), GFP_KERNEL);

		/* Assign the context value to test->priv.*/
		*((int *)test->priv) = ctx;

		/* Register the parameter array. */
		kunit_register_params_array(test, example_params_array, arr_size, NULL);
		return 0;
	}

	static void example_params_test_with_init_priv(struct kunit *test)
	{
		int threshold;
		const struct example_param *param = test->param_value;

		/* By design, test->parent will not be NULL. */
		KUNIT_ASSERT_NOT_NULL(test, test->parent);

		/* Here we use test->parent->priv to access the shared resource. */
		threshold = *(int *)test->parent->priv;

		KUNIT_ASSERT_LE(test, param->value, threshold);
	}

	static struct kunit_case example_tests[] = {
		KUNIT_CASE_PARAM_WITH_INIT(example_params_test_with_init_priv,
					   kunit_array_gen_params,
					   example_param_init_priv, NULL),
		{}
	};

Allocating Memory
-----------------

Where you might use ``kzalloc``, you can instead use ``kunit_kzalloc`` as KUnit
will then ensure that the memory is freed once the test completes.

This is useful because it lets us use the ``KUNIT_ASSERT_EQ`` macros to exit
early from a test without having to worry about remembering to call ``kfree``.
For example:

.. code-block:: c

	void example_test_allocation(struct kunit *test)
	{
		char *buffer = kunit_kzalloc(test, 16, GFP_KERNEL);
		/* Ensure allocation succeeded. */
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buffer);

		KUNIT_ASSERT_STREQ(test, buffer, "");
	}

Registering Cleanup Actions
---------------------------

If you need to perform some cleanup beyond simple use of ``kunit_kzalloc``,
you can register a custom "deferred action", which is a cleanup function
run when the test exits (whether cleanly, or via a failed assertion).

Actions are simple functions with no return value, and a single ``void*``
context argument, and fulfill the same role as "cleanup" functions in Python
and Go tests, "defer" statements in languages which support them, and
(in some cases) destructors in RAII languages.

These are very useful for unregistering things from global lists, closing
files or other resources, or freeing resources.

For example:

.. code-block:: C

	static void cleanup_device(void *ctx)
	{
		struct device *dev = (struct device *)ctx;

		device_unregister(dev);
	}

	void example_device_test(struct kunit *test)
	{
		struct my_device dev;

		device_register(&dev);

		kunit_add_action(test, &cleanup_device, &dev);
	}

Note that, for functions like device_unregister which only accept a single
pointer-sized argument, it's possible to automatically generate a wrapper
with the ``KUNIT_DEFINE_ACTION_WRAPPER()`` macro, for example:

.. code-block:: C

	KUNIT_DEFINE_ACTION_WRAPPER(device_unregister, device_unregister_wrapper, struct device *);
	kunit_add_action(test, &device_unregister_wrapper, &dev);

You should do this in preference to manually casting to the ``kunit_action_t`` type,
as casting function pointers will break Control Flow Integrity (CFI).

``kunit_add_action`` can fail if, for example, the system is out of memory.
You can use ``kunit_add_action_or_reset`` instead which runs the action
immediately if it cannot be deferred.

If you need more control over when the cleanup function is called, you
can trigger it early using ``kunit_release_action``, or cancel it entirely
with ``kunit_remove_action``.


Testing Static Functions
------------------------

If you want to test static functions without exposing those functions outside of
testing, one option is conditionally export the symbol. When KUnit is enabled,
the symbol is exposed but remains static otherwise. To use this method, follow
the template below.

.. code-block:: c

	/* In the file containing functions to test "my_file.c" */

	#include <kunit/visibility.h>
	#include <my_file.h>
	...
	VISIBLE_IF_KUNIT int do_interesting_thing()
	{
	...
	}
	EXPORT_SYMBOL_IF_KUNIT(do_interesting_thing);

	/* In the header file "my_file.h" */

	#if IS_ENABLED(CONFIG_KUNIT)
		int do_interesting_thing(void);
	#endif

	/* In the KUnit test file "my_file_test.c" */

	#include <kunit/visibility.h>
	#include <my_file.h>
	...
	MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
	...
	// Use do_interesting_thing() in tests

For a full example, see this `patch <https://lore.kernel.org/all/20221207014024.340230-3-rmoar@google.com/>`_
where a test is modified to conditionally expose static functions for testing
using the macros above.

As an **alternative** to the method above, you could conditionally ``#include``
the test file at the end of your .c file. This is not recommended but works
if needed. For example:

.. code-block:: c

	/* In "my_file.c" */

	static int do_interesting_thing();

	#ifdef CONFIG_MY_KUNIT_TEST
	#include "my_kunit_test.c"
	#endif

Injecting Test-Only Code
------------------------

Similar to as shown above, we can add test-specific logic. For example:

.. code-block:: c

	/* In my_file.h */

	#ifdef CONFIG_MY_KUNIT_TEST
	/* Defined in my_kunit_test.c */
	void test_only_hook(void);
	#else
	void test_only_hook(void) { }
	#endif

This test-only code can be made more useful by accessing the current ``kunit_test``
as shown in next section: *Accessing The Current Test*.

Accessing The Current Test
--------------------------

In some cases, we need to call test-only code from outside the test file.  This
is helpful, for example, when providing a fake implementation of a function, or
to fail any current test from within an error handler.
We can do this via the ``kunit_test`` field in ``task_struct``, which we can
access using the ``kunit_get_current_test()`` function in ``kunit/test-bug.h``.

``kunit_get_current_test()`` is safe to call even if KUnit is not enabled. If
KUnit is not enabled, or if no test is running in the current task, it will
return ``NULL``. This compiles down to either a no-op or a static key check,
so will have a negligible performance impact when no test is running.

The example below uses this to implement a "mock" implementation of a function, ``foo``:

.. code-block:: c

	#include <kunit/test-bug.h> /* for kunit_get_current_test */

	struct test_data {
		int foo_result;
		int want_foo_called_with;
	};

	static int fake_foo(int arg)
	{
		struct kunit *test = kunit_get_current_test();
		struct test_data *test_data = test->priv;

		KUNIT_EXPECT_EQ(test, test_data->want_foo_called_with, arg);
		return test_data->foo_result;
	}

	static void example_simple_test(struct kunit *test)
	{
		/* Assume priv (private, a member used to pass test data from
		 * the init function) is allocated in the suite's .init */
		struct test_data *test_data = test->priv;

		test_data->foo_result = 42;
		test_data->want_foo_called_with = 1;

		/* In a real test, we'd probably pass a pointer to fake_foo somewhere
		 * like an ops struct, etc. instead of calling it directly. */
		KUNIT_EXPECT_EQ(test, fake_foo(1), 42);
	}

In this example, we are using the ``priv`` member of ``struct kunit`` as a way
of passing data to the test from the init function. In general ``priv`` is
pointer that can be used for any user data. This is preferred over static
variables, as it avoids concurrency issues.

Had we wanted something more flexible, we could have used a named ``kunit_resource``.
Each test can have multiple resources which have string names providing the same
flexibility as a ``priv`` member, but also, for example, allowing helper
functions to create resources without conflicting with each other. It is also
possible to define a clean up function for each resource, making it easy to
avoid resource leaks. For more information, see Documentation/dev-tools/kunit/api/resource.rst.

Failing The Current Test
------------------------

If we want to fail the current test, we can use ``kunit_fail_current_test(fmt, args...)``
which is defined in ``<kunit/test-bug.h>`` and does not require pulling in ``<kunit/test.h>``.
For example, we have an option to enable some extra debug checks on some data
structures as shown below:

.. code-block:: c

	#include <kunit/test-bug.h>

	#ifdef CONFIG_EXTRA_DEBUG_CHECKS
	static void validate_my_data(struct data *data)
	{
		if (is_valid(data))
			return;

		kunit_fail_current_test("data %p is invalid", data);

		/* Normal, non-KUnit, error reporting code here. */
	}
	#else
	static void my_debug_function(void) { }
	#endif

``kunit_fail_current_test()`` is safe to call even if KUnit is not enabled. If
KUnit is not enabled, or if no test is running in the current task, it will do
nothing. This compiles down to either a no-op or a static key check, so will
have a negligible performance impact when no test is running.

Managing Fake Devices and Drivers
---------------------------------

When testing drivers or code which interacts with drivers, many functions will
require a ``struct device`` or ``struct device_driver``. In many cases, setting
up a real device is not required to test any given function, so a fake device
can be used instead.

KUnit provides helper functions to create and manage these fake devices, which
are internally of type ``struct kunit_device``, and are attached to a special
``kunit_bus``. These devices support managed device resources (devres), as
described in Documentation/driver-api/driver-model/devres.rst

To create a KUnit-managed ``struct device_driver``, use ``kunit_driver_create()``,
which will create a driver with the given name, on the ``kunit_bus``. This driver
will automatically be destroyed when the corresponding test finishes, but can also
be manually destroyed with ``driver_unregister()``.

To create a fake device, use the ``kunit_device_register()``, which will create
and register a device, using a new KUnit-managed driver created with ``kunit_driver_create()``.
To provide a specific, non-KUnit-managed driver, use ``kunit_device_register_with_driver()``
instead. Like with managed drivers, KUnit-managed fake devices are automatically
cleaned up when the test finishes, but can be manually cleaned up early with
``kunit_device_unregister()``.

The KUnit devices should be used in preference to ``root_device_register()``, and
instead of ``platform_device_register()`` in cases where the device is not otherwise
a platform device.

For example:

.. code-block:: c

	#include <kunit/device.h>

	static void test_my_device(struct kunit *test)
	{
		struct device *fake_device;
		const char *dev_managed_string;

		// Create a fake device.
		fake_device = kunit_device_register(test, "my_device");
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_device)

		// Pass it to functions which need a device.
		dev_managed_string = devm_kstrdup(fake_device, "Hello, World!");

		// Everything is cleaned up automatically when the test ends.
	}