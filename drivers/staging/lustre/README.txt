Lustre Parallel Filesystem Client
=================================

The Lustre file system is an open-source, parallel file system
that supports many requirements of leadership class HPC simulation
environments.
Born from from a research project at Carnegie Mellon University,
the Lustre file system is a widely-used option in HPC.
The Lustre file system provides a POSIX compliant file system interface,
can scale to thousands of clients, petabytes of storage and
hundreds of gigabytes per second of I/O bandwidth.

Unlike shared disk storage cluster filesystems (e.g. OCFS2, GFS, GPFS),
Lustre has independent Metadata and Data servers that clients can access
in parallel to maximize performance.

In order to use Lustre client you will need to download the "lustre-client"
package that contains the userspace tools from http://lustre.org/download/

You will need to install and configure your Lustre servers separately.

Mount Syntax
============
After you installed the lustre-client tools including mount.lustre binary
you can mount your Lustre filesystem with:

mount -t lustre mgs:/fsname mnt

where mgs is the host name or ip address of your Lustre MGS(management service)
fsname is the name of the filesystem you would like to mount.


Mount Options
=============

  noflock
	Disable posix file locking (Applications trying to use
	the functionality will get ENOSYS)

  localflock
	Enable local flock support, using only client-local flock
	(faster, for applications that require flock but do not run
	 on multiple nodes).

  flock
	Enable cluster-global posix file locking coherent across all
	client nodes.

  user_xattr, nouser_xattr
	Support "user." extended attributes (or not)

  user_fid2path, nouser_fid2path
	Enable FID to path translation by regular users (or not)

  checksum, nochecksum
	Verify data consistency on the wire and in memory as it passes
	between the layers (or not).

  lruresize, nolruresize
	Allow lock LRU to be controlled by memory pressure on the server
	(or only 100 (default, controlled by lru_size proc parameter) locks
	 per CPU per server on this client).

  lazystatfs, nolazystatfs
	Do not block in statfs() if some of the servers are down.

  32bitapi
	Shrink inode numbers to fit into 32 bits. This is necessary
	if you plan to reexport Lustre filesystem from this client via
	NFSv4.

  verbose, noverbose
	Enable mount/umount console messages (or not)

More Information
================
You can get more information at the Lustre website: http://wiki.lustre.org/

Source for the userspace tools and out-of-tree client and server code
is available at: http://git.hpdd.intel.com/fs/lustre-release.git

Latest binary packages:
http://lustre.org/download/
