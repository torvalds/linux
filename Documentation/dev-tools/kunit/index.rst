.. SPDX-License-Identifier: GPL-2.0

=========================================
KUnit - Unit Testing for the Linux Kernel
=========================================

.. toctree::
	:maxdepth: 2

	start
	usage
	kunit-tool
	api/index
	style
	faq

What is KUnit?
==============

KUnit is a lightweight unit testing and mocking framework for the Linux kernel.

KUnit is heavily inspired by JUnit, Python's unittest.mock, and
Googletest/Googlemock for C++. KUnit provides facilities for defining unit test
cases, grouping related test cases into test suites, providing common
infrastructure for running tests, and much more.

KUnit consists of a kernel component, which provides a set of macros for easily
writing unit tests. Tests written against KUnit will run on kernel boot if
built-in, or when loaded if built as a module. These tests write out results to
the kernel log in `TAP <https://testanything.org/>`_ format.

To make running these tests (and reading the results) easier, KUnit offers
:doc:`kunit_tool <kunit-tool>`, which builds a `User Mode Linux
<http://user-mode-linux.sourceforge.net>`_ kernel, runs it, and parses the test
results. This provides a quick way of running KUnit tests during development,
without requiring a virtual machine or separate hardware.

Get started now: :doc:`start`

Why KUnit?
==========

A unit test is supposed to test a single unit of code in isolation, hence the
name. A unit test should be the finest granularity of testing and as such should
allow all possible code paths to be tested in the code under test; this is only
possible if the code under test is very small and does not have any external
dependencies outside of the test's control like hardware.

KUnit provides a common framework for unit tests within the kernel.

KUnit tests can be run on most architectures, and most tests are architecture
independent. All built-in KUnit tests run on kernel startup.  Alternatively,
KUnit and KUnit tests can be built as modules and tests will run when the test
module is loaded.

.. note::

        KUnit can also run tests without needing a virtual machine or actual
        hardware under User Mode Linux. User Mode Linux is a Linux architecture,
        like ARM or x86, which compiles the kernel as a Linux executable. KUnit
        can be used with UML either by building with ``ARCH=um`` (like any other
        architecture), or by using :doc:`kunit_tool <kunit-tool>`.

KUnit is fast. Excluding build time, from invocation to completion KUnit can run
several dozen tests in only 10 to 20 seconds; this might not sound like a big
deal to some people, but having such fast and easy to run tests fundamentally
changes the way you go about testing and even writing code in the first place.
Linus himself said in his `git talk at Google
<https://gist.github.com/lorn/1272686/revisions#diff-53c65572127855f1b003db4064a94573R874>`_:

	"... a lot of people seem to think that performance is about doing the
	same thing, just doing it faster, and that is not true. That is not what
	performance is all about. If you can do something really fast, really
	well, people will start using it differently."

In this context Linus was talking about branching and merging,
but this point also applies to testing. If your tests are slow, unreliable, are
difficult to write, and require a special setup or special hardware to run,
then you wait a lot longer to write tests, and you wait a lot longer to run
tests; this means that tests are likely to break, unlikely to test a lot of
things, and are unlikely to be rerun once they pass. If your tests are really
fast, you run them all the time, every time you make a change, and every time
someone sends you some code. Why trust that someone ran all their tests
correctly on every change when you can just run them yourself in less time than
it takes to read their test log?

How do I use it?
================

*   :doc:`start` - for new users of KUnit
*   :doc:`usage` - for a more detailed explanation of KUnit features
*   :doc:`api/index` - for the list of KUnit APIs used for testing
*   :doc:`kunit-tool` - for more information on the kunit_tool helper script
*   :doc:`faq` - for answers to some common questions about KUnit
