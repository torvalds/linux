.. SPDX-License-Identifier: GPL-2.0

=========================================
Flexible Return and Event Delivery (FRED)
=========================================

Overview
========

The FRED architecture defines simple new transitions that change
privilege level (ring transitions). The FRED architecture was
designed with the following goals:

1) Improve overall performance and response time by replacing event
   delivery through the interrupt descriptor table (IDT event
   delivery) and event return by the IRET instruction with lower
   latency transitions.

2) Improve software robustness by ensuring that event delivery
   establishes the full supervisor context and that event return
   establishes the full user context.

The new transitions defined by the FRED architecture are FRED event
delivery and, for returning from events, two FRED return instructions.
FRED event delivery can effect a transition from ring 3 to ring 0, but
it is used also to deliver events incident to ring 0. One FRED
instruction (ERETU) effects a return from ring 0 to ring 3, while the
other (ERETS) returns while remaining in ring 0. Collectively, FRED
event delivery and the FRED return instructions are FRED transitions.

In addition to these transitions, the FRED architecture defines a new
instruction (LKGS) for managing the state of the GS segment register.
The LKGS instruction can be used by 64-bit operating systems that do
not use the new FRED transitions.

Furthermore, the FRED architecture is easy to extend for future CPU
architectures.

Software based event dispatching
================================

FRED operates differently from IDT in terms of event handling. Instead
of directly dispatching an event to its handler based on the event
vector, FRED requires the software to dispatch an event to its handler
based on both the event's type and vector. Therefore, an event dispatch
framework must be implemented to facilitate the event-to-handler
dispatch process. The FRED event dispatch framework takes control
once an event is delivered, and employs a two-level dispatch.

The first level dispatching is event type based, and the second level
dispatching is event vector based.

Full supervisor/user context
============================

FRED event delivery atomically save and restore full supervisor/user
context upon event delivery and return. Thus it avoids the problem of
transient states due to %cr2 and/or %dr6, and it is no longer needed
to handle all the ugly corner cases caused by half baked entry states.

FRED allows explicit unblock of NMI with new event return instructions
ERETS/ERETU, avoiding the mess caused by IRET which unconditionally
unblocks NMI, e.g., when an exception happens during NMI handling.

FRED always restores the full value of %rsp, thus ESPFIX is no longer
needed when FRED is enabled.

LKGS
====

LKGS behaves like the MOV to GS instruction except that it loads the
base address into the IA32_KERNEL_GS_BASE MSR instead of the GS
segment’s descriptor cache. With LKGS, it ends up with avoiding
mucking with kernel GS, i.e., an operating system can always operate
with its own GS base address.

Because FRED event delivery from ring 3 and ERETU both swap the value
of the GS base address and that of the IA32_KERNEL_GS_BASE MSR, plus
the introduction of LKGS instruction, the SWAPGS instruction is no
longer needed when FRED is enabled, thus is disallowed (#UD).

Stack levels
============

4 stack levels 0~3 are introduced to replace the nonreentrant IST for
event handling, and each stack level should be configured to use a
dedicated stack.

The current stack level could be unchanged or go higher upon FRED
event delivery. If unchanged, the CPU keeps using the current event
stack. If higher, the CPU switches to a new event stack specified by
the MSR of the new stack level, i.e., MSR_IA32_FRED_RSP[123].

Only execution of a FRED return instruction ERET[US], could lower the
current stack level, causing the CPU to switch back to the stack it was
on before a previous event delivery that promoted the stack level.
