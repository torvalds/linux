.. SPDX-License-Identifier: GPL-2.0

====================
Compute Express Link
====================

CXL device configuration has a complex handoff between platform (Hardware,
BIOS, EFI), OS (early boot, core kernel, driver), and user policy decisions
that have impacts on each other.  The docs here break up configurations steps.

.. toctree::
   :maxdepth: 2
   :caption: Overview

   theory-of-operation
   maturity-map
   conventions

.. toctree::
   :maxdepth: 2
   :caption: Device Reference

   devices/device-types

.. toctree::
   :maxdepth: 2
   :caption: Platform Configuration

   platform/bios-and-efi
   platform/acpi
   platform/cdat
   platform/example-configs

.. toctree::
   :maxdepth: 2
   :caption: Linux Kernel Configuration

   linux/overview
   linux/early-boot
   linux/cxl-driver
   linux/dax-driver
   linux/memory-hotplug
   linux/access-coordinates

.. toctree::
   :maxdepth: 2
   :caption: Memory Allocation

   allocation/dax
   allocation/page-allocator
   allocation/reclaim
   allocation/hugepages.rst

.. only::  subproject and html
