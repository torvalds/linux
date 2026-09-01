====================
How FunctionFS works
====================

Overview
========

From kernel point of view it is just a composite function with some
unique behaviour.  It may be added to an USB configuration only after
the user space driver has registered by writing descriptors and
strings (the user space program has to provide the same information
that kernel level composite functions provide when they are added to
the configuration).

This in particular means that the composite initialisation functions
may not be in init section (ie. may not use the __init tag).

From user space point of view it is a file system which when
mounted provides an "ep0" file.  User space driver need to
write descriptors and strings to that file.  It does not need
to worry about endpoints, interfaces or strings numbers but
simply provide descriptors such as if the function was the
only one (endpoints and strings numbers starting from one and
interface numbers starting from zero).  The FunctionFS changes
them as needed also handling situation when numbers differ in
different configurations.

For more information about FunctionFS descriptors see :doc:`functionfs-desc`

When descriptors and strings are written "ep#" files appear
(one for each declared endpoint) which handle communication on
a single endpoint.  Again, FunctionFS takes care of the real
numbers and changing of the configuration (which means that
"ep1" file may be really mapped to (say) endpoint 3 (and when
configuration changes to (say) endpoint 2)).  "ep0" is used
for receiving events and handling setup requests.

When all files are closed the function disables itself.

What I also want to mention is that the FunctionFS is designed in such
a way that it is possible to mount it several times so in the end
a gadget could use several FunctionFS functions. The idea is that
each FunctionFS instance is identified by the device name used
when mounting.

One can imagine a gadget that has an Ethernet, MTP and HID interfaces
where the last two are implemented via FunctionFS.  On user space
level it would look like this::

  $ insmod g_ffs.ko idVendor=<ID> iSerialNumber=<string> functions=mtp,hid
  $ mkdir /dev/ffs-mtp && mount -t functionfs mtp /dev/ffs-mtp
  $ ( cd /dev/ffs-mtp && mtp-daemon ) &
  $ mkdir /dev/ffs-hid && mount -t functionfs hid /dev/ffs-hid
  $ ( cd /dev/ffs-hid && hid-daemon ) &

On kernel level the gadget checks ffs_data->dev_name to identify
whether its FunctionFS is designed for MTP ("mtp") or HID ("hid").

If no "functions" module parameters is supplied, the driver accepts
just one function with any name.

When "functions" module parameter is supplied, only functions
with listed names are accepted. In particular, if the "functions"
parameter's value is just a one-element list, then the behaviour
is similar to when there is no "functions" at all; however,
only a function with the specified name is accepted.

The gadget is registered only after all the declared function
filesystems have been mounted and USB descriptors of all functions
have been written to their ep0's.

Conversely, the gadget is unregistered after the first USB function
closes its endpoints.

Endpoint IOCTLs
===============

FunctionFS supports additional IOCTLs that can be performed on data endpoints
(ie. not ep0). For a full list of these IOCTLs, please refer to the documentation
in ``include/uapi/linux/usb/functionfs.h``.

One such IOCTL is:

  ``FUNCTIONFS_ENDPOINT_ENABLE_ZLP(__u32 *)``
    Enable or disable automatic zero-length packet (ZLP) appending for the
    endpoint. The argument is a pointer to a __u32: 0 to disable, non-zero to
    enable. When enabled, the kernel will automatically append a ZLP at the end
    of a transfer if the payload length is an exact multiple of the endpoint's
    max packet size. This is useful for compatibility with legacy protocols
    which require automatic ZLP appending to data written from userspace. This
    IOCTL can only be used on IN endpoints. It can be called at any time after
    the FunctionFS instance is active, even before the host has connected or
    enabled the endpoint. Returns zero on success, or a negative errno value on
    error:

    * ``-ENODEV``: The FunctionFS instance is not active.
    * ``-EINVAL``: The endpoint is not an IN endpoint.
    * ``-EFAULT``: Invalid user space pointer for the argument.

RW Proxy Endpoints
==================

If the ``FUNCTIONFS_RW_PROXY_EPS`` flag is passed in the descriptor header
(requires ``FUNCTIONFS_DESCRIPTORS_MAGIC_V2``), FunctionFS will provision a
bidirectional rw_proxy file descriptor (e.g., "ep1_rw") alongside each pair
of IN and OUT endpoints. The rw_proxy file aliases the underlying hardware
endpoints, allowing userspace to use a single file descriptor for both reading
(OUT) and writing (IN).

This flag requires the total number of hardware endpoints to be an even number.
FunctionFS will automatically walk the provided endpoints and group them into
adjacent pairs (e.g., ep1 and ep2 form the first pair, ep3 and ep4 form the
second pair). Each pair must consist of exactly one IN endpoint and one OUT
endpoint.

For each valid pair, a rw_proxy file is created and named after the first
endpoint in the pair with a "_rw" suffix. For example, if ep1 and ep2 are
paired, a rw_proxy file named "ep1_rw" is created. If ep3 and ep4 are paired,
"ep3_rw" is created.

If the ``FUNCTIONFS_VIRTUAL_ADDR`` flag is also enabled, the endpoints will be
named using their physical endpoint address in hexadecimal instead of their
index. RW proxy files will inherit this naming convention. For example, if the
first endpoint of a pair maps to address 0x02, the rw_proxy file will be
named "ep02_rw".

When this flag is enabled, userspace has the choice of performing data transfers
via the single rw_proxy file descriptor or the two base file descriptors. The
rw_proxy file descriptor acts as a pure VFS alias that proxies all operations
directly to the underlying base file descriptors.

Because it is a pure proxy, there are no data races or buffer corruptions if
userspace uses both the rw_proxy endpoint and the base endpoints concurrently.
The native mutexes of the base endpoints perfectly serialize all concurrent
transfers. However, userspace should generally pick one method and stick to it
to avoid interleaving its own data stream.

- **IOCTLs (Clear Halt, etc.):** RW proxy endpoints do not support IOCTLs and
  will return ``-ENOTTY``. To clear a host-initiated halt, userspace must issue
  the ``FUNCTIONFS_CLEAR_HALT`` ioctl directly on the corresponding base
  endpoint file descriptor.
- **Intentional Stalls:** The traditional mechanism for intentionally halting an
  endpoint by issuing a reverse-direction data operation (e.g., attempting to
  read from an IN endpoint) continues to work, but it must be issued on the
  base endpoint. RW proxy endpoints cannot be used to trigger a stall because
  they are fully bidirectional.

Note that DMABUF data transfers (``FUNCTIONFS_DMABUF_TRANSFER``) are unsupported
via the rw_proxy endpoint because it does not support IOCTLs. If DMABUF
transfers are required, users must use the standard base endpoints.
DMABUF interface
================

FunctionFS additionally supports a DMABUF based interface, where the
userspace can attach DMABUF objects (externally created) to an endpoint,
and subsequently use them for data transfers.

Note: The DMABUF interface is unsupported on rw_proxy endpoints. See
the RW Proxy Endpoints section for details on using DMABUF alongside
the ``FUNCTIONFS_RW_PROXY_EPS`` flag.

A userspace application can then use this interface to share DMABUF
objects between several interfaces, allowing it to transfer data in a
zero-copy fashion, for instance between IIO and the USB stack.

As part of this interface, three new IOCTLs have been added. These three
IOCTLs have to be performed on a data endpoint (ie. not ep0). They are:

  ``FUNCTIONFS_DMABUF_ATTACH(int)``
    Attach the DMABUF object, identified by its file descriptor, to the
    data endpoint. Returns zero on success, and a negative errno value
    on error.

  ``FUNCTIONFS_DMABUF_DETACH(int)``
    Detach the given DMABUF object, identified by its file descriptor,
    from the data endpoint. Returns zero on success, and a negative
    errno value on error. Note that closing the endpoint's file
    descriptor will automatically detach all attached DMABUFs.

  ``FUNCTIONFS_DMABUF_TRANSFER(struct usb_ffs_dmabuf_transfer_req *)``
    Enqueue the previously attached DMABUF to the transfer queue.
    The argument is a structure that packs the DMABUF's file descriptor,
    the size in bytes to transfer (which should generally correspond to
    the size of the DMABUF), and a 'flags' field which is unused
    for now. Returns zero on success, and a negative errno value on
    error.
