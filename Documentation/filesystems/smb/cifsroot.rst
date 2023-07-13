.. SPDX-License-Identifier: GPL-2.0

===========================================
Mounting root file system via SMB (cifs.ko)
===========================================

Written 2019 by Paulo Alcantara <palcantara@suse.de>

Written 2019 by Aurelien Aptel <aaptel@suse.com>

The CONFIG_CIFS_ROOT option enables experimental root file system
support over the SMB protocol via cifs.ko.

It introduces a new kernel command-line option called 'cifsroot='
which will tell the kernel to mount the root file system over the
network by utilizing SMB or CIFS protocol.

In order to mount, the network stack will also need to be set up by
using 'ip=' config option. For more details, see
Documentation/admin-guide/nfs/nfsroot.rst.

A CIFS root mount currently requires the use of SMB1+UNIX Extensions
which is only supported by the Samba server. SMB1 is the older
deprecated version of the protocol but it has been extended to support
POSIX features (See [1]). The equivalent extensions for the newer
recommended version of the protocol (SMB3) have not been fully
implemented yet which means SMB3 doesn't support some required POSIX
file system objects (e.g. block devices, pipes, sockets).

As a result, a CIFS root will default to SMB1 for now but the version
to use can nonetheless be changed via the 'vers=' mount option.  This
default will change once the SMB3 POSIX extensions are fully
implemented.

Server configuration
====================

To enable SMB1+UNIX extensions you will need to set these global
settings in Samba smb.conf::

    [global]
    server min protocol = NT1
    unix extension = yes        # default

Kernel command line
===================

::

    root=/dev/cifs

This is just a virtual device that basically tells the kernel to mount
the root file system via SMB protocol.

::

    cifsroot=//<server-ip>/<share>[,options]

Enables the kernel to mount the root file system via SMB that are
located in the <server-ip> and <share> specified in this option.

The default mount options are set in fs/smb/client/cifsroot.c.

server-ip
	IPv4 address of the server.

share
	Path to SMB share (rootfs).

options
	Optional mount options. For more information, see mount.cifs(8).

Examples
========

Export root file system as a Samba share in smb.conf file::

    ...
    [linux]
	    path = /path/to/rootfs
	    read only = no
	    guest ok = yes
	    force user = root
	    force group = root
	    browseable = yes
	    writeable = yes
	    admin users = root
	    public = yes
	    create mask = 0777
	    directory mask = 0777
    ...

Restart smb service::

    # systemctl restart smb

Test it under QEMU on a kernel built with CONFIG_CIFS_ROOT and
CONFIG_IP_PNP options enabled::

    # qemu-system-x86_64 -enable-kvm -cpu host -m 1024 \
    -kernel /path/to/linux/arch/x86/boot/bzImage -nographic \
    -append "root=/dev/cifs rw ip=dhcp cifsroot=//10.0.2.2/linux,username=foo,password=bar console=ttyS0 3"


1: https://wiki.samba.org/index.php/UNIX_Extensions
