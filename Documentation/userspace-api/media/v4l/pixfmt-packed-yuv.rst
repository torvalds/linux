.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _packed-yuv:

******************
Packed YUV formats
******************

Similarly to the packed RGB formats, the packed YUV formats store the Y, Cb and
Cr components consecutively in memory. They may apply subsampling to the chroma
components and thus differ in how they interlave the three components.

.. note::

   - In all the tables that follow, bit 7 is the most significant bit in a byte.
   - 'Y', 'Cb' and 'Cr' denote bits of the luma, blue chroma (also known as
     'U') and red chroma (also known as 'V') components respectively. 'A'
     denotes bits of the alpha component (if supported by the format), and 'X'
     denotes padding bits.


4:4:4 Subsampling
=================

These formats do not subsample the chroma components and store each pixels as a
full triplet of Y, Cb and Cr values.

The next table lists the packed YUV 4:4:4 formats with less than 8 bits per
component. They are named based on the order of the Y, Cb and Cr components as
seen in a 16-bit word, which is then stored in memory in little endian byte
order, and on the number of bits for each component. For instance the YUV565
format stores a pixel in a 16-bit word [15:0] laid out at as [Y'\ :sub:`4-0`
Cb\ :sub:`5-0` Cr\ :sub:`4-0`], and stored in memory in two bytes,
[Cb\ :sub:`2-0` Cr\ :sub:`4-0`] followed by [Y'\ :sub:`4-0` Cb\ :sub:`5-3`].

.. raw:: latex

    \begingroup
    \scriptsize
    \setlength{\tabcolsep}{2pt}

.. tabularcolumns:: |p{3.5cm}|p{0.96cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|p{0.52cm}|

.. flat-table:: Packed YUV 4:4:4 Image Formats (less than 8bpc)
    :header-rows:  2
    :stub-columns: 0

    * - Identifier
      - Code

      - :cspan:`7` Byte 0 in memory

      - :cspan:`7` Byte 1

    * -
      -
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0

      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0

    * .. _V4L2-PIX-FMT-YUV444:

      - ``V4L2_PIX_FMT_YUV444``
      - 'Y444'

      - Cb\ :sub:`3`
      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`

    * .. _V4L2-PIX-FMT-YUV555:

      - ``V4L2_PIX_FMT_YUV555``
      - 'YUVO'

      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`
      - Cr\ :sub:`4`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

      - a
      - Y'\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`
      - Cb\ :sub:`4`
      - Cb\ :sub:`3`

    * .. _V4L2-PIX-FMT-YUV565:

      - ``V4L2_PIX_FMT_YUV565``
      - 'YUVP'

      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`
      - Cr\ :sub:`4`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

      - Y'\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`
      - Cb\ :sub:`5`
      - Cb\ :sub:`4`
      - Cb\ :sub:`3`

.. raw:: latex

    \endgroup

.. note::

    For the YUV444 and YUV555 formats, the value of alpha bits is undefined
    when reading from the driver, ignored when writing to the driver, except
    when alpha blending has been negotiated for a :ref:`Video Overlay
    <overlay>` or :ref:`Video Output Overlay <osd>`.


The next table lists the packed YUV 4:4:4 formats with 8 bits per component.
They are named based on the order of the Y, Cb and Cr components as stored in
memory, and on the total number of bits per pixel. For instance, the VUYX32
format stores a pixel with Cr\ :sub:`7-0` in the first byte, Cb\ :sub:`7-0` in
the second byte and Y'\ :sub:`7-0` in the third byte.

.. flat-table:: Packed YUV Image Formats (8bpc)
    :header-rows: 1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0
      - Byte 1
      - Byte 2
      - Byte 3

    * .. _V4L2-PIX-FMT-YUV32:

      - ``V4L2_PIX_FMT_YUV32``
      - 'YUV4'

      - A\ :sub:`7-0`
      - Y'\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Cr\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-AYUV32:

      - ``V4L2_PIX_FMT_AYUV32``
      - 'AYUV'

      - A\ :sub:`7-0`
      - Y'\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Cr\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-XYUV32:

      - ``V4L2_PIX_FMT_XYUV32``
      - 'XYUV'

      - X\ :sub:`7-0`
      - Y'\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Cr\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-VUYA32:

      - ``V4L2_PIX_FMT_VUYA32``
      - 'VUYA'

      - Cr\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Y'\ :sub:`7-0`
      - A\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-VUYX32:

      - ``V4L2_PIX_FMT_VUYX32``
      - 'VUYX'

      - Cr\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Y'\ :sub:`7-0`
      - X\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-YUVA32:

      - ``V4L2_PIX_FMT_YUVA32``
      - 'YUVA'

      - Y'\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Cr\ :sub:`7-0`
      - A\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-YUVX32:

      - ``V4L2_PIX_FMT_YUVX32``
      - 'YUVX'

      - Y'\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Cr\ :sub:`7-0`
      - X\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-YUV24:

      - ``V4L2_PIX_FMT_YUV24``
      - 'YUV3'

      - Y'\ :sub:`7-0`
      - Cb\ :sub:`7-0`
      - Cr\ :sub:`7-0`
      - -\

.. note::

    - The alpha component is expected to contain a meaningful value that can be
      used by drivers and applications.
    - The padding bits contain undefined values that must be ignored by all
      applications and drivers.

The next table lists the packed YUV 4:4:4 formats with 12 bits per component.
Expand the bits per component to 16 bits, data in the high bits, zeros in the low bits,
arranged in little endian order, storing 1 pixel in 6 bytes.

.. flat-table:: Packed YUV 4:4:4 Image Formats (12bpc)
    :header-rows: 1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 1-0
      - Byte 3-2
      - Byte 5-4
      - Byte 7-6
      - Byte 9-8
      - Byte 11-10

    * .. _V4L2-PIX-FMT-YUV48-12:

      - ``V4L2_PIX_FMT_YUV48_12``
      - 'Y312'

      - Y'\ :sub:`0`
      - Cb\ :sub:`0`
      - Cr\ :sub:`0`
      - Y'\ :sub:`1`
      - Cb\ :sub:`1`
      - Cr\ :sub:`1`

4:2:2 Subsampling
=================

These formats, commonly referred to as YUYV or YUY2, subsample the chroma
components horizontally by 2, storing 2 pixels in a container. The container
is 32-bits for 8-bit formats, and 64-bits for 10+-bit formats.

The packed YUYV formats with more than 8 bits per component are stored as four
16-bit little-endian words. Each word's most significant bits contain one
component, and the least significant bits are zero padding.

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{3.4cm}|p{1.2cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|

.. flat-table:: Packed YUV 4:2:2 Formats in 32-bit container
    :header-rows: 1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0
      - Byte 1
      - Byte 2
      - Byte 3
      - Byte 4
      - Byte 5
      - Byte 6
      - Byte 7
    * .. _V4L2-PIX-FMT-UYVY:

      - ``V4L2_PIX_FMT_UYVY``
      - 'UYVY'

      - Cb\ :sub:`0`
      - Y'\ :sub:`0`
      - Cr\ :sub:`0`
      - Y'\ :sub:`1`
      - Cb\ :sub:`2`
      - Y'\ :sub:`2`
      - Cr\ :sub:`2`
      - Y'\ :sub:`3`
    * .. _V4L2-PIX-FMT-VYUY:

      - ``V4L2_PIX_FMT_VYUY``
      - 'VYUY'

      - Cr\ :sub:`0`
      - Y'\ :sub:`0`
      - Cb\ :sub:`0`
      - Y'\ :sub:`1`
      - Cr\ :sub:`2`
      - Y'\ :sub:`2`
      - Cb\ :sub:`2`
      - Y'\ :sub:`3`
    * .. _V4L2-PIX-FMT-YUYV:

      - ``V4L2_PIX_FMT_YUYV``
      - 'YUYV'

      - Y'\ :sub:`0`
      - Cb\ :sub:`0`
      - Y'\ :sub:`1`
      - Cr\ :sub:`0`
      - Y'\ :sub:`2`
      - Cb\ :sub:`2`
      - Y'\ :sub:`3`
      - Cr\ :sub:`2`
    * .. _V4L2-PIX-FMT-YVYU:

      - ``V4L2_PIX_FMT_YVYU``
      - 'YVYU'

      - Y'\ :sub:`0`
      - Cr\ :sub:`0`
      - Y'\ :sub:`1`
      - Cb\ :sub:`0`
      - Y'\ :sub:`2`
      - Cr\ :sub:`2`
      - Y'\ :sub:`3`
      - Cb\ :sub:`2`

.. tabularcolumns:: |p{3.4cm}|p{1.2cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|p{0.8cm}|

.. flat-table:: Packed YUV 4:2:2 Formats in 64-bit container
    :header-rows: 1
    :stub-columns: 0

    * - Identifier
      - Code
      - Word 0
      - Word 1
      - Word 2
      - Word 3
    * .. _V4L2-PIX-FMT-Y210:

      - ``V4L2_PIX_FMT_Y210``
      - 'Y210'

      - Y'\ :sub:`0` (bits 15-6)
      - Cb\ :sub:`0` (bits 15-6)
      - Y'\ :sub:`1` (bits 15-6)
      - Cr\ :sub:`0` (bits 15-6)
    * .. _V4L2-PIX-FMT-Y212:

      - ``V4L2_PIX_FMT_Y212``
      - 'Y212'

      - Y'\ :sub:`0` (bits 15-4)
      - Cb\ :sub:`0` (bits 15-4)
      - Y'\ :sub:`1` (bits 15-4)
      - Cr\ :sub:`0` (bits 15-4)
    * .. _V4L2-PIX-FMT-Y216:

      - ``V4L2_PIX_FMT_Y216``
      - 'Y216'

      - Y'\ :sub:`0` (bits 15-0)
      - Cb\ :sub:`0` (bits 15-0)
      - Y'\ :sub:`1` (bits 15-0)
      - Cr\ :sub:`0` (bits 15-0)

.. raw:: latex

    \normalsize

**Color Sample Location:**
Chroma samples are :ref:`interstitially sited<yuv-chroma-centered>`
horizontally.


4:1:1 Subsampling
=================

This format subsamples the chroma components horizontally by 4, storing 8
pixels in 12 bytes.

.. raw:: latex

    \scriptsize

.. tabularcolumns:: |p{2.9cm}|p{0.8cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|

.. flat-table:: Packed YUV 4:1:1 Formats
    :header-rows: 1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0
      - Byte 1
      - Byte 2
      - Byte 3
      - Byte 4
      - Byte 5
      - Byte 6
      - Byte 7
      - Byte 8
      - Byte 9
      - Byte 10
      - Byte 11
    * .. _V4L2-PIX-FMT-Y41P:

      - ``V4L2_PIX_FMT_Y41P``
      - 'Y41P'

      - Cb\ :sub:`0`
      - Y'\ :sub:`0`
      - Cr\ :sub:`0`
      - Y'\ :sub:`1`
      - Cb\ :sub:`4`
      - Y'\ :sub:`2`
      - Cr\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`4`
      - Y'\ :sub:`5`
      - Y'\ :sub:`6`
      - Y'\ :sub:`7`

.. raw:: latex

    \normalsize

.. note::

    Do not confuse ``V4L2_PIX_FMT_Y41P`` with
    :ref:`V4L2_PIX_FMT_YUV411P <V4L2-PIX-FMT-YUV411P>`. Y41P is derived from
    "YUV 4:1:1 **packed**", while YUV411P stands for "YUV 4:1:1 **planar**".

**Color Sample Location:**
Chroma samples are :ref:`interstitially sited<yuv-chroma-centered>`
horizontally.
