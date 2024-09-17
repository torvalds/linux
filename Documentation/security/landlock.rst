.. SPDX-License-Identifier: GPL-2.0
.. Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
.. Copyright © 2019-2020 ANSSI

==================================
Landlock LSM: kernel documentation
==================================

:Author: Mickaël Salaün
:Date: September 2022

Landlock's goal is to create scoped access-control (i.e. sandboxing).  To
harden a whole system, this feature should be available to any process,
including unprivileged ones.  Because such process may be compromised or
backdoored (i.e. untrusted), Landlock's features must be safe to use from the
kernel and other processes point of view.  Landlock's interface must therefore
expose a minimal attack surface.

Landlock is designed to be usable by unprivileged processes while following the
system security policy enforced by other access control mechanisms (e.g. DAC,
LSM).  Indeed, a Landlock rule shall not interfere with other access-controls
enforced on the system, only add more restrictions.

Any user can enforce Landlock rulesets on their processes.  They are merged and
evaluated according to the inherited ones in a way that ensures that only more
constraints can be added.

User space documentation can be found here:
Documentation/userspace-api/landlock.rst.

Guiding principles for safe access controls
===========================================

* A Landlock rule shall be focused on access control on kernel objects instead
  of syscall filtering (i.e. syscall arguments), which is the purpose of
  seccomp-bpf.
* To avoid multiple kinds of side-channel attacks (e.g. leak of security
  policies, CPU-based attacks), Landlock rules shall not be able to
  programmatically communicate with user space.
* Kernel access check shall not slow down access request from unsandboxed
  processes.
* Computation related to Landlock operations (e.g. enforcing a ruleset) shall
  only impact the processes requesting them.

Design choices
==============

Filesystem access rights
------------------------

All access rights are tied to an inode and what can be accessed through it.
Reading the content of a directory does not imply to be allowed to read the
content of a listed inode.  Indeed, a file name is local to its parent
directory, and an inode can be referenced by multiple file names thanks to
(hard) links.  Being able to unlink a file only has a direct impact on the
directory, not the unlinked inode.  This is the reason why
``LANDLOCK_ACCESS_FS_REMOVE_FILE`` or ``LANDLOCK_ACCESS_FS_REFER`` are not
allowed to be tied to files but only to directories.

Tests
=====

Userspace tests for backward compatibility, ptrace restrictions and filesystem
support can be found here: `tools/testing/selftests/landlock/`_.

Kernel structures
=================

Object
------

.. kernel-doc:: security/landlock/object.h
    :identifiers:

Filesystem
----------

.. kernel-doc:: security/landlock/fs.h
    :identifiers:

Ruleset and domain
------------------

A domain is a read-only ruleset tied to a set of subjects (i.e. tasks'
credentials).  Each time a ruleset is enforced on a task, the current domain is
duplicated and the ruleset is imported as a new layer of rules in the new
domain.  Indeed, once in a domain, each rule is tied to a layer level.  To
grant access to an object, at least one rule of each layer must allow the
requested action on the object.  A task can then only transit to a new domain
that is the intersection of the constraints from the current domain and those
of a ruleset provided by the task.

The definition of a subject is implicit for a task sandboxing itself, which
makes the reasoning much easier and helps avoid pitfalls.

.. kernel-doc:: security/landlock/ruleset.h
    :identifiers:

.. Links
.. _tools/testing/selftests/landlock/:
   https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/tools/testing/selftests/landlock/
