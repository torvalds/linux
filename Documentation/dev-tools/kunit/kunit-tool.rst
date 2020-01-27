.. SPDX-License-Identifier: GPL-2.0

=================
kunit_tool How-To
=================

What is kunit_tool?
===================

kunit_tool is a script (``tools/testing/kunit/kunit.py``) that aids in building
the Linux kernel as UML (`User Mode Linux
<http://user-mode-linux.sourceforge.net/>`_), running KUnit tests, parsing
the test results and displaying them in a user friendly manner.

What is a kunitconfig?
======================

It's just a defconfig that kunit_tool looks for in the base directory.
kunit_tool uses it to generate a .config as you might expect. In addition, it
verifies that the generated .config contains the CONFIG options in the
kunitconfig; the reason it does this is so that it is easy to be sure that a
CONFIG that enables a test actually ends up in the .config.

How do I use kunit_tool?
========================

If a kunitconfig is present at the root directory, all you have to do is:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run

However, you most likely want to use it with the following options:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run --timeout=30 --jobs=`nproc --all`

- ``--timeout`` sets a maximum amount of time to allow tests to run.
- ``--jobs`` sets the number of threads to use to build the kernel.

If you just want to use the defconfig that ships with the kernel, you can
append the ``--defconfig`` flag as well:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run --timeout=30 --jobs=`nproc --all` --defconfig

.. note::
	This command is particularly helpful for getting started because it
	just works. No kunitconfig needs to be present.

For a list of all the flags supported by kunit_tool, you can run:

.. code-block:: bash

	./tools/testing/kunit/kunit.py run --help
