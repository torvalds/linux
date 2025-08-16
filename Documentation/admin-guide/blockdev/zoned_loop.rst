.. SPDX-License-Identifier: GPL-2.0

=======================
Zoned Loop Block Device
=======================

.. Contents:

	1) Overview
	2) Creating a Zoned Device
	3) Deleting a Zoned Device
	4) Example


1) Overview
-----------

The zoned loop block device driver (zloop) allows a user to create a zoned block
device using one regular file per zone as backing storage. This driver does not
directly control any hardware and uses read, write and truncate operations to
regular files of a file system to emulate a zoned block device.

Using zloop, zoned block devices with a configurable capacity, zone size and
number of conventional zones can be created. The storage for each zone of the
device is implemented using a regular file with a maximum size equal to the zone
size. The size of a file backing a conventional zone is always equal to the zone
size. The size of a file backing a sequential zone indicates the amount of data
sequentially written to the file, that is, the size of the file directly
indicates the position of the write pointer of the zone.

When resetting a sequential zone, its backing file size is truncated to zero.
Conversely, for a zone finish operation, the backing file is truncated to the
zone size. With this, the maximum capacity of a zloop zoned block device created
can be larger configured to be larger than the storage space available on the
backing file system. Of course, for such configuration, writing more data than
the storage space available on the backing file system will result in write
errors.

The zoned loop block device driver implements a complete zone transition state
machine. That is, zones can be empty, implicitly opened, explicitly opened,
closed or full. The current implementation does not support any limits on the
maximum number of open and active zones.

No user tools are necessary to create and delete zloop devices.

2) Creating a Zoned Device
--------------------------

Once the zloop module is loaded (or if zloop is compiled in the kernel), the
character device file /dev/zloop-control can be used to add a zloop device.
This is done by writing an "add" command directly to the /dev/zloop-control
device::

	$ modprobe zloop
        $ ls -l /dev/zloop*
        crw-------. 1 root root 10, 123 Jan  6 19:18 /dev/zloop-control

        $ mkdir -p <base directory/<device ID>
        $ echo "add [options]" > /dev/zloop-control

The options available for the add command can be listed by reading the
/dev/zloop-control device::

	$ cat /dev/zloop-control
        add id=%d,capacity_mb=%u,zone_size_mb=%u,zone_capacity_mb=%u,conv_zones=%u,base_dir=%s,nr_queues=%u,queue_depth=%u,buffered_io
        remove id=%d

In more details, the options that can be used with the "add" command are as
follows.

================   ===========================================================
id                 Device number (the X in /dev/zloopX).
                   Default: automatically assigned.
capacity_mb        Device total capacity in MiB. This is always rounded up to
                   the nearest higher multiple of the zone size.
                   Default: 16384 MiB (16 GiB).
zone_size_mb       Device zone size in MiB. Default: 256 MiB.
zone_capacity_mb   Device zone capacity (must always be equal to or lower than
                   the zone size. Default: zone size.
conv_zones         Total number of conventioanl zones starting from sector 0.
                   Default: 8.
base_dir           Path to the base directory where to create the directory
                   containing the zone files of the device.
                   Default=/var/local/zloop.
                   The device directory containing the zone files is always
                   named with the device ID. E.g. the default zone file
                   directory for /dev/zloop0 is /var/local/zloop/0.
nr_queues          Number of I/O queues of the zoned block device. This value is
                   always capped by the number of online CPUs
                   Default: 1
queue_depth        Maximum I/O queue depth per I/O queue.
                   Default: 64
buffered_io        Do buffered IOs instead of direct IOs (default: false)
================   ===========================================================

3) Deleting a Zoned Device
--------------------------

Deleting an unused zoned loop block device is done by issuing the "remove"
command to /dev/zloop-control, specifying the ID of the device to remove::

        $ echo "remove id=X" > /dev/zloop-control

The remove command does not have any option.

A zoned device that was removed can be re-added again without any change to the
state of the device zones: the device zones are restored to their last state
before the device was removed. Adding again a zoned device after it was removed
must always be done using the same configuration as when the device was first
added. If a zone configuration change is detected, an error will be returned and
the zoned device will not be created.

To fully delete a zoned device, after executing the remove operation, the device
base directory containing the backing files of the device zones must be deleted.

4) Example
----------

The following sequence of commands creates a 2GB zoned device with zones of 64
MB and a zone capacity of 63 MB::

        $ modprobe zloop
        $ mkdir -p /var/local/zloop/0
        $ echo "add capacity_mb=2048,zone_size_mb=64,zone_capacity=63MB" > /dev/zloop-control

For the device created (/dev/zloop0), the zone backing files are all created
under the default base directory (/var/local/zloop)::

        $ ls -l /var/local/zloop/0
        total 0
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000000
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000001
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000002
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000003
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000004
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000005
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000006
        -rw-------. 1 root root 67108864 Jan  6 22:23 cnv-000007
        -rw-------. 1 root root        0 Jan  6 22:23 seq-000008
        -rw-------. 1 root root        0 Jan  6 22:23 seq-000009
        ...

The zoned device created (/dev/zloop0) can then be used normally::

        $ lsblk -z
        NAME   ZONED        ZONE-SZ ZONE-NR ZONE-AMAX ZONE-OMAX ZONE-APP ZONE-WGRAN
        zloop0 host-managed     64M      32         0         0       1M         4K
        $ blkzone report /dev/zloop0
          start: 0x000000000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x000020000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x000040000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x000060000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x000080000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x0000a0000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x0000c0000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x0000e0000, len 0x020000, cap 0x020000, wptr 0x000000 reset:0 non-seq:0, zcond: 0(nw) [type: 1(CONVENTIONAL)]
          start: 0x000100000, len 0x020000, cap 0x01f800, wptr 0x000000 reset:0 non-seq:0, zcond: 1(em) [type: 2(SEQ_WRITE_REQUIRED)]
          start: 0x000120000, len 0x020000, cap 0x01f800, wptr 0x000000 reset:0 non-seq:0, zcond: 1(em) [type: 2(SEQ_WRITE_REQUIRED)]
          ...

Deleting this device is done using the command::

        $ echo "remove id=0" > /dev/zloop-control

The removed device can be re-added again using the same "add" command as when
the device was first created. To fully delete a zoned device, its backing files
should also be deleted after executing the remove command::

        $ rm -r /var/local/zloop/0
