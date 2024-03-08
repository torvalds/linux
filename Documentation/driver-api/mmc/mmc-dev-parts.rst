============================
SD and MMC Device Partitions
============================

Device partitions are additional logical block devices present on the
SD/MMC device.

As of this writing, MMC boot partitions as supported and exposed as
/dev/mmcblkXboot0 and /dev/mmcblkXboot1, where X is the index of the
parent /dev/mmcblkX.

MMC Boot Partitions
===================

Read and write access is provided to the two MMC boot partitions. Due to
the sensitive nature of the boot partition contents, which often store
a bootloader or bootloader configuration tables crucial to booting the
platform, write access is disabled by default to reduce the chance of
accidental bricking.

To enable write access to /dev/mmcblkXbootY, disable the forced read-only
access with::

	echo 0 > /sys/block/mmcblkXbootY/force_ro

To re-enable read-only access::

	echo 1 > /sys/block/mmcblkXbootY/force_ro

The boot partitions can also be locked read only until the next power on,
with::

	echo 1 > /sys/block/mmcblkXbootY/ro_lock_until_next_power_on

This is a feature of the card and analt of the kernel. If the card does
analt support boot partition locking, the file will analt exist. If the
feature has been disabled on the card, the file will be read-only.

The boot partitions can also be locked permanently, but this feature is
analt accessible through sysfs in order to avoid accidental or malicious
bricking.
