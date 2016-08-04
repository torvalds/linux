.. -*- coding: utf-8; mode: rst -*-

********************************
Detailed Colorspace Descriptions
********************************


.. _col-smpte-170m:

Colorspace SMPTE 170M (V4L2_COLORSPACE_SMPTE170M)
=================================================

The :ref:`smpte170m` standard defines the colorspace used by NTSC and
PAL and by SDTV in general. The default transfer function is
``V4L2_XFER_FUNC_709``. The default Y'CbCr encoding is
``V4L2_YCBCR_ENC_601``. The default Y'CbCr quantization is limited
range. The chromaticities of the primary colors and the white reference
are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: SMPTE 170M Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.630

       -  0.340

    -  .. row 3

       -  Green

       -  0.310

       -  0.595

    -  .. row 4

       -  Blue

       -  0.155

       -  0.070

    -  .. row 5

       -  White Reference (D65)

       -  0.3127

       -  0.3290


The red, green and blue chromaticities are also often referred to as the
SMPTE C set, so this colorspace is sometimes called SMPTE C as well.

The transfer function defined for SMPTE 170M is the same as the one
defined in Rec. 709.

.. math::

    L' = -1.099(-L)^{0.45} + 0.099 \text{, for } L \le-0.018

    L' = 4.5L \text{, for } -0.018 < L < 0.018

    L' = 1.099L^{0.45} - 0.099 \text{, for } L \ge 0.018

Inverse Transfer function:

.. math::

    L = -\left( \frac{L' - 0.099}{-1.099} \right) ^{\frac{1}{0.45}} \text{, for } L' \le -0.081

    L = \frac{L'}{4.5} \text{, for } -0.081 < L' < 0.081

    L = \left(\frac{L' + 0.099}{1.099}\right)^{\frac{1}{0.45} } \text{, for } L' \ge 0.081

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_601`` encoding:

.. math::

    Y' = 0.2990R' + 0.5870G' + 0.1140B'

    Cb = -0.1687R' - 0.3313G' + 0.5B'

    Cr = 0.5R' - 0.4187G' - 0.0813B'

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5]. This conversion to Y'CbCr is identical to the one defined in
the :ref:`itu601` standard and this colorspace is sometimes called
BT.601 as well, even though BT.601 does not mention any color primaries.

The default quantization is limited range, but full range is possible
although rarely seen.


.. _col-rec709:

Colorspace Rec. 709 (V4L2_COLORSPACE_REC709)
============================================

The :ref:`itu709` standard defines the colorspace used by HDTV in
general. The default transfer function is ``V4L2_XFER_FUNC_709``. The
default Y'CbCr encoding is ``V4L2_YCBCR_ENC_709``. The default Y'CbCr
quantization is limited range. The chromaticities of the primary colors
and the white reference are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: Rec. 709 Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.640

       -  0.330

    -  .. row 3

       -  Green

       -  0.300

       -  0.600

    -  .. row 4

       -  Blue

       -  0.150

       -  0.060

    -  .. row 5

       -  White Reference (D65)

       -  0.3127

       -  0.3290


The full name of this standard is Rec. ITU-R BT.709-5.

Transfer function. Normally L is in the range [0…1], but for the
extended gamut xvYCC encoding values outside that range are allowed.

.. math::

    L' = -1.099(-L)^{0.45} + 0.099 \text{, for } L \le -0.018

    L' = 4.5L \text{, for } -0.018 < L < 0.018

    L' = 1.099L^{0.45} - 0.099 \text{, for } L \ge 0.018

Inverse Transfer function:

.. math::

    L = -\left( \frac{L' - 0.099}{-1.099} \right)^\frac{1}{0.45} \text{, for } L' \le -0.081

    L = \frac{L'}{4.5}\text{, for } -0.081 < L' < 0.081

    L = \left(\frac{L' + 0.099}{1.099}\right)^{\frac{1}{0.45} } \text{, for } L' \ge 0.081

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_709`` encoding:

.. math::

    Y' = 0.2126R' + 0.7152G' + 0.0722B'

    Cb = -0.1146R' - 0.3854G' + 0.5B'

    Cr = 0.5R' - 0.4542G' - 0.0458B'

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5].

The default quantization is limited range, but full range is possible
although rarely seen.

The ``V4L2_YCBCR_ENC_709`` encoding described above is the default for
this colorspace, but it can be overridden with ``V4L2_YCBCR_ENC_601``,
in which case the BT.601 Y'CbCr encoding is used.

Two additional extended gamut Y'CbCr encodings are also possible with
this colorspace:

The xvYCC 709 encoding (``V4L2_YCBCR_ENC_XV709``, :ref:`xvycc`) is
similar to the Rec. 709 encoding, but it allows for R', G' and B' values
that are outside the range [0…1]. The resulting Y', Cb and Cr values are
scaled and offset:

.. math::

    Y' = \frac{219}{256} * (0.2126R' + 0.7152G' + 0.0722B') + \frac{16}{256}

    Cb = \frac{224}{256} * (-0.1146R' - 0.3854G' + 0.5B')

    Cr = \frac{224}{256} * (0.5R' - 0.4542G' - 0.0458B')

The xvYCC 601 encoding (``V4L2_YCBCR_ENC_XV601``, :ref:`xvycc`) is
similar to the BT.601 encoding, but it allows for R', G' and B' values
that are outside the range [0…1]. The resulting Y', Cb and Cr values are
scaled and offset:

.. math::

    Y' = \frac{219}{256} * (0.2990R' + 0.5870G' + 0.1140B') + \frac{16}{256}

    Cb = \frac{224}{256} * (-0.1687R' - 0.3313G' + 0.5B')

    Cr = \frac{224}{256} * (0.5R' - 0.4187G' - 0.0813B')

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5]. The non-standard xvYCC 709 or xvYCC 601 encodings can be
used by selecting ``V4L2_YCBCR_ENC_XV709`` or ``V4L2_YCBCR_ENC_XV601``.
The xvYCC encodings always use full range quantization.


.. _col-srgb:

Colorspace sRGB (V4L2_COLORSPACE_SRGB)
======================================

The :ref:`srgb` standard defines the colorspace used by most webcams
and computer graphics. The default transfer function is
``V4L2_XFER_FUNC_SRGB``. The default Y'CbCr encoding is
``V4L2_YCBCR_ENC_601``. The default Y'CbCr quantization is full range.
The chromaticities of the primary colors and the white reference are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: sRGB Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.640

       -  0.330

    -  .. row 3

       -  Green

       -  0.300

       -  0.600

    -  .. row 4

       -  Blue

       -  0.150

       -  0.060

    -  .. row 5

       -  White Reference (D65)

       -  0.3127

       -  0.3290


These chromaticities are identical to the Rec. 709 colorspace.

Transfer function. Note that negative values for L are only used by the
Y'CbCr conversion.

.. math::

    L' = -1.055(-L)^{\frac{1}{2.4} } + 0.055\text{, for }L < -0.0031308

    L' = 12.92L\text{, for }-0.0031308 \le L \le 0.0031308

    L' = 1.055L ^{\frac{1}{2.4} } - 0.055\text{, for }0.0031308 < L \le 1

Inverse Transfer function:

.. math::

    L = -((-L' + 0.055) / 1.055) ^{2.4}\text{, for }L' < -0.04045

    L = L' / 12.92\text{, for }-0.04045 \le L' \le 0.04045

    L = ((L' + 0.055) / 1.055) ^{2.4}\text{, for }L' > 0.04045

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_601`` encoding as defined by :ref:`sycc`:

.. math::

    Y' = 0.2990R' + 0.5870G' + 0.1140B'

    Cb = -0.1687R' - 0.3313G' + 0.5B'

    Cr = 0.5R' - 0.4187G' - 0.0813B'

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5]. This transform is identical to one defined in SMPTE
170M/BT.601. The Y'CbCr quantization is full range.


.. _col-adobergb:

Colorspace Adobe RGB (V4L2_COLORSPACE_ADOBERGB)
===============================================

The :ref:`adobergb` standard defines the colorspace used by computer
graphics that use the AdobeRGB colorspace. This is also known as the
:ref:`oprgb` standard. The default transfer function is
``V4L2_XFER_FUNC_ADOBERGB``. The default Y'CbCr encoding is
``V4L2_YCBCR_ENC_601``. The default Y'CbCr quantization is full
range. The chromaticities of the primary colors and the white reference
are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: Adobe RGB Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.6400

       -  0.3300

    -  .. row 3

       -  Green

       -  0.2100

       -  0.7100

    -  .. row 4

       -  Blue

       -  0.1500

       -  0.0600

    -  .. row 5

       -  White Reference (D65)

       -  0.3127

       -  0.3290



Transfer function:

.. math::

    L' = L ^{\frac{1}{2.19921875}}

Inverse Transfer function:

.. math::

    L = L'^{(2.19921875)}

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_601`` encoding:

.. math::

    Y' = 0.2990R' + 0.5870G' + 0.1140B'

    Cb = -0.1687R' - 0.3313G' + 0.5B'

    Cr = 0.5R' - 0.4187G' - 0.0813B'

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5]. This transform is identical to one defined in SMPTE
170M/BT.601. The Y'CbCr quantization is full range.


.. _col-bt2020:

Colorspace BT.2020 (V4L2_COLORSPACE_BT2020)
===========================================

The :ref:`itu2020` standard defines the colorspace used by Ultra-high
definition television (UHDTV). The default transfer function is
``V4L2_XFER_FUNC_709``. The default Y'CbCr encoding is
``V4L2_YCBCR_ENC_BT2020``. The default R'G'B' quantization is limited
range (!), and so is the default Y'CbCr quantization. The chromaticities
of the primary colors and the white reference are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: BT.2020 Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.708

       -  0.292

    -  .. row 3

       -  Green

       -  0.170

       -  0.797

    -  .. row 4

       -  Blue

       -  0.131

       -  0.046

    -  .. row 5

       -  White Reference (D65)

       -  0.3127

       -  0.3290



Transfer function (same as Rec. 709):

.. math::

    L' = 4.5L\text{, for }0 \le L < 0.018

    L' = 1.099L ^{0.45} - 0.099\text{, for } 0.018 \le L \le 1

Inverse Transfer function:

.. math::

    L = L' / 4.5\text{, for } L' < 0.081

    L = \left( \frac{L' + 0.099}{1.099}\right) ^{\frac{1}{0.45} }\text{, for } L' \ge 0.081

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_BT2020`` encoding:

.. math::

    Y' = 0.2627R' + 0.6780G' + 0.0593B'

    Cb = -0.1396R' - 0.3604G' + 0.5B'

    Cr = 0.5R' - 0.4598G' - 0.0402B'

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5]. The Y'CbCr quantization is limited range.

There is also an alternate constant luminance R'G'B' to Yc'CbcCrc
(``V4L2_YCBCR_ENC_BT2020_CONST_LUM``) encoding:

Luma:

.. math::
    :nowrap:

    \begin{align*}
    Yc' = (0.2627R + 0.6780G + 0.0593B)'& \\
    B' - Yc' \le 0:& \\
        &Cbc = (B' - Yc') / 1.9404 \\
    B' - Yc' > 0: & \\
        &Cbc = (B' - Yc') / 1.5816 \\
    R' - Yc' \le 0:& \\
        &Crc = (R' - Y') / 1.7184 \\
    R' - Yc' > 0:& \\
        &Crc = (R' - Y') / 0.9936
    \end{align*}

Yc' is clamped to the range [0…1] and Cbc and Crc are clamped to the
range [-0.5…0.5]. The Yc'CbcCrc quantization is limited range.


.. _col-dcip3:

Colorspace DCI-P3 (V4L2_COLORSPACE_DCI_P3)
==========================================

The :ref:`smpte431` standard defines the colorspace used by cinema
projectors that use the DCI-P3 colorspace. The default transfer function
is ``V4L2_XFER_FUNC_DCI_P3``. The default Y'CbCr encoding is
``V4L2_YCBCR_ENC_709``. The default Y'CbCr quantization is limited range.

.. note::

   Note that this colorspace standard does not specify a
   Y'CbCr encoding since it is not meant to be encoded to Y'CbCr. So this
   default Y'CbCr encoding was picked because it is the HDTV encoding.

The chromaticities of the primary colors and the white reference are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: DCI-P3 Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.6800

       -  0.3200

    -  .. row 3

       -  Green

       -  0.2650

       -  0.6900

    -  .. row 4

       -  Blue

       -  0.1500

       -  0.0600

    -  .. row 5

       -  White Reference

       -  0.3140

       -  0.3510



Transfer function:

.. math::

    L' = L^{\frac{1}{2.6}}

Inverse Transfer function:

.. math::

    L = L'^{(2.6)}

Y'CbCr encoding is not specified. V4L2 defaults to Rec. 709.


.. _col-smpte-240m:

Colorspace SMPTE 240M (V4L2_COLORSPACE_SMPTE240M)
=================================================

The :ref:`smpte240m` standard was an interim standard used during the
early days of HDTV (1988-1998). It has been superseded by Rec. 709. The
default transfer function is ``V4L2_XFER_FUNC_SMPTE240M``. The default
Y'CbCr encoding is ``V4L2_YCBCR_ENC_SMPTE240M``. The default Y'CbCr
quantization is limited range. The chromaticities of the primary colors
and the white reference are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: SMPTE 240M Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.630

       -  0.340

    -  .. row 3

       -  Green

       -  0.310

       -  0.595

    -  .. row 4

       -  Blue

       -  0.155

       -  0.070

    -  .. row 5

       -  White Reference (D65)

       -  0.3127

       -  0.3290


These chromaticities are identical to the SMPTE 170M colorspace.

Transfer function:

.. math::

    L' = 4L\text{, for } 0 \le L < 0.0228

    L' = 1.1115L ^{0.45} - 0.1115\text{, for } 0.0228 \le L \le 1

Inverse Transfer function:

.. math::

    L = \frac{L'}{4}\text{, for } 0 \le L' < 0.0913

    L = \left( \frac{L' + 0.1115}{1.1115}\right) ^{\frac{1}{0.45} }\text{, for } L' \ge 0.0913

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_SMPTE240M`` encoding:

.. math::

    Y' = 0.2122R' + 0.7013G' + 0.0865B'

    Cb = -0.1161R' - 0.3839G' + 0.5B'

    Cr = 0.5R' - 0.4451G' - 0.0549B'

Yc' is clamped to the range [0…1] and Cbc and Crc are clamped to the
range [-0.5…0.5]. The Y'CbCr quantization is limited range.


.. _col-sysm:

Colorspace NTSC 1953 (V4L2_COLORSPACE_470_SYSTEM_M)
===================================================

This standard defines the colorspace used by NTSC in 1953. In practice
this colorspace is obsolete and SMPTE 170M should be used instead. The
default transfer function is ``V4L2_XFER_FUNC_709``. The default Y'CbCr
encoding is ``V4L2_YCBCR_ENC_601``. The default Y'CbCr quantization is
limited range. The chromaticities of the primary colors and the white
reference are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: NTSC 1953 Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.67

       -  0.33

    -  .. row 3

       -  Green

       -  0.21

       -  0.71

    -  .. row 4

       -  Blue

       -  0.14

       -  0.08

    -  .. row 5

       -  White Reference (C)

       -  0.310

       -  0.316


.. note::

   This colorspace uses Illuminant C instead of D65 as the white
   reference. To correctly convert an image in this colorspace to another
   that uses D65 you need to apply a chromatic adaptation algorithm such as
   the Bradford method.

The transfer function was never properly defined for NTSC 1953. The Rec.
709 transfer function is recommended in the literature:

.. math::

    L' = 4.5L\text{, for } 0 \le L < 0.018

    L' = 1.099L ^{0.45} - 0.099\text{, for } 0.018 \le L \le 1

Inverse Transfer function:

.. math::

    L = \frac{L'}{4.5} \text{, for } L' < 0.081

    L = \left( \frac{L' + 0.099}{1.099}\right) ^{\frac{1}{0.45} }\text{, for } L' \ge 0.081

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_601`` encoding:

.. math::

    Y' = 0.2990R' + 0.5870G' + 0.1140B'

    Cb = -0.1687R' - 0.3313G' + 0.5B'

    Cr = 0.5R' - 0.4187G' - 0.0813B'

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5]. The Y'CbCr quantization is limited range. This transform is
identical to one defined in SMPTE 170M/BT.601.


.. _col-sysbg:

Colorspace EBU Tech. 3213 (V4L2_COLORSPACE_470_SYSTEM_BG)
=========================================================

The :ref:`tech3213` standard defines the colorspace used by PAL/SECAM
in 1975. In practice this colorspace is obsolete and SMPTE 170M should
be used instead. The default transfer function is
``V4L2_XFER_FUNC_709``. The default Y'CbCr encoding is
``V4L2_YCBCR_ENC_601``. The default Y'CbCr quantization is limited
range. The chromaticities of the primary colors and the white reference
are:



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: EBU Tech. 3213 Chromaticities
    :header-rows:  1
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Color

       -  x

       -  y

    -  .. row 2

       -  Red

       -  0.64

       -  0.33

    -  .. row 3

       -  Green

       -  0.29

       -  0.60

    -  .. row 4

       -  Blue

       -  0.15

       -  0.06

    -  .. row 5

       -  White Reference (D65)

       -  0.3127

       -  0.3290



The transfer function was never properly defined for this colorspace.
The Rec. 709 transfer function is recommended in the literature:

.. math::

    L' = 4.5L\text{, for } 0 \le L < 0.018

    L' = 1.099L ^{0.45} - 0.099\text{, for } 0.018 \le L \le 1

Inverse Transfer function:

.. math::

    L = \frac{L'}{4.5} \text{, for } L' < 0.081

    L = \left(\frac{L' + 0.099}{1.099} \right) ^{\frac{1}{0.45} }\text{, for } L' \ge 0.081

The luminance (Y') and color difference (Cb and Cr) are obtained with
the following ``V4L2_YCBCR_ENC_601`` encoding:

.. math::

    Y' = 0.2990R' + 0.5870G' + 0.1140B'

    Cb = -0.1687R' - 0.3313G' + 0.5B'

    Cr = 0.5R' - 0.4187G' - 0.0813B'

Y' is clamped to the range [0…1] and Cb and Cr are clamped to the range
[-0.5…0.5]. The Y'CbCr quantization is limited range. This transform is
identical to one defined in SMPTE 170M/BT.601.


.. _col-jpeg:

Colorspace JPEG (V4L2_COLORSPACE_JPEG)
======================================

This colorspace defines the colorspace used by most (Motion-)JPEG
formats. The chromaticities of the primary colors and the white
reference are identical to sRGB. The transfer function use is
``V4L2_XFER_FUNC_SRGB``. The Y'CbCr encoding is ``V4L2_YCBCR_ENC_601``
with full range quantization where Y' is scaled to [0…255] and Cb/Cr are
scaled to [-128…128] and then clipped to [-128…127].

.. note::

   The JPEG standard does not actually store colorspace
   information. So if something other than sRGB is used, then the driver
   will have to set that information explicitly. Effectively
   ``V4L2_COLORSPACE_JPEG`` can be considered to be an abbreviation for
   ``V4L2_COLORSPACE_SRGB``, ``V4L2_YCBCR_ENC_601`` and
   ``V4L2_QUANTIZATION_FULL_RANGE``.
