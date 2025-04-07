.. SPDX-License-Identifier: GPL-2.0
.. Copyright © 2025 Microsoft Corporation

================================
Landlock: system-wide management
================================

:Author: Mickaël Salaün
:Date: March 2025

Landlock can leverage the audit framework to log events.

User space documentation can be found here:
Documentation/userspace-api/landlock.rst.

Audit
=====

Denied access requests are logged by default for a sandboxed program if `audit`
is enabled.  This default behavior can be changed with the
sys_landlock_restrict_self() flags (cf.
Documentation/userspace-api/landlock.rst).  Landlock logs can also be masked
thanks to audit rules.  Landlock can generate 2 audit record types.

Record types
------------

AUDIT_LANDLOCK_ACCESS
    This record type identifies a denied access request to a kernel resource.
    The ``domain`` field indicates the ID of the domain that blocked the
    request.  The ``blockers`` field indicates the cause(s) of this denial
    (separated by a comma), and the following fields identify the kernel object
    (similar to SELinux).  There may be more than one of this record type per
    audit event.

    Example with a file link request generating two records in the same event::

        domain=195ba459b blockers=fs.refer path="/usr/bin" dev="vda2" ino=351
        domain=195ba459b blockers=fs.make_reg,fs.refer path="/usr/local" dev="vda2" ino=365

AUDIT_LANDLOCK_DOMAIN
    This record type describes the status of a Landlock domain.  The ``status``
    field can be either ``allocated`` or ``deallocated``.

    The ``allocated`` status is part of the same audit event and follows
    the first logged ``AUDIT_LANDLOCK_ACCESS`` record of a domain.  It identifies
    Landlock domain information at the time of the sys_landlock_restrict_self()
    call with the following fields:

    - the ``domain`` ID
    - the enforcement ``mode``
    - the domain creator's ``pid``
    - the domain creator's ``uid``
    - the domain creator's executable path (``exe``)
    - the domain creator's command line (``comm``)

    Example::

        domain=195ba459b status=allocated mode=enforcing pid=300 uid=0 exe="/root/sandboxer" comm="sandboxer"

    The ``deallocated`` status is an event on its own and it identifies a
    Landlock domain release.  After such event, it is guarantee that the
    related domain ID will never be reused during the lifetime of the system.
    The ``domain`` field indicates the ID of the domain which is released, and
    the ``denials`` field indicates the total number of denied access request,
    which might not have been logged according to the audit rules and
    sys_landlock_restrict_self()'s flags.

    Example::

        domain=195ba459b status=deallocated denials=3


Event samples
--------------

Here are two examples of log events (see serial numbers).

In this example a sandboxed program (``kill``) tries to send a signal to the
init process, which is denied because of the signal scoping restriction
(``LL_SCOPED=s``)::

  $ LL_FS_RO=/ LL_FS_RW=/ LL_SCOPED=s LL_FORCE_LOG=1 ./sandboxer kill 1

This command generates two events, each identified with a unique serial
number following a timestamp (``msg=audit(1729738800.268:30)``).  The first
event (serial ``30``) contains 4 records.  The first record
(``type=LANDLOCK_ACCESS``) shows an access denied by the domain `1a6fdc66f`.
The cause of this denial is signal scopping restriction
(``blockers=scope.signal``).  The process that would have receive this signal
is the init process (``opid=1 ocomm="systemd"``).

The second record (``type=LANDLOCK_DOMAIN``) describes (``status=allocated``)
domain `1a6fdc66f`.  This domain was created by process ``286`` executing the
``/root/sandboxer`` program launched by the root user.

The third record (``type=SYSCALL``) describes the syscall, its provided
arguments, its result (``success=no exit=-1``), and the process that called it.

The fourth record (``type=PROCTITLE``) shows the command's name as an
hexadecimal value.  This can be translated with ``python -c
'print(bytes.fromhex("6B696C6C0031"))'``.

Finally, the last record (``type=LANDLOCK_DOMAIN``) is also the only one from
the second event (serial ``31``).  It is not tied to a direct user space action
but an asynchronous one to free resources tied to a Landlock domain
(``status=deallocated``).  This can be useful to know that the following logs
will not concern the domain ``1a6fdc66f`` anymore.  This record also summarize
the number of requests this domain denied (``denials=1``), whether they were
logged or not.

.. code-block::

  type=LANDLOCK_ACCESS msg=audit(1729738800.268:30): domain=1a6fdc66f blockers=scope.signal opid=1 ocomm="systemd"
  type=LANDLOCK_DOMAIN msg=audit(1729738800.268:30): domain=1a6fdc66f status=allocated mode=enforcing pid=286 uid=0 exe="/root/sandboxer" comm="sandboxer"
  type=SYSCALL msg=audit(1729738800.268:30): arch=c000003e syscall=62 success=no exit=-1 [..] ppid=272 pid=286 auid=0 uid=0 gid=0 [...] comm="kill" [...]
  type=PROCTITLE msg=audit(1729738800.268:30): proctitle=6B696C6C0031
  type=LANDLOCK_DOMAIN msg=audit(1729738800.324:31): domain=1a6fdc66f status=deallocated denials=1

Here is another example showcasing filesystem access control::

  $ LL_FS_RO=/ LL_FS_RW=/tmp LL_FORCE_LOG=1 ./sandboxer sh -c "echo > /etc/passwd"

The related audit logs contains 8 records from 3 different events (serials 33,
34 and 35) created by the same domain `1a6fdc679`::

  type=LANDLOCK_ACCESS msg=audit(1729738800.221:33): domain=1a6fdc679 blockers=fs.write_file path="/dev/tty" dev="devtmpfs" ino=9
  type=LANDLOCK_DOMAIN msg=audit(1729738800.221:33): domain=1a6fdc679 status=allocated mode=enforcing pid=289 uid=0 exe="/root/sandboxer" comm="sandboxer"
  type=SYSCALL msg=audit(1729738800.221:33): arch=c000003e syscall=257 success=no exit=-13 [...] ppid=272 pid=289 auid=0 uid=0 gid=0 [...] comm="sh" [...]
  type=PROCTITLE msg=audit(1729738800.221:33): proctitle=7368002D63006563686F203E202F6574632F706173737764
  type=LANDLOCK_ACCESS msg=audit(1729738800.221:34): domain=1a6fdc679 blockers=fs.write_file path="/etc/passwd" dev="vda2" ino=143821
  type=SYSCALL msg=audit(1729738800.221:34): arch=c000003e syscall=257 success=no exit=-13 [...] ppid=272 pid=289 auid=0 uid=0 gid=0 [...] comm="sh" [...]
  type=PROCTITLE msg=audit(1729738800.221:34): proctitle=7368002D63006563686F203E202F6574632F706173737764
  type=LANDLOCK_DOMAIN msg=audit(1729738800.261:35): domain=1a6fdc679 status=deallocated denials=2


Event filtering
---------------

If you get spammed with audit logs related to Landlock, this is either an
attack attempt or a bug in the security policy.  We can put in place some
filters to limit noise with two complementary ways:

- with sys_landlock_restrict_self()'s flags if we can fix the sandboxed
  programs,
- or with audit rules (see :manpage:`auditctl(8)`).

Additional documentation
========================

* `Linux Audit Documentation`_
* Documentation/userspace-api/landlock.rst
* Documentation/security/landlock.rst
* https://landlock.io

.. Links
.. _Linux Audit Documentation:
   https://github.com/linux-audit/audit-documentation/wiki
