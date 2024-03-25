.. SPDX-License-Identifier: GPL-2.0

========================
membarrier() System Call
========================

MEMBARRIER_CMD_{PRIVATE,GLOBAL}_EXPEDITED - Architecture requirements
=====================================================================

Memory barriers before updating rq->curr
----------------------------------------

The commands MEMBARRIER_CMD_PRIVATE_EXPEDITED and MEMBARRIER_CMD_GLOBAL_EXPEDITED
require each architecture to have a full memory barrier after coming from
user-space, before updating rq->curr.  This barrier is implied by the sequence
rq_lock(); smp_mb__after_spinlock() in __schedule().  The barrier matches a full
barrier in the proximity of the membarrier system call exit, cf.
membarrier_{private,global}_expedited().

Memory barriers after updating rq->curr
---------------------------------------

The commands MEMBARRIER_CMD_PRIVATE_EXPEDITED and MEMBARRIER_CMD_GLOBAL_EXPEDITED
require each architecture to have a full memory barrier after updating rq->curr,
before returning to user-space.  The schemes providing this barrier on the various
architectures are as follows.

 - alpha, arc, arm, hexagon, mips rely on the full barrier implied by
   spin_unlock() in finish_lock_switch().

 - arm64 relies on the full barrier implied by switch_to().

 - powerpc, riscv, s390, sparc, x86 rely on the full barrier implied by
   switch_mm(), if mm is not NULL; they rely on the full barrier implied
   by mmdrop(), otherwise.  On powerpc and riscv, switch_mm() relies on
   membarrier_arch_switch_mm().

The barrier matches a full barrier in the proximity of the membarrier system call
entry, cf. membarrier_{private,global}_expedited().
