.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-YUV411P:

*****************************
V4L2_PIX_FMT_YUV411P ('411P')
*****************************

*man V4L2_PIX_FMT_YUV411P(2)*

Format with ¼ horizontal chroma resolution, also known as YUV 4:1:1.
Planar layout as opposed to ``V4L2_PIX_FMT_Y41P``


Description
===========

This format is not commonly used. This is a planar format similar to the
4:2:2 planar format except with half as many chroma. The three
components are separated into three sub-images or planes. The Y plane is
first. The Y plane has one byte per pixel. The Cb plane immediately
follows the Y plane in memory. The Cb plane is ¼ the width of the Y
plane (and of the image). Each Cb belongs to 4 pixels all on the same
row. For example, Cb\ :sub:`0` belongs to Y'\ :sub:`00`, Y'\ :sub:`01`,
Y'\ :sub:`02` and Y'\ :sub:`03`. Following the Cb plane is the Cr plane,
just like the Cb plane.

If the Y plane has pad bytes after each row, then the Cr and Cb planes
have ¼ as many pad bytes after their rows. In other words, four C x rows
(including padding) is exactly as long as one Y row (including padding).

**Byte Order.**
Each cell is one byte.


.. tabularcolumns:: |p{5.8cm}|p{2.9cm}|p{2.9cm}|p{2.9cm}|p{3.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  Y'\ :sub:`00`

       -  Y'\ :sub:`01`

       -  Y'\ :sub:`02`

       -  Y'\ :sub:`03`

    -  .. row 2

       -  start + 4:

       -  Y'\ :sub:`10`

       -  Y'\ :sub:`11`

       -  Y'\ :sub:`12`

       -  Y'\ :sub:`13`

    -  .. row 3

       -  start + 8:

       -  Y'\ :sub:`20`

       -  Y'\ :sub:`21`

       -  Y'\ :sub:`22`

       -  Y'\ :sub:`23`

    -  .. row 4

       -  start + 12:

       -  Y'\ :sub:`30`

       -  Y'\ :sub:`31`

       -  Y'\ :sub:`32`

       -  Y'\ :sub:`33`

    -  .. row 5

       -  start + 16:

       -  Cb\ :sub:`00`

    -  .. row 6

       -  start + 17:

       -  Cb\ :sub:`10`

    -  .. row 7

       -  start + 18:

       -  Cb\ :sub:`20`

    -  .. row 8

       -  start + 19:

       -  Cb\ :sub:`30`

    -  .. row 9

       -  start + 20:

       -  Cr\ :sub:`00`

    -  .. row 10

       -  start + 21:

       -  Cr\ :sub:`10`

    -  .. row 11

       -  start + 22:

       -  Cr\ :sub:`20`

    -  .. row 12

       -  start + 23:

       -  Cr\ :sub:`30`


**Color Sample Location..**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -
       -  0

       -  1

       -
       -  2

       -  3

    -  .. row 2

       -  0

       -  Y

       -  Y

       -  C

       -  Y

       -  Y

    -  .. row 3

       -  1

       -  Y

       -  Y

       -  C

       -  Y

       -  Y

    -  .. row 4

       -  2

       -  Y

       -  Y

       -  C

       -  Y

       -  Y

    -  .. row 5

       -  3

       -  Y

       -  Y

       -  C

       -  Y

       -  Y
