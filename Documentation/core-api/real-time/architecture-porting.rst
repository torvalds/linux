.. SPDX-License-Identifier: GPL-2.0

=============================================
Porting an architecture to support PREEMPT_RT
=============================================

:Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>

This list outlines the architecture specific requirements that must be
implemented in order to enable PREEMPT_RT. Once all required features are
implemented, ARCH_SUPPORTS_RT can be selected in architecture’s Kconfig to make
PREEMPT_RT selectable.
Many prerequisites (genirq support for example) are enforced by the common code
and are omitted here.

The optional features are not strictly required but it is worth to consider
them.

Requirements
------------

Forced threaded interrupts
  CONFIG_IRQ_FORCED_THREADING must be selected. Any interrupts that must
  remain in hard-IRQ context must be marked with IRQF_NO_THREAD. This
  requirement applies for instance to clocksource event interrupts,
  perf interrupts and cascading interrupt-controller handlers.

PREEMPTION support
  Kernel preemption must be supported and requires that
  CONFIG_ARCH_NO_PREEMPT remain unselected. Scheduling requests, such as those
  issued from an interrupt or other exception handler, must be processed
  immediately.

POSIX CPU timers and KVM
  POSIX CPU timers must expire from thread context rather than directly within
  the timer interrupt. This behavior is enabled by setting the configuration
  option CONFIG_HAVE_POSIX_CPU_TIMERS_TASK_WORK.
  When KVM is enabled, CONFIG_KVM_XFER_TO_GUEST_WORK must also be set to ensure
  that any pending work, such as POSIX timer expiration, is handled before
  transitioning into guest mode.

Hard-IRQ and Soft-IRQ stacks
  Soft interrupts are handled in the thread context in which they are raised. If
  a soft interrupt is triggered from hard-IRQ context, its execution is deferred
  to the ksoftirqd thread. Preemption is never disabled during soft interrupt
  handling, which makes soft interrupts preemptible.
  If an architecture provides a custom __do_softirq() implementation that uses a
  separate stack, it must select CONFIG_HAVE_SOFTIRQ_ON_OWN_STACK. The
  functionality should only be enabled when CONFIG_SOFTIRQ_ON_OWN_STACK is set.

FPU and SIMD access in kernel mode
  FPU and SIMD registers are typically not used in kernel mode and are therefore
  not saved during kernel preemption. As a result, any kernel code that uses
  these registers must be enclosed within a kernel_fpu_begin() and
  kernel_fpu_end() section.
  The kernel_fpu_begin() function usually invokes local_bh_disable() to prevent
  interruptions from softirqs and to disable regular preemption. This allows the
  protected code to run safely in both thread and softirq contexts.
  On PREEMPT_RT kernels, however, kernel_fpu_begin() must not call
  local_bh_disable(). Instead, it should use preempt_disable(), since softirqs
  are always handled in thread context under PREEMPT_RT. In this case, disabling
  preemption alone is sufficient.
  The crypto subsystem operates on memory pages and requires users to "walk and
  map" these pages while processing a request. This operation must occur outside
  the kernel_fpu_begin()/ kernel_fpu_end() section because it requires preemption
  to be enabled. These preemption points are generally sufficient to avoid
  excessive scheduling latency.

Exception handlers
  Exception handlers, such as the page fault handler, typically enable interrupts
  early, before invoking any generic code to process the exception. This is
  necessary because handling a page fault may involve operations that can sleep.
  Enabling interrupts is especially important on PREEMPT_RT, where certain
  locks, such as spinlock_t, become sleepable. For example, handling an
  invalid opcode may result in sending a SIGILL signal to the user task. A
  debug excpetion will send a SIGTRAP signal.
  In both cases, if the exception occurred in user space, it is safe to enable
  interrupts early. Sending a signal requires both interrupts and kernel
  preemption to be enabled.

Optional features
-----------------

Timer and clocksource
  A high-resolution clocksource and clockevents device are recommended. The
  clockevents device should support the CLOCK_EVT_FEAT_ONESHOT feature for
  optimal timer behavior. In most cases, microsecond-level accuracy is
  sufficient

Lazy preemption
  This mechanism allows an in-kernel scheduling request for non-real-time tasks
  to be delayed until the task is about to return to user space. It helps avoid
  preempting a task that holds a sleeping lock at the time of the scheduling
  request.
  With CONFIG_GENERIC_IRQ_ENTRY enabled, supporting this feature requires
  defining a bit for TIF_NEED_RESCHED_LAZY, preferably near TIF_NEED_RESCHED.

Serial console with NBCON
  With PREEMPT_RT enabled, all console output is handled by a dedicated thread
  rather than directly from the context in which printk() is invoked. This design
  allows printk() to be safely used in atomic contexts.
  However, this also means that if the kernel crashes and cannot switch to the
  printing thread, no output will be visible preventing the system from printing
  its final messages.
  There are exceptions for immediate output, such as during panic() handling. To
  support this, the console driver must implement new-style lock handling. This
  involves setting the CON_NBCON flag in console::flags and providing
  implementations for the write_atomic, write_thread, device_lock, and
  device_unlock callbacks.
