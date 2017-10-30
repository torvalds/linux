zram: Compressed RAM based block devices
----------------------------------------

* Introduction

The zram module creates RAM based block devices named /dev/zram<id>
(<id> = 0, 1, ...). Pages written to these disks are compressed and stored
in memory itself. These disks allow very fast I/O and compression provides
good amounts of memory savings. Some of the usecases include /tmp storage,
use as swap disks, various caches under /var and maybe many more :)

Statistics for individual zram devices are exported through sysfs nodes at
/sys/block/zram<id>/

* Usage

There are several ways to configure and manage zram device(-s):
a) using zram and zram_control sysfs attributes
b) using zramctl utility, provided by util-linux (util-linux@vger.kernel.org).

In this document we will describe only 'manual' zram configuration steps,
IOW, zram and zram_control sysfs attributes.

In order to get a better idea about zramctl please consult util-linux
documentation, zramctl man-page or `zramctl --help'. Please be informed
that zram maintainers do not develop/maintain util-linux or zramctl, should
you have any questions please contact util-linux@vger.kernel.org

Following shows a typical sequence of steps for using zram.

WARNING
=======
For the sake of simplicity we skip error checking parts in most of the
examples below. However, it is your sole responsibility to handle errors.

zram sysfs attributes always return negative values in case of errors.
The list of possible return codes:
-EBUSY	-- an attempt to modify an attribute that cannot be changed once
the device has been initialised. Please reset device first;
-ENOMEM	-- zram was not able to allocate enough memory to fulfil your
needs;
-EINVAL	-- invalid input has been provided.

If you use 'echo', the returned value that is changed by 'echo' utility,
and, in general case, something like:

	echo 3 > /sys/block/zram0/max_comp_streams
	if [ $? -ne 0 ];
		handle_error
	fi

should suffice.

1) Load Module:
	modprobe zram num_devices=4
	This creates 4 devices: /dev/zram{0,1,2,3}

num_devices parameter is optional and tells zram how many devices should be
pre-created. Default: 1.

2) Set max number of compression streams
Regardless the value passed to this attribute, ZRAM will always
allocate multiple compression streams - one per online CPUs - thus
allowing several concurrent compression operations. The number of
allocated compression streams goes down when some of the CPUs
become offline. There is no single-compression-stream mode anymore,
unless you are running a UP system or has only 1 CPU online.

To find out how many streams are currently available:
	cat /sys/block/zram0/max_comp_streams

3) Select compression algorithm
Using comp_algorithm device attribute one can see available and
currently selected (shown in square brackets) compression algorithms,
change selected compression algorithm (once the device is initialised
there is no way to change compression algorithm).

Examples:
	#show supported compression algorithms
	cat /sys/block/zram0/comp_algorithm
	lzo [lz4]

	#select lzo compression algorithm
	echo lzo > /sys/block/zram0/comp_algorithm

For the time being, the `comp_algorithm' content does not necessarily
show every compression algorithm supported by the kernel. We keep this
list primarily to simplify device configuration and one can configure
a new device with a compression algorithm that is not listed in
`comp_algorithm'. The thing is that, internally, ZRAM uses Crypto API
and, if some of the algorithms were built as modules, it's impossible
to list all of them using, for instance, /proc/crypto or any other
method. This, however, has an advantage of permitting the usage of
custom crypto compression modules (implementing S/W or H/W compression).

4) Set Disksize
Set disk size by writing the value to sysfs node 'disksize'.
The value can be either in bytes or you can use mem suffixes.
Examples:
	# Initialize /dev/zram0 with 50MB disksize
	echo $((50*1024*1024)) > /sys/block/zram0/disksize

	# Using mem suffixes
	echo 256K > /sys/block/zram0/disksize
	echo 512M > /sys/block/zram0/disksize
	echo 1G > /sys/block/zram0/disksize

Note:
There is little point creating a zram of greater than twice the size of memory
since we expect a 2:1 compression ratio. Note that zram uses about 0.1% of the
size of the disk when not in use so a huge zram is wasteful.

5) Set memory limit: Optional
Set memory limit by writing the value to sysfs node 'mem_limit'.
The value can be either in bytes or you can use mem suffixes.
In addition, you could change the value in runtime.
Examples:
	# limit /dev/zram0 with 50MB memory
	echo $((50*1024*1024)) > /sys/block/zram0/mem_limit

	# Using mem suffixes
	echo 256K > /sys/block/zram0/mem_limit
	echo 512M > /sys/block/zram0/mem_limit
	echo 1G > /sys/block/zram0/mem_limit

	# To disable memory limit
	echo 0 > /sys/block/zram0/mem_limit

6) Activate:
	mkswap /dev/zram0
	swapon /dev/zram0

	mkfs.ext4 /dev/zram1
	mount /dev/zram1 /tmp

7) Add/remove zram devices

zram provides a control interface, which enables dynamic (on-demand) device
addition and removal.

In order to add a new /dev/zramX device, perform read operation on hot_add
attribute. This will return either new device's device id (meaning that you
can use /dev/zram<id>) or error code.

Example:
	cat /sys/class/zram-control/hot_add
	1

To remove the existing /dev/zramX device (where X is a device id)
execute
	echo X > /sys/class/zram-control/hot_remove

8) Stats:
Per-device statistics are exported as various nodes under /sys/block/zram<id>/

A brief description of exported device attributes. For more details please
read Documentation/ABI/testing/sysfs-block-zram.

Name            access            description
----            ------            -----------
disksize          RW    show and set the device's disk size
initstate         RO    shows the initialization state of the device
reset             WO    trigger device reset
mem_used_max      WO    reset the `mem_used_max' counter (see later)
mem_limit         WO    specifies the maximum amount of memory ZRAM can use
                        to store the compressed data
max_comp_streams  RW    the number of possible concurrent compress operations
comp_algorithm    RW    show and change the compression algorithm
compact           WO    trigger memory compaction
debug_stat        RO    this file is used for zram debugging purposes
backing_dev	  RW	set up backend storage for zram to write out


User space is advised to use the following files to read the device statistics.

File /sys/block/zram<id>/stat

Represents block layer statistics. Read Documentation/block/stat.txt for
details.

File /sys/block/zram<id>/io_stat

The stat file represents device's I/O statistics not accounted by block
layer and, thus, not available in zram<id>/stat file. It consists of a
single line of text and contains the following stats separated by
whitespace:
 failed_reads     the number of failed reads
 failed_writes    the number of failed writes
 invalid_io       the number of non-page-size-aligned I/O requests
 notify_free      Depending on device usage scenario it may account
                  a) the number of pages freed because of swap slot free
                  notifications or b) the number of pages freed because of
                  REQ_DISCARD requests sent by bio. The former ones are
                  sent to a swap block device when a swap slot is freed,
                  which implies that this disk is being used as a swap disk.
                  The latter ones are sent by filesystem mounted with
                  discard option, whenever some data blocks are getting
                  discarded.

File /sys/block/zram<id>/mm_stat

The stat file represents device's mm statistics. It consists of a single
line of text and contains the following stats separated by whitespace:
 orig_data_size   uncompressed size of data stored in this disk.
		  This excludes same-element-filled pages (same_pages) since
		  no memory is allocated for them.
                  Unit: bytes
 compr_data_size  compressed size of data stored in this disk
 mem_used_total   the amount of memory allocated for this disk. This
                  includes allocator fragmentation and metadata overhead,
                  allocated for this disk. So, allocator space efficiency
                  can be calculated using compr_data_size and this statistic.
                  Unit: bytes
 mem_limit        the maximum amount of memory ZRAM can use to store
                  the compressed data
 mem_used_max     the maximum amount of memory zram have consumed to
                  store the data
 same_pages       the number of same element filled pages written to this disk.
                  No memory is allocated for such pages.
 pages_compacted  the number of pages freed during compaction

9) Deactivate:
	swapoff /dev/zram0
	umount /dev/zram1

10) Reset:
	Write any positive value to 'reset' sysfs node
	echo 1 > /sys/block/zram0/reset
	echo 1 > /sys/block/zram1/reset

	This frees all the memory allocated for the given device and
	resets the disksize to zero. You must set the disksize again
	before reusing the device.

* Optional Feature

= writeback

With incompressible pages, there is no memory saving with zram.
Instead, with CONFIG_ZRAM_WRITEBACK, zram can write incompressible page
to backing storage rather than keeping it in memory.
User should set up backing device via /sys/block/zramX/backing_dev
before disksize setting.

Nitin Gupta
ngupta@vflare.org
