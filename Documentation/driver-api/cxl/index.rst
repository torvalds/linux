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

.. toctree::
   :maxdepth: 1
   :caption: Linux Kernel Configuration

   linux/access-coordinates


.. only::  subproject and html
