.. SPDX-License-Identifier: GPL-2.0

===================================
Memory copy/set instructions (MOPS)
===================================

A MOPS memory copy/set operation consists of three consecutive CPY* or SET*
instructions: a prologue, main and epilogue (for example: CPYP, CPYM, CPYE).

A main or epilogue instruction can take a MOPS exception for various reasons,
for example when a task is migrated to a CPU with a different MOPS
implementation, or when the instruction's alignment and size requirements are
not met. The software exception handler is then expected to reset the registers
and restart execution from the prologue instruction. Normally this is handled
by the kernel.

For more details refer to "D1.3.5.7 Memory Copy and Memory Set exceptions" in
the Arm Architecture Reference Manual DDI 0487K.a (Arm ARM).

.. _arm64_mops_hyp:

Hypervisor requirements
-----------------------

A hypervisor running a Linux guest must handle all MOPS exceptions from the
guest kernel, as Linux may not be able to handle the exception at all times.
For example, a MOPS exception can be taken when the hypervisor migrates a vCPU
to another physical CPU with a different MOPS implementation.

To do this, the hypervisor must:

  - Set HCRX_EL2.MCE2 to 1 so that the exception is taken to the hypervisor.

  - Have an exception handler that implements the algorithm from the Arm ARM
    rules CNTMJ and MWFQH.

  - Set the guest's PSTATE.SS to 0 in the exception handler, to handle a
    potential step of the current instruction.

    Note: Clearing PSTATE.SS is needed so that a single step exception is taken
    on the next instruction (the prologue instruction). Otherwise prologue
    would get silently stepped over and the single step exception taken on the
    main instruction. Note that if the guest instruction is not being stepped
    then clearing PSTATE.SS has no effect.
