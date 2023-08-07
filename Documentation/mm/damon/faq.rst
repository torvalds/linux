.. SPDX-License-Identifier: GPL-2.0

==========================
Frequently Asked Questions
==========================

Does DAMON support virtual memory only?
=======================================

No.  The core of the DAMON is address space independent.  The address space
specific monitoring operations including monitoring target regions
constructions and actual access checks can be implemented and configured on the
DAMON core by the users.  In this way, DAMON users can monitor any address
space with any access check technique.

Nonetheless, DAMON provides vma/rmap tracking and PTE Accessed bit check based
implementations of the address space dependent functions for the virtual memory
and the physical memory by default, for a reference and convenient use.


Can I simply monitor page granularity?
======================================

Yes.  You can do so by setting the ``min_nr_regions`` attribute higher than the
working set size divided by the page size.  Because the monitoring target
regions size is forced to be ``>=page size``, the region split will make no
effect.
