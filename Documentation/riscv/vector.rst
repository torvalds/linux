.. SPDX-License-Identifier: GPL-2.0

=========================================
Vector Extension Support for RISC-V Linux
=========================================

This document briefly outlines the interface provided to userspace by Linux in
order to support the use of the RISC-V Vector Extension.

1.  prctl() Interface
---------------------

Two new prctl() calls are added to allow programs to manage the enablement
status for the use of Vector in userspace. The intended usage guideline for
these interfaces is to give init systems a way to modify the availability of V
for processes running under its domain. Calling thess interfaces is not
recommended in libraries routines because libraries should not override policies
configured from the parant process. Also, users must noted that these interfaces
are not portable to non-Linux, nor non-RISC-V environments, so it is discourage
to use in a portable code. To get the availability of V in an ELF program,
please read :c:macro:`COMPAT_HWCAP_ISA_V` bit of :c:macro:`ELF_HWCAP` in the
auxiliary vector.

* prctl(PR_RISCV_V_SET_CONTROL, unsigned long arg)

    Sets the Vector enablement status of the calling thread, where the control
    argument consists of two 2-bit enablement statuses and a bit for inheritance
    mode. Other threads of the calling process are unaffected.

    Enablement status is a tri-state value each occupying 2-bit of space in
    the control argument:

    * :c:macro:`PR_RISCV_V_VSTATE_CTRL_DEFAULT`: Use the system-wide default
      enablement status on execve(). The system-wide default setting can be
      controlled via sysctl interface (see sysctl section below).

    * :c:macro:`PR_RISCV_V_VSTATE_CTRL_ON`: Allow Vector to be run for the
      thread.

    * :c:macro:`PR_RISCV_V_VSTATE_CTRL_OFF`: Disallow Vector. Executing Vector
      instructions under such condition will trap and casuse the termination of the thread.

    arg: The control argument is a 5-bit value consisting of 3 parts, and
    accessed by 3 masks respectively.

    The 3 masks, PR_RISCV_V_VSTATE_CTRL_CUR_MASK,
    PR_RISCV_V_VSTATE_CTRL_NEXT_MASK, and PR_RISCV_V_VSTATE_CTRL_INHERIT
    represents bit[1:0], bit[3:2], and bit[4]. bit[1:0] accounts for the
    enablement status of current thread, and the setting at bit[3:2] takes place
    at next execve(). bit[4] defines the inheritance mode of the setting in
    bit[3:2].

        * :c:macro:`PR_RISCV_V_VSTATE_CTRL_CUR_MASK`: bit[1:0]: Account for the
          Vector enablement status for the calling thread. The calling thread is
          not able to turn off Vector once it has been enabled. The prctl() call
          fails with EPERM if the value in this mask is PR_RISCV_V_VSTATE_CTRL_OFF
          but the current enablement status is not off. Setting
          PR_RISCV_V_VSTATE_CTRL_DEFAULT here takes no effect but to set back
          the original enablement status.

        * :c:macro:`PR_RISCV_V_VSTATE_CTRL_NEXT_MASK`: bit[3:2]: Account for the
          Vector enablement setting for the calling thread at the next execve()
          system call. If PR_RISCV_V_VSTATE_CTRL_DEFAULT is used in this mask,
          then the enablement status will be decided by the system-wide
          enablement status when execve() happen.

        * :c:macro:`PR_RISCV_V_VSTATE_CTRL_INHERIT`: bit[4]: the inheritance
          mode for the setting at PR_RISCV_V_VSTATE_CTRL_NEXT_MASK. If the bit
          is set then the following execve() will not clear the setting in both
          PR_RISCV_V_VSTATE_CTRL_NEXT_MASK and PR_RISCV_V_VSTATE_CTRL_INHERIT.
          This setting persists across changes in the system-wide default value.

    Return value:
        * 0 on success;
        * EINVAL: Vector not supported, invalid enablement status for current or
          next mask;
        * EPERM: Turning off Vector in PR_RISCV_V_VSTATE_CTRL_CUR_MASK if Vector
          was enabled for the calling thread.

    On success:
        * A valid setting for PR_RISCV_V_VSTATE_CTRL_CUR_MASK takes place
          immediately. The enablement status specified in
          PR_RISCV_V_VSTATE_CTRL_NEXT_MASK happens at the next execve() call, or
          all following execve() calls if PR_RISCV_V_VSTATE_CTRL_INHERIT bit is
          set.
        * Every successful call overwrites a previous setting for the calling
          thread.

* prctl(PR_RISCV_V_GET_CONTROL)

    Gets the same Vector enablement status for the calling thread. Setting for
    next execve() call and the inheritance bit are all OR-ed together.

    Note that ELF programs are able to get the availability of V for itself by
    reading :c:macro:`COMPAT_HWCAP_ISA_V` bit of :c:macro:`ELF_HWCAP` in the
    auxiliary vector.

    Return value:
        * a nonnegative value on success;
        * EINVAL: Vector not supported.

2.  System runtime configuration (sysctl)
-----------------------------------------

To mitigate the ABI impact of expansion of the signal stack, a
policy mechanism is provided to the administrators, distro maintainers, and
developers to control the default Vector enablement status for userspace
processes in form of sysctl knob:

* /proc/sys/abi/riscv_v_default_allow

    Writing the text representation of 0 or 1 to this file sets the default
    system enablement status for new starting userspace programs. Valid values
    are:

    * 0: Do not allow Vector code to be executed as the default for new processes.
    * 1: Allow Vector code to be executed as the default for new processes.

    Reading this file returns the current system default enablement status.

    At every execve() call, a new enablement status of the new process is set to
    the system default, unless:

      * PR_RISCV_V_VSTATE_CTRL_INHERIT is set for the calling process, and the
        setting in PR_RISCV_V_VSTATE_CTRL_NEXT_MASK is not
        PR_RISCV_V_VSTATE_CTRL_DEFAULT. Or,

      * The setting in PR_RISCV_V_VSTATE_CTRL_NEXT_MASK is not
        PR_RISCV_V_VSTATE_CTRL_DEFAULT.

    Modifying the system default enablement status does not affect the enablement
    status of any existing process of thread that do not make an execve() call.

3.  Vector Register State Across System Calls
---------------------------------------------

As indicated by version 1.0 of the V extension [1], vector registers are
clobbered by system calls.

1: https://github.com/riscv/riscv-v-spec/blob/master/calling-convention.adoc
