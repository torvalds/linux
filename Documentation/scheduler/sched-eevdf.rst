===============
EEVDF Scheduler
===============

The "Earliest Eligible Virtual Deadline First" (EEVDF) was first introduced
in a scientific publication in 1995 [1]. The Linux kernel began
transitioning to EEVDF in version 6.6 (as a new option in 2024), moving
away from the earlier Completely Fair Scheduler (CFS) in favor of a version
of EEVDF proposed by Peter Zijlstra in 2023 [2-4]. More information
regarding CFS can be found in
Documentation/scheduler/sched-design-CFS.rst.

Similarly to CFS, EEVDF aims to distribute CPU time equally among all
runnable tasks with the same priority. To do so, it assigns a virtual run
time to each task, creating a "lag" value that can be used to determine
whether a task has received its fair share of CPU time. In this way, a task
with a positive lag is owed CPU time, while a negative lag means the task
has exceeded its portion. EEVDF picks tasks with lag greater or equal to
zero and calculates a virtual deadline (VD) for each, selecting the task
with the earliest VD to execute next. It's important to note that this
allows latency-sensitive tasks with shorter time slices to be prioritized,
which helps with their responsiveness.

There are ongoing discussions on how to manage lag, especially for sleeping
tasks; but at the time of writing EEVDF uses a "decaying" mechanism based
on virtual run time (VRT). This prevents tasks from exploiting the system
by sleeping briefly to reset their negative lag: when a task sleeps, it
remains on the run queue but marked for "deferred dequeue," allowing its
lag to decay over VRT. Hence, long-sleeping tasks eventually have their lag
reset. Finally, tasks can preempt others if their VD is earlier, and tasks
can request specific time slices using the new sched_setattr() system call,
which further facilitates the job of latency-sensitive applications.

REFERENCES
==========

[1] https://citeseerx.ist.psu.edu/document?repid=rep1&type=pdf&doi=805acf7726282721504c8f00575d91ebfd750564

[2] https://lore.kernel.org/lkml/a79014e6-ea83-b316-1e12-2ae056bda6fa@linux.vnet.ibm.com/

[3] https://lwn.net/Articles/969062/

[4] https://lwn.net/Articles/925371/
