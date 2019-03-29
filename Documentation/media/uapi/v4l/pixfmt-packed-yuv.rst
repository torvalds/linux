.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _packed-yuv:

******************
Packed YUV formats
******************

Description
===========

Similar to the packed RGB formats these formats store the Y, Cb and Cr
component of each pixel in one 16 or 32 bit word.


.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. _packed-yuv-formats:

.. tabularcolumns:: |p{2.0cm}|p{0.67cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|p{0.29cm}|

.. flat-table:: Packed YUV Image Formats
    :header-rows:  2
    :stub-columns: 0

    * - Identifier
      - Code

      - :cspan:`7` Byte 0 in memory

      - :cspan:`7` Byte 1

      - :cspan:`7` Byte 2

      - :cspan:`7` Byte 3

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

      -  :cspan:`15`

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

      -  :cspan:`15`
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

      -  :cspan:`15`

    * .. _V4L2-PIX-FMT-YUV32:

      - ``V4L2_PIX_FMT_YUV32``
      - 'YUV4'

      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`

      - Y'\ :sub:`7`
      - Y'\ :sub:`6`
      - Y'\ :sub:`5`
      - Y'\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`

      - Cb\ :sub:`7`
      - Cb\ :sub:`6`
      - Cb\ :sub:`5`
      - Cb\ :sub:`4`
      - Cb\ :sub:`3`
      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`

      - Cr\ :sub:`7`
      - Cr\ :sub:`6`
      - Cr\ :sub:`5`
      - Cr\ :sub:`4`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

    * .. _V4L2-PIX-FMT-AYUV32:

      - ``V4L2_PIX_FMT_AYUV32``
      - 'AYUV'

      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`

      - Y'\ :sub:`7`
      - Y'\ :sub:`6`
      - Y'\ :sub:`5`
      - Y'\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`

      - Cb\ :sub:`7`
      - Cb\ :sub:`6`
      - Cb\ :sub:`5`
      - Cb\ :sub:`4`
      - Cb\ :sub:`3`
      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`

      - Cr\ :sub:`7`
      - Cr\ :sub:`6`
      - Cr\ :sub:`5`
      - Cr\ :sub:`4`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

    * .. _V4L2-PIX-FMT-XYUV32:

      - ``V4L2_PIX_FMT_XYUV32``
      - 'XYUV'

      -
      -
      -
      -
      -
      -
      -
      -

      - Y'\ :sub:`7`
      - Y'\ :sub:`6`
      - Y'\ :sub:`5`
      - Y'\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`

      - Cb\ :sub:`7`
      - Cb\ :sub:`6`
      - Cb\ :sub:`5`
      - Cb\ :sub:`4`
      - Cb\ :sub:`3`
      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`

      - Cr\ :sub:`7`
      - Cr\ :sub:`6`
      - Cr\ :sub:`5`
      - Cr\ :sub:`4`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

    * .. _V4L2-PIX-FMT-VUYA32:

      - ``V4L2_PIX_FMT_VUYA32``
      - 'VUYA'

      - Cr\ :sub:`7`
      - Cr\ :sub:`6`
      - Cr\ :sub:`5`
      - Cr\ :sub:`4`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

      - Cb\ :sub:`7`
      - Cb\ :sub:`6`
      - Cb\ :sub:`5`
      - Cb\ :sub:`4`
      - Cb\ :sub:`3`
      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`

      - Y'\ :sub:`7`
      - Y'\ :sub:`6`
      - Y'\ :sub:`5`
      - Y'\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`

      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`

    * .. _V4L2-PIX-FMT-VUYX32:

      - ``V4L2_PIX_FMT_VUYX32``
      - 'VUYX'

      - Cr\ :sub:`7`
      - Cr\ :sub:`6`
      - Cr\ :sub:`5`
      - Cr\ :sub:`4`
      - Cr\ :sub:`3`
      - Cr\ :sub:`2`
      - Cr\ :sub:`1`
      - Cr\ :sub:`0`

      - Cb\ :sub:`7`
      - Cb\ :sub:`6`
      - Cb\ :sub:`5`
      - Cb\ :sub:`4`
      - Cb\ :sub:`3`
      - Cb\ :sub:`2`
      - Cb\ :sub:`1`
      - Cb\ :sub:`0`

      - Y'\ :sub:`7`
      - Y'\ :sub:`6`
      - Y'\ :sub:`5`
      - Y'\ :sub:`4`
      - Y'\ :sub:`3`
      - Y'\ :sub:`2`
      - Y'\ :sub:`1`
      - Y'\ :sub:`0`

      -
      -
      -
      -
      -
      -
      -
      -

.. raw:: latex

    \endgroup

.. note::

    #) Bit 7 is the most significant bit;

    #) The value of a = alpha bits is undefined when reading from the driver,
       ignored when writing to the driver, except when alpha blending has
       been negotiated for a :ref:`Video Overlay <overlay>` or
       :ref:`Video Output Overlay <osd>` for the formats Y444, YUV555 and
       YUV4. However, for formats AYUV32 and VUYA32, the alpha component is
       expected to contain a meaningful value that can be used by drivers
       and applications. And, the formats XYUV32 and VUYX32 contain undefined
       alpha values that must be ignored by all applications and drivers.
