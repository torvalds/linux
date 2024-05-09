.. SPDX-License-Identifier: GPL-2.0-or-later

==========================================
DEXCR (Dynamic Execution Control Register)
==========================================

Overview
========

The DEXCR is a privileged special purpose register (SPR) introduced in
PowerPC ISA 3.1B (Power10) that allows per-cpu control over several dynamic
execution behaviours. These behaviours include speculation (e.g., indirect
branch target prediction) and enabling return-oriented programming (ROP)
protection instructions.

The execution control is exposed in hardware as up to 32 bits ('aspects') in
the DEXCR. Each aspect controls a certain behaviour, and can be set or cleared
to enable/disable the aspect. There are several variants of the DEXCR for
different purposes:

DEXCR
    A privileged SPR that can control aspects for userspace and kernel space
HDEXCR
    A hypervisor-privileged SPR that can control aspects for the hypervisor and
    enforce aspects for the kernel and userspace.
UDEXCR
    An optional ultravisor-privileged SPR that can control aspects for the ultravisor.

Userspace can examine the current DEXCR state using a dedicated SPR that
provides a non-privileged read-only view of the userspace DEXCR aspects.
There is also an SPR that provides a read-only view of the hypervisor enforced
aspects, which ORed with the userspace DEXCR view gives the effective DEXCR
state for a process.


Configuration
=============

The DEXCR is currently unconfigurable. All threads are run with the
NPHIE aspect enabled.


coredump and ptrace
===================

The userspace values of the DEXCR and HDEXCR (in this order) are exposed under
``NT_PPC_DEXCR``. These are each 64 bits and readonly, and are intended to
assist with core dumps. The DEXCR may be made writable in future. The top 32
bits of both registers (corresponding to the non-userspace bits) are masked off.

If the kernel config ``CONFIG_CHECKPOINT_RESTORE`` is enabled, then
``NT_PPC_HASHKEYR`` is available and exposes the HASHKEYR value of the process
for reading and writing. This is a tradeoff between increased security and
checkpoint/restore support: a process should normally have no need to know its
secret key, but restoring a process requires setting its original key. The key
therefore appears in core dumps, and an attacker may be able to retrieve it from
a coredump and effectively bypass ROP protection on any threads that share this
key (potentially all threads from the same parent that have not run ``exec()``).
