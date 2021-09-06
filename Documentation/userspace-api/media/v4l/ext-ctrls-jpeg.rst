.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _jpeg-controls:

**********************
JPEG Control Reference
**********************

The JPEG class includes controls for common features of JPEG encoders
and decoders. Currently it includes features for codecs implementing
progressive baseline DCT compression process with Huffman entrophy
coding.


.. _jpeg-control-id:

JPEG Control IDs
================

``V4L2_CID_JPEG_CLASS (class)``
    The JPEG class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_JPEG_CHROMA_SUBSAMPLING (menu)``
    The chroma subsampling factors describe how each component of an
    input image is sampled, in respect to maximum sample rate in each
    spatial dimension. See :ref:`itu-t81`, clause A.1.1. for more
    details. The ``V4L2_CID_JPEG_CHROMA_SUBSAMPLING`` control determines
    how Cb and Cr components are downsampled after converting an input
    image from RGB to Y'CbCr color space.

.. tabularcolumns:: |p{7.5cm}|p{10.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_444``
      - No chroma subsampling, each pixel has Y, Cr and Cb values.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_422``
      - Horizontally subsample Cr, Cb components by a factor of 2.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_420``
      - Subsample Cr, Cb components horizontally and vertically by 2.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_411``
      - Horizontally subsample Cr, Cb components by a factor of 4.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_410``
      - Subsample Cr, Cb components horizontally by 4 and vertically by 2.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY``
      - Use only luminance component.



``V4L2_CID_JPEG_RESTART_INTERVAL (integer)``
    The restart interval determines an interval of inserting RSTm
    markers (m = 0..7). The purpose of these markers is to additionally
    reinitialize the encoder process, in order to process blocks of an
    image independently. For the lossy compression processes the restart
    interval unit is MCU (Minimum Coded Unit) and its value is contained
    in DRI (Define Restart Interval) marker. If
    ``V4L2_CID_JPEG_RESTART_INTERVAL`` control is set to 0, DRI and RSTm
    markers will not be inserted.

.. _jpeg-quality-control:

``V4L2_CID_JPEG_COMPRESSION_QUALITY (integer)``
    Determines trade-off between image quality and size.
    It provides simpler method for applications to control image quality,
    without a need for direct reconfiguration of luminance and chrominance
    quantization tables. In cases where a driver uses quantization tables
    configured directly by an application, using interfaces defined
    elsewhere, ``V4L2_CID_JPEG_COMPRESSION_QUALITY`` control should be set by
    driver to 0.

    The value range of this control is driver-specific. Only positive,
    non-zero values are meaningful. The recommended range is 1 - 100,
    where larger values correspond to better image quality.

.. _jpeg-active-marker-control:

``V4L2_CID_JPEG_ACTIVE_MARKER (bitmask)``
    Specify which JPEG markers are included in compressed stream. This
    control is valid only for encoders.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_JPEG_ACTIVE_MARKER_APP0``
      - Application data segment APP\ :sub:`0`.
    * - ``V4L2_JPEG_ACTIVE_MARKER_APP1``
      - Application data segment APP\ :sub:`1`.
    * - ``V4L2_JPEG_ACTIVE_MARKER_COM``
      - Comment segment.
    * - ``V4L2_JPEG_ACTIVE_MARKER_DQT``
      - Quantization tables segment.
    * - ``V4L2_JPEG_ACTIVE_MARKER_DHT``
      - Huffman tables segment.



For more details about JPEG specification, refer to :ref:`itu-t81`,
:ref:`jfif`, :ref:`w3c-jpeg-jfif`.
