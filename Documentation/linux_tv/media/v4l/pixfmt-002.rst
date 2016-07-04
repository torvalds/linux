.. -*- coding: utf-8; mode: rst -*-

******************************
Single-planar format structure
******************************


.. _v4l2-pix-format:

.. flat-table:: struct v4l2_pix_format
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``width``

       -  Image width in pixels.

    -  .. row 2

       -  __u32

       -  ``height``

       -  Image height in pixels. If ``field`` is one of ``V4L2_FIELD_TOP``,
	  ``V4L2_FIELD_BOTTOM`` or ``V4L2_FIELD_ALTERNATE`` then height
	  refers to the number of lines in the field, otherwise it refers to
	  the number of lines in the frame (which is twice the field height
	  for interlaced formats).

    -  .. row 3

       -  :cspan:`2` Applications set these fields to request an image
	  size, drivers return the closest possible values. In case of
	  planar formats the ``width`` and ``height`` applies to the largest
	  plane. To avoid ambiguities drivers must return values rounded up
	  to a multiple of the scale factor of any smaller planes. For
	  example when the image format is YUV 4:2:0, ``width`` and
	  ``height`` must be multiples of two.

    -  .. row 4

       -  __u32

       -  ``pixelformat``

       -  The pixel format or type of compression, set by the application.
	  This is a little endian
	  :ref:`four character code <v4l2-fourcc>`. V4L2 defines standard
	  RGB formats in :ref:`rgb-formats`, YUV formats in
	  :ref:`yuv-formats`, and reserved codes in
	  :ref:`reserved-formats`

    -  .. row 5

       -  enum :ref:`v4l2_field <v4l2-field>`

       -  ``field``

       -  Video images are typically interlaced. Applications can request to
	  capture or output only the top or bottom field, or both fields
	  interlaced or sequentially stored in one buffer or alternating in
	  separate buffers. Drivers return the actual field order selected.
	  For more details on fields see :ref:`field-order`.

    -  .. row 6

       -  __u32

       -  ``bytesperline``

       -  Distance in bytes between the leftmost pixels in two adjacent
	  lines.

    -  .. row 7

       -  :cspan:`2`

	  Both applications and drivers can set this field to request
	  padding bytes at the end of each line. Drivers however may ignore
	  the value requested by the application, returning ``width`` times
	  bytes per pixel or a larger value required by the hardware. That
	  implies applications can just set this field to zero to get a
	  reasonable default.

	  Video hardware may access padding bytes, therefore they must
	  reside in accessible memory. Consider cases where padding bytes
	  after the last line of an image cross a system page boundary.
	  Input devices may write padding bytes, the value is undefined.
	  Output devices ignore the contents of padding bytes.

	  When the image format is planar the ``bytesperline`` value applies
	  to the first plane and is divided by the same factor as the
	  ``width`` field for the other planes. For example the Cb and Cr
	  planes of a YUV 4:2:0 image have half as many padding bytes
	  following each line as the Y plane. To avoid ambiguities drivers
	  must return a ``bytesperline`` value rounded up to a multiple of
	  the scale factor.

	  For compressed formats the ``bytesperline`` value makes no sense.
	  Applications and drivers must set this to 0 in that case.

    -  .. row 8

       -  __u32

       -  ``sizeimage``

       -  Size in bytes of the buffer to hold a complete image, set by the
	  driver. Usually this is ``bytesperline`` times ``height``. When
	  the image consists of variable length compressed data this is the
	  maximum number of bytes required to hold an image.

    -  .. row 9

       -  enum :ref:`v4l2_colorspace <v4l2-colorspace>`

       -  ``colorspace``

       -  This information supplements the ``pixelformat`` and must be set
	  by the driver for capture streams and by the application for
	  output streams, see :ref:`colorspaces`.

    -  .. row 10

       -  __u32

       -  ``priv``

       -  This field indicates whether the remaining fields of the
	  :ref:`struct v4l2_pix_format <v4l2-pix-format>` structure, also called the
	  extended fields, are valid. When set to
	  ``V4L2_PIX_FMT_PRIV_MAGIC``, it indicates that the extended fields
	  have been correctly initialized. When set to any other value it
	  indicates that the extended fields contain undefined values.

	  Applications that wish to use the pixel format extended fields
	  must first ensure that the feature is supported by querying the
	  device for the :ref:`V4L2_CAP_EXT_PIX_FORMAT <querycap>`
	  capability. If the capability isn't set the pixel format extended
	  fields are not supported and using the extended fields will lead
	  to undefined results.

	  To use the extended fields, applications must set the ``priv``
	  field to ``V4L2_PIX_FMT_PRIV_MAGIC``, initialize all the extended
	  fields and zero the unused bytes of the
	  :ref:`struct v4l2_format <v4l2-format>` ``raw_data`` field.

	  When the ``priv`` field isn't set to ``V4L2_PIX_FMT_PRIV_MAGIC``
	  drivers must act as if all the extended fields were set to zero.
	  On return drivers must set the ``priv`` field to
	  ``V4L2_PIX_FMT_PRIV_MAGIC`` and all the extended fields to
	  applicable values.

    -  .. row 11

       -  __u32

       -  ``flags``

       -  Flags set by the application or driver, see :ref:`format-flags`.

    -  .. row 12

       -  enum :ref:`v4l2_ycbcr_encoding <v4l2-ycbcr-encoding>`

       -  ``ycbcr_enc``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 13

       -  enum :ref:`v4l2_quantization <v4l2-quantization>`

       -  ``quantization``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 14

       -  enum :ref:`v4l2_xfer_func <v4l2-xfer-func>`

       -  ``xfer_func``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.
