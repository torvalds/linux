.. SPDX-License-Identifier: GPL-2.0

:Author: Deepak Gupta <debug@rivosinc.com>
:Date:   12 January 2024

=========================================================
Shadow stack to protect function returns on RISC-V Linux
=========================================================

This document briefly describes the interface provided to userspace by Linux
to enable shadow stacks for user mode applications on RISC-V.

1. Feature Overview
--------------------

Memory corruption issues usually result in crashes.  However, in the
hands of a creative adversary, these issues can result in a variety of
security problems.

Some of those security issues can be code re-use attacks on programs
where an adversary can use corrupt return addresses present on the
stack. chaining them together to perform return oriented programming
(ROP) and thus compromising the control flow integrity (CFI) of the
program.

Return addresses live on the stack in read-write memory.  Therefore
they are susceptible to corruption, which allows an adversary to
control the program counter. On RISC-V, the ``zicfiss`` extension
provides an alternate stack (the "shadow stack") on which return
addresses can be safely placed in the prologue of the function and
retrieved in the epilogue.  The ``zicfiss`` extension makes the
following changes:

- PTE encodings for shadow stack virtual memory
  An earlier reserved encoding in first stage translation i.e.
  PTE.R=0, PTE.W=1, PTE.X=0  becomes the PTE encoding for shadow stack pages.

- The ``sspush x1/x5`` instruction pushes (stores) ``x1/x5`` to shadow stack.

- The ``sspopchk x1/x5`` instruction pops (loads) from shadow stack and compares
  with ``x1/x5`` and if not equal, the CPU raises a ``software check exception``
  with ``*tval = 3``

The compiler toolchain ensures that function prologues have ``sspush
x1/x5`` to save the return address on shadow stack in addition to the
regular stack.  Similarly, function epilogues have ``ld x5,
offset(x2)`` followed by ``sspopchk x5`` to ensure that a popped value
from the regular stack matches with the popped value from the shadow
stack.

2. Shadow stack protections and linux memory manager
-----------------------------------------------------

As mentioned earlier, shadow stacks get new page table encodings that
have some special properties assigned to them, along with instructions
that operate on the shadow stacks:

- Regular stores to shadow stack memory raise store access faults. This
  protects shadow stack memory from stray writes.

- Regular loads from shadow stack memory are allowed. This allows
  stack trace utilities or backtrace functions to read the true call
  stack and ensure that it has not been tampered with.

- Only shadow stack instructions can generate shadow stack loads or
  shadow stack stores.

- Shadow stack loads and stores on read-only memory raise AMO/store
  page faults. Thus both ``sspush x1/x5`` and ``sspopchk x1/x5`` will
  raise AMO/store page fault. This simplies COW handling in kernel
  during fork(). The kernel can convert shadow stack pages into
  read-only memory (as it does for regular read-write memory).  As
  soon as subsequent ``sspush`` or ``sspopchk`` instructions in
  userspace are encountered, the kernel can perform COW.

- Shadow stack loads and stores on read-write or read-write-execute
  memory raise an access fault. This is a fatal condition because
  shadow stack loads and stores should never be operating on
  read-write or read-write-execute memory.

3. ELF and psABI
-----------------

The toolchain sets up :c:macro:`GNU_PROPERTY_RISCV_FEATURE_1_BCFI` for
property :c:macro:`GNU_PROPERTY_RISCV_FEATURE_1_AND` in the notes
section of the object file.

4. Linux enabling
------------------

User space programs can have multiple shared objects loaded in their
address space.  It's a difficult task to make sure all the
dependencies have been compiled with shadow stack support.  Thus
it's left to the dynamic loader to enable shadow stacks for the
program.

5. prctl() enabling
--------------------

:c:macro:`PR_SET_SHADOW_STACK_STATUS` / :c:macro:`PR_GET_SHADOW_STACK_STATUS` /
:c:macro:`PR_LOCK_SHADOW_STACK_STATUS` are three prctls added to manage shadow
stack enabling for tasks.  These prctls are architecture-agnostic and return
-EINVAL if not implemented.

* prctl(PR_SET_SHADOW_STACK_STATUS, unsigned long arg)

If arg = :c:macro:`PR_SHADOW_STACK_ENABLE` and if CPU supports
``zicfiss`` then the kernel will enable shadow stacks for the task.
The dynamic loader can issue this :c:macro:`prctl` once it has
determined that all the objects loaded in address space have support
for shadow stacks.  Additionally, if there is a :c:macro:`dlopen` to
an object which wasn't compiled with ``zicfiss``, the dynamic loader
can issue this prctl with arg set to 0 (i.e.
:c:macro:`PR_SHADOW_STACK_ENABLE` being clear)

* prctl(PR_GET_SHADOW_STACK_STATUS, unsigned long * arg)

Returns the current status of indirect branch tracking. If enabled
it'll return :c:macro:`PR_SHADOW_STACK_ENABLE`.

* prctl(PR_LOCK_SHADOW_STACK_STATUS, unsigned long arg)

Locks the current status of shadow stack enabling on the
task. Userspace may want to run with a strict security posture and
wouldn't want loading of objects without ``zicfiss`` support.  In this
case userspace can use this prctl to disallow disabling of shadow
stacks on the current task.

5. violations related to returns with shadow stack enabled
-----------------------------------------------------------

Pertaining to shadow stacks, the CPU raises a ``software check
exception`` upon executing ``sspopchk x1/x5`` if ``x1/x5`` doesn't
match the top of shadow stack.  If a mismatch happens, then the CPU
sets ``*tval = 3`` and raises the exception.

The Linux kernel will treat this as a :c:macro:`SIGSEGV` with code =
:c:macro:`SEGV_CPERR` and follow the normal course of signal delivery.

6. Shadow stack tokens
-----------------------

Regular stores on shadow stacks are not allowed and thus can't be
tampered with via arbitrary stray writes.  However, one method of
pivoting / switching to a shadow stack is simply writing to the CSR
``CSR_SSP``.  This will change the active shadow stack for the
program.  Writes to ``CSR_SSP`` in the program should be mostly
limited to context switches, stack unwinds, or longjmp or similar
mechanisms (like context switching of Green Threads) in languages like
Go and Rust. CSR_SSP writes can be problematic because an attacker can
use memory corruption bugs and leverage context switching routines to
pivot to any shadow stack. Shadow stack tokens can help mitigate this
problem by making sure that:

- When software is switching away from a shadow stack, the shadow
  stack pointer should be saved on the shadow stack itself (this is
  called the ``shadow stack token``).

- When software is switching to a shadow stack, it should read the
  ``shadow stack token`` from the shadow stack pointer and verify that
  the ``shadow stack token`` itself is a pointer to the shadow stack
  itself.

- Once the token verification is done, software can perform the write
  to ``CSR_SSP`` to switch shadow stacks.

Here "software" could refer to the user mode task runtime itself,
managing various contexts as part of a single thread.  Or "software"
could refer to the kernel, when the kernel has to deliver a signal to
a user task and must save the shadow stack pointer.  The kernel can
perform similar procedure itself by saving a token on the user mode
task's shadow stack.  This way, whenever :c:macro:`sigreturn` happens,
the kernel can read and verify the token and then switch to the shadow
stack. Using this mechanism, the kernel helps the user task so that
any corruption issue in the user task is not exploited by adversaries
arbitrarily using :c:macro:`sigreturn`. Adversaries will have to make
sure that there is a valid ``shadow stack token`` in addition to
invoking :c:macro:`sigreturn`.

7. Signal shadow stack
-----------------------
The following structure has been added to sigcontext for RISC-V::

    struct __sc_riscv_cfi_state {
        unsigned long ss_ptr;
    };

As part of signal delivery, the shadow stack token is saved on the
current shadow stack itself.  The updated pointer is saved away in the
:c:macro:`ss_ptr` field in :c:macro:`__sc_riscv_cfi_state` under
:c:macro:`sigcontext`. The existing shadow stack allocation is used
for signal delivery.  During :c:macro:`sigreturn`, kernel will obtain
:c:macro:`ss_ptr` from :c:macro:`sigcontext`, verify the saved
token on the shadow stack, and switch the shadow stack.
