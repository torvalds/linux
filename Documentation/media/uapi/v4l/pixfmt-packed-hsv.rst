.. -*- coding: utf-8; mode: rst -*-

.. _packed-hsv:

******************
Packed HSV formats
******************

Description
===========

The *hue* (h) is measured in degrees, the equivalence between degrees and LSBs
depends on the hsv-encoding used, see :ref:`colorspaces`.
The *saturation* (s) and the *value* (v) are measured in percentage of the
cylinder: 0 being the smallest value and 255 the maximum.


The values are packed in 24 or 32 bit formats.

.. raw:: latex

    \newline\begin{adjustbox}{width=\columnwidth}

.. tabularcolumns:: |p{4.2cm}|p{1.0cm}|p{0.7cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{1.7cm}|

.. _packed-hsv-formats:

.. flat-table:: Packed HSV Image Formats
    :header-rows:  2
    :stub-columns: 0

    * - Identifier
      - Code
      -
      - :cspan:`7` Byte 0 in memory
      -
      - :cspan:`7` Byte 1
      -
      - :cspan:`7` Byte 2
      -
      - :cspan:`7` Byte 3
    * -
      -
      - Bit
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
      -
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
      -
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
      -
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _V4L2-PIX-FMT-HSV32:

      - ``V4L2_PIX_FMT_HSV32``
      - 'HSV4'
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - h\ :sub:`7`
      - h\ :sub:`6`
      - h\ :sub:`5`
      - h\ :sub:`4`
      - h\ :sub:`3`
      - h\ :sub:`2`
      - h\ :sub:`1`
      - h\ :sub:`0`
      -
      - s\ :sub:`7`
      - s\ :sub:`6`
      - s\ :sub:`5`
      - s\ :sub:`4`
      - s\ :sub:`3`
      - s\ :sub:`2`
      - s\ :sub:`1`
      - s\ :sub:`0`
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _V4L2-PIX-FMT-HSV24:

      - ``V4L2_PIX_FMT_HSV24``
      - 'HSV3'
      -
      - h\ :sub:`7`
      - h\ :sub:`6`
      - h\ :sub:`5`
      - h\ :sub:`4`
      - h\ :sub:`3`
      - h\ :sub:`2`
      - h\ :sub:`1`
      - h\ :sub:`0`
      -
      - s\ :sub:`7`
      - s\ :sub:`6`
      - s\ :sub:`5`
      - s\ :sub:`4`
      - s\ :sub:`3`
      - s\ :sub:`2`
      - s\ :sub:`1`
      - s\ :sub:`0`
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      -
      -
.. raw:: latex

    \end{adjustbox}\newline\newline

Bit 7 is the most significant bit.
