.. -*- coding: utf-8; mode: rst -*-

.. _format:

************
Data Formats
************


Data Format Negotiation
=======================

Different devices exchange different kinds of data with applications,
for example video images, raw or sliced VBI data, RDS datagrams. Even
within one kind many different formats are possible, in particular an
abundance of image formats. Although drivers must provide a default and
the selection persists across closing and reopening a device,
applications should always negotiate a data format before engaging in
data exchange. Negotiation means the application asks for a particular
format and the driver selects and reports the best the hardware can do
to satisfy the request. Of course applications can also just query the
current selection.

A single mechanism exists to negotiate all data formats using the
aggregate struct :ref:`v4l2_format <v4l2-format>` and the
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctls. Additionally the
:ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` ioctl can be used to examine
what the hardware *could* do, without actually selecting a new data
format. The data formats supported by the V4L2 API are covered in the
respective device section in :ref:`devices`. For a closer look at
image formats see :ref:`pixfmt`.

The :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl is a major turning-point in the
initialization sequence. Prior to this point multiple panel applications
can access the same device concurrently to select the current input,
change controls or modify other properties. The first :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`
assigns a logical stream (video data, VBI data etc.) exclusively to one
file descriptor.

Exclusive means no other application, more precisely no other file
descriptor, can grab this stream or change device properties
inconsistent with the negotiated parameters. A video standard change for
example, when the new standard uses a different number of scan lines,
can invalidate the selected image format. Therefore only the file
descriptor owning the stream can make invalidating changes. Accordingly
multiple file descriptors which grabbed different logical streams
prevent each other from interfering with their settings. When for
example video overlay is about to start or already in progress,
simultaneous video capturing may be restricted to the same cropping and
image size.

When applications omit the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl its locking side
effects are implied by the next step, the selection of an I/O method
with the :ref:`VIDIOC_REQBUFS` ioctl or implicit
with the first :ref:`read() <func-read>` or
:ref:`write() <func-write>` call.

Generally only one logical stream can be assigned to a file descriptor,
the exception being drivers permitting simultaneous video capturing and
overlay using the same file descriptor for compatibility with V4L and
earlier versions of V4L2. Switching the logical stream or returning into
"panel mode" is possible by closing and reopening the device. Drivers
*may* support a switch using :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`.

All drivers exchanging data with applications must support the
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl. Implementation of the
:ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` is highly recommended but optional.


Image Format Enumeration
========================

Apart of the generic format negotiation functions a special ioctl to
enumerate all image formats supported by video capture, overlay or
output devices is available. [#f1]_

The :ref:`VIDIOC_ENUM_FMT` ioctl must be supported
by all drivers exchanging image data with applications.

    **Important**

    Drivers are not supposed to convert image formats in kernel space.
    They must enumerate only formats directly supported by the hardware.
    If necessary driver writers should publish an example conversion
    routine or library for integration into applications.

.. [#f1]
   Enumerating formats an application has no a-priori knowledge of
   (otherwise it could explicitly ask for them and need not enumerate)
   seems useless, but there are applications serving as proxy between
   drivers and the actual video applications for which this is useful.
