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

prctl
-----

A process can control its own userspace DEXCR value using the
``PR_PPC_GET_DEXCR`` and ``PR_PPC_SET_DEXCR`` pair of
:manpage:`prctl(2)` commands. These calls have the form::

    prctl(PR_PPC_GET_DEXCR, unsigned long which, 0, 0, 0);
    prctl(PR_PPC_SET_DEXCR, unsigned long which, unsigned long ctrl, 0, 0);

The possible 'which' and 'ctrl' values are as follows. Note there is no relation
between the 'which' value and the DEXCR aspect's index.

.. flat-table::
   :header-rows: 1
   :widths: 2 7 1

   * - ``prctl()`` which
     - Aspect name
     - Aspect index

   * - ``PR_PPC_DEXCR_SBHE``
     - Speculative Branch Hint Enable (SBHE)
     - 0

   * - ``PR_PPC_DEXCR_IBRTPD``
     - Indirect Branch Recurrent Target Prediction Disable (IBRTPD)
     - 3

   * - ``PR_PPC_DEXCR_SRAPD``
     - Subroutine Return Address Prediction Disable (SRAPD)
     - 4

   * - ``PR_PPC_DEXCR_NPHIE``
     - Non-Privileged Hash Instruction Enable (NPHIE)
     - 5

.. flat-table::
   :header-rows: 1
   :widths: 2 8

   * - ``prctl()`` ctrl
     - Meaning

   * - ``PR_PPC_DEXCR_CTRL_EDITABLE``
     - This aspect can be configured with PR_PPC_SET_DEXCR (get only)

   * - ``PR_PPC_DEXCR_CTRL_SET``
     - This aspect is set / set this aspect

   * - ``PR_PPC_DEXCR_CTRL_CLEAR``
     - This aspect is clear / clear this aspect

   * - ``PR_PPC_DEXCR_CTRL_SET_ONEXEC``
     - This aspect will be set after exec / set this aspect after exec

   * - ``PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC``
     - This aspect will be clear after exec / clear this aspect after exec

Note that

* which is a plain value, not a bitmask. Aspects must be worked with individually.

* ctrl is a bitmask. ``PR_PPC_GET_DEXCR`` returns both the current and onexec
  configuration. For example, ``PR_PPC_GET_DEXCR`` may return
  ``PR_PPC_DEXCR_CTRL_EDITABLE | PR_PPC_DEXCR_CTRL_SET |
  PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC``. This would indicate the aspect is currently
  set, it will be cleared when you run exec, and you can change this with the
  ``PR_PPC_SET_DEXCR`` prctl.

* The set/clear terminology refers to setting/clearing the bit in the DEXCR.
  For example::

      prctl(PR_PPC_SET_DEXCR, PR_PPC_DEXCR_IBRTPD, PR_PPC_DEXCR_CTRL_SET, 0, 0);

  will set the IBRTPD aspect bit in the DEXCR, causing indirect branch prediction
  to be disabled.

* The status returned by ``PR_PPC_GET_DEXCR`` represents what value the process
  would like applied. It does not include any alternative overrides, such as if
  the hypervisor is enforcing the aspect be set. To see the true DEXCR state
  software should read the appropriate SPRs directly.

* The aspect state when starting a process is copied from the parent's state on
  :manpage:`fork(2)`. The state is reset to a fixed value on
  :manpage:`execve(2)`. The PR_PPC_SET_DEXCR prctl() can control both of these
  values.

* The ``*_ONEXEC`` controls do not change the current process's DEXCR.

Use ``PR_PPC_SET_DEXCR`` with one of ``PR_PPC_DEXCR_CTRL_SET`` or
``PR_PPC_DEXCR_CTRL_CLEAR`` to edit a given aspect.

Common error codes for both getting and setting the DEXCR are as follows:

.. flat-table::
   :header-rows: 1
   :widths: 2 8

   * - Error
     - Meaning

   * - ``EINVAL``
     - The DEXCR is not supported by the kernel.

   * - ``ENODEV``
     - The aspect is not recognised by the kernel or not supported by the
       hardware.

``PR_PPC_SET_DEXCR`` may also report the following error codes:

.. flat-table::
   :header-rows: 1
   :widths: 2 8

   * - Error
     - Meaning

   * - ``EINVAL``
     - The ctrl value contains unrecognised flags.

   * - ``EINVAL``
     - The ctrl value contains mutually conflicting flags (e.g.,
       ``PR_PPC_DEXCR_CTRL_SET | PR_PPC_DEXCR_CTRL_CLEAR``)

   * - ``EPERM``
     - This aspect cannot be modified with prctl() (check for the
       PR_PPC_DEXCR_CTRL_EDITABLE flag with PR_PPC_GET_DEXCR).

   * - ``EPERM``
     - The process does not have sufficient privilege to perform the operation.
       For example, clearing NPHIE on exec is a privileged operation (a process
       can still clear its own NPHIE aspect without privileges).

This interface allows a process to control its own DEXCR aspects, and also set
the initial DEXCR value for any children in its process tree (up to the next
child to use an ``*_ONEXEC`` control). This allows fine-grained control over the
default value of the DEXCR, for example allowing containers to run with different
default values.


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
