.. SPDX-License-Identifier: GPL-2.0

=======================================
efivarfs - a (U)EFI variable filesystem
=======================================

The efivarfs filesystem was created to address the shortcomings of
using entries in sysfs to maintain EFI variables. The old sysfs EFI
variables code only supported variables of up to 1024 bytes. This
limitation existed in version 0.99 of the EFI specification, but was
removed before any full releases. Since variables can now be larger
than a single page, sysfs isn't the best interface for this.

Variables can be created, deleted and modified with the efivarfs
filesystem.

efivarfs is typically mounted like this::

	mount -t efivarfs none /sys/firmware/efi/efivars

Due to the presence of numerous firmware bugs where removing non-standard
UEFI variables causes the system firmware to fail to POST, efivarfs
files that are not well-known standardized variables are created
as immutable files.  This doesn't prevent removal - "chattr -i" will work -
but it does prevent this kind of failure from being accomplished
accidentally.

.. warning ::
      When a content of an UEFI variable in /sys/firmware/efi/efivars is
      displayed, for example using "hexdump", pay attention that the first
      4 bytes of the output represent the UEFI variable attributes,
      in little-endian format.

      Practically the output of each efivar is composed of:

          +-----------------------------------+
          |4_bytes_of_attributes + efivar_data|
          +-----------------------------------+

*See also:*

- Documentation/admin-guide/acpi/ssdt-overlays.rst
- Documentation/ABI/stable/sysfs-firmware-efi-vars
