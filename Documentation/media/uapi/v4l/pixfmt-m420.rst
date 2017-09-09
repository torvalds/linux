.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-M420:

**************************
V4L2_PIX_FMT_M420 ('M420')
**************************

Format with ½ horizontal and vertical chroma resolution, also known as
YUV 4:2:0. Hybrid plane line-interleaved layout.


Description
===========

M420 is a YUV format with ½ horizontal and vertical chroma subsampling
(YUV 4:2:0). Pixels are organized as interleaved luma and chroma planes.
Two lines of luma data are followed by one line of chroma data.

The luma plane has one byte per pixel. The chroma plane contains
interleaved CbCr pixels subsampled by ½ in the horizontal and vertical
directions. Each CbCr pair belongs to four pixels. For example,
Cb\ :sub:`0`/Cr\ :sub:`0` belongs to Y'\ :sub:`00`, Y'\ :sub:`01`,
Y'\ :sub:`10`, Y'\ :sub:`11`.

All line lengths are identical: if the Y lines include pad bytes so do
the CbCr lines.

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
      - Cb\ :sub:`00`
      - Cr\ :sub:`00`
      - Cb\ :sub:`01`
      - Cr\ :sub:`01`
    * - start + 16:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 20:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 24:
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
