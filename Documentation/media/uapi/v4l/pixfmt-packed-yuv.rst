.. -*- coding: utf-8; mode: rst -*-

.. _packed-yuv:

******************
Packed YUV formats
******************

Description
===========

Similar to the packed RGB formats these formats store the Y, Cb and Cr
component of each pixel in one 16 or 32 bit word.

.. raw:: latex

    \newline\newline\begin{adjustbox}{width=\columnwidth}

.. _rgb-formats:

.. tabularcolumns:: |p{4.5cm}|p{3.3cm}|p{0.7cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{1.7cm}|

.. flat-table:: Packed YUV Image Formats
    :header-rows:  2
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Code

       -
       -  :cspan:`7` Byte 0 in memory

       -
       -  :cspan:`7` Byte 1

       -
       -  :cspan:`7` Byte 2

       -
       -  :cspan:`7` Byte 3

    -  .. row 2

       -
       -
       -  Bit

       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

    -  .. _V4L2-PIX-FMT-YUV444:

       -  ``V4L2_PIX_FMT_YUV444``

       -  'Y444'

       -
       -  Cb\ :sub:`3`

       -  Cb\ :sub:`2`

       -  Cb\ :sub:`1`

       -  Cb\ :sub:`0`

       -  Cr\ :sub:`3`

       -  Cr\ :sub:`2`

       -  Cr\ :sub:`1`

       -  Cr\ :sub:`0`

       -
       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -  Y'\ :sub:`3`

       -  Y'\ :sub:`2`

       -  Y'\ :sub:`1`

       -  Y'\ :sub:`0`

    -  .. _V4L2-PIX-FMT-YUV555:

       -  ``V4L2_PIX_FMT_YUV555``

       -  'YUVO'

       -
       -  Cb\ :sub:`2`

       -  Cb\ :sub:`1`

       -  Cb\ :sub:`0`

       -  Cr\ :sub:`4`

       -  Cr\ :sub:`3`

       -  Cr\ :sub:`2`

       -  Cr\ :sub:`1`

       -  Cr\ :sub:`0`

       -
       -  a

       -  Y'\ :sub:`4`

       -  Y'\ :sub:`3`

       -  Y'\ :sub:`2`

       -  Y'\ :sub:`1`

       -  Y'\ :sub:`0`

       -  Cb\ :sub:`4`

       -  Cb\ :sub:`3`

    -  .. _V4L2-PIX-FMT-YUV565:

       -  ``V4L2_PIX_FMT_YUV565``

       -  'YUVP'

       -
       -  Cb\ :sub:`2`

       -  Cb\ :sub:`1`

       -  Cb\ :sub:`0`

       -  Cr\ :sub:`4`

       -  Cr\ :sub:`3`

       -  Cr\ :sub:`2`

       -  Cr\ :sub:`1`

       -  Cr\ :sub:`0`

       -
       -  Y'\ :sub:`4`

       -  Y'\ :sub:`3`

       -  Y'\ :sub:`2`

       -  Y'\ :sub:`1`

       -  Y'\ :sub:`0`

       -  Cb\ :sub:`5`

       -  Cb\ :sub:`4`

       -  Cb\ :sub:`3`

    -  .. _V4L2-PIX-FMT-YUV32:

       -  ``V4L2_PIX_FMT_YUV32``

       -  'YUV4'

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -
       -  Y'\ :sub:`7`

       -  Y'\ :sub:`6`

       -  Y'\ :sub:`5`

       -  Y'\ :sub:`4`

       -  Y'\ :sub:`3`

       -  Y'\ :sub:`2`

       -  Y'\ :sub:`1`

       -  Y'\ :sub:`0`

       -
       -  Cb\ :sub:`7`

       -  Cb\ :sub:`6`

       -  Cb\ :sub:`5`

       -  Cb\ :sub:`4`

       -  Cb\ :sub:`3`

       -  Cb\ :sub:`2`

       -  Cb\ :sub:`1`

       -  Cb\ :sub:`0`

       -
       -  Cr\ :sub:`7`

       -  Cr\ :sub:`6`

       -  Cr\ :sub:`5`

       -  Cr\ :sub:`4`

       -  Cr\ :sub:`3`

       -  Cr\ :sub:`2`

       -  Cr\ :sub:`1`

       -  Cr\ :sub:`0`

.. raw:: latex

    \end{adjustbox}\newline\newline

.. note::

    #) Bit 7 is the most significant bit;

    #) The value of a = alpha bits is undefined when reading from the driver,
       ignored when writing to the driver, except when alpha blending has
       been negotiated for a :ref:`Video Overlay <overlay>` or
       :ref:`Video Output Overlay <osd>`.
