===============================
Numa policy hit/miss statistics
===============================

/sys/devices/system/node/node*/numastat

All units are pages. Hugepages have separate counters.

=============== ============================================================
numa_hit	A process wanted to allocate memory from this node,
		and succeeded.

numa_miss	A process wanted to allocate memory from another node,
		but ended up with memory from this node.

numa_foreign	A process wanted to allocate on this node,
		but ended up with memory from another one.

local_node	A process ran on this node and got memory from it.

other_node	A process ran on this node and got memory from another node.

interleave_hit 	Interleaving wanted to allocate from this node
		and succeeded.
=============== ============================================================

For easier reading you can use the numastat utility from the numactl package
(http://oss.sgi.com/projects/libnuma/). Note that it only works
well right now on machines with a small number of CPUs.

