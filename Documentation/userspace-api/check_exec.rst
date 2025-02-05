.. SPDX-License-Identifier: GPL-2.0
.. Copyright © 2024 Microsoft Corporation

===================
Executability check
===================

The ``AT_EXECVE_CHECK`` :manpage:`execveat(2)` flag, and the
``SECBIT_EXEC_RESTRICT_FILE`` and ``SECBIT_EXEC_DENY_INTERACTIVE`` securebits
are intended for script interpreters and dynamic linkers to enforce a
consistent execution security policy handled by the kernel.  See the
`samples/check-exec/inc.c`_ example.

Whether an interpreter should check these securebits or not depends on the
security risk of running malicious scripts with respect to the execution
environment, and whether the kernel can check if a script is trustworthy or
not.  For instance, Python scripts running on a server can use arbitrary
syscalls and access arbitrary files.  Such interpreters should then be
enlighten to use these securebits and let users define their security policy.
However, a JavaScript engine running in a web browser should already be
sandboxed and then should not be able to harm the user's environment.

Script interpreters or dynamic linkers built for tailored execution environments
(e.g. hardened Linux distributions or hermetic container images) could use
``AT_EXECVE_CHECK`` without checking the related securebits if backward
compatibility is handled by something else (e.g. atomic update ensuring that
all legitimate libraries are allowed to be executed).  It is then recommended
for script interpreters and dynamic linkers to check the securebits at run time
by default, but also to provide the ability for custom builds to behave like if
``SECBIT_EXEC_RESTRICT_FILE`` or ``SECBIT_EXEC_DENY_INTERACTIVE`` were always
set to 1 (i.e. always enforce restrictions).

AT_EXECVE_CHECK
===============

Passing the ``AT_EXECVE_CHECK`` flag to :manpage:`execveat(2)` only performs a
check on a regular file and returns 0 if execution of this file would be
allowed, ignoring the file format and then the related interpreter dependencies
(e.g. ELF libraries, script's shebang).

Programs should always perform this check to apply kernel-level checks against
files that are not directly executed by the kernel but passed to a user space
interpreter instead.  All files that contain executable code, from the point of
view of the interpreter, should be checked.  However the result of this check
should only be enforced according to ``SECBIT_EXEC_RESTRICT_FILE`` or
``SECBIT_EXEC_DENY_INTERACTIVE.``.

The main purpose of this flag is to improve the security and consistency of an
execution environment to ensure that direct file execution (e.g.
``./script.sh``) and indirect file execution (e.g. ``sh script.sh``) lead to
the same result.  For instance, this can be used to check if a file is
trustworthy according to the caller's environment.

In a secure environment, libraries and any executable dependencies should also
be checked.  For instance, dynamic linking should make sure that all libraries
are allowed for execution to avoid trivial bypass (e.g. using ``LD_PRELOAD``).
For such secure execution environment to make sense, only trusted code should
be executable, which also requires integrity guarantees.

To avoid race conditions leading to time-of-check to time-of-use issues,
``AT_EXECVE_CHECK`` should be used with ``AT_EMPTY_PATH`` to check against a
file descriptor instead of a path.

SECBIT_EXEC_RESTRICT_FILE and SECBIT_EXEC_DENY_INTERACTIVE
==========================================================

When ``SECBIT_EXEC_RESTRICT_FILE`` is set, a process should only interpret or
execute a file if a call to :manpage:`execveat(2)` with the related file
descriptor and the ``AT_EXECVE_CHECK`` flag succeed.

This secure bit may be set by user session managers, service managers,
container runtimes, sandboxer tools...  Except for test environments, the
related ``SECBIT_EXEC_RESTRICT_FILE_LOCKED`` bit should also be set.

Programs should only enforce consistent restrictions according to the
securebits but without relying on any other user-controlled configuration.
Indeed, the use case for these securebits is to only trust executable code
vetted by the system configuration (through the kernel), so we should be
careful to not let untrusted users control this configuration.

However, script interpreters may still use user configuration such as
environment variables as long as it is not a way to disable the securebits
checks.  For instance, the ``PATH`` and ``LD_PRELOAD`` variables can be set by
a script's caller.  Changing these variables may lead to unintended code
executions, but only from vetted executable programs, which is OK.  For this to
make sense, the system should provide a consistent security policy to avoid
arbitrary code execution e.g., by enforcing a write xor execute policy.

When ``SECBIT_EXEC_DENY_INTERACTIVE`` is set, a process should never interpret
interactive user commands (e.g. scripts).  However, if such commands are passed
through a file descriptor (e.g. stdin), its content should be interpreted if a
call to :manpage:`execveat(2)` with the related file descriptor and the
``AT_EXECVE_CHECK`` flag succeed.

For instance, script interpreters called with a script snippet as argument
should always deny such execution if ``SECBIT_EXEC_DENY_INTERACTIVE`` is set.

This secure bit may be set by user session managers, service managers,
container runtimes, sandboxer tools...  Except for test environments, the
related ``SECBIT_EXEC_DENY_INTERACTIVE_LOCKED`` bit should also be set.

Here is the expected behavior for a script interpreter according to combination
of any exec securebits:

1. ``SECBIT_EXEC_RESTRICT_FILE=0`` and ``SECBIT_EXEC_DENY_INTERACTIVE=0``

   Always interpret scripts, and allow arbitrary user commands (default).

   No threat, everyone and everything is trusted, but we can get ahead of
   potential issues thanks to the call to :manpage:`execveat(2)` with
   ``AT_EXECVE_CHECK`` which should always be performed but ignored by the
   script interpreter.  Indeed, this check is still important to enable systems
   administrators to verify requests (e.g. with audit) and prepare for
   migration to a secure mode.

2. ``SECBIT_EXEC_RESTRICT_FILE=1`` and ``SECBIT_EXEC_DENY_INTERACTIVE=0``

   Deny script interpretation if they are not executable, but allow
   arbitrary user commands.

   The threat is (potential) malicious scripts run by trusted (and not fooled)
   users.  That can protect against unintended script executions (e.g. ``sh
   /tmp/*.sh``).  This makes sense for (semi-restricted) user sessions.

3. ``SECBIT_EXEC_RESTRICT_FILE=0`` and ``SECBIT_EXEC_DENY_INTERACTIVE=1``

   Always interpret scripts, but deny arbitrary user commands.

   This use case may be useful for secure services (i.e. without interactive
   user session) where scripts' integrity is verified (e.g.  with IMA/EVM or
   dm-verity/IPE) but where access rights might not be ready yet.  Indeed,
   arbitrary interactive commands would be much more difficult to check.

4. ``SECBIT_EXEC_RESTRICT_FILE=1`` and ``SECBIT_EXEC_DENY_INTERACTIVE=1``

   Deny script interpretation if they are not executable, and also deny
   any arbitrary user commands.

   The threat is malicious scripts run by untrusted users (but trusted code).
   This makes sense for system services that may only execute trusted scripts.

.. Links
.. _samples/check-exec/inc.c:
   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/samples/check-exec/inc.c
