.. SPDX-License-Identifier: GPL-2.0

============================
Configurable Error Injection
============================

Overview
--------

Configurable error injection allows injecting specific block layer status codes
for sector ranges of a block device.  Errors can be injected unconditionally, or
with a given probability.

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
			code, e.g. IOERR or MEDIUM. Mandatory.
start=<number>		First block layer sector the rule applies to.
			Optional, defaults to 0.
nr_sectors=<number>	Number of sectors this rule applies.
			Optional, defaults to the remainder of the device.
chance=<number>		Only return a failure with a likelihood of 1/chance.
			Optional, defaults to 1 (always).
===================	=======================================================

Example
-------

Return BLK_STS_IOERR for one in 10 reads of sector 0 of /dev/nvme0n1:

	$ echo 'add,op=READ,start=0,status=IOERR,chance=10' > /sys/kernel/debug/block/nvme0n1/error_injection

Return BLK_STS_MEDIUM for every write to /dev/nvme0n1:

	$ echo 'add,op=WRITE,start=0,status=MEDIUM' > /sys/kernel/debug/block/nvme0n1/error_injection

Remove all rules for /dev/nvme0n1:

	$ echo 'removeall' > /sys/kernel/debug/block/nvme0n1/error_injection
