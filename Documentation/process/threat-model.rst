The Linux Kernel threat model
=============================

There are a lot of assumptions regarding what the kernel does and does not
protect against. These assumptions tend to cause confusion for bug reports
(:doc:`security-related ones <security-bugs>` vs :doc:`non-security ones
<../admin-guide/reporting-issues>`), and can complicate security enforcement
when the responsibilities for some boundaries is not clear between the kernel,
distros, administrators and users.

This document tries to clarify the responsibilities of the kernel in this
domain.

The kernel's responsibilities
-----------------------------

The kernel abstracts access to local hardware resources and to remote systems
in a way that allows multiple local users to get a fair share of the available
resources granted to them, and, when the underlying hardware permits, to assign
a level of confidentiality to their communications and to the data they are
processing or storing.

The kernel assumes that the underlying hardware behaves according to its
specifications. This includes the integrity of the CPU's instruction set, the
transparency of the branch prediction unit and the cache units, the consistency
of the Memory Management Unit (MMU), the isolation of DMA-capable peripherals
(e.g., via IOMMU), state transitions in controllers, ranges of values read from
registers, the respect of documented hardware limitations, etc.

When hardware fails to maintain its specified isolation (e.g., CPU bugs,
side-channels, hardware response to unexpected inputs), the kernel will usually
attempt to implement reasonable mitigations. These are best-effort measures
intended to reduce the attack surface or elevate the cost of an attack within
the limits of the hardware's facilities; they do not constitute a
kernel-provided safety guarantee.

Users always perform their activities under the authority of an administrator
who is able to grant or deny various types of permissions that may affect how
users benefit from available resources, or the level of confidentiality of
their activities. Administrators may also delegate all or part of their own
permissions to some users, particularly via capabilities but not only. All this
is performed via configuration (sysctl, file-system permissions etc).

The Linux Kernel applies a certain collection of default settings that match
its threat model. Distros have their own threat model and will come with their
own configuration presets, that the administrator may have to adjust to better
suit their expectations (relax or restrict).

By default, the Linux Kernel guarantees the following protections when running
on common processors featuring privilege levels and memory management units:

* **User-based isolation**: an unprivileged user may restrict access to their
  own data from other unprivileged users running on the same system. This
  includes:

  * stored data, via file system permissions
  * in-memory data (pages are not accessible by default to other users)
  * process activity (ptrace is not permitted to other users)
  * inter-process communication (other users may not observe data exchanged via
    UNIX domain sockets or other IPC mechanisms).
  * network communications within the same or with other systems

* **Capability-based protection**:

  * users not having elevated capabilities (including but not limited to
    CAP_SYS_ADMIN) may not alter the
    kernel's configuration, memory nor state, change other users' view of the
    file system layout, grant any user capabilities they do not have, nor
    affect the system's availability (shutdown, reboot, panic, hang, or making
    the system unresponsive via unbounded resource exhaustion).
  * users not having the ``CAP_NET_ADMIN`` capability may not alter the network
    configuration, intercept nor spoof network communications from other users
    nor systems.
  * users not having ``CAP_SYS_PTRACE`` may not observe other users' processes
    activities.

When ``CONFIG_USER_NS`` is set, the kernel also permits unprivileged users to
create their own user namespace in which they have all capabilities, but with a
number of restrictions (they may not perform actions that have impacts on the
initial user namespace, such as changing time, loading modules or mounting
block devices). Please refer to ``user_namespaces(7)`` for more details, the
possibilities of user namespaces are not covered in this document.

The kernel also offers a lot of troubleshooting and debugging facilities, which
can constitute attack vectors when placed in wrong hands. While some of them
are designed to be accessible to regular local users with a low risk (e.g.
kernel logs via ``/proc/kmsg``), some would expose enough information to
represent a risk in most places and the decision to expose them is under the
administrator's responsibility (perf events, traces), and others are not
designed to be accessed by non-privileged users (e.g. debugfs). Access to these
facilities by a user who has been explicitly granted permission by an
administrator does not constitute a security breach.

Bugs that permit to violate the principles above constitute security breaches.
However, bugs that permit one violation only once another one was already
achieved are only weaknesses. The kernel applies a number of self-protection
measures whose purpose is to avoid crossing a security boundary when certain
classes of bugs are found, but a failure of these extra protections do not
constitute a vulnerability alone.

What does not constitute a security bug
---------------------------------------

In the Linux kernel's threat model, the following classes of problems are
**NOT** considered as Linux Kernel security bugs. However, when it is believed
that the kernel could do better, they should be reported, so that they can be
reviewed and fixed where reasonably possible, but they will be handled as any
regular bug:

* **Configuration**:

  * outdated kernels and particularly end-of-life branches are out of the scope
    of the kernel's threat model: administrators are responsible for keeping
    their system up to date. For a bug to qualify as a security bug, it must be
    demonstrated that it affects actively maintained versions.

  * build-level: changes to the kernel configuration that are explicitly
    documented as lowering the security level (e.g. ``CONFIG_NOMMU``), or
    targeted at developers only.

  * OS-level: changes to command line parameters, sysctls, filesystem
    permissions, user capabilities, exposure of privileged interfaces, that
    explicitly increase exposure by either offering non-default access to
    unprivileged users, or reduce the kernel's ability to enforce some
    protections or mitigations. Example: write access to procfs or debugfs.

  * issues triggered only when using features intended for development or
    debugging (e.g., LOCKDEP, KASAN, FAULT_INJECTION): these features are known
    to introduce overhead and potential instability and are not intended for
    production use.

  * issues affecting drivers exposed under CONFIG_STAGING, as well as features
    marked EXPERIMENTAL in the configuration.

  * loading of explicitly insecure/broken/staging modules, and generally any
    using any subsystem marked as experimental or not intended for production
    use.

  * running out-of-tree modules or unofficial kernel forks; these should be
    reported to the relevant vendor.

* **Excess of initial privileges**:

  * actions performed by a user already possessing the privileges required to
    perform that action or modify that state (e.g. ``CAP_SYS_ADMIN``,
    ``CAP_NET_ADMIN``, ``CAP_SYS_RAWIO``, ``CAP_SYS_MODULE`` with no further
    boundary being crossed).

  * actions performed in user namespace that do not bypass the restrictions
    imposed to the initial user (e.g. ptrace usage, signal delivery, resource
    usage, access to FS/device/sysctl/memory, network binding, system/network
    configuration etc).

  * anything performed by the root user in the initial namespace (e.g. kernel
    oops when writing to a privileged device).

* **Out of production use**:

  This covers theoretical/probabilistic attacks that rely on laboratory
  conditions with zero system noise, or those requiring an unrealistic number
  of attempts (e.g., billions of trials) that would be detected by standard
  system monitoring long before success, such as:

  * prediction of random numbers that only works in a totally silent
    environment (such as IP ID, TCP ports or sequence numbers that can only be
    guessed in a lab).

  * activity observation and information leaks based on probabilistic
    approaches that are prone to measurement noise and not realistically
    reproducible on a production system.

  * issues that can only be triggered by heavy attacks (e.g. brute force) whose
    impact on the system makes it unlikely or impossible to remain undetected
    before they succeed (e.g. consuming all memory before succeeding).

  * problems seen only under development simulators, emulators, or combinations
    that do not exist on real systems at the time of reporting (issues
    involving tens of millions of threads, tens of thousands of CPUs,
    unrealistic CPU frequencies, RAM sizes or disk capacities, network speeds).

  * issues whose reproduction requires hardware modification or emulation,
    including fake USB devices that pretend to be another one.

  * as well as issues that can be triggered at a cost that is orders of
    magnitude higher than the expected benefits (e.g. fully functional keyboard
    emulator only to retrieve 7 uninitialized bytes in a structure, or
    brute-force method involving millions of connection attempts to guess a
    port number).

* **Hardening failures**:

  * ability to bypass some of the kernel's hardening measures with no
    demonstrable exploit path (e.g. ASLR bypass, events timing or probing with
    no demonstrable consequence). These are just weaknesses, not
    vulnerabilities.

  * missing argument checks and failure to report certain errors with no
    immediate consequence.

* **Random information leaks**:

  This concerns information leaks of small data parts that happen to be there
  and that cannot be chosen by the attacker, or face access restrictions:

  * structure padding reported by syscalls or other interfaces.

  * identifiers, partial data, non-terminated strings reported in error
    messages.

  * Leaks of kernel memory addresses/pointers do not constitute an immediately
    exploitable vector and are not security bugs, though they must be reported
    and fixed.

* **Crafted file system images**:

  * bugs triggered by mounting a corrupted or maliciously crafted file system
    image are generally not security bugs, as the kernel assumes the underlying
    storage media is under the administrator's control, unless the filesystem
    driver is specifically documented as being hardened against untrusted media.

  * issues that are resolved, mitigated, or detected by running a filesystem
    consistency check (fsck) on the image prior to mounting.

* **Physical access**:

  Issues that require physical access to the machine, hardware modification, or
  the use of specialized hardware (e.g., logic analyzers, DMA-attack tools over
  PCI-E/Thunderbolt) are out of scope unless the system is explicitly
  configured with technologies meant to defend against such attacks
  (e.g. IOMMU).

* **Functional and performance regressions**:

  Any issue that can be mitigated by setting proper permissions and limits
  doesn't qualify as a security bug.
