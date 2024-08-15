===============================
Generic Block Device Capability
===============================

This file documents the sysfs file ``block/<disk>/capability``.

``capability`` is a bitfield, printed in hexadecimal, indicating which
capabilities a specific block device supports:

.. kernel-doc:: include/linux/blkdev.h
