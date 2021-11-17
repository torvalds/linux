=====================
CMA Debugfs Interface
=====================

The CMA debugfs interface is useful to retrieve basic information out of the
different CMA areas and to test allocation/release in each of the areas.

Each CMA zone represents a directory under <debugfs>/cma/, indexed by the
kernel's CMA index. So the first CMA zone would be:

	<debugfs>/cma/cma-0

The structure of the files created under that directory is as follows:

 - [RO] base_pfn: The base PFN (Page Frame Number) of the zone.
 - [RO] count: Amount of memory in the CMA area.
 - [RO] order_per_bit: Order of pages represented by one bit.
 - [RO] bitmap: The bitmap of page states in the zone.
 - [WO] alloc: Allocate N pages from that CMA area. For example::

	echo 5 > <debugfs>/cma/cma-2/alloc

would try to allocate 5 pages from the cma-2 area.

 - [WO] free: Free N pages from that CMA area, similar to the above.
