		SECure COMPuting with filters
		=============================

Introduction
------------

A large number of system calls are exposed to every userland process
with many of them going unused for the entire lifetime of the process.
As system calls change and mature, bugs are found and eradicated.  A
certain subset of userland applications benefit by having a reduced set
of available system calls.  The resulting set reduces the total kernel
surface exposed to the application.  System call filtering is meant for
use with those applications.

Seccomp filtering provides a means for a process to specify a filter for
incoming system calls.  The filter is expressed as a Berkeley Packet
Filter (BPF) program, as with socket filters, except that the data
operated on is related to the system call being made: system call
number and the system call arguments.  This allows for expressive
filtering of system calls using a filter program language with a long
history of being exposed to userland and a straightforward data set.

Additionally, BPF makes it impossible for users of seccomp to fall prey
to time-of-check-time-of-use (TOCTOU) attacks that are common in system
call interposition frameworks.  BPF programs may not dereference
pointers which constrains all filters to solely evaluating the system
call arguments directly.

What it isn't
-------------

System call filtering isn't a sandbox.  It provides a clearly defined
mechanism for minimizing the exposed kernel surface.  It is meant to be
a tool for sandbox developers to use.  Beyond that, policy for logical
behavior and information flow should be managed with a combination of
other system hardening techniques and, potentially, an LSM of your
choosing.  Expressive, dynamic filters provide further options down this
path (avoiding pathological sizes or selecting which of the multiplexed
system calls in socketcall() is allowed, for instance) which could be
construed, incorrectly, as a more complete sandboxing solution.

Usage
-----

An additional seccomp mode is added and is enabled using the same
prctl(2) call as the strict seccomp.  If the architecture has
CONFIG_HAVE_ARCH_SECCOMP_FILTER, then filters may be added as below:

PR_SET_SECCOMP:
	Now takes an additional argument which specifies a new filter
	using a BPF program.
	The BPF program will be executed over struct seccomp_data
	reflecting the system call number, arguments, and other
	metadata.  The BPF program must then return one of the
	acceptable values to inform the kernel which action should be
	taken.

	Usage:
		prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, prog);

	The 'prog' argument is a pointer to a struct sock_fprog which
	will contain the filter program.  If the program is invalid, the
	call will return -1 and set errno to EINVAL.

	If fork/clone and execve are allowed by @prog, any child
	processes will be constrained to the same filters and system
	call ABI as the parent.

	Prior to use, the task must call prctl(PR_SET_NO_NEW_PRIVS, 1) or
	run with CAP_SYS_ADMIN privileges in its namespace.  If these are not
	true, -EACCES will be returned.  This requirement ensures that filter
	programs cannot be applied to child processes with greater privileges
	than the task that installed them.

	Additionally, if prctl(2) is allowed by the attached filter,
	additional filters may be layered on which will increase evaluation
	time, but allow for further decreasing the attack surface during
	execution of a process.

The above call returns 0 on success and non-zero on error.

Return values
-------------
A seccomp filter may return any of the following values. If multiple
filters exist, the return value for the evaluation of a given system
call will always use the highest precedent value. (For example,
SECCOMP_RET_KILL will always take precedence.)

In precedence order, they are:

SECCOMP_RET_KILL:
	Results in the task exiting immediately without executing the
	system call.  The exit status of the task (status & 0x7f) will
	be SIGSYS, not SIGKILL.

SECCOMP_RET_TRAP:
	Results in the kernel sending a SIGSYS signal to the triggering
	task without executing the system call.  siginfo->si_call_addr
	will show the address of the system call instruction, and
	siginfo->si_syscall and siginfo->si_arch will indicate which
	syscall was attempted.  The program counter will be as though
	the syscall happened (i.e. it will not point to the syscall
	instruction).  The return value register will contain an arch-
	dependent value -- if resuming execution, set it to something
	sensible.  (The architecture dependency is because replacing
	it with -ENOSYS could overwrite some useful information.)

	The SECCOMP_RET_DATA portion of the return value will be passed
	as si_errno.

	SIGSYS triggered by seccomp will have a si_code of SYS_SECCOMP.

SECCOMP_RET_ERRNO:
	Results in the lower 16-bits of the return value being passed
	to userland as the errno without executing the system call.

SECCOMP_RET_TRACE:
	When returned, this value will cause the kernel to attempt to
	notify a ptrace()-based tracer prior to executing the system
	call.  If there is no tracer present, -ENOSYS is returned to
	userland and the system call is not executed.

	A tracer will be notified if it requests PTRACE_O_TRACESECCOMP
	using ptrace(PTRACE_SETOPTIONS).  The tracer will be notified
	of a PTRACE_EVENT_SECCOMP and the SECCOMP_RET_DATA portion of
	the BPF program return value will be available to the tracer
	via PTRACE_GETEVENTMSG.

	The tracer can skip the system call by changing the syscall number
	to -1.  Alternatively, the tracer can change the system call
	requested by changing the system call to a valid syscall number.  If
	the tracer asks to skip the system call, then the system call will
	appear to return the value that the tracer puts in the return value
	register.

	The seccomp check will not be run again after the tracer is
	notified.  (This means that seccomp-based sandboxes MUST NOT
	allow use of ptrace, even of other sandboxed processes, without
	extreme care; ptracers can use this mechanism to escape.)

SECCOMP_RET_ALLOW:
	Results in the system call being executed.

If multiple filters exist, the return value for the evaluation of a
given system call will always use the highest precedent value.

Precedence is only determined using the SECCOMP_RET_ACTION mask.  When
multiple filters return values of the same precedence, only the
SECCOMP_RET_DATA from the most recently installed filter will be
returned.

Pitfalls
--------

The biggest pitfall to avoid during use is filtering on system call
number without checking the architecture value.  Why?  On any
architecture that supports multiple system call invocation conventions,
the system call numbers may vary based on the specific invocation.  If
the numbers in the different calling conventions overlap, then checks in
the filters may be abused.  Always check the arch value!

Example
-------

The samples/seccomp/ directory contains both an x86-specific example
and a more generic example of a higher level macro interface for BPF
program generation.



Adding architecture support
-----------------------

See arch/Kconfig for the authoritative requirements.  In general, if an
architecture supports both ptrace_event and seccomp, it will be able to
support seccomp filter with minor fixup: SIGSYS support and seccomp return
value checking.  Then it must just add CONFIG_HAVE_ARCH_SECCOMP_FILTER
to its arch-specific Kconfig.



Caveats
-------

The vDSO can cause some system calls to run entirely in userspace,
leading to surprises when you run programs on different machines that
fall back to real syscalls.  To minimize these surprises on x86, make
sure you test with
/sys/devices/system/clocksource/clocksource0/current_clocksource set to
something like acpi_pm.

On x86-64, vsyscall emulation is enabled by default.  (vsyscalls are
legacy variants on vDSO calls.)  Currently, emulated vsyscalls will honor seccomp, with a few oddities:

- A return value of SECCOMP_RET_TRAP will set a si_call_addr pointing to
  the vsyscall entry for the given call and not the address after the
  'syscall' instruction.  Any code which wants to restart the call
  should be aware that (a) a ret instruction has been emulated and (b)
  trying to resume the syscall will again trigger the standard vsyscall
  emulation security checks, making resuming the syscall mostly
  pointless.

- A return value of SECCOMP_RET_TRACE will signal the tracer as usual,
  but the syscall may not be changed to another system call using the
  orig_rax register. It may only be changed to -1 order to skip the
  currently emulated call. Any other change MAY terminate the process.
  The rip value seen by the tracer will be the syscall entry address;
  this is different from normal behavior.  The tracer MUST NOT modify
  rip or rsp.  (Do not rely on other changes terminating the process.
  They might work.  For example, on some kernels, choosing a syscall
  that only exists in future kernels will be correctly emulated (by
  returning -ENOSYS).

To detect this quirky behavior, check for addr & ~0x0C00 ==
0xFFFFFFFFFF600000.  (For SECCOMP_RET_TRACE, use rip.  For
SECCOMP_RET_TRAP, use siginfo->si_call_addr.)  Do not check any other
condition: future kernels may improve vsyscall emulation and current
kernels in vsyscall=native mode will behave differently, but the
instructions at 0xF...F600{0,4,8,C}00 will not be system calls in these
cases.

Note that modern systems are unlikely to use vsyscalls at all -- they
are a legacy feature and they are considerably slower than standard
syscalls.  New code will use the vDSO, and vDSO-issued system calls
are indistinguishable from normal system calls.
