.. SPDX-License-Identifier: GPL-2.0
.. Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
.. Copyright © 2019-2020 ANSSI
.. Copyright © 2026 Cloudflare, Inc.

==================================
Landlock LSM: kernel documentation
==================================

:Author: Mickaël Salaün
:Date: October 2026

Landlock's goal is to create scoped access-control (i.e. sandboxing).  To
harden a whole system, this feature should be available to any process,
including unprivileged ones.  Because such a process may be compromised or
backdoored (i.e. untrusted), Landlock's features must be safe to use from the
kernel and other processes point of view.  Landlock's interface must therefore
expose a minimal attack surface.

Landlock is designed to be usable by unprivileged processes while following the
system security policy enforced by other access control mechanisms (e.g. DAC,
LSM).  A Landlock rule shall not interfere with other access-controls enforced
on the system, only add more restrictions.

Any user can enforce Landlock rulesets on their processes.  They are merged and
evaluated against inherited rulesets in a way that ensures that only more
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
* Resources (e.g. file descriptors) directly obtained from the kernel by a
  sandboxed process shall retain their scoped accesses (at the time of resource
  acquisition) whatever process uses them.
  Cf. `File descriptor access rights`_.
* Access denials shall be logged according to system and Landlock domain
  configurations.  Log entries must contain information about the cause of the
  denial and the owner of the related security policy.  Such log generation
  should have a negligible performance and memory impact on allowed requests.

Design choices
==============

Inode access rights
-------------------

All access rights are tied to an inode and what can be accessed through it.
Reading the content of a directory does not imply to be allowed to read the
content of a listed inode.  Indeed, a file name is local to its parent
directory, and an inode can be referenced by multiple file names thanks to
(hard) links.  Being able to unlink a file only has a direct impact on the
directory, not the unlinked inode.  This is the reason why
``LANDLOCK_ACCESS_FS_REMOVE_FILE`` or ``LANDLOCK_ACCESS_FS_REFER`` are not
allowed to be tied to files but only to directories.

File descriptor access rights
-----------------------------

Access rights are checked and tied to file descriptors at open time.  The
underlying principle is that equivalent sequences of operations should lead to
the same results, when they are executed under the same Landlock domain.

Taking the ``LANDLOCK_ACCESS_FS_TRUNCATE`` right as an example, it may be
allowed to open a file for writing without being allowed to
:manpage:`ftruncate` the resulting file descriptor if the related file
hierarchy doesn't grant that access right.  The following sequences of
operations have the same semantic and should then have the same result:

* ``truncate(path);``
* ``int fd = open(path, O_WRONLY); ftruncate(fd); close(fd);``

Similarly to file access modes (e.g. ``O_RDWR``), Landlock access rights
attached to file descriptors are retained even if they are passed between
processes (e.g. through a Unix domain socket).  Such access rights will then be
enforced even if the receiving process is not sandboxed by Landlock.  Indeed,
this is required to keep access controls consistent over the whole system, and
this avoids unattended bypasses through file descriptor passing (i.e. confused
deputy attack).

.. _scoped-flags-interaction:

Interaction between scoped flags and other access rights
--------------------------------------------------------

The ``scoped`` flags in &struct landlock_ruleset_attr restrict the
use of *outgoing* IPC from the created Landlock domain, while they
permit reaching out to IPC endpoints *within* the created Landlock
domain.

In the future, scoped flags *may* interact with other access rights,
e.g. so that abstract UNIX sockets can be allow-listed by name, or so
that signals can be allow-listed by signal number or target process.

When introducing ``LANDLOCK_ACCESS_FS_RESOLVE_UNIX``, we defined it to
implicitly have the same scoping semantics as a
``LANDLOCK_SCOPE_PATHNAME_UNIX_SOCKET`` flag would have: connecting to
UNIX sockets within the same domain (where
``LANDLOCK_ACCESS_FS_RESOLVE_UNIX`` is used) is unconditionally
allowed.

The reasoning is:

* Like other IPC mechanisms, connecting to named UNIX sockets in the
  same domain should be expected and harmless.  (If needed, users can
  further refine their Landlock policies with nested domains or by
  restricting ``LANDLOCK_ACCESS_FS_MAKE_SOCK``.)
* We reserve the option to still introduce
  ``LANDLOCK_SCOPE_PATHNAME_UNIX_SOCKET`` in the future.  (This would
  be useful if we wanted to have a Landlock rule to permit IPC access
  to other Landlock domains.)
* But we can postpone the point in time when users have to deal with
  two interacting flags visible in the userspace API.  (In particular,
  it is possible that it won't be needed in practice, in which case we
  can avoid the second flag altogether.)
* If we *do* introduce ``LANDLOCK_SCOPE_PATHNAME_UNIX_SOCKET`` in the
  future, setting this scoped flag in a ruleset does *not reduce* the
  restrictions, because access within the same scope is already
  allowed based on ``LANDLOCK_ACCESS_FS_RESOLVE_UNIX``.

Composability with user namespaces
----------------------------------

Landlock domain-based scoping and the kernel's user-namespace-based capability
scoping enforce isolation over independent hierarchies.  Landlock checks domain
ancestry; the kernel's ``ns_capable()`` checks user namespace ancestry.  These
hierarchies are orthogonal: Landlock enforcement is deterministic with respect
to its own configuration, regardless of namespace or capability state, and vice
versa.  This orthogonality is a design invariant that must hold for all Landlock
access controls.

Design philosophy
-----------------

Landlock's goal is to restrict a sandboxed process's access to three kinds of
resources: data (files, sockets, pipes), other processes (signals, ptrace), and
kernel-internal resources whose use widens the kernel attack surface
(capabilities, namespace types).  Each access right or permission gates one or
more operations that grant such access; restricting the operations is how
Landlock restricts the underlying access.

When designing a new access control, identify the protected resource kind
first (data, processes, or kernel-internal resources).  The operations to
restrict follow from the protected resource, by identifying which kernel code
paths grant access to the resource and at which place in the code the access to
the resource can be gated.  Do not design a permission around
"restrict the unshare(2) syscall" or similar mechanism-centric framings; design
it around "restrict the process from acquiring access to namespace types" (the
protected resource), letting the operation set follow.

Ruleset restriction models
--------------------------

Landlock provides three restriction models that differ in how rules identify the
resource being restricted.

In general, the ``struct landlock_ruleset_attr`` specifies the operations to be
denied by default under the enforced policy.  The *rules* added to the ruleset
define the exceptions to these restrictions, allow-listing specific conditions
under which these operations are still permitted.

Per-object access rights (``handled_access_*``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Per-object access rights control operations on a specific resource instance,
identified in the rule key by a value drawn from an open-ended space: a file
hierarchy referenced by ``parent_fd``, or a network port identified by its
16-bit number.

Each ``handled_access_*`` field declares a set of access rights, operations
which are to be denied by default once the ruleset is enforced.

The rule body declares which of the multiple distinct operations on that object
instance are allowed (open, read, write, truncate; bind, connect).

Operations are grouped by object type in the respective ``handled_access_*``
field: a new operation on an existing type extends that field, and a new object
type gets its own ``handled_access_*`` field.

Per-category permissions (``handled_permissions``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Per-category permissions control the process's exercise of category members,
where the category is a small kernel-defined enumeration (a Linux capability
number ``CAP_*``, a namespace type ``CLONE_NEW*``).  Unlike per-object access
rights, which restrict specific operations on a single resource instance,
per-category permissions gate the prerequisite operation itself (exercising a
capability, acquiring access to a namespace), so gating it transitively covers a
broad set of downstream operations (e.g. denying ``CAP_SYS_ADMIN`` blocks all
admin operations such as mounts).

These category members are the LSM-level access-control objects (the entities
the process is authorized against) even though they are enum values rather than
externally-instantiated kernel data structures.  Per-category permissions apply
where the controlled operation collapses to "may the process use this category
member at all" (use a capability; acquire access to a namespace), so the rule
body lists which category members the process may exercise.

Each ``LANDLOCK_PERMISSION_*`` flag maps to its own rule type and covers every
kernel path that exercises a member.  When a ruleset handles a permission, all
uses of category members are denied unless explicitly allowed by a rule.

Logging of a denied member can be suppressed at the same granularity as the
restriction itself: per rule and per member.  A rule names the members whose
denial should not be audited; suppression only affects the audit record, never
the denial or its trace event, which reports ``logged=0``.  Suppression only
applies when the layer owning the rule is the one that denied the member.

See Documentation/userspace-api/landlock.rst for the concrete syscall paths
covered by each permission.

The category enum is owned by the corresponding kernel subsystem (capabilities,
namespaces, etc.).  Userspace policy authors query category member availability
via the relevant non-Landlock interfaces:

* For capabilities: ``<linux/capability.h>``,
  ``/proc/sys/kernel/cap_last_cap``, ``prctl(PR_CAPBSET_READ)``.
* For namespaces: ``<linux/sched.h>``, ``/proc/$$/ns/*``,
  :manpage:`unshare(2)` runtime probe.

The Landlock ABI version does not encode this availability; ABI versioning
describes which Landlock features (rule types, access rights, scopes,
permissions) the kernel implements, not which category members the kernel knows
about.

Forward compatibility for new category members follows a simple rule set:

* New members in future kernels are automatically denied: rules whitelist
  specific values, and a member not in any rule is denied.
* Kernel-side compatibility for split categories is handled by the owning
  subsystem (e.g., when ``CAP_BPF`` was split from ``CAP_SYS_ADMIN``, either
  capability became sufficient for the affected operations, so a rule allowing
  ``CAP_SYS_ADMIN`` continues to allow operations now gated by
  ``CAP_SYS_ADMIN || CAP_BPF``).
* Unknown values in the rule body are silently accepted rather than rejected.
  Rejecting them would tie Landlock policy semantics to the running kernel's
  category-member set: a rule built against future headers would fail to load
  on older kernels, forcing policy authors to know each kernel's enumeration.
  Acceptance is fail-safe in both directions: a rule referring to a value the
  running kernel does not yet know has no effect (deny-by-default still applies
  to that operation), and a rule written against future headers loads
  identically across kernels so the same policy keeps the same restrictions.
  When a value becomes real on a future kernel, the policy activates as written
  by the author.
* In contrast, unknown ``LANDLOCK_PERMISSION_*`` flags in
  ``handled_permissions`` are rejected (``-EINVAL``), since Landlock owns that
  bit space.

Cross-domain scopes (``scoped``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Scopes restrict **cross-domain interactions** categorically, without rules.
Setting a scope flag (e.g.  ``LANDLOCK_SCOPE_SIGNAL``) denies the operation to
targets outside the Landlock domain or its children.  Like per-category
permissions, scopes provide complete coverage of the controlled operation.

Choosing a model for a new feature
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* If the new feature controls operations on resource objects supplied by the
  sandbox author, extend or add a per-object access right
  (``handled_access_*``).
* If the new feature controls a per-category operation gated by an enum (a
  Linux capability, a namespace type, a socket family, etc.), use a
  per-category permission (``handled_permissions``).  When several such enums
  could classify the operation, prefer the enum the originating subsystem
  already uses for capability/access checks (e.g. ``CAP_*`` for ``capable()``
  hooks, ``CLONE_NEW*`` for namespace hooks).
* When an operation is gated by multiple kernel-defined enums (a classic
  example being ``CAP_SYS_ADMIN`` plus a ``CLONE_NEW*`` flag for non-user
  namespace creation), define one per-category permission per enum dimension.
  Sandbox authors handle each dimension's permission in ``handled_permissions``
  and add rules for each; the kernel enforces each dimension at its own LSM
  hook.  ``LANDLOCK_PERMISSION_NAMESPACE_USE`` and
  ``LANDLOCK_PERMISSION_CAPABILITY_USE`` follow this pattern.
* If the new feature restricts a categorical cross-domain interaction with no
  per-target granularity, use a cross-domain scope (``scoped``).
* For all three models, confirm a single LSM hook (or small set of related
  hooks) covers every kernel path that exercises the operation.

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

Namespace
---------

.. kernel-doc:: security/landlock/ns.h
    :identifiers:

Capability
----------

.. kernel-doc:: security/landlock/cap.h
    :identifiers:

Process credential
------------------

.. kernel-doc:: security/landlock/cred.h
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

.. kernel-doc:: security/landlock/domain.h
    :identifiers:

Denial logging
==============

Access denials are logged through two independent channels: audit
records and tracepoints.  Both are managed by the common denial
framework in ``log.c``, compiled under ``CONFIG_SECURITY_LANDLOCK_LOG``
(automatically selected by ``CONFIG_AUDIT`` or ``CONFIG_TRACEPOINTS``).

Audit records respect audit configuration, the domain's Landlock log
flags, and ``LANDLOCK_LOG_DISABLED``.  Tracepoints fire unconditionally,
independent of these settings.  The denial counter (``num_denials``) is
always incremented regardless of logging configuration.

Each denial tracepoint carries a ``logged`` field reporting the
audit-logging verdict: whether the denial would be written to the audit
log if audit were configured and active.  This verdict is the same
whether or not the kernel is built with audit support, so a
tracepoints-only build reports the selection audit would make.  A quiet
rule (``LANDLOCK_ADD_RULE_QUIET`` with the access in the ``quiet_*``
fields of ``struct landlock_ruleset_attr``) suppresses logging by
setting ``logged=0`` the same way, as do the per-member ``quiet_*``
masks of capability and namespace rules.

See Documentation/admin-guide/LSM/landlock.rst for audit record format,
tracepoint usage, and filtering examples.

.. kernel-doc:: security/landlock/log.h
    :identifiers:

Trace events
------------

See Documentation/trace/events-landlock.rst for trace event usage and format
details; the full event reference lives there and is not duplicated here.

Additional documentation
========================

* Documentation/userspace-api/landlock.rst
* Documentation/admin-guide/LSM/landlock.rst
* Documentation/trace/events-landlock.rst
* https://landlock.io

.. Links
.. _tools/testing/selftests/landlock/:
   https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/tools/testing/selftests/landlock/
