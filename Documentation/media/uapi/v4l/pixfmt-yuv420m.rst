.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-YUV420M:
.. _v4l2-pix-fmt-yvu420m:

************************************************************
V4L2_PIX_FMT_YUV420M ('YM12'), V4L2_PIX_FMT_YVU420M ('YM21')
************************************************************


V4L2_PIX_FMT_YVU420M
Variation of ``V4L2_PIX_FMT_YUV420`` and ``V4L2_PIX_FMT_YVU420`` with
planes non contiguous in memory.


Description
===========

This is a multi-planar format, as opposed to a packed format. The three
components are separated into three sub-images or planes.

The Y plane is first. The Y plane has one byte per pixel. For
``V4L2_PIX_FMT_YUV420M`` the Cb data constitutes the second plane which
is half the width and half the height of the Y plane (and of the image).
Each Cb belongs to four pixels, a two-by-two square of the image. For
example, Cb\ :sub:`0` belongs to Y'\ :sub:`00`, Y'\ :sub:`01`,
Y'\ :sub:`10`, and Y'\ :sub:`11`. The Cr data, just like the Cb plane,
is in the third plane.

``V4L2_PIX_FMT_YVU420M`` is the same except the Cr data is stored in the
second plane and the Cb data in the third plane.

If the Y plane has pad bytes after each row, then the Cb and Cr planes
have half as many pad bytes after their rows. In other words, two Cx
rows (including padding) is exactly as long as one Y row (including
padding).

``V4L2_PIX_FMT_YUV420M`` and ``V4L2_PIX_FMT_YVU420M`` are intended to be
used only in drivers and applications that support the multi-planar API,
described in :ref:`planar-apis`.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start0 + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start0 + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start0 + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * -
    * - start1 + 0:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start1 + 2:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`
    * -
    * - start2 + 0:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start2 + 2:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`


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
