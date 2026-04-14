.. SPDX-License-Identifier: GPL-2.0

===============
Python unittest
===============

Checking consistency of python modules can be complex. Sometimes, it is
useful to define a set of unit tests to help checking them.

While the actual test implementation is usecase dependent, Python already
provides a standard way to add unit tests by using ``import unittest``.

Using such class, requires setting up a test suite. Also, the default format
is a little bit ackward. To improve it and provide a more uniform way to
report errors, some unittest classes and functions are defined.


Unittest helper module
======================

.. automodule:: lib.python.unittest_helper
   :members:
   :show-inheritance:
   :undoc-members:
