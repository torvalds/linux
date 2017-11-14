.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-NV12:
.. _V4L2-PIX-FMT-NV21:

******************************************************
V4L2_PIX_FMT_NV12 ('NV12'), V4L2_PIX_FMT_NV21 ('NV21')
******************************************************


V4L2_PIX_FMT_NV21
Formats with ½ horizontal and vertical chroma resolution, also known as
YUV 4:2:0. One luminance and one chrominance plane with alternating
chroma samples as opposed to ``V4L2_PIX_FMT_YVU420``


Description
===========

These are two-plane versions of the YUV 4:2:0 format. The three
components are separated into two sub-images or planes. The Y plane is
first. The Y plane has one byte per pixel. For ``V4L2_PIX_FMT_NV12``, a
combined CbCr plane immediately follows the Y plane in memory. The CbCr
plane is the same width, in bytes, as the Y plane (and of the image),
but is half as tall in pixels. Each CbCr pair belongs to four pixels.
For example, Cb\ :sub:`0`/Cr\ :sub:`0` belongs to Y'\ :sub:`00`,
Y'\ :sub:`01`, Y'\ :sub:`10`, Y'\ :sub:`11`. ``V4L2_PIX_FMT_NV21`` is
the same except the Cb and Cr bytes are swapped, the CrCb plane starts
with a Cr byte.

If the Y plane has pad bytes after each row, then the CbCr plane has as
many pad bytes after its rows.

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
      - Cr\ :sub:`00`
      - Cb\ :sub:`01`
      - Cr\ :sub:`01`
    * - start + 20:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`11`
      - Cr\ :sub:`11`


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
      -
      - Y
      - Y
      -
      - Y
    * -
      -
      - C
      -
      -
      - C
      -
    * - 1
      - Y
      -
      - Y
      - Y
      -
      - Y
    * -
    * - 2
      - Y
      -
      - Y
      - Y
      -
      - Y
    * -
      -
      - C
      -
      -
      - C
      -
    * - 3
      - Y
      -
      - Y
      - Y
      -
      - Y
