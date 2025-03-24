.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_ENUM_FMT:

*********************
ioctl VIDIOC_ENUM_FMT
*********************

Name
====

VIDIOC_ENUM_FMT - Enumerate image formats

Synopsis
========

.. c:macro:: VIDIOC_ENUM_FMT

``int ioctl(int fd, VIDIOC_ENUM_FMT, struct v4l2_fmtdesc *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_fmtdesc`.

Description
===========

To enumerate image formats applications initialize the ``type``, ``mbus_code``
and ``index`` fields of struct :c:type:`v4l2_fmtdesc` and call
the :ref:`VIDIOC_ENUM_FMT` ioctl with a pointer to this structure. Drivers
fill the rest of the structure or return an ``EINVAL`` error code. All
formats are enumerable by beginning at index zero and incrementing by
one until ``EINVAL`` is returned. If applicable, drivers shall return
formats in preference order, where preferred formats are returned before
(that is, with lower ``index`` value) less-preferred formats.

Depending on the ``V4L2_CAP_IO_MC`` :ref:`capability <device-capabilities>`,
the ``mbus_code`` field is handled differently:

1) ``V4L2_CAP_IO_MC`` is not set (also known as a 'video-node-centric' driver)

   Applications shall initialize the ``mbus_code`` field to zero and drivers
   shall ignore the value of the field.

   Drivers shall enumerate all image formats.

   .. note::

      After switching the input or output the list of enumerated image
      formats may be different.

2) ``V4L2_CAP_IO_MC`` is set (also known as an 'MC-centric' driver)

   If the ``mbus_code`` field is zero, then all image formats
   shall be enumerated.

   If the ``mbus_code`` field is initialized to a valid (non-zero)
   :ref:`media bus format code <v4l2-mbus-pixelcode>`, then drivers
   shall restrict enumeration to only the image formats that can produce
   (for video output devices) or be produced from (for video capture
   devices) that media bus code. If the ``mbus_code`` is unsupported by
   the driver, then ``EINVAL`` shall be returned.

   Regardless of the value of the ``mbus_code`` field, the enumerated image
   formats shall not depend on the active configuration of the video device
   or device pipeline.

.. c:type:: v4l2_fmtdesc

.. cssclass:: longtable

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.5cm}|

.. flat-table:: struct v4l2_fmtdesc
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - Number of the format in the enumeration, set by the application.
        This is in no way related to the ``pixelformat`` field.
        When the index is ORed with ``V4L2_FMTDESC_FLAG_ENUM_ALL`` the
        driver clears the flag and enumerates all the possible formats,
        ignoring any limitations from the current configuration. Drivers
        which do not support this flag always return an ``EINVAL``
        error code without clearing this flag.
        Formats enumerated when using ``V4L2_FMTDESC_FLAG_ENUM_ALL`` flag
        shouldn't be used when calling :c:func:`VIDIOC_ENUM_FRAMESIZES`
        or :c:func:`VIDIOC_ENUM_FRAMEINTERVALS`.
        ``V4L2_FMTDESC_FLAG_ENUM_ALL`` should only be used by drivers that
        can return different format list depending on this flag.
    * - __u32
      - ``type``
      - Type of the data stream, set by the application. Only these types
	are valid here: ``V4L2_BUF_TYPE_VIDEO_CAPTURE``,
	``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``,
	``V4L2_BUF_TYPE_VIDEO_OUTPUT``,
	``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE``,
	``V4L2_BUF_TYPE_VIDEO_OVERLAY``,
	``V4L2_BUF_TYPE_SDR_CAPTURE``,
	``V4L2_BUF_TYPE_SDR_OUTPUT``,
	``V4L2_BUF_TYPE_META_CAPTURE`` and
	``V4L2_BUF_TYPE_META_OUTPUT``.
	See :c:type:`v4l2_buf_type`.
    * - __u32
      - ``flags``
      - See :ref:`fmtdesc-flags`
    * - __u8
      - ``description``\ [32]
      - Description of the format, a NUL-terminated ASCII string. This
	information is intended for the user, for example: "YUV 4:2:2".
    * - __u32
      - ``pixelformat``
      - The image format identifier. This is a four character code as
	computed by the v4l2_fourcc() macro:
    * - :cspan:`2`

	.. _v4l2-fourcc:

	``#define v4l2_fourcc(a,b,c,d)``

	``(((__u32)(a)<<0)|((__u32)(b)<<8)|((__u32)(c)<<16)|((__u32)(d)<<24))``

	Several image formats are already defined by this specification in
	:ref:`pixfmt`.

	.. attention::

	   These codes are not the same as those used
	   in the Windows world.
    * - __u32
      - ``mbus_code``
      - Media bus code restricting the enumerated formats, set by the
        application. Only applicable to drivers that advertise the
        ``V4L2_CAP_IO_MC`` :ref:`capability <device-capabilities>`, shall be 0
        otherwise.
    * - __u32
      - ``reserved``\ [3]
      - Reserved for future extensions. Drivers must set the array to
	zero.


.. tabularcolumns:: |p{8.4cm}|p{1.8cm}|p{7.1cm}|

.. cssclass:: longtable

.. _fmtdesc-flags:

.. flat-table:: Image Format Description Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_FMT_FLAG_COMPRESSED``
      - 0x0001
      - This is a compressed format.
    * - ``V4L2_FMT_FLAG_EMULATED``
      - 0x0002
      - This format is not native to the device but emulated through
	software (usually libv4l2), where possible try to use a native
	format instead for better performance.
    * - ``V4L2_FMT_FLAG_CONTINUOUS_BYTESTREAM``
      - 0x0004
      - The hardware decoder for this compressed bytestream format (aka coded
	format) is capable of parsing a continuous bytestream. Applications do
	not need to parse the bytestream themselves to find the boundaries
	between frames/fields.

	This flag can only be used in combination with the
	``V4L2_FMT_FLAG_COMPRESSED`` flag, since this applies to compressed
	formats only. This flag is valid for stateful decoders only.
    * - ``V4L2_FMT_FLAG_DYN_RESOLUTION``
      - 0x0008
      - Dynamic resolution switching is supported by the device for this
	compressed bytestream format (aka coded format). It will notify the user
	via the event ``V4L2_EVENT_SOURCE_CHANGE`` when changes in the video
	parameters are detected.

	This flag can only be used in combination with the
	``V4L2_FMT_FLAG_COMPRESSED`` flag, since this applies to
	compressed formats only. This flag is valid for stateful codecs only.
    * - ``V4L2_FMT_FLAG_ENC_CAP_FRAME_INTERVAL``
      - 0x0010
      - The hardware encoder supports setting the ``CAPTURE`` coded frame
	interval separately from the ``OUTPUT`` raw frame interval.
	Setting the ``OUTPUT`` raw frame interval with :ref:`VIDIOC_S_PARM <VIDIOC_G_PARM>`
	also sets the ``CAPTURE`` coded frame interval to the same value.
	If this flag is set, then the ``CAPTURE`` coded frame interval can be
	set to a different value afterwards. This is typically used for
	offline encoding where the ``OUTPUT`` raw frame interval is used as
	a hint for reserving hardware encoder resources and the ``CAPTURE`` coded
	frame interval is the actual frame rate embedded in the encoded video
	stream.

	This flag can only be used in combination with the
	``V4L2_FMT_FLAG_COMPRESSED`` flag, since this applies to
        compressed formats only. This flag is valid for stateful encoders only.
    * - ``V4L2_FMT_FLAG_CSC_COLORSPACE``
      - 0x0020
      - The driver allows the application to try to change the default
	colorspace. This flag is relevant only for capture devices.
	The application can ask to configure the colorspace of the capture device
	when calling the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl with
	:ref:`V4L2_PIX_FMT_FLAG_SET_CSC <v4l2-pix-fmt-flag-set-csc>` set.
    * - ``V4L2_FMT_FLAG_CSC_XFER_FUNC``
      - 0x0040
      - The driver allows the application to try to change the default
	transfer function. This flag is relevant only for capture devices.
	The application can ask to configure the transfer function of the capture
	device when calling the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl with
	:ref:`V4L2_PIX_FMT_FLAG_SET_CSC <v4l2-pix-fmt-flag-set-csc>` set.
    * - ``V4L2_FMT_FLAG_CSC_YCBCR_ENC``
      - 0x0080
      - The driver allows the application to try to change the default
	Y'CbCr encoding. This flag is relevant only for capture devices.
	The application can ask to configure the Y'CbCr encoding of the capture device
	when calling the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl with
	:ref:`V4L2_PIX_FMT_FLAG_SET_CSC <v4l2-pix-fmt-flag-set-csc>` set.
    * - ``V4L2_FMT_FLAG_CSC_HSV_ENC``
      - 0x0080
      - The driver allows the application to try to change the default
	HSV encoding. This flag is relevant only for capture devices.
	The application can ask to configure the HSV encoding of the capture device
	when calling the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl with
	:ref:`V4L2_PIX_FMT_FLAG_SET_CSC <v4l2-pix-fmt-flag-set-csc>` set.
    * - ``V4L2_FMT_FLAG_CSC_QUANTIZATION``
      - 0x0100
      - The driver allows the application to try to change the default
	quantization. This flag is relevant only for capture devices.
	The application can ask to configure the quantization of the capture
	device when calling the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl with
	:ref:`V4L2_PIX_FMT_FLAG_SET_CSC <v4l2-pix-fmt-flag-set-csc>` set.
    * - ``V4L2_FMT_FLAG_META_LINE_BASED``
      - 0x0200
      - The metadata format is line-based. In this case the ``width``,
	``height`` and ``bytesperline`` fields of :c:type:`v4l2_meta_format` are
	valid. The buffer consists of ``height`` lines, each having ``width``
	Data Units of data and the offset (in bytes) between the beginning of
	each two consecutive lines is ``bytesperline``.
    * - ``V4L2_FMTDESC_FLAG_ENUM_ALL``
      - 0x80000000
      - When the applications ORs ``index`` with ``V4L2_FMTDESC_FLAG_ENUM_ALL`` flag
        the driver enumerates all the possible pixel formats without taking care
        of any already set configuration. Drivers which do not support this flag,
        always return ``EINVAL`` without clearing this flag.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_fmtdesc` ``type`` is not
    supported or the ``index`` is out of bounds.

    If ``V4L2_CAP_IO_MC`` is set and the specified ``mbus_code``
    is unsupported, then also return this error code.
