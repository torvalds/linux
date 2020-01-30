.. SPDX-License-Identifier: GPL-2.0

===============
Getting Started
===============

Installing dependencies
=======================
KUnit has the same dependencies as the Linux kernel. As long as you can build
the kernel, you can run KUnit.

KUnit Wrapper
=============
Included with KUnit is a simple Python wrapper that helps format the output to
easily use and read KUnit output. It handles building and running the kernel, as
well as formatting the output.

The wrapper can be run with:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run --defconfig

For more information on this wrapper (also called kunit_tool) checkout the
:doc:`kunit-tool` page.

Creating a .kunitconfig
=======================
The Python script is a thin wrapper around Kbuild. As such, it needs to be
configured with a ``.kunitconfig`` file. This file essentially contains the
regular Kernel config, with the specific test targets as well.

.. code-block:: bash

	cd $PATH_TO_LINUX_REPO
	cp arch/um/configs/kunit_defconfig .kunitconfig

Verifying KUnit Works
---------------------

To make sure that everything is set up correctly, simply invoke the Python
wrapper from your kernel repo:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run

.. note::
   You may want to run ``make mrproper`` first.

If everything worked correctly, you should see the following:

.. code-block:: bash

	Generating .config ...
	Building KUnit Kernel ...
	Starting KUnit Kernel ...

followed by a list of tests that are run. All of them should be passing.

.. note::
	Because it is building a lot of sources for the first time, the
	``Building KUnit kernel`` step may take a while.

Writing your first test
=======================

In your kernel repo let's add some code that we can test. Create a file
``drivers/misc/example.h`` with the contents:

.. code-block:: c

	int misc_example_add(int left, int right);

create a file ``drivers/misc/example.c``:

.. code-block:: c

	#include <linux/errno.h>

	#include "example.h"

	int misc_example_add(int left, int right)
	{
		return left + right;
	}

Now add the following lines to ``drivers/misc/Kconfig``:

.. code-block:: kconfig

	config MISC_EXAMPLE
		bool "My example"

and the following lines to ``drivers/misc/Makefile``:

.. code-block:: make

	obj-$(CONFIG_MISC_EXAMPLE) += example.o

Now we are ready to write the test. The test will be in
``drivers/misc/example-test.c``:

.. code-block:: c

	#include <kunit/test.h>
	#include "example.h"

	/* Define the test cases. */

	static void misc_example_add_test_basic(struct kunit *test)
	{
		KUNIT_EXPECT_EQ(test, 1, misc_example_add(1, 0));
		KUNIT_EXPECT_EQ(test, 2, misc_example_add(1, 1));
		KUNIT_EXPECT_EQ(test, 0, misc_example_add(-1, 1));
		KUNIT_EXPECT_EQ(test, INT_MAX, misc_example_add(0, INT_MAX));
		KUNIT_EXPECT_EQ(test, -1, misc_example_add(INT_MAX, INT_MIN));
	}

	static void misc_example_test_failure(struct kunit *test)
	{
		KUNIT_FAIL(test, "This test never passes.");
	}

	static struct kunit_case misc_example_test_cases[] = {
		KUNIT_CASE(misc_example_add_test_basic),
		KUNIT_CASE(misc_example_test_failure),
		{}
	};

	static struct kunit_suite misc_example_test_suite = {
		.name = "misc-example",
		.test_cases = misc_example_test_cases,
	};
	kunit_test_suite(misc_example_test_suite);

Now add the following to ``drivers/misc/Kconfig``:

.. code-block:: kconfig

	config MISC_EXAMPLE_TEST
		bool "Test for my example"
		depends on MISC_EXAMPLE && KUNIT

and the following to ``drivers/misc/Makefile``:

.. code-block:: make

	obj-$(CONFIG_MISC_EXAMPLE_TEST) += example-test.o

Now add it to your ``.kunitconfig``:

.. code-block:: none

	CONFIG_MISC_EXAMPLE=y
	CONFIG_MISC_EXAMPLE_TEST=y

Now you can run the test:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run

You should see the following failure:

.. code-block:: none

	...
	[16:08:57] [PASSED] misc-example:misc_example_add_test_basic
	[16:08:57] [FAILED] misc-example:misc_example_test_failure
	[16:08:57] EXPECTATION FAILED at drivers/misc/example-test.c:17
	[16:08:57] 	This test never passes.
	...

Congrats! You just wrote your first KUnit test!

Next Steps
==========
*   Check out the :doc:`usage` page for a more
    in-depth explanation of KUnit.
