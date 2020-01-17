===============================
Numa policy hit/miss statistics
===============================

/sys/devices/system/yesde/yesde*/numastat

All units are pages. Hugepages have separate counters.

=============== ============================================================
numa_hit	A process wanted to allocate memory from this yesde,
		and succeeded.

numa_miss	A process wanted to allocate memory from ayesther yesde,
		but ended up with memory from this yesde.

numa_foreign	A process wanted to allocate on this yesde,
		but ended up with memory from ayesther one.

local_yesde	A process ran on this yesde and got memory from it.

other_yesde	A process ran on this yesde and got memory from ayesther yesde.

interleave_hit 	Interleaving wanted to allocate from this yesde
		and succeeded.
=============== ============================================================

For easier reading you can use the numastat utility from the numactl package
(http://oss.sgi.com/projects/libnuma/). Note that it only works
well right yesw on machines with a small number of CPUs.

