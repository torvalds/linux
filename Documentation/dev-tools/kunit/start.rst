.. SPDX-License-Identifier: GPL-2.0

===============
Getting Started
===============

This page contains an overview of the kunit_tool and KUnit framework,
teaching how to run existing tests and then how to write a simple test case,
and covers common problems users face when using KUnit for the first time.

Installing Dependencies
=======================
KUnit has the same dependencies as the Linux kernel. As long as you can
build the kernel, you can run KUnit.

Running tests with kunit_tool
=============================
kunit_tool is a Python script, which configures and builds a kernel, runs
tests, and formats the test results. From the kernel repository, you
can run kunit_tool:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run

.. note ::
	You may see the following error:
	"The source tree is not clean, please run 'make ARCH=um mrproper'"

	This happens because internally kunit.py specifies ``.kunit``
	(default option) as the build directory in the command ``make O=output/dir``
	through the argument ``--build_dir``.  Hence, before starting an
	out-of-tree build, the source tree must be clean.

	There is also the same caveat mentioned in the "Build directory for
	the kernel" section of the :doc:`admin-guide </admin-guide/README>`,
	that is, its use, it must be used for all invocations of ``make``.
	The good news is that it can indeed be solved by running
	``make ARCH=um mrproper``, just be aware that this will delete the
	current configuration and all generated files.

If everything worked correctly, you should see the following:

.. code-block::

	Configuring KUnit Kernel ...
	Building KUnit Kernel ...
	Starting KUnit Kernel ...

The tests will pass or fail.

.. note ::
   Because it is building a lot of sources for the first time,
   the ``Building KUnit Kernel`` step may take a while.

For detailed information on this wrapper, see:
Documentation/dev-tools/kunit/run_wrapper.rst.

Selecting which tests to run
----------------------------

By default, kunit_tool runs all tests reachable with minimal configuration,
that is, using default values for most of the kconfig options.  However,
you can select which tests to run by:

- `Customizing Kconfig`_ used to compile the kernel, or
- `Filtering tests by name`_ to select specifically which compiled tests to run.

Customizing Kconfig
~~~~~~~~~~~~~~~~~~~
A good starting point for the ``.kunitconfig`` is the KUnit default config.
If you didn't run ``kunit.py run`` yet, you can generate it by running:

.. code-block:: bash

	cd $PATH_TO_LINUX_REPO
	tools/testing/kunit/kunit.py config
	cat .kunit/.kunitconfig

.. note ::
   ``.kunitconfig`` lives in the ``--build_dir`` used by kunit.py, which is
   ``.kunit`` by default.

Before running the tests, kunit_tool ensures that all config options
set in ``.kunitconfig`` are set in the kernel ``.config``. It will warn
you if you have not included dependencies for the options used.

There are many ways to customize the configurations:

a. Edit ``.kunit/.kunitconfig``. The file should contain the list of kconfig
   options required to run the desired tests, including their dependencies.
   You may want to remove CONFIG_KUNIT_ALL_TESTS from the ``.kunitconfig`` as
   it will enable a number of additional tests that you may not want.
   If you need to run on an architecture other than UML see :ref:`kunit-on-qemu`.

b. Enable additional kconfig options on top of ``.kunit/.kunitconfig``.
   For example, to include the kernel's linked-list test you can run::

	./tools/testing/kunit/kunit.py run \
		--kconfig_add CONFIG_LIST_KUNIT_TEST=y

c. Provide the path of one or more .kunitconfig files from the tree.
   For example, to run only ``FAT_FS`` and ``EXT4`` tests you can run::

	./tools/testing/kunit/kunit.py run \
		--kunitconfig ./fs/fat/.kunitconfig \
		--kunitconfig ./fs/ext4/.kunitconfig

d. If you change the ``.kunitconfig``, kunit.py will trigger a rebuild of the
   ``.config`` file. But you can edit the ``.config`` file directly or with
   tools like ``make menuconfig O=.kunit``. As long as its a superset of
   ``.kunitconfig``, kunit.py won't overwrite your changes.


.. note ::

	To save a .kunitconfig after finding a satisfactory configuration::

		make savedefconfig O=.kunit
		cp .kunit/defconfig .kunit/.kunitconfig

Filtering tests by name
~~~~~~~~~~~~~~~~~~~~~~~
If you want to be more specific than Kconfig can provide, it is also possible
to select which tests to execute at boot-time by passing a glob filter
(read instructions regarding the pattern in the manpage :manpage:`glob(7)`).
If there is a ``"."`` (period) in the filter, it will be interpreted as a
separator between the name of the test suite and the test case,
otherwise, it will be interpreted as the name of the test suite.
For example, let's assume we are using the default config:

a. inform the name of a test suite, like ``"kunit_executor_test"``,
   to run every test case it contains::

	./tools/testing/kunit/kunit.py run "kunit_executor_test"

b. inform the name of a test case prefixed by its test suite,
   like ``"example.example_simple_test"``, to run specifically that test case::

	./tools/testing/kunit/kunit.py run "example.example_simple_test"

c. use wildcard characters (``*?[``) to run any test case that matches the pattern,
   like ``"*.*64*"`` to run test cases containing ``"64"`` in the name inside
   any test suite::

	./tools/testing/kunit/kunit.py run "*.*64*"

Running Tests without the KUnit Wrapper
=======================================
If you do not want to use the KUnit Wrapper (for example: you want code
under test to integrate with other systems, or use a different/
unsupported architecture or configuration), KUnit can be included in
any kernel, and the results are read out and parsed manually.

.. note ::
   ``CONFIG_KUNIT`` should not be enabled in a production environment.
   Enabling KUnit disables Kernel Address-Space Layout Randomization
   (KASLR), and tests may affect the state of the kernel in ways not
   suitable for production.

Configuring the Kernel
----------------------
To enable KUnit itself, you need to enable the ``CONFIG_KUNIT`` Kconfig
option (under Kernel Hacking/Kernel Testing and Coverage in
``menuconfig``). From there, you can enable any KUnit tests. They
usually have config options ending in ``_KUNIT_TEST``.

KUnit and KUnit tests can be compiled as modules. The tests in a module
will run when the module is loaded.

Running Tests (without KUnit Wrapper)
-------------------------------------
Build and run your kernel. In the kernel log, the test output is printed
out in the TAP format. This will only happen by default if KUnit/tests
are built-in. Otherwise the module will need to be loaded.

.. note ::
   Some lines and/or data may get interspersed in the TAP output.

Writing Your First Test
=======================
In your kernel repository, let's add some code that we can test.

1. Create a file ``drivers/misc/example.h``, which includes:

.. code-block:: c

	int misc_example_add(int left, int right);

2. Create a file ``drivers/misc/example.c``, which includes:

.. code-block:: c

	#include <linux/errno.h>

	#include "example.h"

	int misc_example_add(int left, int right)
	{
		return left + right;
	}

3. Add the following lines to ``drivers/misc/Kconfig``:

.. code-block:: kconfig

	config MISC_EXAMPLE
		bool "My example"

4. Add the following lines to ``drivers/misc/Makefile``:

.. code-block:: make

	obj-$(CONFIG_MISC_EXAMPLE) += example.o

Now we are ready to write the test cases.

1. Add the below test case in ``drivers/misc/example_test.c``:

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

2. Add the following lines to ``drivers/misc/Kconfig``:

.. code-block:: kconfig

	config MISC_EXAMPLE_TEST
		tristate "Test for my example" if !KUNIT_ALL_TESTS
		depends on MISC_EXAMPLE && KUNIT=y
		default KUNIT_ALL_TESTS

3. Add the following lines to ``drivers/misc/Makefile``:

.. code-block:: make

	obj-$(CONFIG_MISC_EXAMPLE_TEST) += example_test.o

4. Add the following lines to ``.kunit/.kunitconfig``:

.. code-block:: none

	CONFIG_MISC_EXAMPLE=y
	CONFIG_MISC_EXAMPLE_TEST=y

5. Run the test:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run

You should see the following failure:

.. code-block:: none

	...
	[16:08:57] [PASSED] misc-example:misc_example_add_test_basic
	[16:08:57] [FAILED] misc-example:misc_example_test_failure
	[16:08:57] EXPECTATION FAILED at drivers/misc/example-test.c:17
	[16:08:57]      This test never passes.
	...

Congrats! You just wrote your first KUnit test.

Next Steps
==========

If you're interested in using some of the more advanced features of kunit.py,
take a look at Documentation/dev-tools/kunit/run_wrapper.rst

If you'd like to run tests without using kunit.py, check out
Documentation/dev-tools/kunit/run_manual.rst

For more information on writing KUnit tests (including some common techniques
for testing different things), see Documentation/dev-tools/kunit/usage.rst
