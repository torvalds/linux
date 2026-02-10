=====================
Restartable Sequences
=====================

Restartable Sequences allow to register a per thread userspace memory area
to be used as an ABI between kernel and userspace for three purposes:

 * userspace restartable sequences

 * quick access to read the current CPU number, node ID from userspace

 * scheduler time slice extensions

Restartable sequences (per-cpu atomics)
---------------------------------------

Restartable sequences allow userspace to perform update operations on
per-cpu data without requiring heavyweight atomic operations. The actual
ABI is unfortunately only available in the code and selftests.

Quick access to CPU number, node ID
-----------------------------------

Allows to implement per CPU data efficiently. Documentation is in code and
selftests. :(

Scheduler time slice extensions
-------------------------------

This allows a thread to request a time slice extension when it enters a
critical section to avoid contention on a resource when the thread is
scheduled out inside of the critical section.

The prerequisites for this functionality are:

    * Enabled in Kconfig

    * Enabled at boot time (default is enabled)

    * A rseq userspace pointer has been registered for the thread

The thread has to enable the functionality via prctl(2)::

    prctl(PR_RSEQ_SLICE_EXTENSION, PR_RSEQ_SLICE_EXTENSION_SET,
          PR_RSEQ_SLICE_EXT_ENABLE, 0, 0);

prctl() returns 0 on success or otherwise with the following error codes:

========= ==============================================================
Errorcode Meaning
========= ==============================================================
EINVAL	  Functionality not available or invalid function arguments.
          Note: arg4 and arg5 must be zero
ENOTSUPP  Functionality was disabled on the kernel command line
ENXIO	  Available, but no rseq user struct registered
========= ==============================================================

The state can be also queried via prctl(2)::

  prctl(PR_RSEQ_SLICE_EXTENSION, PR_RSEQ_SLICE_EXTENSION_GET, 0, 0, 0);

prctl() returns ``PR_RSEQ_SLICE_EXT_ENABLE`` when it is enabled or 0 if
disabled. Otherwise it returns with the following error codes:

========= ==============================================================
Errorcode Meaning
========= ==============================================================
EINVAL	  Functionality not available or invalid function arguments.
          Note: arg3 and arg4 and arg5 must be zero
========= ==============================================================

The availability and status is also exposed via the rseq ABI struct flags
field via the ``RSEQ_CS_FLAG_SLICE_EXT_AVAILABLE_BIT`` and the
``RSEQ_CS_FLAG_SLICE_EXT_ENABLED_BIT``. These bits are read-only for user
space and only for informational purposes.

If the mechanism was enabled via prctl(), the thread can request a time
slice extension by setting rseq::slice_ctrl::request to 1. If the thread is
interrupted and the interrupt results in a reschedule request in the
kernel, then the kernel can grant a time slice extension and return to
userspace instead of scheduling out. The length of the extension is
determined by debugfs:rseq/slice_ext_nsec. The default value is 5 usec; which
is the minimum value. It can be incremented to 50 usecs, however doing so
can/will affect the minimum scheduling latency.

Any proposed changes to this default will have to come with a selftest and
rseq-slice-hist.py output that shows the new value has merrit.

The kernel indicates the grant by clearing rseq::slice_ctrl::request and
setting rseq::slice_ctrl::granted to 1. If there is a reschedule of the
thread after granting the extension, the kernel clears the granted bit to
indicate that to userspace.

If the request bit is still set when the leaving the critical section,
userspace can clear it and continue.

If the granted bit is set, then userspace invokes rseq_slice_yield(2) when
leaving the critical section to relinquish the CPU. The kernel enforces
this by arming a timer to prevent misbehaving userspace from abusing this
mechanism.

If both the request bit and the granted bit are false when leaving the
critical section, then this indicates that a grant was revoked and no
further action is required by userspace.

The required code flow is as follows::

    rseq->slice_ctrl.request = 1;
    barrier();  // Prevent compiler reordering
    critical_section();
    barrier();  // Prevent compiler reordering
    rseq->slice_ctrl.request = 0;
    if (rseq->slice_ctrl.granted)
        rseq_slice_yield();

As all of this is strictly CPU local, there are no atomicity requirements.
Checking the granted state is racy, but that cannot be avoided at all::

    if (rseq->slice_ctrl.granted)
      -> Interrupt results in schedule and grant revocation
        rseq_slice_yield();

So there is no point in pretending that this might be solved by an atomic
operation.

If the thread issues a syscall other than rseq_slice_yield(2) within the
granted timeslice extension, the grant is also revoked and the CPU is
relinquished immediately when entering the kernel. This is required as
syscalls might consume arbitrary CPU time until they reach a scheduling
point when the preemption model is either NONE or VOLUNTARY and therefore
might exceed the grant by far.

The preferred solution for user space is to use rseq_slice_yield(2) which
is side effect free. The support for arbitrary syscalls is required to
support onion layer architectured applications, where the code handling the
critical section and requesting the time slice extension has no control
over the code within the critical section.

The kernel enforces flag consistency and terminates the thread with SIGSEGV
if it detects a violation.
