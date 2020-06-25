======
dm-ebs
======


This target is similar to the linear target except that it emulates
a smaller logical block size on a device with a larger logical block
size.  Its main purpose is to provide emulation of 512 byte sectors on
devices that do not provide this emulation (i.e. 4K native disks).

Supported emulated logical block sizes 512, 1024, 2048 and 4096.

Underlying block size can be set to > 4K to test buffering larger units.


Table parameters
----------------
  <dev path> <offset> <emulated sectors> [<underlying sectors>]

Mandatory parameters:

    <dev path>:
        Full pathname to the underlying block-device,
        or a "major:minor" device-number.
    <offset>:
        Starting sector within the device;
        has to be a multiple of <emulated sectors>.
    <emulated sectors>:
        Number of sectors defining the logical block size to be emulated;
        1, 2, 4, 8 sectors of 512 bytes supported.

Optional parameter:

    <underyling sectors>:
        Number of sectors defining the logical block size of <dev path>.
        2^N supported, e.g. 8 = emulate 8 sectors of 512 bytes = 4KiB.
        If not provided, the logical block size of <dev path> will be used.


Examples:

Emulate 1 sector = 512 bytes logical block size on /dev/sda starting at
offset 1024 sectors with underlying devices block size automatically set:

ebs /dev/sda 1024 1

Emulate 2 sector = 1KiB logical block size on /dev/sda starting at
offset 128 sectors, enforce 2KiB underlying device block size.
This presumes 2KiB logical blocksize on /dev/sda or less to work:

ebs /dev/sda 128 2 4
