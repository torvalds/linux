
==================================
pNFS SCSI layout server user guide
==================================

This document describes support for pNFS SCSI layouts in the Linux NFS server.
With pNFS SCSI layouts, the NFS server acts as Metadata Server (MDS) for pNFS,
which in addition to handling all the metadata access to the NFS export,
also hands out layouts to the clients so that they can directly access the
underlying SCSI LUNs that are shared with the client.

To use pNFS SCSI layouts with the Linux NFS server, the exported file
system needs to support the pNFS SCSI layouts (currently just XFS), and the
file system must sit on a SCSI LUN that is accessible to the clients in
addition to the MDS.  As of now the file system needs to sit directly on the
exported LUN, striping or concatenation of LUNs on the MDS and clients
is not supported yet.

On a server built with CONFIG_NFSD_SCSILAYOUT, the pNFS SCSI volume support is
automatically enabled if the file system is exported using the "pnfs"
option and the underlying SCSI device support persistent reservations.
On the client make sure the kernel has the CONFIG_PNFS_BLOCK option
enabled, and the file system is mounted using the NFSv4.1 protocol
version (mount -o vers=4.1).

If the nfsd server needs to fence a non-responding client and the
fencing operation fails, the server logs a warning message in the
system log with the following format:

    FENCE failed client[IP_address] clid[#n] device[dev_name]

    where:

    - IP_address: refers to the IP address of the affected client.
    - #n: indicates the unique client identifier.
    - dev_name: specifies the name of the block device related
      to the fencing attempt.

The server will repeatedly retry the operation indefinitely. During
this time, access to the affected file is restricted for all other
clients. This is to prevent potential data corruption if multiple
clients access the same file simultaneously.

To restore access to the affected file for other clients, the admin
needs to take the following actions:

    - shutdown or power off the client being fenced.
    - manually expire the client to release all its state on the server::

        echo 'expire' > /proc/fs/nfsd/clients/clid/ctl

    where:

      - clid: is the unique client identifier displayed in the system log.

