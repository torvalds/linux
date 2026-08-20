.. SPDX-License-Identifier: GPL-2.0

==============================
Real-Time Kernel configuration
==============================

.. contents:: Table of Contents
   :depth: 3
   :local:

Introduction
============

This document lists the kernel configuration options that might affect a
real-time kernel's worst-case latency.  It is intended for system integrators.

Configuration options
=====================

.. Please keep the configuration listings alphabetically ordered

CPU frequency governors
-----------------------

``CONFIG_CPU_FREQ``
^^^^^^^^^^^^^^^^^^^

:Expectation: enabled
:Severity: *high*

The CPU frequency scaling subsystem ensures that the processor can operate at
its maximum supported frequency.  While, in general, bootloaders are tasked
with setting the CPU clock to the highest speed on boot, some do not.  It is
thus desirable to keep this option enabled.

.. caution::

  A real-time kernel is not about being "as fast as possible", however
  real-time requirements may demand that the CPU is clocked at a particular
  speed.

``CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:Expectation: enabled
:Severity: *high*

Real-Time workloads expect a fixed CPU frequency during execution.  Using the
performance governor is an easy way to achieve that purely from kernel
configuration.

This is not an absolute rule.  Some setups might prefer to clock the CPU to
lower speeds due to thermal packaging or other requirements.  The key is that
the CPU frequency remains constant once set.

Non-performance CPU frequency governors
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:Expectation: disabled
:Severity: *medium*

To ensure reproducible system latency measurements, disable the
non-``PERFORMANCE`` CPU frequency governors whenever possible.  This avoids
the risk of unknown userspace tasks implicitly or explicitly setting a
different CPU frequency governor, and thereby changing latency behavior while
the system is running.

If disabling other frequency governors is not an option, use a governor that
keeps the CPU frequency fixed.  For example,
``CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE`` can be enabled when userspace is
responsible for setting a *stable* frequency during system initialization.

If a low CPU frequency is desired, then
``CONFIG_CPU_FREQ_DEFAULT_GOV_POWERSAVE`` can be set.

The ``ONDEMAND`` governor should not be enabled on a real-time system.  Its
frequency changes depend on workload behavior and can significantly harm
determinism.

For more information, see Documentation/admin-guide/pm/cpufreq.rst

``CONFIG_CPU_IDLE``
-------------------

:Expectation: enabled
:Severity: *info*

CPU idle states (C-states) allow the processor to enter low-power modes during
periods of inactivity.  Very-low CPU idle states may require flushing the CPU
caches and lowering or disabling the clocking.  This can lower power
consumption, but it also increases the entry and exit latency from such
states.

While disabling this option eliminates cpuidle-related latencies, doing so can
significantly impact hardware longevity, warranty, and thermal behavior.
Users should cap the maximum C-state to C1 instead.  For ACPI platforms, this
can be achieved by using the boot parameter [1]_::

  processor.max_cstate=1

Higher C-states can be acceptable depending on the user workload's latency
requirements.  For ACPI-based platforms, use the ``cpupower idle-info``
command to inspect the available idle states.

For more information, please see:

- ``linux/tools/power/cpupower``
- Documentation/admin-guide/pm/cpuidle.rst
- Documentation/admin-guide/pm/index.rst

``CONFIG_DRM``
--------------

:Expectation: disabled
:Severity: *info*

GPU-accelerated workloads can share system resources with the CPU, including
last-level cache (LLC) and memory bandwidth.  Modern integrated GPUs optimize
graphics performance at the expense of CPU determinism.

Examples of affected platforms:

- Intel processors with integrated graphics (Gen9 and later)
- AMD APUs with Radeon Graphics
- Xilinx Zynq UltraScale+ MPSoC EG/EV series

If graphics workloads must run alongside real-time tasks, users must conduct
thorough stress testing using tools like ``glmark2`` while measuring the
overall system latency.

For more information, please check:

- Documentation/core-api/real-time/hardware.rst ("Regarding hardware" section)
- Documentation/filesystems/resctrl.rst
- `Real-Time and Graphics: A Contradiction? <https://web.archive.org/web/20221025085614/https://linutronix.de/PDF/Realtime_and_graphics-acontradiction2021.pdf>`_

``CONFIG_EFI_DISABLE_RUNTIME``
------------------------------

:Expectation: enabled
:Severity: *medium*

EFI is the standard boot and firmware interface for multiple architectures.
EFI runtime services provide callback functions to be called from the kernel;
e.g., as utilized by (``CONFIG_EFI_VARS*``) or (``CONFIG_RTC_DRV_EFI``).  For
the former, the kernel calls into EFI to update the EFI variables.

Calling into EFI means invoking firmware callbacks.  During such invocations,
the system might not be able to react to interrupts and will thus not be able
to perform a context switch.  This can cause significant latency spikes for
the real-time system.

``CONFIG_PREEMPT_RT`` enables this option by default.  If this option is
manually disabled at build time, the following boot parameter [1]_ may be used
to disable EFI runtime at boot up::

  efi=noruntime

Alternatively, confine EFI runtime service calls to a housekeeping CPU by
restricting the ``efi_runtime`` workqueue CPU affinity.  For example, set that
workqueue's affinity to CPU #0 and pin your RT tasks to a different CPU range.
See Documentation/core-api/workqueue.rst

``CONFIG_NO_HZ`` / ``CONFIG_NO_HZ_FULL``
----------------------------------------

:Expectation: disabled
:Severity: *medium*

Tickless operation can increase kernel-to-userspace transition latency due to
the extra accounting and state book-keeping.

*Guidance by real-time workload type:*

- For periodic workloads; e.g., control loops executing every 100 µs, avoid
  ``NO_HZ`` modes.  Consistent kernel ticks are preferable.

- For computation-intensive workloads; e.g. extended userspace execution,
  ``NO_HZ_FULL`` may be beneficial.  In such cases, users should offload the
  kernel housekeeping to dedicated CPUs and isolate compute cores.

See also Documentation/timers/no_hz.rst

``CONFIG_PREEMPT_RT``
---------------------

:Expectation: enabled
:Severity: **fatal**

This option must be enabled, or the resulting kernel will not be fully
preemptible and real-time capable.

``CONFIG_TRACING`` (and tracing options)
----------------------------------------

:Expectation: enabled
:Severity: *info*

Shipping kernels with tracing support enabled (but not actively running) is
highly recommended.  This will allow the users to extract more information if
latency problems arise.  Nonetheless, some tracers do incur latency overhead
just by being enabled.

.. caution::

  Users should *not* make use of tracers or trace events during production
  real-time kernel operation as they can add considerable overhead and degrade
  the system's latency.

``CONFIG_IRQSOFF_TRACER`` and ``CONFIG_PREEMPT_TRACER``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:Expectation: disabled
:Severity: *high*

These tracers do incur measurable latency overhead even when tracing is not
currently active.

Kernel Debug Options
====================

Most kernel debug options add runtime overhead that increases the worst-case
latency.

.. caution::

  During development and early testing, users are encouraged to run their
  real-time workloads and peripherals with lockdep (:ref:`lockdep`) and other
  kernel debug options enabled, for a considerable amount of time.  Such
  workloads might trigger kernel code paths that were not triggered during the
  internal Linux real-time kernel development, thus helping to uncover locking
  and other types of kernel bugs.

``CONFIG_DEBUG_ATOMIC_SLEEP``
-----------------------------

:Expectation: allowed

This sanity check catches common kernel programming errors with a tolerable
latency cost.  It also increases overall scheduling as each ``might_sleep()``
can lead to a context switch.

``CONFIG_DEBUG_BUGVERBOSE`` and ``CONFIG_DEBUG_INFO*``
------------------------------------------------------

:Expectation: allowed

These options increase the kernel image size but have no latency impact.  They
are also essential for meaningful BUG logs, crash dumps, and profiling.

``CONFIG_DEBUG_FS``
-------------------

:Expectation: allowed

This is safe to include in real-time kernels, *provided that debugfs is not
accessed during production runtime*.

``CONFIG_DEBUG_KERNEL``
-----------------------

:Expectation: allowed

Meta-option which allows debug features to be enabled.  It has no runtime
impact, but beware of any debug features that it may have implicitly enabled.

``CONFIG_LOCKUP_DETECTOR``
--------------------------

:Expectation: disabled
:Severity: *high*

The lockup detector creates kernel timer callbacks that execute every few
seconds, in hard-IRQ context, even on real-time kernels.  These periodic
interrupts can cause latency spikes.

Users should use hardware watchdogs instead, which will provide a similar
functionality without the software-induced latency.

.. _lockdep:

``CONFIG_PROVE_LOCKING``
------------------------

:Expectation: disabled
:Severity: *high*

Proving the correctness of all kernel locking adds substantial overhead and
significantly increases worst-case latency.

Summary
=======

There is no "one size fits all" solution for configuring a real-time Linux
system.  Beginning with the system real-time requirements, integrators must
consider the features and functions of the system's hardware, kernel, and
userspace.  All such components must be properly configured in order to
establish and constrain the system's maximum latency.

With that in mind, any incorrect real-time kernel configuration could cause a
new maximum latency that shows up at the wrong time and is catastrophic for
the real-time system's latency.

References
==========

.. [1] See Documentation/admin-guide/kernel-parameters.rst
