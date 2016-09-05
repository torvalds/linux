.. -*- coding: utf-8; mode: rst -*-

****************************
Defining Colorspaces in V4L2
****************************

In V4L2 colorspaces are defined by four values. The first is the
colorspace identifier (enum :c:type:`v4l2_colorspace`)
which defines the chromaticities, the default transfer function, the
default Y'CbCr encoding and the default quantization method. The second
is the transfer function identifier (enum
:c:type:`v4l2_xfer_func`) to specify non-standard
transfer functions. The third is the Y'CbCr encoding identifier (enum
:c:type:`v4l2_ycbcr_encoding`) to specify
non-standard Y'CbCr encodings and the fourth is the quantization
identifier (enum :c:type:`v4l2_quantization`) to
specify non-standard quantization methods. Most of the time only the
colorspace field of struct :c:type:`v4l2_pix_format`
or struct :c:type:`v4l2_pix_format_mplane`
needs to be filled in.

.. note::

   The default R'G'B' quantization is full range for all
   colorspaces except for BT.2020 which uses limited range R'G'B'
   quantization.

.. tabularcolumns:: |p{6.0cm}|p{11.5cm}|

.. c:type:: v4l2_colorspace

.. flat-table:: V4L2 Colorspaces
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Details
    * - ``V4L2_COLORSPACE_DEFAULT``
      - The default colorspace. This can be used by applications to let
	the driver fill in the colorspace.
    * - ``V4L2_COLORSPACE_SMPTE170M``
      - See :ref:`col-smpte-170m`.
    * - ``V4L2_COLORSPACE_REC709``
      - See :ref:`col-rec709`.
    * - ``V4L2_COLORSPACE_SRGB``
      - See :ref:`col-srgb`.
    * - ``V4L2_COLORSPACE_ADOBERGB``
      - See :ref:`col-adobergb`.
    * - ``V4L2_COLORSPACE_BT2020``
      - See :ref:`col-bt2020`.
    * - ``V4L2_COLORSPACE_DCI_P3``
      - See :ref:`col-dcip3`.
    * - ``V4L2_COLORSPACE_SMPTE240M``
      - See :ref:`col-smpte-240m`.
    * - ``V4L2_COLORSPACE_470_SYSTEM_M``
      - See :ref:`col-sysm`.
    * - ``V4L2_COLORSPACE_470_SYSTEM_BG``
      - See :ref:`col-sysbg`.
    * - ``V4L2_COLORSPACE_JPEG``
      - See :ref:`col-jpeg`.
    * - ``V4L2_COLORSPACE_RAW``
      - The raw colorspace. This is used for raw image capture where the
	image is minimally processed and is using the internal colorspace
	of the device. The software that processes an image using this
	'colorspace' will have to know the internals of the capture
	device.



.. c:type:: v4l2_xfer_func

.. flat-table:: V4L2 Transfer Function
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Details
    * - ``V4L2_XFER_FUNC_DEFAULT``
      - Use the default transfer function as defined by the colorspace.
    * - ``V4L2_XFER_FUNC_709``
      - Use the Rec. 709 transfer function.
    * - ``V4L2_XFER_FUNC_SRGB``
      - Use the sRGB transfer function.
    * - ``V4L2_XFER_FUNC_ADOBERGB``
      - Use the AdobeRGB transfer function.
    * - ``V4L2_XFER_FUNC_SMPTE240M``
      - Use the SMPTE 240M transfer function.
    * - ``V4L2_XFER_FUNC_NONE``
      - Do not use a transfer function (i.e. use linear RGB values).
    * - ``V4L2_XFER_FUNC_DCI_P3``
      - Use the DCI-P3 transfer function.
    * - ``V4L2_XFER_FUNC_SMPTE2084``
      - Use the SMPTE 2084 transfer function.



.. c:type:: v4l2_ycbcr_encoding

.. tabularcolumns:: |p{6.5cm}|p{11.0cm}|

.. flat-table:: V4L2 Y'CbCr Encodings
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Details
    * - ``V4L2_YCBCR_ENC_DEFAULT``
      - Use the default Y'CbCr encoding as defined by the colorspace.
    * - ``V4L2_YCBCR_ENC_601``
      - Use the BT.601 Y'CbCr encoding.
    * - ``V4L2_YCBCR_ENC_709``
      - Use the Rec. 709 Y'CbCr encoding.
    * - ``V4L2_YCBCR_ENC_XV601``
      - Use the extended gamut xvYCC BT.601 encoding.
    * - ``V4L2_YCBCR_ENC_XV709``
      - Use the extended gamut xvYCC Rec. 709 encoding.
    * - ``V4L2_YCBCR_ENC_BT2020``
      - Use the default non-constant luminance BT.2020 Y'CbCr encoding.
    * - ``V4L2_YCBCR_ENC_BT2020_CONST_LUM``
      - Use the constant luminance BT.2020 Yc'CbcCrc encoding.
    * - ``V4L2_YCBCR_ENC_SMPTE_240M``
      - Use the SMPTE 240M Y'CbCr encoding.



.. c:type:: v4l2_quantization

.. tabularcolumns:: |p{6.5cm}|p{11.0cm}|

.. flat-table:: V4L2 Quantization Methods
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Details
    * - ``V4L2_QUANTIZATION_DEFAULT``
      - Use the default quantization encoding as defined by the
	colorspace. This is always full range for R'G'B' (except for the
	BT.2020 colorspace) and usually limited range for Y'CbCr.
    * - ``V4L2_QUANTIZATION_FULL_RANGE``
      - Use the full range quantization encoding. I.e. the range [0…1] is
	mapped to [0…255] (with possible clipping to [1…254] to avoid the
	0x00 and 0xff values). Cb and Cr are mapped from [-0.5…0.5] to
	[0…255] (with possible clipping to [1…254] to avoid the 0x00 and
	0xff values).
    * - ``V4L2_QUANTIZATION_LIM_RANGE``
      - Use the limited range quantization encoding. I.e. the range [0…1]
	is mapped to [16…235]. Cb and Cr are mapped from [-0.5…0.5] to
	[16…240].
