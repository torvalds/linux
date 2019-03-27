======
Driver
======

Note: this document discuss Mach-O port of LLD. For ELF and COFF,
see :doc:`index`.

.. contents::
   :local:

Introduction
============

This document describes the lld driver. The purpose of this document is to
describe both the motivation and design goals for the driver, as well as details
of the internal implementation.

Overview
========

The lld driver is designed to support a number of different command line
interfaces. The main interfaces we plan to support are binutils' ld, Apple's
ld, and Microsoft's link.exe.

Flavors
-------

Each of these different interfaces is referred to as a flavor. There is also an
extra flavor "core" which is used to exercise the core functionality of the
linker it the test suite.

* gnu
* darwin
* link
* core

Selecting a Flavor
^^^^^^^^^^^^^^^^^^

There are two different ways to tell lld which flavor to be. They are checked in
order, so the second overrides the first. The first is to symlink :program:`lld`
as :program:`lld-{flavor}` or just :program:`{flavor}`. You can also specify
it as the first command line argument using ``-flavor``::

  $ lld -flavor gnu

There is a shortcut for ``-flavor core`` as ``-core``.


Adding an Option to an existing Flavor
======================================

#. Add the option to the desired :file:`lib/Driver/{flavor}Options.td`.

#. Add to :cpp:class:`lld::FlavorLinkingContext` a getter and setter method
   for the option.

#. Modify :cpp:func:`lld::FlavorDriver::parse` in :file:
   `lib/Driver/{Flavor}Driver.cpp` to call the targetInfo setter
   for corresponding to the option.

#. Modify {Flavor}Reader and {Flavor}Writer to use the new targtInfo option.


Adding a Flavor
===============

#. Add an entry for the flavor in :file:`include/lld/Common/Driver.h` to
   :cpp:class:`lld::UniversalDriver::Flavor`.

#. Add an entry in :file:`lib/Driver/UniversalDriver.cpp` to
   :cpp:func:`lld::Driver::strToFlavor` and
   :cpp:func:`lld::UniversalDriver::link`.
   This allows the flavor to be selected via symlink and `-flavor`.

#. Add a tablegen file called :file:`lib/Driver/{flavor}Options.td` that
   describes the options. If the options are a superset of another driver, that
   driver's td file can simply be included. The :file:`{flavor}Options.td` file
   must also be added to :file:`lib/Driver/CMakeLists.txt`.

#. Add a ``{flavor}Driver`` as a subclass of :cpp:class:`lld::Driver`
   in :file:`lib/Driver/{flavor}Driver.cpp`.
