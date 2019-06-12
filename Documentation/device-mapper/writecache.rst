=================
Writecache target
=================

The writecache target caches writes on persistent memory or on SSD. It
doesn't cache reads because reads are supposed to be cached in page cache
in normal RAM.

When the device is constructed, the first sector should be zeroed or the
first sector should contain valid superblock from previous invocation.

Constructor parameters:

1. type of the cache device - "p" or "s"

	- p - persistent memory
	- s - SSD
2. the underlying device that will be cached
3. the cache device
4. block size (4096 is recommended; the maximum block size is the page
   size)
5. the number of optional parameters (the parameters with an argument
   count as two)

	start_sector n		(default: 0)
		offset from the start of cache device in 512-byte sectors
	high_watermark n	(default: 50)
		start writeback when the number of used blocks reach this
		watermark
	low_watermark x		(default: 45)
		stop writeback when the number of used blocks drops below
		this watermark
	writeback_jobs n	(default: unlimited)
		limit the number of blocks that are in flight during
		writeback. Setting this value reduces writeback
		throughput, but it may improve latency of read requests
	autocommit_blocks n	(default: 64 for pmem, 65536 for ssd)
		when the application writes this amount of blocks without
		issuing the FLUSH request, the blocks are automatically
		commited
	autocommit_time ms	(default: 1000)
		autocommit time in milliseconds. The data is automatically
		commited if this time passes and no FLUSH request is
		received
	fua			(by default on)
		applicable only to persistent memory - use the FUA flag
		when writing data from persistent memory back to the
		underlying device
	nofua
		applicable only to persistent memory - don't use the FUA
		flag when writing back data and send the FLUSH request
		afterwards

		- some underlying devices perform better with fua, some
		  with nofua. The user should test it

Status:
1. error indicator - 0 if there was no error, otherwise error number
2. the number of blocks
3. the number of free blocks
4. the number of blocks under writeback

Messages:
	flush
		flush the cache device. The message returns successfully
		if the cache device was flushed without an error
	flush_on_suspend
		flush the cache device on next suspend. Use this message
		when you are going to remove the cache device. The proper
		sequence for removing the cache device is:

		1. send the "flush_on_suspend" message
		2. load an inactive table with a linear target that maps
		   to the underlying device
		3. suspend the device
		4. ask for status and verify that there are no errors
		5. resume the device, so that it will use the linear
		   target
		6. the cache device is now inactive and it can be deleted
