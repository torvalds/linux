======================
Linux Kernel Selftests
======================

The kernel contains a set of "self tests" under the tools/testing/selftests/
directory. These are intended to be small tests to exercise individual code
paths in the kernel. Tests are intended to be run after building, installing
and booting a kernel.

On some systems, hot-plug tests could hang forever waiting for cpu and
memory to be ready to be offlined. A special hot-plug target is created
to run full range of hot-plug tests. In default mode, hot-plug tests run
in safe mode with a limited scope. In limited mode, cpu-hotplug test is
run on a single cpu as opposed to all hotplug capable cpus, and memory
hotplug test is run on 2% of hotplug capable memory instead of 10%.

Running the selftests (hotplug tests are run in limited mode)
=============================================================

To build the tests::

  $ make -C tools/testing/selftests

To run the tests::

  $ make -C tools/testing/selftests run_tests

To build and run the tests with a single command, use::

  $ make kselftest

Note that some tests will require root privileges.

Build and run from user specific object directory (make O=dir)::

  $ make O=/tmp/kselftest kselftest

Build and run KBUILD_OUTPUT directory (make KBUILD_OUTPUT=)::

  $ make KBUILD_OUTPUT=/tmp/kselftest kselftest

The above commands run the tests and print pass/fail summary to make it
easier to understand the test results. Please find the detailed individual
test results for each test in /tmp/testname file(s).

Running a subset of selftests
=============================

You can use the "TARGETS" variable on the make command line to specify
single test to run, or a list of tests to run.

To run only tests targeted for a single subsystem::

  $ make -C tools/testing/selftests TARGETS=ptrace run_tests

You can specify multiple tests to build and run::

  $  make TARGETS="size timers" kselftest

Build and run from user specific object directory (make O=dir)::

  $ make O=/tmp/kselftest TARGETS="size timers" kselftest

Build and run KBUILD_OUTPUT directory (make KBUILD_OUTPUT=)::

  $ make KBUILD_OUTPUT=/tmp/kselftest TARGETS="size timers" kselftest

The above commands run the tests and print pass/fail summary to make it
easier to understand the test results. Please find the detailed individual
test results for each test in /tmp/testname file(s).

See the top-level tools/testing/selftests/Makefile for the list of all
possible targets.

Running the full range hotplug selftests
========================================

To build the hotplug tests::

  $ make -C tools/testing/selftests hotplug

To run the hotplug tests::

  $ make -C tools/testing/selftests run_hotplug

Note that some tests will require root privileges.


Install selftests
=================

You can use kselftest_install.sh tool installs selftests in default
location which is tools/testing/selftests/kselftest or a user specified
location.

To install selftests in default location::

   $ cd tools/testing/selftests
   $ ./kselftest_install.sh

To install selftests in a user specified location::

   $ cd tools/testing/selftests
   $ ./kselftest_install.sh install_dir

Running installed selftests
===========================

Kselftest install as well as the Kselftest tarball provide a script
named "run_kselftest.sh" to run the tests.

You can simply do the following to run the installed Kselftests. Please
note some tests will require root privileges::

   $ cd kselftest
   $ ./run_kselftest.sh

Contributing new tests
======================

In general, the rules for selftests are

 * Do as much as you can if you're not root;

 * Don't take too long;

 * Don't break the build on any architecture, and

 * Don't cause the top-level "make run_tests" to fail if your feature is
   unconfigured.

Contributing new tests (details)
================================

 * Use TEST_GEN_XXX if such binaries or files are generated during
   compiling.

   TEST_PROGS, TEST_GEN_PROGS mean it is the executable tested by
   default.

   TEST_CUSTOM_PROGS should be used by tests that require custom build
   rule and prevent common build rule use.

   TEST_PROGS are for test shell scripts. Please ensure shell script has
   its exec bit set. Otherwise, lib.mk run_tests will generate a warning.

   TEST_CUSTOM_PROGS and TEST_PROGS will be run by common run_tests.

   TEST_PROGS_EXTENDED, TEST_GEN_PROGS_EXTENDED mean it is the
   executable which is not tested by default.
   TEST_FILES, TEST_GEN_FILES mean it is the file which is used by
   test.

 * First use the headers inside the kernel source and/or git repo, and then the
   system headers.  Headers for the kernel release as opposed to headers
   installed by the distro on the system should be the primary focus to be able
   to find regressions.

 * If a test needs specific kernel config options enabled, add a config file in
   the test directory to enable them.

   e.g: tools/testing/selftests/android/config

Test Harness
============

The kselftest_harness.h file contains useful helpers to build tests.  The tests
from tools/testing/selftests/seccomp/seccomp_bpf.c can be used as example.

Example
-------

.. kernel-doc:: tools/testing/selftests/kselftest_harness.h
    :doc: example


Helpers
-------

.. kernel-doc:: tools/testing/selftests/kselftest_harness.h
    :functions: TH_LOG TEST TEST_SIGNAL FIXTURE FIXTURE_DATA FIXTURE_SETUP
                FIXTURE_TEARDOWN TEST_F TEST_HARNESS_MAIN

Operators
---------

.. kernel-doc:: tools/testing/selftests/kselftest_harness.h
    :doc: operators

.. kernel-doc:: tools/testing/selftests/kselftest_harness.h
    :functions: ASSERT_EQ ASSERT_NE ASSERT_LT ASSERT_LE ASSERT_GT ASSERT_GE
                ASSERT_NULL ASSERT_TRUE ASSERT_NULL ASSERT_TRUE ASSERT_FALSE
                ASSERT_STREQ ASSERT_STRNE EXPECT_EQ EXPECT_NE EXPECT_LT
                EXPECT_LE EXPECT_GT EXPECT_GE EXPECT_NULL EXPECT_TRUE
                EXPECT_FALSE EXPECT_STREQ EXPECT_STRNE
