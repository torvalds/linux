********************************
RDMA Network Block Device (RNBD)
********************************

Introduction
------------

RNBD (RDMA Network Block Device) is a pair of kernel modules
(client and server) that allow for remote access of a block device on
the server over RTRS protocol using the RDMA (InfiniBand, RoCE, iWARP)
transport. After being mapped, the remote block devices can be accessed
on the client side as local block devices.

I/O is transferred between client and server by the RTRS transport
modules. The administration of RNBD and RTRS modules is done via
sysfs entries.

Requirements
------------

  RTRS kernel modules

Quick Start
-----------

Server side:
  # modprobe rnbd_server

Client side:
  # modprobe rnbd_client
  # echo "sessname=blya path=ip:10.50.100.66 device_path=/dev/ram0" > \
            /sys/devices/virtual/rnbd-client/ctl/map_device

  Where "sessname=" is a session name, a string to identify the session
  on client and on server sides; "path=" is a destination IP address or
  a pair of a source and a destination IPs, separated by comma.  Multiple
  "path=" options can be specified in order to use multipath  (see RTRS
  description for details); "device_path=" is the block device to be
  mapped from the server side. After the session to the server machine is
  established, the mapped device will appear on the client side under
  /dev/rnbd<N>.


RNBD-Server Module Parameters
=============================

dev_search_path
---------------

When a device is mapped from the client, the server generates the path
to the block device on the server side by concatenating dev_search_path
and the "device_path" that was specified in the map_device operation.

The default dev_search_path is: "/".

dev_search_path option can also contain %SESSNAME% in order to provide
different device namespaces for different sessions.  See "device_path"
option for details.

============================
Protocol (rnbd/rnbd-proto.h)
============================

1. Before mapping first device from a given server, client sends an
RNBD_MSG_SESS_INFO to the server. Server responds with
RNBD_MSG_SESS_INFO_RSP. Currently the messages only contain the protocol
version for backward compatibility.

2. Client requests to open a device by sending RNBD_MSG_OPEN message. This
contains the path to the device and access mode (read-only or writable).
Server responds to the message with RNBD_MSG_OPEN_RSP. This contains
a 32 bit device id to be used for  IOs and device "geometry" related
information: side, max_hw_sectors, etc.

3. Client attaches RNBD_MSG_IO to each IO message send to a device. This
message contains device id, provided by server in his rnbd_msg_open_rsp,
sector to be accessed, read-write flags and bi_size.

4. Client closes a device by sending RNBD_MSG_CLOSE which contains only the
device id provided by the server.

=========================================
Contributors List(in alphabetical order)
=========================================
Danil Kipnis <danil.kipnis@profitbricks.com>
Fabian Holler <mail@fholler.de>
Guoqing Jiang <guoqing.jiang@cloud.ionos.com>
Jack Wang <jinpu.wang@profitbricks.com>
Kleber Souza <kleber.souza@profitbricks.com>
Lutz Pogrell <lutz.pogrell@cloud.ionos.com>
Milind Dumbare <Milind.dumbare@gmail.com>
Roman Penyaev <roman.penyaev@profitbricks.com>
