==============================================
Embedded device command line partition parsing
==============================================

The "blkdevparts" command line option adds support for reading the
block device partition table from the kernel command line.

It is typically used for fixed block (eMMC) embedded devices.
It has no MBR, so saves storage space. Bootloader can be easily accessed
by absolute address of data on the block device.
Users can easily change the partition.

The format for the command line is just like mtdparts:

blkdevparts=<blkdev-def>[;<blkdev-def>]
  <blkdev-def> := <blkdev-id>:<partdef>[,<partdef>]
    <partdef> := <size>[@<offset>](part-name)

<blkdev-id>
    block device disk name. Embedded device uses fixed block device.
    Its disk name is also fixed, such as: mmcblk0, mmcblk1, mmcblk0boot0.

<size>
    partition size, in bytes, such as: 512, 1m, 1G.
    size may contain an optional suffix of (upper or lower case):

      K, M, G, T, P, E.

    "-" is used to denote all remaining space.

<offset>
    partition start address, in bytes.
    offset may contain an optional suffix of (upper or lower case):

      K, M, G, T, P, E.

(part-name)
    partition name. Kernel sends uevent with "PARTNAME". Application can
    create a link to block device partition with the name "PARTNAME".
    User space application can access partition by partition name.

Example:

    eMMC disk names are "mmcblk0" and "mmcblk0boot0".

  bootargs::

    'blkdevparts=mmcblk0:1G(data0),1G(data1),-;mmcblk0boot0:1m(boot),-(kernel)'

  dmesg::

    mmcblk0: p1(data0) p2(data1) p3()
    mmcblk0boot0: p1(boot) p2(kernel)
