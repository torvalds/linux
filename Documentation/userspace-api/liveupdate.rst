.. SPDX-License-Identifier: GPL-2.0

================
Live Update uAPI
================
:Author: Pasha Tatashin <pasha.tatashin@soleen.com>

ioctl interface
===============
.. kernel-doc:: kernel/liveupdate/luo_core.c
   :doc: LUO ioctl Interface

ioctl uAPI
===========
.. kernel-doc:: include/uapi/linux/liveupdate.h

Note on Compatibility
=====================

Note that the Live Update feature is still under development and subject to
change, so compatibility is not guaranteed when kexec rebooting between
different kernel versions. This is expected to change and stabilize in a future
version.

Userspace Integration
=====================

systemd (since version v261) uses LUO to preserve its per-service file
descriptor store across a kexec-based live update. Services opt in by setting
``FileDescriptorStoreMax=`` and ``FileDescriptorStorePreserve=`` in their unit,
and push file descriptors with a name into the store via
``sd_pid_notify_with_fds(... "FDSTORE=1\nFDNAME=foo")``.

Services may also create their own LUO sessions (via ``/dev/liveupdate``) and
push the resulting session fds into their file descriptor store like any other
fd. systemd detects such session fds and handles them accordingly, and
hands the re-retrieved session fd back to the service after kexec, using the
existing file descriptor store service interface.

For details, see:

- `File Descriptor Store <https://systemd.io/FILE_DESCRIPTOR_STORE/>`_
- `systemd.service(5) FileDescriptorStorePreserve=
  <https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html#FileDescriptorStorePreserve=>`_
- `sd_pid_notify_with_fds(3)
  <https://www.freedesktop.org/software/systemd/man/latest/sd_pid_notify_with_fds.html>`_

See Also
========

- :doc:`Live Update Orchestrator </core-api/liveupdate>`
