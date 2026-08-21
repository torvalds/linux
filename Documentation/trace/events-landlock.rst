.. SPDX-License-Identifier: GPL-2.0
.. Copyright © 2026 Cloudflare, Inc.

=====================
Landlock Trace Events
=====================

:Author: Mickaël Salaün
:Date: August 2026

Landlock emits trace events for sandbox lifecycle operations and access
denials.  These events can be consumed by ftrace (for human-readable
trace output and filtering) and by eBPF programs (for programmatic
introspection via BTF).

User space documentation can be found here:
Documentation/userspace-api/landlock.rst

.. warning::

   Landlock trace events, like audit records, expose sensitive
   information about all sandboxed processes on the system.  See
   :ref:`landlock_observability_security` for security considerations
   and privilege requirements.

Event overview
==============

Landlock trace events are organized in four categories:

**Syscall events** are emitted during Landlock system calls:

- ``landlock_create_ruleset``: a new ruleset is created
- ``landlock_add_rule_fs``: a filesystem rule is added to a ruleset
- ``landlock_add_rule_net``: a network port rule is added to a ruleset
- ``landlock_create_domain``: a new domain is created from a ruleset
- ``landlock_enforce_domain``: a domain is enforced on a thread

**Denial events** are emitted when an access is denied:

- ``landlock_deny_access_fs``: filesystem access denied
- ``landlock_deny_access_net``: network access denied
- ``landlock_deny_ptrace``: ptrace access denied
- ``landlock_deny_scope_signal``: signal delivery denied
- ``landlock_deny_scope_abstract_unix_socket``: abstract unix socket
  access denied

**Rule evaluation events** are emitted during rule matching:

- ``landlock_check_rule_fs``: a filesystem rule is evaluated
- ``landlock_check_rule_net``: a network port rule is evaluated

**Lifecycle events**:

- ``landlock_free_domain``: a domain is freed
- ``landlock_free_ruleset``: a ruleset is freed

Enabling events
===============

Enable all Landlock events::

    echo 1 > /sys/kernel/tracing/events/landlock/enable

Enable a specific event::

    echo 1 > /sys/kernel/tracing/events/landlock/landlock_deny_access_fs/enable

Read the trace output::

    cat /sys/kernel/tracing/trace_pipe

Event samples
=============

A fully unprivileged program is sandboxed so that it can still run (its
binary and shared libraries stay readable) and write only ``/tmp``, then
it is denied reading ``/etc/passwd``, which lies outside its read-only
set.  ``/etc/passwd`` is world-readable, so the denial comes solely from
Landlock, not from regular file permissions::

  $ cd /sys/kernel/tracing/events/landlock/
  $ echo 1 | tee landlock_{create_ruleset,create_domain,enforce_domain,deny_access_fs,free_domain}/enable >/dev/null
  $ LC_ALL=C LL_FS_RO=/usr:/lib:/lib64:/bin:/etc/ld.so.cache LL_FS_RW=/tmp \
      ./sandboxer cat /etc/passwd
  $ cat /sys/kernel/tracing/trace_pipe
  cat-127 [...] landlock_create_ruleset: ruleset=195cc6b76.0 handled_fs=execute|write_file|read_file|read_dir|remove_dir|remove_file|make_char|make_dir|make_reg|make_sock|make_fifo|make_block|make_sym|refer|truncate|ioctl_dev|resolve_unix handled_net= scoped=
  cat-127 [...] landlock_create_domain: domain=195cc6b7c parent=0 ruleset=195cc6b76.6
  cat-127 [...] landlock_enforce_domain: domain=195cc6b7c complete=1 process_wide=1 no_new_privs=1
  cat-127 [...] landlock_deny_access_fs: domain=195cc6b7c same_exec=0 logged=0 blockers=read_file dev=0:17 ino=5901179 path=/etc/passwd
  kworker/0:1-11 [...] landlock_free_domain: domain=195cc6b7c denials=1

The ``[...]`` replaces the ftrace CPU, flags, and timestamp columns.  The
first four events share the ``cat`` command name and PID because the
sandboxer replaces itself with ``cat`` via ``execve()`` before the
denial, and ftrace resolves a recorded PID to its latest command name.
``landlock_free_domain`` fires later from a kworker thread, so it carries
that thread's name instead.

Here ``logged=0`` shows that audit would not record this cross-execution
denial under the default flags, yet the ``deny_access_fs`` event still
appears.

Differences from audit records
==============================

Tracepoints and audit records both log Landlock denials, but differ
in some field formats:

- **Paths**: Most filesystem tracepoints resolve the path with
  ``d_absolute_path()`` (namespace-independent absolute paths), while
  mount-topology denials that carry only a dentry use ``dentry_path_raw()``.
  Audit uses ``d_path()`` (relative to the process's chroot).  A resolution
  failure is reported as ``<no_mem>``, ``<too_long>``, or ``<unreachable>``.
  Path-based tracepoint output is deterministic regardless of the tracer's
  mount namespace.

- **Device names**: Tracepoints use numeric ``dev=<major>:<minor>``.
  Audit uses string ``dev="<s_id>"``.  Numeric format is more precise
  for machine parsing.

- **Denied access field**: The ``deny_access_fs`` and ``deny_access_net``
  tracepoints use the ``blockers=`` field name (same as audit).  Both
  render the blocked access rights as names: audit prefixes the category
  and separates with commas (e.g., ``blockers=fs.read_file``), while the
  tracepoints omit the category (carried by the event name) and separate
  with ``|`` (e.g., ``blockers=read_file``).  Scope and ptrace
  tracepoints omit ``blockers`` because the event name identifies the
  denial type.

- **Scope and ptrace target names**: Tracepoints use role-specific field
  names (``tracee_pid``, ``target_pid``, ``peer_pid``) that reflect the
  semantic of each event.  Audit uses generic names (``opid``, ``ocomm``)
  because the audit log format is not event-type-specific.

- **Process name**: The ptrace and signal denial tracepoints include the
  role-prefixed ``tracee_comm=`` and ``target_comm=`` labels in the
  printk output for stateless consumers (each matches its sibling
  ``tracee_pid=``/``target_pid=`` field).  eBPF consumers can read
  ``comm`` directly from the task_struct via BTF.  The ``comm`` value is
  treated as untrusted input and escaped in the trace text output so it
  cannot inject field separators or control characters.

- **Other party's domain**: A scope or ptrace denial compares the
  subject's denying domain (``domain=``, always the enforcing domain and
  never the current task) with the other party's domain, so these
  tracepoints also report the other party's domain as a scalar ID:
  ``tracee_domain=`` (ptrace), ``target_domain=`` (signal), and
  ``peer_domain=`` (abstract unix socket).  It is ``0`` when the other
  party is unsandboxed, and otherwise a domain ID that a consumer resolves
  against the ``landlock_create_ruleset`` and ``landlock_create_domain``
  events it recorded.  Because a scope or ptrace verdict is decided by
  comparing the two domains, resolving both the subject ``domain=`` and
  this other-party ID against those lifecycle events lets a consumer
  verify or reproduce the verdict by redoing the same two-domain
  comparison, rather than only noting which boundary was crossed.  Audit
  records do not carry the other party's domain.

Ruleset versioning
==================

Syscall events include a ruleset version (``ruleset=<hex_id>.<version>``)
that tracks the number of rules added to the ruleset.  The version is
incremented on each ``landlock_add_rule()`` call and frozen at
``landlock_restrict_self()`` time.  This enables trace consumers to
correlate a domain with the exact set of rules it was created from.

Domain enforcement
==================

The whole-process-enforced guarantee (``complete=1 && process_wide=1``)
is the observable outcome of a successful
``landlock_restrict_self(..., LANDLOCK_RESTRICT_SELF_TSYNC)``; see the
thread synchronization section of
Documentation/userspace-api/landlock.rst.

The Landlock events and the generic syscall tracepoints are
complementary: the Landlock events expose the *semantic effect* of an
operation (the domain, its scope, the resulting ``no_new_privs`` state),
while ``raw_syscalls:sys_enter``/``sys_exit`` (or the per-syscall
``syscalls:sys_{enter,exit}_landlock_*`` under
``CONFIG_FTRACE_SYSCALLS``) expose the *raw API* -- the exact
``landlock_restrict_self()`` flags, arguments, and return value.
Correlate them by thread; a ``LANDLOCK_RESTRICT_SELF_TSYNC`` operation
also enforces the domain on the sibling threads, whose
``landlock_enforce_domain`` events fire in each sibling's own context
rather than the caller's, so correlate those to the syscall by domain ID.

Interpreting check_rule events
==============================

The ``check_rule_fs`` and ``check_rule_net`` events expose the per-layer
rule evaluation, which is useful for understanding *why* a specific
access is allowed or denied.

.. warning::

   These events fire on the access-check hot path, once per matching rule
   per check.  On a busy sandboxed workload this can be very high
   frequency.  Enable them only for targeted debugging, ideally combined
   with an ftrace filter (for example on ``ino`` or ``domain_id``), and
   expect tracing overhead while they are enabled.

Two output fields carry the evaluation:

- ``access_request=`` is the set of access rights being evaluated against the
  rule, rendered as ``|``-separated names.  For most checks this is the
  access the operation requested.  For filesystem ``rename`` and ``link``
  double-checks it is the domain's full handled mask, because those
  operations re-evaluate every handled right.

- ``grants=`` is a per-layer breakdown of the requested rights that this
  rule grants, in the form ``{<layer>,<layer>,...}``:

  - The braces wrap one comma-separated group per domain layer, ordered
    from the outermost (least nested) sandbox layer to the innermost.
  - Each group lists the requested rights the rule grants at that layer,
    joined by ``|``.
  - An empty group (for example the middle layer in
    ``{read_file,,read_file}``) means the rule grants none of the
    requested rights at that layer.

A Landlock domain allows an access only when, for every requested right,
every layer that handles that right has at least one matching rule
granting it.  A single ``check_rule`` event therefore shows one rule's
contribution, not the final decision:

- If a right appears in every layer's group, this rule alone is
  sufficient to allow that right.
- If a right is missing from some layer's group, that layer must grant it
  through another matching rule, or the right is denied and appears in the
  ``blockers=`` field of the corresponding ``deny_access`` event.

To reconstruct the decision for an object, aggregate the ``grants=``
groups of all ``check_rule`` events emitted for that object during the
check.

.. note::

   Because a verdict requires aggregating ``grants=`` across all matching
   rules of one access check, a stateless ftrace filter on a single
   ``check_rule`` event cannot distinguish an allowed access from a
   denied one.

For example, a program sandboxed with read and execute access to the
whole filesystem reads ``/etc/passwd``; both the ``execve()`` and the
read match the rule covering ``/`` (inode 2), so ``check_rule_fs`` fires
with the requested rights intersected against what that rule grants.
The ``access_request=`` mask includes ``truncate`` because the file-open hook
evaluates that optional right alongside the required access, but the
rule does not grant it, so ``truncate`` never appears in ``grants=``::

  cat-127 [...] landlock_check_rule_fs: domain=1e40cb56f access_request=execute|read_file|truncate dev=0:17 ino=2 grants={execute|read_file}
  cat-127 [...] landlock_check_rule_fs: domain=1e40cb56f access_request=read_file|truncate dev=0:17 ino=2 grants={read_file}

The ``[...]`` replaces the ftrace CPU, flags, and timestamp columns.  A
single ``grants=`` group means the enforcing domain has one layer.  With
two nested sandboxes that each grant the same rights, the rule spans both
layers, so ``grants=`` has one group per layer::

  cat-128 [...] landlock_check_rule_fs: domain=184788b52 access_request=execute|read_file|truncate dev=0:17 ino=2 grants={execute|read_file,execute|read_file}
  cat-128 [...] landlock_check_rule_fs: domain=184788b52 access_request=read_file|truncate dev=0:17 ino=2 grants={read_file,read_file}

eBPF access
===========

eBPF programs attached via ``BPF_RAW_TRACEPOINT`` can access the
tracepoint arguments directly through BTF.  The arguments include both
standard kernel objects and Landlock-internal objects:

- Standard kernel objects (``struct task_struct``, ``struct sock``,
  ``struct path``, ``struct dentry``) can be used with existing BPF
  helpers.
- Landlock-internal objects (``struct landlock_domain``,
  ``struct landlock_ruleset``, ``struct landlock_rule``,
  ``struct landlock_hierarchy``) can be read via ``BPF_CORE_READ``.
  Internal struct layouts may change between kernel versions; use CO-RE
  for field relocation.

A stateful eBPF program observes the full event stream and maintains
per-domain state in BPF maps:

1. On ``landlock_create_domain``: record the domain ID and parent (the
   per-domain Landlock log flags are not event fields; read them from
   ``struct landlock_hierarchy`` via BTF if needed).
2. On ``landlock_enforce_domain``: record the sandboxed thread under the
   ``domain=`` key (join to the ``create_domain`` recorded in step 1),
   building the per-domain thread set; filter ``complete==1`` for a
   one-event-per-operation summary.
3. On ``landlock_deny_access_*``: look up the domain, decide whether
   to count, alert, or ignore the denial based on custom policy.
4. On ``landlock_free_domain``: clean up the per-domain state, log
   final statistics.

This approach requires no kernel modification and no Landlock-specific
BPF helpers.  The Landlock IDs serve as correlation keys across events.

Audit filtering equivalence
===========================

The ``logged`` field reflects the domain's log policy but not the global
``audit_enabled`` toggle, so it does not change when audit is turned on
or off.  When audit is enabled, ``logged==1`` selects the denials the
domain submits to audit (audit-side rate-limiting and exclude rules may
still drop some), so a stateless ftrace filter can select them::

    # Show only denials that audit would also log:
    echo 'logged==1' > \
        /sys/kernel/tracing/events/landlock/landlock_deny_access_fs/filter

Event reference
===============

.. kernel-doc:: include/trace/events/landlock.h
    :doc: Landlock trace events

.. kernel-doc:: include/trace/events/landlock.h
    :internal:

Additional documentation
========================

* Documentation/userspace-api/landlock.rst
* Documentation/admin-guide/LSM/landlock.rst
* Documentation/security/landlock.rst
* https://landlock.io
