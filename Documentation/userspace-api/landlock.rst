.. SPDX-License-Identifier: GPL-2.0
.. Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
.. Copyright © 2019-2020 ANSSI
.. Copyright © 2021-2022 Microsoft Corporation

=====================================
Landlock: unprivileged access control
=====================================

:Author: Mickaël Salaün
:Date: April 2024

The goal of Landlock is to enable to restrict ambient rights (e.g. global
filesystem or network access) for a set of processes.  Because Landlock
is a stackable LSM, it makes possible to create safe security sandboxes as new
security layers in addition to the existing system-wide access-controls. This
kind of sandbox is expected to help mitigate the security impact of bugs or
unexpected/malicious behaviors in user space applications.  Landlock empowers
any process, including unprivileged ones, to securely restrict themselves.

We can quickly make sure that Landlock is enabled in the running system by
looking for "landlock: Up and running" in kernel logs (as root):
``dmesg | grep landlock || journalctl -kb -g landlock`` .
Developers can also easily check for Landlock support with a
:ref:`related system call <landlock_abi_versions>`.
If Landlock is not currently supported, we need to
:ref:`configure the kernel appropriately <kernel_support>`.

Landlock rules
==============

A Landlock rule describes an action on an object which the process intends to
perform.  A set of rules is aggregated in a ruleset, which can then restrict
the thread enforcing it, and its future children.

The two existing types of rules are:

Filesystem rules
    For these rules, the object is a file hierarchy,
    and the related filesystem actions are defined with
    `filesystem access rights`.

Network rules (since ABI v4)
    For these rules, the object is a TCP port,
    and the related actions are defined with `network access rights`.

Defining and enforcing a security policy
----------------------------------------

We first need to define the ruleset that will contain our rules.

For this example, the ruleset will contain rules that only allow filesystem
read actions and establish a specific TCP connection. Filesystem write
actions and other TCP actions will be denied.

The ruleset then needs to handle both these kinds of actions.  This is
required for backward and forward compatibility (i.e. the kernel and user
space may not know each other's supported restrictions), hence the need
to be explicit about the denied-by-default access rights.

.. code-block:: c

    struct landlock_ruleset_attr ruleset_attr = {
        .handled_access_fs =
            LANDLOCK_ACCESS_FS_EXECUTE |
            LANDLOCK_ACCESS_FS_WRITE_FILE |
            LANDLOCK_ACCESS_FS_READ_FILE |
            LANDLOCK_ACCESS_FS_READ_DIR |
            LANDLOCK_ACCESS_FS_REMOVE_DIR |
            LANDLOCK_ACCESS_FS_REMOVE_FILE |
            LANDLOCK_ACCESS_FS_MAKE_CHAR |
            LANDLOCK_ACCESS_FS_MAKE_DIR |
            LANDLOCK_ACCESS_FS_MAKE_REG |
            LANDLOCK_ACCESS_FS_MAKE_SOCK |
            LANDLOCK_ACCESS_FS_MAKE_FIFO |
            LANDLOCK_ACCESS_FS_MAKE_BLOCK |
            LANDLOCK_ACCESS_FS_MAKE_SYM |
            LANDLOCK_ACCESS_FS_REFER |
            LANDLOCK_ACCESS_FS_TRUNCATE |
            LANDLOCK_ACCESS_FS_IOCTL_DEV,
        .handled_access_net =
            LANDLOCK_ACCESS_NET_BIND_TCP |
            LANDLOCK_ACCESS_NET_CONNECT_TCP,
    };

Because we may not know on which kernel version an application will be
executed, it is safer to follow a best-effort security approach.  Indeed, we
should try to protect users as much as possible whatever the kernel they are
using.

To be compatible with older Linux versions, we detect the available Landlock ABI
version, and only use the available subset of access rights:

.. code-block:: c

    int abi;

    abi = landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 0) {
        /* Degrades gracefully if Landlock is not handled. */
        perror("The running kernel does not enable to use Landlock");
        return 0;
    }
    switch (abi) {
    case 1:
        /* Removes LANDLOCK_ACCESS_FS_REFER for ABI < 2 */
        ruleset_attr.handled_access_fs &= ~LANDLOCK_ACCESS_FS_REFER;
        __attribute__((fallthrough));
    case 2:
        /* Removes LANDLOCK_ACCESS_FS_TRUNCATE for ABI < 3 */
        ruleset_attr.handled_access_fs &= ~LANDLOCK_ACCESS_FS_TRUNCATE;
        __attribute__((fallthrough));
    case 3:
        /* Removes network support for ABI < 4 */
        ruleset_attr.handled_access_net &=
            ~(LANDLOCK_ACCESS_NET_BIND_TCP |
              LANDLOCK_ACCESS_NET_CONNECT_TCP);
        __attribute__((fallthrough));
    case 4:
        /* Removes LANDLOCK_ACCESS_FS_IOCTL_DEV for ABI < 5 */
        ruleset_attr.handled_access_fs &= ~LANDLOCK_ACCESS_FS_IOCTL_DEV;
    }

This enables to create an inclusive ruleset that will contain our rules.

.. code-block:: c

    int ruleset_fd;

    ruleset_fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
    if (ruleset_fd < 0) {
        perror("Failed to create a ruleset");
        return 1;
    }

We can now add a new rule to this ruleset thanks to the returned file
descriptor referring to this ruleset.  The rule will only allow reading the
file hierarchy ``/usr``.  Without another rule, write actions would then be
denied by the ruleset.  To add ``/usr`` to the ruleset, we open it with the
``O_PATH`` flag and fill the &struct landlock_path_beneath_attr with this file
descriptor.

.. code-block:: c

    int err;
    struct landlock_path_beneath_attr path_beneath = {
        .allowed_access =
            LANDLOCK_ACCESS_FS_EXECUTE |
            LANDLOCK_ACCESS_FS_READ_FILE |
            LANDLOCK_ACCESS_FS_READ_DIR,
    };

    path_beneath.parent_fd = open("/usr", O_PATH | O_CLOEXEC);
    if (path_beneath.parent_fd < 0) {
        perror("Failed to open file");
        close(ruleset_fd);
        return 1;
    }
    err = landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
                            &path_beneath, 0);
    close(path_beneath.parent_fd);
    if (err) {
        perror("Failed to update ruleset");
        close(ruleset_fd);
        return 1;
    }

It may also be required to create rules following the same logic as explained
for the ruleset creation, by filtering access rights according to the Landlock
ABI version.  In this example, this is not required because all of the requested
``allowed_access`` rights are already available in ABI 1.

For network access-control, we can add a set of rules that allow to use a port
number for a specific action: HTTPS connections.

.. code-block:: c

    struct landlock_net_port_attr net_port = {
        .allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
        .port = 443,
    };

    err = landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
                            &net_port, 0);

The next step is to restrict the current thread from gaining more privileges
(e.g. through a SUID binary).  We now have a ruleset with the first rule
allowing read access to ``/usr`` while denying all other handled accesses for
the filesystem, and a second rule allowing HTTPS connections.

.. code-block:: c

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        perror("Failed to restrict privileges");
        close(ruleset_fd);
        return 1;
    }

The current thread is now ready to sandbox itself with the ruleset.

.. code-block:: c

    if (landlock_restrict_self(ruleset_fd, 0)) {
        perror("Failed to enforce ruleset");
        close(ruleset_fd);
        return 1;
    }
    close(ruleset_fd);

If the ``landlock_restrict_self`` system call succeeds, the current thread is
now restricted and this policy will be enforced on all its subsequently created
children as well.  Once a thread is landlocked, there is no way to remove its
security policy; only adding more restrictions is allowed.  These threads are
now in a new Landlock domain, merge of their parent one (if any) with the new
ruleset.

Full working code can be found in `samples/landlock/sandboxer.c`_.

Good practices
--------------

It is recommended setting access rights to file hierarchy leaves as much as
possible.  For instance, it is better to be able to have ``~/doc/`` as a
read-only hierarchy and ``~/tmp/`` as a read-write hierarchy, compared to
``~/`` as a read-only hierarchy and ``~/tmp/`` as a read-write hierarchy.
Following this good practice leads to self-sufficient hierarchies that do not
depend on their location (i.e. parent directories).  This is particularly
relevant when we want to allow linking or renaming.  Indeed, having consistent
access rights per directory enables to change the location of such directory
without relying on the destination directory access rights (except those that
are required for this operation, see ``LANDLOCK_ACCESS_FS_REFER``
documentation).

Having self-sufficient hierarchies also helps to tighten the required access
rights to the minimal set of data.  This also helps avoid sinkhole directories,
i.e.  directories where data can be linked to but not linked from.  However,
this depends on data organization, which might not be controlled by developers.
In this case, granting read-write access to ``~/tmp/``, instead of write-only
access, would potentially allow to move ``~/tmp/`` to a non-readable directory
and still keep the ability to list the content of ``~/tmp/``.

Layers of file path access rights
---------------------------------

Each time a thread enforces a ruleset on itself, it updates its Landlock domain
with a new layer of policy.  Indeed, this complementary policy is stacked with
the potentially other rulesets already restricting this thread.  A sandboxed
thread can then safely add more constraints to itself with a new enforced
ruleset.

One policy layer grants access to a file path if at least one of its rules
encountered on the path grants the access.  A sandboxed thread can only access
a file path if all its enforced policy layers grant the access as well as all
the other system access controls (e.g. filesystem DAC, other LSM policies,
etc.).

Bind mounts and OverlayFS
-------------------------

Landlock enables to restrict access to file hierarchies, which means that these
access rights can be propagated with bind mounts (cf.
Documentation/filesystems/sharedsubtree.rst) but not with
Documentation/filesystems/overlayfs.rst.

A bind mount mirrors a source file hierarchy to a destination.  The destination
hierarchy is then composed of the exact same files, on which Landlock rules can
be tied, either via the source or the destination path.  These rules restrict
access when they are encountered on a path, which means that they can restrict
access to multiple file hierarchies at the same time, whether these hierarchies
are the result of bind mounts or not.

An OverlayFS mount point consists of upper and lower layers.  These layers are
combined in a merge directory, result of the mount point.  This merge hierarchy
may include files from the upper and lower layers, but modifications performed
on the merge hierarchy only reflects on the upper layer.  From a Landlock
policy point of view, each OverlayFS layers and merge hierarchies are
standalone and contains their own set of files and directories, which is
different from bind mounts.  A policy restricting an OverlayFS layer will not
restrict the resulted merged hierarchy, and vice versa.  Landlock users should
then only think about file hierarchies they want to allow access to, regardless
of the underlying filesystem.

Inheritance
-----------

Every new thread resulting from a :manpage:`clone(2)` inherits Landlock domain
restrictions from its parent.  This is similar to the seccomp inheritance (cf.
Documentation/userspace-api/seccomp_filter.rst) or any other LSM dealing with
task's :manpage:`credentials(7)`.  For instance, one process's thread may apply
Landlock rules to itself, but they will not be automatically applied to other
sibling threads (unlike POSIX thread credential changes, cf.
:manpage:`nptl(7)`).

When a thread sandboxes itself, we have the guarantee that the related security
policy will stay enforced on all this thread's descendants.  This allows
creating standalone and modular security policies per application, which will
automatically be composed between themselves according to their runtime parent
policies.

Ptrace restrictions
-------------------

A sandboxed process has less privileges than a non-sandboxed process and must
then be subject to additional restrictions when manipulating another process.
To be allowed to use :manpage:`ptrace(2)` and related syscalls on a target
process, a sandboxed process should have a subset of the target process rules,
which means the tracee must be in a sub-domain of the tracer.

Truncating files
----------------

The operations covered by ``LANDLOCK_ACCESS_FS_WRITE_FILE`` and
``LANDLOCK_ACCESS_FS_TRUNCATE`` both change the contents of a file and sometimes
overlap in non-intuitive ways.  It is recommended to always specify both of
these together.

A particularly surprising example is :manpage:`creat(2)`.  The name suggests
that this system call requires the rights to create and write files.  However,
it also requires the truncate right if an existing file under the same name is
already present.

It should also be noted that truncating files does not require the
``LANDLOCK_ACCESS_FS_WRITE_FILE`` right.  Apart from the :manpage:`truncate(2)`
system call, this can also be done through :manpage:`open(2)` with the flags
``O_RDONLY | O_TRUNC``.

The truncate right is associated with the opened file (see below).

Rights associated with file descriptors
---------------------------------------

When opening a file, the availability of the ``LANDLOCK_ACCESS_FS_TRUNCATE`` and
``LANDLOCK_ACCESS_FS_IOCTL_DEV`` rights is associated with the newly created
file descriptor and will be used for subsequent truncation and ioctl attempts
using :manpage:`ftruncate(2)` and :manpage:`ioctl(2)`.  The behavior is similar
to opening a file for reading or writing, where permissions are checked during
:manpage:`open(2)`, but not during the subsequent :manpage:`read(2)` and
:manpage:`write(2)` calls.

As a consequence, it is possible that a process has multiple open file
descriptors referring to the same file, but Landlock enforces different things
when operating with these file descriptors.  This can happen when a Landlock
ruleset gets enforced and the process keeps file descriptors which were opened
both before and after the enforcement.  It is also possible to pass such file
descriptors between processes, keeping their Landlock properties, even when some
of the involved processes do not have an enforced Landlock ruleset.

Compatibility
=============

Backward and forward compatibility
----------------------------------

Landlock is designed to be compatible with past and future versions of the
kernel.  This is achieved thanks to the system call attributes and the
associated bitflags, particularly the ruleset's ``handled_access_fs``.  Making
handled access right explicit enables the kernel and user space to have a clear
contract with each other.  This is required to make sure sandboxing will not
get stricter with a system update, which could break applications.

Developers can subscribe to the `Landlock mailing list
<https://subspace.kernel.org/lists.linux.dev.html>`_ to knowingly update and
test their applications with the latest available features.  In the interest of
users, and because they may use different kernel versions, it is strongly
encouraged to follow a best-effort security approach by checking the Landlock
ABI version at runtime and only enforcing the supported features.

.. _landlock_abi_versions:

Landlock ABI versions
---------------------

The Landlock ABI version can be read with the sys_landlock_create_ruleset()
system call:

.. code-block:: c

    int abi;

    abi = landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 0) {
        switch (errno) {
        case ENOSYS:
            printf("Landlock is not supported by the current kernel.\n");
            break;
        case EOPNOTSUPP:
            printf("Landlock is currently disabled.\n");
            break;
        }
        return 0;
    }
    if (abi >= 2) {
        printf("Landlock supports LANDLOCK_ACCESS_FS_REFER.\n");
    }

The following kernel interfaces are implicitly supported by the first ABI
version.  Features only supported from a specific version are explicitly marked
as such.

Kernel interface
================

Access rights
-------------

.. kernel-doc:: include/uapi/linux/landlock.h
    :identifiers: fs_access net_access

Creating a new ruleset
----------------------

.. kernel-doc:: security/landlock/syscalls.c
    :identifiers: sys_landlock_create_ruleset

.. kernel-doc:: include/uapi/linux/landlock.h
    :identifiers: landlock_ruleset_attr

Extending a ruleset
-------------------

.. kernel-doc:: security/landlock/syscalls.c
    :identifiers: sys_landlock_add_rule

.. kernel-doc:: include/uapi/linux/landlock.h
    :identifiers: landlock_rule_type landlock_path_beneath_attr
                  landlock_net_port_attr

Enforcing a ruleset
-------------------

.. kernel-doc:: security/landlock/syscalls.c
    :identifiers: sys_landlock_restrict_self

Current limitations
===================

Filesystem topology modification
--------------------------------

Threads sandboxed with filesystem restrictions cannot modify filesystem
topology, whether via :manpage:`mount(2)` or :manpage:`pivot_root(2)`.
However, :manpage:`chroot(2)` calls are not denied.

Special filesystems
-------------------

Access to regular files and directories can be restricted by Landlock,
according to the handled accesses of a ruleset.  However, files that do not
come from a user-visible filesystem (e.g. pipe, socket), but can still be
accessed through ``/proc/<pid>/fd/*``, cannot currently be explicitly
restricted.  Likewise, some special kernel filesystems such as nsfs, which can
be accessed through ``/proc/<pid>/ns/*``, cannot currently be explicitly
restricted.  However, thanks to the `ptrace restrictions`_, access to such
sensitive ``/proc`` files are automatically restricted according to domain
hierarchies.  Future Landlock evolutions could still enable to explicitly
restrict such paths with dedicated ruleset flags.

Ruleset layers
--------------

There is a limit of 16 layers of stacked rulesets.  This can be an issue for a
task willing to enforce a new ruleset in complement to its 16 inherited
rulesets.  Once this limit is reached, sys_landlock_restrict_self() returns
E2BIG.  It is then strongly suggested to carefully build rulesets once in the
life of a thread, especially for applications able to launch other applications
that may also want to sandbox themselves (e.g. shells, container managers,
etc.).

Memory usage
------------

Kernel memory allocated to create rulesets is accounted and can be restricted
by the Documentation/admin-guide/cgroup-v1/memory.rst.

IOCTL support
-------------

The ``LANDLOCK_ACCESS_FS_IOCTL_DEV`` right restricts the use of
:manpage:`ioctl(2)`, but it only applies to *newly opened* device files.  This
means specifically that pre-existing file descriptors like stdin, stdout and
stderr are unaffected.

Users should be aware that TTY devices have traditionally permitted to control
other processes on the same TTY through the ``TIOCSTI`` and ``TIOCLINUX`` IOCTL
commands.  Both of these require ``CAP_SYS_ADMIN`` on modern Linux systems, but
the behavior is configurable for ``TIOCSTI``.

On older systems, it is therefore recommended to close inherited TTY file
descriptors, or to reopen them from ``/proc/self/fd/*`` without the
``LANDLOCK_ACCESS_FS_IOCTL_DEV`` right, if possible.

Landlock's IOCTL support is coarse-grained at the moment, but may become more
fine-grained in the future.  Until then, users are advised to establish the
guarantees that they need through the file hierarchy, by only allowing the
``LANDLOCK_ACCESS_FS_IOCTL_DEV`` right on files where it is really required.

Previous limitations
====================

File renaming and linking (ABI < 2)
-----------------------------------

Because Landlock targets unprivileged access controls, it needs to properly
handle composition of rules.  Such property also implies rules nesting.
Properly handling multiple layers of rulesets, each one of them able to
restrict access to files, also implies inheritance of the ruleset restrictions
from a parent to its hierarchy.  Because files are identified and restricted by
their hierarchy, moving or linking a file from one directory to another implies
propagation of the hierarchy constraints, or restriction of these actions
according to the potentially lost constraints.  To protect against privilege
escalations through renaming or linking, and for the sake of simplicity,
Landlock previously limited linking and renaming to the same directory.
Starting with the Landlock ABI version 2, it is now possible to securely
control renaming and linking thanks to the new ``LANDLOCK_ACCESS_FS_REFER``
access right.

File truncation (ABI < 3)
-------------------------

File truncation could not be denied before the third Landlock ABI, so it is
always allowed when using a kernel that only supports the first or second ABI.

Starting with the Landlock ABI version 3, it is now possible to securely control
truncation thanks to the new ``LANDLOCK_ACCESS_FS_TRUNCATE`` access right.

Network support (ABI < 4)
-------------------------

Starting with the Landlock ABI version 4, it is now possible to restrict TCP
bind and connect actions to only a set of allowed ports thanks to the new
``LANDLOCK_ACCESS_NET_BIND_TCP`` and ``LANDLOCK_ACCESS_NET_CONNECT_TCP``
access rights.

IOCTL (ABI < 5)
---------------

IOCTL operations could not be denied before the fifth Landlock ABI, so
:manpage:`ioctl(2)` is always allowed when using a kernel that only supports an
earlier ABI.

Starting with the Landlock ABI version 5, it is possible to restrict the use of
:manpage:`ioctl(2)` using the new ``LANDLOCK_ACCESS_FS_IOCTL_DEV`` right.

.. _kernel_support:

Kernel support
==============

Build time configuration
------------------------

Landlock was first introduced in Linux 5.13 but it must be configured at build
time with ``CONFIG_SECURITY_LANDLOCK=y``.  Landlock must also be enabled at boot
time as the other security modules.  The list of security modules enabled by
default is set with ``CONFIG_LSM``.  The kernel configuration should then
contains ``CONFIG_LSM=landlock,[...]`` with ``[...]``  as the list of other
potentially useful security modules for the running system (see the
``CONFIG_LSM`` help).

Boot time configuration
-----------------------

If the running kernel does not have ``landlock`` in ``CONFIG_LSM``, then we can
enable Landlock by adding ``lsm=landlock,[...]`` to
Documentation/admin-guide/kernel-parameters.rst in the boot loader
configuration.

For example, if the current built-in configuration is:

.. code-block:: console

    $ zgrep -h "^CONFIG_LSM=" "/boot/config-$(uname -r)" /proc/config.gz 2>/dev/null
    CONFIG_LSM="lockdown,yama,integrity,apparmor"

...and if the cmdline doesn't contain ``landlock`` either:

.. code-block:: console

    $ sed -n 's/.*\(\<lsm=\S\+\).*/\1/p' /proc/cmdline
    lsm=lockdown,yama,integrity,apparmor

...we should configure the boot loader to set a cmdline extending the ``lsm``
list with the ``landlock,`` prefix::

  lsm=landlock,lockdown,yama,integrity,apparmor

After a reboot, we can check that Landlock is up and running by looking at
kernel logs:

.. code-block:: console

    # dmesg | grep landlock || journalctl -kb -g landlock
    [    0.000000] Command line: [...] lsm=landlock,lockdown,yama,integrity,apparmor
    [    0.000000] Kernel command line: [...] lsm=landlock,lockdown,yama,integrity,apparmor
    [    0.000000] LSM: initializing lsm=lockdown,capability,landlock,yama,integrity,apparmor
    [    0.000000] landlock: Up and running.

The kernel may be configured at build time to always load the ``lockdown`` and
``capability`` LSMs.  In that case, these LSMs will appear at the beginning of
the ``LSM: initializing`` log line as well, even if they are not configured in
the boot loader.

Network support
---------------

To be able to explicitly allow TCP operations (e.g., adding a network rule with
``LANDLOCK_ACCESS_NET_BIND_TCP``), the kernel must support TCP
(``CONFIG_INET=y``).  Otherwise, sys_landlock_add_rule() returns an
``EAFNOSUPPORT`` error, which can safely be ignored because this kind of TCP
operation is already not possible.

Questions and answers
=====================

What about user space sandbox managers?
---------------------------------------

Using user space process to enforce restrictions on kernel resources can lead
to race conditions or inconsistent evaluations (i.e. `Incorrect mirroring of
the OS code and state
<https://www.ndss-symposium.org/ndss2003/traps-and-pitfalls-practical-problems-system-call-interposition-based-security-tools/>`_).

What about namespaces and containers?
-------------------------------------

Namespaces can help create sandboxes but they are not designed for
access-control and then miss useful features for such use case (e.g. no
fine-grained restrictions).  Moreover, their complexity can lead to security
issues, especially when untrusted processes can manipulate them (cf.
`Controlling access to user namespaces <https://lwn.net/Articles/673597/>`_).

Additional documentation
========================

* Documentation/security/landlock.rst
* https://landlock.io

.. Links
.. _samples/landlock/sandboxer.c:
   https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/samples/landlock/sandboxer.c
