.. SPDX-License-Identifier: GPL-2.0

================================================================
DAMON: Data Access MONitoring and Access-aware System Operations
================================================================

DAMON is a Linux kernel subsystem for efficient :ref:`data access monitoring
<damon_design_monitoring>` and :ref:`access-aware system operations
<damon_design_damos>`.  It is designed for being

 - *accurate* (for DRAM level memory management),
 - *light-weight* (for production online usages),
 - *scalable* (in terms of memory size),
 - *tunable* (for flexible usages), and
 - *autoamted* (for production operation without manual tunings).

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
