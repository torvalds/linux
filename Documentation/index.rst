.. SPDX-License-Identifier: GPL-2.0


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

* :ref:`kernel_licensing`

User-oriented documentation
---------------------------

The following manuals are written for *users* of the kernel — those who are
trying to get it to work optimally on a given system.

.. toctree::
   :maxdepth: 2

   admin-guide/index
   kbuild/index

Firmware-related documentation
------------------------------
The following holds information on the kernel's expectations regarding the
platform firmwares.

.. toctree::
   :maxdepth: 2

   firmware-guide/index
   devicetree/index

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
   fault-injection/index
   livepatch/index


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
   locking/index
   accounting/index
   block/index
   cdrom/index
   cpu-freq/index
   ide/index
   fb/index
   fpga/index
   hid/index
   i2c/index
   iio/index
   isdn/index
   infiniband/index
   leds/index
   netlabel/index
   networking/index
   pcmcia/index
   power/index
   target/index
   timers/index
   spi/index
   w1/index
   watchdog/index
   virt/index
   input/index
   hwmon/index
   gpu/index
   security/index
   sound/index
   crypto/index
   filesystems/index
   vm/index
   bpf/index
   usb/index
   PCI/index
   scsi/index
   misc-devices/index
   scheduler/index
   mhi/index

Architecture-agnostic documentation
-----------------------------------

.. toctree::
   :maxdepth: 2

   asm-annotations

Architecture-specific documentation
-----------------------------------

These books provide programming details about architecture-specific
implementation.

.. toctree::
   :maxdepth: 2

   arm/index
   arm64/index
   ia64/index
   m68k/index
   mips/index
   nios2/index
   openrisc/index
   parisc/index
   powerpc/index
   riscv/index
   s390/index
   sh/index
   sparc/index
   x86/index
   xtensa/index

Other documentation
-------------------

There are several unsorted documents that don't seem to fit on other parts
of the documentation body, or may require some adjustments and/or conversion
to ReStructured Text format, or are simply too old.

.. toctree::
   :maxdepth: 2

   staging/index
   watch_queue


Translations
------------

.. toctree::
   :maxdepth: 2

   translations/index

Indices and tables
==================

* :ref:`genindex`
