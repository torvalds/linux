.. SPDX-License-Identifier: GPL-2.0

======
failfs
======

failfs is a kernel-internal filesystem that fails every operation
reaching it with ``EOPNOTSUPP``. It is the counterpart to nullfs. Where
nullfs is permanently empty, failfs means "nothing is supported here".
It cannot be mounted from userspace, nothing can be mounted on top of
it. It cannot be cloned.

The only way into it is the ``FD_FAILFS_ROOT`` file descriptor sentinel which
is understood by ``fchdir(2)`` and ``fchroot(2)``.

Semantics
=========

Every path walk of a component through failfs fails with
``EOPNOTSUPP`` before that component is parsed, including ``.``.

No path lookup can open the root, not even with ``O_PATH``.

A process with its working directory in failfs fails every
``AT_FDCWD``-relative lookup. As with any working directory that is
unreachable from the process root, the ``getcwd(2)`` system call returns
a path prefixed with ``(unreachable)``.

A process with its root directory in failfs fails every absolute path
lookup including absolute symlinks and the interpreter of dynamically
linked binaries. In other words, this fails exec.

Lookups anchored at explicit directory file descriptors keep working. It
is the ``fs_struct`` equivalent of ``RESOLVE_BENEATH``. The process must
anchor every lookup at a file descriptor it explicitly holds.

Entering
========

``fchroot(FD_FAILFS_ROOT, 0)`` requires ``CAP_SYS_CHROOT`` in the
caller's user namespace, mirroring ``chroot(2)``. Unprivileged callers
may enter if all of the following hold:

* ``no_new_privs`` is set: setuid binaries on regular mounts remain
  reachable via inherited directory file descriptors and executing them
  with an unusable root directory is the classic confused deputy.

* The caller is not already chrooted: the root directory is what
  confines ``..`` resolution and the failfs root can never be reached by
  walking up a real mount tree, so moving the root of a chrooted task to
  failfs would allow it to escape its chroot via ``openat(fd, "..")``.

* The caller does not share its ``fs_struct``: ``no_new_privs`` is
  checked on the calling thread, but the root lives in the ``fs_struct``.
  A ``CLONE_FS`` sibling without ``no_new_privs`` could otherwise execute
  a setuid binary with the failfs root, so entry requires ``fs->users ==
  1``, the same restriction ``setns(2)`` applies for the mount and user
  namespaces.

Leaving
=======

Backing out is currently hard, but this is a property of the current
implementation, not a guaranteed interface, and may be loosened later.
For now a process that entered failfs counts as chrooted, so it cannot
create user namespaces to regain ``CAP_SYS_CHROOT``, and ``chroot(2)``
or ``fchroot(2)`` back out require ``CAP_SYS_CHROOT``. The remaining way
out today is ``setns(2)`` with a mount namespace file descriptor, which
requires ``CAP_SYS_ADMIN`` over the target mount namespace as well as
``CAP_SYS_CHROOT`` and ``CAP_SYS_ADMIN`` in the caller's user namespace
and resets both root and working directory. A process that holds no such
file descriptor and restricts ``*chdir()``/``*chroot()``/``setns()`` via
seccomp cannot currently get back out.
