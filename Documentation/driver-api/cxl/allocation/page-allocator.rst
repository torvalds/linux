.. SPDX-License-Identifier: GPL-2.0

==================
The Page Allocator
==================

The kernel page allocator services all general page allocation requests, such
as :code:`kmalloc`.  CXL configuration steps affect the behavior of the page
allocator based on the selected `Memory Zone` and `NUMA node` the capacity is
placed in.

This section mostly focuses on how these configurations affect the page
allocator (as of Linux v6.15) rather than the overall page allocator behavior.

NUMA nodes and mempolicy
========================
Unless a task explicitly registers a mempolicy, the default memory policy
of the linux kernel is to allocate memory from the `local NUMA node` first,
and fall back to other nodes only if the local node is pressured.

Generally, we expect to see local DRAM and CXL memory on separate NUMA nodes,
with the CXL memory being non-local.  Technically, however, it is possible
for a compute node to have no local DRAM, and for CXL memory to be the
`local` capacity for that compute node.


Memory Zones
============
CXL capacity may be onlined in :code:`ZONE_NORMAL` or :code:`ZONE_MOVABLE`.

As of v6.15, the page allocator attempts to allocate from the highest
available and compatible ZONE for an allocation from the local node first.

An example of a `zone incompatibility` is attempting to service an allocation
marked :code:`GFP_KERNEL` from :code:`ZONE_MOVABLE`.  Kernel allocations are
typically not migratable, and as a result can only be serviced from
:code:`ZONE_NORMAL` or lower.

To simplify this, the page allocator will prefer :code:`ZONE_MOVABLE` over
:code:`ZONE_NORMAL` by default, but if :code:`ZONE_MOVABLE` is depleted, it
will fallback to allocate from :code:`ZONE_NORMAL`.


Zone and Node Quirks
====================
Let's consider a configuration where the local DRAM capacity is largely onlined
into :code:`ZONE_NORMAL`, with no :code:`ZONE_MOVABLE` capacity present. The
CXL capacity has the opposite configuration - all onlined in
:code:`ZONE_MOVABLE`.

Under the default allocation policy, the page allocator will completely skip
:code:`ZONE_MOVABLE` as a valid allocation target.  This is because, as of
Linux v6.15, the page allocator does (approximately) the following: ::

  for (each zone in local_node):

    for (each node in fallback_order):

      attempt_allocation(gfp_flags);

Because the local node does not have :code:`ZONE_MOVABLE`, the CXL node is
functionally unreachable for direct allocation.  As a result, the only way
for CXL capacity to be used is via `demotion` in the reclaim path.

This configuration also means that if the DRAM ndoe has :code:`ZONE_MOVABLE`
capacity - when that capacity is depleted, the page allocator will actually
prefer CXL :code:`ZONE_MOVABLE` pages over DRAM :code:`ZONE_NORMAL` pages.

We may wish to invert this priority in future Linux versions.

If `demotion` and `swap` are disabled, Linux will begin to cause OOM crashes
when the DRAM nodes are depleted. See the reclaim section for more details.


CGroups and CPUSets
===================
Finally, assuming CXL memory is reachable via the page allocation (i.e. onlined
in :code:`ZONE_NORMAL`), the :code:`cpusets.mems_allowed` may be used by
containers to limit the accessibility of certain NUMA nodes for tasks in that
container.  Users may wish to utilize this in multi-tenant systems where some
tasks prefer not to use slower memory.

In the reclaim section we'll discuss some limitations of this interface to
prevent demotions of shared data to CXL memory (if demotions are enabled).

