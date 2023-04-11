.. SPDX-License-Identifier: GPL-2.0

==========================
DAMON: Data Access MONitor
==========================

DAMON is a Linux kernel subsystem that provides a framework for data access
monitoring and the monitoring results based system operations.  The core
monitoring mechanisms of DAMON (refer to :doc:`design` for the detail) make it

 - *accurate* (the monitoring output is useful enough for DRAM level memory
   management; It might not appropriate for CPU Cache levels, though),
 - *light-weight* (the monitoring overhead is low enough to be applied online),
   and
 - *scalable* (the upper-bound of the overhead is in constant range regardless
   of the size of target workloads).

Using this framework, therefore, the kernel can operate system in an
access-aware fashion.  Because the features are also exposed to the user space,
users who have special information about their workloads can write personalized
applications for better understanding and optimizations of their workloads and
systems.

For easier development of such systems, DAMON provides a feature called DAMOS
(DAMon-based Operation Schemes) in addition to the monitoring.  Using the
feature, DAMON users in both kernel and user spaces can do access-aware system
operations with no code but simple configurations.

.. toctree::
   :maxdepth: 2

   faq
   design
   api
   maintainer-profile
