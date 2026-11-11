.. SPDX-License-Identifier: GPL-2.0

============================
Configurable Error Injection
============================

Overview
--------

Configurable error injection allows injecting delays and/or specific block
layer status codes for sector ranges of a block device by adding rules.  The
rules can be configured to trigger unconditionally, or with a given probability.

To use configurable error injection, CONFIG_BLK_ERROR_INJECTION must be enabled.

The only interface is the error_injection debugfs file, which is created for
each registered gendisk.  Writes to this file are used to create or delete rules
and reads return a list of the current error injection sites.

Options
-------

The following options specify the operations:

===================	=======================================================
add			add a new rule
removeall		remove all existing rules
===================	=======================================================

The following options specify the details of the rule for the add operation:

===================	=======================================================
op=<string>		block layer operation this rule applies to.  This uses
			the XYZ for each REQ_OP_XYZ operation, e.g. READ, WRITE
			or DISCARD. Mandatory.
status=<string>		Status to return.  This uses XYZ for each BLK_STS_XYZ
			code, e.g. IOERR or MEDIUM.  Mandatory unless delay_us
			is given.
start=<number>		First block layer sector the rule applies to.
			Optional, defaults to 0.
nr_sectors=<number>	Number of sectors this rule applies.
			Optional, defaults to the remainder of the device.
chance=<number>		Only apply the rule with a likelihood of 1/chance.
			Optional, defaults to 1 (always).
delay_us=<number>	Hold the bio back for this many microseconds.  Without
			status the bio is then submitted to the device as
			usual.  With status it is failed once the delay has
			expired.  Bios with REQ_NOWAIT set fail with
			BLK_STS_AGAIN.  Optional, defaults to 0 (no delay).
			Values above 600 seconds are rejected.
===================	=======================================================

Delays
------

A delayed bio is held above the driver, so the device never sees a slow I/O.
A delay does not reach the blk-mq timeout handler or SCSI error handling.

A bio that matched a delay rule is not evaluated against the rules again, even
after it is split and resubmitted internally.  Removing a rule does not release
bios it is already delaying.

Holding a bio back reorders it against bios submitted later.  On zoned devices
this breaks sequential write ordering and the block layer fails the
out-of-order writes, so only delay reads there.

Example
-------

Return BLK_STS_IOERR for one in 10 reads of sector 0 of /dev/nvme0n1:

	$ echo 'add,op=READ,start=0,status=IOERR,chance=10' > /sys/kernel/debug/block/nvme0n1/error_injection

Return BLK_STS_MEDIUM for every write to /dev/nvme0n1:

	$ echo 'add,op=WRITE,start=0,status=MEDIUM' > /sys/kernel/debug/block/nvme0n1/error_injection

Delay every read of /dev/nvme0n1 by 10 milliseconds, then issue it normally:

	$ echo 'add,op=READ,delay_us=10000' > /sys/kernel/debug/block/nvme0n1/error_injection

Fail one in 100 writes with BLK_STS_TIMEOUT, but only after 30 seconds:

	$ echo 'add,op=WRITE,status=TIMEOUT,chance=100,delay_us=30000000' > /sys/kernel/debug/block/nvme0n1/error_injection

Remove all rules for /dev/nvme0n1:

	$ echo 'removeall' > /sys/kernel/debug/block/nvme0n1/error_injection
