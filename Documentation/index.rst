.. The Linux Kernel documentation master file, created by
   sphinx-quickstart on Fri Feb 12 13:51:46 2016.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. _linux_doc:

The Linux Kernel documentation
==============================

This is the top level of the kernel's documentation tree.  Kernel
documentation, like the kernel itself, is very much a work in progress;
that is especially true as we work to integrate our many scattered
documents into a coherent whole.  Please note that improvements to the
documentation are welcome; join the linux-doc list at vger.kernel.org if
you want to help out.

Licensing documentation
-----------------------

The following describes the license of the Linux kernel source code
(GPLv2), how to properly mark the license of individual files in the source
tree, as well as links to the full license text.

.. toctree::
   :maxdepth: 2

   process/license-rules.rst

User-oriented documentation
---------------------------

The following manuals are written for *users* of the kernel — those who are
trying to get it to work optimally on a given system.

.. toctree::
   :maxdepth: 2

   admin-guide/index

Application-developer documentation
-----------------------------------

The user-space API manual gathers together documents describing aspects of
the kernel interface as seen by application developers.

.. toctree::
   :maxdepth: 2

   userspace-api/index


Introduction to kernel development
----------------------------------

These manuals contain overall information about how to develop the kernel.
The kernel community is quite large, with thousands of developers
contributing over the course of a year.  As with any large community,
knowing how things are done will make the process of getting your changes
merged much easier.

.. toctree::
   :maxdepth: 2

   process/index
   dev-tools/index
   doc-guide/index
   kernel-hacking/index
   trace/index
   maintainer/index

Kernel API documentation
------------------------

These books get into the details of how specific kernel subsystems work
from the point of view of a kernel developer.  Much of the information here
is taken directly from the kernel source, with supplemental material added
as needed (or at least as we managed to add it — probably *not* all that is
needed).

.. toctree::
   :maxdepth: 2

   driver-api/index
   core-api/index
   media/index
   networking/index
   input/index
   gpu/index
   security/index
   sound/index
   crypto/index
   filesystems/index
   vm/index
   bpf/index

Architecture-specific documentation
-----------------------------------

These books provide programming details about architecture-specific
implementation.

.. toctree::
   :maxdepth: 2

   sh/index
   x86/index

Filesystem Documentation
------------------------

The documentation in this section are provided by specific filesystem
subprojects.

.. toctree::
   :maxdepth: 2

   filesystems/ext4/index

Translations
------------

.. toctree::
   :maxdepth: 2

   translations/index

Indices and tables
==================

* :ref:`genindex`
