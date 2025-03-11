.. SPDX-License-Identifier: GPL-2.0

:orphan:

==============
MSM Preemption
==============

Preemption allows Adreno GPUs to switch to a higher priority ring when work is
pushed to it, reducing latency for high priority submissions.

When preemption is enabled 4 rings are initialized, corresponding to different
priority levels. Having multiple rings is purely a software concept as the GPU
only has registers to keep track of one graphics ring.
The kernel is able to switch which ring is currently being processed by
requesting preemption. When certain conditions are met, depending on the
priority level, the GPU will save its current state in a series of buffers,
then restores state from a similar set of buffers specified by the kernel. It
then resumes execution and fires an IRQ to let the kernel know the context
switch has completed.

This mechanism can be used by the kernel to switch between rings. Whenever a
submission occurs the kernel finds the highest priority ring which isn't empty
and preempts to it if said ring is not the one being currently executed. This is
also done whenever a submission completes to make sure execution resumes on a
lower priority ring when a higher priority ring is done.

Preemption levels
-----------------

Preemption can only occur at certain boundaries. The exact conditions can be
configured by changing the preemption level, this allows to compromise between
latency (ie. the time that passes between when the kernel requests preemption
and when the SQE begins saving state) and overhead (the amount of state that
needs to be saved).

The GPU offers 3 levels:

Level 0
  Preemption only occurs at the submission level. This requires the least amount
  of state to be saved as the execution of userspace submitted IBs is never
  interrupted, however it offers very little benefit compared to not enabling
  preemption of any kind.

Level 1
  Preemption occurs at either bin level, if using GMEM rendering, or draw level
  in the sysmem rendering case.

Level 2
  Preemption occurs at draw level.

Level 1 is the mode that is used by the msm driver.

Additionally the GPU allows to specify a `skip_save_restore` option. This
disables the saving and restoring of all registers except those relating to the
operation of the SQE itself, reducing overhead. Saving and restoring is only
skipped when using GMEM with Level 1 preemption. When enabling this userspace is
expected to set the state that isn't preserved whenever preemption occurs which
is done by specifying preamble and postambles. Those are IBs that are executed
before and after preemption.

Preemption buffers
------------------

A series of buffers are necessary to store the state of rings while they are not
being executed. There are different kinds of preemption records and most of
those require one buffer per ring. This is because preemption never occurs
between submissions on the same ring, which always run in sequence when the ring
is active. This means that only one context per ring is effectively active.

SMMU_INFO
  This buffer contains info about the current SMMU configuration such as the
  ttbr0 register. The SQE firmware isn't actually able to save this record.
  As a result SMMU info must be saved manually from the CP to a buffer and the
  SMMU record updated with info from said buffer before triggering
  preemption.

NON_SECURE
  This is the main preemption record where most state is saved. It is mostly
  opaque to the kernel except for the first few words that must be initialized
  by the kernel.

SECURE
  This saves state related to the GPU's secure mode.

NON_PRIV
  The intended purpose of this record is unknown. The SQE firmware actually
  ignores it and therefore msm doesn't handle it.

COUNTER
  This record is used to save and restore performance counters.

Handling the permissions of those buffers is critical for security. All but the
NON_PRIV records need to be inaccessible from userspace, so they must be mapped
in the kernel address space with the MSM_BO_MAP_PRIV flag.
For example, making the NON_SECURE record accessible from userspace would allow
any process to manipulate a saved ring's RPTR which can be used to skip the
execution of some packets in a ring and execute user commands with higher
privileges.
