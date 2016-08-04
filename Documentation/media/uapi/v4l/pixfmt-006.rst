.. -*- coding: utf-8; mode: rst -*-

****************************
Defining Colorspaces in V4L2
****************************

In V4L2 colorspaces are defined by four values. The first is the
colorspace identifier (enum :ref:`v4l2_colorspace <v4l2-colorspace>`)
which defines the chromaticities, the default transfer function, the
default Y'CbCr encoding and the default quantization method. The second
is the transfer function identifier (enum
:ref:`v4l2_xfer_func <v4l2-xfer-func>`) to specify non-standard
transfer functions. The third is the Y'CbCr encoding identifier (enum
:ref:`v4l2_ycbcr_encoding <v4l2-ycbcr-encoding>`) to specify
non-standard Y'CbCr encodings and the fourth is the quantization
identifier (enum :ref:`v4l2_quantization <v4l2-quantization>`) to
specify non-standard quantization methods. Most of the time only the
colorspace field of struct :ref:`v4l2_pix_format <v4l2-pix-format>`
or struct :ref:`v4l2_pix_format_mplane <v4l2-pix-format-mplane>`
needs to be filled in.

.. note::

   The default R'G'B' quantization is full range for all
   colorspaces except for BT.2020 which uses limited range R'G'B'
   quantization.

.. tabularcolumns:: |p{6.0cm}|p{11.5cm}|

.. _v4l2-colorspace:

.. flat-table:: V4L2 Colorspaces
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Details

    -  .. row 2

       -  ``V4L2_COLORSPACE_DEFAULT``

       -  The default colorspace. This can be used by applications to let
	  the driver fill in the colorspace.

    -  .. row 3

       -  ``V4L2_COLORSPACE_SMPTE170M``

       -  See :ref:`col-smpte-170m`.

    -  .. row 4

       -  ``V4L2_COLORSPACE_REC709``

       -  See :ref:`col-rec709`.

    -  .. row 5

       -  ``V4L2_COLORSPACE_SRGB``

       -  See :ref:`col-srgb`.

    -  .. row 6

       -  ``V4L2_COLORSPACE_ADOBERGB``

       -  See :ref:`col-adobergb`.

    -  .. row 7

       -  ``V4L2_COLORSPACE_BT2020``

       -  See :ref:`col-bt2020`.

    -  .. row 8

       -  ``V4L2_COLORSPACE_DCI_P3``

       -  See :ref:`col-dcip3`.

    -  .. row 9

       -  ``V4L2_COLORSPACE_SMPTE240M``

       -  See :ref:`col-smpte-240m`.

    -  .. row 10

       -  ``V4L2_COLORSPACE_470_SYSTEM_M``

       -  See :ref:`col-sysm`.

    -  .. row 11

       -  ``V4L2_COLORSPACE_470_SYSTEM_BG``

       -  See :ref:`col-sysbg`.

    -  .. row 12

       -  ``V4L2_COLORSPACE_JPEG``

       -  See :ref:`col-jpeg`.

    -  .. row 13

       -  ``V4L2_COLORSPACE_RAW``

       -  The raw colorspace. This is used for raw image capture where the
	  image is minimally processed and is using the internal colorspace
	  of the device. The software that processes an image using this
	  'colorspace' will have to know the internals of the capture
	  device.



.. _v4l2-xfer-func:

.. flat-table:: V4L2 Transfer Function
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Details

    -  .. row 2

       -  ``V4L2_XFER_FUNC_DEFAULT``

       -  Use the default transfer function as defined by the colorspace.

    -  .. row 3

       -  ``V4L2_XFER_FUNC_709``

       -  Use the Rec. 709 transfer function.

    -  .. row 4

       -  ``V4L2_XFER_FUNC_SRGB``

       -  Use the sRGB transfer function.

    -  .. row 5

       -  ``V4L2_XFER_FUNC_ADOBERGB``

       -  Use the AdobeRGB transfer function.

    -  .. row 6

       -  ``V4L2_XFER_FUNC_SMPTE240M``

       -  Use the SMPTE 240M transfer function.

    -  .. row 7

       -  ``V4L2_XFER_FUNC_NONE``

       -  Do not use a transfer function (i.e. use linear RGB values).

    -  .. row 8

       -  ``V4L2_XFER_FUNC_DCI_P3``

       -  Use the DCI-P3 transfer function.

    -  .. row 9

       -  ``V4L2_XFER_FUNC_SMPTE2084``

       -  Use the SMPTE 2084 transfer function.



.. _v4l2-ycbcr-encoding:

.. tabularcolumns:: |p{6.5cm}|p{11.0cm}|

.. flat-table:: V4L2 Y'CbCr Encodings
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Details

    -  .. row 2

       -  ``V4L2_YCBCR_ENC_DEFAULT``

       -  Use the default Y'CbCr encoding as defined by the colorspace.

    -  .. row 3

       -  ``V4L2_YCBCR_ENC_601``

       -  Use the BT.601 Y'CbCr encoding.

    -  .. row 4

       -  ``V4L2_YCBCR_ENC_709``

       -  Use the Rec. 709 Y'CbCr encoding.

    -  .. row 5

       -  ``V4L2_YCBCR_ENC_XV601``

       -  Use the extended gamut xvYCC BT.601 encoding.

    -  .. row 6

       -  ``V4L2_YCBCR_ENC_XV709``

       -  Use the extended gamut xvYCC Rec. 709 encoding.

    -  .. row 7

       -  ``V4L2_YCBCR_ENC_BT2020``

       -  Use the default non-constant luminance BT.2020 Y'CbCr encoding.

    -  .. row 8

       -  ``V4L2_YCBCR_ENC_BT2020_CONST_LUM``

       -  Use the constant luminance BT.2020 Yc'CbcCrc encoding.

    -  .. row 9

       -  ``V4L2_YCBCR_ENC_SMPTE_240M``

       -  Use the SMPTE 240M Y'CbCr encoding.



.. _v4l2-quantization:

.. tabularcolumns:: |p{6.5cm}|p{11.0cm}|

.. flat-table:: V4L2 Quantization Methods
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Details

    -  .. row 2

       -  ``V4L2_QUANTIZATION_DEFAULT``

       -  Use the default quantization encoding as defined by the
	  colorspace. This is always full range for R'G'B' (except for the
	  BT.2020 colorspace) and usually limited range for Y'CbCr.

    -  .. row 3

       -  ``V4L2_QUANTIZATION_FULL_RANGE``

       -  Use the full range quantization encoding. I.e. the range [0…1] is
	  mapped to [0…255] (with possible clipping to [1…254] to avoid the
	  0x00 and 0xff values). Cb and Cr are mapped from [-0.5…0.5] to
	  [0…255] (with possible clipping to [1…254] to avoid the 0x00 and
	  0xff values).

    -  .. row 4

       -  ``V4L2_QUANTIZATION_LIM_RANGE``

       -  Use the limited range quantization encoding. I.e. the range [0…1]
	  is mapped to [16…235]. Cb and Cr are mapped from [-0.5…0.5] to
	  [16…240].
