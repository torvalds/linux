.. SPDX-License-Identifier: GPL-2.0

.. _linux_doc:

==============================
The Linux Kernel documentation
==============================

This is the top level of the kernel's documentation tree.  Kernel
documentation, like the kernel itself, is very much a work in progress;
that is especially true as we work to integrate our many scattered
documents into a coherent whole.  Please note that improvements to the
documentation are welcome; join the linux-doc list at vger.kernel.org if
you want to help out.

Working with the development community
======================================

The essential guides for interacting with the kernel's development
community and getting your work upstream.

.. toctree::
   :maxdepth: 1

   process/development-process
   process/submitting-patches
   Code of conduct <process/code-of-conduct>
   maintainer/index
   All development-process docs <process/index>


Internal API manuals
====================

Manuals for use by developers working to interface with the rest of the
kernel.

.. toctree::
   :maxdepth: 1

   core-api/index
   driver-api/index
   subsystem-apis
   Locking in the kernel <locking/index>

Development tools and processes
===============================

Various other manuals with useful information for all kernel developers.

.. toctree::
   :maxdepth: 1

   process/license-rules
   doc-guide/index
   dev-tools/index
   dev-tools/testing-overview
   kernel-hacking/index
   trace/index
   fault-injection/index
   livepatch/index
   rust/index


User-oriented documentation
===========================

The following manuals are written for *users* of the kernel — those who are
trying to get it to work optimally on a given system and application
developers seeking information on the kernel's user-space APIs.

.. toctree::
   :maxdepth: 1

   admin-guide/index
   The kernel build system <kbuild/index>
   admin-guide/reporting-issues.rst
   User-space tools <tools/index>
   userspace-api/index

See also: the `Linux man pages <https://www.kernel.org/doc/man-pages/>`_,
which are kept separately from the kernel's own documentation.

Firmware-related documentation
==============================
The following holds information on the kernel's expectations regarding the
platform firmwares.

.. toctree::
   :maxdepth: 1

   firmware-guide/index
   devicetree/index


Architecture-specific documentation
===================================

.. toctree::
   :maxdepth: 2

   arch/index


Other documentation
===================

There are several unsorted documents that don't seem to fit on other parts
of the documentation body, or may require some adjustments and/or conversion
to ReStructured Text format, or are simply too old.

.. toctree::
   :maxdepth: 1

   staging/index


Translations
============

.. toctree::
   :maxdepth: 2

   translations/index

Indices and tables
==================

* :ref:`genindex`
