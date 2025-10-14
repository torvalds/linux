================
Delay accounting
================

Tasks encounter delays in execution when they wait
for some kernel resource to become available e.g. a
runnable task may wait for a free CPU to run on.

The per-task delay accounting functionality measures
the delays experienced by a task while

a) waiting for a CPU (while being runnable)
b) completion of synchronous block I/O initiated by the task
c) swapping in pages
d) memory reclaim
e) thrashing
f) direct compact
g) write-protect copy
h) IRQ/SOFTIRQ

and makes these statistics available to userspace through
the taskstats interface.

Such delays provide feedback for setting a task's cpu priority,
io priority and rss limit values appropriately. Long delays for
important tasks could be a trigger for raising its corresponding priority.

The functionality, through its use of the taskstats interface, also provides
delay statistics aggregated for all tasks (or threads) belonging to a
thread group (corresponding to a traditional Unix process). This is a commonly
needed aggregation that is more efficiently done by the kernel.

Userspace utilities, particularly resource management applications, can also
aggregate delay statistics into arbitrary groups. To enable this, delay
statistics of a task are available both during its lifetime as well as on its
exit, ensuring continuous and complete monitoring can be done.


Interface
---------

Delay accounting uses the taskstats interface which is described
in detail in a separate document in this directory. Taskstats returns a
generic data structure to userspace corresponding to per-pid and per-tgid
statistics. The delay accounting functionality populates specific fields of
this structure. See

     include/uapi/linux/taskstats.h

for a description of the fields pertaining to delay accounting.
It will generally be in the form of counters returning the cumulative
delay seen for cpu, sync block I/O, swapin, memory reclaim, thrash page
cache, direct compact, write-protect copy, IRQ/SOFTIRQ etc.

Taking the difference of two successive readings of a given
counter (say cpu_delay_total) for a task will give the delay
experienced by the task waiting for the corresponding resource
in that interval.

When a task exits, records containing the per-task statistics
are sent to userspace without requiring a command. If it is the last exiting
task of a thread group, the per-tgid statistics are also sent. More details
are given in the taskstats interface description.

The getdelays.c userspace utility in tools/accounting directory allows simple
commands to be run and the corresponding delay statistics to be displayed. It
also serves as an example of using the taskstats interface.

Usage
-----

Compile the kernel with::

	CONFIG_TASK_DELAY_ACCT=y
	CONFIG_TASKSTATS=y

Delay accounting is disabled by default at boot up.
To enable, add::

   delayacct

to the kernel boot options. The rest of the instructions below assume this has
been done. Alternatively, use sysctl kernel.task_delayacct to switch the state
at runtime. Note however that only tasks started after enabling it will have
delayacct information.

After the system has booted up, use a utility
similar to  getdelays.c to access the delays
seen by a given task or a task group (tgid).
The utility also allows a given command to be
executed and the corresponding delays to be
seen.

General format of the getdelays command::

	getdelays [-dilv] [-t tgid] [-p pid]

Get delays, since system boot, for pid 10::

	# ./getdelays -d -p 10
	(output similar to next case)

Get sum and peak of delays, since system boot, for all pids with tgid 242::

	bash-4.4# ./getdelays -d -t 242
	print delayacct stats ON
	TGID    242


	CPU         count     real total  virtual total    delay total  delay average      delay max      delay min
	               39      156000000      156576579        2111069          0.054ms     0.212296ms     0.031307ms
	IO          count    delay total  delay average      delay max      delay min
	                0              0          0.000ms     0.000000ms     0.000000ms
	SWAP        count    delay total  delay average      delay max      delay min
	                0              0          0.000ms     0.000000ms     0.000000ms
	RECLAIM     count    delay total  delay average      delay max      delay min
	                0              0          0.000ms     0.000000ms     0.000000ms
	THRASHING   count    delay total  delay average      delay max      delay min
	                0              0          0.000ms     0.000000ms     0.000000ms
	COMPACT     count    delay total  delay average      delay max      delay min
	                0              0          0.000ms     0.000000ms     0.000000ms
	WPCOPY      count    delay total  delay average      delay max      delay min
	              156       11215873          0.072ms     0.207403ms     0.033913ms
	IRQ         count    delay total  delay average      delay max      delay min
	                0              0          0.000ms     0.000000ms     0.000000ms

Get IO accounting for pid 1, it works only with -p::

	# ./getdelays -i -p 1
	printing IO accounting
	linuxrc: read=65536, write=0, cancelled_write=0

The above command can be used with -v to get more debug information.

After the system starts, use `delaytop` to get the system-wide delay information,
which includes system-wide PSI information and Top-N high-latency tasks.
Note: PSI support requires `CONFIG_PSI=y` and `psi=1` for full functionality.

`delaytop` is an interactive tool for monitoring system pressure and task delays.
It supports multiple sorting options, display modes, and real-time keyboard controls.

Basic usage with default settings (sorts by CPU delay, shows top 20 tasks, refreshes every 2 seconds)::

	bash# ./delaytop
	System Pressure Information: (avg10/avg60vg300/total)
	CPU some:       0.0%/   0.0%/   0.0%/  106137(ms)
	CPU full:       0.0%/   0.0%/   0.0%/       0(ms)
	Memory full:    0.0%/   0.0%/   0.0%/       0(ms)
	Memory some:    0.0%/   0.0%/   0.0%/       0(ms)
	IO full:        0.0%/   0.0%/   0.0%/    2240(ms)
	IO some:        0.0%/   0.0%/   0.0%/    2783(ms)
	IRQ full:       0.0%/   0.0%/   0.0%/       0(ms)
	[o]sort [M]memverbose [q]quit
	Top 20 processes (sorted by cpu delay):
		PID      TGID  COMMAND           CPU(ms)   IO(ms)  IRQ(ms)  MEM(ms)
	------------------------------------------------------------------------
		110       110  kworker/15:0H-s   27.91     0.00     0.00     0.00
		57        57  cpuhp/7            3.18     0.00     0.00     0.00
		99        99  cpuhp/14           2.97     0.00     0.00     0.00
		51        51  cpuhp/6            0.90     0.00     0.00     0.00
		44        44  kworker/4:0H-sy    0.80     0.00     0.00     0.00
		60        60  ksoftirqd/7        0.74     0.00     0.00     0.00
		76        76  idle_inject/10     0.31     0.00     0.00     0.00
		100       100  idle_inject/14     0.30     0.00     0.00     0.00
		1309      1309  systemsettings     0.29     0.00     0.00     0.00
		45        45  cpuhp/5            0.22     0.00     0.00     0.00
		63        63  cpuhp/8            0.20     0.00     0.00     0.00
		87        87  cpuhp/12           0.18     0.00     0.00     0.00
		93        93  cpuhp/13           0.17     0.00     0.00     0.00
		1265      1265  acpid              0.17     0.00     0.00     0.00
		1552      1552  sshd               0.17     0.00     0.00     0.00
		2584      2584  sddm-helper        0.16     0.00     0.00     0.00
		1284      1284  rtkit-daemon       0.15     0.00     0.00     0.00
		1326      1326  nde-netfilter      0.14     0.00     0.00     0.00
		27        27  cpuhp/2            0.13     0.00     0.00     0.00
		631       631  kworker/11:2-rc    0.11     0.00     0.00     0.00

Interactive keyboard controls during runtime::

	o - Select sort field (CPU, IO, IRQ, Memory, etc.)
	M - Toggle display mode (Default/Memory Verbose)
	q - Quit

Available sort fields(use -s/--sort or interactive command)::

	cpu(c)       - CPU delay
	blkio(i)     - I/O delay
	irq(q)       - IRQ delay
	mem(m)       - Total memory delay
	swapin(s)    - Swapin delay (memory verbose mode only)
	freepages(r) - Freepages reclaim delay (memory verbose mode only)
	thrashing(t) - Thrashing delay (memory verbose mode only)
	compact(p)   - Compaction delay (memory verbose mode only)
	wpcopy(w)    - Write page copy delay (memory verbose mode only)

Advanced usage examples::

	# ./delaytop -s blkio
	Sorted by IO delay

	# ./delaytop -s mem -M
	Sorted by memory delay in memory verbose mode

	# ./delaytop -p pid
	Print delayacct stats

	# ./delaytop -P num
	Display the top N tasks

	# ./delaytop -n num
	Set delaytop refresh frequency (num times)

	# ./delaytop -d secs
	Specify refresh interval as secs
