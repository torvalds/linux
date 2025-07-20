.. SPDX-License-Identifier: GPL-2.0

===========================
SMB Direct - SMB3 over RDMA
===========================

This document describes how to set up the Linux SMB client and server to
use RDMA.

Overview
========
The Linux SMB kernel client supports SMB Direct, which is a transport
scheme for SMB3 that uses RDMA (Remote Direct Memory Access) to provide
high throughput and low latencies by bypassing the traditional TCP/IP
stack.
SMB Direct on the Linux SMB client can be tested against KSMBD - a
kernel-space SMB server.

Installation
=============
- Install an RDMA device. As long as the RDMA device driver is supported
  by the kernel, it should work. This includes both software emulators (soft
  RoCE, soft iWARP) and hardware devices (InfiniBand, RoCE, iWARP).

- Install a kernel with SMB Direct support. The first kernel release to
  support SMB Direct on both the client and server side is 5.15. Therefore,
  a distribution compatible with kernel 5.15 or later is required.

- Install cifs-utils, which provides the `mount.cifs` command to mount SMB
  shares.

- Configure the RDMA stack

  Make sure that your kernel configuration has RDMA support enabled. Under
  Device Drivers -> Infiniband support, update the kernel configuration to
  enable Infiniband support.

  Enable the appropriate IB HCA support or iWARP adapter support,
  depending on your hardware.

  If you are using InfiniBand, enable IP-over-InfiniBand support.

  For soft RDMA, enable either the soft iWARP (`RDMA _SIW`) or soft RoCE
  (`RDMA_RXE`) module. Install the `iproute2` package and use the
  `rdma link add` command to load the module and create an
  RDMA interface.

  e.g. if your local ethernet interface is `eth0`, you can use:

    .. code-block:: bash

        sudo rdma link add siw0 type siw netdev eth0

- Enable SMB Direct support for both the server and the client in the kernel
  configuration.

    Server Setup

    .. code-block:: text

        Network File Systems  --->
            <M> SMB3 server support
                [*] Support for SMB Direct protocol

    Client Setup

    .. code-block:: text

        Network File Systems  --->
            <M> SMB3 and CIFS support (advanced network filesystem)
                [*] SMB Direct support

- Build and install the kernel. SMB Direct support will be enabled in the
  cifs.ko and ksmbd.ko modules.

Setup and Usage
================

- Set up and start a KSMBD server as described in the `KSMBD documentation
  <https://www.kernel.org/doc/Documentation/filesystems/smb/ksmbd.rst>`_.
  Also add the "server multi channel support = yes" parameter to ksmbd.conf.

- On the client, mount the share with `rdma` mount option to use SMB Direct
  (specify a SMB version 3.0 or higher using `vers`).

  For example:

    .. code-block:: bash

        mount -t cifs //server/share /mnt/point -o vers=3.1.1,rdma

- To verify that the mount is using SMB Direct, you can check dmesg for the
  following log line after mounting:

    .. code-block:: text

        CIFS: VFS: RDMA transport established

  Or, verify `rdma` mount option for the share in `/proc/mounts`:

    .. code-block:: bash

        cat /proc/mounts | grep cifs
