.. SPDX-License-Identifier: GPL-2.0

==========
Huge Pages
==========

Contiguous Memory Allocator
===========================
CXL Memory onlined as SystemRAM during early boot is eligible for use by CMA,
as the NUMA node hosting that capacity will be `Online` at the time CMA
carves out contiguous capacity.

CXL Memory deferred to the CXL Driver for configuration cannot have its
capacity allocated by CMA - as the NUMA node hosting the capacity is `Offline`
at :code:`__init` time - when CMA carves out contiguous capacity.

HugeTLB
=======
Different huge page sizes allow different memory configurations.

2MB Huge Pages
--------------
All CXL capacity regardless of configuration time or memory zone is eligible
for use as 2MB huge pages.

1GB Huge Pages
--------------
CXL capacity onlined in :code:`ZONE_NORMAL` is eligible for 1GB Gigantic Page
allocation.

CXL capacity onlined in :code:`ZONE_MOVABLE` is not eligible for 1GB Gigantic
Page allocation.
