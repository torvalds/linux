===================================================
Scalable Matrix Extension support for AArch64 Linux
===================================================

This document outlines briefly the interface provided to userspace by Linux in
order to support use of the ARM Scalable Matrix Extension (SME).

This is an outline of the most important features and issues only and not
intended to be exhaustive.  It should be read in conjunction with the SVE
documentation in sve.rst which provides details on the Streaming SVE mode
included in SME.

This document does not aim to describe the SME architecture or programmer's
model.  To aid understanding, a minimal description of relevant programmer's
model features for SME is included in Appendix A.


1.  General
-----------

* PSTATE.SM, PSTATE.ZA, the streaming mode vector length, the ZA
  register state and TPIDR2_EL0 are tracked per thread.

* The presence of SME is reported to userspace via HWCAP2_SME in the aux vector
  AT_HWCAP2 entry.  Presence of this flag implies the presence of the SME
  instructions and registers, and the Linux-specific system interfaces
  described in this document.  SME is reported in /proc/cpuinfo as "sme".

* Support for the execution of SME instructions in userspace can also be
  detected by reading the CPU ID register ID_AA64PFR1_EL1 using an MRS
  instruction, and checking that the value of the SME field is nonzero. [3]

  It does not guarantee the presence of the system interfaces described in the
  following sections: software that needs to verify that those interfaces are
  present must check for HWCAP2_SME instead.

* There are a number of optional SME features, presence of these is reported
  through AT_HWCAP2 through:

	HWCAP2_SME_I16I64
	HWCAP2_SME_F64F64
	HWCAP2_SME_I8I32
	HWCAP2_SME_F16F32
	HWCAP2_SME_B16F32
	HWCAP2_SME_F32F32
	HWCAP2_SME_FA64

  This list may be extended over time as the SME architecture evolves.

  These extensions are also reported via the CPU ID register ID_AA64SMFR0_EL1,
  which userspace can read using an MRS instruction.  See elf_hwcaps.txt and
  cpu-feature-registers.txt for details.

* Debuggers should restrict themselves to interacting with the target via the
  NT_ARM_SVE, NT_ARM_SSVE and NT_ARM_ZA regsets.  The recommended way
  of detecting support for these regsets is to connect to a target process
  first and then attempt a

	ptrace(PTRACE_GETREGSET, pid, NT_ARM_<regset>, &iov).

* Whenever ZA register values are exchanged in memory between userspace and
  the kernel, the register value is encoded in memory as a series of horizontal
  vectors from 0 to VL/8-1 stored in the same endianness invariant format as is
  used for SVE vectors.

* On thread creation TPIDR2_EL0 is preserved unless CLONE_SETTLS is specified,
  in which case it is set to 0.

2.  Vector lengths
------------------

SME defines a second vector length similar to the SVE vector length which is
controls the size of the streaming mode SVE vectors and the ZA matrix array.
The ZA matrix is square with each side having as many bytes as a streaming
mode SVE vector.


3.  Sharing of streaming and non-streaming mode SVE state
---------------------------------------------------------

It is implementation defined which if any parts of the SVE state are shared
between streaming and non-streaming modes.  When switching between modes
via software interfaces such as ptrace if no register content is provided as
part of switching no state will be assumed to be shared and everything will
be zeroed.


4.  System call behaviour
-------------------------

* On syscall PSTATE.ZA is preserved, if PSTATE.ZA==1 then the contents of the
  ZA matrix are preserved.

* On syscall PSTATE.SM will be cleared and the SVE registers will be handled
  as per the standard SVE ABI.

* Neither the SVE registers nor ZA are used to pass arguments to or receive
  results from any syscall.

* On process creation (eg, clone()) the newly created process will have
  PSTATE.SM cleared.

* All other SME state of a thread, including the currently configured vector
  length, the state of the PR_SME_VL_INHERIT flag, and the deferred vector
  length (if any), is preserved across all syscalls, subject to the specific
  exceptions for execve() described in section 6.


5.  Signal handling
-------------------

* Signal handlers are invoked with streaming mode and ZA disabled.

* A new signal frame record za_context encodes the ZA register contents on
  signal delivery. [1]

* The signal frame record for ZA always contains basic metadata, in particular
  the thread's vector length (in za_context.vl).

* The ZA matrix may or may not be included in the record, depending on
  the value of PSTATE.ZA.  The registers are present if and only if:
  za_context.head.size >= ZA_SIG_CONTEXT_SIZE(sve_vq_from_vl(za_context.vl))
  in which case PSTATE.ZA == 1.

* If matrix data is present, the remainder of the record has a vl-dependent
  size and layout.  Macros ZA_SIG_* are defined [1] to facilitate access to
  them.

* The matrix is stored as a series of horizontal vectors in the same format as
  is used for SVE vectors.

* If the ZA context is too big to fit in sigcontext.__reserved[], then extra
  space is allocated on the stack, an extra_context record is written in
  __reserved[] referencing this space.  za_context is then written in the
  extra space.  Refer to [1] for further details about this mechanism.


5.  Signal return
-----------------

When returning from a signal handler:

* If there is no za_context record in the signal frame, or if the record is
  present but contains no register data as described in the previous section,
  then ZA is disabled.

* If za_context is present in the signal frame and contains matrix data then
  PSTATE.ZA is set to 1 and ZA is populated with the specified data.

* The vector length cannot be changed via signal return.  If za_context.vl in
  the signal frame does not match the current vector length, the signal return
  attempt is treated as illegal, resulting in a forced SIGSEGV.


6.  prctl extensions
--------------------

Some new prctl() calls are added to allow programs to manage the SME vector
length:

prctl(PR_SME_SET_VL, unsigned long arg)

    Sets the vector length of the calling thread and related flags, where
    arg == vl | flags.  Other threads of the calling process are unaffected.

    vl is the desired vector length, where sve_vl_valid(vl) must be true.

    flags:

	PR_SME_VL_INHERIT

	    Inherit the current vector length across execve().  Otherwise, the
	    vector length is reset to the system default at execve().  (See
	    Section 9.)

	PR_SME_SET_VL_ONEXEC

	    Defer the requested vector length change until the next execve()
	    performed by this thread.

	    The effect is equivalent to implicit execution of the following
	    call immediately after the next execve() (if any) by the thread:

		prctl(PR_SME_SET_VL, arg & ~PR_SME_SET_VL_ONEXEC)

	    This allows launching of a new program with a different vector
	    length, while avoiding runtime side effects in the caller.

	    Without PR_SME_SET_VL_ONEXEC, the requested change takes effect
	    immediately.


    Return value: a nonnegative on success, or a negative value on error:
	EINVAL: SME not supported, invalid vector length requested, or
	    invalid flags.


    On success:

    * Either the calling thread's vector length or the deferred vector length
      to be applied at the next execve() by the thread (dependent on whether
      PR_SME_SET_VL_ONEXEC is present in arg), is set to the largest value
      supported by the system that is less than or equal to vl.  If vl ==
      SVE_VL_MAX, the value set will be the largest value supported by the
      system.

    * Any previously outstanding deferred vector length change in the calling
      thread is cancelled.

    * The returned value describes the resulting configuration, encoded as for
      PR_SME_GET_VL.  The vector length reported in this value is the new
      current vector length for this thread if PR_SME_SET_VL_ONEXEC was not
      present in arg; otherwise, the reported vector length is the deferred
      vector length that will be applied at the next execve() by the calling
      thread.

    * Changing the vector length causes all of ZA, P0..P15, FFR and all bits of
      Z0..Z31 except for Z0 bits [127:0] .. Z31 bits [127:0] to become
      unspecified, including both streaming and non-streaming SVE state.
      Calling PR_SME_SET_VL with vl equal to the thread's current vector
      length, or calling PR_SME_SET_VL with the PR_SVE_SET_VL_ONEXEC flag,
      does not constitute a change to the vector length for this purpose.

    * Changing the vector length causes PSTATE.ZA and PSTATE.SM to be cleared.
      Calling PR_SME_SET_VL with vl equal to the thread's current vector
      length, or calling PR_SME_SET_VL with the PR_SVE_SET_VL_ONEXEC flag,
      does not constitute a change to the vector length for this purpose.


prctl(PR_SME_GET_VL)

    Gets the vector length of the calling thread.

    The following flag may be OR-ed into the result:

	PR_SME_VL_INHERIT

	    Vector length will be inherited across execve().

    There is no way to determine whether there is an outstanding deferred
    vector length change (which would only normally be the case between a
    fork() or vfork() and the corresponding execve() in typical use).

    To extract the vector length from the result, bitwise and it with
    PR_SME_VL_LEN_MASK.

    Return value: a nonnegative value on success, or a negative value on error:
	EINVAL: SME not supported.


7.  ptrace extensions
---------------------

* A new regset NT_ARM_SSVE is defined for access to streaming mode SVE
  state via PTRACE_GETREGSET and  PTRACE_SETREGSET, this is documented in
  sve.rst.

* A new regset NT_ARM_ZA is defined for ZA state for access to ZA state via
  PTRACE_GETREGSET and PTRACE_SETREGSET.

  Refer to [2] for definitions.

The regset data starts with struct user_za_header, containing:

    size

	Size of the complete regset, in bytes.
	This depends on vl and possibly on other things in the future.

	If a call to PTRACE_GETREGSET requests less data than the value of
	size, the caller can allocate a larger buffer and retry in order to
	read the complete regset.

    max_size

	Maximum size in bytes that the regset can grow to for the target
	thread.  The regset won't grow bigger than this even if the target
	thread changes its vector length etc.

    vl

	Target thread's current streaming vector length, in bytes.

    max_vl

	Maximum possible streaming vector length for the target thread.

    flags

	Zero or more of the following flags, which have the same
	meaning and behaviour as the corresponding PR_SET_VL_* flags:

	    SME_PT_VL_INHERIT

	    SME_PT_VL_ONEXEC (SETREGSET only).

* The effects of changing the vector length and/or flags are equivalent to
  those documented for PR_SME_SET_VL.

  The caller must make a further GETREGSET call if it needs to know what VL is
  actually set by SETREGSET, unless is it known in advance that the requested
  VL is supported.

* The size and layout of the payload depends on the header fields.  The
  SME_PT_ZA_*() macros are provided to facilitate access to the data.

* In either case, for SETREGSET it is permissible to omit the payload, in which
  case the vector length and flags are changed and PSTATE.ZA is set to 0
  (along with any consequences of those changes).  If a payload is provided
  then PSTATE.ZA will be set to 1.

* For SETREGSET, if the requested VL is not supported, the effect will be the
  same as if the payload were omitted, except that an EIO error is reported.
  No attempt is made to translate the payload data to the correct layout
  for the vector length actually set.  It is up to the caller to translate the
  payload layout for the actual VL and retry.

* The effect of writing a partial, incomplete payload is unspecified.


8.  ELF coredump extensions
---------------------------

* NT_ARM_SSVE notes will be added to each coredump for
  each thread of the dumped process.  The contents will be equivalent to the
  data that would have been read if a PTRACE_GETREGSET of the corresponding
  type were executed for each thread when the coredump was generated.

* A NT_ARM_ZA note will be added to each coredump for each thread of the
  dumped process.  The contents will be equivalent to the data that would have
  been read if a PTRACE_GETREGSET of NT_ARM_ZA were executed for each thread
  when the coredump was generated.

* The NT_ARM_TLS note will be extended to two registers, the second register
  will contain TPIDR2_EL0 on systems that support SME and will be read as
  zero with writes ignored otherwise.

9.  System runtime configuration
--------------------------------

* To mitigate the ABI impact of expansion of the signal frame, a policy
  mechanism is provided for administrators, distro maintainers and developers
  to set the default vector length for userspace processes:

/proc/sys/abi/sme_default_vector_length

    Writing the text representation of an integer to this file sets the system
    default vector length to the specified value, unless the value is greater
    than the maximum vector length supported by the system in which case the
    default vector length is set to that maximum.

    The result can be determined by reopening the file and reading its
    contents.

    At boot, the default vector length is initially set to 32 or the maximum
    supported vector length, whichever is smaller and supported.  This
    determines the initial vector length of the init process (PID 1).

    Reading this file returns the current system default vector length.

* At every execve() call, the new vector length of the new process is set to
  the system default vector length, unless

    * PR_SME_VL_INHERIT (or equivalently SME_PT_VL_INHERIT) is set for the
      calling thread, or

    * a deferred vector length change is pending, established via the
      PR_SME_SET_VL_ONEXEC flag (or SME_PT_VL_ONEXEC).

* Modifying the system default vector length does not affect the vector length
  of any existing process or thread that does not make an execve() call.


Appendix A.  SME programmer's model (informative)
=================================================

This section provides a minimal description of the additions made by SME to the
ARMv8-A programmer's model that are relevant to this document.

Note: This section is for information only and not intended to be complete or
to replace any architectural specification.

A.1.  Registers
---------------

In A64 state, SME adds the following:

* A new mode, streaming mode, in which a subset of the normal FPSIMD and SVE
  features are available.  When supported EL0 software may enter and leave
  streaming mode at any time.

  For best system performance it is strongly encouraged for software to enable
  streaming mode only when it is actively being used.

* A new vector length controlling the size of ZA and the Z registers when in
  streaming mode, separately to the vector length used for SVE when not in
  streaming mode.  There is no requirement that either the currently selected
  vector length or the set of vector lengths supported for the two modes in
  a given system have any relationship.  The streaming mode vector length
  is referred to as SVL.

* A new ZA matrix register.  This is a square matrix of SVLxSVL bits.  Most
  operations on ZA require that streaming mode be enabled but ZA can be
  enabled without streaming mode in order to load, save and retain data.

  For best system performance it is strongly encouraged for software to enable
  ZA only when it is actively being used.

* Two new 1 bit fields in PSTATE which may be controlled via the SMSTART and
  SMSTOP instructions or by access to the SVCR system register:

  * PSTATE.ZA, if this is 1 then the ZA matrix is accessible and has valid
    data while if it is 0 then ZA can not be accessed.  When PSTATE.ZA is
    changed from 0 to 1 all bits in ZA are cleared.

  * PSTATE.SM, if this is 1 then the PE is in streaming mode.  When the value
    of PSTATE.SM is changed then it is implementation defined if the subset
    of the floating point register bits valid in both modes may be retained.
    Any other bits will be cleared.


References
==========

[1] arch/arm64/include/uapi/asm/sigcontext.h
    AArch64 Linux signal ABI definitions

[2] arch/arm64/include/uapi/asm/ptrace.h
    AArch64 Linux ptrace ABI definitions

[3] Documentation/arm64/cpu-feature-registers.rst
