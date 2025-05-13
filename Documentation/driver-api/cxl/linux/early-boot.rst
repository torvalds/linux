.. SPDX-License-Identifier: GPL-2.0

=======================
Linux Init (Early Boot)
=======================

Linux configuration is split into two major steps: Early-Boot and everything else.

During early boot, Linux sets up immutable resources (such as numa nodes), while
later operations include things like driver probe and memory hotplug.  Linux may
read EFI and ACPI information throughout this process to configure logical
representations of the devices.

During Linux Early Boot stage (functions in the kernel that have the __init
decorator), the system takes the resources created by EFI/BIOS
(:doc:`ACPI tables <../platform/acpi>`) and turns them into resources that the
kernel can consume.


BIOS, Build and Boot Options
============================

There are 4 pre-boot options that need to be considered during kernel build
which dictate how memory will be managed by Linux during early boot.

* EFI_MEMORY_SP

  * BIOS/EFI Option that dictates whether memory is SystemRAM or
    Specific Purpose.  Specific Purpose memory will be deferred to
    drivers to manage - and not immediately exposed as system RAM.

* CONFIG_EFI_SOFT_RESERVE

  * Linux Build config option that dictates whether the kernel supports
    Specific Purpose memory.

* CONFIG_MHP_DEFAULT_ONLINE_TYPE

  * Linux Build config that dictates whether and how Specific Purpose memory
    converted to a dax device should be managed (left as DAX or onlined as
    SystemRAM in ZONE_NORMAL or ZONE_MOVABLE).

* nosoftreserve

  * Linux kernel boot option that dictates whether Soft Reserve should be
    supported.  Similar to CONFIG_EFI_SOFT_RESERVE.

Memory Map Creation
===================

While the kernel parses the EFI memory map, if :code:`Specific Purpose` memory
is supported and detected, it will set this region aside as
:code:`SOFT_RESERVED`.

If :code:`EFI_MEMORY_SP=0`, :code:`CONFIG_EFI_SOFT_RESERVE=n`, or
:code:`nosoftreserve=y` - Linux will default a CXL device memory region to
SystemRAM.  This will expose the memory to the kernel page allocator in
:code:`ZONE_NORMAL`, making it available for use for most allocations (including
:code:`struct page` and page tables).

If `Specific Purpose` is set and supported, :code:`CONFIG_MHP_DEFAULT_ONLINE_TYPE_*`
dictates whether the memory is onlined by default (:code:`_OFFLINE` or
:code:`_ONLINE_*`), and if online which zone to online this memory to by default
(:code:`_NORMAL` or :code:`_MOVABLE`).

If placed in :code:`ZONE_MOVABLE`, the memory will not be available for most
kernel allocations (such as :code:`struct page` or page tables).  This may
significant impact performance depending on the memory capacity of the system.


NUMA Node Reservation
=====================

Linux refers to the proximity domains (:code:`PXM`) defined in the :doc:`SRAT
<../platform/acpi/srat>` to create NUMA nodes in :code:`acpi_numa_init`.
Typically, there is a 1:1 relation between :code:`PXM` and NUMA node IDs.

The SRAT is the only ACPI defined way of defining Proximity Domains. Linux
chooses to, at most, map those 1:1 with NUMA nodes.
:doc:`CEDT <../platform/acpi/cedt>` adds a description of SPA ranges which
Linux may map to one or more NUMA nodes.

If there are CXL ranges in the CFMWS but not in SRAT, then a fake :code:`PXM`
is created (as of v6.15). In the future, Linux may reject CFMWS not described
by SRAT due to the ambiguity of proximity domain association.

It is important to note that NUMA node creation cannot be done at runtime. All
possible NUMA nodes are identified at :code:`__init` time, more specifically
during :code:`mm_init`. The CEDT and SRAT must contain sufficient :code:`PXM`
data for Linux to identify NUMA nodes their associated memory regions.

The relevant code exists in: :code:`linux/drivers/acpi/numa/srat.c`.

See :doc:`Example Platform Configurations <../platform/example-configs>`
for more info.

Memory Tiers Creation
=====================
Memory tiers are a collection of NUMA nodes grouped by performance characteristics.
During :code:`__init`, Linux initializes the system with a default memory tier that
contains all nodes marked :code:`N_MEMORY`.

:code:`memory_tier_init` is called at boot for all nodes with memory online by
default. :code:`memory_tier_late_init` is called during late-init for nodes setup
during driver configuration.

Nodes are only marked :code:`N_MEMORY` if they have *online* memory.

Tier membership can be inspected in ::

  /sys/devices/virtual/memory_tiering/memory_tierN/nodelist
  0-1

If nodes are grouped which have clear difference in performance, check the
:doc:`HMAT <../platform/acpi/hmat>` and CDAT information for the CXL nodes. All
nodes default to the DRAM tier, unless HMAT/CDAT information is reported to the
memory_tier component via `access_coordinates`.

For more, see :doc:`CXL access coordinates documentation
<../linux/access-coordinates>`.

Contiguous Memory Allocation
============================
The contiguous memory allocator (CMA) enables reservation of contiguous memory
regions on NUMA nodes during early boot.  However, CMA cannot reserve memory
on NUMA nodes that are not online during early boot. ::

  void __init hugetlb_cma_reserve(int order) {
    if (!node_online(nid))
      /* do not allow reservations */
  }

This means if users intend to defer management of CXL memory to the driver, CMA
cannot be used to guarantee huge page allocations.  If enabling CXL memory as
SystemRAM in `ZONE_NORMAL` during early boot, CMA reservations per-node can be
made with the :code:`cma_pernuma` or :code:`numa_cma` kernel command line
parameters.
