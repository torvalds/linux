.. SPDX-License-Identifier: GPL-2.0

==================================
Introduction of non-executable mfd
==================================
:Author:
    Daniel Verkamp <dverkamp@chromium.org>
    Jeff Xu <jeffxu@chromium.org>

:Contributor:
	Aleksa Sarai <cyphar@cyphar.com>

Since Linux introduced the memfd feature, memfds have always had their
execute bit set, and the memfd_create() syscall doesn't allow setting
it differently.

However, in a secure-by-default system, such as ChromeOS, (where all
executables should come from the rootfs, which is protected by verified
boot), this executable nature of memfd opens a door for NoExec bypass
and enables “confused deputy attack”.  E.g, in VRP bug [1]: cros_vm
process created a memfd to share the content with an external process,
however the memfd is overwritten and used for executing arbitrary code
and root escalation. [2] lists more VRP of this kind.

On the other hand, executable memfd has its legit use: runc uses memfd’s
seal and executable feature to copy the contents of the binary then
execute them. For such a system, we need a solution to differentiate runc's
use of executable memfds and an attacker's [3].

To address those above:
 - Let memfd_create() set X bit at creation time.
 - Let memfd be sealed for modifying X bit when NX is set.
 - Add a new pid namespace sysctl: vm.memfd_noexec to help applications in
   migrating and enforcing non-executable MFD.

User API
========
``int memfd_create(const char *name, unsigned int flags)``

``MFD_NOEXEC_SEAL``
	When MFD_NOEXEC_SEAL bit is set in the ``flags``, memfd is created
	with NX. F_SEAL_EXEC is set and the memfd can't be modified to
	add X later. MFD_ALLOW_SEALING is also implied.
	This is the most common case for the application to use memfd.

``MFD_EXEC``
	When MFD_EXEC bit is set in the ``flags``, memfd is created with X.

Note:
	``MFD_NOEXEC_SEAL`` implies ``MFD_ALLOW_SEALING``. In case that
	an app doesn't want sealing, it can add F_SEAL_SEAL after creation.


Sysctl:
========
``pid namespaced sysctl vm.memfd_noexec``

The new pid namespaced sysctl vm.memfd_noexec has 3 values:

 - 0: MEMFD_NOEXEC_SCOPE_EXEC
	memfd_create() without MFD_EXEC nor MFD_NOEXEC_SEAL acts like
	MFD_EXEC was set.

 - 1: MEMFD_NOEXEC_SCOPE_NOEXEC_SEAL
	memfd_create() without MFD_EXEC nor MFD_NOEXEC_SEAL acts like
	MFD_NOEXEC_SEAL was set.

 - 2: MEMFD_NOEXEC_SCOPE_NOEXEC_ENFORCED
	memfd_create() without MFD_NOEXEC_SEAL will be rejected.

The sysctl allows finer control of memfd_create for old software that
doesn't set the executable bit; for example, a container with
vm.memfd_noexec=1 means the old software will create non-executable memfd
by default while new software can create executable memfd by setting
MFD_EXEC.

The value of vm.memfd_noexec is passed to child namespace at creation
time. In addition, the setting is hierarchical, i.e. during memfd_create,
we will search from current ns to root ns and use the most restrictive
setting.

[1] https://crbug.com/1305267

[2] https://bugs.chromium.org/p/chromium/issues/list?q=type%3Dbug-security%20memfd%20escalation&can=1

[3] https://lwn.net/Articles/781013/
