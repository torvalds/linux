.. SPDX-License-Identifier: GPL-2.0

==============================
Confidential Computing secrets
==============================

This document describes how Confidential Computing secret injection is handled
from the firmware to the operating system, in the EFI driver and the efi_secret
kernel module.


Introduction
============

Confidential Computing (coco) hardware such as AMD SEV (Secure Encrypted
Virtualization) allows guest owners to inject secrets into the VMs
memory without the host/hypervisor being able to read them.  In SEV,
secret injection is performed early in the VM launch process, before the
guest starts running.

The efi_secret kernel module allows userspace applications to access these
secrets via securityfs.


Secret data flow
================

The guest firmware may reserve a designated memory area for secret injection,
and publish its location (base GPA and length) in the EFI configuration table
under a ``LINUX_EFI_COCO_SECRET_AREA_GUID`` entry
(``adf956ad-e98c-484c-ae11-b51c7d336447``).  This memory area should be marked
by the firmware as ``EFI_RESERVED_TYPE``, and therefore the kernel should not
be use it for its own purposes.

During the VM's launch, the virtual machine manager may inject a secret to that
area.  In AMD SEV and SEV-ES this is performed using the
``KVM_SEV_LAUNCH_SECRET`` command (see [sev]_).  The strucutre of the injected
Guest Owner secret data should be a GUIDed table of secret values; the binary
format is described in ``drivers/virt/coco/efi_secret/efi_secret.c`` under
"Structure of the EFI secret area".

On kernel start, the kernel's EFI driver saves the location of the secret area
(taken from the EFI configuration table) in the ``efi.coco_secret`` field.
Later it checks if the secret area is populated: it maps the area and checks
whether its content begins with ``EFI_SECRET_TABLE_HEADER_GUID``
(``1e74f542-71dd-4d66-963e-ef4287ff173b``).  If the secret area is populated,
the EFI driver will autoload the efi_secret kernel module, which exposes the
secrets to userspace applications via securityfs.  The details of the
efi_secret filesystem interface are in [secrets-coco-abi]_.


Application usage example
=========================

Consider a guest performing computations on encrypted files.  The Guest Owner
provides the decryption key (= secret) using the secret injection mechanism.
The guest application reads the secret from the efi_secret filesystem and
proceeds to decrypt the files into memory and then performs the needed
computations on the content.

In this example, the host can't read the files from the disk image
because they are encrypted.  Host can't read the decryption key because
it is passed using the secret injection mechanism (= secure channel).
Host can't read the decrypted content from memory because it's a
confidential (memory-encrypted) guest.

Here is a simple example for usage of the efi_secret module in a guest
to which an EFI secret area with 4 secrets was injected during launch::

	# ls -la /sys/kernel/security/secrets/coco
	total 0
	drwxr-xr-x 2 root root 0 Jun 28 11:54 .
	drwxr-xr-x 3 root root 0 Jun 28 11:54 ..
	-r--r----- 1 root root 0 Jun 28 11:54 736870e5-84f0-4973-92ec-06879ce3da0b
	-r--r----- 1 root root 0 Jun 28 11:54 83c83f7f-1356-4975-8b7e-d3a0b54312c6
	-r--r----- 1 root root 0 Jun 28 11:54 9553f55d-3da2-43ee-ab5d-ff17f78864d2
	-r--r----- 1 root root 0 Jun 28 11:54 e6f5a162-d67f-4750-a67c-5d065f2a9910

	# hd /sys/kernel/security/secrets/coco/e6f5a162-d67f-4750-a67c-5d065f2a9910
	00000000  74 68 65 73 65 2d 61 72  65 2d 74 68 65 2d 6b 61  |these-are-the-ka|
	00000010  74 61 2d 73 65 63 72 65  74 73 00 01 02 03 04 05  |ta-secrets......|
	00000020  06 07                                             |..|
	00000022

	# rm /sys/kernel/security/secrets/coco/e6f5a162-d67f-4750-a67c-5d065f2a9910

	# ls -la /sys/kernel/security/secrets/coco
	total 0
	drwxr-xr-x 2 root root 0 Jun 28 11:55 .
	drwxr-xr-x 3 root root 0 Jun 28 11:54 ..
	-r--r----- 1 root root 0 Jun 28 11:54 736870e5-84f0-4973-92ec-06879ce3da0b
	-r--r----- 1 root root 0 Jun 28 11:54 83c83f7f-1356-4975-8b7e-d3a0b54312c6
	-r--r----- 1 root root 0 Jun 28 11:54 9553f55d-3da2-43ee-ab5d-ff17f78864d2


References
==========

See [sev-api-spec]_ for more info regarding SEV ``LAUNCH_SECRET`` operation.

.. [sev] Documentation/virt/kvm/x86/amd-memory-encryption.rst
.. [secrets-coco-abi] Documentation/ABI/testing/securityfs-secrets-coco
.. [sev-api-spec] https://www.amd.com/system/files/TechDocs/55766_SEV-KM_API_Specification.pdf
