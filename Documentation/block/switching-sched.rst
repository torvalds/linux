===================
Switching Scheduler
===================

To choose IO schedulers at boot time, use the argument 'elevator=deadline'.
'noop' and 'cfq' (the default) are also available. IO schedulers are assigned
globally at boot time only presently.

Each io queue has a set of io scheduler tunables associated with it. These
tunables control how the io scheduler works. You can find these entries
in::

	/sys/block/<device>/queue/iosched

assuming that you have sysfs mounted on /sys. If you don't have sysfs mounted,
you can do so by typing::

	# mount none /sys -t sysfs

It is possible to change the IO scheduler for a given block device on
the fly to select one of mq-deadline, none, bfq, or kyber schedulers -
which can improve that device's throughput.

To set a specific scheduler, simply do this::

	echo SCHEDNAME > /sys/block/DEV/queue/scheduler

where SCHEDNAME is the name of a defined IO scheduler, and DEV is the
device name (hda, hdb, sga, or whatever you happen to have).

The list of defined schedulers can be found by simply doing
a "cat /sys/block/DEV/queue/scheduler" - the list of valid names
will be displayed, with the currently selected scheduler in brackets::

  # cat /sys/block/sda/queue/scheduler
  [mq-deadline] kyber bfq none
  # echo none >/sys/block/sda/queue/scheduler
  # cat /sys/block/sda/queue/scheduler
  [none] mq-deadline kyber bfq
