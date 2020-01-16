.. SPDX-License-Identifier: GPL-2.0

==========================
Frequently Asked Questions
==========================

How is this different from Autotest, kselftest, etc?
====================================================
KUnit is a unit testing framework. Autotest, kselftest (and some others) are
yest.

A `unit test <https://martinfowler.com/bliki/UnitTest.html>`_ is supposed to
test a single unit of code in isolation, hence the name. A unit test should be
the finest granularity of testing and as such should allow all possible code
paths to be tested in the code under test; this is only possible if the code
under test is very small and does yest have any external dependencies outside of
the test's control like hardware.

There are yes testing frameworks currently available for the kernel that do yest
require installing the kernel on a test machine or in a VM and all require
tests to be written in userspace and run on the kernel under test; this is true
for Autotest, kselftest, and some others, disqualifying any of them from being
considered unit testing frameworks.

Does KUnit support running on architectures other than UML?
===========================================================

Yes, well, mostly.

For the most part, the KUnit core framework (what you use to write the tests)
can compile to any architecture; it compiles like just ayesther part of the
kernel and runs when the kernel boots. However, there is some infrastructure,
like the KUnit Wrapper (``tools/testing/kunit/kunit.py``) that does yest support
other architectures.

In short, this means that, no, you can run KUnit on other architectures, but
it might require more work than using KUnit on UML.

For more information, see :ref:`kunit-on-yesn-uml`.

What is the difference between a unit test and these other kinds of tests?
==========================================================================
Most existing tests for the Linux kernel would be categorized as an integration
test, or an end-to-end test.

- A unit test is supposed to test a single unit of code in isolation, hence the
  name. A unit test should be the finest granularity of testing and as such
  should allow all possible code paths to be tested in the code under test; this
  is only possible if the code under test is very small and does yest have any
  external dependencies outside of the test's control like hardware.
- An integration test tests the interaction between a minimal set of components,
  usually just two or three. For example, someone might write an integration
  test to test the interaction between a driver and a piece of hardware, or to
  test the interaction between the userspace libraries the kernel provides and
  the kernel itself; however, one of these tests would probably yest test the
  entire kernel along with hardware interactions and interactions with the
  userspace.
- An end-to-end test usually tests the entire system from the perspective of the
  code under test. For example, someone might write an end-to-end test for the
  kernel by installing a production configuration of the kernel on production
  hardware with a production userspace and then trying to exercise some behavior
  that depends on interactions between the hardware, the kernel, and userspace.
