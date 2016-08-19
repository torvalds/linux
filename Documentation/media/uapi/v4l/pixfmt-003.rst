.. -*- coding: utf-8; mode: rst -*-

******************************
Multi-planar format structures
******************************

The :ref:`struct v4l2_plane_pix_format <v4l2-plane-pix-format>` structures define size
and layout for each of the planes in a multi-planar format. The
:ref:`struct v4l2_pix_format_mplane <v4l2-pix-format-mplane>` structure contains
information common to all planes (such as image width and height) and an
array of :ref:`struct v4l2_plane_pix_format <v4l2-plane-pix-format>` structures,
describing all planes of that format.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. _v4l2-plane-pix-format:

.. flat-table:: struct v4l2_plane_pix_format
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``sizeimage``

       -  Maximum size in bytes required for image data in this plane.

    -  .. row 2

       -  __u32

       -  ``bytesperline``

       -  Distance in bytes between the leftmost pixels in two adjacent
	  lines. See struct :ref:`v4l2_pix_format <v4l2-pix-format>`.

    -  .. row 3

       -  __u16

       -  ``reserved[6]``

       -  Reserved for future extensions. Should be zeroed by drivers and
	  applications.


.. tabularcolumns:: |p{4.4cm}|p{5.6cm}|p{7.5cm}|

.. _v4l2-pix-format-mplane:

.. flat-table:: struct v4l2_pix_format_mplane
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``width``

       -  Image width in pixels. See struct
	  :ref:`v4l2_pix_format <v4l2-pix-format>`.

    -  .. row 2

       -  __u32

       -  ``height``

       -  Image height in pixels. See struct
	  :ref:`v4l2_pix_format <v4l2-pix-format>`.

    -  .. row 3

       -  __u32

       -  ``pixelformat``

       -  The pixel format. Both single- and multi-planar four character
	  codes can be used.

    -  .. row 4

       -  enum :ref:`v4l2_field <v4l2-field>`

       -  ``field``

       -  See struct :ref:`v4l2_pix_format <v4l2-pix-format>`.

    -  .. row 5

       -  enum :ref:`v4l2_colorspace <v4l2-colorspace>`

       -  ``colorspace``

       -  See struct :ref:`v4l2_pix_format <v4l2-pix-format>`.

    -  .. row 6

       -  struct :ref:`v4l2_plane_pix_format <v4l2-plane-pix-format>`

       -  ``plane_fmt[VIDEO_MAX_PLANES]``

       -  An array of structures describing format of each plane this pixel
	  format consists of. The number of valid entries in this array has
	  to be put in the ``num_planes`` field.

    -  .. row 7

       -  __u8

       -  ``num_planes``

       -  Number of planes (i.e. separate memory buffers) for this format
	  and the number of valid entries in the ``plane_fmt`` array.

    -  .. row 8

       -  __u8

       -  ``flags``

       -  Flags set by the application or driver, see :ref:`format-flags`.

    -  .. row 9

       -  enum :ref:`v4l2_ycbcr_encoding <v4l2-ycbcr-encoding>`

       -  ``ycbcr_enc``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 10

       -  enum :ref:`v4l2_quantization <v4l2-quantization>`

       -  ``quantization``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 11

       -  enum :ref:`v4l2_xfer_func <v4l2-xfer-func>`

       -  ``xfer_func``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 12

       -  __u8

       -  ``reserved[7]``

       -  Reserved for future extensions. Should be zeroed by drivers and
	  applications.
