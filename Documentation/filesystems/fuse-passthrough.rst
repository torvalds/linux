.. SPDX-License-Identifier: GPL-2.0

================
FUSE Passthrough
================

Introduction
============

FUSE (Filesystem in Userspace) passthrough is a feature designed to improve the
performance of FUSE filesystems for I/O operations. Typically, FUSE operations
involve communication between the kernel and a userspace FUSE daemon, which can
incur overhead. Passthrough allows certain operations on a FUSE file to bypass
the userspace daemon and be executed directly by the kernel on an underlying
"backing file".

This is achieved by the FUSE daemon registering a file descriptor (pointing to
the backing file on a lower filesystem) with the FUSE kernel module. The kernel
then receives an identifier (``backing_id``) for this registered backing file.
When a FUSE file is subsequently opened, the FUSE daemon can, in its response to
the ``OPEN`` request, include this ``backing_id`` and set the
``FOPEN_PASSTHROUGH`` flag. This establishes a direct link for specific
operations.

Currently, passthrough is supported for operations like ``read(2)``/``write(2)``
(via ``read_iter``/``write_iter``), ``splice(2)``, and ``mmap(2)``.

Enabling Passthrough
====================

To use FUSE passthrough:

  1. The FUSE filesystem must be compiled with ``CONFIG_FUSE_PASSTHROUGH``
     enabled.
  2. The FUSE daemon, during the ``FUSE_INIT`` handshake, must negotiate the
     ``FUSE_PASSTHROUGH`` capability and specify its desired
     ``max_stack_depth``.
  3. The (privileged) FUSE daemon uses the ``FUSE_DEV_IOC_BACKING_OPEN`` ioctl
     on its connection file descriptor (e.g., ``/dev/fuse``) to register a
     backing file descriptor and obtain a ``backing_id``.
  4. When handling an ``OPEN`` or ``CREATE`` request for a FUSE file, the daemon
     replies with the ``FOPEN_PASSTHROUGH`` flag set in
     ``fuse_open_out::open_flags`` and provides the corresponding ``backing_id``
     in ``fuse_open_out::backing_id``.
  5. The FUSE daemon should eventually call ``FUSE_DEV_IOC_BACKING_CLOSE`` with
     the ``backing_id`` to release the kernel's reference to the backing file
     when it's no longer needed for passthrough setups.

Privilege Requirements
======================

Setting up passthrough functionality currently requires the FUSE daemon to
possess the ``CAP_SYS_ADMIN`` capability. This requirement stems from several
security and resource management considerations that are actively being
discussed and worked on. The primary reasons for this restriction are detailed
below.

Resource Accounting and Visibility
----------------------------------

The core mechanism for passthrough involves the FUSE daemon opening a file
descriptor to a backing file and registering it with the FUSE kernel module via
the ``FUSE_DEV_IOC_BACKING_OPEN`` ioctl. This ioctl returns a ``backing_id``
associated with a kernel-internal ``struct fuse_backing`` object, which holds a
reference to the backing ``struct file``.

A significant concern arises because the FUSE daemon can close its own file
descriptor to the backing file after registration. The kernel, however, will
still hold a reference to the ``struct file`` via the ``struct fuse_backing``
object as long as it's associated with a ``backing_id`` (or subsequently, with
an open FUSE file in passthrough mode).

This behavior leads to two main issues for unprivileged FUSE daemons:

  1. **Invisibility to lsof and other inspection tools**: Once the FUSE
     daemon closes its file descriptor, the open backing file held by the kernel
     becomes "hidden." Standard tools like ``lsof``, which typically inspect
     process file descriptor tables, would not be able to identify that this
     file is still open by the system on behalf of the FUSE filesystem. This
     makes it difficult for system administrators to track resource usage or
     debug issues related to open files (e.g., preventing unmounts).

  2. **Bypassing RLIMIT_NOFILE**: The FUSE daemon process is subject to
     resource limits, including the maximum number of open file descriptors
     (``RLIMIT_NOFILE``). If an unprivileged daemon could register backing files
     and then close its own FDs, it could potentially cause the kernel to hold
     an unlimited number of open ``struct file`` references without these being
     accounted against the daemon's ``RLIMIT_NOFILE``. This could lead to a
     denial-of-service (DoS) by exhausting system-wide file resources.

The ``CAP_SYS_ADMIN`` requirement acts as a safeguard against these issues,
restricting this powerful capability to trusted processes.

**NOTE**: ``io_uring`` solves this similar issue by exposing its "fixed files",
which are visible via ``fdinfo`` and accounted under the registering user's
``RLIMIT_NOFILE``.

Filesystem Stacking and Shutdown Loops
--------------------------------------

Another concern relates to the potential for creating complex and problematic
filesystem stacking scenarios if unprivileged users could set up passthrough.
A FUSE passthrough filesystem might use a backing file that resides:

  * On the *same* FUSE filesystem.
  * On another filesystem (like OverlayFS) which itself might have an upper or
    lower layer that is a FUSE filesystem.

These configurations could create dependency loops, particularly during
filesystem shutdown or unmount sequences, leading to deadlocks or system
instability. This is conceptually similar to the risks associated with the
``LOOP_SET_FD`` ioctl, which also requires ``CAP_SYS_ADMIN``.

To mitigate this, FUSE passthrough already incorporates checks based on
filesystem stacking depth (``sb->s_stack_depth`` and ``fc->max_stack_depth``).
For example, during the ``FUSE_INIT`` handshake, the FUSE daemon can negotiate
the ``max_stack_depth`` it supports. When a backing file is registered via
``FUSE_DEV_IOC_BACKING_OPEN``, the kernel checks if the backing file's
filesystem stack depth is within the allowed limit.

The ``CAP_SYS_ADMIN`` requirement provides an additional layer of security,
ensuring that only privileged users can create these potentially complex
stacking arrangements.

General Security Posture
------------------------

As a general principle for new kernel features that allow userspace to instruct
the kernel to perform direct operations on its behalf based on user-provided
file descriptors, starting with a higher privilege requirement (like
``CAP_SYS_ADMIN``) is a conservative and common security practice. This allows
the feature to be used and tested while further security implications are
evaluated and addressed.
