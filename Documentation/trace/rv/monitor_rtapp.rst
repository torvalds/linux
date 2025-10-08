Real-time application monitors
==============================

- Name: rtapp
- Type: container for multiple monitors
- Author: Nam Cao <namcao@linutronix.de>

Description
-----------

Real-time applications may have design flaws such that they experience
unexpected latency and fail to meet their time requirements. Often, these flaws
follow a few patterns:

  - Page faults: A real-time thread may access memory that does not have a
    mapped physical backing or must first be copied (such as for copy-on-write).
    Thus a page fault is raised and the kernel must first perform the expensive
    action. This causes significant delays to the real-time thread
  - Priority inversion: A real-time thread blocks waiting for a lower-priority
    thread. This causes the real-time thread to effectively take on the
    scheduling priority of the lower-priority thread. For example, the real-time
    thread needs to access a shared resource that is protected by a
    non-pi-mutex, but the mutex is currently owned by a non-real-time thread.

The `rtapp` monitor detects these patterns. It aids developers to identify
reasons for unexpected latency with real-time applications. It is a container of
multiple sub-monitors described in the following sections.

Monitor pagefault
+++++++++++++++++

The `pagefault` monitor reports real-time tasks raising page faults. Its
specification is::

  RULE = always (RT imply not PAGEFAULT)

To fix warnings reported by this monitor, `mlockall()` or `mlock()` can be used
to ensure physical backing for memory.

This monitor may have false negatives because the pages used by the real-time
threads may just happen to be directly available during testing.  To minimize
this, the system can be put under memory pressure (e.g.  invoking the OOM killer
using a program that does `ptr = malloc(SIZE_OF_RAM); memset(ptr, 0,
SIZE_OF_RAM);`) so that the kernel executes aggressive strategies to recycle as
much physical memory as possible.

Monitor sleep
+++++++++++++

The `sleep` monitor reports real-time threads sleeping in a manner that may
cause undesirable latency. Real-time applications should only put a real-time
thread to sleep for one of the following reasons:

  - Cyclic work: real-time thread sleeps waiting for the next cycle. For this
    case, only the `clock_nanosleep` syscall should be used with `TIMER_ABSTIME`
    (to avoid time drift) and `CLOCK_MONOTONIC` (to avoid the clock being
    changed). No other method is safe for real-time. For example, threads
    waiting for timerfd can be woken by softirq which provides no real-time
    guarantee.
  - Real-time thread waiting for something to happen (e.g. another thread
    releasing shared resources, or a completion signal from another thread). In
    this case, only futexes (FUTEX_LOCK_PI, FUTEX_LOCK_PI2 or one of
    FUTEX_WAIT_*) should be used.  Applications usually do not use futexes
    directly, but use PI mutexes and PI condition variables which are built on
    top of futexes. Be aware that the C library might not implement conditional
    variables as safe for real-time. As an alternative, the librtpi library
    exists to provide a conditional variable implementation that is correct for
    real-time applications in Linux.

Beside the reason for sleeping, the eventual waker should also be
real-time-safe. Namely, one of:

  - An equal-or-higher-priority thread
  - Hard interrupt handler
  - Non-maskable interrupt handler

This monitor's warning usually means one of the following:

  - Real-time thread is blocked by a non-real-time thread (e.g. due to
    contention on a mutex without priority inheritance). This is priority
    inversion.
  - Time-critical work waits for something which is not safe for real-time (e.g.
    timerfd).
  - The work executed by the real-time thread does not need to run at real-time
    priority at all.  This is not a problem for the real-time thread itself, but
    it is potentially taking the CPU away from other important real-time work.

Application developers may purposely choose to have their real-time application
sleep in a way that is not safe for real-time. It is debatable whether that is a
problem. Application developers must analyze the warnings to make a proper
assessment.

The monitor's specification is::

  RULE = always ((RT and SLEEP) imply (RT_FRIENDLY_SLEEP or ALLOWLIST))

  RT_FRIENDLY_SLEEP = (RT_VALID_SLEEP_REASON or KERNEL_THREAD)
                  and ((not WAKE) until RT_FRIENDLY_WAKE)

  RT_VALID_SLEEP_REASON = FUTEX_WAIT
                       or RT_FRIENDLY_NANOSLEEP

  RT_FRIENDLY_NANOSLEEP = CLOCK_NANOSLEEP
                      and NANOSLEEP_TIMER_ABSTIME
                      and NANOSLEEP_CLOCK_MONOTONIC

  RT_FRIENDLY_WAKE = WOKEN_BY_EQUAL_OR_HIGHER_PRIO
                  or WOKEN_BY_HARDIRQ
                  or WOKEN_BY_NMI
                  or KTHREAD_SHOULD_STOP

  ALLOWLIST = BLOCK_ON_RT_MUTEX
           or FUTEX_LOCK_PI
           or TASK_IS_RCU
           or TASK_IS_MIGRATION

Beside the scenarios described above, this specification also handle some
special cases:

  - `KERNEL_THREAD`: kernel tasks do not have any pattern that can be recognized
    as valid real-time sleeping reasons. Therefore sleeping reason is not
    checked for kernel tasks.
  - `KTHREAD_SHOULD_STOP`: a non-real-time thread may stop a real-time kernel
    thread by waking it and waiting for it to exit (`kthread_stop()`). This
    wakeup is safe for real-time.
  - `ALLOWLIST`: to handle known false positives with the kernel.
  - `BLOCK_ON_RT_MUTEX` is included in the allowlist due to its implementation.
    In the release path of rt_mutex, a boosted task is de-boosted before waking
    the rt_mutex's waiter. Consequently, the monitor may see a real-time-unsafe
    wakeup (e.g. non-real-time task waking real-time task). This is actually
    real-time-safe because preemption is disabled for the duration.
  - `FUTEX_LOCK_PI` is included in the allowlist for the same reason as
    `BLOCK_ON_RT_MUTEX`.
