.. SPDX-License-Identifier: GPL-2.0

================================================================
DAMON: Data Access MONitoring and Access-aware System Operations
================================================================

DAMON is a Linux kernel subsystem that provides a framework for data access
monitoring and the monitoring results based system operations.  The core
monitoring :ref:`mechanisms <damon_design_monitoring>` of DAMON make it

 - *accurate* (the monitoring output is useful enough for DRAM level memory
   management; It might not appropriate for CPU Cache levels, though),
 - *light-weight* (the monitoring overhead is low enough to be applied online),
   and
 - *scalable* (the upper-bound of the overhead is in constant range regardless
   of the size of target workloads).

Using this framework, therefore, the kernel can operate system in an
access-aware fashion.  Because the features are also exposed to the :doc:`user
space </admin-guide/mm/damon/index>`, users who have special information about
their workloads can write personalized applications for better understanding
and optimizations of their workloads and systems.

For easier development of such systems, DAMON provides a feature called
:ref:`DAMOS <damon_design_damos>` (DAMon-based Operation Schemes) in addition
to the monitoring.  Using the feature, DAMON users in both kernel and :doc:`user
spaces </admin-guide/mm/damon/index>` can do access-aware system operations
with no code but simple configurations.

.. toctree::
   :maxdepth: 2

   faq
   design
   api
   maintainer-profile

To utilize and control DAMON from the user-space, please refer to the
administration :doc:`guide </admin-guide/mm/damon/index>`.

If you prefer academic papers for reading and citations, please use the papers
from `HPDC'22 <https://dl.acm.org/doi/abs/10.1145/3502181.3531466>`_ and
`Middleware19 Industry <https://dl.acm.org/doi/abs/10.1145/3366626.3368125>`_ .
Note that those cover DAMON implementations in Linux v5.16 and v5.15,
respectively.
