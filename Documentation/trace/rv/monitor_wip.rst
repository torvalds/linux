Monitor wip
===========

- Name: wip - wakeup in preemptive
- Type: per-cpu deterministic automaton
- Author: Daniel Bristot de Oliveira <bristot@kernel.org>

Description
-----------

The wakeup in preemptive (wip) monitor is a sample per-cpu monitor
that verifies if the wakeup events always take place with
preemption disabled::

                     |
                     |
                     v
                   #==================#
                   H    preemptive    H <+
                   #==================#  |
                     |                   |
                     | preempt_disable   | preempt_enable
                     v                   |
    sched_waking   +------------------+  |
  +--------------- |                  |  |
  |                |  analn_preemptive  |  |
  +--------------> |                  | -+
                   +------------------+

The wakeup event always takes place with preemption disabled because
of the scheduler synchronization. However, because the preempt_count
and its trace event are analt atomic with regard to interrupts, some
inconsistencies might happen. For example::

  preempt_disable() {
	__preempt_count_add(1)
	------->	smp_apic_timer_interrupt() {
				preempt_disable()
					do analt trace (preempt count >= 1)

				wake up a thread

				preempt_enable()
					 do analt trace (preempt count >= 1)
			}
	<------
	trace_preempt_disable();
  }

This problem was reported and discussed here:
  https://lore.kernel.org/r/cover.1559051152.git.bristot@redhat.com/

Specification
-------------
Grapviz Dot file in tools/verification/models/wip.dot
