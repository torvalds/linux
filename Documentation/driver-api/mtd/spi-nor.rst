=================
SPI NOR framework
=================

How to propose a new flash addition
-----------------------------------

Most SPI NOR flashes comply with the JEDEC JESD216
Serial Flash Discoverable Parameter (SFDP) standard. SFDP describes
the functional and feature capabilities of serial flash devices in a
standard set of internal read-only parameter tables.

The SPI NOR driver queries the SFDP tables in order to determine the
flash's parameters and settings. If the flash defines the SFDP tables
it's likely that you won't need a flash entry at all, and instead
rely on the generic flash driver which probes the flash solely based
on its SFDP data. All one has to do is to specify the "jedec,spi-nor"
compatible in the device tree.

There are cases however where you need to define an explicit flash
entry. This typically happens when the flash has settings or support
that is not covered by the SFDP tables (e.g. Block Protection), or
when the flash contains mangled SFDP data. If the later, one needs
to implement the ``spi_nor_fixups`` hooks in order to amend the SFDP
parameters with the correct values.

Minimum testing requirements
-----------------------------

Do all the tests from below and paste them in the commit's comments
section, after the ``---`` marker.

1) Specify the controller that you used to test the flash and specify
   the frequency at which the flash was operated, e.g.::

    This flash is populated on the X board and was tested at Y
    frequency using the Z (put compatible) SPI controller.

2) Dump the sysfs entries and print the md5/sha1/sha256 SFDP checksum::

    root@1:~# cat /sys/bus/spi/devices/spi0.0/spi-nor/partname
    sst26vf064b
    root@1:~# cat /sys/bus/spi/devices/spi0.0/spi-nor/jedec_id
    bf2643
    root@1:~# cat /sys/bus/spi/devices/spi0.0/spi-nor/manufacturer
    sst
    root@1:~# xxd -p /sys/bus/spi/devices/spi0.0/spi-nor/sfdp
    53464450060102ff00060110300000ff81000106000100ffbf0001180002
    0001fffffffffffffffffffffffffffffffffd20f1ffffffff0344eb086b
    083b80bbfeffffffffff00ffffff440b0c200dd80fd810d820914824806f
    1d81ed0f773830b030b0f7ffffff29c25cfff030c080ffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffff0004fff37f0000f57f0000f9ff
    7d00f57f0000f37f0000ffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
    ffffbf2643ffb95ffdff30f260f332ff0a122346ff0f19320f1919ffffff
    ffffffff00669938ff05013506040232b03072428de89888a585c09faf5a
    ffff06ec060c0003080bffffffffff07ffff0202ff060300fdfd040700fc
    0300fefe0202070e
    root@1:~# sha256sum /sys/bus/spi/devices/spi0.0/spi-nor/sfdp
    428f34d0461876f189ac97f93e68a05fa6428c6650b3b7baf736a921e5898ed1  /sys/bus/spi/devices/spi0.0/spi-nor/sfdp

   Please dump the SFDP tables using ``xxd -p``. It enables us to do
   the reverse operation and convert the hexdump to binary with
   ``xxd -rp``. Dumping the SFDP data with ``hexdump -Cv`` is accepted,
   but less desirable.

3) Dump debugfs data::

    root@1:~# cat /sys/kernel/debug/spi-nor/spi0.0/capabilities
    Supported read modes by the flash
     1S-1S-1S
      opcode		0x03
      mode cycles	0
      dummy cycles	0
     1S-1S-1S (fast read)
      opcode		0x0b
      mode cycles	0
      dummy cycles	8
     1S-1S-2S
      opcode		0x3b
      mode cycles	0
      dummy cycles	8
     1S-2S-2S
      opcode		0xbb
      mode cycles	4
      dummy cycles	0
     1S-1S-4S
      opcode		0x6b
      mode cycles	0
      dummy cycles	8
     1S-4S-4S
      opcode		0xeb
      mode cycles	2
      dummy cycles	4
     4S-4S-4S
      opcode		0x0b
      mode cycles	2
      dummy cycles	4

    Supported page program modes by the flash
     1S-1S-1S
      opcode	0x02

    root@1:~# cat /sys/kernel/debug/spi-nor/spi0.0/params
    name		sst26vf064b
    id			bf 26 43 bf 26 43
    size		8.00 MiB
    write size		1
    page size		256
    address nbytes	3
    flags		HAS_LOCK | HAS_16BIT_SR | SOFT_RESET | SWP_IS_VOLATILE

    opcodes
     read		0xeb
      dummy cycles	6
     erase		0x20
     program		0x02
     8D extension	none

    protocols
     read		1S-4S-4S
     write		1S-1S-1S
     register		1S-1S-1S

    erase commands
     20 (4.00 KiB) [0]
     d8 (8.00 KiB) [1]
     d8 (32.0 KiB) [2]
     d8 (64.0 KiB) [3]
     c7 (8.00 MiB)

    sector map
     region (in hex)   | erase mask | flags
     ------------------+------------+----------
     00000000-00007fff |     [01  ] |
     00008000-0000ffff |     [0 2 ] |
     00010000-007effff |     [0  3] |
     007f0000-007f7fff |     [0 2 ] |
     007f8000-007fffff |     [01  ] |

4) Use `mtd-utils <https://git.infradead.org/mtd-utils.git>`__
   and verify that erase, read and page program operations work fine::

    root@1:~# dd if=/dev/urandom of=./spi_test bs=1M count=2
    2+0 records in
    2+0 records out
    2097152 bytes (2.1 MB, 2.0 MiB) copied, 0.848566 s, 2.5 MB/s

    root@1:~# mtd_debug erase /dev/mtd0 0 2097152
    Erased 2097152 bytes from address 0x00000000 in flash

    root@1:~# mtd_debug read /dev/mtd0 0 2097152 spi_read
    Copied 2097152 bytes from address 0x00000000 in flash to spi_read

    root@1:~# hexdump spi_read
    0000000 ffff ffff ffff ffff ffff ffff ffff ffff
    *
    0200000

    root@1:~# sha256sum spi_read
    4bda3a28f4ffe603c0ec1258c0034d65a1a0d35ab7bd523a834608adabf03cc5  spi_read

    root@1:~# mtd_debug write /dev/mtd0 0 2097152 spi_test
    Copied 2097152 bytes from spi_test to address 0x00000000 in flash

    root@1:~# mtd_debug read /dev/mtd0 0 2097152 spi_read
    Copied 2097152 bytes from address 0x00000000 in flash to spi_read

    root@1:~# sha256sum spi*
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_read
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_test

   If the flash comes erased by default and the previous erase was ignored,
   we won't catch it, thus test the erase again::

    root@1:~# mtd_debug erase /dev/mtd0 0 2097152
    Erased 2097152 bytes from address 0x00000000 in flash

    root@1:~# mtd_debug read /dev/mtd0 0 2097152 spi_read
    Copied 2097152 bytes from address 0x00000000 in flash to spi_read

    root@1:~# sha256sum spi*
    4bda3a28f4ffe603c0ec1258c0034d65a1a0d35ab7bd523a834608adabf03cc5  spi_read
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_test

   Dump some other relevant data::

    root@1:~# mtd_debug info /dev/mtd0
    mtd.type = MTD_NORFLASH
    mtd.flags = MTD_CAP_NORFLASH
    mtd.size = 8388608 (8M)
    mtd.erasesize = 4096 (4K)
    mtd.writesize = 1
    mtd.oobsize = 0
    regions = 0

5) If your flash supports locking, please go through the following test
   procedure to make sure it correctly behaves. The below example
   expects the typical situation where eraseblocks and lock sectors have
   the same size. In case you enabled MTD_SPI_NOR_USE_4K_SECTORS, you
   must adapt `bs` accordingly.

   Warning: These tests may hard lock your device! Make sure:

   - The device is not hard locked already (#WP strapped to low and
     SR_SRWD bit set)
   - If you have a WPn pin, you may want to set `no-wp` in your DT for
     the time of the test, to only make use of software protection.
     Otherwise, clearing the locking state depends on the WPn
     signal and if it is tied to low, the flash will be permanently
     locked.

   Test full chip locking and make sure expectations, the MEMISLOCKED
   ioctl output, the debugfs output and experimental results are all
   aligned::

    root@1:~# alias show_sectors='grep -A4 "locked sectors" /sys/kernel/debug/spi-nor/spi0.0/params'
    root@1:~# flash_lock -u /dev/mtd0
    root@1:~# flash_lock -i /dev/mtd0
    Device: /dev/mtd0
    Start: 0
    Len: 0x4000000
    Lock status: unlocked
    Return code: 0
    root@1:~# mtd_debug erase /dev/mtd0 0 2097152
    Erased 2097152 bytes from address 0x00000000 in flash
    root@1:~# mtd_debug write /dev/mtd0 0 2097152 spi_test
    Copied 2097152 bytes from spi_test to address 0x00000000 in flash
    root@1:~# mtd_debug read /dev/mtd0 0 2097152 spi_read
    Copied 2097152 bytes from address 0x00000000 in flash to spi_read
    root@1:~# sha256sum spi*
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_read
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_test
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-03ffffff | unlocked | 1024

    root@1:~# flash_lock -l /dev/mtd0
    root@1:~# flash_lock -i /dev/mtd0
    Device: /dev/mtd0
    Start: 0
    Len: 0x4000000
    Lock status: locked
    Return code: 1
    root@1:~# mtd_debug erase /dev/mtd0 0 2097152
    Erased 2097152 bytes from address 0x00000000 in flash
    root@1:~# mtd_debug read /dev/mtd0 0 2097152 spi_read
    Copied 2097152 bytes from address 0x00000000 in flash to spi_read
    root@1:~# sha256sum spi*
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_read
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_test
    root@1:~# dd if=/dev/urandom of=./spi_test2 bs=1M count=2
    2+0 records in
    2+0 records out
    root@1:~# mtd_debug write /dev/mtd0 0 2097152 spi_test2
    Copied 2097152 bytes from spi_test2 to address 0x00000000 in flash
    root@1:~# mtd_debug read /dev/mtd0 0 2097152 spi_read2
    Copied 2097152 bytes from address 0x00000000 in flash to spi_read2
    root@1:~# sha256sum spi*
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_read
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_read2
    c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_test
    bea9334df51c620440f86751cba0799214a016329f1736f9456d40cf40efdc88  spi_test2
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-03ffffff |   locked | 1024
    root@1:~# flash_lock -u /dev/mtd0

   Once we trust the debugfs output we can use it to test various
   situations. Check top locking/unlocking (end of the device)::

    root@1:~# size=$(cat /sys/class/mtd/mtd0/size)
    root@1:~# bs=$(cat /sys/class/mtd/mtd0/erasesize)
    root@1:~# nsectors=$(grep unlocked /sys/kernel/debug/spi-nor/spi0.0/params | sed -e 's/.*unlocked | //')
    root@1:~# ss=$(($size / $nsectors))
    root@1:~# bps=$(($ss / $bs))

    root@1:~# flash_lock -u /dev/mtd0
    root@1:~# flash_lock -l /dev/mtd0 $(($size - (2 * $ss))) $((2 * $bps)) # last two
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-03fdffff | unlocked | 1022
     03fe0000-03ffffff |   locked | 2
    root@1:~# flash_lock -u /dev/mtd0 $(($size - (2 * $ss))) $((1 * $bps)) # last one
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-03feffff | unlocked | 1023
     03ff0000-03ffffff |   locked | 1

   If the flash features 4 block protection bits (BP), we can protect
   more than 4MB (typically 128 64kiB-blocks or more), with a finer
   grain than locking the entire device::

    root@1:~# flash_lock -u /dev/mtd0
    root@1:~# flash_lock -l /dev/mtd0 $(($size - (2**7 * $ss))) $((2**7 * $bps))
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-037fffff | unlocked | 896
     03800000-03ffffff |   locked | 128

   If the flash features a Top/Bottom (TB) bit, we can protect the
   beginning of the flash::

    root@1:~# flash_lock -u /dev/mtd0
    root@1:~# flash_lock -l /dev/mtd0 0 $((2 * $bps)) # first two
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-0001ffff |   locked | 2
     00020000-03ffffff | unlocked | 1022
    root@1:~# flash_lock -u /dev/mtd0 $ss $((1 * $bps)) # first one
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-0000ffff |   locked | 1
     00010000-03ffffff | unlocked | 1023

   If the flash features a Complement (CMP) bit, we can protect with
   more granularity above half of the capacity. Let's lock all but one
   block, then unlock one more block::

    root@1:~# all_but_one=$((($size / $bs) - ($ss / $bs)))

    root@1:~# flash_lock -u /dev/mtd0
    root@1:~# flash_lock -l /dev/mtd0 $ss $all_but_one # all but the first
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-0000ffff | unlocked | 1
     00010000-03ffffff |   locked | 1023
    root@1:~# flash_lock -u /dev/mtd0 $ss $(($ss / $bs)) # all but the two first
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-0001ffff | unlocked | 2
     00020000-03ffffff |   locked | 1022
    root@1:~# flash_lock -u /dev/mtd0
    root@1:~# flash_lock -l /dev/mtd0 0 $all_but_one # same from the other side
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-03feffff |   locked | 1023
     03ff0000-03ffffff | unlocked | 1
    root@1:~# flash_lock -u /dev/mtd0 $(($size - (2 * $ss))) $(($ss / $bs)) # all but two
    root@1:~# show_sectors
    software locked sectors
     region (in hex)   | status   | #sectors
     ------------------+----------+---------
     00000000-03fdffff |   locked | 1022
     03fe0000-03ffffff | unlocked | 2
