==================
Control Groupstats
==================

Control Groupstats is inspired by the discussion at
http://lkml.org/lkml/2007/4/11/187 and implements per cgroup statistics as
suggested by Andrew Morton in http://lkml.org/lkml/2007/4/11/263.

Per cgroup statistics infrastructure re-uses code from the taskstats
interface. A new set of cgroup operations are registered with commands
and attributes specific to cgroups. It should be very easy to
extend per cgroup statistics, by adding members to the cgroupstats
structure.

The current model for cgroupstats is a pull, a push model (to post
statistics on interesting events), should be very easy to add. Currently
user space requests for statistics by passing the cgroup path.
Statistics about the state of all the tasks in the cgroup is returned to
user space.

NOTE: We currently rely on delay accounting for extracting information
about tasks blocked on I/O. If CONFIG_TASK_DELAY_ACCT is disabled, this
information will not be available.

To extract cgroup statistics a utility very similar to getdelays.c
has been developed, the sample output of the utility is shown below::

  ~/balbir/cgroupstats # ./getdelays  -C "/sys/fs/cgroup/a"
  sleeping 1, blocked 0, running 1, stopped 0, uninterruptible 0
  ~/balbir/cgroupstats # ./getdelays  -C "/sys/fs/cgroup"
  sleeping 155, blocked 0, running 1, stopped 0, uninterruptible 2
