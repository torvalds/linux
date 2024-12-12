.. SPDX-License-Identifier: GPL-2.0
.. Copyright © 2024 Microsoft Corporation

===================
Executability check
===================

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
