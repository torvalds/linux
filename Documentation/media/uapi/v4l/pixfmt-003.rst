.. -*- coding: utf-8; mode: rst -*-

******************************
Multi-planar format structures
******************************

The struct :c:type:`v4l2_plane_pix_format` structures define size
and layout for each of the planes in a multi-planar format. The
struct :c:type:`v4l2_pix_format_mplane` structure contains
information common to all planes (such as image width and height) and an
array of struct :c:type:`v4l2_plane_pix_format` structures,
describing all planes of that format.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_plane_pix_format

.. flat-table:: struct v4l2_plane_pix_format
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``sizeimage``
      - Maximum size in bytes required for image data in this plane.
    * - __u32
      - ``bytesperline``
      - Distance in bytes between the leftmost pixels in two adjacent
	lines. See struct :c:type:`v4l2_pix_format`.
    * - __u16
      - ``reserved[6]``
      - Reserved for future extensions. Should be zeroed by drivers and
	applications.


.. tabularcolumns:: |p{4.4cm}|p{5.6cm}|p{7.5cm}|

.. c:type:: v4l2_pix_format_mplane

.. flat-table:: struct v4l2_pix_format_mplane
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``width``
      - Image width in pixels. See struct
	:c:type:`v4l2_pix_format`.
    * - __u32
      - ``height``
      - Image height in pixels. See struct
	:c:type:`v4l2_pix_format`.
    * - __u32
      - ``pixelformat``
      - The pixel format. Both single- and multi-planar four character
	codes can be used.
    * - enum :c:type:`v4l2_field`
      - ``field``
      - See struct :c:type:`v4l2_pix_format`.
    * - enum :c:type:`v4l2_colorspace`
      - ``colorspace``
      - See struct :c:type:`v4l2_pix_format`.
    * - struct :c:type:`v4l2_plane_pix_format`
      - ``plane_fmt[VIDEO_MAX_PLANES]``
      - An array of structures describing format of each plane this pixel
	format consists of. The number of valid entries in this array has
	to be put in the ``num_planes`` field.
    * - __u8
      - ``num_planes``
      - Number of planes (i.e. separate memory buffers) for this format
	and the number of valid entries in the ``plane_fmt`` array.
    * - __u8
      - ``flags``
      - Flags set by the application or driver, see :ref:`format-flags`.
    * - enum :c:type:`v4l2_ycbcr_encoding`
      - ``ycbcr_enc``
      - This information supplements the ``colorspace`` and must be set by
	the driver for capture streams and by the application for output
	streams, see :ref:`colorspaces`.
    * - enum :c:type:`v4l2_hsv_encoding`
      - ``hsv_enc``
      - This information supplements the ``colorspace`` and must be set by
	the driver for capture streams and by the application for output
	streams, see :ref:`colorspaces`.
    * - enum :c:type:`v4l2_quantization`
      - ``quantization``
      - This information supplements the ``colorspace`` and must be set by
	the driver for capture streams and by the application for output
	streams, see :ref:`colorspaces`.
    * - enum :c:type:`v4l2_xfer_func`
      - ``xfer_func``
      - This information supplements the ``colorspace`` and must be set by
	the driver for capture streams and by the application for output
	streams, see :ref:`colorspaces`.
    * - __u8
      - ``reserved[7]``
      - Reserved for future extensions. Should be zeroed by drivers and
	applications.
