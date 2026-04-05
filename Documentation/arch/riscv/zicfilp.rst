.. SPDX-License-Identifier: GPL-2.0

:Author: Deepak Gupta <debug@rivosinc.com>
:Date:   12 January 2024

====================================================
Tracking indirect control transfers on RISC-V Linux
====================================================

This document briefly describes the interface provided to userspace by Linux
to enable indirect branch tracking for user mode applications on RISC-V.

1. Feature Overview
--------------------

Memory corruption issues usually result in crashes.  However, in the
hands of a creative adversary, these can result in a variety of
security issues.

Some of those security issues can be code re-use attacks, where an
adversary can use corrupt function pointers, chaining them together to
perform jump oriented programming (JOP) or call oriented programming
(COP) and thus compromise control flow integrity (CFI) of the program.

Function pointers live in read-write memory and thus are susceptible
to corruption.  This can allow an adversary to control the program
counter (PC) value.  On RISC-V, the zicfilp extension enforces a
restriction on such indirect control transfers:

- Indirect control transfers must land on a landing pad instruction ``lpad``.
  There are two exceptions to this rule:

  - rs1 = x1 or rs1 = x5, i.e. a return from a function and returns are
    protected using shadow stack (see zicfiss.rst)

  - rs1 = x7. On RISC-V, the compiler usually does the following to reach a
    function which is beyond the offset of possible J-type instruction::

      auipc x7, <imm>
      jalr (x7)

    This form of indirect control transfer is immutable and doesn't
    rely on memory.  Thus rs1=x7 is exempted from tracking and
    these are considered software guarded jumps.

The ``lpad`` instruction is a pseudo-op of ``auipc rd, <imm_20bit>``
with ``rd=x0``.  This is a HINT op.  The ``lpad`` instruction must be
aligned on a 4 byte boundary.  It compares the 20 bit immediate with
x7. If ``imm_20bit`` == 0, the CPU doesn't perform any comparison with
``x7``. If ``imm_20bit`` != 0, then ``imm_20bit`` must match ``x7``
else CPU will raise ``software check exception`` (``cause=18``) with
``*tval = 2``.

The compiler can generate a hash over function signatures and set them
up (truncated to 20 bits) in x7 at callsites.  Function prologues can
have ``lpad`` instructions encoded with the same function hash. This
further reduces the number of valid program counter addresses a call
site can reach.

2. ELF and psABI
-----------------

The toolchain sets up :c:macro:`GNU_PROPERTY_RISCV_FEATURE_1_FCFI` for
property :c:macro:`GNU_PROPERTY_RISCV_FEATURE_1_AND` in the notes
section of the object file.

3. Linux enabling
------------------

User space programs can have multiple shared objects loaded in their
address spaces.  It's a difficult task to make sure all the
dependencies have been compiled with indirect branch support. Thus
it's left to the dynamic loader to enable indirect branch tracking for
the program.

4. prctl() enabling
--------------------

Per-task indirect branch tracking state can be monitored and
controlled via the :c:macro:`PR_GET_CFI` and :c:macro:`PR_SET_CFI`
``prctl()` arguments (respectively), by supplying
:c:macro:`PR_CFI_BRANCH_LANDING_PADS` as the second argument.  These
are architecture-agnostic, and will return -EINVAL if the underlying
functionality is not supported.

* prctl(:c:macro:`PR_SET_CFI`, :c:macro:`PR_CFI_BRANCH_LANDING_PADS`, unsigned long arg)

arg is a bitmask.

If :c:macro:`PR_CFI_ENABLE` is set in arg, and the CPU supports
``zicfilp``, then the kernel will enable indirect branch tracking for
the task.  The dynamic loader can issue this ``prctl()`` once it has
determined that all the objects loaded in the address space support
indirect branch tracking.

Indirect branch tracking state can also be locked once enabled.  This
prevents the task from subsequently disabling it.  This is done by
setting the bit :c:macro:`PR_CFI_LOCK` in arg.  Either indirect branch
tracking must already be enabled for the task, or the bit
:c:macro:`PR_CFI_ENABLE` must also be set in arg.  This is intended
for environments that wish to run with a strict security posture that
do not wish to load objects without ``zicfilp`` support.

Indirect branch tracking can also be disabled for the task, assuming
that it has not previously been enabled and locked.  If there is a
``dlopen()`` to an object which wasn't compiled with ``zicfilp``, the
dynamic loader can issue this ``prctl()`` with arg set to
:c:macro:`PR_CFI_DISABLE`.  Disabling indirect branch tracking for the
task is not possible if it has previously been enabled and locked.


* prctl(:c:macro:`PR_GET_CFI`, :c:macro:`PR_CFI_BRANCH_LANDING_PADS`, unsigned long * arg)

Returns the current status of indirect branch tracking into a bitmask
stored into the memory location pointed to by arg.  The bitmask will
have the :c:macro:`PR_CFI_ENABLE` bit set if indirect branch tracking
is currently enabled for the task, and if it is locked, will
additionally have the :c:macro:`PR_CFI_LOCK` bit set.  If indirect
branch tracking is currently disabled for the task, the
:c:macro:`PR_CFI_DISABLE` bit will be set.


5. violations related to indirect branch tracking
--------------------------------------------------

Pertaining to indirect branch tracking, the CPU raises a software
check exception in the following conditions:

- missing ``lpad`` after indirect call / jmp
- ``lpad`` not on 4 byte boundary
- ``imm_20bit`` embedded in ``lpad`` instruction doesn't match with ``x7``

In all 3 cases, ``*tval = 2`` is captured and software check exception is
raised (``cause=18``).

The kernel will treat this as :c:macro:`SIGSEGV` with code =
:c:macro:`SEGV_CPERR` and follow the normal course of signal delivery.
