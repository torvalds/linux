.. SPDX-License-Identifier: GPL-2.0

===============
Getting Started
===============

Installing dependencies
=======================
KUnit has the same dependencies as the Linux kernel. As long as you can build
the kernel, you can run KUnit.

Running tests with the KUnit Wrapper
====================================
Included with KUnit is a simple Python wrapper which runs tests under User Mode
Linux, and formats the test results.

The wrapper can be run with:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run --defconfig

For more information on this wrapper (also called kunit_tool) check out the
:doc:`kunit-tool` page.

Creating a .kunitconfig
-----------------------
If you want to run a specific set of tests (rather than those listed in the
KUnit defconfig), you can provide Kconfig options in the ``.kunitconfig`` file.
This file essentially contains the regular Kernel config, with the specific
test targets as well. The ``.kunitconfig`` should also contain any other config
options required by the tests.

A good starting point for a ``.kunitconfig`` is the KUnit defconfig:
.. code-block:: bash

	cd $PATH_TO_LINUX_REPO
	cp arch/um/configs/kunit_defconfig .kunitconfig

You can then add any other Kconfig options you wish, e.g.:
.. code-block:: none

        CONFIG_LIST_KUNIT_TEST=y

:doc:`kunit_tool <kunit-tool>` will ensure that all config options set in
``.kunitconfig`` are set in the kernel ``.config`` before running the tests.
It'll warn you if you haven't included the dependencies of the options you're
using.

.. note::
   Note that removing something from the ``.kunitconfig`` will not trigger a
   rebuild of the ``.config`` file: the configuration is only updated if the
   ``.kunitconfig`` is not a subset of ``.config``. This means that you can use
   other tools (such as make menuconfig) to adjust other config options.


Running the tests
-----------------

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

Running tests without the KUnit Wrapper
=======================================

If you'd rather not use the KUnit Wrapper (if, for example, you need to
integrate with other systems, or use an architecture other than UML), KUnit can
be included in any kernel, and the results read out and parsed manually.

.. note::
   KUnit is not designed for use in a production system, and it's possible that
   tests may reduce the stability or security of the system.



Configuring the kernel
----------------------

In order to enable KUnit itself, you simply need to enable the ``CONFIG_KUNIT``
Kconfig option (it's under Kernel Hacking/Kernel Testing and Coverage in
menuconfig). From there, you can enable any KUnit tests you want: they usually
have config options ending in ``_KUNIT_TEST``.

KUnit and KUnit tests can be compiled as modules: in this case the tests in a
module will be run when the module is loaded.

Running the tests
-----------------

Build and run your kernel as usual. Test output will be written to the kernel
log in `TAP <https://testanything.org/>`_ format.

.. note::
   It's possible that there will be other lines and/or data interspersed in the
   TAP output.


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
