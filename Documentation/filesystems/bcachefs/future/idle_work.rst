Idle/background work classes design doc:

Right now, our behaviour at idle isn't ideal, it was designed for servers that
would be under sustained load, to keep pending work at a "medium" level, to
let work build up so we can process it in more efficient batches, while also
giving headroom for bursts in load.

But for desktops or mobile - scenarios where work is less sustained and power
usage is more important - we want to operate differently, with a "rush to
idle" so the system can go to sleep. We don't want to be dribbling out
background work while the system should be idle.

The complicating factor is that there are a number of background tasks, which
form a heirarchy (or a digraph, depending on how you divide it up) - one
background task may generate work for another.

Thus proper idle detection needs to model this heirarchy.

- Foreground writes
- Page cache writeback
- Copygc, rebalance
- Journal reclaim

When we implement idle detection and rush to idle, we need to be careful not
to disturb too much the existing behaviour that works reasonably well when the
system is under sustained load (or perhaps improve it in the case of
rebalance, which currently does not actively attempt to let work batch up).

SUSTAINED LOAD REGIME
---------------------

When the system is under continuous load, we want these jobs to run
continuously - this is perhaps best modelled with a P/D controller, where
they'll be trying to keep a target value (i.e. fragmented disk space,
available journal space) roughly in the middle of some range.

The goal under sustained load is to balance our ability to handle load spikes
without running out of x resource (free disk space, free space in the
journal), while also letting some work accumululate to be batched (or become
unnecessary).

For example, we don't want to run copygc too aggressively, because then it
will be evacuating buckets that would have become empty (been overwritten or
deleted) anyways, and we don't want to wait until we're almost out of free
space because then the system will behave unpredicably - suddenly we're doing
a lot more work to service each write and the system becomes much slower.

IDLE REGIME
-----------

When the system becomes idle, we should start flushing our pending work
quicker so the system can go to sleep.

Note that the definition of "idle" depends on where in the heirarchy a task
is - a task should start flushing work more quickly when the task above it has
stopped generating new work.

e.g. rebalance should start flushing more quickly when page cache writeback is
idle, and journal reclaim should only start flushing more quickly when both
copygc and rebalance are idle.

It's important to let work accumulate when more work is still incoming and we
still have room, because flushing is always more efficient if we let it batch
up. New writes may overwrite data before rebalance moves it, and tasks may be
generating more updates for the btree nodes that journal reclaim needs to flush.

On idle, how much work we do at each interval should be proportional to the
length of time we have been idle for. If we're idle only for a short duration,
we shouldn't flush everything right away; the system might wake up and start
generating new work soon, and flushing immediately might end up doing a lot of
work that would have been unnecessary if we'd allowed things to batch more.
 
To summarize, we will need:

 - A list of classes for background tasks that generate work, which will
   include one "foreground" class.
 - Tracking for each class - "Am I doing work, or have I gone to sleep?"
 - And each class should check the class above it when deciding how much work to issue.
