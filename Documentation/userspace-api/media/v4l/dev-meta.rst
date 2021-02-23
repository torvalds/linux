.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _metadata:

******************
Metadata Interface
******************

Metadata refers to any non-image data that supplements video frames with
additional information. This may include statistics computed over the image,
frame capture parameters supplied by the image source or device specific
parameters for specifying how the device processes images. This interface is
intended for transfer of metadata between the userspace and the hardware and
control of that operation.

The metadata interface is implemented on video device nodes. The device can be
dedicated to metadata or can support both video and metadata as specified in its
reported capabilities.

Querying Capabilities
=====================

Device nodes supporting the metadata capture interface set the
``V4L2_CAP_META_CAPTURE`` flag in the ``device_caps`` field of the
:c:type:`v4l2_capability` structure returned by the :c:func:`VIDIOC_QUERYCAP`
ioctl. That flag means the device can capture metadata to memory. Similarly,
device nodes supporting metadata output interface set the
``V4L2_CAP_META_OUTPUT`` flag in the ``device_caps`` field of
:c:type:`v4l2_capability` structure. That flag means the device can read
metadata from memory.

At least one of the read/write or streaming I/O methods must be supported.


Data Format Negotiation
=======================

The metadata device uses the :ref:`format` ioctls to select the capture format.
The metadata buffer content format is bound to that selected format. In addition
to the basic :ref:`format` ioctls, the :c:func:`VIDIOC_ENUM_FMT` ioctl must be
supported as well.

To use the :ref:`format` ioctls applications set the ``type`` field of the
:c:type:`v4l2_format` structure to ``V4L2_BUF_TYPE_META_CAPTURE`` or to
``V4L2_BUF_TYPE_META_OUTPUT`` and use the :c:type:`v4l2_meta_format` ``meta``
member of the ``fmt`` union as needed per the desired operation. Both drivers
and applications must set the remainder of the :c:type:`v4l2_format` structure
to 0.

.. c:type:: v4l2_meta_format

.. tabularcolumns:: |p{1.4cm}|p{2.2cm}|p{13.9cm}|

.. flat-table:: struct v4l2_meta_format
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``dataformat``
      - The data format, set by the application. This is a little endian
        :ref:`four character code <v4l2-fourcc>`. V4L2 defines metadata formats
        in :ref:`meta-formats`.
    * - __u32
      - ``buffersize``
      - Maximum buffer size in bytes required for data. The value is set by the
        driver.
