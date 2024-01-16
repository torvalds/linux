==================================
Network Block Device (TCP version)
==================================

1) Overview
-----------

What is it: With this compiled in the kernel (or as a module), Linux
can use a remote server as one of its block devices. So every time
the client computer wants to read, e.g., /dev/nb0, it sends a
request over TCP to the server, which will reply with the data read.
This can be used for stations with low disk space (or even diskless)
to borrow disk space from another computer.
Unlike NFS, it is possible to put any filesystem on it, etc.

For more information, or to download the nbd-client and nbd-server
tools, go to http://nbd.sf.net/.

The nbd kernel module need only be installed on the client
system, as the nbd-server is completely in userspace. In fact,
the nbd-server has been successfully ported to other operating
systems, including Windows.

A) NBD parameters
-----------------

max_part
	Number of partitions per device (default: 0).

nbds_max
	Number of block devices that should be initialized (default: 16).
