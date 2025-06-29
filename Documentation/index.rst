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

   Development process <process/development-process>
   Submitting patches <process/submitting-patches>
   Code of conduct <process/code-of-conduct>
   Maintainer handbook <maintainer/index>
   All development-process docs <process/index>


Internal API manuals
====================

Manuals for use by developers working to interface with the rest of the
kernel.

.. toctree::
   :maxdepth: 1

   Core API <core-api/index>
   Driver APIs <driver-api/index>
   Subsystems <subsystem-apis>
   Locking <locking/index>

Development tools and processes
===============================

Various other manuals with useful information for all kernel developers.

.. toctree::
   :maxdepth: 1

   Licensing rules <process/license-rules>
   Writing documentation <doc-guide/index>
   Development tools <dev-tools/index>
   Testing guide <dev-tools/testing-overview>
   Hacking guide <kernel-hacking/index>
   Tracing <trace/index>
   Fault injection <fault-injection/index>
   Livepatching <livepatch/index>
   Rust <rust/index>


User-oriented documentation
===========================

The following manuals are written for *users* of the kernel — those who are
trying to get it to work optimally on a given system and application
developers seeking information on the kernel's user-space APIs.

.. toctree::
   :maxdepth: 1

   Administration <admin-guide/index>
   Build system <kbuild/index>
   Reporting issues <admin-guide/reporting-issues.rst>
   Userspace tools <tools/index>
   Userspace API <userspace-api/index>

See also: the `Linux man pages <https://www.kernel.org/doc/man-pages/>`_,
which are kept separately from the kernel's own documentation.

Firmware-related documentation
==============================
The following holds information on the kernel's expectations regarding the
platform firmware.

.. toctree::
   :maxdepth: 1

   Firmware <firmware-guide/index>
   Firmware and Devicetree <devicetree/index>


Architecture-specific documentation
===================================

.. toctree::
   :maxdepth: 2

   CPU architectures <arch/index>


Other documentation
===================

There are several unsorted documents that don't seem to fit on other parts
of the documentation body, or may require some adjustments and/or conversion
to reStructuredText format, or are simply too old.

.. toctree::
   :maxdepth: 1

   Unsorted documentation <staging/index>


Translations
============

.. toctree::
   :maxdepth: 2

   Translations <translations/index>

Indices and tables
==================

* :ref:`genindex`
