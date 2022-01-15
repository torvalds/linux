.. SPDX-License-Identifier: GPL-2.0

==========================
DAMON: Data Access MONitor
==========================

DAMON is a data access monitoring framework subsystem for the Linux kernel.
The core mechanisms of DAMON (refer to :doc:`design` for the detail) make it

 - *accurate* (the monitoring output is useful enough for DRAM level memory
   management; It might not appropriate for CPU Cache levels, though),
 - *light-weight* (the monitoring overhead is low enough to be applied online),
   and
 - *scalable* (the upper-bound of the overhead is in constant range regardless
   of the size of target workloads).

Using this framework, therefore, the kernel's memory management mechanisms can
make advanced decisions.  Experimental memory management optimization works
that incurring high data accesses monitoring overhead could implemented again.
In user space, meanwhile, users who have some special workloads can write
personalized applications for better understanding and optimizations of their
workloads and systems.

.. toctree::
   :maxdepth: 2

   faq
   design
   api
   plans
