.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-YVU420:
.. _V4L2-PIX-FMT-YUV420:

**********************************************************
V4L2_PIX_FMT_YVU420 ('YV12'), V4L2_PIX_FMT_YUV420 ('YU12')
**********************************************************


V4L2_PIX_FMT_YUV420
Planar formats with ½ horizontal and vertical chroma resolution, also
known as YUV 4:2:0


Description
===========

These are planar formats, as opposed to a packed format. The three
components are separated into three sub- images or planes. The Y plane
is first. The Y plane has one byte per pixel. For
``V4L2_PIX_FMT_YVU420``, the Cr plane immediately follows the Y plane in
memory. The Cr plane is half the width and half the height of the Y
plane (and of the image). Each Cr belongs to four pixels, a two-by-two
square of the image. For example, Cr\ :sub:`0` belongs to Y'\ :sub:`00`,
Y'\ :sub:`01`, Y'\ :sub:`10`, and Y'\ :sub:`11`. Following the Cr plane
is the Cb plane, just like the Cr plane. ``V4L2_PIX_FMT_YUV420`` is the
same except the Cb plane comes first, then the Cr plane.

If the Y plane has pad bytes after each row, then the Cr and Cb planes
have half as many pad bytes after their rows. In other words, two Cx
rows (including padding) is exactly as long as one Y row (including
padding).

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start + 18:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`
    * - start + 20:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start + 22:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`


**Color Sample Location..**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * -
      - 0
      -
      - 1
      -
      - 2
      -
      - 3
    * - 0
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
      -
      - C
      -
      -
      -
      - C
      -
    * - 1
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
    * - 2
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
      -
      - C
      -
      -
      -
      - C
      -
    * - 3
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
