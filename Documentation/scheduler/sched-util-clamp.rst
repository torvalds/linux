.. SPDX-License-Identifier: GPL-2.0

====================
Utilization Clamping
====================

1. Introduction
===============

Utilization clamping, also known as util clamp or uclamp, is a scheduler
feature that allows user space to help in managing the performance requirement
of tasks. It was introduced in v5.3 release. The CGroup support was merged in
v5.4.

Uclamp is a hinting mechanism that allows the scheduler to understand the
performance requirements and restrictions of the tasks, thus it helps the
scheduler to make a better decision. And when schedutil cpufreq governor is
used, util clamp will influence the CPU frequency selection as well.

Since the scheduler and schedutil are both driven by PELT (util_avg) signals,
util clamp acts on that to achieve its goal by clamping the signal to a certain
point; hence the name. That is, by clamping utilization we are making the
system run at a certain performance point.

The right way to view util clamp is as a mechanism to make request or hint on
performance constraints. It consists of two tunables:

        * UCLAMP_MIN, which sets the lower bound.
        * UCLAMP_MAX, which sets the upper bound.

These two bounds will ensure a task will operate within this performance range
of the system. UCLAMP_MIN implies boosting a task, while UCLAMP_MAX implies
capping a task.

One can tell the system (scheduler) that some tasks require a minimum
performance point to operate at to deliver the desired user experience. Or one
can tell the system that some tasks should be restricted from consuming too
much resources and should not go above a specific performance point. Viewing
the uclamp values as performance points rather than utilization is a better
abstraction from user space point of view.

As an example, a game can use util clamp to form a feedback loop with its
perceived Frames Per Second (FPS). It can dynamically increase the minimum
performance point required by its display pipeline to ensure no frame is
dropped. It can also dynamically 'prime' up these tasks if it knows in the
coming few hundred milliseconds a computationally intensive scene is about to
happen.

On mobile hardware where the capability of the devices varies a lot, this
dynamic feedback loop offers a great flexibility to ensure best user experience
given the capabilities of any system.

Of course a static configuration is possible too. The exact usage will depend
on the system, application and the desired outcome.

Another example is in Android where tasks are classified as background,
foreground, top-app, etc. Util clamp can be used to constrain how much
resources background tasks are consuming by capping the performance point they
can run at. This constraint helps reserve resources for important tasks, like
the ones belonging to the currently active app (top-app group). Beside this
helps in limiting how much power they consume. This can be more obvious in
heterogeneous systems (e.g. Arm big.LITTLE); the constraint will help bias the
background tasks to stay on the little cores which will ensure that:

        1. The big cores are free to run top-app tasks immediately. top-app
           tasks are the tasks the user is currently interacting with, hence
           the most important tasks in the system.
        2. They don't run on a power hungry core and drain battery even if they
           are CPU intensive tasks.

.. note::
  **little cores**:
    CPUs with capacity < 1024

  **big cores**:
    CPUs with capacity = 1024

By making these uclamp performance requests, or rather hints, user space can
ensure system resources are used optimally to deliver the best possible user
experience.

Another use case is to help with **overcoming the ramp up latency inherit in
how scheduler utilization signal is calculated**.

On the other hand, a busy task for instance that requires to run at maximum
performance point will suffer a delay of ~200ms (PELT HALFIFE = 32ms) for the
scheduler to realize that. This is known to affect workloads like gaming on
mobile devices where frames will drop due to slow response time to select the
higher frequency required for the tasks to finish their work in time. Setting
UCLAMP_MIN=1024 will ensure such tasks will always see the highest performance
level when they start running.

The overall visible effect goes beyond better perceived user
experience/performance and stretches to help achieve a better overall
performance/watt if used effectively.

User space can form a feedback loop with the thermal subsystem too to ensure
the device doesn't heat up to the point where it will throttle.

Both SCHED_NORMAL/OTHER and SCHED_FIFO/RR honour uclamp requests/hints.

In the SCHED_FIFO/RR case, uclamp gives the option to run RT tasks at any
performance point rather than being tied to MAX frequency all the time. Which
can be useful on general purpose systems that run on battery powered devices.

Note that by design RT tasks don't have per-task PELT signal and must always
run at a constant frequency to combat undeterministic DVFS rampup delays.

Note that using schedutil always implies a single delay to modify the frequency
when an RT task wakes up. This cost is unchanged by using uclamp. Uclamp only
helps picking what frequency to request instead of schedutil always requesting
MAX for all RT tasks.

See :ref:`section 3.4 <uclamp-default-values>` for default values and
:ref:`3.4.1 <sched-util-clamp-min-rt-default>` on how to change RT tasks
default value.

2. Design
=========

Util clamp is a property of every task in the system. It sets the boundaries of
its utilization signal; acting as a bias mechanism that influences certain
decisions within the scheduler.

The actual utilization signal of a task is never clamped in reality. If you
inspect PELT signals at any point of time you should continue to see them as
they are intact. Clamping happens only when needed, e.g: when a task wakes up
and the scheduler needs to select a suitable CPU for it to run on.

Since the goal of util clamp is to allow requesting a minimum and maximum
performance point for a task to run on, it must be able to influence the
frequency selection as well as task placement to be most effective. Both of
which have implications on the utilization value at CPU runqueue (rq for short)
level, which brings us to the main design challenge.

When a task wakes up on an rq, the utilization signal of the rq will be
affected by the uclamp settings of all the tasks enqueued on it. For example if
a task requests to run at UTIL_MIN = 512, then the util signal of the rq needs
to respect to this request as well as all other requests from all of the
enqueued tasks.

To be able to aggregate the util clamp value of all the tasks attached to the
rq, uclamp must do some housekeeping at every enqueue/dequeue, which is the
scheduler hot path. Hence care must be taken since any slow down will have
significant impact on a lot of use cases and could hinder its usability in
practice.

The way this is handled is by dividing the utilization range into buckets
(struct uclamp_bucket) which allows us to reduce the search space from every
task on the rq to only a subset of tasks on the top-most bucket.

When a task is enqueued, the counter in the matching bucket is incremented,
and on dequeue it is decremented. This makes keeping track of the effective
uclamp value at rq level a lot easier.

As tasks are enqueued and dequeued, we keep track of the current effective
uclamp value of the rq. See :ref:`section 2.1 <uclamp-buckets>` for details on
how this works.

Later at any path that wants to identify the effective uclamp value of the rq,
it will simply need to read this effective uclamp value of the rq at that exact
moment of time it needs to take a decision.

For task placement case, only Energy Aware and Capacity Aware Scheduling
(EAS/CAS) make use of uclamp for now, which implies that it is applied on
heterogeneous systems only.
When a task wakes up, the scheduler will look at the current effective uclamp
value of every rq and compare it with the potential new value if the task were
to be enqueued there. Favoring the rq that will end up with the most energy
efficient combination.

Similarly in schedutil, when it needs to make a frequency update it will look
at the current effective uclamp value of the rq which is influenced by the set
of tasks currently enqueued there and select the appropriate frequency that
will satisfy constraints from requests.

Other paths like setting overutilization state (which effectively disables EAS)
make use of uclamp as well. Such cases are considered necessary housekeeping to
allow the 2 main use cases above and will not be covered in detail here as they
could change with implementation details.

.. _uclamp-buckets:

2.1. Buckets
------------

::

                           [struct rq]

  (bottom)                                                    (top)

    0                                                          1024
    |                                                           |
    +-----------+-----------+-----------+----   ----+-----------+
    |  Bucket 0 |  Bucket 1 |  Bucket 2 |    ...    |  Bucket N |
    +-----------+-----------+-----------+----   ----+-----------+
       :           :                                   :
       +- p0       +- p3                               +- p4
       :                                               :
       +- p1                                           +- p5
       :
       +- p2


.. note::
  The diagram above is an illustration rather than a true depiction of the
  internal data structure.

To reduce the search space when trying to decide the effective uclamp value of
an rq as tasks are enqueued/dequeued, the whole utilization range is divided
into N buckets where N is configured at compile time by setting
CONFIG_UCLAMP_BUCKETS_COUNT. By default it is set to 5.

The rq has a bucket for each uclamp_id tunables: [UCLAMP_MIN, UCLAMP_MAX].

The range of each bucket is 1024/N. For example, for the default value of
5 there will be 5 buckets, each of which will cover the following range:

::

        DELTA = round_closest(1024/5) = 204.8 = 205

        Bucket 0: [0:204]
        Bucket 1: [205:409]
        Bucket 2: [410:614]
        Bucket 3: [615:819]
        Bucket 4: [820:1024]

When a task p with following tunable parameters

::

        p->uclamp[UCLAMP_MIN] = 300
        p->uclamp[UCLAMP_MAX] = 1024

is enqueued into the rq, bucket 1 will be incremented for UCLAMP_MIN and bucket
4 will be incremented for UCLAMP_MAX to reflect the fact the rq has a task in
this range.

The rq then keeps track of its current effective uclamp value for each
uclamp_id.

When a task p is enqueued, the rq value changes to:

::

        // update bucket logic goes here
        rq->uclamp[UCLAMP_MIN] = max(rq->uclamp[UCLAMP_MIN], p->uclamp[UCLAMP_MIN])
        // repeat for UCLAMP_MAX

Similarly, when p is dequeued the rq value changes to:

::

        // update bucket logic goes here
        rq->uclamp[UCLAMP_MIN] = search_top_bucket_for_highest_value()
        // repeat for UCLAMP_MAX

When all buckets are empty, the rq uclamp values are reset to system defaults.
See :ref:`section 3.4 <uclamp-default-values>` for details on default values.


2.2. Max aggregation
--------------------

Util clamp is tuned to honour the request for the task that requires the
highest performance point.

When multiple tasks are attached to the same rq, then util clamp must make sure
the task that needs the highest performance point gets it even if there's
another task that doesn't need it or is disallowed from reaching this point.

For example, if there are multiple tasks attached to an rq with the following
values:

::

        p0->uclamp[UCLAMP_MIN] = 300
        p0->uclamp[UCLAMP_MAX] = 900

        p1->uclamp[UCLAMP_MIN] = 500
        p1->uclamp[UCLAMP_MAX] = 500

then assuming both p0 and p1 are enqueued to the same rq, both UCLAMP_MIN
and UCLAMP_MAX become:

::

        rq->uclamp[UCLAMP_MIN] = max(300, 500) = 500
        rq->uclamp[UCLAMP_MAX] = max(900, 500) = 900

As we shall see in :ref:`section 5.1 <uclamp-capping-fail>`, this max
aggregation is the cause of one of limitations when using util clamp, in
particular for UCLAMP_MAX hint when user space would like to save power.

2.3. Hierarchical aggregation
-----------------------------

As stated earlier, util clamp is a property of every task in the system. But
the actual applied (effective) value can be influenced by more than just the
request made by the task or another actor on its behalf (middleware library).

The effective util clamp value of any task is restricted as follows:

  1. By the uclamp settings defined by the cgroup CPU controller it is attached
     to, if any.
  2. The restricted value in (1) is then further restricted by the system wide
     uclamp settings.

:ref:`Section 3 <uclamp-interfaces>` discusses the interfaces and will expand
further on that.

For now suffice to say that if a task makes a request, its actual effective
value will have to adhere to some restrictions imposed by cgroup and system
wide settings.

The system will still accept the request even if effectively will be beyond the
constraints, but as soon as the task moves to a different cgroup or a sysadmin
modifies the system settings, the request will be satisfied only if it is
within new constraints.

In other words, this aggregation will not cause an error when a task changes
its uclamp values, but rather the system may not be able to satisfy requests
based on those factors.

2.4. Range
----------

Uclamp performance request has the range of 0 to 1024 inclusive.

For cgroup interface percentage is used (that is 0 to 100 inclusive).
Just like other cgroup interfaces, you can use 'max' instead of 100.

.. _uclamp-interfaces:

3. Interfaces
=============

3.1. Per task interface
-----------------------

sched_setattr() syscall was extended to accept two new fields:

* sched_util_min: requests the minimum performance point the system should run
  at when this task is running. Or lower performance bound.
* sched_util_max: requests the maximum performance point the system should run
  at when this task is running. Or upper performance bound.

For example, the following scenario have 40% to 80% utilization constraints:

::

        attr->sched_util_min = 40% * 1024;
        attr->sched_util_max = 80% * 1024;

When task @p is running, **the scheduler should try its best to ensure it
starts at 40% performance level**. If the task runs for a long enough time so
that its actual utilization goes above 80%, the utilization, or performance
level, will be capped.

The special value -1 is used to reset the uclamp settings to the system
default.

Note that resetting the uclamp value to system default using -1 is not the same
as manually setting uclamp value to system default. This distinction is
important because as we shall see in system interfaces, the default value for
RT could be changed. SCHED_NORMAL/OTHER might gain similar knobs too in the
future.

3.2. cgroup interface
---------------------

There are two uclamp related values in the CPU cgroup controller:

* cpu.uclamp.min
* cpu.uclamp.max

When a task is attached to a CPU controller, its uclamp values will be impacted
as follows:

* cpu.uclamp.min is a protection as described in :ref:`section 3-3 of cgroup
  v2 documentation <cgroupv2-protections-distributor>`.

  If a task uclamp_min value is lower than cpu.uclamp.min, then the task will
  inherit the cgroup cpu.uclamp.min value.

  In a cgroup hierarchy, effective cpu.uclamp.min is the max of (child,
  parent).

* cpu.uclamp.max is a limit as described in :ref:`section 3-2 of cgroup v2
  documentation <cgroupv2-limits-distributor>`.

  If a task uclamp_max value is higher than cpu.uclamp.max, then the task will
  inherit the cgroup cpu.uclamp.max value.

  In a cgroup hierarchy, effective cpu.uclamp.max is the min of (child,
  parent).

For example, given following parameters:

::

        p0->uclamp[UCLAMP_MIN] = // system default;
        p0->uclamp[UCLAMP_MAX] = // system default;

        p1->uclamp[UCLAMP_MIN] = 40% * 1024;
        p1->uclamp[UCLAMP_MAX] = 50% * 1024;

        cgroup0->cpu.uclamp.min = 20% * 1024;
        cgroup0->cpu.uclamp.max = 60% * 1024;

        cgroup1->cpu.uclamp.min = 60% * 1024;
        cgroup1->cpu.uclamp.max = 100% * 1024;

when p0 and p1 are attached to cgroup0, the values become:

::

        p0->uclamp[UCLAMP_MIN] = cgroup0->cpu.uclamp.min = 20% * 1024;
        p0->uclamp[UCLAMP_MAX] = cgroup0->cpu.uclamp.max = 60% * 1024;

        p1->uclamp[UCLAMP_MIN] = 40% * 1024; // intact
        p1->uclamp[UCLAMP_MAX] = 50% * 1024; // intact

when p0 and p1 are attached to cgroup1, these instead become:

::

        p0->uclamp[UCLAMP_MIN] = cgroup1->cpu.uclamp.min = 60% * 1024;
        p0->uclamp[UCLAMP_MAX] = cgroup1->cpu.uclamp.max = 100% * 1024;

        p1->uclamp[UCLAMP_MIN] = cgroup1->cpu.uclamp.min = 60% * 1024;
        p1->uclamp[UCLAMP_MAX] = 50% * 1024; // intact

Note that cgroup interfaces allows cpu.uclamp.max value to be lower than
cpu.uclamp.min. Other interfaces don't allow that.

3.3. System interface
---------------------

3.3.1 sched_util_clamp_min
--------------------------

System wide limit of allowed UCLAMP_MIN range. By default it is set to 1024,
which means that permitted effective UCLAMP_MIN range for tasks is [0:1024].
By changing it to 512 for example the range reduces to [0:512]. This is useful
to restrict how much boosting tasks are allowed to acquire.

Requests from tasks to go above this knob value will still succeed, but
they won't be satisfied until it is more than p->uclamp[UCLAMP_MIN].

The value must be smaller than or equal to sched_util_clamp_max.

3.3.2 sched_util_clamp_max
--------------------------

System wide limit of allowed UCLAMP_MAX range. By default it is set to 1024,
which means that permitted effective UCLAMP_MAX range for tasks is [0:1024].

By changing it to 512 for example the effective allowed range reduces to
[0:512]. This means is that no task can run above 512, which implies that all
rqs are restricted too. IOW, the whole system is capped to half its performance
capacity.

This is useful to restrict the overall maximum performance point of the system.
For example, it can be handy to limit performance when running low on battery
or when the system wants to limit access to more energy hungry performance
levels when it's in idle state or screen is off.

Requests from tasks to go above this knob value will still succeed, but they
won't be satisfied until it is more than p->uclamp[UCLAMP_MAX].

The value must be greater than or equal to sched_util_clamp_min.

.. _uclamp-default-values:

3.4. Default values
-------------------

By default all SCHED_NORMAL/SCHED_OTHER tasks are initialized to:

::

        p_fair->uclamp[UCLAMP_MIN] = 0
        p_fair->uclamp[UCLAMP_MAX] = 1024

That is, by default they're boosted to run at the maximum performance point of
changed at boot or runtime. No argument was made yet as to why we should
provide this, but can be added in the future.

For SCHED_FIFO/SCHED_RR tasks:

::

        p_rt->uclamp[UCLAMP_MIN] = 1024
        p_rt->uclamp[UCLAMP_MAX] = 1024

That is by default they're boosted to run at the maximum performance point of
the system which retains the historical behavior of the RT tasks.

RT tasks default uclamp_min value can be modified at boot or runtime via
sysctl. See below section.

.. _sched-util-clamp-min-rt-default:

3.4.1 sched_util_clamp_min_rt_default
-------------------------------------

Running RT tasks at maximum performance point is expensive on battery powered
devices and not necessary. To allow system developer to offer good performance
guarantees for these tasks without pushing it all the way to maximum
performance point, this sysctl knob allows tuning the best boost value to
address the system requirement without burning power running at maximum
performance point all the time.

Application developer are encouraged to use the per task util clamp interface
to ensure they are performance and power aware. Ideally this knob should be set
to 0 by system designers and leave the task of managing performance
requirements to the apps.

4. How to use util clamp
========================

Util clamp promotes the concept of user space assisted power and performance
management. At the scheduler level there is no info required to make the best
decision. However, with util clamp user space can hint to the scheduler to make
better decision about task placement and frequency selection.

Best results are achieved by not making any assumptions about the system the
application is running on and to use it in conjunction with a feedback loop to
dynamically monitor and adjust. Ultimately this will allow for a better user
experience at a better perf/watt.

For some systems and use cases, static setup will help to achieve good results.
Portability will be a problem in this case. How much work one can do at 100,
200 or 1024 is different for each system. Unless there's a specific target
system, static setup should be avoided.

There are enough possibilities to create a whole framework based on util clamp
or self contained app that makes use of it directly.

4.1. Boost important and DVFS-latency-sensitive tasks
-----------------------------------------------------

A GUI task might not be busy to warrant driving the frequency high when it
wakes up. However, it requires to finish its work within a specific time window
to deliver the desired user experience. The right frequency it requires at
wakeup will be system dependent. On some underpowered systems it will be high,
on other overpowered ones it will be low or 0.

This task can increase its UCLAMP_MIN value every time it misses the deadline
to ensure on next wake up it runs at a higher performance point. It should try
to approach the lowest UCLAMP_MIN value that allows to meet its deadline on any
particular system to achieve the best possible perf/watt for that system.

On heterogeneous systems, it might be important for this task to run on
a faster CPU.

**Generally it is advised to perceive the input as performance level or point
which will imply both task placement and frequency selection**.

4.2. Cap background tasks
-------------------------

Like explained for Android case in the introduction. Any app can lower
UCLAMP_MAX for some background tasks that don't care about performance but
could end up being busy and consume unnecessary system resources on the system.

4.3. Powersave mode
-------------------

sched_util_clamp_max system wide interface can be used to limit all tasks from
operating at the higher performance points which are usually energy
inefficient.

This is not unique to uclamp as one can achieve the same by reducing max
frequency of the cpufreq governor. It can be considered a more convenient
alternative interface.

4.4. Per-app performance restriction
------------------------------------

Middleware/Utility can provide the user an option to set UCLAMP_MIN/MAX for an
app every time it is executed to guarantee a minimum performance point and/or
limit it from draining system power at the cost of reduced performance for
these apps.

If you want to prevent your laptop from heating up while on the go from
compiling the kernel and happy to sacrifice performance to save power, but
still would like to keep your browser performance intact, uclamp makes it
possible.

5. Limitations
==============

.. _uclamp-capping-fail:

5.1. Capping frequency with uclamp_max fails under certain conditions
---------------------------------------------------------------------

If task p0 is capped to run at 512:

::

        p0->uclamp[UCLAMP_MAX] = 512

and it shares the rq with p1 which is free to run at any performance point:

::

        p1->uclamp[UCLAMP_MAX] = 1024

then due to max aggregation the rq will be allowed to reach max performance
point:

::

        rq->uclamp[UCLAMP_MAX] = max(512, 1024) = 1024

Assuming both p0 and p1 have UCLAMP_MIN = 0, then the frequency selection for
the rq will depend on the actual utilization value of the tasks.

If p1 is a small task but p0 is a CPU intensive task, then due to the fact that
both are running at the same rq, p1 will cause the frequency capping to be left
from the rq although p1, which is allowed to run at any performance point,
doesn't actually need to run at that frequency.

5.2. UCLAMP_MAX can break PELT (util_avg) signal
------------------------------------------------

PELT assumes that frequency will always increase as the signals grow to ensure
there's always some idle time on the CPU. But with UCLAMP_MAX, this frequency
increase will be prevented which can lead to no idle time in some
circumstances. When there's no idle time, a task will stuck in a busy loop,
which would result in util_avg being 1024.

Combing with issue described below, this can lead to unwanted frequency spikes
when severely capped tasks share the rq with a small non capped task.

As an example if task p, which have:

::

        p0->util_avg = 300
        p0->uclamp[UCLAMP_MAX] = 0

wakes up on an idle CPU, then it will run at min frequency (Fmin) this
CPU is capable of. The max CPU frequency (Fmax) matters here as well,
since it designates the shortest computational time to finish the task's
work on this CPU.

::

        rq->uclamp[UCLAMP_MAX] = 0

If the ratio of Fmax/Fmin is 3, then maximum value will be:

::

        300 * (Fmax/Fmin) = 900

which indicates the CPU will still see idle time since 900 is < 1024. The
_actual_ util_avg will not be 900 though, but somewhere between 300 and 900. As
long as there's idle time, p->util_avg updates will be off by a some margin,
but not proportional to Fmax/Fmin.

::

        p0->util_avg = 300 + small_error

Now if the ratio of Fmax/Fmin is 4, the maximum value becomes:

::

        300 * (Fmax/Fmin) = 1200

which is higher than 1024 and indicates that the CPU has no idle time. When
this happens, then the _actual_ util_avg will become:

::

        p0->util_avg = 1024

If task p1 wakes up on this CPU, which have:

::

        p1->util_avg = 200
        p1->uclamp[UCLAMP_MAX] = 1024

then the effective UCLAMP_MAX for the CPU will be 1024 according to max
aggregation rule. But since the capped p0 task was running and throttled
severely, then the rq->util_avg will be:

::

        p0->util_avg = 1024
        p1->util_avg = 200

        rq->util_avg = 1024
        rq->uclamp[UCLAMP_MAX] = 1024

Hence lead to a frequency spike since if p0 wasn't throttled we should get:

::

        p0->util_avg = 300
        p1->util_avg = 200

        rq->util_avg = 500

and run somewhere near mid performance point of that CPU, not the Fmax we get.

5.3. Schedutil response time issues
-----------------------------------

schedutil has three limitations:

        1. Hardware takes non-zero time to respond to any frequency change
           request. On some platforms can be in the order of few ms.
        2. Non fast-switch systems require a worker deadline thread to wake up
           and perform the frequency change, which adds measurable overhead.
        3. schedutil rate_limit_us drops any requests during this rate_limit_us
           window.

If a relatively small task is doing critical job and requires a certain
performance point when it wakes up and starts running, then all these
limitations will prevent it from getting what it wants in the time scale it
expects.

This limitation is not only impactful when using uclamp, but will be more
prevalent as we no longer gradually ramp up or down. We could easily be
jumping between frequencies depending on the order tasks wake up, and their
respective uclamp values.

We regard that as a limitation of the capabilities of the underlying system
itself.

There is room to improve the behavior of schedutil rate_limit_us, but not much
to be done for 1 or 2. They are considered hard limitations of the system.
