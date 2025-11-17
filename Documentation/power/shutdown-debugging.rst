.. SPDX-License-Identifier: GPL-2.0

Debugging Kernel Shutdown Hangs with pstore
+++++++++++++++++++++++++++++++++++++++++++

Overview
========
If the system hangs while shutting down, the kernel logs may need to be
retrieved to debug the issue.

On systems that have a UART available, it is best to configure the kernel to use
this UART for kernel console output.

If a UART isn't available, the ``pstore`` subsystem provides a mechanism to
persist this data across a system reset, allowing it to be retrieved on the next
boot.

Kernel Configuration
====================
To enable ``pstore`` and enable saving kernel ring buffer logs, set the
following kernel configuration options:

* ``CONFIG_PSTORE=y``
* ``CONFIG_PSTORE_CONSOLE=y``

Additionally, enable a backend to store the data. Depending upon your platform
some potential options include:

* ``CONFIG_EFI_VARS_PSTORE=y``
* ``CONFIG_PSTORE_RAM=y``
* ``CONFIG_CHROMEOS_PSTORE=y``
* ``CONFIG_PSTORE_BLK=y``

Kernel Command-line Parameters
==============================
Add these parameters to your kernel command line:

* ``printk.always_kmsg_dump=Y``
	* Forces the kernel to dump the entire message buffer to pstore during
		shutdown
* ``efi_pstore.pstore_disable=N``
	* For EFI-based systems, ensures the EFI backend is active

Userspace Interaction and Log Retrieval
=======================================
On the next boot after a hang, pstore logs will be available in the pstore
filesystem (``/sys/fs/pstore``) and can be retrieved by userspace.

On systemd systems, the ``systemd-pstore`` service will help do the following:

#. Locate pstore data in ``/sys/fs/pstore``
#. Read and save it to ``/var/lib/systemd/pstore``
#. Clear pstore data for the next event
