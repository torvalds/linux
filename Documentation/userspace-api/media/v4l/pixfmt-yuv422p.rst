.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-PIX-FMT-YUV422P:

*****************************
V4L2_PIX_FMT_YUV422P ('422P')
*****************************


Format with ½ horizontal chroma resolution, also known as YUV 4:2:2.
Planar layout as opposed to ``V4L2_PIX_FMT_YUYV``


Description
===========

This format is not commonly used. This is a planar version of the YUYV
format. The three components are separated into three sub-images or
planes. The Y plane is first. The Y plane has one byte per pixel. The Cb
plane immediately follows the Y plane in memory. The Cb plane is half
the width of the Y plane (and of the image). Each Cb belongs to two
pixels. For example, Cb\ :sub:`0` belongs to Y'\ :sub:`00`,
Y'\ :sub:`01`. Following the Cb plane is the Cr plane, just like the Cb
plane.

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
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start + 18:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`
    * - start + 20:
      - Cb\ :sub:`20`
      - Cb\ :sub:`21`
    * - start + 22:
      - Cb\ :sub:`30`
      - Cb\ :sub:`31`
    * - start + 24:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start + 26:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`
    * - start + 28:
      - Cr\ :sub:`20`
      - Cr\ :sub:`21`
    * - start + 30:
      - Cr\ :sub:`30`
      - Cr\ :sub:`31`


**Color Sample Location:**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * -
      - 0
      -
      - 1
      - 2
      -
      - 3
    * - 0
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 1
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 2
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 3
      - Y
      - C
      - Y
      - Y
      - C
      - Y
