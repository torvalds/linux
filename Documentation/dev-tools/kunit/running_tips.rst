.. SPDX-License-Identifier: GPL-2.0

============================
Tips For Running KUnit Tests
============================

Using ``kunit.py run`` ("kunit tool")
=====================================

Running from any directory
--------------------------

It can be handy to create a bash function like:

.. code-block:: bash

	function run_kunit() {
	  ( cd "$(git rev-parse --show-toplevel)" && ./tools/testing/kunit/kunit.py run "$@" )
	}

.. note::
	Early versions of ``kunit.py`` (before 5.6) didn't work unless run from
	the kernel root, hence the use of a subshell and ``cd``.

Running a subset of tests
-------------------------

``kunit.py run`` accepts an optional glob argument to filter tests. The format
is ``"<suite_glob>[.test_glob]"``.

Say that we wanted to run the sysctl tests, we could do so via:

.. code-block:: bash

	$ echo -e 'CONFIG_KUNIT=y\nCONFIG_KUNIT_ALL_TESTS=y' > .kunit/.kunitconfig
	$ ./tools/testing/kunit/kunit.py run 'sysctl*'

We can filter down to just the "write" tests via:

.. code-block:: bash

	$ echo -e 'CONFIG_KUNIT=y\nCONFIG_KUNIT_ALL_TESTS=y' > .kunit/.kunitconfig
	$ ./tools/testing/kunit/kunit.py run 'sysctl*.*write*'

We're paying the cost of building more tests than we need this way, but it's
easier than fiddling with ``.kunitconfig`` files or commenting out
``kunit_suite``'s.

However, if we wanted to define a set of tests in a less ad hoc way, the next
tip is useful.

Defining a set of tests
-----------------------

``kunit.py run`` (along with ``build``, and ``config``) supports a
``--kunitconfig`` flag. So if you have a set of tests that you want to run on a
regular basis (especially if they have other dependencies), you can create a
specific ``.kunitconfig`` for them.

E.g. kunit has one for its tests:

.. code-block:: bash

	$ ./tools/testing/kunit/kunit.py run --kunitconfig=lib/kunit/.kunitconfig

Alternatively, if you're following the convention of naming your
file ``.kunitconfig``, you can just pass in the dir, e.g.

.. code-block:: bash

	$ ./tools/testing/kunit/kunit.py run --kunitconfig=lib/kunit

.. note::
	This is a relatively new feature (5.12+) so we don't have any
	conventions yet about on what files should be checked in versus just
	kept around locally. It's up to you and your maintainer to decide if a
	config is useful enough to submit (and therefore have to maintain).

.. note::
	Having ``.kunitconfig`` fragments in a parent and child directory is
	iffy. There's discussion about adding an "import" statement in these
	files to make it possible to have a top-level config run tests from all
	child directories. But that would mean ``.kunitconfig`` files are no
	longer just simple .config fragments.

	One alternative would be to have kunit tool recursively combine configs
	automagically, but tests could theoretically depend on incompatible
	options, so handling that would be tricky.

Setting kernel commandline parameters
-------------------------------------

You can use ``--kernel_args`` to pass arbitrary kernel arguments, e.g.

.. code-block:: bash

	$ ./tools/testing/kunit/kunit.py run --kernel_args=param=42 --kernel_args=param2=false


Generating code coverage reports under UML
------------------------------------------

.. note::
	TODO(brendanhiggins@google.com): There are various issues with UML and
	versions of gcc 7 and up. You're likely to run into missing ``.gcda``
	files or compile errors.

This is different from the "normal" way of getting coverage information that is
documented in Documentation/dev-tools/gcov.rst.

Instead of enabling ``CONFIG_GCOV_KERNEL=y``, we can set these options:

.. code-block:: none

	CONFIG_DEBUG_KERNEL=y
	CONFIG_DEBUG_INFO=y
	CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y
	CONFIG_GCOV=y


Putting it together into a copy-pastable sequence of commands:

.. code-block:: bash

	# Append coverage options to the current config
	$ ./tools/testing/kunit/kunit.py run --kunitconfig=.kunit/ --kunitconfig=tools/testing/kunit/configs/coverage_uml.config
	# Extract the coverage information from the build dir (.kunit/)
	$ lcov -t "my_kunit_tests" -o coverage.info -c -d .kunit/

	# From here on, it's the same process as with CONFIG_GCOV_KERNEL=y
	# E.g. can generate an HTML report in a tmp dir like so:
	$ genhtml -o /tmp/coverage_html coverage.info


If your installed version of gcc doesn't work, you can tweak the steps:

.. code-block:: bash

	$ ./tools/testing/kunit/kunit.py run --make_options=CC=/usr/bin/gcc-6
	$ lcov -t "my_kunit_tests" -o coverage.info -c -d .kunit/ --gcov-tool=/usr/bin/gcov-6


Running tests manually
======================

Running tests without using ``kunit.py run`` is also an important use case.
Currently it's your only option if you want to test on architectures other than
UML.

As running the tests under UML is fairly straightforward (configure and compile
the kernel, run the ``./linux`` binary), this section will focus on testing
non-UML architectures.


Running built-in tests
----------------------

When setting tests to ``=y``, the tests will run as part of boot and print
results to dmesg in TAP format. So you just need to add your tests to your
``.config``, build and boot your kernel as normal.

So if we compiled our kernel with:

.. code-block:: none

	CONFIG_KUNIT=y
	CONFIG_KUNIT_EXAMPLE_TEST=y

Then we'd see output like this in dmesg signaling the test ran and passed:

.. code-block:: none

	TAP version 14
	1..1
	    # Subtest: example
	    1..1
	    # example_simple_test: initializing
	    ok 1 - example_simple_test
	ok 1 - example

Running tests as modules
------------------------

Depending on the tests, you can build them as loadable modules.

For example, we'd change the config options from before to

.. code-block:: none

	CONFIG_KUNIT=y
	CONFIG_KUNIT_EXAMPLE_TEST=m

Then after booting into our kernel, we can run the test via

.. code-block:: none

	$ modprobe kunit-example-test

This will then cause it to print TAP output to stdout.

.. note::
	The ``modprobe`` will *not* have a non-zero exit code if any test
	failed (as of 5.13). But ``kunit.py parse`` would, see below.

.. note::
	You can set ``CONFIG_KUNIT=m`` as well, however, some features will not
	work and thus some tests might break. Ideally tests would specify they
	depend on ``KUNIT=y`` in their ``Kconfig``'s, but this is an edge case
	most test authors won't think about.
	As of 5.13, the only difference is that ``current->kunit_test`` will
	not exist.

Pretty-printing results
-----------------------

You can use ``kunit.py parse`` to parse dmesg for test output and print out
results in the same familiar format that ``kunit.py run`` does.

.. code-block:: bash

	$ ./tools/testing/kunit/kunit.py parse /var/log/dmesg


Retrieving per suite results
----------------------------

Regardless of how you're running your tests, you can enable
``CONFIG_KUNIT_DEBUGFS`` to expose per-suite TAP-formatted results:

.. code-block:: none

	CONFIG_KUNIT=y
	CONFIG_KUNIT_EXAMPLE_TEST=m
	CONFIG_KUNIT_DEBUGFS=y

The results for each suite will be exposed under
``/sys/kernel/debug/kunit/<suite>/results``.
So using our example config:

.. code-block:: bash

	$ modprobe kunit-example-test > /dev/null
	$ cat /sys/kernel/debug/kunit/example/results
	... <TAP output> ...

	# After removing the module, the corresponding files will go away
	$ modprobe -r kunit-example-test
	$ cat /sys/kernel/debug/kunit/example/results
	/sys/kernel/debug/kunit/example/results: No such file or directory

Generating code coverage reports
--------------------------------

See Documentation/dev-tools/gcov.rst for details on how to do this.

The only vaguely KUnit-specific advice here is that you probably want to build
your tests as modules. That way you can isolate the coverage from tests from
other code executed during boot, e.g.

.. code-block:: bash

	# Reset coverage counters before running the test.
	$ echo 0 > /sys/kernel/debug/gcov/reset
	$ modprobe kunit-example-test


Test Attributes and Filtering
=============================

Test suites and cases can be marked with test attributes, such as speed of
test. These attributes will later be printed in test output and can be used to
filter test execution.

Marking Test Attributes
-----------------------

Tests are marked with an attribute by including a ``kunit_attributes`` object
in the test definition.

Test cases can be marked using the ``KUNIT_CASE_ATTR(test_name, attributes)``
macro to define the test case instead of ``KUNIT_CASE(test_name)``.

.. code-block:: c

	static const struct kunit_attributes example_attr = {
		.speed = KUNIT_VERY_SLOW,
	};

	static struct kunit_case example_test_cases[] = {
		KUNIT_CASE_ATTR(example_test, example_attr),
	};

.. note::
	To mark a test case as slow, you can also use ``KUNIT_CASE_SLOW(test_name)``.
	This is a helpful macro as the slow attribute is the most commonly used.

Test suites can be marked with an attribute by setting the "attr" field in the
suite definition.

.. code-block:: c

	static const struct kunit_attributes example_attr = {
		.speed = KUNIT_VERY_SLOW,
	};

	static struct kunit_suite example_test_suite = {
		...,
		.attr = example_attr,
	};

.. note::
	Not all attributes need to be set in a ``kunit_attributes`` object. Unset
	attributes will remain uninitialized and act as though the attribute is set
	to 0 or NULL. Thus, if an attribute is set to 0, it is treated as unset.
	These unset attributes will not be reported and may act as a default value
	for filtering purposes.

Reporting Attributes
--------------------

When a user runs tests, attributes will be present in the raw kernel output (in
KTAP format). Note that attributes will be hidden by default in kunit.py output
for all passing tests but the raw kernel output can be accessed using the
``--raw_output`` flag. This is an example of how test attributes for test cases
will be formatted in kernel output:

.. code-block:: none

	# example_test.speed: slow
	ok 1 example_test

This is an example of how test attributes for test suites will be formatted in
kernel output:

.. code-block:: none

	  KTAP version 2
	  # Subtest: example_suite
	  # module: kunit_example_test
	  1..3
	  ...
	ok 1 example_suite

Additionally, users can output a full attribute report of tests with their
attributes, using the command line flag ``--list_tests_attr``:

.. code-block:: bash

	kunit.py run "example" --list_tests_attr

.. note::
	This report can be accessed when running KUnit manually by passing in the
	module_param ``kunit.action=list_attr``.

Filtering
---------

Users can filter tests using the ``--filter`` command line flag when running
tests. As an example:

.. code-block:: bash

	kunit.py run --filter speed=slow


You can also use the following operations on filters: "<", ">", "<=", ">=",
"!=", and "=". Example:

.. code-block:: bash

	kunit.py run --filter "speed>slow"

This example will run all tests with speeds faster than slow. Note that the
characters < and > are often interpreted by the shell, so they may need to be
quoted or escaped, as above.

Additionally, you can use multiple filters at once. Simply separate filters
using commas. Example:

.. code-block:: bash

	kunit.py run --filter "speed>slow, module=kunit_example_test"

.. note::
	You can use this filtering feature when running KUnit manually by passing
	the filter as a module param: ``kunit.filter="speed>slow, speed<=normal"``.

Filtered tests will not run or show up in the test output. You can use the
``--filter_action=skip`` flag to skip filtered tests instead. These tests will be
shown in the test output in the test but will not run. To use this feature when
running KUnit manually, use the module param ``kunit.filter_action=skip``.

Rules of Filtering Procedure
----------------------------

Since both suites and test cases can have attributes, there may be conflicts
between attributes during filtering. The process of filtering follows these
rules:

- Filtering always operates at a per-test level.

- If a test has an attribute set, then the test's value is filtered on.

- Otherwise, the value falls back to the suite's value.

- If neither are set, the attribute has a global "default" value, which is used.

List of Current Attributes
--------------------------

``speed``

This attribute indicates the speed of a test's execution (how slow or fast the
test is).

This attribute is saved as an enum with the following categories: "normal",
"slow", or "very_slow". The assumed default speed for tests is "normal". This
indicates that the test takes a relatively trivial amount of time (less than
1 second), regardless of the machine it is running on. Any test slower than
this could be marked as "slow" or "very_slow".

The macro ``KUNIT_CASE_SLOW(test_name)`` can be easily used to set the speed
of a test case to "slow".

``module``

This attribute indicates the name of the module associated with the test.

This attribute is automatically saved as a string and is printed for each suite.
Tests can also be filtered using this attribute.
