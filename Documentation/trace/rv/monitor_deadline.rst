Deadline monitors
=================

- Name: deadline
- Type: container for multiple monitors
- Author: Gabriele Monaco <gmonaco@redhat.com>

Description
-----------

The deadline monitor is a set of specifications to describe the deadline
scheduler behaviour. It includes monitors per scheduling entity (deadline tasks
and servers) that work independently to verify different specifications the
deadline scheduler should follow.

Specifications
--------------

Monitor nomiss
~~~~~~~~~~~~~~

The nomiss monitor ensures dl entities get to run *and* run to completion
before their deadline, although deferrable servers may not run. An entity is
considered done if ``throttled``, either because it yielded or used up its
runtime, or when it voluntarily starts ``sleeping``.
The monitor includes a user configurable deadline threshold. If the total
utilisation of deadline tasks is larger than 1, they are only guaranteed
bounded tardiness. See Documentation/scheduler/sched-deadline.rst for more
details. The threshold (module parameter ``nomiss.deadline_thresh``) can be
configured to avoid the monitor to fail based on the acceptable tardiness in
the system. Since ``dl_throttle`` is a valid outcome for the entity to be done,
the minimum tardiness needs be 1 tick to consider the throttle delay, unless
the ``HRTICK_DL`` scheduler feature is active.

Servers have also an intermediate ``idle`` state, occurring as soon as no
runnable task is available from ready or running where no timing constraint
is applied. A server goes to sleep by stopping, there is no wakeup equivalent
as the order of a server starting and replenishing is not defined, hence a
server can run from sleeping without being ready::

                                  |
  sched_wakeup                    v
  dl_replenish;reset(clk) -- #=========================#
               |             H                         H dl_replenish;reset(clk)
               +-----------> H                         H <--------------------+
                             H                         H                      |
      +- dl_server_stop ---- H          ready          H                      |
      |  +-----------------> H   clk < DEADLINE_NS()   H   dl_throttle;       |
      |  |                   H                         H     is_defer == 1    |
      |  | sched_switch_in - H                         H -----------------+   |
      |  |   |               #=========================#                  |   |
      |  |   |                       |            ^                       |   |
      |  |   |             dl_server_idle    dl_replenish;reset(clk)      |   |
      |  |   |                       v            |                       |   |
      |  |   |                      +--------------+                      |   |
      |  |   |              +------ |              |                      |   |
      |  |   |     dl_server_idle   |              | dl_throttle          |   |
      |  |   |              |       |     idle     | -----------------+   |   |
      |  |   |              +-----> |              |                  |   |   |
      |  |   |                      |              |                  |   |   |
      |  |   |                      |              |                  |   |   |
   +--+--+---+--- dl_server_stop -- +--------------+                  |   |   |
   |  |  |   |                       |           ^                    |   |   |
   |  |  |   |            sched_switch_in    dl_server_idle           |   |   |
   |  |  |   |                       v           |                    |   |   |
   |  |  |   |      +---------- +---------------------+               |   |   |
   |  |  |   | sched_switch_in  |                     |               |   |   |
   |  |  |   | sched_wakeup     |                     |               |   |   |
   |  |  |   | dl_replenish;    |      running        | -------+      |   |   |
   |  |  |   |      reset(clk)  | clk < DEADLINE_NS() |        |      |   |   |
   |  |  |   |      +---------> |                     | dl_throttle   |   |   |
   |  |  |   +----------------> |                     |        |      |   |   |
   |  |  |                      +---------------------+        |      |   |   |
   |  | sched_wakeup                ^   sched_switch_suspend   |      |   |   |
   v  v dl_replenish;reset(clk)     |   dl_server_stop         |      |   |   |
 +--------------+                   |   |                      v      v   v   |
 |              | - sched_switch_in +   |                     +---------------+
 |              | <---------------------+     dl_throttle +-- |               |
 |   sleeping   |                            sched_wakeup |   |   throttled   |
 |              | -- dl_server_stop        dl_server_idle +-> |               |
 |              |    dl_server_idle     sched_switch_suspend  +---------------+
 +--------------+ <---------+                                        ^
        |                                                            |
        +------ dl_throttle;is_constr_dl == 1 || is_defer == 1 ------+
