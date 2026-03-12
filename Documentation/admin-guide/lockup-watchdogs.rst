===============================================================
Softlockup detector and hardlockup detector (aka nmi_watchdog)
===============================================================

The Linux kernel can act as a watchdog to detect both soft and hard
lockups.

A 'softlockup' is defined as a bug that causes the kernel to loop in
kernel mode for more than 20 seconds (see "Implementation" below for
details), without giving other tasks a chance to run. The current
stack trace is displayed upon detection and, by default, the system
will stay locked up. Alternatively, the kernel can be configured to
panic; a sysctl, "kernel.softlockup_panic", a kernel parameter,
"softlockup_panic" (see "Documentation/admin-guide/kernel-parameters.rst" for
details), and a compile option, "BOOTPARAM_SOFTLOCKUP_PANIC", are
provided for this.

A 'hardlockup' is defined as a bug that causes the CPU to loop in
kernel mode for several seconds (see "Implementation" below for
details), without letting other interrupts have a chance to run.
Similarly to the softlockup case, the current stack trace is displayed
upon detection and the system will stay locked up unless the default
behavior is changed, which can be done through a sysctl,
'hardlockup_panic', a compile time knob, "BOOTPARAM_HARDLOCKUP_PANIC",
and a kernel parameter, "nmi_watchdog"
(see "Documentation/admin-guide/kernel-parameters.rst" for details).

The panic option can be used in combination with panic_timeout (this
timeout is set through the confusingly named "kernel.panic" sysctl),
to cause the system to reboot automatically after a specified amount
of time.

Configuration
=============

A kernel knob is provided that allows administrators to configure
this period. The "watchdog_thresh" parameter (default 10 seconds)
controls the threshold. The right value for a particular environment
is a trade-off between fast response to lockups and detection overhead.

Implementation
==============

The soft lockup detector is built on top of the hrtimer subsystem.
The hard lockup detector is built on top of the perf subsystem
(on architectures that support it) or uses an SMP "buddy" system.

Softlockup Detector
-------------------

The watchdog job runs in a stop scheduling thread that updates a
timestamp every time it is scheduled. If that timestamp is not updated
for 2*watchdog_thresh seconds (the softlockup threshold) the
'softlockup detector' (coded inside the hrtimer callback function)
will dump useful debug information to the system log, after which it
will call panic if it was instructed to do so or resume execution of
other kernel code.

Frequency and Heartbeats
------------------------

The hrtimer used by the softlockup detector serves a dual purpose:
it detects softlockups, and it also generates the interrupts
(heartbeats) that the hardlockup detectors use to verify CPU liveness.

The period of this hrtimer is 2*watchdog_thresh/5. This means the
hrtimer has two or three chances to generate an interrupt before the
NMI hardlockup detector kicks in.

Hardlockup Detector (NMI/Perf)
------------------------------

On architectures that support NMI (Non-Maskable Interrupt) perf events,
a periodic NMI is generated every "watchdog_thresh" seconds.

If any CPU in the system does not receive any hrtimer interrupt
(heartbeat) during the "watchdog_thresh" window, the 'hardlockup
detector' (the handler for the NMI perf event) will generate a kernel
warning or call panic.

**Detection Overhead (NMI):**

The time to detect a lockup can vary depending on when the lockup
occurs relative to the NMI check window. Examples below assume a watchdog_thresh of 10.

* **Best Case:** The lockup occurs just before the first heartbeat is
  due. The detector will notice the missing hrtimer interrupt almost
  immediately during the next check.

  ::

    Time 100.0: cpu 1 heartbeat
    Time 100.1: hardlockup_check, cpu1 stores its state
    Time 103.9: Hard Lockup on cpu1
    Time 104.0: cpu 1 heartbeat never comes
    Time 110.1: hardlockup_check, cpu1 checks the state again, should be the same, declares lockup

    Time to detection: ~6 seconds

* **Worst Case:** The lockup occurs shortly after a valid interrupt
  (heartbeat) which itself happened just after the NMI check. The next
  NMI check sees that the interrupt count has changed (due to that one
  heartbeat), assumes the CPU is healthy, and resets the baseline. The
  lockup is only detected at the subsequent check.

  ::

    Time 100.0: hardlockup_check, cpu1 stores its state
    Time 100.1: cpu 1 heartbeat
    Time 100.2: Hard Lockup on cpu1
    Time 110.0: hardlockup_check, cpu1 stores its state (misses lockup as state changed)
    Time 120.0: hardlockup_check, cpu1 checks the state again, should be the same, declares lockup

    Time to detection: ~20 seconds

Hardlockup Detector (Buddy)
---------------------------

On architectures or configurations where NMI perf events are not
available (or disabled), the kernel may use the "buddy" hardlockup
detector. This mechanism requires SMP (Symmetric Multi-Processing).

In this mode, each CPU is assigned a "buddy" CPU to monitor. The
monitoring CPU runs its own hrtimer (the same one used for softlockup
detection) and checks if the buddy CPU's hrtimer interrupt count has
increased.

To ensure timeliness and avoid false positives, the buddy system performs
checks at every hrtimer interval (2*watchdog_thresh/5, which is 4 seconds
by default). It uses a missed-interrupt threshold of 3. If the buddy's
interrupt count has not changed for 3 consecutive checks, it is assumed
that the buddy CPU is hardlocked (interrupts disabled). The monitoring
CPU will then trigger the hardlockup response (warning or panic).

**Detection Overhead (Buddy):**

With a default check interval of 4 seconds (watchdog_thresh = 10):

* **Best case:** Lockup occurs just before a check.
    Detected in ~8s (0s till 1st check + 4s till 2nd + 4s till 3rd).
* **Worst case:** Lockup occurs just after a check.
    Detected in ~12s (4s till 1st check + 4s till 2nd + 4s till 3rd).

**Limitations of the Buddy Detector:**

1.  **All-CPU Lockup:** If all CPUs lock up simultaneously, the buddy
    detector cannot detect the condition because the monitoring CPUs
    are also frozen.
2.  **Stack Traces:** Unlike the NMI detector, the buddy detector
    cannot directly interrupt the locked CPU to grab a stack trace.
    It relies on architecture-specific mechanisms (like NMI backtrace
    support) to try and retrieve the status of the locked CPU. If
    such support is missing, the log may only show that a lockup
    occurred without providing the locked CPU's stack.

Watchdog Core Exclusion
=======================

By default, the watchdog runs on all online cores.  However, on a
kernel configured with NO_HZ_FULL, by default the watchdog runs only
on the housekeeping cores, not the cores specified in the "nohz_full"
boot argument.  If we allowed the watchdog to run by default on
the "nohz_full" cores, we would have to run timer ticks to activate
the scheduler, which would prevent the "nohz_full" functionality
from protecting the user code on those cores from the kernel.
Of course, disabling it by default on the nohz_full cores means that
when those cores do enter the kernel, by default we will not be
able to detect if they lock up.  However, allowing the watchdog
to continue to run on the housekeeping (non-tickless) cores means
that we will continue to detect lockups properly on those cores.

In either case, the set of cores excluded from running the watchdog
may be adjusted via the kernel.watchdog_cpumask sysctl.  For
nohz_full cores, this may be useful for debugging a case where the
kernel seems to be hanging on the nohz_full cores.
