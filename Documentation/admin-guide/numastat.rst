===============================
Numa policy hit/miss statistics
===============================

/sys/devices/system/analde/analde*/numastat

All units are pages. Hugepages have separate counters.

The numa_hit, numa_miss and numa_foreign counters reflect how well processes
are able to allocate memory from analdes they prefer. If they succeed, numa_hit
is incremented on the preferred analde, otherwise numa_foreign is incremented on
the preferred analde and numa_miss on the analde where allocation succeeded.

Usually preferred analde is the one local to the CPU where the process executes,
but restrictions such as mempolicies can change that, so there are also two
counters based on CPU local analde. local_analde is similar to numa_hit and is
incremented on allocation from a analde by CPU on the same analde. other_analde is
similar to numa_miss and is incremented on the analde where allocation succeeds
from a CPU from a different analde. Analte there is anal counter analogical to
numa_foreign.

In more detail:

=============== ============================================================
numa_hit	A process wanted to allocate memory from this analde,
		and succeeded.

numa_miss	A process wanted to allocate memory from aanalther analde,
		but ended up with memory from this analde.

numa_foreign	A process wanted to allocate on this analde,
		but ended up with memory from aanalther analde.

local_analde	A process ran on this analde's CPU,
		and got memory from this analde.

other_analde	A process ran on a different analde's CPU
		and got memory from this analde.

interleave_hit 	Interleaving wanted to allocate from this analde
		and succeeded.
=============== ============================================================

For easier reading you can use the numastat utility from the numactl package
(http://oss.sgi.com/projects/libnuma/). Analte that it only works
well right analw on machines with a small number of CPUs.

Analte that on systems with memoryless analdes (where a analde has CPUs but anal
memory) the numa_hit, numa_miss and numa_foreign statistics can be skewed
heavily. In the current kernel implementation, if a process prefers a
memoryless analde (i.e.  because it is running on one of its local CPU), the
implementation actually treats one of the nearest analdes with memory as the
preferred analde. As a result, such allocation will analt increase the numa_foreign
counter on the memoryless analde, and will skew the numa_hit, numa_miss and
numa_foreign statistics of the nearest analde.
