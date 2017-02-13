===========================
The Linux-USB Host Side API
===========================

Introduction to USB on Linux
============================

A Universal Serial Bus (USB) is used to connect a host, such as a PC or
workstation, to a number of peripheral devices. USB uses a tree
structure, with the host as the root (the system's master), hubs as
interior nodes, and peripherals as leaves (and slaves). Modern PCs
support several such trees of USB devices, usually
a few USB 3.0 (5 GBit/s) or USB 3.1 (10 GBit/s) and some legacy
USB 2.0 (480 MBit/s) busses just in case.

That master/slave asymmetry was designed-in for a number of reasons, one
being ease of use. It is not physically possible to mistake upstream and
downstream or it does not matter with a type C plug (or they are built into the
peripheral). Also, the host software doesn't need to deal with
distributed auto-configuration since the pre-designated master node
manages all that.

Kernel developers added USB support to Linux early in the 2.2 kernel
series and have been developing it further since then. Besides support
for each new generation of USB, various host controllers gained support,
new drivers for peripherals have been added and advanced features for latency
measurement and improved power management introduced.

Linux can run inside USB devices as well as on the hosts that control
the devices. But USB device drivers running inside those peripherals
don't do the same things as the ones running inside hosts, so they've
been given a different name: *gadget drivers*. This document does not
cover gadget drivers.

USB Host-Side API Model
=======================

Host-side drivers for USB devices talk to the "usbcore" APIs. There are
two. One is intended for *general-purpose* drivers (exposed through
driver frameworks), and the other is for drivers that are *part of the
core*. Such core drivers include the *hub* driver (which manages trees
of USB devices) and several different kinds of *host controller
drivers*, which control individual busses.

The device model seen by USB drivers is relatively complex.

-  USB supports four kinds of data transfers (control, bulk, interrupt,
   and isochronous). Two of them (control and bulk) use bandwidth as
   it's available, while the other two (interrupt and isochronous) are
   scheduled to provide guaranteed bandwidth.

-  The device description model includes one or more "configurations"
   per device, only one of which is active at a time. Devices are supposed
   to be capable of operating at lower than their top
   speeds and may provide a BOS descriptor showing the lowest speed they
   remain fully operational at.

-  From USB 3.0 on configurations have one or more "functions", which
   provide a common functionality and are grouped together for purposes
   of power management.

-  Configurations or functions have one or more "interfaces", each of which may have
   "alternate settings". Interfaces may be standardized by USB "Class"
   specifications, or may be specific to a vendor or device.

   USB device drivers actually bind to interfaces, not devices. Think of
   them as "interface drivers", though you may not see many devices
   where the distinction is important. *Most USB devices are simple,
   with only one function, one configuration, one interface, and one alternate
   setting.*

-  Interfaces have one or more "endpoints", each of which supports one
   type and direction of data transfer such as "bulk out" or "interrupt
   in". The entire configuration may have up to sixteen endpoints in
   each direction, allocated as needed among all the interfaces.

-  Data transfer on USB is packetized; each endpoint has a maximum
   packet size. Drivers must often be aware of conventions such as
   flagging the end of bulk transfers using "short" (including zero
   length) packets.

-  The Linux USB API supports synchronous calls for control and bulk
   messages. It also supports asynchronous calls for all kinds of data
   transfer, using request structures called "URBs" (USB Request
   Blocks).

Accordingly, the USB Core API exposed to device drivers covers quite a
lot of territory. You'll probably need to consult the USB 3.0
specification, available online from www.usb.org at no cost, as well as
class or device specifications.

The only host-side drivers that actually touch hardware (reading/writing
registers, handling IRQs, and so on) are the HCDs. In theory, all HCDs
provide the same functionality through the same API. In practice, that's
becoming more true, but there are still differences
that crop up especially with fault handling on the less common controllers.
Different controllers don't
necessarily report the same aspects of failures, and recovery from
faults (including software-induced ones like unlinking an URB) isn't yet
fully consistent. Device driver authors should make a point of doing
disconnect testing (while the device is active) with each different host
controller driver, to make sure drivers don't have bugs of their own as
well as to make sure they aren't relying on some HCD-specific behavior.

USB-Standard Types
==================

In ``<linux/usb/ch9.h>`` you will find the USB data types defined in
chapter 9 of the USB specification. These data types are used throughout
USB, and in APIs including this host side API, gadget APIs, and usbfs.

.. kernel-doc:: include/linux/usb/ch9.h
   :internal:

Host-Side Data Types and Macros
===============================

The host side API exposes several layers to drivers, some of which are
more necessary than others. These support lifecycle models for host side
drivers and devices, and support passing buffers through usbcore to some
HCD that performs the I/O for the device driver.

.. kernel-doc:: include/linux/usb.h
   :internal:

USB Core APIs
=============

There are two basic I/O models in the USB API. The most elemental one is
asynchronous: drivers submit requests in the form of an URB, and the
URB's completion callback handles the next step. All USB transfer types
support that model, although there are special cases for control URBs
(which always have setup and status stages, but may not have a data
stage) and isochronous URBs (which allow large packets and include
per-packet fault reports). Built on top of that is synchronous API
support, where a driver calls a routine that allocates one or more URBs,
submits them, and waits until they complete. There are synchronous
wrappers for single-buffer control and bulk transfers (which are awkward
to use in some driver disconnect scenarios), and for scatterlist based
streaming i/o (bulk or interrupt).

USB drivers need to provide buffers that can be used for DMA, although
they don't necessarily need to provide the DMA mapping themselves. There
are APIs to use used when allocating DMA buffers, which can prevent use
of bounce buffers on some systems. In some cases, drivers may be able to
rely on 64bit DMA to eliminate another kind of bounce buffer.

.. kernel-doc:: drivers/usb/core/urb.c
   :export:

.. kernel-doc:: drivers/usb/core/message.c
   :export:

.. kernel-doc:: drivers/usb/core/file.c
   :export:

.. kernel-doc:: drivers/usb/core/driver.c
   :export:

.. kernel-doc:: drivers/usb/core/usb.c
   :export:

.. kernel-doc:: drivers/usb/core/hub.c
   :export:

Host Controller APIs
====================

These APIs are only for use by host controller drivers, most of which
implement standard register interfaces such as XHCI, EHCI, OHCI, or UHCI. UHCI
was one of the first interfaces, designed by Intel and also used by VIA;
it doesn't do much in hardware. OHCI was designed later, to have the
hardware do more work (bigger transfers, tracking protocol state, and so
on). EHCI was designed with USB 2.0; its design has features that
resemble OHCI (hardware does much more work) as well as UHCI (some parts
of ISO support, TD list processing). XHCI was designed with USB 3.0. It
continues to shift support for functionality into hardware.

There are host controllers other than the "big three", although most PCI
based controllers (and a few non-PCI based ones) use one of those
interfaces. Not all host controllers use DMA; some use PIO, and there is
also a simulator and a virtual host controller to pipe USB over the network.

The same basic APIs are available to drivers for all those controllers.
For historical reasons they are in two layers: :c:type:`struct
usb_bus <usb_bus>` is a rather thin layer that became available
in the 2.2 kernels, while :c:type:`struct usb_hcd <usb_hcd>`
is a more featureful layer
that lets HCDs share common code, to shrink driver size and
significantly reduce hcd-specific behaviors.

.. kernel-doc:: drivers/usb/core/hcd.c
   :export:

.. kernel-doc:: drivers/usb/core/hcd-pci.c
   :export:

.. kernel-doc:: drivers/usb/core/buffer.c
   :internal:

The USB Filesystem (usbfs)
==========================

This chapter presents the Linux *usbfs*. You may prefer to avoid writing
new kernel code for your USB driver; that's the problem that usbfs set
out to solve. User mode device drivers are usually packaged as
applications or libraries, and may use usbfs through some programming
library that wraps it. Such libraries include
`libusb <http://libusb.sourceforge.net>`__ for C/C++, and
`jUSB <http://jUSB.sourceforge.net>`__ for Java.

    **Note**

    This particular documentation is incomplete, especially with respect
    to the asynchronous mode. As of kernel 2.5.66 the code and this
    (new) documentation need to be cross-reviewed.

Configure usbfs into Linux kernels by enabling the *USB filesystem*
option (CONFIG_USB_DEVICEFS), and you get basic support for user mode
USB device drivers. Until relatively recently it was often (confusingly)
called *usbdevfs* although it wasn't solving what *devfs* was. Every USB
device will appear in usbfs, regardless of whether or not it has a
kernel driver.

What files are in "usbfs"?
--------------------------

Conventionally mounted at ``/proc/bus/usb``, usbfs features include:

-  ``/proc/bus/usb/devices`` ... a text file showing each of the USB
   devices on known to the kernel, and their configuration descriptors.
   You can also poll() this to learn about new devices.

-  ``/proc/bus/usb/BBB/DDD`` ... magic files exposing the each device's
   configuration descriptors, and supporting a series of ioctls for
   making device requests, including I/O to devices. (Purely for access
   by programs.)

Each bus is given a number (BBB) based on when it was enumerated; within
each bus, each device is given a similar number (DDD). Those BBB/DDD
paths are not "stable" identifiers; expect them to change even if you
always leave the devices plugged in to the same hub port. *Don't even
think of saving these in application configuration files.* Stable
identifiers are available, for user mode applications that want to use
them. HID and networking devices expose these stable IDs, so that for
example you can be sure that you told the right UPS to power down its
second server. "usbfs" doesn't (yet) expose those IDs.

Mounting and Access Control
---------------------------

There are a number of mount options for usbfs, which will be of most
interest to you if you need to override the default access control
policy. That policy is that only root may read or write device files
(``/proc/bus/BBB/DDD``) although anyone may read the ``devices`` or
``drivers`` files. I/O requests to the device also need the
CAP_SYS_RAWIO capability,

The significance of that is that by default, all user mode device
drivers need super-user privileges. You can change modes or ownership in
a driver setup when the device hotplugs, or maye just start the driver
right then, as a privileged server (or some activity within one). That's
the most secure approach for multi-user systems, but for single user
systems ("trusted" by that user) it's more convenient just to grant
everyone all access (using the *devmode=0666* option) so the driver can
start whenever it's needed.

The mount options for usbfs, usable in /etc/fstab or in command line
invocations of *mount*, are:

*busgid*\ =NNNNN
    Controls the GID used for the /proc/bus/usb/BBB directories.
    (Default: 0)

*busmode*\ =MMM
    Controls the file mode used for the /proc/bus/usb/BBB directories.
    (Default: 0555)

*busuid*\ =NNNNN
    Controls the UID used for the /proc/bus/usb/BBB directories.
    (Default: 0)

*devgid*\ =NNNNN
    Controls the GID used for the /proc/bus/usb/BBB/DDD files. (Default:
    0)

*devmode*\ =MMM
    Controls the file mode used for the /proc/bus/usb/BBB/DDD files.
    (Default: 0644)

*devuid*\ =NNNNN
    Controls the UID used for the /proc/bus/usb/BBB/DDD files. (Default:
    0)

*listgid*\ =NNNNN
    Controls the GID used for the /proc/bus/usb/devices and drivers
    files. (Default: 0)

*listmode*\ =MMM
    Controls the file mode used for the /proc/bus/usb/devices and
    drivers files. (Default: 0444)

*listuid*\ =NNNNN
    Controls the UID used for the /proc/bus/usb/devices and drivers
    files. (Default: 0)

Note that many Linux distributions hard-wire the mount options for usbfs
in their init scripts, such as ``/etc/rc.d/rc.sysinit``, rather than
making it easy to set this per-system policy in ``/etc/fstab``.

/proc/bus/usb/devices
---------------------

This file is handy for status viewing tools in user mode, which can scan
the text format and ignore most of it. More detailed device status
(including class and vendor status) is available from device-specific
files. For information about the current format of this file, see the
``Documentation/usb/proc_usb_info.txt`` file in your Linux kernel
sources.

This file, in combination with the poll() system call, can also be used
to detect when devices are added or removed:

::

    int fd;
    struct pollfd pfd;

    fd = open("/proc/bus/usb/devices", O_RDONLY);
    pfd = { fd, POLLIN, 0 };
    for (;;) {
        /* The first time through, this call will return immediately. */
        poll(&pfd, 1, -1);

        /* To see what's changed, compare the file's previous and current
           contents or scan the filesystem.  (Scanning is more precise.) */
    }

Note that this behavior is intended to be used for informational and
debug purposes. It would be more appropriate to use programs such as
udev or HAL to initialize a device or start a user-mode helper program,
for instance.

/proc/bus/usb/BBB/DDD
---------------------

Use these files in one of these basic ways:

*They can be read,* producing first the device descriptor (18 bytes) and
then the descriptors for the current configuration. See the USB 2.0 spec
for details about those binary data formats. You'll need to convert most
multibyte values from little endian format to your native host byte
order, although a few of the fields in the device descriptor (both of
the BCD-encoded fields, and the vendor and product IDs) will be
byteswapped for you. Note that configuration descriptors include
descriptors for interfaces, altsettings, endpoints, and maybe additional
class descriptors.

*Perform USB operations* using *ioctl()* requests to make endpoint I/O
requests (synchronously or asynchronously) or manage the device. These
requests need the CAP_SYS_RAWIO capability, as well as filesystem
access permissions. Only one ioctl request can be made on one of these
device files at a time. This means that if you are synchronously reading
an endpoint from one thread, you won't be able to write to a different
endpoint from another thread until the read completes. This works for
*half duplex* protocols, but otherwise you'd use asynchronous i/o
requests.

Life Cycle of User Mode Drivers
-------------------------------

Such a driver first needs to find a device file for a device it knows
how to handle. Maybe it was told about it because a ``/sbin/hotplug``
event handling agent chose that driver to handle the new device. Or
maybe it's an application that scans all the /proc/bus/usb device files,
and ignores most devices. In either case, it should :c:func:`read()`
all the descriptors from the device file, and check them against what it
knows how to handle. It might just reject everything except a particular
vendor and product ID, or need a more complex policy.

Never assume there will only be one such device on the system at a time!
If your code can't handle more than one device at a time, at least
detect when there's more than one, and have your users choose which
device to use.

Once your user mode driver knows what device to use, it interacts with
it in either of two styles. The simple style is to make only control
requests; some devices don't need more complex interactions than those.
(An example might be software using vendor-specific control requests for
some initialization or configuration tasks, with a kernel driver for the
rest.)

More likely, you need a more complex style driver: one using non-control
endpoints, reading or writing data and claiming exclusive use of an
interface. *Bulk* transfers are easiest to use, but only their sibling
*interrupt* transfers work with low speed devices. Both interrupt and
*isochronous* transfers offer service guarantees because their bandwidth
is reserved. Such "periodic" transfers are awkward to use through usbfs,
unless you're using the asynchronous calls. However, interrupt transfers
can also be used in a synchronous "one shot" style.

Your user-mode driver should never need to worry about cleaning up
request state when the device is disconnected, although it should close
its open file descriptors as soon as it starts seeing the ENODEV errors.

The ioctl() Requests
--------------------

To use these ioctls, you need to include the following headers in your
userspace program:

::

    #include <linux/usb.h>
    #include <linux/usbdevice_fs.h>
    #include <asm/byteorder.h>

The standard USB device model requests, from "Chapter 9" of the USB 2.0
specification, are automatically included from the ``<linux/usb/ch9.h>``
header.

Unless noted otherwise, the ioctl requests described here will update
the modification time on the usbfs file to which they are applied
(unless they fail). A return of zero indicates success; otherwise, a
standard USB error code is returned. (These are documented in
``Documentation/usb/error-codes.txt`` in your kernel sources.)

Each of these files multiplexes access to several I/O streams, one per
endpoint. Each device has one control endpoint (endpoint zero) which
supports a limited RPC style RPC access. Devices are configured by
hub_wq (in the kernel) setting a device-wide *configuration* that
affects things like power consumption and basic functionality. The
endpoints are part of USB *interfaces*, which may have *altsettings*
affecting things like which endpoints are available. Many devices only
have a single configuration and interface, so drivers for them will
ignore configurations and altsettings.

Management/Status Requests
~~~~~~~~~~~~~~~~~~~~~~~~~~

A number of usbfs requests don't deal very directly with device I/O.
They mostly relate to device management and status. These are all
synchronous requests.

USBDEVFS_CLAIMINTERFACE
    This is used to force usbfs to claim a specific interface, which has
    not previously been claimed by usbfs or any other kernel driver. The
    ioctl parameter is an integer holding the number of the interface
    (bInterfaceNumber from descriptor).

    Note that if your driver doesn't claim an interface before trying to
    use one of its endpoints, and no other driver has bound to it, then
    the interface is automatically claimed by usbfs.

    This claim will be released by a RELEASEINTERFACE ioctl, or by
    closing the file descriptor. File modification time is not updated
    by this request.

USBDEVFS_CONNECTINFO
    Says whether the device is lowspeed. The ioctl parameter points to a
    structure like this:

    ::

        struct usbdevfs_connectinfo {
                unsigned int   devnum;
                unsigned char  slow;
        };

    File modification time is not updated by this request.

    *You can't tell whether a "not slow" device is connected at high
    speed (480 MBit/sec) or just full speed (12 MBit/sec).* You should
    know the devnum value already, it's the DDD value of the device file
    name.

USBDEVFS_GETDRIVER
    Returns the name of the kernel driver bound to a given interface (a
    string). Parameter is a pointer to this structure, which is
    modified:

    ::

        struct usbdevfs_getdriver {
                unsigned int  interface;
                char          driver[USBDEVFS_MAXDRIVERNAME + 1];
        };

    File modification time is not updated by this request.

USBDEVFS_IOCTL
    Passes a request from userspace through to a kernel driver that has
    an ioctl entry in the *struct usb_driver* it registered.

    ::

        struct usbdevfs_ioctl {
                int     ifno;
                int     ioctl_code;
                void    *data;
        };

        /* user mode call looks like this.
         * 'request' becomes the driver->ioctl() 'code' parameter.
         * the size of 'param' is encoded in 'request', and that data
         * is copied to or from the driver->ioctl() 'buf' parameter.
         */
        static int
        usbdev_ioctl (int fd, int ifno, unsigned request, void *param)
        {
                struct usbdevfs_ioctl   wrapper;

                wrapper.ifno = ifno;
                wrapper.ioctl_code = request;
                wrapper.data = param;

                return ioctl (fd, USBDEVFS_IOCTL, &wrapper);
        }

    File modification time is not updated by this request.

    This request lets kernel drivers talk to user mode code through
    filesystem operations even when they don't create a character or
    block special device. It's also been used to do things like ask
    devices what device special file should be used. Two pre-defined
    ioctls are used to disconnect and reconnect kernel drivers, so that
    user mode code can completely manage binding and configuration of
    devices.

USBDEVFS_RELEASEINTERFACE
    This is used to release the claim usbfs made on interface, either
    implicitly or because of a USBDEVFS_CLAIMINTERFACE call, before the
    file descriptor is closed. The ioctl parameter is an integer holding
    the number of the interface (bInterfaceNumber from descriptor); File
    modification time is not updated by this request.

        **Warning**

        *No security check is made to ensure that the task which made
        the claim is the one which is releasing it. This means that user
        mode driver may interfere other ones.*

USBDEVFS_RESETEP
    Resets the data toggle value for an endpoint (bulk or interrupt) to
    DATA0. The ioctl parameter is an integer endpoint number (1 to 15,
    as identified in the endpoint descriptor), with USB_DIR_IN added
    if the device's endpoint sends data to the host.

        **Warning**

        *Avoid using this request. It should probably be removed.* Using
        it typically means the device and driver will lose toggle
        synchronization. If you really lost synchronization, you likely
        need to completely handshake with the device, using a request
        like CLEAR_HALT or SET_INTERFACE.

USBDEVFS_DROP_PRIVILEGES
    This is used to relinquish the ability to do certain operations
    which are considered to be privileged on a usbfs file descriptor.
    This includes claiming arbitrary interfaces, resetting a device on
    which there are currently claimed interfaces from other users, and
    issuing USBDEVFS_IOCTL calls. The ioctl parameter is a 32 bit mask
    of interfaces the user is allowed to claim on this file descriptor.
    You may issue this ioctl more than one time to narrow said mask.

Synchronous I/O Support
~~~~~~~~~~~~~~~~~~~~~~~

Synchronous requests involve the kernel blocking until the user mode
request completes, either by finishing successfully or by reporting an
error. In most cases this is the simplest way to use usbfs, although as
noted above it does prevent performing I/O to more than one endpoint at
a time.

USBDEVFS_BULK
    Issues a bulk read or write request to the device. The ioctl
    parameter is a pointer to this structure:

    ::

        struct usbdevfs_bulktransfer {
                unsigned int  ep;
                unsigned int  len;
                unsigned int  timeout; /* in milliseconds */
                void          *data;
        };

    The "ep" value identifies a bulk endpoint number (1 to 15, as
    identified in an endpoint descriptor), masked with USB_DIR_IN when
    referring to an endpoint which sends data to the host from the
    device. The length of the data buffer is identified by "len"; Recent
    kernels support requests up to about 128KBytes. *FIXME say how read
    length is returned, and how short reads are handled.*.

USBDEVFS_CLEAR_HALT
    Clears endpoint halt (stall) and resets the endpoint toggle. This is
    only meaningful for bulk or interrupt endpoints. The ioctl parameter
    is an integer endpoint number (1 to 15, as identified in an endpoint
    descriptor), masked with USB_DIR_IN when referring to an endpoint
    which sends data to the host from the device.

    Use this on bulk or interrupt endpoints which have stalled,
    returning *-EPIPE* status to a data transfer request. Do not issue
    the control request directly, since that could invalidate the host's
    record of the data toggle.

USBDEVFS_CONTROL
    Issues a control request to the device. The ioctl parameter points
    to a structure like this:

    ::

        struct usbdevfs_ctrltransfer {
                __u8   bRequestType;
                __u8   bRequest;
                __u16  wValue;
                __u16  wIndex;
                __u16  wLength;
                __u32  timeout;  /* in milliseconds */
                void   *data;
        };

    The first eight bytes of this structure are the contents of the
    SETUP packet to be sent to the device; see the USB 2.0 specification
    for details. The bRequestType value is composed by combining a
    USB_TYPE_\* value, a USB_DIR_\* value, and a USB_RECIP_\*
    value (from *<linux/usb.h>*). If wLength is nonzero, it describes
    the length of the data buffer, which is either written to the device
    (USB_DIR_OUT) or read from the device (USB_DIR_IN).

    At this writing, you can't transfer more than 4 KBytes of data to or
    from a device; usbfs has a limit, and some host controller drivers
    have a limit. (That's not usually a problem.) *Also* there's no way
    to say it's not OK to get a short read back from the device.

USBDEVFS_RESET
    Does a USB level device reset. The ioctl parameter is ignored. After
    the reset, this rebinds all device interfaces. File modification
    time is not updated by this request.

        **Warning**

        *Avoid using this call* until some usbcore bugs get fixed, since
        it does not fully synchronize device, interface, and driver (not
        just usbfs) state.

USBDEVFS_SETINTERFACE
    Sets the alternate setting for an interface. The ioctl parameter is
    a pointer to a structure like this:

    ::

        struct usbdevfs_setinterface {
                unsigned int  interface;
                unsigned int  altsetting;
        };

    File modification time is not updated by this request.

    Those struct members are from some interface descriptor applying to
    the current configuration. The interface number is the
    bInterfaceNumber value, and the altsetting number is the
    bAlternateSetting value. (This resets each endpoint in the
    interface.)

USBDEVFS_SETCONFIGURATION
    Issues the :c:func:`usb_set_configuration()` call for the
    device. The parameter is an integer holding the number of a
    configuration (bConfigurationValue from descriptor). File
    modification time is not updated by this request.

        **Warning**

        *Avoid using this call* until some usbcore bugs get fixed, since
        it does not fully synchronize device, interface, and driver (not
        just usbfs) state.

Asynchronous I/O Support
~~~~~~~~~~~~~~~~~~~~~~~~

As mentioned above, there are situations where it may be important to
initiate concurrent operations from user mode code. This is particularly
important for periodic transfers (interrupt and isochronous), but it can
be used for other kinds of USB requests too. In such cases, the
asynchronous requests described here are essential. Rather than
submitting one request and having the kernel block until it completes,
the blocking is separate.

These requests are packaged into a structure that resembles the URB used
by kernel device drivers. (No POSIX Async I/O support here, sorry.) It
identifies the endpoint type (USBDEVFS_URB_TYPE_\*), endpoint
(number, masked with USB_DIR_IN as appropriate), buffer and length,
and a user "context" value serving to uniquely identify each request.
(It's usually a pointer to per-request data.) Flags can modify requests
(not as many as supported for kernel drivers).

Each request can specify a realtime signal number (between SIGRTMIN and
SIGRTMAX, inclusive) to request a signal be sent when the request
completes.

When usbfs returns these urbs, the status value is updated, and the
buffer may have been modified. Except for isochronous transfers, the
actual_length is updated to say how many bytes were transferred; if the
USBDEVFS_URB_DISABLE_SPD flag is set ("short packets are not OK"), if
fewer bytes were read than were requested then you get an error report.

::

    struct usbdevfs_iso_packet_desc {
            unsigned int                     length;
            unsigned int                     actual_length;
            unsigned int                     status;
    };

    struct usbdevfs_urb {
            unsigned char                    type;
            unsigned char                    endpoint;
            int                              status;
            unsigned int                     flags;
            void                             *buffer;
            int                              buffer_length;
            int                              actual_length;
            int                              start_frame;
            int                              number_of_packets;
            int                              error_count;
            unsigned int                     signr;
            void                             *usercontext;
            struct usbdevfs_iso_packet_desc  iso_frame_desc[];
    };

For these asynchronous requests, the file modification time reflects
when the request was initiated. This contrasts with their use with the
synchronous requests, where it reflects when requests complete.

USBDEVFS_DISCARDURB
    *TBS* File modification time is not updated by this request.

USBDEVFS_DISCSIGNAL
    *TBS* File modification time is not updated by this request.

USBDEVFS_REAPURB
    *TBS* File modification time is not updated by this request.

USBDEVFS_REAPURBNDELAY
    *TBS* File modification time is not updated by this request.

USBDEVFS_SUBMITURB
    *TBS*
