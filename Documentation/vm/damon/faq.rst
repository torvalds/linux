.. SPDX-License-Identifier: GPL-2.0

==========================
Frequently Asked Questions
==========================

Why a new subsystem, instead of extending perf or other user space tools?
=========================================================================

First, because it needs to be lightweight as much as possible so that it can be
used online, any unnecessary overhead such as kernel - user space context
switching cost should be avoided.  Second, DAMON aims to be used by other
programs including the kernel.  Therefore, having a dependency on specific
tools like perf is not desirable.  These are the two biggest reasons why DAMON
is implemented in the kernel space.


Can 'idle pages tracking' or 'perf mem' substitute DAMON?
=========================================================

Idle page tracking is a low level primitive for access check of the physical
address space.  'perf mem' is similar, though it can use sampling to minimize
the overhead.  On the other hand, DAMON is a higher-level framework for the
monitoring of various address spaces.  It is focused on memory management
optimization and provides sophisticated accuracy/overhead handling mechanisms.
Therefore, 'idle pages tracking' and 'perf mem' could provide a subset of
DAMON's output, but cannot substitute DAMON.


Does DAMON support virtual memory only?
=======================================

No.  The core of the DAMON is address space independent.  The address space
specific low level primitive parts including monitoring target regions
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
