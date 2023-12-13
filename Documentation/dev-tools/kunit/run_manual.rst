.. SPDX-License-Identifier: GPL-2.0

============================
Run Tests without kunit_tool
============================

If we do not want to use kunit_tool (For example: we want to integrate
with other systems, or run tests on real hardware), we can
include KUnit in any kernel, read out results, and parse manually.

.. note:: KUnit is not designed for use in a production system. It is
          possible that tests may reduce the stability or security of
          the system.

Configure the Kernel
====================

KUnit tests can run without kunit_tool. This can be useful, if:

- We have an existing kernel configuration to test.
- Need to run on real hardware (or using an emulator/VM kunit_tool
  does not support).
- Wish to integrate with some existing testing systems.

KUnit is configured with the ``CONFIG_KUNIT`` option, and individual
tests can also be built by enabling their config options in our
``.config``. KUnit tests usually (but don't always) have config options
ending in ``_KUNIT_TEST``. Most tests can either be built as a module,
or be built into the kernel.

.. note ::

	We can enable the ``KUNIT_ALL_TESTS`` config option to
	automatically enable all tests with satisfied dependencies. This is
	a good way of quickly testing everything applicable to the current
	config.

Once we have built our kernel (and/or modules), it is simple to run
the tests. If the tests are built-in, they will run automatically on the
kernel boot. The results will be written to the kernel log (``dmesg``)
in TAP format.

If the tests are built as modules, they will run when the module is
loaded.

.. code-block :: bash

	# modprobe example-test

The results will appear in TAP format in ``dmesg``.

debugfs
=======

KUnit can be accessed from userspace via the debugfs filesystem (See more
information about debugfs at Documentation/filesystems/debugfs.rst).

If ``CONFIG_KUNIT_DEBUGFS`` is enabled, the KUnit debugfs filesystem is
mounted at /sys/kernel/debug/kunit. You can use this filesystem to perform
the following actions.

Retrieve Test Results
=====================

You can use debugfs to retrieve KUnit test results. The test results are
accessible from the debugfs filesystem in the following read-only file:

.. code-block :: bash

	/sys/kernel/debug/kunit/<test_suite>/results

The test results are printed in a KTAP document. Note this document is separate
to the kernel log and thus, may have different test suite numbering.

Run Tests After Kernel Has Booted
=================================

You can use the debugfs filesystem to trigger built-in tests to run after
boot. To run the test suite, you can use the following command to write to
the ``/sys/kernel/debug/kunit/<test_suite>/run`` file:

.. code-block :: bash

	echo "any string" > /sys/kernel/debugfs/kunit/<test_suite>/run

As a result, the test suite runs and the results are printed to the kernel
log.

However, this feature is not available with KUnit suites that use init data,
because init data may have been discarded after the kernel boots. KUnit
suites that use init data should be defined using the
kunit_test_init_section_suites() macro.

Also, you cannot use this feature to run tests concurrently. Instead a test
will wait to run until other tests have completed or failed.

.. note ::

	For test authors, to use this feature, tests will need to correctly initialise
	and/or clean up any data, so the test runs correctly a second time.
